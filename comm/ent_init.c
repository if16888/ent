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
#ifdef WIN32
#pragma warning(disable : 4996)
#else
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <limits.h>
#include <errno.h>
#include <sys/mman.h>
#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#endif
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#define _MAX_PATH PATH_MAX
#endif
#include <stdio.h>
#include "ient_log.h"
#include "ient_comm.h"
#include "ient_runtime.h"
#include "ent_init.h"
#include "ent_msg.h"
#include "ent_utility.h"

#if defined(_MSC_VER)
#define ENT_THREAD_LOCAL __declspec(thread)
#else
#define ENT_THREAD_LOCAL __thread
#endif

ENT_CTX gEntCtx;
static ENT_THREAD_LOCAL ENT_CTX* sEntActiveCtx = NULL;

#ifdef WIN32
static SRWLOCK sEntRuntimeLock = SRWLOCK_INIT;
static void iENT_RuntimeLock(void)
{
    AcquireSRWLockExclusive(&sEntRuntimeLock);
}

static void iENT_RuntimeUnlock(void)
{
    ReleaseSRWLockExclusive(&sEntRuntimeLock);
}
#else
static pthread_mutex_t sEntRuntimeLock = PTHREAD_MUTEX_INITIALIZER;
static void iENT_RuntimeLock(void)
{
    pthread_mutex_lock(&sEntRuntimeLock);
}

static void iENT_RuntimeUnlock(void)
{
    pthread_mutex_unlock(&sEntRuntimeLock);
}
#endif

static unsigned int sEntLogUsers = 0;

ENT_CTX* iENT_RuntimeActiveCtx(void)
{
    if(sEntActiveCtx != NULL)
    {
        return sEntActiveCtx;
    }

    return &gEntCtx;
}

static ENT_CTX* iENT_RuntimeSetActiveCtx(ENT_CTX* ctx)
{
    ENT_CTX* prev = sEntActiveCtx;
    sEntActiveCtx = ctx;
    return prev;
}

static MSG_ID_T iENT_CTXApplyRtAttributes(ENT_CTX* ctx,
                                          int rtCpu,
                                          ENT_RT_POLICY_E rtPolicy,
                                          int rtPriority);
static MSG_ID_T iENT_CTXInit(ENT_CTX* ctx,
                             const char* name,
                             const char* workPath,
                             ENT_LOG_LEV_E logLevel,
                             ENT_MODE_E mode);
static MSG_ID_T iENT_CTXClose(ENT_CTX* ctx);
static MSG_ID_T iENT_CTXRun(ENT_CTX* ctx);

static MSG_ID_T iENT_HandleInit(ENT_HANDLE* handle,
                                const char* name,
                                const char* workPath,
                                ENT_LOG_LEV_E logLevel,
                                ENT_MODE_E mode)
{
    ENT_HANDLE_CTX_T* handleCtx = NULL;
    ENT_CTX* prevCtx = NULL;
    MSG_ID_T sts = ENT_SYS_NORMAL;

    if(handle == NULL)
    {
        return ENT_INIT_INVALID_ARGUMENT;
    }

    *handle = NULL;

    handleCtx = (ENT_HANDLE_CTX_T*)calloc(1, sizeof(*handleCtx));
    if(handleCtx == NULL)
    {
        return ENT_INIT_LOGPATH_ALLOCFAIL;
    }

    prevCtx = iENT_RuntimeSetActiveCtx(&handleCtx->ctx);
    sts = iENT_CTXInit(&handleCtx->ctx, name, workPath, logLevel, mode);
    iENT_RuntimeSetActiveCtx(prevCtx);
    if(sts != ENT_SYS_NORMAL)
    {
        free(handleCtx);
        return sts;
    }

    *handle = (ENT_HANDLE)handleCtx;
    return ENT_SYS_NORMAL;
}

static MSG_ID_T iENT_HandleClose(ENT_HANDLE* handle)
{
    ENT_HANDLE_CTX_T* handleCtx = NULL;
    ENT_CTX* prevCtx = NULL;
    MSG_ID_T sts = ENT_SYS_CLOSE_UNINITIALIZED;

    if(handle == NULL)
    {
        return ENT_INIT_INVALID_ARGUMENT;
    }

    handleCtx = (ENT_HANDLE_CTX_T*)(*handle);
    if(handleCtx == NULL || !handleCtx->ctx.isInit)
    {
        return ENT_SYS_CLOSE_UNINITIALIZED;
    }

    prevCtx = iENT_RuntimeSetActiveCtx(&handleCtx->ctx);
    sts = iENT_CTXClose(&handleCtx->ctx);
    iENT_RuntimeSetActiveCtx(prevCtx);
    free(handleCtx);
    *handle = NULL;
    return sts;
}

static MSG_ID_T iENT_HandleRun(ENT_HANDLE handle)
{
    ENT_HANDLE_CTX_T* handleCtx = (ENT_HANDLE_CTX_T*)handle;
    ENT_CTX* prevCtx = NULL;
    MSG_ID_T sts = ENT_SYS_NORMAL;

    if(handleCtx == NULL || !handleCtx->ctx.isInit)
    {
        return ENT_SYS_RUN_UNINITIALIZED;
    }

    prevCtx = iENT_RuntimeSetActiveCtx(&handleCtx->ctx);
    sts = iENT_CTXRun(&handleCtx->ctx);
    iENT_RuntimeSetActiveCtx(prevCtx);
    return sts;
}

static MSG_ID_T iENT_HandleSetRtAttributes(ENT_HANDLE handle,
                                           int rtCpu,
                                           ENT_RT_POLICY_E rtPolicy,
                                           int rtPriority)
{
    ENT_HANDLE_CTX_T* handleCtx = (ENT_HANDLE_CTX_T*)handle;
    ENT_CTX* prevCtx = NULL;
    MSG_ID_T sts = ENT_SYS_NORMAL;

    if(handleCtx == NULL || !handleCtx->ctx.isInit)
    {
        return ENT_RT_NOT_INITIALIZED;
    }

    if(handleCtx->ctx.rtRequested == false)
    {
        return ENT_RT_NOTRT;
    }

    prevCtx = iENT_RuntimeSetActiveCtx(&handleCtx->ctx);
    sts = iENT_CTXApplyRtAttributes(&handleCtx->ctx, rtCpu, rtPolicy, rtPriority);
    iENT_RuntimeSetActiveCtx(prevCtx);
    return sts;
}

static MSG_ID_T iENT_RuntimeAcquireLogService(void)
{
    MSG_ID_T sts = ENT_SYS_NORMAL;

    iENT_RuntimeLock();
    if(sEntLogUsers == 0)
    {
        sts = ENT_LogInit();
        if(sts < 0)
        {
            iENT_RuntimeUnlock();
            return sts;
        }
    }
    sEntLogUsers++;
    iENT_RuntimeUnlock();
    return ENT_SYS_NORMAL;
}

static void iENT_RuntimeReleaseLogService(void)
{
    bool closeLog = false;

    iENT_RuntimeLock();
    if(sEntLogUsers > 0)
    {
        sEntLogUsers--;
        closeLog = (sEntLogUsers == 0);
    }
    iENT_RuntimeUnlock();

    if(closeLog)
    {
        ENT_LogClose();
    }
}

static inline void iENT_CTXResetRuntime(ENT_CTX* ctx)
{
    if(ctx == NULL)
        return;

    ctx->rtRequested = false;
    ctx->rtEnabled = false;
    ctx->rtCpu = -1;
    ctx->rtPolicy = ENT_RT_POLICY_OTHER_E;
    ctx->rtPriority = 0;
    ctx->rtLastError = 0;
    ctx->logLevel = LOG_LEV_WARN_E;
    ctx->entLog = NULL;
    ctx->entLock = NULL;
    ctx->entCV = NULL;
    ctx->isInit = false;
}

static inline void iENT_CTXReleaseLogHandles(ENT_CTX* ctx,
                                             bool closeEntLog,
                                             bool closeDefaultLog)
{
    if(closeEntLog && ctx != NULL && ctx->entLog != NULL)
    {
        ENT_LogCloseHandle(ctx->entLog);
    }

    if(closeDefaultLog)
    {
        ENT_LogCloseHandle(NULL);
    }

    if(ctx != NULL)
    {
        ctx->entLog = NULL;
    }
}

static inline void iENT_CTXCloseLog(ENT_CTX* ctx,
                                    bool closeEntLog,
                                    bool closeDefaultLog)
{
    iENT_CTXReleaseLogHandles(ctx, closeEntLog, closeDefaultLog);
    iENT_RuntimeReleaseLogService();
}

static inline void iENT_CTXFree(ENT_CTX* ctx)
{
    if(ctx == NULL)
        return;

    if(ctx->entName)
    {
        free(ctx->entName);
        ctx->entName = NULL;
    }

    if(ctx->workPath)
    {
        free(ctx->workPath);
        ctx->workPath = NULL;
    }

    if(ctx->logName)
    {
        free(ctx->logName);
        ctx->logName = NULL;
    }

    if(ctx->logPath)
    {
        free(ctx->logPath);
        ctx->logPath = NULL;
    }
}

static inline void iENT_CTXSetRtRequestedState(ENT_CTX* ctx,
                                               ENT_MODE_E mode)
{
    if(ctx == NULL)
        return;

    ctx->rtRequested = (mode == ENT_MODE_REALTIME_E) ? true : false;
    ctx->rtEnabled = false;
    ctx->rtCpu = -1;
    ctx->rtPolicy = ENT_RT_POLICY_OTHER_E;
    ctx->rtPriority = 0;
    ctx->rtLastError = 0;
}

static MSG_ID_T iENT_CTXApplyRtMode(ENT_CTX* ctx,
                                    ENT_MODE_E mode)
{
    iENT_CTXSetRtRequestedState(ctx, mode);
    if(mode != ENT_MODE_REALTIME_E)
    {
        return ENT_SYS_NORMAL;
    }

#ifdef WIN32
    IENT_LOG_WARN("rt mode not supported on current platform,fallback to normal mode\n");
#else
    if(mlockall(MCL_CURRENT | MCL_FUTURE) == 0)
    {
        ctx->rtEnabled = true;
        IENT_LOG_PRINT("rt mode enabled via mlockall\n");
    }
    else
    {
        ctx->rtLastError = errno;
        IENT_LOG_WARN("mlockall failed,error [%d]->[%s],fallback to normal mode\n",
                      errno,
                      strerror(errno));
    }
#endif
    return ENT_SYS_NORMAL;
}

static MSG_ID_T iENT_CTXApplyRtAttributes(ENT_CTX* ctx,
                                          int rtCpu,
                                          ENT_RT_POLICY_E rtPolicy,
                                          int rtPriority)
{
    if(ctx == NULL)
    {
        return ENT_RT_NOT_INITIALIZED;
    }

    if(rtPolicy != ENT_RT_POLICY_OTHER_E &&
       rtPolicy != ENT_RT_POLICY_FIFO_E &&
       rtPolicy != ENT_RT_POLICY_RR_E)
    {
        IENT_LOG_ERROR("invalid rt policy [%d]\n",(int)rtPolicy);
        return ENT_RT_BAD_POLICY;
    }

    ctx->rtCpu = rtCpu;
    ctx->rtPolicy = (int)rtPolicy;
    ctx->rtPriority = rtPriority;
    ctx->rtLastError = 0;

#ifdef __linux__
    if(rtCpu >= 0)
    {
        cpu_set_t cpuSet;
        int ret = 0;

        if(rtCpu >= CPU_SETSIZE)
        {
            ctx->rtLastError = EINVAL;
            IENT_LOG_WARN("rt affinity cpu [%d] invalid\n",rtCpu);
            return ENT_RT_BAD_CPU;
        }
        CPU_ZERO(&cpuSet);
        CPU_SET(rtCpu,&cpuSet);
        ret = pthread_setaffinity_np(pthread_self(),sizeof(cpuSet),&cpuSet);
        if(ret != 0)
        {
            ctx->rtLastError = ret;
            IENT_LOG_WARN("pthread_setaffinity_np failed,error [%d]->[%s]\n",ret,strerror(ret));
            return ENT_RT_AFFINITY_FAILED;
        }
    }

    if(rtPolicy == ENT_RT_POLICY_FIFO_E || rtPolicy == ENT_RT_POLICY_RR_E)
    {
        int nativePolicy = (rtPolicy == ENT_RT_POLICY_FIFO_E) ? SCHED_FIFO : SCHED_RR;
        int minPrio = sched_get_priority_min(nativePolicy);
        int maxPrio = sched_get_priority_max(nativePolicy);
        int targetPrio = rtPriority;
        struct sched_param sch;
        int ret = 0;

        if(minPrio == -1 || maxPrio == -1)
        {
            ctx->rtLastError = errno;
            IENT_LOG_WARN("sched priority range query failed,error [%d]->[%s]\n",errno,strerror(errno));
            return ENT_RT_SCHED_QUERYFAIL;
        }
        if(targetPrio < minPrio)
        {
            targetPrio = minPrio;
        }
        if(targetPrio > maxPrio)
        {
            targetPrio = maxPrio;
        }
        memset(&sch,0,sizeof(sch));
        sch.sched_priority = targetPrio;
        ret = pthread_setschedparam(pthread_self(),nativePolicy,&sch);
        if(ret != 0)
        {
            ctx->rtLastError = ret;
            IENT_LOG_WARN("pthread_setschedparam failed,error [%d]->[%s]\n",ret,strerror(ret));
            return ENT_RT_SCHED_SETFAIL;
        }
        ctx->rtPriority = targetPrio;
    }
    return ENT_SYS_NORMAL;
#else
    if(rtCpu >= 0 || rtPolicy != ENT_RT_POLICY_OTHER_E)
    {
        IENT_LOG_WARN("rt cpu/policy attributes are unsupported on this platform\n");
        return ENT_RT_UNSUPPORTED_PLATFORM;
    }
    return ENT_SYS_NORMAL;
#endif
}

static MSG_ID_T iENT_CTXInit(ENT_CTX* ctx,
                             const char* name,
                             const char* workPath,
                             ENT_LOG_LEV_E logLevel,
                             ENT_MODE_E mode)
{
    MSG_ID_T  sts = ENT_SYS_NORMAL;

    if(ctx == NULL)
    {
        fprintf(stderr,"ENT_Init context invalid.\n");
        return ENT_INIT_INVALID_ARGUMENT;
    }

    if(ctx->isInit)
    {
        fprintf(stderr,"ENT_Init already init.\n");
        return ENT_SYS_ALREADY_INITIALIZED;
    }
    if(name==NULL||workPath==NULL||name[0]=='\0'||workPath[0]=='\0')
    {
        fprintf(stderr,"ENT_Init arguments invalid.\n");
        return ENT_INIT_INVALID_ARGUMENT;
    }
    /* Security: reject excessively long inputs to prevent buffer overflow */
    if(strlen(name) > _MAX_PATH - 8 || strlen(workPath) > _MAX_PATH - 8)
    {
        fprintf(stderr,"ENT_Init name or workPath too long.\n");
        return ENT_INIT_INVALID_ARGUMENT;
    }

    ctx->workPath = ENT_StrDup(workPath);
    ctx->entName  = ENT_StrDup(name);
    ctx->logLevel = logLevel;

    size_t len     = strlen(workPath);
    size_t size    = len+4+1+1;//
    char*  tmpStr  = (char*)malloc(size);
    if(tmpStr == NULL)
    {
        fprintf(stderr,"malloc size [%zu] failed.\n",size);
        iENT_CTXFree(ctx);
        iENT_CTXResetRuntime(ctx);
        return ENT_INIT_LOGPATH_ALLOCFAIL;
    }
#ifdef WIN32
    if(workPath[len-1]=='\\'||workPath[len-1]=='/')
#else
    if(workPath[len-1]=='/')
#endif
    {
        snprintf(tmpStr,size,"%slog",workPath);
    }
    else
    {
        snprintf(tmpStr,size,"%s%slog",workPath,ENT_FILE_SEP);
    }
    ctx->logPath = tmpStr;

    len    = strlen(name);
    size   = 4+len+1; // 4->size of "log" or "/log"
    tmpStr = (char*)malloc(size);
    if(tmpStr == NULL)
    {
        fprintf(stderr,"malloc size [%zu] failed.\n",size);
        iENT_CTXFree(ctx);
        iENT_CTXResetRuntime(ctx);
        return ENT_INIT_LOGNAME_ALLOCFAIL;
    }
    snprintf(tmpStr,size,"ent_%s",name);
    ctx->logName = tmpStr;

    sts = iENT_RuntimeAcquireLogService();
    if(sts<0)
    {
        fprintf(stderr,"ENT_LogInit failed,sts [%d].\n",sts);
        iENT_CTXFree(ctx);
        iENT_CTXResetRuntime(ctx);
        return ENT_INIT_LOG_INITFAIL;
    }

    sts = ENT_LogInitHandle(&ctx->entLog,ctx->logName,ctx->logPath);
    if(sts<0)
    {
        fprintf(stderr,"ENT_LogInitHandle failed,sts [%d].\n",sts);
        iENT_CTXCloseLog(ctx,false,false);
        iENT_CTXFree(ctx);
        iENT_CTXResetRuntime(ctx);
        return ENT_INIT_ENTITY_HANDLEFAIL;
    }
    sts = ENT_LogSetOption(ctx->entLog,ENT_LOG_LEVEL_E,&logLevel);
    if(sts < 0)
    {
        fprintf(stderr,"ENT_LogSetOption failed,sts [%d].\n",sts);
        iENT_CTXCloseLog(ctx,true,false);
        iENT_CTXFree(ctx);
        iENT_CTXResetRuntime(ctx);
        return ENT_INIT_ENTITY_LEVELFAIL;
    }

    sts = UTL_LockInit(&ctx->entLock,"ent");
    if(sts<0)
    {
        IENT_LOG_ERROR("UTL_LockInit failed,sts [%d]\n",sts);
        iENT_CTXFree(ctx);
        iENT_CTXCloseLog(ctx,true,false);
        iENT_CTXResetRuntime(ctx);
        return ENT_INIT_LOCK_INITFAIL;
    }

    sts = UTL_CVInit(&ctx->entCV,"ent");
    if(sts<0)
    {
        IENT_LOG_ERROR("UTL_CVInit failed,sts [%d]\n",sts);
        iENT_CTXFree(ctx);
        UTL_LockClose(&ctx->entLock);
        ctx->entLock = NULL;
        iENT_CTXCloseLog(ctx,true,false);
        iENT_CTXResetRuntime(ctx);
        return ENT_INIT_CV_INITFAIL;
    }

    iENT_CTXApplyRtMode(ctx,mode);
    ctx->isInit = true;

    return ENT_SYS_NORMAL;
}

static MSG_ID_T iENT_CTXClose(ENT_CTX* ctx)
{
    if(ctx == NULL)
    {
        return ENT_SYS_CLOSE_UNINITIALIZED;
    }

    if(!ctx->isInit)
    {
        return ENT_SYS_CLOSE_UNINITIALIZED;
    }

#ifndef WIN32
    if(ctx->rtEnabled)
    {
        if(munlockall() != 0)
        {
            IENT_LOG_WARN("munlockall failed,error [%d]->[%s]\n",errno,strerror(errno));
        }
    }
#endif

    UTL_CVClose(&ctx->entCV);
    ctx->entCV = NULL;

    UTL_LockClose(&ctx->entLock);
    ctx->entLock = NULL;

    iENT_CTXCloseLog(ctx, true, false);

    iENT_CTXFree(ctx);
    iENT_CTXResetRuntime(ctx);
    return ENT_SYS_NORMAL;
}

static MSG_ID_T iENT_CTXRun(ENT_CTX* ctx)
{
    if(ctx == NULL || !ctx->isInit)
    {
        return ENT_SYS_RUN_UNINITIALIZED;
    }

    while(1)
    {
        UTL_LockEnter(ctx->entLock);
        UTL_CVWait(ctx->entCV,ctx->entLock,0,RW_WRITE_E);
        UTL_LockLeave(ctx->entLock);
        break;
    }
    return ENT_SYS_NORMAL;
}

/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_Init
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
ENT_PUBLIC MSG_ID_T  ENT_Init(ENT_HANDLE* handle,
                              const char* name,
                              const char* workPath,
                              ENT_LOG_LEV_E logLevel,
                              ENT_MODE_E mode)
{
    return iENT_HandleInit(handle, name, workPath, logLevel, mode);
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_Close
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
ENT_PUBLIC MSG_ID_T  ENT_Close(ENT_HANDLE* handle)
{
    return iENT_HandleClose(handle);
}

ENT_PUBLIC MSG_ID_T  ENT_SetRtAttributes(ENT_HANDLE handle,
                                         int rtCpu,
                                         ENT_RT_POLICY_E rtPolicy,
                                         int rtPriority)
{
    return iENT_HandleSetRtAttributes(handle, rtCpu, rtPolicy, rtPriority);
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_Run
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
ENT_PUBLIC MSG_ID_T  ENT_Run(ENT_HANDLE handle)
{
    return iENT_HandleRun(handle);
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_Helpers
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
ENT_PUBLIC MSG_ID_T  ENT_Helpers()
{
    return ENT_SYS_NORMAL;
}
