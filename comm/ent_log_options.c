/*-----------------------------------------------------------------------------
 *   Copyright 2019 Fei Li
 *-----------------------------------------------------------------------------
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef WIN32
#include <Windows.h>
#else
#include <pthread.h>
#endif

#include "ent_log.h"
#include "ient_log.h"

MSG_ID_T ENT_LogSetOption(ENT_LOG logHandle, ENT_LOG_OPTIONS_E option, const void* arg)
{
    ENT_LOG_CTX_INTERNAL* log = (ENT_LOG_CTX_INTERNAL*)logHandle;
    size_t len;
    MSG_ID_T sts = 0;
    MSG_ID_T bufferSts = 0;
    bool startBufferThread = false;

    if(sLogMutexInit == false)
    {
        fprintf(stderr, "Func [%s] Line [%d],Uninitialized,please call ENT_LogInit.\n", "ENT_LogSetOption", __LINE__);
        return ENT_LOG_RC_ERROR;
    }

    if(arg == NULL)
    {
        fprintf(stderr, "Func [%s] Line [%d],arguments is invalid.\n", "ENT_LogSetOption", __LINE__);
        return ENT_LOG_RC_ERROR;
    }

    if(log == NULL)
    {
        if(iENT_LogDefaultCtx()->isInit)
            log = iENT_LogDefaultCtx();
        else
        {
            fprintf(stderr, "Func [%s] Line [%d],arguments is invalid.\n", "ENT_LogSetOption", __LINE__);
            return ENT_LOG_RC_INVALID_HANDLE;
        }
    }
    else if(log->tag != ENTLOG_TAG || log->isInit == false)
    {
        fprintf(stderr, "Func [%s] Line [%d],arguments is invalid.\n", "ENT_LogSetOption", __LINE__);
        return ENT_LOG_RC_INVALID_HANDLE;
    }

#ifdef WIN32
    EnterCriticalSection(&log->cs);
#else
    pthread_mutex_lock(&log->cs);
#endif
    if(iENT_LogStateGet(log) != ENT_LOG_HANDLE_ACTIVE_E)
    {
        sts = ENT_LOG_RC_IN_USE;
        goto END_OF_ROUTINE;
    }

    switch(option)
    {
        case ENT_LOG_DEBUG_E:
            log->isDebug = *(bool*)arg;
            iENT_LogFastFlagSet(&log->isDebugFast, log->isDebug ? 1 : 0);
            break;

        case ENT_LOG_PATH_E:
        {
            char* newPath = NULL;
            const char* newPathArg = (const char*)arg;

            newPath = strdup(newPathArg);
            if(newPath == NULL)
            {
                sts = ENT_LOG_RC_IN_USE;
                fprintf(stderr, "Func [%s] Line [%d],strdup failed.\n", "ENT_LogSetOption", __LINE__);
                goto END_OF_ROUTINE;
            }
            len = strlen(newPath);
            if(len > 0 && (newPath[len - 1] == '\\' || newPath[len - 1] == '/'))
            {
                newPath[len - 1] = '\0';
            }
            sts = iENT_LogPathCheck(newPath);
            if(sts < 0)
            {
                free(newPath);
                goto END_OF_ROUTINE;
            }
            if(log->logPath)
            {
                free(log->logPath);
                log->logPath = NULL;
            }
            log->logPath = newPath;
            break;
        }

        case ENT_LOG_MAX_E:
        {
            int maxNum = *(int*)arg;
            if(maxNum < 0)
            {
                sts = ENT_LOG_RC_ERROR;
                break;
            }
            log->maxNum = maxNum;
            break;
        }

        case ENT_LOG_BUFFER_E:
            log->isBuffer = *(bool*)arg;
            iENT_LogFastFlagSet(&log->isBufferFast, log->isBuffer ? 1 : 0);
            log->pendingFlushes = 0;
            log->lastFlushMs = iENT_LogNowMs();
            startBufferThread = log->isBuffer && !log->bufferThreadStarted;
            break;

        case ENT_LOG_LEVEL_E:
        {
            ENT_LOG_LEV_E level = *(ENT_LOG_LEV_E*)arg;
            if(level < LOG_LEV_FATAL_E || level > LOG_LEV_DEBUG_E)
            {
                sts = ENT_LOG_RC_ERROR;
                break;
            }
            log->logLevel = level;
            break;
        }

        case ENT_LOG_FLUSH_BATCH_E:
        {
            int flushBatch = *(int*)arg;
            if(flushBatch <= 0)
            {
                sts = ENT_LOG_RC_ERROR;
                break;
            }
            log->flushBatch = flushBatch;
            break;
        }

        case ENT_LOG_FLUSH_INTERVAL_E:
        {
            int flushIntervalMs = *(int*)arg;
            if(flushIntervalMs < 0)
            {
                sts = ENT_LOG_RC_ERROR;
                break;
            }
            log->flushIntervalMs = flushIntervalMs;
            log->lastFlushMs = iENT_LogNowMs();
            break;
        }

        default:
            break;
    }
END_OF_ROUTINE:
#ifdef WIN32
    LeaveCriticalSection(&log->cs);
#else
    pthread_mutex_unlock(&log->cs);
#endif
    if(sts == 0 && startBufferThread)
    {
        bufferSts = iENT_LogStartBufferThread(log);
        if(bufferSts < 0)
        {
            sts = bufferSts;
        }
    }
    return sts;
}
