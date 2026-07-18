/*-----------------------------------------------------------------------------
 *   Copyright 2019 Fei Li
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 *
 *-----------------------------------------------------------------------------
 */
#include "ent_script.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef WIN32
#include <windows.h>
#endif

#ifndef WIN32
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#if ENT_ENABLE_LUA
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#define ENT_SCRIPT_MEMORY_LIMIT_BYTES ((size_t)16u * 1024u * 1024u)
#define ENT_SCRIPT_INSTRUCTION_LIMIT   1000000
#define ENT_SCRIPT_HOOK_INTERVAL       1000
#endif

#ifndef ENT_SCRIPT_PATH_MAX
#ifdef WIN32
#define ENT_SCRIPT_PATH_MAX 260
#else
#define ENT_SCRIPT_PATH_MAX 1024
#endif
#endif

#ifdef WIN32
static SRWLOCK sScriptLock = SRWLOCK_INIT;
static void iENT_ScriptLock(void)
{
    AcquireSRWLockExclusive(&sScriptLock);
}

static void iENT_ScriptUnlock(void)
{
    ReleaseSRWLockExclusive(&sScriptLock);
}
#else
static pthread_mutex_t sScriptLock = PTHREAD_MUTEX_INITIALIZER;
static void iENT_ScriptLock(void)
{
    pthread_mutex_lock(&sScriptLock);
}

static void iENT_ScriptUnlock(void)
{
    pthread_mutex_unlock(&sScriptLock);
}
#endif

typedef struct
{
    bool isInit;
    char scriptRoot[ENT_SCRIPT_PATH_MAX];
#if ENT_ENABLE_LUA
    lua_State* state;
    size_t luaMemoryUsed;
    int luaInstructionsRemaining;
#endif
} ENT_SCRIPT_CTX_T;

static ENT_SCRIPT_CTX_T gEntScriptCtx = {false, {0}};

static bool iENT_ScriptIsBlank(const char* text)
{
    return (text == NULL || text[0] == '\0');
}

static ENT_SCRIPT_ARG_TYPE_E iENT_ScriptResolveArgType(int rawType)
{
    if(rawType < ENT_SCRIPT_ARG_STRING_E || rawType > ENT_SCRIPT_ARG_NULL_E)
    {
        return ENT_SCRIPT_ARG_STRING_E;
    }

    return (ENT_SCRIPT_ARG_TYPE_E)rawType;
}

static MSG_ID_T iENT_ScriptCopyPath(char* out,
                                    size_t outSize,
                                    const char* path)
{
    int n = 0;

    n = snprintf(out, outSize, "%s", path);
    if(n < 0 || (size_t)n >= outSize)
    {
        return ENT_SCR_LOAD_FAILED;
    }

    return ENT_SYS_NORMAL;
}

#if ENT_ENABLE_LUA
static bool iENT_ScriptNameIsSafe(const char* name)
{
    const char* componentStart;
    const char* cursor;

    if(name == NULL || name[0] == '\0')
    {
        return false;
    }

    if(name[0] == '/' || name[0] == '\\')
    {
        return false;
    }

#ifdef WIN32
    if((name[0] >= 'A' && name[0] <= 'Z') ||
       (name[0] >= 'a' && name[0] <= 'z'))
    {
        if(name[1] == ':')
        {
            return false;
        }
    }
#endif

    componentStart = name;
    for(cursor = name;; cursor++)
    {
        if(*cursor == '/' || *cursor == '\\' || *cursor == '\0')
        {
            size_t componentLength = (size_t)(cursor - componentStart);

            if(componentLength == 2 &&
               componentStart[0] == '.' && componentStart[1] == '.')
            {
                return false;
            }

            if(componentLength == 1 && componentStart[0] == '.')
            {
                return false;
            }

            if(*cursor == '\0')
            {
                break;
            }
            componentStart = cursor + 1;
        }
    }

    return true;
}

static MSG_ID_T iENT_ScriptJoinPath(char* out,
                                    size_t outSize,
                                    const char* base,
                                    const char* name)
{
    int n = 0;
    char sep = ENT_FILE_SEP_C;

    if(base == NULL || name == NULL)
    {
        return ENT_SCR_BAD_ARGUMENT;
    }

    if(!iENT_ScriptNameIsSafe(name))
    {
        return ENT_SCR_BAD_ARGUMENT;
    }

    if(base[0] == '\0')
    {
        return ENT_SCR_BAD_ARGUMENT;
    }

    if(base[strlen(base) - 1] == sep)
    {
        n = snprintf(out, outSize, "%s%s", base, name);
    }
    else
    {
        n = snprintf(out, outSize, "%s%c%s", base, sep, name);
    }

    if(n < 0 || (size_t)n >= outSize)
    {
        return ENT_SCR_LOAD_FAILED;
    }

    return ENT_SYS_NORMAL;
}

static bool iENT_ScriptPathHasRootPrefix(const char* root, const char* path)
{
    size_t rootLength;

    if(root == NULL || path == NULL)
    {
        return false;
    }
    rootLength = strlen(root);
    if(strlen(path) < rootLength)
    {
        return false;
    }
#ifdef WIN32
    if(_strnicmp(root, path, rootLength) != 0)
#else
    if(strncmp(root, path, rootLength) != 0)
#endif
    {
        return false;
    }

#ifdef WIN32
    return path[rootLength] == '\0' || path[rootLength] == '/' || path[rootLength] == '\\';
#else
    return path[rootLength] == '\0' || path[rootLength] == '/';
#endif
}

static MSG_ID_T iENT_ScriptResolvePath(char* out,
                                       size_t outSize,
                                       const char* base,
                                       const char* name)
{
    char candidate[ENT_SCRIPT_PATH_MAX * 4] = {0};

    if(out == NULL || outSize == 0)
    {
        return ENT_SCR_BAD_ARGUMENT;
    }
    if(iENT_ScriptJoinPath(candidate, sizeof(candidate), base, name) != ENT_SYS_NORMAL)
    {
        return ENT_SCR_BAD_ARGUMENT;
    }

#ifdef WIN32
    {
        char fullRoot[ENT_SCRIPT_PATH_MAX * 4] = {0};
        char rootFinal[ENT_SCRIPT_PATH_MAX * 4] = {0};
        char candidateFinal[ENT_SCRIPT_PATH_MAX * 4] = {0};
        DWORD fullRootLength;
        DWORD rootFinalLength;
        DWORD candidateFinalLength;
        HANDLE rootHandle = INVALID_HANDLE_VALUE;
        HANDLE candidateHandle = INVALID_HANDLE_VALUE;

        fullRootLength = GetFullPathNameA(base, (DWORD)sizeof(fullRoot), fullRoot, NULL);
        if(fullRootLength == 0 || fullRootLength >= sizeof(fullRoot))
        {
            return ENT_SCR_LOAD_FAILED;
        }
        rootHandle = CreateFileA(fullRoot,
                                 0,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 NULL,
                                 OPEN_EXISTING,
                                 FILE_FLAG_BACKUP_SEMANTICS,
                                 NULL);
        candidateHandle = CreateFileA(candidate,
                                      GENERIC_READ,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      NULL,
                                      OPEN_EXISTING,
                                      FILE_ATTRIBUTE_NORMAL,
                                      NULL);
        if(rootHandle == INVALID_HANDLE_VALUE || candidateHandle == INVALID_HANDLE_VALUE)
        {
            if(rootHandle != INVALID_HANDLE_VALUE)
            {
                CloseHandle(rootHandle);
            }
            if(candidateHandle != INVALID_HANDLE_VALUE)
            {
                CloseHandle(candidateHandle);
            }
            return ENT_SCR_LOAD_FAILED;
        }

        rootFinalLength = GetFinalPathNameByHandleA(rootHandle,
                                                     rootFinal,
                                                     (DWORD)sizeof(rootFinal),
                                                     FILE_NAME_NORMALIZED);
        candidateFinalLength = GetFinalPathNameByHandleA(candidateHandle,
                                                          candidateFinal,
                                                          (DWORD)sizeof(candidateFinal),
                                                          FILE_NAME_NORMALIZED);
        CloseHandle(rootHandle);
        CloseHandle(candidateHandle);
        if(rootFinalLength == 0 || candidateFinalLength == 0 ||
           rootFinalLength >= sizeof(rootFinal) || candidateFinalLength >= sizeof(candidateFinal) ||
           !iENT_ScriptPathHasRootPrefix(rootFinal, candidateFinal))
        {
            return ENT_SCR_BAD_ARGUMENT;
        }
    }

    if(strlen(candidate) >= outSize)
    {
        return ENT_SCR_LOAD_FAILED;
    }
    snprintf(out, outSize, "%s", candidate);
    return ENT_SYS_NORMAL;
#else
    {
        char rootResolved[PATH_MAX] = {0};
        char candidateResolved[PATH_MAX] = {0};

        if(realpath(base, rootResolved) == NULL || realpath(candidate, candidateResolved) == NULL)
        {
            return ENT_SCR_LOAD_FAILED;
        }
        if(!iENT_ScriptPathHasRootPrefix(rootResolved, candidateResolved))
        {
            return ENT_SCR_BAD_ARGUMENT;
        }
        if(strlen(candidateResolved) >= outSize)
        {
            return ENT_SCR_LOAD_FAILED;
        }
        snprintf(out, outSize, "%s", candidateResolved);
    }
    return ENT_SYS_NORMAL;
#endif
}

static MSG_ID_T iENT_ScriptLoadChunk(lua_State* state,
                                     const char* root,
                                     const char* name,
                                     const char* displayPath,
                                     int* luaStatus)
{
    if(state == NULL || root == NULL || name == NULL || displayPath == NULL || luaStatus == NULL)
    {
        return ENT_SCR_BAD_ARGUMENT;
    }

#ifdef WIN32
    {
        char candidate[ENT_SCRIPT_PATH_MAX * 4] = {0};
        char fullRoot[ENT_SCRIPT_PATH_MAX * 4] = {0};
        char rootFinal[ENT_SCRIPT_PATH_MAX * 4] = {0};
        char candidateFinal[ENT_SCRIPT_PATH_MAX * 4] = {0};
        DWORD fullRootLength;
        DWORD rootFinalLength;
        DWORD candidateFinalLength;
        HANDLE rootHandle = INVALID_HANDLE_VALUE;
        HANDLE fileHandle = INVALID_HANDLE_VALUE;
        LARGE_INTEGER fileSize;
        char* buffer = NULL;
        DWORD bytesRead = 0;

        if(iENT_ScriptJoinPath(candidate, sizeof(candidate), root, name) != ENT_SYS_NORMAL)
        {
            return ENT_SCR_BAD_ARGUMENT;
        }
        fullRootLength = GetFullPathNameA(root, (DWORD)sizeof(fullRoot), fullRoot, NULL);
        if(fullRootLength == 0 || fullRootLength >= sizeof(fullRoot))
        {
            return ENT_SCR_LOAD_FAILED;
        }
        rootHandle = CreateFileA(fullRoot, 0,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
        fileHandle = CreateFileA(candidate, GENERIC_READ,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if(rootHandle == INVALID_HANDLE_VALUE || fileHandle == INVALID_HANDLE_VALUE)
        {
            if(rootHandle != INVALID_HANDLE_VALUE) CloseHandle(rootHandle);
            if(fileHandle != INVALID_HANDLE_VALUE) CloseHandle(fileHandle);
            return ENT_SCR_LOAD_FAILED;
        }
        rootFinalLength = GetFinalPathNameByHandleA(rootHandle, rootFinal, (DWORD)sizeof(rootFinal), FILE_NAME_NORMALIZED);
        candidateFinalLength = GetFinalPathNameByHandleA(fileHandle, candidateFinal, (DWORD)sizeof(candidateFinal), FILE_NAME_NORMALIZED);
        if(rootFinalLength == 0 || candidateFinalLength == 0 ||
           rootFinalLength >= sizeof(rootFinal) || candidateFinalLength >= sizeof(candidateFinal) ||
           !iENT_ScriptPathHasRootPrefix(rootFinal, candidateFinal) ||
           !GetFileSizeEx(fileHandle, &fileSize) || fileSize.QuadPart < 0 ||
           (unsigned long long)fileSize.QuadPart > (unsigned long long)((size_t)-1 - 1) ||
           (unsigned long long)fileSize.QuadPart > 0xFFFFFFFFull)
        {
            CloseHandle(rootHandle);
            CloseHandle(fileHandle);
            return ENT_SCR_BAD_ARGUMENT;
        }
        buffer = (char*)malloc((size_t)fileSize.QuadPart + 1);
        if(buffer == NULL)
        {
            CloseHandle(rootHandle);
            CloseHandle(fileHandle);
            return ENT_SCR_LOAD_FAILED;
        }
        if((size_t)fileSize.QuadPart > 0 &&
           (!ReadFile(fileHandle, buffer, (DWORD)fileSize.QuadPart, &bytesRead, NULL) ||
            bytesRead != (DWORD)fileSize.QuadPart))
        {
            free(buffer);
            CloseHandle(rootHandle);
            CloseHandle(fileHandle);
            return ENT_SCR_LOAD_FAILED;
        }
        CloseHandle(rootHandle);
        CloseHandle(fileHandle);
        buffer[(size_t)fileSize.QuadPart] = '\0';
        *luaStatus = luaL_loadbuffer(state, buffer, (size_t)fileSize.QuadPart, displayPath);
        free(buffer);
        return ENT_SYS_NORMAL;
    }
#else
    {
        char relative[ENT_SCRIPT_PATH_MAX * 4] = {0};
        char* component;
        char* next;
        int dirFd;
        int fileFd = -1;
        struct stat fileStat;
        char* buffer = NULL;
        size_t totalRead = 0;

        if(!iENT_ScriptNameIsSafe(name) || strlen(name) >= sizeof(relative))
        {
            return ENT_SCR_BAD_ARGUMENT;
        }
        snprintf(relative, sizeof(relative), "%s", name);
        dirFd = open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if(dirFd < 0)
        {
            return ENT_SCR_LOAD_FAILED;
        }
        component = relative;
        for(;;)
        {
            next = strchr(component, '/');
            if(next != NULL)
            {
                int childFd;
                *next = '\0';
                if(component[0] == '\0')
                {
                    close(dirFd);
                    return ENT_SCR_BAD_ARGUMENT;
                }
                childFd = openat(dirFd, component, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
                close(dirFd);
                if(childFd < 0)
                {
                    return ENT_SCR_BAD_ARGUMENT;
                }
                dirFd = childFd;
                component = next + 1;
                continue;
            }
            break;
        }
        if(component[0] == '\0')
        {
            close(dirFd);
            return ENT_SCR_BAD_ARGUMENT;
        }
        fileFd = openat(dirFd, component, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
        close(dirFd);
        if(fileFd < 0 || fstat(fileFd, &fileStat) != 0 || !S_ISREG(fileStat.st_mode) || fileStat.st_size < 0)
        {
            if(fileFd >= 0) close(fileFd);
            return ENT_SCR_BAD_ARGUMENT;
        }
        if((unsigned long long)fileStat.st_size > (unsigned long long)((size_t)-1 - 1))
        {
            close(fileFd);
            return ENT_SCR_LOAD_FAILED;
        }
        buffer = (char*)malloc((size_t)fileStat.st_size + 1);
        if(buffer == NULL)
        {
            close(fileFd);
            return ENT_SCR_LOAD_FAILED;
        }
        while(totalRead < (size_t)fileStat.st_size)
        {
            ssize_t count = read(fileFd, buffer + totalRead, (size_t)fileStat.st_size - totalRead);
            if(count <= 0)
            {
                free(buffer);
                close(fileFd);
                return ENT_SCR_LOAD_FAILED;
            }
            totalRead += (size_t)count;
        }
        close(fileFd);
        buffer[totalRead] = '\0';
        *luaStatus = luaL_loadbuffer(state, buffer, totalRead, displayPath);
        free(buffer);
        return ENT_SYS_NORMAL;
    }
#endif
}

static void* iENT_ScriptLuaAllocate(void* userData,
                                    void* pointer,
                                    size_t oldSize,
                                    size_t newSize)
{
    ENT_SCRIPT_CTX_T* context = (ENT_SCRIPT_CTX_T*)userData;
    void* resized;

    if(newSize == 0)
    {
        free(pointer);
        if(pointer != NULL && oldSize <= context->luaMemoryUsed)
        {
            context->luaMemoryUsed -= oldSize;
        }
        return NULL;
    }

    if(pointer == NULL)
    {
        oldSize = 0;
    }
    if(newSize > oldSize &&
       newSize - oldSize > ENT_SCRIPT_MEMORY_LIMIT_BYTES - context->luaMemoryUsed)
    {
        return NULL;
    }

    resized = realloc(pointer, newSize);
    if(resized == NULL)
    {
        return NULL;
    }
    context->luaMemoryUsed = context->luaMemoryUsed - oldSize + newSize;
    return resized;
}

static void iENT_ScriptInstructionHook(lua_State* state, lua_Debug* debug)
{
    (void)debug;
    gEntScriptCtx.luaInstructionsRemaining -= ENT_SCRIPT_HOOK_INTERVAL;
    if(gEntScriptCtx.luaInstructionsRemaining <= 0)
    {
        luaL_error(state, "script instruction limit exceeded");
    }
}

static void iENT_ScriptBeginExecution(lua_State* state)
{
    gEntScriptCtx.luaInstructionsRemaining = ENT_SCRIPT_INSTRUCTION_LIMIT;
    lua_sethook(state, iENT_ScriptInstructionHook, LUA_MASKCOUNT, ENT_SCRIPT_HOOK_INTERVAL);
}

static void iENT_ScriptEndExecution(lua_State* state)
{
    lua_sethook(state, NULL, 0, 0);
    gEntScriptCtx.luaInstructionsRemaining = 0;
}

static void iENT_ScriptOpenAllowedLibraries(lua_State* state)
{
    static const luaL_Reg allowedLibraries[] = {
        {LUA_GNAME, luaopen_base},
        {LUA_COLIBNAME, luaopen_coroutine},
        {LUA_TABLIBNAME, luaopen_table},
        {LUA_STRLIBNAME, luaopen_string},
        {LUA_MATHLIBNAME, luaopen_math},
        {LUA_UTF8LIBNAME, luaopen_utf8},
        {NULL, NULL}
    };
    const luaL_Reg* library;

    for(library = allowedLibraries; library->func != NULL; ++library)
    {
        luaL_requiref(state, library->name, library->func, 1);
        lua_pop(state, 1);
    }

    lua_pushnil(state);
    lua_setglobal(state, "dofile");
    lua_pushnil(state);
    lua_setglobal(state, "loadfile");
    lua_pushnil(state);
    lua_setglobal(state, "load");
}
#endif

ENT_PUBLIC MSG_ID_T ENT_ScriptInit(const char* scriptRoot)
{
    MSG_ID_T sts = ENT_SYS_NORMAL;

    iENT_ScriptLock();
    if(gEntScriptCtx.isInit == true)
    {
        iENT_ScriptUnlock();
        return ENT_SCR_ALREADY_INIT;
    }

    if(iENT_ScriptIsBlank(scriptRoot))
    {
        iENT_ScriptUnlock();
        return ENT_SCR_BAD_ARGUMENT;
    }

    if(iENT_ScriptCopyPath(gEntScriptCtx.scriptRoot,
                           sizeof(gEntScriptCtx.scriptRoot),
                           scriptRoot) != ENT_SYS_NORMAL)
    {
        iENT_ScriptUnlock();
        return ENT_SCR_LOAD_FAILED;
    }

#if ENT_ENABLE_LUA
    gEntScriptCtx.luaMemoryUsed = 0;
    gEntScriptCtx.state = lua_newstate(iENT_ScriptLuaAllocate, &gEntScriptCtx);
    if(gEntScriptCtx.state == NULL)
    {
        memset(gEntScriptCtx.scriptRoot, 0, sizeof(gEntScriptCtx.scriptRoot));
        iENT_ScriptUnlock();
        return ENT_SCR_LOAD_FAILED;
    }

    iENT_ScriptOpenAllowedLibraries(gEntScriptCtx.state);
#endif

    gEntScriptCtx.isInit = true;
    sts = ENT_SYS_NORMAL;
    iENT_ScriptUnlock();
    return sts;
}

ENT_PUBLIC MSG_ID_T ENT_ScriptReload(const char* scriptName)
{
#if ENT_ENABLE_LUA
    char scriptPath[ENT_SCRIPT_PATH_MAX] = {0};
    MSG_ID_T sts = ENT_SYS_NORMAL;
    int rc = 0;
#endif
    MSG_ID_T ret = ENT_SYS_NORMAL;

    iENT_ScriptLock();

    if(gEntScriptCtx.isInit == false)
    {
        iENT_ScriptUnlock();
        return ENT_SCR_NOT_INITIALIZED;
    }

    if(iENT_ScriptIsBlank(scriptName))
    {
        iENT_ScriptUnlock();
        return ENT_SCR_BAD_ARGUMENT;
    }

#if ENT_ENABLE_LUA
    sts = iENT_ScriptResolvePath(scriptPath,
                                 sizeof(scriptPath),
                                 gEntScriptCtx.scriptRoot,
                                 scriptName);
    if(sts != ENT_SYS_NORMAL)
    {
        ret = sts;
        goto END_OF_ROUTINE;
    }

    sts = iENT_ScriptLoadChunk(gEntScriptCtx.state,
                               gEntScriptCtx.scriptRoot,
                               scriptName,
                               scriptPath,
                               &rc);
    if(sts != ENT_SYS_NORMAL)
    {
        ret = sts;
        goto END_OF_ROUTINE;
    }
    if(rc != LUA_OK)
    {
        fprintf(stderr, "ENT_ScriptReload compile failed,file[%s],reason[%s]\n",
                scriptPath, lua_tostring(gEntScriptCtx.state, -1));
        lua_pop(gEntScriptCtx.state, 1);
        ret = ENT_SCR_COMPILE_FAILED;
        goto END_OF_ROUTINE;
    }

    iENT_ScriptBeginExecution(gEntScriptCtx.state);
    rc = lua_pcall(gEntScriptCtx.state, 0, 0, 0);
    iENT_ScriptEndExecution(gEntScriptCtx.state);
    if(rc != LUA_OK)
    {
        fprintf(stderr, "ENT_ScriptReload runtime failed,file[%s],reason[%s]\n",
                scriptPath, lua_tostring(gEntScriptCtx.state, -1));
        lua_pop(gEntScriptCtx.state, 1);
        ret = ENT_SCR_RUNTIME_FAILED;
        goto END_OF_ROUTINE;
    }

    ret = ENT_SYS_NORMAL;
END_OF_ROUTINE:
    iENT_ScriptUnlock();
    return ret;
#endif

    ret = ENT_SCR_UNSUPPORTED;
    iENT_ScriptUnlock();
    return ret;
}

ENT_PUBLIC MSG_ID_T ENT_ScriptCall(const char* fn,
                                   const ENT_SCRIPT_ARG_T* in,
                                   ENT_SCRIPT_RET_T* out)
{
    MSG_ID_T ret = ENT_SYS_NORMAL;

    iENT_ScriptLock();
    if(gEntScriptCtx.isInit == false)
    {
        iENT_ScriptUnlock();
        return ENT_SCR_NOT_INITIALIZED;
    }

    if(iENT_ScriptIsBlank(fn) || out == NULL)
    {
        iENT_ScriptUnlock();
        return ENT_SCR_BAD_ARGUMENT;
    }

#if ENT_ENABLE_LUA
    {
        size_t i = 0;
        int rc = 0;

        lua_getglobal(gEntScriptCtx.state, fn);
        if(!lua_isfunction(gEntScriptCtx.state, -1))
        {
            lua_pop(gEntScriptCtx.state, 1);
            ret = ENT_SCR_FUNC_NOTFOUND;
            goto END_OF_ROUTINE;
        }

        lua_newtable(gEntScriptCtx.state);
        if(in != NULL && in->items != NULL)
        {
            for(i = 0; i < in->count; ++i)
            {
                ENT_SCRIPT_ARG_TYPE_E argType = ENT_SCRIPT_ARG_STRING_E;

                if(in->items[i].key == NULL)
                {
                    continue;
                }

                argType = iENT_ScriptResolveArgType(in->items[i].type);
                switch(argType)
                {
                    case ENT_SCRIPT_ARG_STRING_E:
                        lua_pushstring(gEntScriptCtx.state,
                                       (in->items[i].value == NULL) ? "" : in->items[i].value);
                        break;

                    case ENT_SCRIPT_ARG_INT_E:
                        lua_pushinteger(gEntScriptCtx.state, (lua_Integer)in->items[i].intValue);
                        break;

                    case ENT_SCRIPT_ARG_DOUBLE_E:
                        lua_pushnumber(gEntScriptCtx.state, (lua_Number)in->items[i].numValue);
                        break;

                    case ENT_SCRIPT_ARG_BOOL_E:
                        lua_pushboolean(gEntScriptCtx.state, in->items[i].boolValue ? 1 : 0);
                        break;

                    case ENT_SCRIPT_ARG_NULL_E:
                        lua_pushnil(gEntScriptCtx.state);
                        break;

                    default:
                        lua_pushstring(gEntScriptCtx.state,
                                       (in->items[i].value == NULL) ? "" : in->items[i].value);
                        break;
                }
                lua_setfield(gEntScriptCtx.state, -2, in->items[i].key);
            }
        }

        iENT_ScriptBeginExecution(gEntScriptCtx.state);
        rc = lua_pcall(gEntScriptCtx.state, 1, 1, 0);
        iENT_ScriptEndExecution(gEntScriptCtx.state);
        if(rc != LUA_OK)
        {
            fprintf(stderr, "ENT_ScriptCall runtime failed,fn[%s],reason[%s]\n",
                    fn, lua_tostring(gEntScriptCtx.state, -1));
            lua_pop(gEntScriptCtx.state, 1);
            ret = ENT_SCR_RUNTIME_FAILED;
            goto END_OF_ROUTINE;
        }

        out->code = 0;
        out->message[0] = '\0';
        if(lua_istable(gEntScriptCtx.state, -1))
        {
            lua_getfield(gEntScriptCtx.state, -1, "code");
            if(lua_isnumber(gEntScriptCtx.state, -1))
            {
                out->code = (int)lua_tointeger(gEntScriptCtx.state, -1);
            }
            lua_pop(gEntScriptCtx.state, 1);

            lua_getfield(gEntScriptCtx.state, -1, "message");
            if(lua_isstring(gEntScriptCtx.state, -1))
            {
                iENT_ScriptCopyPath(out->message,
                                    sizeof(out->message),
                                    lua_tostring(gEntScriptCtx.state, -1));
            }
            lua_pop(gEntScriptCtx.state, 1);
        }
        else if(lua_isstring(gEntScriptCtx.state, -1))
        {
            iENT_ScriptCopyPath(out->message,
                                sizeof(out->message),
                                lua_tostring(gEntScriptCtx.state, -1));
        }
        else if(lua_isnumber(gEntScriptCtx.state, -1))
        {
            out->code = (int)lua_tointeger(gEntScriptCtx.state, -1);
        }

        lua_pop(gEntScriptCtx.state, 1);
        ret = ENT_SYS_NORMAL;
END_OF_ROUTINE:
        iENT_ScriptUnlock();
        return ret;
    }
#endif

    out->code = -1;
    out->message[0] = '\0';
    ret = ENT_SCR_UNSUPPORTED;
    iENT_ScriptUnlock();
    return ret;
}

ENT_PUBLIC MSG_ID_T ENT_ScriptClose(void)
{
    MSG_ID_T ret = ENT_SYS_NORMAL;

    iENT_ScriptLock();
    if(gEntScriptCtx.isInit == false)
    {
        iENT_ScriptUnlock();
        return ENT_SCR_NOT_INITIALIZED;
    }

#if ENT_ENABLE_LUA
    if(gEntScriptCtx.state != NULL)
    {
        lua_close(gEntScriptCtx.state);
        gEntScriptCtx.state = NULL;
    }
#endif

    memset(&gEntScriptCtx, 0, sizeof(gEntScriptCtx));
    ret = ENT_SYS_NORMAL;
    iENT_ScriptUnlock();
    return ret;
}
