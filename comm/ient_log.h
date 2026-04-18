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
#ifndef _I_ENT_LOG_H_
#define _I_ENT_LOG_H_

#include <stdio.h>
#include <time.h>

#ifdef WIN32
#include <Windows.h>
#else
#include <pthread.h>
#endif

#include "ent_log.h"

#define ENT_LOG_FLUSH_BATCH      256
#define ENT_LOG_WRITE_BATCH      64
#define ENT_LOG_FILE_BUFFER_SIZE  (64 * 1024)
#define ENT_LOG_POOL_MSG_SIZE    1024
#define ENT_LOG_POOL_MAX_FREE_NODES 512

#define ENTLOG_TAG     (0x6AFEFE6A)
#define ENTLOG_CTX_TAG (0x6AFEFE6B)

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

typedef struct ENT_LOG_CTX_INTERNAL_TAG
{
    unsigned int     tag;
    bool             isInit;
    bool             isDebug;
    bool             isBuffer;
    ENT_LOG_LEV_E    logLevel;
    FILE*            logFp;
#ifdef WIN32
    CRITICAL_SECTION cs;
    CONDITION_VARIABLE closeCv;
    CONDITION_VARIABLE bufferCv;
    HANDLE            bufferThread;
    volatile LONG     closing;
    volatile LONG     activeWriters;
    volatile LONG     isDebugFast;
    volatile LONG     isBufferFast;
#else
    pthread_mutex_t   cs;
    pthread_cond_t    closeCv;
    pthread_cond_t    bufferCv;
    pthread_t         bufferThread;
    volatile int      closing;
    volatile int      activeWriters;
    volatile int      isDebugFast;
    volatile int      isBufferFast;
#endif
    bool             bufferThreadStarted;
    bool             bufferThreadStop;
    int              pendingFlushes;
    int              flushBatch;
    int              flushIntervalMs;
    long long        lastFlushMs;
    struct ENT_LOG_MSG_NODE_TAG* bufferHead;
    struct ENT_LOG_MSG_NODE_TAG* bufferTail;
    struct ENT_LOG_MSG_NODE_TAG* poolFreeHead;
    int              poolFreeCount;
    char*            moduleName;
    char*            logPath;
    struct ENT_LOG_CTX_TAG* ownerCtx;
    int              maxNum;
    time_t           nextCreate;
} ENT_LOG_CTX_INTERNAL;

typedef ENT_LOG_CTX_INTERNAL ENT_LOG_PRIV;

typedef struct ENT_LOG_CTX_TAG
{
    unsigned int tag;
    bool         isInit;
    ENT_LOG      logHandle;
} ENT_LOG_CTX_TAG;

ENT_LOG_PRIV* iENT_LogDefaultCtx(void);
ENT_LOG_PRIV* iENT_LogCtxFromHandle(ENT_LOG_CTX ctx);
extern volatile bool sLogMutexInit;
MSG_ID_T      iENT_LogInit(void);
MSG_ID_T      iENT_LogClose(void);
MSG_ID_T      iENT_LogInitHandle(ENT_LOG* pLogHandle, const char* moduleName, const char* logPath);
MSG_ID_T      iENT_LogCloseHandle(ENT_LOG logHandle);
MSG_ID_T      iENT_LogCtxValidate(const struct ENT_LOG_CTX_TAG* ctx, ENT_LOG logHandle);
MSG_ID_T      iENT_LogPathCheck(const char* path);
MSG_ID_T      iENT_LogGetCtx(ENT_LOG_CTX_INTERNAL** logCtx, ENT_LOG logHandle);
MSG_ID_T      iENT_LogAcquireWriter(ENT_LOG_CTX_INTERNAL** logCtx, ENT_LOG logHandle);
void          iENT_LogReleaseWriter(ENT_LOG_CTX_INTERNAL* log);
int           iENT_LogIsClosing(const ENT_LOG_CTX_INTERNAL* log);
void          iENT_LogSetClosing(ENT_LOG_CTX_INTERNAL* log);
int           iENT_LogActiveGet(const ENT_LOG_CTX_INTERNAL* log);
void          iENT_LogActiveInc(ENT_LOG_CTX_INTERNAL* log);
int           iENT_LogActiveDec(ENT_LOG_CTX_INTERNAL* log);
MSG_ID_T      iENT_LogFormatMessage(const char* format,
                                    va_list va_args,
                                    char* stackBuf,
                                    size_t stackBufLen,
                                    char** msgBuf,
                                    size_t* msgLen);
void          iENT_LogFlushMaybe(ENT_LOG_CTX_INTERNAL* log, FILE* fp, bool forceFlush);
int           iENT_LogFastFlagGet(
#ifdef WIN32
                               const volatile LONG* flag
#else
                               const volatile int* flag
#endif
                               );
void          iENT_LogFastFlagSet(
#ifdef WIN32
                                volatile LONG* flag,
#else
                                volatile int* flag,
#endif
                                int value);
long long     iENT_LogNowMs(void);
bool          iENT_LogBufferReady(const ENT_LOG_CTX_INTERNAL* log);
ENT_LOG_MSG_NODE* iENT_LogAllocNode(size_t msgCap, bool pooled);
void          iENT_LogFreePoolNodes(ENT_LOG_CTX_INTERNAL* log);
MSG_ID_T      iENT_LogFormatPrefix(ENT_LOG_LEV_E logLevel,
                                   char* prefixBuf,
                                   size_t prefixBufLen,
                                   size_t* prefixLen,
                                   time_t* rollTime);
MSG_ID_T      iENT_LogStartBufferThread(ENT_LOG_CTX_INTERNAL* log);
void          iENT_LogStopBufferThread(ENT_LOG_CTX_INTERNAL* log);
MSG_ID_T      iENT_LogQueueMessage(ENT_LOG_CTX_INTERNAL* log,
                                   const char* msg,
                                   size_t msgLen,
                                   time_t rollTime,
                                   bool forceFlush);
MSG_ID_T      iENT_LogVRaw(ENT_LOG_CTX_INTERNAL* log, const char* format, va_list va_args);
MSG_ID_T      iENT_LogVPrint(ENT_LOG_CTX_INTERNAL* log, ENT_LOG_LEV_E logLevel, const char* format, va_list va_args);

#endif
