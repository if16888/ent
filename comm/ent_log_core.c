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
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#ifdef WIN32
#include <Windows.h>
#include <tchar.h>
#else
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#endif

#include "ent_log.h"
#include "ient_log.h"
#include "ient_comm.h"

#define DEF_MAX_NUM_LOG 15

#ifdef WIN32
static SRWLOCK sLogMutex = SRWLOCK_INIT;
#pragma warning(disable : 4996)
#else
static pthread_mutex_t sLogMutex = PTHREAD_MUTEX_INITIALIZER;
#endif

static int sLogNum = 0;
static int sLogCtxNum = 0;
volatile bool sLogMutexInit = false;

static ENT_LOG_CTX_INTERNAL sDefLog;
static struct ENT_LOG_CTX_TAG* sLogCtxHead = NULL;

static void iENT_LogGlobalLock(void)
{
#ifdef WIN32
    AcquireSRWLockExclusive(&sLogMutex);
#else
    pthread_mutex_lock(&sLogMutex);
#endif
}

static void iENT_LogGlobalUnlock(void)
{
#ifdef WIN32
    ReleaseSRWLockExclusive(&sLogMutex);
#else
    pthread_mutex_unlock(&sLogMutex);
#endif
}

static struct ENT_LOG_CTX_TAG* iENT_LogCtxFindLocked(const struct ENT_LOG_CTX_TAG* target)
{
    struct ENT_LOG_CTX_TAG* current = sLogCtxHead;

    while(current != NULL)
    {
        if(current == target)
        {
            return current;
        }
        current = current->registryNext;
    }
    return NULL;
}

static void iENT_LogCtxUnlinkLocked(struct ENT_LOG_CTX_TAG* target)
{
    struct ENT_LOG_CTX_TAG** current = &sLogCtxHead;

    while(*current != NULL)
    {
        if(*current == target)
        {
            *current = target->registryNext;
            target->registryNext = NULL;
            return;
        }
        current = &(*current)->registryNext;
    }
}

ENT_LOG_CTX_INTERNAL* iENT_LogDefaultCtx(void)
{
    return &sDefLog;
}

ENT_LOG_CTX_INTERNAL* iENT_LogCtxFromHandle(ENT_LOG_CTX ctx)
{
    return (ENT_LOG_CTX_INTERNAL*)ctx;
}

MSG_ID_T iENT_LogCtxValidate(const struct ENT_LOG_CTX_TAG* ctx, ENT_LOG logHandle)
{
    if(ctx == NULL || ctx->tag != ENTLOG_CTX_TAG || ctx->isInit == false)
    {
        return ENT_LOG_BAD_HANDLE;
    }

    if(ctx->logHandle != NULL && ctx->logHandle != logHandle)
    {
        return ENT_LOG_BAD_HANDLE;
    }

    return ENT_SYS_NORMAL;
}

static MSG_ID_T iENT_LogCtxValidateOwnedHandle(const struct ENT_LOG_CTX_TAG* ctx, ENT_LOG logHandle)
{
    if(ctx == NULL || ctx->tag != ENTLOG_CTX_TAG || ctx->isInit == false)
    {
        return ENT_LOG_BAD_HANDLE;
    }

    if(ctx->logHandle == NULL || ctx->logHandle != logHandle)
    {
        return ENT_LOG_BAD_HANDLE;
    }

    return ENT_SYS_NORMAL;
}

static MSG_ID_T iENT_LogCtxBeginCall(struct ENT_LOG_CTX_TAG* ctx,
                                     ENT_LOG logHandle,
                                     bool requireOwnedHandle,
                                     bool requireEmptyHandle)
{
    MSG_ID_T sts = ENT_SYS_NORMAL;

#ifdef WIN32
    AcquireSRWLockExclusive(&sLogMutex);
#else
    pthread_mutex_lock(&sLogMutex);
#endif
    ctx = iENT_LogCtxFindLocked(ctx);
    if(ctx == NULL || ctx->tag != ENTLOG_CTX_TAG || ctx->isInit == false ||
       ctx->state != ENT_LOG_HANDLE_ACTIVE_E)
    {
        sts = ENT_LOG_BAD_HANDLE;
    }
    else if(requireOwnedHandle && (ctx->logHandle == NULL || ctx->logHandle != logHandle))
    {
        sts = ENT_LOG_BAD_HANDLE;
    }
    else if(requireEmptyHandle && ctx->logHandle != NULL)
    {
        sts = ENT_LOG_BAD_HANDLE;
    }
    else
    {
        ctx->activeCalls++;
    }
#ifdef WIN32
    ReleaseSRWLockExclusive(&sLogMutex);
#else
    pthread_mutex_unlock(&sLogMutex);
#endif
    return sts;
}

static void iENT_LogCtxEndCall(struct ENT_LOG_CTX_TAG* ctx)
{
    if(ctx == NULL)
    {
        return;
    }

#ifdef WIN32
    iENT_LogGlobalLock();
#else
    pthread_mutex_lock(&sLogMutex);
#endif
    if(ctx->activeCalls > 0)
    {
        ctx->activeCalls--;
    }
    if(ctx->state == ENT_LOG_HANDLE_CLOSING_E && ctx->activeCalls == 0)
    {
#ifdef WIN32
        WakeAllConditionVariable(&ctx->closeCv);
#else
        pthread_cond_broadcast(&ctx->closeCv);
#endif
    }
#ifdef WIN32
    iENT_LogGlobalUnlock();
#else
    pthread_mutex_unlock(&sLogMutex);
#endif
}

MSG_ID_T iENT_LogPathCheck(const char* path)
{
    if(path == NULL || path[0] == '\0')
    {
        fprintf(stderr, "Func [%s] Line [%d],arguments is invalid.\n", "iENT_LogPathCheck", __LINE__);
        return ENT_LOG_BAD_ARGUMENT;
    }

#ifdef WIN32
    {
        HANDLE hDir = INVALID_HANDLE_VALUE;
        WCHAR wPath[MAX_PATH];
        int wLen = MultiByteToWideChar(CP_ACP, 0, path, -1, wPath, MAX_PATH);
        if(wLen <= 0)
        {
            return ENT_LOG_PATH_FAILED;
        }
        if(wPath[wLen - 1] == L'\\' || wPath[wLen - 1] == L'/')
        {
            wPath[wLen - 1] = L'\0';
        }

        hDir = CreateFileW(wPath,
                           GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL,
                           OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS,
                           NULL);

        if(hDir == INVALID_HANDLE_VALUE)
        {
            if(!CreateDirectoryW(wPath, NULL))
            {
                fprintf(stderr,
                        "Func [%s] Line [%d],Can not create log directory, invalid path name[%s]\n",
                        "iENT_LogPathCheck",
                        __LINE__,
                        path);
                return ENT_LOG_PATH_FAILED;
            }
        }
        else
        {
            DWORD attrs = GetFileAttributesW(wPath);
            CloseHandle(hDir);
            if(attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY))
            {
                fprintf(stderr,
                        "Func [%s] Line [%d],path exists but is not a directory[%s]\n",
                        "iENT_LogPathCheck",
                        __LINE__,
                        path);
                return ENT_LOG_PATH_FAILED;
            }
        }
    }
#else
    {
        struct stat st;
        if(stat(path, &st) == -1)
        {
            if(mkdir(path, 0777) == -1)
            {
                fprintf(stderr,
                        "Func [%s] Line [%d],Can not create log directory [%s],msg->[%s]\n",
                        "iENT_LogPathCheck",
                        __LINE__,
                        path,
                        strerror(errno));
                return ENT_LOG_PATH_FAILED;
            }
            return ENT_SYS_NORMAL;
        }

        if(!S_ISDIR(st.st_mode))
        {
            fprintf(stderr,
                    "Func [%s] Line [%d],path exists but is not a directory [%s].\n",
                    "iENT_LogPathCheck",
                    __LINE__,
                    path);
            return ENT_LOG_PATH_FAILED;
        }
    }
#endif

    return ENT_SYS_NORMAL;
}

static MSG_ID_T iENT_LogInitCtx(ENT_LOG_CTX_INTERNAL* log,
                                const char* moduleName,
                                const char* logPath)
{
    size_t len = 0;
    MSG_ID_T sts = ENT_SYS_NORMAL;

    if(log == NULL)
    {
        return ENT_LOG_BAD_ARGUMENT;
    }

    memset(log, 0, sizeof(*log));
    if(moduleName)
        log->moduleName = ENT_StrDup(moduleName);
    else
        log->moduleName = ENT_StrDup("default");

    if(log->moduleName == NULL)
    {
        return ENT_LOG_ALLOC_FAILED;
    }

    if(logPath)
    {
        log->logPath = ENT_StrDup(logPath);
        if(log->logPath == NULL)
        {
            free(log->moduleName);
            log->moduleName = NULL;
            return ENT_LOG_ALLOC_FAILED;
        }
        len = strlen(log->logPath);
        if(len > 0 && (log->logPath[len - 1] == '\\' || log->logPath[len - 1] == '/'))
        {
            log->logPath[len - 1] = '\0';
        }
        sts = iENT_LogPathCheck(log->logPath);
        if(sts < 0)
        {
            free(log->moduleName);
            log->moduleName = NULL;
            free(log->logPath);
            log->logPath = NULL;
            return sts;
        }
    }

#ifdef WIN32
    InitializeCriticalSection(&log->cs);
    InitializeConditionVariable(&log->closeCv);
    InitializeConditionVariable(&log->bufferCv);
    log->bufferThread = NULL;
#else
    pthread_mutex_init(&log->cs, NULL);
    if(pthread_cond_init(&log->closeCv, NULL) != 0)
    {
        free(log->moduleName);
        log->moduleName = NULL;
        free(log->logPath);
        log->logPath = NULL;
        return ENT_LOG_ALLOC_FAILED;
    }
    if(pthread_cond_init(&log->bufferCv, NULL) != 0)
    {
        pthread_cond_destroy(&log->closeCv);
        pthread_mutex_destroy(&log->cs);
        free(log->moduleName);
        log->moduleName = NULL;
        free(log->logPath);
        log->logPath = NULL;
        return ENT_LOG_ALLOC_FAILED;
    }
#endif
    iENT_LogStateSet(log, ENT_LOG_HANDLE_CREATED_E);
    log->activeWriters = 0;
    log->bufferThreadStarted = false;
    log->bufferThreadStop = false;
    log->closeAttemptActive = false;
    log->pendingFlushes = 0;
    log->flushBatch = 256;
    log->flushIntervalMs = 0;
    log->lastFlushMs = 0;
    log->bufferHead = NULL;
    log->bufferTail = NULL;
    log->poolFreeHead = NULL;
    log->poolFreeCount = 0;
    log->ownerCtx = NULL;
    log->isInit = true;
    log->isDebug = false;
    log->isBuffer = false;
    iENT_LogFastFlagSet(&log->isDebugFast, 0);
    iENT_LogFastFlagSet(&log->isBufferFast, 0);
    log->logLevel = LOG_LEV_WARN_E;
    log->maxNum = DEF_MAX_NUM_LOG;
    log->tag = ENTLOG_TAG;
    iENT_LogStateSet(log, ENT_LOG_HANDLE_ACTIVE_E);
    return ENT_SYS_NORMAL;
}

ENT_LOG_HANDLE_STATE_E iENT_LogStateGet(const ENT_LOG_CTX_INTERNAL* log)
{
#ifdef WIN32
    return (ENT_LOG_HANDLE_STATE_E)InterlockedCompareExchange((volatile LONG*)&log->handleState, 0, 0);
#else
    return (ENT_LOG_HANDLE_STATE_E)__sync_val_compare_and_swap((volatile int*)&log->handleState, 0, 0);
#endif
}

void iENT_LogStateSet(ENT_LOG_CTX_INTERNAL* log, ENT_LOG_HANDLE_STATE_E state)
{
#ifdef WIN32
    InterlockedExchange(&log->handleState, (LONG)state);
#else
    __sync_lock_test_and_set(&log->handleState, (int)state);
#endif
}

int iENT_LogActiveGet(const ENT_LOG_CTX_INTERNAL* log)
{
#ifdef WIN32
    return (int)InterlockedCompareExchange((volatile LONG*)&log->activeWriters, 0, 0);
#else
    return __sync_val_compare_and_swap((volatile int*)&log->activeWriters, 0, 0);
#endif
}

void iENT_LogActiveInc(ENT_LOG_CTX_INTERNAL* log)
{
#ifdef WIN32
    InterlockedIncrement(&log->activeWriters);
#else
    __sync_add_and_fetch(&log->activeWriters, 1);
#endif
}

int iENT_LogActiveDec(ENT_LOG_CTX_INTERNAL* log)
{
#ifdef WIN32
    return (int)InterlockedDecrement(&log->activeWriters);
#else
    return __sync_sub_and_fetch(&log->activeWriters, 1);
#endif
}

MSG_ID_T iENT_LogGetCtx(ENT_LOG_CTX_INTERNAL** logCtx, ENT_LOG logHandle)
{
    ENT_LOG_CTX_INTERNAL* log = (ENT_LOG_CTX_INTERNAL*)logHandle;

    *logCtx = NULL;

    if(sLogMutexInit == false)
    {
        fprintf(stderr, "Func [%s] Line [%d],Uninitialized,please call ENT_LogInit.\n", "iENT_LogGetCtx", __LINE__);
        return ENT_LOG_NOT_INITIALIZED;
    }

    if(log == NULL)
    {
        if(iENT_LogDefaultCtx()->isInit)
            log = iENT_LogDefaultCtx();
        else
        {
            fprintf(stderr, "Func [%s] Line [%d],arguments is invalid.\n", "iENT_LogGetCtx", __LINE__);
            return ENT_LOG_BAD_HANDLE;
        }
    }
    else if(log->tag != ENTLOG_TAG || log->isInit == false)
    {
        fprintf(stderr, "Func [%s] Line [%d],arguments is invalid.\n", "iENT_LogGetCtx", __LINE__);
        return ENT_LOG_BAD_HANDLE;
    }

    *logCtx = log;
    return ENT_SYS_NORMAL;
}

MSG_ID_T iENT_LogAcquireWriter(ENT_LOG_CTX_INTERNAL** logCtx, ENT_LOG logHandle)
{
    ENT_LOG_CTX_INTERNAL* log = NULL;

    *logCtx = NULL;
    if(sLogMutexInit == false)
    {
        fprintf(stderr, "Func [%s] Line [%d],Uninitialized,please call ENT_LogInit.\n", "iENT_LogAcquireWriter", __LINE__);
        return ENT_LOG_NOT_INITIALIZED;
    }

#ifdef WIN32
    iENT_LogGlobalLock();
#else
    pthread_mutex_lock(&sLogMutex);
#endif

    if(sLogMutexInit == false)
    {
#ifdef WIN32
        iENT_LogGlobalUnlock();
#else
        pthread_mutex_unlock(&sLogMutex);
#endif
        return ENT_LOG_NOT_INITIALIZED;
    }

    if(logHandle == NULL)
    {
        if(iENT_LogDefaultCtx()->isInit)
            log = iENT_LogDefaultCtx();
        else
        {
#ifdef WIN32
            iENT_LogGlobalUnlock();
#else
            pthread_mutex_unlock(&sLogMutex);
#endif
            fprintf(stderr, "Func [%s] Line [%d],arguments is invalid.\n", "iENT_LogAcquireWriter", __LINE__);
            return ENT_LOG_BAD_HANDLE;
        }
    }
    else
    {
        log = (ENT_LOG_CTX_INTERNAL*)logHandle;
        if(log->tag != ENTLOG_TAG || log->isInit == false)
        {
#ifdef WIN32
            iENT_LogGlobalUnlock();
#else
            pthread_mutex_unlock(&sLogMutex);
#endif
            fprintf(stderr, "Func [%s] Line [%d],arguments is invalid.\n", "iENT_LogAcquireWriter", __LINE__);
            return ENT_LOG_BAD_HANDLE;
        }
    }

    if(iENT_LogStateGet(log) != ENT_LOG_HANDLE_ACTIVE_E)
    {
#ifdef WIN32
        iENT_LogGlobalUnlock();
#else
        pthread_mutex_unlock(&sLogMutex);
#endif
        return ENT_LOG_IN_USE;
    }

    iENT_LogActiveInc(log);
    *logCtx = log;
#ifdef WIN32
    iENT_LogGlobalUnlock();
#else
    pthread_mutex_unlock(&sLogMutex);
#endif
    return ENT_SYS_NORMAL;
}

void iENT_LogReleaseWriter(ENT_LOG_CTX_INTERNAL* log)
{
    if(iENT_LogActiveDec(log) == 0 && iENT_LogStateGet(log) == ENT_LOG_HANDLE_CLOSING_E)
    {
#ifdef WIN32
        iENT_LogGlobalLock();
        if(iENT_LogStateGet(log) == ENT_LOG_HANDLE_CLOSING_E && iENT_LogActiveGet(log) == 0)
            WakeAllConditionVariable(&log->closeCv);
        iENT_LogGlobalUnlock();
#else
        pthread_mutex_lock(&sLogMutex);
        if(iENT_LogStateGet(log) == ENT_LOG_HANDLE_CLOSING_E && iENT_LogActiveGet(log) == 0)
            pthread_cond_broadcast(&log->closeCv);
        pthread_mutex_unlock(&sLogMutex);
#endif
    }
}

MSG_ID_T iENT_LogInit(void)
{
    iENT_LogGlobalLock();
    if(sLogMutexInit)
    {
        iENT_LogGlobalUnlock();
        return ENT_LOG_NON_FATAL;
    }
    fprintf(stderr, "Func [%s] Line [%d],sucessful.\n", "ENT_LogInit", __LINE__);
    sLogMutexInit = true;
    iENT_LogGlobalUnlock();
    return ENT_SYS_NORMAL;
}

MSG_ID_T iENT_LogClose(void)
{
    iENT_LogGlobalLock();
    if(sLogMutexInit == false)
    {
        iENT_LogGlobalUnlock();
        return ENT_LOG_NOT_INITIALIZED;
    }
    if(sLogNum > 0 || sLogCtxNum > 0)
    {
        iENT_LogGlobalUnlock();
        return ENT_LOG_IN_USE;
    }
    sLogMutexInit = false;
    iENT_LogGlobalUnlock();

    fprintf(stderr, "Func [%s] Line [%d],sucessful.\n", "ENT_LogClose", __LINE__);
    return ENT_SYS_NORMAL;
}

MSG_ID_T ENT_LogCtxInit(ENT_LOG_CTX* pCtx)
{
    struct ENT_LOG_CTX_TAG* ctx = NULL;

    if(sLogMutexInit == false)
    {
        fprintf(stderr, "Func [%s] Line [%d],Uninitialized,please call ENT_LogInit.\n", "ENT_LogCtxInit", __LINE__);
        return ENT_LOG_NOT_INITIALIZED;
    }

    if(pCtx == NULL)
    {
        fprintf(stderr, "Func [%s] Line [%d],arguments is invalid.\n", "ENT_LogCtxInit", __LINE__);
        return ENT_LOG_BAD_ARGUMENT;
    }

    ctx = (struct ENT_LOG_CTX_TAG*)malloc(sizeof(*ctx));
    if(ctx == NULL)
    {
        fprintf(stderr, "Func [%s] Line [%d],malloc failed.\n", "ENT_LogCtxInit", __LINE__);
        return ENT_LOG_ALLOC_FAILED;
    }

    ctx->tag = ENTLOG_CTX_TAG;
    ctx->isInit = true;
    ctx->logHandle = NULL;
#ifdef WIN32
    InitializeConditionVariable(&ctx->closeCv);
#else
    if(pthread_cond_init(&ctx->closeCv, NULL) != 0)
    {
        free(ctx);
        return ENT_LOG_THREAD_FAILED;
    }
#endif
    ctx->state = ENT_LOG_HANDLE_ACTIVE_E;
    ctx->activeCalls = 0;
    ctx->registryNext = NULL;
#ifdef WIN32
    iENT_LogGlobalLock();
#else
    pthread_mutex_lock(&sLogMutex);
#endif
    ctx->registryNext = sLogCtxHead;
    sLogCtxHead = ctx;
    sLogCtxNum++;
#ifdef WIN32
    iENT_LogGlobalUnlock();
#else
    pthread_mutex_unlock(&sLogMutex);
#endif
    *pCtx = ctx;
    return ENT_SYS_NORMAL;
}

MSG_ID_T ENT_LogCtxClose(ENT_LOG_CTX ctx)
{
    struct ENT_LOG_CTX_TAG* logCtx = (struct ENT_LOG_CTX_TAG*)ctx;
    MSG_ID_T closeSts = ENT_SYS_NORMAL;

    if(sLogMutexInit == false)
    {
        fprintf(stderr, "Func [%s] Line [%d],Uninitialized,please call ENT_LogInit.\n", "ENT_LogCtxClose", __LINE__);
        return ENT_LOG_NOT_INITIALIZED;
    }

#ifdef WIN32
    iENT_LogGlobalLock();
#else
    pthread_mutex_lock(&sLogMutex);
#endif
    logCtx = iENT_LogCtxFindLocked(logCtx);
    if(logCtx == NULL)
    {
#ifdef WIN32
        iENT_LogGlobalUnlock();
#else
        pthread_mutex_unlock(&sLogMutex);
#endif
        return ENT_LOG_BAD_HANDLE;
    }
    if(logCtx == NULL || logCtx->tag != ENTLOG_CTX_TAG || logCtx->isInit == false)
    {
#ifdef WIN32
        iENT_LogGlobalUnlock();
#else
        pthread_mutex_unlock(&sLogMutex);
#endif
        fprintf(stderr, "Func [%s] Line [%d],arguments is invalid.\n", "ENT_LogCtxClose", __LINE__);
        return ENT_LOG_BAD_HANDLE;
    }
    if(logCtx->state == ENT_LOG_HANDLE_CLOSING_E)
    {
#ifdef WIN32
        iENT_LogGlobalUnlock();
#else
        pthread_mutex_unlock(&sLogMutex);
#endif
        return ENT_LOG_IN_USE;
    }
    if(logCtx->state != ENT_LOG_HANDLE_ACTIVE_E)
    {
#ifdef WIN32
        iENT_LogGlobalUnlock();
#else
        pthread_mutex_unlock(&sLogMutex);
#endif
        fprintf(stderr, "Func [%s] Line [%d],arguments is invalid.\n", "ENT_LogCtxClose", __LINE__);
        return ENT_LOG_BAD_HANDLE;
    }
    logCtx->state = ENT_LOG_HANDLE_CLOSING_E;
    while(logCtx->activeCalls > 0)
    {
#ifdef WIN32
        SleepConditionVariableSRW(&logCtx->closeCv, &sLogMutex, INFINITE, 0);
#else
        pthread_cond_wait(&logCtx->closeCv, &sLogMutex);
#endif
    }
#ifdef WIN32
    iENT_LogGlobalUnlock();
#else
    pthread_mutex_unlock(&sLogMutex);
#endif

    if(logCtx->logHandle != NULL)
    {
        closeSts = iENT_LogCloseHandle(logCtx->logHandle);
        if(closeSts != ENT_SYS_NORMAL)
        {
#ifdef WIN32
            iENT_LogGlobalLock();
#else
            pthread_mutex_lock(&sLogMutex);
#endif
            if(logCtx->tag == ENTLOG_CTX_TAG && logCtx->isInit)
            {
                logCtx->state = ENT_LOG_HANDLE_ACTIVE_E;
            }
#ifdef WIN32
            iENT_LogGlobalUnlock();
#else
            pthread_mutex_unlock(&sLogMutex);
#endif
            return closeSts;
        }
        logCtx->logHandle = NULL;
    }

#ifdef WIN32
    iENT_LogGlobalLock();
#else
    pthread_mutex_lock(&sLogMutex);
#endif
    logCtx->isInit = false;
    logCtx->state = ENT_LOG_HANDLE_CLOSED_E;
    logCtx->tag = 0;
    iENT_LogCtxUnlinkLocked(logCtx);
    if(sLogCtxNum > 0)
    {
        sLogCtxNum--;
    }
#ifdef WIN32
    iENT_LogGlobalUnlock();
#else
    pthread_mutex_unlock(&sLogMutex);
#endif
#ifndef WIN32
    pthread_cond_destroy(&logCtx->closeCv);
#endif
    free(logCtx);
    return closeSts;
}

MSG_ID_T ENT_LogCtxInitHandle(ENT_LOG_CTX ctx, ENT_LOG* pLogHandle, const char* moduleName, const char* logPath)
{
    struct ENT_LOG_CTX_TAG* logCtx = (struct ENT_LOG_CTX_TAG*)ctx;

    if(sLogMutexInit == false)
    {
        fprintf(stderr, "Func [%s] Line [%d],Uninitialized,please call ENT_LogInit.\n", "ENT_LogCtxInitHandle", __LINE__);
        return ENT_LOG_NOT_INITIALIZED;
    }

    if(iENT_LogCtxBeginCall(logCtx, NULL, false, true) != ENT_SYS_NORMAL)
    {
        fprintf(stderr, "Func [%s] Line [%d],arguments is invalid.\n", "ENT_LogCtxInitHandle", __LINE__);
        return ENT_LOG_BAD_HANDLE;
    }

    if(pLogHandle == NULL)
    {
        fprintf(stderr, "Func [%s] Line [%d],arguments is invalid.\n", "ENT_LogCtxInitHandle", __LINE__);
        iENT_LogCtxEndCall(logCtx);
        return ENT_LOG_BAD_ARGUMENT;
    }

    if(logCtx->logHandle != NULL)
    {
        fprintf(stderr, "Func [%s] Line [%d],arguments is invalid.\n", "ENT_LogCtxInitHandle", __LINE__);
        iENT_LogCtxEndCall(logCtx);
        return ENT_LOG_BAD_HANDLE;
    }

    {
        MSG_ID_T sts = iENT_LogInitHandle(pLogHandle, moduleName, logPath);
        if(sts == ENT_SYS_NORMAL)
        {
            logCtx->logHandle = *pLogHandle;
            ((ENT_LOG_CTX_INTERNAL*)(*pLogHandle))->ownerCtx = logCtx;
        }
        iENT_LogCtxEndCall(logCtx);
        return sts;
    }
}

MSG_ID_T ENT_LogCtxSetOption(ENT_LOG_CTX ctx, ENT_LOG logHandle, ENT_LOG_OPTIONS_E option, const void* arg)
{
    struct ENT_LOG_CTX_TAG* logCtx = (struct ENT_LOG_CTX_TAG*)ctx;

    if(sLogMutexInit == false)
    {
        fprintf(stderr, "Func [%s] Line [%d],Uninitialized,please call ENT_LogInit.\n", "ENT_LogCtxSetOption", __LINE__);
        return ENT_LOG_NOT_INITIALIZED;
    }

    if(iENT_LogCtxBeginCall(logCtx, logHandle, true, false) != ENT_SYS_NORMAL)
    {
        fprintf(stderr, "Func [%s] Line [%d],arguments is invalid.\n", "ENT_LogCtxSetOption", __LINE__);
        return ENT_LOG_BAD_HANDLE;
    }

    {
        MSG_ID_T sts = ENT_LogSetOption(logHandle, option, arg);
        iENT_LogCtxEndCall(logCtx);
        return sts;
    }
}

MSG_ID_T ENT_LogCtxCloseHandle(ENT_LOG_CTX ctx, ENT_LOG logHandle)
{
    struct ENT_LOG_CTX_TAG* logCtx = (struct ENT_LOG_CTX_TAG*)ctx;

    if(sLogMutexInit == false)
    {
        fprintf(stderr, "Func [%s] Line [%d],Uninitialized,please call ENT_LogInit.\n", "ENT_LogCtxCloseHandle", __LINE__);
        return ENT_LOG_NOT_INITIALIZED;
    }

    if(iENT_LogCtxBeginCall(logCtx, logHandle, true, false) != ENT_SYS_NORMAL)
    {
        fprintf(stderr, "Func [%s] Line [%d],arguments is invalid.\n", "ENT_LogCtxCloseHandle", __LINE__);
        return ENT_LOG_BAD_HANDLE;
    }

    {
        MSG_ID_T sts = iENT_LogCloseHandle(logHandle);
        if(sts == ENT_SYS_NORMAL && logCtx->logHandle == logHandle)
        {
            logCtx->logHandle = NULL;
        }
        iENT_LogCtxEndCall(logCtx);
        return sts;
    }
}

MSG_ID_T ENT_LogCtxRaw(ENT_LOG_CTX ctx, ENT_LOG logHandle, const char* format, ...)
{
    MSG_ID_T sts = ENT_SYS_NORMAL;
    struct ENT_LOG_CTX_TAG* ownerCtx = (struct ENT_LOG_CTX_TAG*)ctx;
    ENT_LOG_CTX_INTERNAL* logCtx = NULL;
    va_list va_args;

    if(sLogMutexInit == false)
    {
        fprintf(stderr, "Func [%s] Line [%d],Uninitialized,please call ENT_LogInit.\n", "ENT_LogCtxRaw", __LINE__);
        return ENT_LOG_NOT_INITIALIZED;
    }

    if(iENT_LogCtxBeginCall(ownerCtx, logHandle, true, false) != ENT_SYS_NORMAL)
    {
        fprintf(stderr, "Func [%s] Line [%d],arguments is invalid.\n", "ENT_LogCtxRaw", __LINE__);
        return ENT_LOG_BAD_HANDLE;
    }

    sts = iENT_LogAcquireWriter(&logCtx, logHandle);
    if(sts < 0)
    {
        iENT_LogCtxEndCall(ownerCtx);
        return sts;
    }

    va_start(va_args, format);
    sts = iENT_LogVRaw(logCtx, format, va_args);
    va_end(va_args);
    iENT_LogReleaseWriter(logCtx);
    iENT_LogCtxEndCall(ownerCtx);
    return sts;
}

MSG_ID_T ENT_LogCtxFatal(ENT_LOG_CTX ctx, ENT_LOG logHandle, const char* format, ...)
{
    MSG_ID_T sts = ENT_SYS_NORMAL;
    struct ENT_LOG_CTX_TAG* ownerCtx = (struct ENT_LOG_CTX_TAG*)ctx;
    ENT_LOG_CTX_INTERNAL* logCtx = NULL;
    va_list va_args;

    if(sLogMutexInit == false)
    {
        fprintf(stderr, "Func [%s] Line [%d],Uninitialized,please call ENT_LogInit.\n", "ENT_LogCtxFatal", __LINE__);
        return ENT_LOG_NOT_INITIALIZED;
    }

    if(iENT_LogCtxBeginCall(ownerCtx, logHandle, true, false) != ENT_SYS_NORMAL)
    {
        fprintf(stderr, "Func [%s] Line [%d],arguments is invalid.\n", "ENT_LogCtxFatal", __LINE__);
        return ENT_LOG_BAD_HANDLE;
    }

    sts = iENT_LogAcquireWriter(&logCtx, logHandle);
    if(sts < 0)
    {
        iENT_LogCtxEndCall(ownerCtx);
        return sts;
    }
    if(LOG_LEV_FATAL_E > logCtx->logLevel)
    {
        iENT_LogReleaseWriter(logCtx);
        iENT_LogCtxEndCall(ownerCtx);
        return ENT_LOG_NON_FATAL;
    }

    va_start(va_args, format);
    sts = iENT_LogVPrint(logCtx, LOG_LEV_FATAL_E, format, va_args);
    va_end(va_args);
    iENT_LogReleaseWriter(logCtx);
    iENT_LogCtxEndCall(ownerCtx);
    return sts;
}

MSG_ID_T ENT_LogCtxError(ENT_LOG_CTX ctx, ENT_LOG logHandle, const char* format, ...)
{
    MSG_ID_T sts = ENT_SYS_NORMAL;
    struct ENT_LOG_CTX_TAG* ownerCtx = (struct ENT_LOG_CTX_TAG*)ctx;
    ENT_LOG_CTX_INTERNAL* logCtx = NULL;
    va_list va_args;

    if(sLogMutexInit == false)
    {
        fprintf(stderr, "Func [%s] Line [%d],Uninitialized,please call ENT_LogInit.\n", "ENT_LogCtxError", __LINE__);
        return ENT_LOG_NOT_INITIALIZED;
    }

    if(iENT_LogCtxBeginCall(ownerCtx, logHandle, true, false) != ENT_SYS_NORMAL)
    {
        fprintf(stderr, "Func [%s] Line [%d],arguments is invalid.\n", "ENT_LogCtxError", __LINE__);
        return ENT_LOG_BAD_HANDLE;
    }

    sts = iENT_LogAcquireWriter(&logCtx, logHandle);
    if(sts < 0)
    {
        iENT_LogCtxEndCall(ownerCtx);
        return sts;
    }
    if(LOG_LEV_ERROR_E > logCtx->logLevel)
    {
        iENT_LogReleaseWriter(logCtx);
        iENT_LogCtxEndCall(ownerCtx);
        return ENT_LOG_NON_FATAL;
    }

    va_start(va_args, format);
    sts = iENT_LogVPrint(logCtx, LOG_LEV_ERROR_E, format, va_args);
    va_end(va_args);
    iENT_LogReleaseWriter(logCtx);
    iENT_LogCtxEndCall(ownerCtx);
    return sts;
}

MSG_ID_T ENT_LogCtxWarn(ENT_LOG_CTX ctx, ENT_LOG logHandle, const char* format, ...)
{
    MSG_ID_T sts = ENT_SYS_NORMAL;
    struct ENT_LOG_CTX_TAG* ownerCtx = (struct ENT_LOG_CTX_TAG*)ctx;
    ENT_LOG_CTX_INTERNAL* logCtx = NULL;
    va_list va_args;

    if(sLogMutexInit == false)
    {
        fprintf(stderr, "Func [%s] Line [%d],Uninitialized,please call ENT_LogInit.\n", "ENT_LogCtxWarn", __LINE__);
        return ENT_LOG_NOT_INITIALIZED;
    }

    if(iENT_LogCtxBeginCall(ownerCtx, logHandle, true, false) != ENT_SYS_NORMAL)
    {
        fprintf(stderr, "Func [%s] Line [%d],arguments is invalid.\n", "ENT_LogCtxWarn", __LINE__);
        return ENT_LOG_BAD_HANDLE;
    }

    sts = iENT_LogAcquireWriter(&logCtx, logHandle);
    if(sts < 0)
    {
        iENT_LogCtxEndCall(ownerCtx);
        return sts;
    }
    if(LOG_LEV_WARN_E > logCtx->logLevel)
    {
        iENT_LogReleaseWriter(logCtx);
        iENT_LogCtxEndCall(ownerCtx);
        return ENT_LOG_NON_FATAL;
    }

    va_start(va_args, format);
    sts = iENT_LogVPrint(logCtx, LOG_LEV_WARN_E, format, va_args);
    va_end(va_args);
    iENT_LogReleaseWriter(logCtx);
    iENT_LogCtxEndCall(ownerCtx);
    return sts;
}

MSG_ID_T ENT_LogCtxPrint(ENT_LOG_CTX ctx, ENT_LOG logHandle, const char* format, ...)
{
    MSG_ID_T sts = ENT_SYS_NORMAL;
    struct ENT_LOG_CTX_TAG* ownerCtx = (struct ENT_LOG_CTX_TAG*)ctx;
    ENT_LOG_CTX_INTERNAL* logCtx = NULL;
    va_list va_args;

    if(sLogMutexInit == false)
    {
        fprintf(stderr, "Func [%s] Line [%d],Uninitialized,please call ENT_LogInit.\n", "ENT_LogCtxPrint", __LINE__);
        return ENT_LOG_NOT_INITIALIZED;
    }

    if(iENT_LogCtxBeginCall(ownerCtx, logHandle, true, false) != ENT_SYS_NORMAL)
    {
        fprintf(stderr, "Func [%s] Line [%d],arguments is invalid.\n", "ENT_LogCtxPrint", __LINE__);
        return ENT_LOG_BAD_HANDLE;
    }

    sts = iENT_LogAcquireWriter(&logCtx, logHandle);
    if(sts < 0)
    {
        iENT_LogCtxEndCall(ownerCtx);
        return sts;
    }
    if(LOG_LEV_INFO_E > logCtx->logLevel)
    {
        iENT_LogReleaseWriter(logCtx);
        iENT_LogCtxEndCall(ownerCtx);
        return ENT_LOG_NON_FATAL;
    }

    va_start(va_args, format);
    sts = iENT_LogVPrint(logCtx, LOG_LEV_INFO_E, format, va_args);
    va_end(va_args);
    iENT_LogReleaseWriter(logCtx);
    iENT_LogCtxEndCall(ownerCtx);
    return sts;
}

MSG_ID_T ENT_LogCtxDebug(ENT_LOG_CTX ctx, ENT_LOG logHandle, const char* format, ...)
{
    MSG_ID_T sts = ENT_SYS_NORMAL;
    struct ENT_LOG_CTX_TAG* ownerCtx = (struct ENT_LOG_CTX_TAG*)ctx;
    ENT_LOG_CTX_INTERNAL* logCtx = NULL;
    va_list va_args;

    if(sLogMutexInit == false)
    {
        fprintf(stderr, "Func [%s] Line [%d],Uninitialized,please call ENT_LogInit.\n", "ENT_LogCtxDebug", __LINE__);
        return ENT_LOG_NOT_INITIALIZED;
    }

    if(iENT_LogCtxBeginCall(ownerCtx, logHandle, true, false) != ENT_SYS_NORMAL)
    {
        fprintf(stderr, "Func [%s] Line [%d],arguments is invalid.\n", "ENT_LogCtxDebug", __LINE__);
        return ENT_LOG_BAD_HANDLE;
    }

    sts = iENT_LogAcquireWriter(&logCtx, logHandle);
    if(sts < 0)
    {
        iENT_LogCtxEndCall(ownerCtx);
        return sts;
    }
    if(LOG_LEV_DEBUG_E > logCtx->logLevel)
    {
        iENT_LogReleaseWriter(logCtx);
        iENT_LogCtxEndCall(ownerCtx);
        return ENT_LOG_NON_FATAL;
    }

    va_start(va_args, format);
    sts = iENT_LogVPrint(logCtx, LOG_LEV_DEBUG_E, format, va_args);
    va_end(va_args);
    iENT_LogReleaseWriter(logCtx);
    iENT_LogCtxEndCall(ownerCtx);
    return sts;
}

MSG_ID_T iENT_LogInitHandle(ENT_LOG* pLogHandle, const char* moduleName, const char* logPath)
{
    MSG_ID_T sts = ENT_SYS_NORMAL;
    ENT_LOG_CTX_INTERNAL* log = NULL;
    ENT_LOG_CTX_INTERNAL* existing = NULL;

    if(sLogMutexInit == false)
    {
        fprintf(stderr, "Func [%s] Line [%d],Uninitialized,please call ENT_LogInit.\n", "iENT_LogInitHandle", __LINE__);
        return ENT_LOG_NOT_INITIALIZED;
    }

#ifdef WIN32
    iENT_LogGlobalLock();
#else
    pthread_mutex_lock(&sLogMutex);
#endif
    if(pLogHandle == NULL)
    {
        existing = iENT_LogDefaultCtx();
        if(existing->isInit)
        {
            sts = ENT_LOG_NON_FATAL;
            fprintf(stderr, "Func [%s] Line [%d],The module [%s] log path [%s] was already opened.\n",
                    "iENT_LogInitHandle",
                    __LINE__,
                    moduleName,
                    logPath);
            goto END_OF_ROUTINE;
        }
        log = existing;
    }
    else
    {
        log = (ENT_LOG_CTX_INTERNAL*)malloc(sizeof(ENT_LOG_CTX_INTERNAL));
        if(log == NULL)
        {
            sts = ENT_LOG_ALLOC_FAILED;
            fprintf(stderr, "Func [%s] Line [%d],The module [%s] log path [%s] malloc failed.\n",
                    "iENT_LogInitHandle",
                    __LINE__,
                    moduleName,
                    logPath);
            goto END_OF_ROUTINE;
        }
    }

    sts = iENT_LogInitCtx(log, moduleName, logPath);
    if(sts < 0)
    {
        if(log != existing && log != NULL)
        {
            free(log);
        }
        goto END_OF_ROUTINE;
    }
    if(pLogHandle)
    {
        *pLogHandle = log;
    }
    sLogNum++;

END_OF_ROUTINE:
#ifdef WIN32
    iENT_LogGlobalUnlock();
#else
    pthread_mutex_unlock(&sLogMutex);
#endif
    return sts;
}

MSG_ID_T iENT_LogCloseHandle(ENT_LOG logHandle)
{
    MSG_ID_T sts = ENT_SYS_NORMAL;
    bool closeStarted = false;

    if(sLogMutexInit == false)
    {
        fprintf(stderr, "Func [%s] Line [%d],Uninitialized,please call ENT_LogInit.\n", "iENT_LogCloseHandle", __LINE__);
        return ENT_LOG_NOT_INITIALIZED;
    }

#ifdef WIN32
    iENT_LogGlobalLock();
#else
    pthread_mutex_lock(&sLogMutex);
#endif

    ENT_LOG_CTX_INTERNAL* log = (ENT_LOG_CTX_INTERNAL*)logHandle;
    if(log == NULL)
    {
        if(iENT_LogDefaultCtx()->isInit)
            log = iENT_LogDefaultCtx();
        else
        {
            sts = ENT_LOG_BAD_HANDLE;
            fprintf(stderr, "Func [%s] Line [%d],arguments is invalid.\n", "iENT_LogCloseHandle", __LINE__);
            goto END_OF_ROUTINE;
        }
    }
    else if(log->tag != ENTLOG_TAG || log->isInit == false)
    {
        sts = ENT_LOG_BAD_HANDLE;
        fprintf(stderr, "Func [%s] Line [%d],arguments is invalid.\n", "iENT_LogCloseHandle", __LINE__);
        goto END_OF_ROUTINE;
    }
    if(iENT_LogStateGet(log) == ENT_LOG_HANDLE_CLOSING_E && log->closeAttemptActive)
    {
        sts = ENT_LOG_IN_USE;
        goto END_OF_ROUTINE;
    }
    if(iENT_LogStateGet(log) != ENT_LOG_HANDLE_ACTIVE_E &&
       iENT_LogStateGet(log) != ENT_LOG_HANDLE_CLOSING_E)
    {
        sts = ENT_LOG_BAD_HANDLE;
        goto END_OF_ROUTINE;
    }

    if(iENT_LogStateGet(log) == ENT_LOG_HANDLE_ACTIVE_E)
    {
        iENT_LogStateSet(log, ENT_LOG_HANDLE_CLOSING_E);
    }
    log->closeAttemptActive = true;
    closeStarted = true;
    while(iENT_LogActiveGet(log) > 0)
    {
#ifdef WIN32
        SleepConditionVariableSRW(&log->closeCv, &sLogMutex, INFINITE, 0);
#else
        pthread_cond_wait(&log->closeCv, &sLogMutex);
#endif
    }

    log->isBuffer = false;
    iENT_LogFastFlagSet(&log->isBufferFast, 0);
    log->isDebug = false;
    iENT_LogFastFlagSet(&log->isDebugFast, 0);

    sts = iENT_LogStopBufferThread(log);
    if(sts < 0)
    {
        goto END_OF_ROUTINE;
    }
    iENT_LogFreePoolNodes(log);

    if(log->logFp)
    {
        if(fclose(log->logFp) != 0)
        {
            log->logFp = NULL;
            sts = ENT_LOG_IO_FAILED;
            goto END_OF_ROUTINE;
        }
        log->logFp = NULL;
    }

    log->isInit = false;
    iENT_LogStateSet(log, ENT_LOG_HANDLE_CLOSED_E);
#ifdef WIN32
    DeleteCriticalSection(&log->cs);
#else
    pthread_mutex_destroy(&log->cs);
    pthread_cond_destroy(&log->closeCv);
    pthread_cond_destroy(&log->bufferCv);
#endif

    if(log->ownerCtx != NULL && log->ownerCtx->logHandle == logHandle)
    {
        log->ownerCtx->logHandle = NULL;
    }
    if(log->moduleName)
    {
        free(log->moduleName);
        log->moduleName = NULL;
    }
    if(log->logPath)
    {
        free(log->logPath);
        log->logPath = NULL;
    }

    if(log != iENT_LogDefaultCtx())
    {
        free(log);
    }
    sLogNum--;

END_OF_ROUTINE:
    if(sts < 0 && closeStarted)
    {
        log->closeAttemptActive = false;
    }
#ifdef WIN32
    iENT_LogGlobalUnlock();
#else
    pthread_mutex_unlock(&sLogMutex);
#endif

    return sts;
}
