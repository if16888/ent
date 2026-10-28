/*-----------------------------------------------------------------------------
 *   Copyright 2019 Fei Li
 *-----------------------------------------------------------------------------
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

#include "ient_comm.h"
#include "ent_log.h"
#include "ient_log.h"

MSG_ID_T ENT_LogSetOption(ENT_LOG logHandle, ENT_LOG_OPTIONS_E option, const void* arg)
{
    ENT_LOG_CTX_INTERNAL* log = NULL;
    size_t len;
    MSG_ID_T sts = ENT_SYS_NORMAL;
    MSG_ID_T bufferSts = ENT_SYS_NORMAL;
    bool startBufferThread = false;
    bool acquired = false;

    if(sLogMutexInit == false)
    {
        fprintf(stderr, "Func [%s] Line [%d],Uninitialized,please call ENT_LogInit.\n", "ENT_LogSetOption", __LINE__);
        return ENT_LOG_NOT_INITIALIZED;
    }

    if(arg == NULL)
    {
        fprintf(stderr, "Func [%s] Line [%d],arguments is invalid.\n", "ENT_LogSetOption", __LINE__);
        return ENT_LOG_BAD_ARGUMENT;
    }

    sts = iENT_LogAcquireWriter(&log, logHandle);
    if(sts < 0)
    {
        return sts;
    }
    acquired = true;

    iENT_LogIoLock(log);
#ifdef _WIN32
    EnterCriticalSection(&log->cs);
#else
    pthread_mutex_lock(&log->cs);
#endif
    if(iENT_LogStateGet(log) != ENT_LOG_HANDLE_ACTIVE_E)
    {
        sts = ENT_LOG_IN_USE;
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

            newPath = ENT_StrDup(newPathArg);
            if(newPath == NULL)
            {
                sts = ENT_LOG_ALLOC_FAILED;
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
            /* Reopen on the next write even if the calendar day is unchanged.
             * iENT_LogRollCheck closes the old FILE under the I/O lock. */
            log->nextCreate = 0;
            break;
        }

        case ENT_LOG_MAX_E:
        {
            int maxNum = *(int*)arg;
            if(maxNum < 0)
            {
                sts = ENT_LOG_BAD_ARGUMENT;
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
                sts = ENT_LOG_BAD_ARGUMENT;
                break;
            }
            iENT_LogLevelSet(log, level);
            break;
        }

        case ENT_LOG_FLUSH_BATCH_E:
        {
            int flushBatch = *(int*)arg;
            if(flushBatch <= 0)
            {
                sts = ENT_LOG_BAD_ARGUMENT;
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
                sts = ENT_LOG_BAD_ARGUMENT;
                break;
            }
            log->flushIntervalMs = flushIntervalMs;
            log->lastFlushMs = iENT_LogNowMs();
            break;
        }

        case ENT_LOG_BUFFER_LIMIT_E:
        {
            size_t limitBytes = *(const size_t*)arg;
            if(limitBytes == 0)
            {
                sts = ENT_LOG_BAD_ARGUMENT;
                break;
            }
            log->bufferQueueLimitBytes = limitBytes;
            break;
        }

        default:
            sts = ENT_LOG_BAD_ARGUMENT;
            break;
    }
END_OF_ROUTINE:
#ifdef _WIN32
    LeaveCriticalSection(&log->cs);
#else
    pthread_mutex_unlock(&log->cs);
#endif
    if(sts == ENT_SYS_NORMAL && startBufferThread)
    {
        bufferSts = iENT_LogStartBufferThread(log);
        if(bufferSts < 0)
        {
            sts = bufferSts;
        }
    }
    iENT_LogIoUnlock(log);
    if(acquired)
    {
        iENT_LogReleaseWriter(log);
    }
    return sts;
}

MSG_ID_T ENT_LogGetBufferStats(ENT_LOG logHandle, ENT_LOG_BUFFER_STATS_T* stats)
{
    ENT_LOG_CTX_INTERNAL* log = NULL;
    MSG_ID_T sts = ENT_SYS_NORMAL;

    if(stats == NULL)
    {
        return ENT_LOG_BAD_ARGUMENT;
    }
    if(sLogMutexInit == false)
    {
        return ENT_LOG_NOT_INITIALIZED;
    }

    sts = iENT_LogAcquireWriter(&log, logHandle);
    if(sts < 0)
    {
        return sts;
    }

#ifdef _WIN32
    EnterCriticalSection(&log->cs);
#else
    pthread_mutex_lock(&log->cs);
#endif
    stats->queuedBytes = log->bufferQueuedBytes;
    stats->limitBytes = log->bufferQueueLimitBytes;
    stats->rejectedMessages = log->bufferRejectedMessages;
#ifdef _WIN32
    LeaveCriticalSection(&log->cs);
#else
    pthread_mutex_unlock(&log->cs);
#endif

    iENT_LogReleaseWriter(log);
    return ENT_SYS_NORMAL;
}
