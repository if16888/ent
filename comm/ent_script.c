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
#include <string.h>

#ifndef WIN32
#include <pthread.h>
#endif

#if ENT_ENABLE_LUA
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
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

static void iENT_ScriptDisableDangerousGlobals(lua_State* state)
{
    lua_pushnil(state);
    lua_setglobal(state, "io");
    lua_pushnil(state);
    lua_setglobal(state, "os");
    lua_pushnil(state);
    lua_setglobal(state, "debug");
    lua_pushnil(state);
    lua_setglobal(state, "package");
    lua_pushnil(state);
    lua_setglobal(state, "dofile");
    lua_pushnil(state);
    lua_setglobal(state, "loadfile");
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
    gEntScriptCtx.state = luaL_newstate();
    if(gEntScriptCtx.state == NULL)
    {
        memset(gEntScriptCtx.scriptRoot, 0, sizeof(gEntScriptCtx.scriptRoot));
        iENT_ScriptUnlock();
        return ENT_SCR_LOAD_FAILED;
    }

    luaL_openlibs(gEntScriptCtx.state);
    iENT_ScriptDisableDangerousGlobals(gEntScriptCtx.state);
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
    sts = iENT_ScriptJoinPath(scriptPath,
                              sizeof(scriptPath),
                              gEntScriptCtx.scriptRoot,
                              scriptName);
    if(sts != ENT_SYS_NORMAL)
    {
        ret = sts;
        goto END_OF_ROUTINE;
    }

    rc = luaL_loadfile(gEntScriptCtx.state, scriptPath);
    if(rc != LUA_OK)
    {
        fprintf(stderr, "ENT_ScriptReload compile failed,file[%s],reason[%s]\n",
                scriptPath, lua_tostring(gEntScriptCtx.state, -1));
        lua_pop(gEntScriptCtx.state, 1);
        ret = ENT_SCR_COMPILE_FAILED;
        goto END_OF_ROUTINE;
    }

    rc = lua_pcall(gEntScriptCtx.state, 0, 0, 0);
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

        rc = lua_pcall(gEntScriptCtx.state, 1, 1, 0);
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
