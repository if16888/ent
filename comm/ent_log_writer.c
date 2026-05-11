/*-----------------------------------------------------------------------------
 *   Copyright 2019 Fei Li
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *-----------------------------------------------------------------------------
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include "ient_comm.h"
#ifdef WIN32
#include <sys/timeb.h>
#include <Windows.h>
#include <tchar.h>
#include <limits.h>
#else
#include <pthread.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <limits.h>
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#include "ent_log.h"
#include "ient_log.h"

static const char* sLogLevelStr[] = {
    "FATAL",
    "ERROR",
    "WARN",
    "INFO",
    "DEBUG"
};

static MSG_ID_T iENT_LogRollCheck(ENT_LOG logHandle, time_t nowTime)
{
    ENT_LOG_CTX_INTERNAL* log = (ENT_LOG_CTX_INTERNAL*)logHandle;
    const static char* tmFormat = "%Y%m%d";
    char dayStr[9];
    struct tm nowTm;

    if(log == NULL)
    {
        log = iENT_LogDefaultCtx();
    }

    if(log->nextCreate > nowTime)
        return ENT_LOG_NON_FATAL;

    if(log->logFp == NULL || log->nextCreate + 86400 < nowTime)
    {
#ifdef WIN32
        localtime_s(&nowTm, &nowTime);
#else
        localtime_r(&nowTime, &nowTm);
#endif
        strftime(dayStr, 9, tmFormat, &nowTm);
        nowTm.tm_hour = 0;
        nowTm.tm_min = 0;
        nowTm.tm_sec = 0;
        log->nextCreate = mktime(&nowTm) + 86400;
    }
    else
    {
#ifdef WIN32
        localtime_s(&nowTm, &log->nextCreate);
#else
        localtime_r(&log->nextCreate, &nowTm);
#endif
        strftime(dayStr, 9, tmFormat, &nowTm);
        log->nextCreate += 86400;
    }

    if(log->logFp != NULL)
    {
        if(fclose(log->logFp) != 0)
        {
            log->logFp = NULL;
            return ENT_LOG_IO_FAILED;
        }
        log->logFp = NULL;
    }

    {
        char fileName[PATH_MAX];
        memset(fileName, 0, sizeof(fileName));
        if(log->logPath == NULL)
        {
            snprintf(fileName, sizeof(fileName) - 1, "%s_%s.log", log->moduleName, dayStr);
        }
        else
        {
            snprintf(fileName, sizeof(fileName) - 1, "%s%s%s_%s.log", log->logPath, ENT_FILE_SEP, log->moduleName, dayStr);
        }
        fileName[sizeof(fileName) - 1] = '\0';

        log->logFp = ENT_FOpen(fileName, "a");
        if(log->logFp == NULL)
        {
            fprintf(stderr, "Func [%s] Line [%d],The file %s  was not opened\n", "iENT_LogRollCheck", __LINE__, fileName);
            return ENT_LOG_IO_FAILED;
        }
        else
        {
            setvbuf(log->logFp, NULL, _IOFBF, 64 * 1024);
        }

        if(log->maxNum > 0)
        {
            time_t deleteTime;
            deleteTime = log->nextCreate - (log->maxNum + 1) * 86400;
#ifdef WIN32
            localtime_s(&nowTm, &deleteTime);
#else
            localtime_r(&deleteTime, &nowTm);
#endif
            strftime(dayStr, 9, tmFormat, &nowTm);
            memset(fileName, 0, sizeof(fileName));
            if(log->logPath == NULL)
                snprintf(fileName, sizeof(fileName) - 1, "%s_%s.log", log->moduleName, dayStr);
            else
                snprintf(fileName, sizeof(fileName) - 1, "%s%s%s_%s.log", log->logPath, ENT_FILE_SEP, log->moduleName, dayStr);

            fileName[sizeof(fileName) - 1] = '\0';
#ifdef WIN32
            DeleteFileA(fileName);
#else
            unlink(fileName);
#endif
        }
    }

    return ENT_SYS_NORMAL;
}

MSG_ID_T iENT_LogFormatMessage(const char* format,
                               va_list va_args,
                               char* stackBuf,
                               size_t stackBufLen,
                               char** msgBuf,
                               size_t* msgLen)
{
    int requiredLen = 0;
    int writeLen = 0;
    va_list writeArgs;
    char* targetBuf = stackBuf;

    if(format == NULL || stackBuf == NULL || stackBufLen == 0 || msgBuf == NULL || msgLen == NULL)
    {
        return ENT_LOG_BAD_ARGUMENT;
    }

    va_copy(writeArgs, va_args);
    writeLen = vsnprintf(stackBuf, stackBufLen, format, writeArgs);
    va_end(writeArgs);
    if(writeLen >= 0 && (size_t)writeLen < stackBufLen)
    {
        *msgBuf = stackBuf;
        *msgLen = (size_t)writeLen;
        return ENT_SYS_NORMAL;
    }

    if(writeLen >= 0)
    {
        requiredLen = writeLen;
    }
    else
    {
#ifdef WIN32
        va_list sizeArgs;
        va_copy(sizeArgs, va_args);
        requiredLen = _vscprintf(format, sizeArgs);
        va_end(sizeArgs);
#else
        return ENT_LOG_FORMAT_FAILED;
#endif
    }

    if(requiredLen < 0)
    {
        return ENT_LOG_FORMAT_FAILED;
    }

    if((size_t)requiredLen + 1 > stackBufLen)
    {
        targetBuf = (char*)malloc((size_t)requiredLen + 1);
        if(targetBuf == NULL)
        {
            return ENT_LOG_ALLOC_FAILED;
        }
    }

    va_copy(writeArgs, va_args);
    if(vsnprintf(targetBuf, (size_t)requiredLen + 1, format, writeArgs) < 0)
    {
        va_end(writeArgs);
        if(targetBuf != stackBuf)
        {
            free(targetBuf);
        }
        return ENT_LOG_FORMAT_FAILED;
    }
    va_end(writeArgs);

    *msgBuf = targetBuf;
    *msgLen = (size_t)requiredLen;
    return ENT_SYS_NORMAL;
}

MSG_ID_T iENT_LogFlushMaybe(ENT_LOG_CTX_INTERNAL* log, FILE* fp, bool forceFlush)
{
    long long nowMs = 0;
    int flushBatch = 0;
    int flushIntervalMs = 0;

    if(log == NULL || fp == NULL)
    {
        return ENT_LOG_BAD_ARGUMENT;
    }

    nowMs = iENT_LogNowMs();
    flushBatch = log->flushBatch > 0 ? log->flushBatch : 256;
    flushIntervalMs = log->flushIntervalMs;

    if(forceFlush || !log->isBuffer)
    {
        if(fflush(fp) != 0)
        {
            return ENT_LOG_IO_FAILED;
        }
        log->pendingFlushes = 0;
        log->lastFlushMs = nowMs;
        return ENT_SYS_NORMAL;
    }

    log->pendingFlushes++;
    if(log->lastFlushMs == 0)
    {
        log->lastFlushMs = nowMs;
    }

    if(log->pendingFlushes >= flushBatch ||
       (flushIntervalMs > 0 && nowMs - log->lastFlushMs >= (long long)flushIntervalMs))
    {
        if(fflush(fp) != 0)
        {
            return ENT_LOG_IO_FAILED;
        }
        log->pendingFlushes = 0;
        log->lastFlushMs = nowMs;
    }

    return ENT_SYS_NORMAL;
}

int iENT_LogFastFlagGet(
#ifdef WIN32
    const volatile LONG* flag
#else
    const volatile int* flag
#endif
)
{
#ifdef WIN32
    return InterlockedCompareExchange((volatile LONG*)flag, 0, 0) != 0;
#else
    return __sync_val_compare_and_swap((volatile int*)flag, 0, 0) != 0;
#endif
}

void iENT_LogFastFlagSet(
#ifdef WIN32
    volatile LONG* flag,
#else
    volatile int* flag,
#endif
    int value)
{
#ifdef WIN32
    InterlockedExchange(flag, value ? 1 : 0);
#else
    __sync_lock_test_and_set(flag, value ? 1 : 0);
#endif
}

long long iENT_LogNowMs(void)
{
#ifdef WIN32
    return (long long)GetTickCount64();
#else
    struct timespec ts;
    if(clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        return (long long)tv.tv_sec * 1000LL + (long long)tv.tv_usec / 1000LL;
    }
    return (long long)ts.tv_sec * 1000LL + (long long)ts.tv_nsec / 1000000LL;
#endif
}

bool iENT_LogBufferReady(const ENT_LOG_CTX_INTERNAL* log)
{
    return log != NULL && log->isBuffer && log->bufferThreadStarted && !log->bufferThreadStop;
}

ENT_LOG_MSG_NODE* iENT_LogAllocNode(size_t msgCap, bool pooled)
{
    ENT_LOG_MSG_NODE* node = NULL;
    size_t allocSize = sizeof(ENT_LOG_MSG_NODE) + msgCap;

    node = (ENT_LOG_MSG_NODE*)malloc(allocSize);
    if(node == NULL)
    {
        return NULL;
    }

    node->next = NULL;
    node->rollTime = 0;
    node->msgLen = 0;
    node->msgCap = msgCap;
    node->forceFlush = false;
    node->pooled = pooled;
    node->msg[0] = '\0';
    return node;
}

void iENT_LogFreePoolNodes(ENT_LOG_CTX_INTERNAL* log)
{
    ENT_LOG_MSG_NODE* node = NULL;
    ENT_LOG_MSG_NODE* next = NULL;

    if(log == NULL)
    {
        return;
    }

    node = log->poolFreeHead;
    log->poolFreeHead = NULL;
    log->poolFreeCount = 0;
    while(node != NULL)
    {
        next = node->next;
        free(node);
        node = next;
    }
}

MSG_ID_T iENT_LogFormatPrefix(ENT_LOG_LEV_E logLevel,
                              char* prefixBuf,
                              size_t prefixBufLen,
                              size_t* prefixLen,
                              time_t* rollTime)
{
    int writeLen = 0;
#ifdef WIN32
    struct _timeb nowTmb;
    struct tm nowTm;

    if(prefixBuf == NULL || prefixLen == NULL || rollTime == NULL)
    {
        return ENT_LOG_BAD_ARGUMENT;
    }

    ENT_FTime64(&nowTmb);
    *rollTime = nowTmb.time;
    localtime_s(&nowTm, &nowTmb.time);
    writeLen = _snprintf_s(prefixBuf,
                           prefixBufLen,
                           _TRUNCATE,
                           "[Time %04d%02d%02d %02d:%02d:%02d.%03d] [%5s] [tid %5lu] ",
                           nowTm.tm_year + 1900,
                           nowTm.tm_mon + 1,
                           nowTm.tm_mday,
                           nowTm.tm_hour,
                           nowTm.tm_min,
                           nowTm.tm_sec,
                           nowTmb.millitm,
                           sLogLevelStr[logLevel],
                           (unsigned long)GetCurrentThreadId());
#else
    struct timeval nowTmv;
    struct tm nowTm;

    if(prefixBuf == NULL || prefixLen == NULL || rollTime == NULL)
    {
        return ENT_LOG_BAD_ARGUMENT;
    }

    gettimeofday(&nowTmv, NULL);
    *rollTime = nowTmv.tv_sec;
    localtime_r(&nowTmv.tv_sec, &nowTm);
    writeLen = snprintf(prefixBuf,
                        prefixBufLen,
                        "[Time %04d%02d%02d %02d:%02d:%02d.%03ld] [%5s] [tid %5lu] ",
                        nowTm.tm_year + 1900,
                        nowTm.tm_mon + 1,
                        nowTm.tm_mday,
                        nowTm.tm_hour,
                        nowTm.tm_min,
                        nowTm.tm_sec,
                        (long)(nowTmv.tv_usec / 1000),
                        sLogLevelStr[logLevel],
                        (unsigned long int)pthread_self());
#endif

    if(writeLen < 0 || (size_t)writeLen >= prefixBufLen)
    {
        return ENT_LOG_FORMAT_FAILED;
    }

    *prefixLen = (size_t)writeLen;
    return ENT_SYS_NORMAL;
}

#ifdef WIN32
static DWORD WINAPI iENT_LogBufferThreadMain(LPVOID data)
#else
static void* iENT_LogBufferThreadMain(void* data)
#endif
{
    ENT_LOG_CTX_INTERNAL* log = (ENT_LOG_CTX_INTERNAL*)data;
    ENT_LOG_MSG_NODE* head = NULL;
    ENT_LOG_MSG_NODE* batchHead = NULL;
    ENT_LOG_MSG_NODE* batchTail = NULL;
    ENT_LOG_MSG_NODE* node = NULL;
    ENT_LOG_MSG_NODE* nextNode = NULL;
    int batchCount = 0;

    for(;;)
    {
#ifdef WIN32
        EnterCriticalSection(&log->cs);
        while(log->bufferHead == NULL && !log->bufferThreadStop)
        {
            DWORD waitMs = INFINITE;
            if(log->flushIntervalMs > 0 && log->pendingFlushes > 0)
            {
                long long nowMs = iENT_LogNowMs();
                long long elapsedMs = nowMs - log->lastFlushMs;
                if(log->lastFlushMs == 0 || elapsedMs >= (long long)log->flushIntervalMs)
                {
                    FILE* flushFp = log->logFp == NULL ? stderr : log->logFp;
                    fflush(flushFp);
                    log->pendingFlushes = 0;
                    log->lastFlushMs = nowMs;
                    continue;
                }
                waitMs = (DWORD)(log->flushIntervalMs - (int)elapsedMs);
                if(waitMs == 0)
                {
                    waitMs = 1;
                }
            }
            SleepConditionVariableCS(&log->bufferCv, &log->cs, waitMs);
        }
        if(log->bufferHead == NULL && log->bufferThreadStop)
        {
            log->bufferThreadStarted = false;
            LeaveCriticalSection(&log->cs);
            break;
        }
#else
        pthread_mutex_lock(&log->cs);
        while(log->bufferHead == NULL && !log->bufferThreadStop)
        {
            if(log->flushIntervalMs > 0 && log->pendingFlushes > 0)
            {
                long long nowMs = iENT_LogNowMs();
                long long elapsedMs = nowMs - log->lastFlushMs;
                if(log->lastFlushMs == 0 || elapsedMs >= (long long)log->flushIntervalMs)
                {
                    FILE* flushFp = log->logFp == NULL ? stderr : log->logFp;
                    fflush(flushFp);
                    log->pendingFlushes = 0;
                    log->lastFlushMs = nowMs;
                    continue;
                }
                else
                {
                    int waitMs = log->flushIntervalMs - (int)elapsedMs;
                    struct timespec ts;

                    if(waitMs <= 0)
                    {
                        waitMs = 1;
                    }
                    clock_gettime(CLOCK_REALTIME, &ts);
                    ts.tv_sec += waitMs / 1000;
                    ts.tv_nsec += (long)(waitMs % 1000) * 1000000L;
                    if(ts.tv_nsec >= 1000000000L)
                    {
                        ts.tv_sec++;
                        ts.tv_nsec -= 1000000000L;
                    }
                    pthread_cond_timedwait(&log->bufferCv, &log->cs, &ts);
                    continue;
                }
            }
            pthread_cond_wait(&log->bufferCv, &log->cs);
        }
        if(log->bufferHead == NULL && log->bufferThreadStop)
        {
            log->bufferThreadStarted = false;
            pthread_mutex_unlock(&log->cs);
            break;
        }
#endif
        head = log->bufferHead;
        log->bufferHead = NULL;
        log->bufferTail = NULL;
#ifdef WIN32
        LeaveCriticalSection(&log->cs);
#else
        pthread_mutex_unlock(&log->cs);
#endif

        while(head != NULL)
        {
            ENT_LOG_MSG_NODE* freeHead = NULL;
            batchHead = head;
            batchTail = NULL;
            batchCount = 0;
            while(head != NULL && batchCount < 64)
            {
                batchTail = head;
                head = head->next;
                batchCount++;
            }
            if(batchTail != NULL)
            {
                batchTail->next = NULL;
            }

#ifdef WIN32
            EnterCriticalSection(&log->cs);
#else
            pthread_mutex_lock(&log->cs);
#endif
            for(node = batchHead; node != NULL; node = node->next)
            {
                iENT_LogRollCheck(log, node->rollTime);
                {
                    FILE* fp = log->logFp == NULL ? stderr : log->logFp;
                    fwrite(node->msg, 1, node->msgLen, fp);
                    iENT_LogFlushMaybe(log, fp, node->forceFlush || fp == stderr);
                }
            }

            node = batchHead;
            while(node != NULL)
            {
                nextNode = node->next;
                if(node->pooled && log->poolFreeCount < 512)
                {
                    node->next = log->poolFreeHead;
                    log->poolFreeHead = node;
                    log->poolFreeCount++;
                }
                else
                {
                    node->next = freeHead;
                    freeHead = node;
                }
                node = nextNode;
            }
#ifdef WIN32
            LeaveCriticalSection(&log->cs);
#else
            pthread_mutex_unlock(&log->cs);
#endif

            while(freeHead != NULL)
            {
                nextNode = freeHead->next;
                free(freeHead);
                freeHead = nextNode;
            }
        }
    }

#ifdef WIN32
    return 0;
#else
    return NULL;
#endif
}

MSG_ID_T iENT_LogStartBufferThread(ENT_LOG_CTX_INTERNAL* log)
{
    MSG_ID_T sts = ENT_SYS_NORMAL;

#ifdef WIN32
    EnterCriticalSection(&log->cs);
#else
    pthread_mutex_lock(&log->cs);
#endif
    if(log->bufferThreadStarted)
    {
#ifdef WIN32
        LeaveCriticalSection(&log->cs);
#else
        pthread_mutex_unlock(&log->cs);
#endif
        return ENT_SYS_NORMAL;
    }
    log->bufferThreadStop = false;
#ifdef WIN32
    LeaveCriticalSection(&log->cs);
#else
    pthread_mutex_unlock(&log->cs);
#endif

#ifdef WIN32
    log->bufferThread = CreateThread(NULL, 0, iENT_LogBufferThreadMain, log, 0, NULL);
    if(log->bufferThread == NULL)
    {
        sts = ENT_LOG_THREAD_FAILED;
    }
#else
    if(pthread_create(&log->bufferThread, NULL, iENT_LogBufferThreadMain, log) != 0)
    {
        sts = ENT_LOG_THREAD_FAILED;
    }
#endif

#ifdef WIN32
    EnterCriticalSection(&log->cs);
#else
    pthread_mutex_lock(&log->cs);
#endif
    if(sts == ENT_SYS_NORMAL)
    {
        log->bufferThreadStarted = true;
    }
    else
    {
        log->isBuffer = false;
        iENT_LogFastFlagSet(&log->isBufferFast, 0);
    }
#ifdef WIN32
    LeaveCriticalSection(&log->cs);
#else
    pthread_mutex_unlock(&log->cs);
#endif

    return sts;
}

MSG_ID_T iENT_LogStopBufferThread(ENT_LOG_CTX_INTERNAL* log)
{
    bool shouldJoin = false;
    MSG_ID_T sts = ENT_SYS_NORMAL;

#ifdef WIN32
    EnterCriticalSection(&log->cs);
#else
    pthread_mutex_lock(&log->cs);
#endif
    if(log->bufferThreadStarted)
    {
        log->bufferThreadStop = true;
        shouldJoin = true;
#ifdef WIN32
        WakeAllConditionVariable(&log->bufferCv);
#else
        pthread_cond_broadcast(&log->bufferCv);
#endif
    }
#ifdef WIN32
    LeaveCriticalSection(&log->cs);
#else
    pthread_mutex_unlock(&log->cs);
#endif

    if(!shouldJoin)
    {
        return ENT_SYS_NORMAL;
    }

#ifdef WIN32
    if(WaitForSingleObject(log->bufferThread, INFINITE) == WAIT_FAILED)
    {
        sts = ENT_LOG_THREAD_FAILED;
    }
    else
    {
        CloseHandle(log->bufferThread);
        log->bufferThread = NULL;
    }
#else
    if(pthread_join(log->bufferThread, NULL) != 0)
    {
        sts = ENT_LOG_THREAD_FAILED;
    }
    else
    {
        memset(&log->bufferThread, 0, sizeof(log->bufferThread));
    }
#endif

    return sts;
}

MSG_ID_T iENT_LogQueueMessage(ENT_LOG_CTX_INTERNAL* log,
                              const char* msg,
                              size_t msgLen,
                              time_t rollTime,
                              bool forceFlush)
{
    ENT_LOG_MSG_NODE* node = NULL;
    bool usePool = false;

    if(log == NULL || msg == NULL || msgLen == 0)
    {
        return ENT_LOG_BAD_ARGUMENT;
    }

    if(!iENT_LogFastFlagGet(&log->isBufferFast))
    {
        return ENT_LOG_NON_FATAL;
    }

    usePool = msgLen <= 1024;

#ifdef WIN32
    EnterCriticalSection(&log->cs);
#else
    pthread_mutex_lock(&log->cs);
#endif
    if(!iENT_LogBufferReady(log))
    {
#ifdef WIN32
        LeaveCriticalSection(&log->cs);
#else
        pthread_mutex_unlock(&log->cs);
#endif
        return ENT_LOG_NON_FATAL;
    }

    if(usePool && log->poolFreeHead != NULL)
    {
        node = log->poolFreeHead;
        log->poolFreeHead = node->next;
        if(log->poolFreeCount > 0)
        {
            log->poolFreeCount--;
        }
    }

#ifdef WIN32
    LeaveCriticalSection(&log->cs);
#else
    pthread_mutex_unlock(&log->cs);
#endif

    if(node == NULL)
    {
        if(usePool)
        {
            node = iENT_LogAllocNode(1024, true);
        }
        else
        {
            node = iENT_LogAllocNode(msgLen, false);
        }
        if(node == NULL)
        {
            return ENT_LOG_ALLOC_FAILED;
        }
    }

    node->next = NULL;
    node->rollTime = rollTime;
    node->msgLen = msgLen;
    node->forceFlush = forceFlush;
    memcpy(node->msg, msg, msgLen);
    node->msg[msgLen] = '\0';

#ifdef WIN32
    EnterCriticalSection(&log->cs);
#else
    pthread_mutex_lock(&log->cs);
#endif
    if(!iENT_LogBufferReady(log))
    {
        bool recycled = false;
        if(node->pooled && log->poolFreeCount < 512)
        {
            node->next = log->poolFreeHead;
            log->poolFreeHead = node;
            log->poolFreeCount++;
            recycled = true;
        }
#ifdef WIN32
        LeaveCriticalSection(&log->cs);
#else
        pthread_mutex_unlock(&log->cs);
#endif
        if(!recycled)
        {
            free(node);
        }
        return ENT_LOG_NON_FATAL;
    }

    if(log->bufferTail != NULL)
    {
        log->bufferTail->next = node;
    }
    else
    {
        log->bufferHead = node;
    }
    log->bufferTail = node;
#ifdef WIN32
    WakeConditionVariable(&log->bufferCv);
    LeaveCriticalSection(&log->cs);
#else
    pthread_cond_signal(&log->bufferCv);
    pthread_mutex_unlock(&log->cs);
#endif

    return ENT_SYS_NORMAL;
}

MSG_ID_T iENT_LogVRaw(ENT_LOG_CTX_INTERNAL* log, const char* format, va_list va_args)
{
    char stackBuf[512];
    char* msgBuf = stackBuf;
    size_t msgLen = 0;
    MSG_ID_T sts = ENT_SYS_NORMAL;
    bool useBuffer = false;

    sts = iENT_LogFormatMessage(format, va_args, stackBuf, sizeof(stackBuf), &msgBuf, &msgLen);
    if(sts < 0)
    {
        return sts;
    }

    useBuffer = iENT_LogFastFlagGet(&log->isBufferFast) ? true : false;

    if(useBuffer && iENT_LogQueueMessage(log, msgBuf, msgLen, time(NULL), false) == 0)
    {
        if(msgBuf != stackBuf)
        {
            free(msgBuf);
        }
        return ENT_SYS_NORMAL;
    }

#ifdef WIN32
    EnterCriticalSection(&log->cs);
#else
    pthread_mutex_lock(&log->cs);
#endif

    time_t nowTime = time(NULL);
    sts = iENT_LogRollCheck(log, nowTime);
    if(sts < 0)
    {
        goto END_OF_ROUTINE;
    }

    FILE* fp = log->logFp == NULL ? stderr : log->logFp;

    if(fwrite(msgBuf, 1, msgLen, fp) != msgLen)
    {
        sts = ENT_LOG_IO_FAILED;
        goto END_OF_ROUTINE;
    }
    sts = iENT_LogFlushMaybe(log, fp, fp == stderr);
    if(sts < 0)
    {
        goto END_OF_ROUTINE;
    }
#ifdef WIN32
    LeaveCriticalSection(&log->cs);
#else
    pthread_mutex_unlock(&log->cs);
#endif

END_OF_ROUTINE:
#ifdef WIN32
    if(sts < 0)
    {
        LeaveCriticalSection(&log->cs);
    }
#else
    if(sts < 0)
    {
        pthread_mutex_unlock(&log->cs);
    }
#endif

    if(msgBuf != stackBuf)
    {
        free(msgBuf);
    }

    return sts < 0 ? sts : ENT_SYS_NORMAL;
}

MSG_ID_T iENT_LogVPrint(ENT_LOG_CTX_INTERNAL* log, ENT_LOG_LEV_E logLevel, const char* format, va_list va_args)
{
    char prefixBuf[96];
    char stackBuf[512];
    char* msgBuf = stackBuf;
    size_t msgLen = 0;
    size_t prefixLen = 0;
    char lineStackBuf[1024];
    char* lineBuf = lineStackBuf;
    size_t lineLen = 0;
    time_t rollTime = 0;
    MSG_ID_T sts = ENT_SYS_NORMAL;
    bool debugMirror = false;
    bool useBuffer = false;

    if(iENT_LogFormatMessage(format, va_args, stackBuf, sizeof(stackBuf), &msgBuf, &msgLen) < 0)
    {
        return ENT_LOG_FORMAT_FAILED;
    }

    if(iENT_LogFormatPrefix(logLevel, prefixBuf, sizeof(prefixBuf), &prefixLen, &rollTime) < 0)
    {
        if(msgBuf != stackBuf)
        {
            free(msgBuf);
        }
        return ENT_LOG_FORMAT_FAILED;
    }

    lineLen = prefixLen + msgLen;
    if(lineLen + 1 > sizeof(lineStackBuf))
    {
        lineBuf = (char*)malloc(lineLen + 1);
        if(lineBuf == NULL)
        {
            if(msgBuf != stackBuf)
            {
                free(msgBuf);
            }
            return ENT_LOG_ALLOC_FAILED;
        }
    }
    memcpy(lineBuf, prefixBuf, prefixLen);
    memcpy(lineBuf + prefixLen, msgBuf, msgLen);
    lineBuf[lineLen] = '\0';

    debugMirror = iENT_LogFastFlagGet(&log->isDebugFast) ? true : false;
    if(debugMirror)
    {
        fwrite(lineBuf, 1, lineLen, stdout);
    }

    useBuffer = iENT_LogFastFlagGet(&log->isBufferFast) ? true : false;
    if(useBuffer)
    {
        if(iENT_LogQueueMessage(log, lineBuf, lineLen, rollTime, logLevel <= LOG_LEV_ERROR_E) == 0)
        {
            if(lineBuf != lineStackBuf)
            {
                free(lineBuf);
            }
            if(msgBuf != stackBuf)
            {
                free(msgBuf);
            }
            return ENT_SYS_NORMAL;
        }
    }

#ifdef WIN32
    EnterCriticalSection(&log->cs);
#else
    pthread_mutex_lock(&log->cs);
#endif

    sts = iENT_LogRollCheck(log, rollTime);
    if(sts < 0)
    {
        goto END_OF_ROUTINE;
    }
    FILE* fp = log->logFp == NULL ? stderr : log->logFp;

    if(fwrite(lineBuf, 1, lineLen, fp) != lineLen)
    {
        sts = ENT_LOG_IO_FAILED;
        goto END_OF_ROUTINE;
    }
    sts = iENT_LogFlushMaybe(log, fp, fp == stderr || logLevel <= LOG_LEV_ERROR_E);
    if(sts < 0)
    {
        goto END_OF_ROUTINE;
    }
#ifdef WIN32
    LeaveCriticalSection(&log->cs);
#else
    pthread_mutex_unlock(&log->cs);
#endif

END_OF_ROUTINE:
#ifdef WIN32
    if(sts < 0)
    {
        LeaveCriticalSection(&log->cs);
    }
#else
    if(sts < 0)
    {
        pthread_mutex_unlock(&log->cs);
    }
#endif

    if(lineBuf != lineStackBuf)
    {
        free(lineBuf);
    }
    if(msgBuf != stackBuf)
    {
        free(msgBuf);
    }

    return sts < 0 ? sts : ENT_SYS_NORMAL;
}
