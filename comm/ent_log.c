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
#ifdef WIN32
#include <sys/timeb.h>
#include <Windows.h>
#include <tchar.h>
#endif

#ifndef WIN32
#include <pthread.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#endif

#include <time.h>
#include "ent_log.h"
#include "ient_comm.h"



#define DEF_MAX_NUM_LOG  15
#define ENT_LOG_FLUSH_BATCH 256
#define ENT_LOG_WRITE_BATCH 64
#define ENT_LOG_FILE_BUFFER_SIZE (64 * 1024)
#define ENT_LOG_POOL_MSG_SIZE 1024
#define ENT_LOG_POOL_MAX_FREE_NODES 512

typedef struct ENT_LOG_MSG_NODE_TAG
{
    struct ENT_LOG_MSG_NODE_TAG* next;
    time_t                       rollTime;
    size_t                       msgLen;
    size_t                       msgCap;
    bool                         forceFlush;
    bool                         pooled;
    char                         msg[1];
} ENT_LOG_MSG_NODE;

#ifdef WIN32
static CRITICAL_SECTION sLogMutex;
#pragma warning(disable : 4996)
#else
static pthread_mutex_t  sLogMutex;
#define _MAX_PATH PATH_MAX
#define _snprintf snprintf
#endif

static int              sLogNum = 0;
volatile static bool    sLogMutexInit = false;


#define ENTLOG_TAG (0x6AFEFE6A)
typedef struct
{
    unsigned int     tag;//check tag
    bool             isInit;//
    bool             isDebug;//
    bool             isBuffer;
    ENT_LOG_LEV_E    logLevel;//log level
    FILE*            logFp;
#ifdef WIN32
    CRITICAL_SECTION cs;
    CONDITION_VARIABLE closeCv;
    CONDITION_VARIABLE bufferCv;
    HANDLE            bufferThread;
    volatile LONG    closing;
    volatile LONG    activeWriters;
    volatile LONG    isDebugFast;
    volatile LONG    isBufferFast;
#else
    pthread_mutex_t  cs;
    pthread_cond_t   closeCv;
    pthread_cond_t   bufferCv;
    pthread_t        bufferThread;
    volatile int     closing;
    volatile int     activeWriters;
    volatile int     isDebugFast;
    volatile int     isBufferFast;
#endif
    bool             bufferThreadStarted;
    bool             bufferThreadStop;
    int              pendingFlushes;
    int              flushBatch;
    int              flushIntervalMs;
    long long        lastFlushMs;
    ENT_LOG_MSG_NODE* bufferHead;
    ENT_LOG_MSG_NODE* bufferTail;
    ENT_LOG_MSG_NODE* poolFreeHead;
    int              poolFreeCount;
    char*            moduleName;
    char*            logPath;
    int              maxNum;
    time_t           nextCreate;
} ENT_LOG_CTX;

static const char* sLogLevelStr[]={
                "FATAL",
                "ERROR",
                "WARN",
                "INFO",
                "DEBUG"};

static ENT_LOG_CTX sDefLog;
            
static MSG_ID_T iENT_LogPathCheck(const char* path);
static MSG_ID_T iENT_LogFormatMessage(const char* format,
                                      va_list va_args,
                                      char* stackBuf,
                                      size_t stackBufLen,
                                      char** msgBuf,
                                      size_t* msgLen);
static void iENT_LogFlushMaybe(ENT_LOG_CTX* log, FILE* fp, bool forceFlush);
static int iENT_LogIsClosing(const ENT_LOG_CTX* log);
static void iENT_LogSetClosing(ENT_LOG_CTX* log);
static int iENT_LogActiveGet(const ENT_LOG_CTX* log);
static void iENT_LogActiveInc(ENT_LOG_CTX* log);
static int iENT_LogActiveDec(ENT_LOG_CTX* log);
static MSG_ID_T iENT_LogFormatPrefix(ENT_LOG_LEV_E logLevel,
                                     char* prefixBuf,
                                     size_t prefixBufLen,
                                     size_t* prefixLen,
                                     time_t* rollTime);
static MSG_ID_T iENT_LogStartBufferThread(ENT_LOG_CTX* log);
static void iENT_LogStopBufferThread(ENT_LOG_CTX* log);
static MSG_ID_T iENT_LogQueueMessage(ENT_LOG_CTX* log,
                                     const char* msg,
                                     size_t msgLen,
                                     time_t rollTime,
                                     bool forceFlush);
static int iENT_LogFastFlagGet(
#ifdef WIN32
                               const volatile LONG* flag
#else
                               const volatile int* flag
#endif
                               );
static void iENT_LogFastFlagSet(
#ifdef WIN32
                                volatile LONG* flag,
#else
                                volatile int* flag,
#endif
                                int value);
static long long iENT_LogNowMs(void);
static bool iENT_LogBufferReady(const ENT_LOG_CTX* log);
static ENT_LOG_MSG_NODE* iENT_LogAllocNode(size_t msgCap, bool pooled);
static void iENT_LogFreePoolNodes(ENT_LOG_CTX* log);
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :iENT_LogRollCheck
 *
 * DESCRIPTION :   
 *                 
 *                   
 *
 * COMPLETION
 * STATUS      :  0
 *                Success; Service has completed successfully.           
 *
 *                            
 *
 *-----------------------------------------------------------------------------
 */
#undef  FUNC_NAME
#define FUNC_NAME "iENT_LogRollCheck"
static MSG_ID_T  iENT_LogRollCheck(ENT_LOG logHandle,time_t nowTime)
{
    ENT_LOG_CTX* log = (ENT_LOG_CTX*)logHandle;
    const static char* tmFormat="%Y%m%d";
    char dayStr[9];//4+2+2
    struct tm nowTm;
    
    if(log == NULL)
    {
        log = &sDefLog;
    }
    
    if(log->nextCreate>nowTime)
        return 1;
    
    if(log->logFp == NULL ||
       log->nextCreate+86400<nowTime
        )
    {
    #ifdef WIN32
        localtime_s(&nowTm,&nowTime);
    #else
        localtime_r(&nowTime,&nowTm);
    #endif
        strftime(dayStr, 9,tmFormat, &nowTm);
        nowTm.tm_hour = 0;
        nowTm.tm_min  = 0;
        nowTm.tm_sec  = 0;
        log->nextCreate = mktime(&nowTm)+86400;//next day
    }
    else
    {
    #ifdef WIN32
        localtime_s(&nowTm,&log->nextCreate);
    #else
        localtime_r(&log->nextCreate,&nowTm);
    #endif
        strftime(dayStr, 9,tmFormat, &nowTm);
        log->nextCreate += 86400;//next day    
    }
    
    if(log->logFp!=NULL)
    {
        fclose(log->logFp);
        log->logFp = NULL;
    }
    char fileName[_MAX_PATH];
    memset(fileName,0,_MAX_PATH);
    if(log->logPath==NULL)
    {
        _snprintf(fileName,_MAX_PATH-1,"%s_%s.log",log->moduleName,dayStr);
    }
    else
    {
        _snprintf(fileName,_MAX_PATH-1,"%s%s%s_%s.log",log->logPath,ENT_FILE_SEP,log->moduleName,dayStr);
    }
    
    fileName[_MAX_PATH-1]='\0';
    
    log->logFp = fopen(fileName,"a");
    if(log->logFp==NULL)
    {
        fprintf(stderr,"Func [%s] Line [%d],The file %s  was not opened\n",FUNC_NAME,__LINE__,fileName);
    }
    else
    {
        setvbuf(log->logFp, NULL, _IOFBF, ENT_LOG_FILE_BUFFER_SIZE);
    }
    
    if(log->maxNum>0)
    {
        time_t deleteTime;
        deleteTime = log->nextCreate-(log->maxNum+1)*86400;
    #ifdef WIN32
        localtime_s(&nowTm,&deleteTime);
    #else
        localtime_r(&deleteTime,&nowTm);
    #endif
        strftime(dayStr, 9,tmFormat, &nowTm);
        memset(fileName,0,_MAX_PATH);
        if(log->logPath==NULL)
            _snprintf(fileName,_MAX_PATH-1,"%s_%s.log",log->moduleName,dayStr);
        else
            _snprintf(fileName,_MAX_PATH-1,"%s%s%s_%s.log",log->logPath,ENT_FILE_SEP,log->moduleName,dayStr);
        
        fileName[_MAX_PATH-1]='\0';

    #ifdef WIN32
        DeleteFileA(fileName);
    #else
        unlink(fileName);
    #endif

    }
    return 0;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :iENT_LogFormatMessage
 *
 * DESCRIPTION :
 *
 *
 * COMPLETION
 * STATUS      :  0
 *                Success; Service has completed successfully.
 *
 *
 *
 *-----------------------------------------------------------------------------
 */
#undef  FUNC_NAME
#define FUNC_NAME "iENT_LogFormatMessage"
static MSG_ID_T iENT_LogFormatMessage(const char* format,
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
        return -1;
    }

    va_copy(writeArgs, va_args);
    writeLen = vsnprintf(stackBuf, stackBufLen, format, writeArgs);
    va_end(writeArgs);
    if(writeLen >= 0 && (size_t)writeLen < stackBufLen)
    {
        *msgBuf = stackBuf;
        *msgLen = (size_t)writeLen;
        return 0;
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
        return -1;
#endif
    }

    if(requiredLen < 0)
    {
        return -1;
    }

    if((size_t)requiredLen + 1 > stackBufLen)
    {
        targetBuf = (char*)malloc((size_t)requiredLen + 1);
        if(targetBuf == NULL)
        {
            return -1;
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
        return -1;
    }
    va_end(writeArgs);

    *msgBuf = targetBuf;
    *msgLen = (size_t)requiredLen;
    return 0;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :iENT_LogFlushMaybe
 *
 * DESCRIPTION :
 *
 *
 * COMPLETION
 * STATUS      :
 *
 *
 *
 *-----------------------------------------------------------------------------
 */
#undef  FUNC_NAME
#define FUNC_NAME "iENT_LogFlushMaybe"
static void iENT_LogFlushMaybe(ENT_LOG_CTX* log, FILE* fp, bool forceFlush)
{
    long long nowMs = 0;
    int flushBatch = 0;
    int flushIntervalMs = 0;

    if(log == NULL || fp == NULL)
    {
        return;
    }

    nowMs = iENT_LogNowMs();
    flushBatch = log->flushBatch > 0 ? log->flushBatch : ENT_LOG_FLUSH_BATCH;
    flushIntervalMs = log->flushIntervalMs;

    if(forceFlush || !log->isBuffer)
    {
        fflush(fp);
        log->pendingFlushes = 0;
        log->lastFlushMs = nowMs;
        return;
    }

    log->pendingFlushes++;
    if(log->lastFlushMs == 0)
    {
        log->lastFlushMs = nowMs;
    }

    if(log->pendingFlushes >= flushBatch ||
       (flushIntervalMs > 0 && nowMs - log->lastFlushMs >= (long long)flushIntervalMs))
    {
        fflush(fp);
        log->pendingFlushes = 0;
        log->lastFlushMs = nowMs;
    }
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :iENT_LogFastFlagGet
 *
 * DESCRIPTION :
 *
 *
 *-----------------------------------------------------------------------------
 */
#undef  FUNC_NAME
#define FUNC_NAME "iENT_LogFastFlagGet"
static int iENT_LogFastFlagGet(
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
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :iENT_LogFastFlagSet
 *
 * DESCRIPTION :
 *
 *
 *-----------------------------------------------------------------------------
 */
#undef  FUNC_NAME
#define FUNC_NAME "iENT_LogFastFlagSet"
static void iENT_LogFastFlagSet(
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
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :iENT_LogNowMs
 *
 * DESCRIPTION :
 *
 *
 *-----------------------------------------------------------------------------
 */
#undef  FUNC_NAME
#define FUNC_NAME "iENT_LogNowMs"
static long long iENT_LogNowMs(void)
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
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :iENT_LogBufferReady
 *
 * DESCRIPTION :
 *
 *
 *-----------------------------------------------------------------------------
 */
#undef  FUNC_NAME
#define FUNC_NAME "iENT_LogBufferReady"
static bool iENT_LogBufferReady(const ENT_LOG_CTX* log)
{
    return log != NULL && log->isBuffer && log->bufferThreadStarted && !log->bufferThreadStop;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :iENT_LogAllocNode
 *
 * DESCRIPTION :
 *
 *
 *-----------------------------------------------------------------------------
 */
#undef  FUNC_NAME
#define FUNC_NAME "iENT_LogAllocNode"
static ENT_LOG_MSG_NODE* iENT_LogAllocNode(size_t msgCap, bool pooled)
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
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :iENT_LogFreePoolNodes
 *
 * DESCRIPTION :
 *
 *
 *-----------------------------------------------------------------------------
 */
#undef  FUNC_NAME
#define FUNC_NAME "iENT_LogFreePoolNodes"
static void iENT_LogFreePoolNodes(ENT_LOG_CTX* log)
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
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :iENT_LogIsClosing
 *
 * DESCRIPTION :
 *
 *
 *-----------------------------------------------------------------------------
 */
#undef  FUNC_NAME
#define FUNC_NAME "iENT_LogIsClosing"
static int iENT_LogIsClosing(const ENT_LOG_CTX* log)
{
#ifdef WIN32
    return InterlockedCompareExchange((volatile LONG*)&log->closing, 0, 0) != 0;
#else
    return __sync_val_compare_and_swap((volatile int*)&log->closing, 0, 0) != 0;
#endif
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :iENT_LogSetClosing
 *
 * DESCRIPTION :
 *
 *
 *-----------------------------------------------------------------------------
 */
#undef  FUNC_NAME
#define FUNC_NAME "iENT_LogSetClosing"
static void iENT_LogSetClosing(ENT_LOG_CTX* log)
{
#ifdef WIN32
    InterlockedExchange(&log->closing, 1);
#else
    __sync_lock_test_and_set(&log->closing, 1);
#endif
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :iENT_LogActiveGet
 *
 * DESCRIPTION :
 *
 *
 *-----------------------------------------------------------------------------
 */
#undef  FUNC_NAME
#define FUNC_NAME "iENT_LogActiveGet"
static int iENT_LogActiveGet(const ENT_LOG_CTX* log)
{
#ifdef WIN32
    return (int)InterlockedCompareExchange((volatile LONG*)&log->activeWriters, 0, 0);
#else
    return __sync_val_compare_and_swap((volatile int*)&log->activeWriters, 0, 0);
#endif
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :iENT_LogActiveInc
 *
 * DESCRIPTION :
 *
 *
 *-----------------------------------------------------------------------------
 */
#undef  FUNC_NAME
#define FUNC_NAME "iENT_LogActiveInc"
static void iENT_LogActiveInc(ENT_LOG_CTX* log)
{
#ifdef WIN32
    InterlockedIncrement(&log->activeWriters);
#else
    __sync_add_and_fetch(&log->activeWriters, 1);
#endif
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :iENT_LogActiveDec
 *
 * DESCRIPTION :
 *
 *
 *-----------------------------------------------------------------------------
 */
#undef  FUNC_NAME
#define FUNC_NAME "iENT_LogActiveDec"
static int iENT_LogActiveDec(ENT_LOG_CTX* log)
{
#ifdef WIN32
    return (int)InterlockedDecrement(&log->activeWriters);
#else
    return __sync_sub_and_fetch(&log->activeWriters, 1);
#endif
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :iENT_LogFormatPrefix
 *
 * DESCRIPTION :
 *
 *
 *-----------------------------------------------------------------------------
 */
#undef  FUNC_NAME
#define FUNC_NAME "iENT_LogFormatPrefix"
static MSG_ID_T iENT_LogFormatPrefix(ENT_LOG_LEV_E logLevel,
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
        return -1;
    }

    _ftime(&nowTmb);
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
        return -1;
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
        return -1;
    }

    *prefixLen = (size_t)writeLen;
    return 0;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :iENT_LogBufferThreadMain
 *
 * DESCRIPTION :
 *
 *
 *-----------------------------------------------------------------------------
 */
#undef  FUNC_NAME
#define FUNC_NAME "iENT_LogBufferThreadMain"
#ifdef WIN32
static DWORD WINAPI iENT_LogBufferThreadMain(LPVOID data)
#else
static void* iENT_LogBufferThreadMain(void* data)
#endif
{
    ENT_LOG_CTX* log = (ENT_LOG_CTX*)data;
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
            while(head != NULL && batchCount < ENT_LOG_WRITE_BATCH)
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
                if(node->pooled && log->poolFreeCount < ENT_LOG_POOL_MAX_FREE_NODES)
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
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :iENT_LogStartBufferThread
 *
 * DESCRIPTION :
 *
 *
 *-----------------------------------------------------------------------------
 */
#undef  FUNC_NAME
#define FUNC_NAME "iENT_LogStartBufferThread"
static MSG_ID_T iENT_LogStartBufferThread(ENT_LOG_CTX* log)
{
    MSG_ID_T sts = 0;

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
        return 0;
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
        sts = -3;
    }
#else
    if(pthread_create(&log->bufferThread, NULL, iENT_LogBufferThreadMain, log) != 0)
    {
        sts = -3;
    }
#endif

#ifdef WIN32
    EnterCriticalSection(&log->cs);
#else
    pthread_mutex_lock(&log->cs);
#endif
    if(sts == 0)
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
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :iENT_LogStopBufferThread
 *
 * DESCRIPTION :
 *
 *
 *-----------------------------------------------------------------------------
 */
#undef  FUNC_NAME
#define FUNC_NAME "iENT_LogStopBufferThread"
static void iENT_LogStopBufferThread(ENT_LOG_CTX* log)
{
    bool shouldJoin = false;

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
        return;
    }

#ifdef WIN32
    WaitForSingleObject(log->bufferThread, INFINITE);
    CloseHandle(log->bufferThread);
    log->bufferThread = NULL;
#else
    pthread_join(log->bufferThread, NULL);
    memset(&log->bufferThread, 0, sizeof(log->bufferThread));
#endif
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :iENT_LogQueueMessage
 *
 * DESCRIPTION :
 *
 *
 *-----------------------------------------------------------------------------
 */
#undef  FUNC_NAME
#define FUNC_NAME "iENT_LogQueueMessage"
static MSG_ID_T iENT_LogQueueMessage(ENT_LOG_CTX* log,
                                     const char* msg,
                                     size_t msgLen,
                                     time_t rollTime,
                                     bool forceFlush)
{
    ENT_LOG_MSG_NODE* node = NULL;
    bool usePool = false;

    if(log == NULL || msg == NULL || msgLen == 0)
    {
        return -1;
    }

    if(!iENT_LogFastFlagGet(&log->isBufferFast))
    {
        return 1;
    }

    usePool = msgLen <= ENT_LOG_POOL_MSG_SIZE;

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
        return 1;
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
            node = iENT_LogAllocNode(ENT_LOG_POOL_MSG_SIZE, true);
        }
        else
        {
            node = iENT_LogAllocNode(msgLen, false);
        }
        if(node == NULL)
        {
            return -1;
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
        if(node->pooled && log->poolFreeCount < ENT_LOG_POOL_MAX_FREE_NODES)
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
        return 1;
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

    return 0;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_LogInit
 *
 * DESCRIPTION :   
 *                 
 *                   
 *
 * COMPLETION
 * STATUS      :  0
 *                Success; Service has completed successfully.           
 *
 *                            
 *
 *-----------------------------------------------------------------------------
 */
#undef  FUNC_NAME
#define FUNC_NAME "ENT_LogInit"
ENT_PUBLIC MSG_ID_T  ENT_LogInit()
{
    if(sLogMutexInit)
    {
        return 1;
    }
#ifdef WIN32
    InitializeCriticalSection(&sLogMutex);
#else
    pthread_mutex_init(&sLogMutex,NULL);
#endif
    fprintf(stderr,"Func [%s] Line [%d],sucessful.\n",FUNC_NAME,__LINE__);
    sLogMutexInit = true;
    return 0;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_LogClose
 *
 * DESCRIPTION :   
 *                 
 *                   
 *
 * COMPLETION
 * STATUS      :  0
 *                Success; Service has completed successfully.           
 *
 *                            
 *
 *-----------------------------------------------------------------------------
 */
#undef  FUNC_NAME
#define FUNC_NAME "ENT_LogClose"
ENT_PUBLIC MSG_ID_T  ENT_LogClose()
{
#ifdef WIN32
    DeleteCriticalSection(&sLogMutex);
#else
    pthread_mutex_destroy(&sLogMutex);
#endif
    
    fprintf(stderr,"Func [%s] Line [%d],sucessful.\n",FUNC_NAME,__LINE__);
    sLogMutexInit = false;
    return 0;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_LogInitHandle
 *
 * DESCRIPTION :   
 *                 
 *                   
 *
 * COMPLETION
 * STATUS      :  0
 *                Success; Service has completed successfully.           
 *
 *                            
 *
 *-----------------------------------------------------------------------------
 */
#undef  FUNC_NAME
#define FUNC_NAME "ENT_LogInitHandle"
ENT_PUBLIC MSG_ID_T  ENT_LogInitHandle(ENT_LOG* pLogHandle,const char* moduleName,const char* logPath)
{
    MSG_ID_T sts = 0;
    size_t   len;
    ENT_LOG_CTX* log;
    
    if(sLogMutexInit==false)
    {
        fprintf(stderr,"Func [%s] Line [%d],Uninitialized,please call ENT_LogInit.\n",FUNC_NAME,__LINE__);
        return -1;
    }
    
#ifdef WIN32
    EnterCriticalSection(&sLogMutex);
#else
    pthread_mutex_lock(&sLogMutex);
#endif
    
    //_ftime(&nowTmb);
    
    if(pLogHandle==NULL)
    {
        log = &sDefLog;
        if(log->isInit)
        {
            sts = 1;
            fprintf(stderr,"Func [%s] Line [%d],The module [%s] log path [%s] was already opened.\n",FUNC_NAME,__LINE__,moduleName,logPath);
            goto END_OF_ROUTINE;
         }      
    }
    else
    {
        log = (ENT_LOG_CTX*)malloc(sizeof(ENT_LOG_CTX));
        if(log == NULL)
        {
            sts = -1;
            fprintf(stderr,"Func [%s] Line [%d],The module [%s] log path [%s] malloc failed.\n",FUNC_NAME,__LINE__,moduleName,logPath);
            goto END_OF_ROUTINE;
        }   
        memset(log,0,sizeof(ENT_LOG_CTX));
    }
    if(moduleName)
        log->moduleName = strdup(moduleName);
    else
        log->moduleName = strdup("default");  
        
    if(logPath)
    {
        log->logPath = strdup(logPath);
        len = strlen(log->logPath);
        if(log->logPath[len-1]=='\\' ||
        log->logPath[len-1]=='/')
        {
            log->logPath[len-1]='\0';
        }
        sts=iENT_LogPathCheck(log->logPath);
    }
    //iENT_LogRollCheck(log,nowTmb.time);
#ifdef WIN32
    InitializeCriticalSection(&log->cs);
    InitializeConditionVariable(&log->closeCv);
    InitializeConditionVariable(&log->bufferCv);
    log->bufferThread = NULL;
#else
    pthread_mutex_init(&log->cs,NULL);
    if(pthread_cond_init(&log->closeCv,NULL) != 0)
    {
        sts = -1;
        fprintf(stderr,"Func [%s] Line [%d],pthread_cond_init failed.\n",FUNC_NAME,__LINE__);
        pthread_mutex_destroy(&log->cs);
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
        if(log!=&sDefLog)
        {
            free(log);
        }
        goto END_OF_ROUTINE;
    }
    if(pthread_cond_init(&log->bufferCv,NULL) != 0)
    {
        sts = -1;
        fprintf(stderr,"Func [%s] Line [%d],pthread_cond_init failed.\n",FUNC_NAME,__LINE__);
        pthread_cond_destroy(&log->closeCv);
        pthread_mutex_destroy(&log->cs);
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
        if(log!=&sDefLog)
        {
            free(log);
        }
        goto END_OF_ROUTINE;
    }
#endif
    log->closing = 0;
    log->activeWriters = 0;
    log->bufferThreadStarted = false;
    log->bufferThreadStop = false;
    log->pendingFlushes = 0;
    log->flushBatch = ENT_LOG_FLUSH_BATCH;
    log->flushIntervalMs = 0;
    log->lastFlushMs = 0;
    log->bufferHead = NULL;
    log->bufferTail = NULL;
    log->poolFreeHead = NULL;
    log->poolFreeCount = 0;
    log->isInit   = true;
    log->isDebug  = false;
    log->isBuffer = false;
    iENT_LogFastFlagSet(&log->isDebugFast, 0);
    iENT_LogFastFlagSet(&log->isBufferFast, 0);
    log->logLevel = LOG_LEV_WARN_E;
    log->maxNum   = DEF_MAX_NUM_LOG;
    log->tag      = ENTLOG_TAG;
    if(pLogHandle)
    {
        *pLogHandle = log;
    }
    sLogNum++;
    
END_OF_ROUTINE:
#ifdef WIN32   
    LeaveCriticalSection(&sLogMutex);
#else
    pthread_mutex_unlock(&sLogMutex);
#endif
    
    return sts;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_LogSetOption
 *
 * DESCRIPTION :   
 *                 
 *                   
 *
 * COMPLETION
 * STATUS      :  0
 *                Success; Service has completed successfully.           
 *                -1
 *                Failed: Arguments is invalid.
 *
 *            
 *
 *-----------------------------------------------------------------------------
 */
#undef  FUNC_NAME
#define FUNC_NAME "ENT_LogSetOption"
ENT_PUBLIC MSG_ID_T  ENT_LogSetOption(ENT_LOG logHandle,ENT_LOG_OPTIONS_E option,const void* arg)
{
    ENT_LOG_CTX* log = (ENT_LOG_CTX*)logHandle;
    size_t       len;
    MSG_ID_T     sts=0;
    MSG_ID_T     bufferSts = 0;
    bool         startBufferThread = false;
    
    if(sLogMutexInit==false)
    {
        fprintf(stderr,"Func [%s] Line [%d],Uninitialized,please call ENT_LogInit.\n",FUNC_NAME,__LINE__);
        return -1;
    }
    
    if(arg == NULL)
    {
        fprintf(stderr,"Func [%s] Line [%d],arguments is invalid.\n",FUNC_NAME,__LINE__);
        return -1;
    }
    
    if(log == NULL)
    {
        if(sDefLog.isInit)
            log = &sDefLog;
        else
        {
            fprintf(stderr,"Func [%s] Line [%d],arguments is invalid.\n",FUNC_NAME,__LINE__);
            return -2;
        }
    }
    else if(log->tag!= ENTLOG_TAG || log->isInit == false)
    {
        fprintf(stderr,"Func [%s] Line [%d],arguments is invalid.\n",FUNC_NAME,__LINE__);
        return -2;
    }

#ifdef WIN32
    EnterCriticalSection(&log->cs);
#else
    pthread_mutex_lock(&log->cs);
#endif
    
    switch(option)
    {
        case ENT_LOG_DEBUG_E:
            log->isDebug = *(bool*)arg;
            iENT_LogFastFlagSet(&log->isDebugFast, log->isDebug ? 1 : 0);
            break;
        
        case ENT_LOG_PATH_E:
            if(log->logPath)
            {
                free(log->logPath);
                log->logPath = NULL;
            }
            log->logPath = strdup((const char*)arg);
			if (log->logPath == NULL)
			{
				sts = -3;
				fprintf(stderr,"Func [%s] Line [%d],strdup failed.\n",FUNC_NAME,__LINE__);
				goto END_OF_ROUTINE;
			}
            len = strlen(log->logPath);
            if(log->logPath[len-1]=='\\' ||
            log->logPath[len-1]=='/')
            {
                log->logPath[len-1]='\0';
            }
            sts=iENT_LogPathCheck(log->logPath);
            if(sts<0)
            {
                if(log->logPath)
                {
                    free(log->logPath);
                    log->logPath=NULL;
                 }
            }
            break;
            
        case ENT_LOG_MAX_E:
            log->maxNum = *(int*)arg;
            break;
        
        case ENT_LOG_BUFFER_E:
            log->isBuffer = *(bool*)arg;
            iENT_LogFastFlagSet(&log->isBufferFast, log->isBuffer ? 1 : 0);
            log->pendingFlushes = 0;
            log->lastFlushMs = iENT_LogNowMs();
            startBufferThread = log->isBuffer && !log->bufferThreadStarted;
            break;
            
        case ENT_LOG_LEVEL_E:
            log->logLevel = *(ENT_LOG_LEV_E*)arg;
            break;

        case ENT_LOG_FLUSH_BATCH_E:
        {
            int flushBatch = *(int*)arg;
            if(flushBatch <= 0)
            {
                sts = -1;
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
                sts = -1;
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
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_LogCloseHandle
 *
 * DESCRIPTION :   
 *                 
 *                   
 *
 * COMPLETION
 * STATUS      :  0
 *                Success; Service has completed successfully.           
 *
 *                            
 *
 *-----------------------------------------------------------------------------
 */
#undef  FUNC_NAME
#define FUNC_NAME "ENT_LogCloseHandle"
ENT_PUBLIC MSG_ID_T ENT_LogCloseHandle(ENT_LOG logHandle)
{
    MSG_ID_T  sts = 0;
    
    if(sLogMutexInit==false)
    {
        fprintf(stderr,"Func [%s] Line [%d],Uninitialized,please call ENT_LogInit.\n",FUNC_NAME,__LINE__);
        return -1;
    }
    
#ifdef WIN32
    EnterCriticalSection(&sLogMutex);
#else
    pthread_mutex_lock(&sLogMutex);
#endif
            
    ENT_LOG_CTX* log = (ENT_LOG_CTX*)logHandle;
    if(log == NULL)
    {
        if(sDefLog.isInit)
            log = &sDefLog;
        else
        {
            sts = -2;
            fprintf(stderr,"Func [%s] Line [%d],arguments is invalid.\n",FUNC_NAME,__LINE__);
            goto END_OF_ROUTINE;
        }
    }
    else if(log->tag!=ENTLOG_TAG || log->isInit == false)
    {
        sts = -2;
        fprintf(stderr,"Func [%s] Line [%d],arguments is invalid.\n",FUNC_NAME,__LINE__);
        goto END_OF_ROUTINE;
    }
    if(iENT_LogIsClosing(log) == 0)
        iENT_LogSetClosing(log);
    while(iENT_LogActiveGet(log) > 0)
    {
#ifdef WIN32
        SleepConditionVariableCS(&log->closeCv, &sLogMutex, INFINITE);
#else
        pthread_cond_wait(&log->closeCv, &sLogMutex);
#endif
    }

    log->isBuffer = false;
    iENT_LogFastFlagSet(&log->isBufferFast, 0);
    log->isDebug = false;
    iENT_LogFastFlagSet(&log->isDebugFast, 0);

    iENT_LogStopBufferThread(log);
    iENT_LogFreePoolNodes(log);

    if(log->logFp)
        fclose(log->logFp);

    log->isInit = false;
    log->logFp  = NULL;
#ifdef WIN32
    DeleteCriticalSection(&log->cs);
#else
    pthread_mutex_destroy(&log->cs);
    pthread_cond_destroy(&log->closeCv);
    pthread_cond_destroy(&log->bufferCv);
#endif
    
    if(log!=&sDefLog)
    {
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
        free(log);
    }
    sLogNum--;
    
END_OF_ROUTINE:
#ifdef WIN32   
    LeaveCriticalSection(&sLogMutex);
#else
    pthread_mutex_unlock(&sLogMutex);
#endif
     
    return sts;  
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :iENT_LogGetCtx
 *
 * DESCRIPTION :   
 *                 
 *                   
 *
 * COMPLETION
 * STATUS      :  0
 *                Success; Service has completed successfully.           
 *
 *                            
 *
 *-----------------------------------------------------------------------------
 */
#undef  FUNC_NAME
#define FUNC_NAME "iENT_LogGetCtx"
static MSG_ID_T iENT_LogGetCtx(ENT_LOG_CTX** logCtx,ENT_LOG logHandle)
{
    ENT_LOG_CTX* log = (ENT_LOG_CTX*)logHandle;
    *logCtx = NULL;
    
    if(sLogMutexInit==false)
    {
        fprintf(stderr,"Func [%s] Line [%d],Uninitialized,please call ENT_LogInit.\n",FUNC_NAME,__LINE__);
        return -1;
    }
    
    if(log == NULL)
    {
        if(sDefLog.isInit)
            log = &sDefLog;
        else
        {
            fprintf(stderr,"Func [%s] Line [%d],arguments is invalid.\n",FUNC_NAME,__LINE__);
            return -2;
        }
    }
    else if(log->tag!=ENTLOG_TAG || log->isInit==false)
    {
        fprintf(stderr,"Func [%s] Line [%d],arguments is invalid.\n",FUNC_NAME,__LINE__);
        return -2;
    }
    *logCtx = log;

    return 0; 
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :iENT_LogAcquireWriter
 *
 * DESCRIPTION :   
 *                 
 *                   
 *
 * COMPLETION
 * STATUS      :  0
 *                Success; Service has completed successfully.           
 *
 *                            
 *
 *-----------------------------------------------------------------------------
 */
#undef  FUNC_NAME
#define FUNC_NAME "iENT_LogAcquireWriter"
static MSG_ID_T iENT_LogAcquireWriter(ENT_LOG_CTX** logCtx,ENT_LOG logHandle)
{
    ENT_LOG_CTX* log = NULL;

    *logCtx = NULL;
    if(sLogMutexInit==false)
    {
        fprintf(stderr,"Func [%s] Line [%d],Uninitialized,please call ENT_LogInit.\n",FUNC_NAME,__LINE__);
        return -1;
    }

#ifdef WIN32
    EnterCriticalSection(&sLogMutex);
#else
    pthread_mutex_lock(&sLogMutex);
#endif

    if(logHandle == NULL)
    {
        if(sDefLog.isInit)
            log = &sDefLog;
        else
        {
#ifdef WIN32
            LeaveCriticalSection(&sLogMutex);
#else
            pthread_mutex_unlock(&sLogMutex);
#endif
            fprintf(stderr,"Func [%s] Line [%d],arguments is invalid.\n",FUNC_NAME,__LINE__);
            return -2;
        }
    }
    else
    {
        log = (ENT_LOG_CTX*)logHandle;
        if(log->tag!=ENTLOG_TAG || log->isInit==false)
        {
#ifdef WIN32
            LeaveCriticalSection(&sLogMutex);
#else
            pthread_mutex_unlock(&sLogMutex);
#endif
            fprintf(stderr,"Func [%s] Line [%d],arguments is invalid.\n",FUNC_NAME,__LINE__);
            return -2;
        }
    }

    if(iENT_LogIsClosing(log))
    {
#ifdef WIN32
        LeaveCriticalSection(&sLogMutex);
#else
        pthread_mutex_unlock(&sLogMutex);
#endif
        return -3;
    }

    iENT_LogActiveInc(log);
    *logCtx = log;
#ifdef WIN32
    LeaveCriticalSection(&sLogMutex);
#else
    pthread_mutex_unlock(&sLogMutex);
#endif
    return 0;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :iENT_LogReleaseWriter
 *
 * DESCRIPTION :   
 *                 
 *                   
 *
 * COMPLETION
 * STATUS      :  0
 *                Success; Service has completed successfully.           
 *
 *                            
 *
 *-----------------------------------------------------------------------------
 */
#undef  FUNC_NAME
#define FUNC_NAME "iENT_LogReleaseWriter"
static void iENT_LogReleaseWriter(ENT_LOG_CTX* log)
{
    if(iENT_LogActiveDec(log) == 0 && iENT_LogIsClosing(log))
    {
#ifdef WIN32
        EnterCriticalSection(&sLogMutex);
        if(iENT_LogIsClosing(log) && iENT_LogActiveGet(log) == 0)
        WakeAllConditionVariable(&log->closeCv);
        LeaveCriticalSection(&sLogMutex);
#else
        pthread_mutex_lock(&sLogMutex);
        if(iENT_LogIsClosing(log) && iENT_LogActiveGet(log) == 0)
        pthread_cond_broadcast(&log->closeCv);
        pthread_mutex_unlock(&sLogMutex);
#endif
    }
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :iENT_LogVRaw
 *
 * DESCRIPTION :   
 *                 
 *                   
 *
 * COMPLETION
 * STATUS      :  0
 *                Success; Service has completed successfully.           
 *
 *                            
 *
 *-----------------------------------------------------------------------------
 */
#undef  FUNC_NAME
#define FUNC_NAME "iENT_LogVRaw"
static MSG_ID_T iENT_LogVRaw(ENT_LOG_CTX* log,const char* format,va_list va_args)
{
    char stackBuf[512];
    char* msgBuf = stackBuf;
    size_t msgLen = 0;
    MSG_ID_T sts = 0;
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
        return 0;
    }

#ifdef WIN32
    EnterCriticalSection(&log->cs);
#else
    pthread_mutex_lock(&log->cs);
#endif

    time_t nowTime = time(NULL);
    iENT_LogRollCheck(log,nowTime);

    FILE* fp = log->logFp==NULL?stderr:log->logFp;

    fwrite(msgBuf, 1, msgLen, fp);
    iENT_LogFlushMaybe(log, fp, fp == stderr);
#ifdef WIN32   
    LeaveCriticalSection(&log->cs);
#else
    pthread_mutex_unlock(&log->cs);
#endif

    if(msgBuf != stackBuf)
    {
        free(msgBuf);
    }

    return 0;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_LogRaw
 *
 * DESCRIPTION :   
 *                 
 *                   
 *
 * COMPLETION
 * STATUS      :  0
 *                Success; Service has completed successfully.           
 *
 *                            
 *
 *-----------------------------------------------------------------------------
 */
#undef  FUNC_NAME
#define FUNC_NAME "ENT_LogRaw"
ENT_PUBLIC MSG_ID_T ENT_LogRaw(ENT_LOG logHandle,const char* format,...)
{
    MSG_ID_T  sts=0;
    ENT_LOG_CTX*  logCtx = NULL;
    
    sts = iENT_LogAcquireWriter(&logCtx,logHandle);
    if(sts<0)
    {
        return sts;
    }
    va_list va_args;
    va_start(va_args,format);
    sts = iENT_LogVRaw(logCtx,format,va_args);
    va_end(va_args);
    iENT_LogReleaseWriter(logCtx);
    return sts;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_LogVPrint
 *
 * DESCRIPTION :   
 *                 
 *                   
 *
 * COMPLETION
 * STATUS      :  0
 *                Success; Service has completed successfully.           
 *
 *                            
 *
 *-----------------------------------------------------------------------------
 */
#undef  FUNC_NAME
#define FUNC_NAME "ENT_LogVPrint"
static MSG_ID_T iENT_LogVPrint(ENT_LOG_CTX* log,ENT_LOG_LEV_E logLevel,const char* format,va_list va_args)
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
    bool debugMirror = false;
    bool useBuffer = false;

    if(iENT_LogFormatMessage(format, va_args, stackBuf, sizeof(stackBuf), &msgBuf, &msgLen) < 0)
    {
        return -1;
    }

    if(iENT_LogFormatPrefix(logLevel, prefixBuf, sizeof(prefixBuf), &prefixLen, &rollTime) < 0)
    {
        if(msgBuf != stackBuf)
        {
            free(msgBuf);
        }
        return -1;
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
            return -1;
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
            return 0;
        }
    }

#ifdef WIN32
    EnterCriticalSection(&log->cs);
#else
    pthread_mutex_lock(&log->cs);
#endif

    iENT_LogRollCheck(log, rollTime);
    FILE* fp = log->logFp==NULL?stderr:log->logFp;

    fwrite(lineBuf, 1, lineLen, fp);
    iENT_LogFlushMaybe(log, fp, fp == stderr || logLevel <= LOG_LEV_ERROR_E);
#ifdef WIN32   
    LeaveCriticalSection(&log->cs);
#else
    pthread_mutex_unlock(&log->cs);
#endif

    if(lineBuf != lineStackBuf)
    {
        free(lineBuf);
    }
    if(msgBuf != stackBuf)
    {
        free(msgBuf);
    }

    return 0;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_LogFatal
 *
 * DESCRIPTION :   
 *                 
 *                   
 *
 * COMPLETION
 * STATUS      :  0
 *                Success; Service has completed successfully.           
 *
 *                            
 *
 *-----------------------------------------------------------------------------
 */
#undef  FUNC_NAME
#define FUNC_NAME "ENT_LogFatal"
ENT_PUBLIC MSG_ID_T ENT_LogFatal(ENT_LOG logHandle,const char* format,...)
{
    MSG_ID_T  sts=0;
    ENT_LOG_CTX*  logCtx = NULL;
    
    sts = iENT_LogAcquireWriter(&logCtx,logHandle);
    if(sts<0)
    {
        return sts;
    }
    if(LOG_LEV_FATAL_E>logCtx->logLevel)
    {
        iENT_LogReleaseWriter(logCtx);
        return 1;
    }
    
    va_list va_args;
    va_start(va_args,format);
    sts = iENT_LogVPrint(logCtx,LOG_LEV_FATAL_E,format,va_args);
    va_end(va_args);
    iENT_LogReleaseWriter(logCtx);
    return sts;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_LogError
 *
 * DESCRIPTION :   
 *                 
 *                   
 *
 * COMPLETION
 * STATUS      :  0
 *                Success; Service has completed successfully.           
 *
 *                            
 *
 *-----------------------------------------------------------------------------
 */
#undef  FUNC_NAME
#define FUNC_NAME "ENT_LogError"
ENT_PUBLIC MSG_ID_T ENT_LogError(ENT_LOG logHandle,const char* format,...)
{
    MSG_ID_T  sts=0;
    ENT_LOG_CTX*  logCtx = NULL;
    
    sts = iENT_LogAcquireWriter(&logCtx,logHandle);
    if(sts<0)
    {
        return sts;
    }
    if(LOG_LEV_ERROR_E>logCtx->logLevel)// log level priority is higher
    {
        iENT_LogReleaseWriter(logCtx);
        return 1;
    }
    
    va_list va_args;
    va_start(va_args,format);
    sts = iENT_LogVPrint(logCtx,LOG_LEV_ERROR_E,format,va_args);
    va_end(va_args);
    iENT_LogReleaseWriter(logCtx);
    return sts;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_LogWarn
 *
 * DESCRIPTION :   
 *                 
 *                   
 *
 * COMPLETION
 * STATUS      :  0
 *                Success; Service has completed successfully.           
 *
 *                            
 *
 *-----------------------------------------------------------------------------
 */
#undef  FUNC_NAME
#define FUNC_NAME "ENT_LogWarn"
ENT_PUBLIC MSG_ID_T ENT_LogWarn(ENT_LOG logHandle,const char* format,...)
{
    MSG_ID_T  sts=0;
    ENT_LOG_CTX*  logCtx = NULL;
    
    sts = iENT_LogAcquireWriter(&logCtx,logHandle);
    if(sts<0)
    {
        return sts;
    }
    if(LOG_LEV_WARN_E>logCtx->logLevel)
    {
        iENT_LogReleaseWriter(logCtx);
        return 1;
    }
    
    va_list va_args;
    va_start(va_args,format);
    sts = iENT_LogVPrint(logCtx,LOG_LEV_WARN_E,format,va_args);
    va_end(va_args);
    iENT_LogReleaseWriter(logCtx);
    return sts;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_LogPrint
 *
 * DESCRIPTION :   
 *                 
 *                   
 *
 * COMPLETION
 * STATUS      :  0
 *                Success; Service has completed successfully.           
 *
 *                            
 *
 *-----------------------------------------------------------------------------
 */
#undef  FUNC_NAME
#define FUNC_NAME "ENT_LogPrint"
ENT_PUBLIC MSG_ID_T ENT_LogPrint(ENT_LOG logHandle,const char* format,...)
{
    MSG_ID_T  sts=0;
    ENT_LOG_CTX*  logCtx = NULL;
    
    sts = iENT_LogAcquireWriter(&logCtx,logHandle);
    if(sts<0)
    {
        return sts;
    }
    if(LOG_LEV_INFO_E>logCtx->logLevel)
    {
        iENT_LogReleaseWriter(logCtx);
        return 1;
    }
    
    va_list va_args;
    va_start(va_args,format);
    sts = iENT_LogVPrint(logCtx,LOG_LEV_INFO_E,format,va_args);
    va_end(va_args);
    iENT_LogReleaseWriter(logCtx);
    return sts;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_LogDebug
 *
 * DESCRIPTION :   
 *                 
 *                   
 *
 * COMPLETION
 * STATUS      :  0
 *                Success; Service has completed successfully.           
 *
 *                            
 *
 *-----------------------------------------------------------------------------
 */
#undef  FUNC_NAME
#define FUNC_NAME "ENT_LogDebug"
ENT_PUBLIC MSG_ID_T ENT_LogDebug(ENT_LOG logHandle,const char* format,...)
{
    MSG_ID_T  sts=0;
    ENT_LOG_CTX*  logCtx = NULL;
    
    sts = iENT_LogAcquireWriter(&logCtx,logHandle);
    if(sts<0)
    {
        return sts;
    }
    if(LOG_LEV_DEBUG_E>logCtx->logLevel)
    {
        iENT_LogReleaseWriter(logCtx);
        return 1;
    }
    
    va_list va_args;
    va_start(va_args,format);
    sts = iENT_LogVPrint(logCtx,LOG_LEV_DEBUG_E,format,va_args);
    va_end(va_args);
    iENT_LogReleaseWriter(logCtx);
    return sts;
}
#ifdef WIN32
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :UTL_MultiToWide
 *
 * DESCRIPTION :   
 *                 
 *                   
 *
 * COMPLETION
 * STATUS      :  Not NULL
 *                Success; Service has completed successfully.           
 *
 *                            
 *
 *-----------------------------------------------------------------------------
 */
#undef  FUNC_NAME
#define FUNC_NAME "UTL_MultiToWide"
static WCHAR* UTL_MultiToWide(WCHAR** pwStr,const char* localStr)
{       
    if(localStr == NULL)
    {
        IENT_LOG_ERROR("arguments is invalid.\n");
        return NULL;
    }
        
    wchar_t *unicodeStr = NULL;
    int nRetLen = 0;
    nRetLen = MultiByteToWideChar(CP_ACP, 0, localStr, -1, NULL, 0);
    unicodeStr = (WCHAR *)malloc(nRetLen * sizeof(WCHAR));
    nRetLen = MultiByteToWideChar(CP_ACP, 0, localStr, -1, unicodeStr, nRetLen);
    if(pwStr)
        *pwStr = unicodeStr;
        
    return unicodeStr;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :iENT_LogPathCheck
 *
 * DESCRIPTION :   for windows
 *                 
 *                   
 *
 * COMPLETION
 * STATUS      :  0
 *                Success; Service has completed successfully.           
 *
 *                            
 *
 *-----------------------------------------------------------------------------
 */
#undef  FUNC_NAME
#define FUNC_NAME "iENT_LogPathCheck"
static MSG_ID_T iENT_LogPathCheck(const char* path)
{
    HANDLE hDir = INVALID_HANDLE_VALUE;        

    if(path==NULL)
    {
        fprintf(stderr,"Func [%s] Line [%d],arguments is invalid.\n",FUNC_NAME,__LINE__);
        return -1;
    }
        
    WCHAR* wPath=NULL;
    UTL_MultiToWide(&wPath,path);
    size_t len = wcslen(wPath);
    if ((wPath[len - 1] == L'\\') || (wPath[len - 1] == L'/'))
    {
        wPath[len - 1] = L'\0';
    }
    
    hDir = CreateFileW(wPath,
        GENERIC_READ,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        NULL
        );

    if (INVALID_HANDLE_VALUE == hDir)
    {
        if(!CreateDirectoryW(wPath, NULL))
        {
            fprintf(stderr,"Func [%s] Line [%d],Can not create log directory, invalid path name[%s]\n",FUNC_NAME,__LINE__,path);
            if(wPath) free(wPath);
            return -1;
        }
    }
    else
    {
        if(wPath) free(wPath);
        CloseHandle(hDir);
    }
    return 0;
}
#else
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :iENT_LogPathCheck
 *
 * DESCRIPTION :   for Linux
 *                 
 *                   
 *
 * COMPLETION
 * STATUS      :  0
 *                Success; Service has completed successfully.           
 *
 *                            
 *
 *-----------------------------------------------------------------------------
 */
#undef  FUNC_NAME
#define FUNC_NAME "iENT_LogPathCheck"
static MSG_ID_T iENT_LogPathCheck(const char* path)
{   
    int ret = 0;     
    if(path==NULL)
    {
        fprintf(stderr,"Func [%s] Line [%d],arguments is invalid.\n",FUNC_NAME,__LINE__);
        return -1;
    }
        
    struct stat st = {0};

    if (stat(path, &st) == -1) 
    {
        ret=mkdir(path, 0777);
        if(ret==-1)
        {
             fprintf(stderr,"Func [%s] Line [%d],Can not create log directory [%s],msg->[%s]\n",FUNC_NAME,__LINE__,path,strerror(errno));
        }
    }
    return 0;
}
#endif
