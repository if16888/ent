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
#ifdef _WIN32
#include "windows.h"
#else
#include <pthread.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#endif
#include <stdlib.h>
#include <string.h>
#include "ient_comm.h"
#include "ent_msg.h"
#include "ent_utility.h"

#ifdef _WIN32
#define ENT_TMR_IMPL_WINDOWS 1
#define ENT_TMR_IMPL_LINUX 0
#define ENT_TMR_IMPL_POSIX_FALLBACK 0
#elif defined(__linux__)
#define ENT_TMR_IMPL_WINDOWS 0
#define ENT_TMR_IMPL_LINUX 1
#define ENT_TMR_IMPL_POSIX_FALLBACK 0
#else
#define ENT_TMR_IMPL_WINDOWS 0
#define ENT_TMR_IMPL_LINUX 0
#define ENT_TMR_IMPL_POSIX_FALLBACK 1
#endif

typedef struct
{
    DLL_D_HDR         dllLnk;
    unsigned int      tag;
    void*             data;
    UTL_TIMER_EV_F    timer_ev_cb;
    bool              isEnable;
    int               ms;
    unsigned int      timerType;
#if ENT_TMR_IMPL_WINDOWS
    UINT              timerId;
#elif ENT_TMR_IMPL_LINUX
    pthread_t         timerThread;
    BOOL              selfDeleteRequested;
    UTL_LOCK          stateLock;
    long long         period_ns;
    long long         next_deadline_ns;
    pthread_t         rtWorker;
    pthread_t         cbWorker;
    UTL_LOCK          cbLock;
    UTL_CV            cbCv;
    unsigned int      pendingCallbacks;
    BOOL              stopWorker;
    BOOL              stopCallbackWorker;
    BOOL              rtMode;
#else
    /* Other POSIX platforms fall back to a worker thread. */
    pthread_t         timerThread;
    BOOL              selfDeleteRequested;
    UTL_LOCK          stateLock;
#endif
} TIMER_CTX_T ,*PTIMER_CTX_T;

typedef struct ENT_TH_CTX
{
    UTL_LOCK          dllLock;
    DLL_D_HDR         dllHeader;
}TIMER_TH_CTX;

#define UTL_TIMER_TAG  0xeb90eb90
#define UTL_TIMER_RT_STOP_POLL_NS 1000000LL

static bool          sUtilTimerInit;
static TIMER_TH_CTX  sTimerCtx;
#ifdef _WIN32
static SRWLOCK       sTimerLifecycleLock = SRWLOCK_INIT;
static __declspec(thread) unsigned int sTimerCallbackDepth = 0;
#else
static pthread_mutex_t sTimerLifecycleLock = PTHREAD_MUTEX_INITIALIZER;
static __thread unsigned int sTimerCallbackDepth = 0;
#endif
static BOOL          sTimerClosing = FALSE;
static unsigned int  sTimerLifecycleOps = 0;
#ifdef ENT_TIMER_TEST_HOOKS
static unsigned int  sTimerCloseEpoch = 0;
#endif
#if ENT_TMR_IMPL_LINUX && defined(ENT_TIMER_TEST_HOOKS)
static unsigned int  sTimerRtLiveContexts = 0;
#endif
#if (ENT_TMR_IMPL_LINUX || ENT_TMR_IMPL_POSIX_FALLBACK) && defined(ENT_TIMER_TEST_HOOKS)
static unsigned int  sTimerThreadSelfDetachSuccess = 0;
#endif

static MSG_ID_T iUTL_TimerDeleteTimer(UTL_TIMER_T* pTimer);
#ifndef _WIN32
static void iUTL_TimerSleepMs(int ms);
#endif

static void iUTL_TimerInvokeCallback(UTL_TIMER_EV_F callback, void* data)
{
    if(callback == NULL)
    {
        return;
    }

    sTimerCallbackDepth++;
    (void)callback(data);
    sTimerCallbackDepth--;
}

static void iUTL_TimerLifecycleLockEnter(void)
{
#ifdef _WIN32
    AcquireSRWLockExclusive(&sTimerLifecycleLock);
#else
    pthread_mutex_lock(&sTimerLifecycleLock);
#endif
}

static void iUTL_TimerLifecycleLockLeave(void)
{
#ifdef _WIN32
    ReleaseSRWLockExclusive(&sTimerLifecycleLock);
#else
    pthread_mutex_unlock(&sTimerLifecycleLock);
#endif
}

static BOOL iUTL_TimerLifecycleBeginOp(void)
{
    BOOL canRun = FALSE;

    iUTL_TimerLifecycleLockEnter();
    if(sUtilTimerInit && !sTimerClosing)
    {
        sTimerLifecycleOps++;
        canRun = TRUE;
    }
    iUTL_TimerLifecycleLockLeave();
    return canRun;
}

static void iUTL_TimerLifecycleRetainOp(void)
{
    iUTL_TimerLifecycleLockEnter();
    sTimerLifecycleOps++;
    iUTL_TimerLifecycleLockLeave();
}

static void iUTL_TimerLifecycleEndOp(void)
{
    iUTL_TimerLifecycleLockEnter();
    if(sTimerLifecycleOps > 0)
    {
        sTimerLifecycleOps--;
    }
    iUTL_TimerLifecycleLockLeave();
}

static void iUTL_TimerLifecycleWaitOps(void)
{
    for(;;)
    {
        BOOL done = FALSE;

        iUTL_TimerLifecycleLockEnter();
        done = (sTimerLifecycleOps == 0) ? TRUE : FALSE;
        iUTL_TimerLifecycleLockLeave();
        if(done)
        {
            return;
        }
#ifdef _WIN32
        Sleep(1);
#else
        iUTL_TimerSleepMs(1);
#endif
    }
}

#if ENT_TMR_IMPL_LINUX && defined(ENT_TIMER_TEST_HOOKS)
static void iUTL_TimerTestRtContextCreated(void)
{
    iUTL_TimerLifecycleLockEnter();
    sTimerRtLiveContexts++;
    iUTL_TimerLifecycleLockLeave();
}

static void iUTL_TimerTestRtContextDestroyed(void)
{
    iUTL_TimerLifecycleLockEnter();
    if(sTimerRtLiveContexts > 0)
    {
        sTimerRtLiveContexts--;
    }
    iUTL_TimerLifecycleLockLeave();
}

int iUTL_TimerTestRtLiveContextCount(void)
{
    unsigned int count;

    iUTL_TimerLifecycleLockEnter();
    count = sTimerRtLiveContexts;
    iUTL_TimerLifecycleLockLeave();
    return (int)count;
}
#else
#define iUTL_TimerTestRtContextCreated() ((void)0)
#define iUTL_TimerTestRtContextDestroyed() ((void)0)
#endif

#ifdef ENT_TIMER_TEST_HOOKS
int iUTL_TimerTestLifecycleOpCount(void)
{
    unsigned int count;

    iUTL_TimerLifecycleLockEnter();
    count = sTimerLifecycleOps;
    iUTL_TimerLifecycleLockLeave();
    return (int)count;
}

int iUTL_TimerTestClosing(void)
{
    BOOL closing;

    iUTL_TimerLifecycleLockEnter();
    closing = sTimerClosing;
    iUTL_TimerLifecycleLockLeave();
    return closing ? 1 : 0;
}

unsigned int iUTL_TimerTestCloseEpoch(void)
{
    unsigned int epoch;

    iUTL_TimerLifecycleLockEnter();
    epoch = sTimerCloseEpoch;
    iUTL_TimerLifecycleLockLeave();
    return epoch;
}

#if ENT_TMR_IMPL_LINUX || ENT_TMR_IMPL_POSIX_FALLBACK
static void iUTL_TimerTestThreadSelfDetached(void)
{
    iUTL_TimerLifecycleLockEnter();
    sTimerThreadSelfDetachSuccess++;
    iUTL_TimerLifecycleLockLeave();
}

int iUTL_TimerTestThreadSelfDetachCount(void)
{
    unsigned int count;

    iUTL_TimerLifecycleLockEnter();
    count = sTimerThreadSelfDetachSuccess;
    iUTL_TimerLifecycleLockLeave();
    return (int)count;
}
#endif
#else
#if ENT_TMR_IMPL_LINUX || ENT_TMR_IMPL_POSIX_FALLBACK
#define iUTL_TimerTestThreadSelfDetached() ((void)0)
#endif
#endif

#if ENT_TMR_IMPL_LINUX
static long long iUTL_TimerMonotonicNs(void);
static void* iUTL_TimerRtWorker(void* data);
static void* iUTL_TimerCallbackWorker(void* data);
static MSG_ID_T iUTL_TimerCreateRt(UTL_TIMER_T* pTimer,unsigned int type,int period_us,UTL_TIMER_EV_F evCb,void* data);
static MSG_ID_T iUTL_TimerDeleteRt(PTIMER_CTX_T timerCtx);
#endif

#if ENT_TMR_IMPL_LINUX || ENT_TMR_IMPL_POSIX_FALLBACK
static void* iUTL_TimerThread(void* data);
static void iUTL_TimerCleanupSelfDeletedThreadTimer(PTIMER_CTX_T timerCtx);
#endif

#if ENT_TMR_IMPL_LINUX || ENT_TMR_IMPL_POSIX_FALLBACK
static BOOL iUTL_TimerThreadStateEnabled(PTIMER_CTX_T timerCtx)
{
    BOOL enabled;

    UTL_LockEnter(timerCtx->stateLock);
    enabled = timerCtx->isEnable ? TRUE : FALSE;
    UTL_LockLeave(timerCtx->stateLock);
    return enabled;
}

static BOOL iUTL_TimerThreadStateSelfDelete(PTIMER_CTX_T timerCtx)
{
    BOOL selfDelete;

    UTL_LockEnter(timerCtx->stateLock);
    selfDelete = timerCtx->selfDeleteRequested;
    UTL_LockLeave(timerCtx->stateLock);
    return selfDelete;
}

static void iUTL_TimerThreadStateStop(PTIMER_CTX_T timerCtx, BOOL selfDelete)
{
    UTL_LockEnter(timerCtx->stateLock);
    timerCtx->isEnable = false;
    if(selfDelete)
    {
        timerCtx->selfDeleteRequested = TRUE;
    }
    UTL_LockLeave(timerCtx->stateLock);
}
#endif

#ifndef _WIN32
static void iUTL_TimerSleepMs(int ms)
{
    struct timespec req;
    struct timespec rem;

    if(ms <= 0)
    {
        return;
    }

    req.tv_sec = ms / 1000;
    req.tv_nsec = (ms % 1000) * 1000000L;
    while(nanosleep(&req, &rem) != 0 && errno == EINTR)
    {
        req = rem;
    }
}
#endif

#if ENT_TMR_IMPL_LINUX || ENT_TMR_IMPL_POSIX_FALLBACK
static void iUTL_TimerCleanupSelfDeletedThreadTimer(PTIMER_CTX_T timerCtx)
{
    if(timerCtx == NULL || timerCtx->tag != UTL_TIMER_TAG)
    {
        return;
    }

    /*
     * Successful self-delete removes the context from the global timer list
     * before detaching the worker. Close therefore cannot take ownership of
     * this context: it only waits for the retained lifecycle operation while
     * the detached worker destroys its private state here.
     */
    if(timerCtx->stateLock != NULL)
    {
        UTL_LockClose(&timerCtx->stateLock);
    }
    memset(timerCtx,0,sizeof(TIMER_CTX_T));
    free(timerCtx);
    iUTL_TimerLifecycleEndOp();
}

static void* iUTL_TimerThread(void* data)
{
    PTIMER_CTX_T timerCtx = (PTIMER_CTX_T)data;

    if(timerCtx == NULL || timerCtx->tag != UTL_TIMER_TAG)
    {
        return NULL;
    }

    while(iUTL_TimerThreadStateEnabled(timerCtx))
    {
        UTL_TIMER_EV_F timerCb = NULL;
        void* timerData = NULL;
        BOOL oneshot = FALSE;

        iUTL_TimerSleepMs(timerCtx->ms);
        if(!iUTL_TimerThreadStateEnabled(timerCtx))
        {
            break;
        }

        timerCb = timerCtx->timer_ev_cb;
        timerData = timerCtx->data;
        oneshot = (timerCtx->timerType & UTL_TIMER_E_ONESHOT) ? TRUE : FALSE;

        if(timerCb)
        {
            iUTL_TimerInvokeCallback(timerCb, timerData);
        }

        if(iUTL_TimerThreadStateSelfDelete(timerCtx))
        {
            break;
        }

        if(oneshot)
        {
            iUTL_TimerThreadStateStop(timerCtx, FALSE);
            break;
        }
    }

    if(iUTL_TimerThreadStateSelfDelete(timerCtx))
    {
        iUTL_TimerCleanupSelfDeletedThreadTimer(timerCtx);
    }

    return NULL;
}
#endif
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :UTL_TimerClose
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
ENT_PUBLIC MSG_ID_T  UTL_TimerClose()
{
    MSG_ID_T     sts = ENT_SYS_NORMAL;
    DLL_D_HDR*   tmpDll = NULL;

    if(sTimerCallbackDepth > 0)
    {
        IENT_LOG_ERROR("UTL_TimerClose cannot be called from a timer callback\n");
        return ENT_TMR_THREAD_FAILED;
    }

    iUTL_TimerLifecycleLockEnter();
    if(!sUtilTimerInit)
    {
        iUTL_TimerLifecycleLockLeave();
        IENT_LOG_ERROR("uninitialized\n");
        return ENT_TMR_NOT_INITIALIZED;
    }
    if(sTimerClosing)
    {
        iUTL_TimerLifecycleLockLeave();
        IENT_LOG_WARN("timer close in progress\n");
        return ENT_SYS_NORMAL;
    }
    sTimerClosing = TRUE;
#ifdef ENT_TIMER_TEST_HOOKS
    sTimerCloseEpoch++;
#endif
    iUTL_TimerLifecycleLockLeave();
    iUTL_TimerLifecycleWaitOps();

    for(;;)
    {
        tmpDll = NULL;
        UTL_LockEnter(sTimerCtx.dllLock);
        sts = UTL_DllNextLe(&sTimerCtx.dllHeader,&tmpDll);
        UTL_LockLeave(sTimerCtx.dllLock);
        if(sts != ENT_SYS_NORMAL)
        {
            break;
        }

        sts = iUTL_TimerDeleteTimer((UTL_TIMER_T*)&tmpDll);
        if(sts < 0)
        {
            IENT_LOG_ERROR("UTL_TimerDelete failed,sts [%d]\n",sts);
        }
    }

    iUTL_TimerLifecycleLockEnter();
    UTL_LockClose(&sTimerCtx.dllLock);
    memset(&sTimerCtx,0,sizeof(sTimerCtx));
    sUtilTimerInit = false;
    sTimerClosing = FALSE;
    iUTL_TimerLifecycleLockLeave();
    return ENT_SYS_NORMAL;
}

#if ENT_TMR_IMPL_WINDOWS
static void CALLBACK iUTL_TimerWinCb(UINT uTimerID, UINT uMsg, DWORD_PTR dwUser, DWORD_PTR dw1, DWORD_PTR dw2)
{
    PTIMER_CTX_T timerCtx = (PTIMER_CTX_T)dwUser;
    (void)uTimerID;
    (void)uMsg;
    (void)dw1;
    (void)dw2;
    if(timerCtx == NULL || timerCtx->tag!=UTL_TIMER_TAG)
    {
        IENT_LOG_ERROR("unvalid arg\n");
        return;
    }

    if(timerCtx->timer_ev_cb)
    {
        iUTL_TimerInvokeCallback(timerCtx->timer_ev_cb, timerCtx->data);
    }
}

ENT_PUBLIC MSG_ID_T  UTL_TimerInit()
{
    MSG_ID_T  sts = 0;

    iUTL_TimerLifecycleLockEnter();
    if(!sUtilTimerInit)
    {
        sts = UTL_LockInit(&sTimerCtx.dllLock,"UTL_TimerInit");
        if(sts<0)
        {
            IENT_LOG_ERROR("UTL_LockInit failed,sts [%d].\n",sts);
            goto END_OF_ROUTINE;
        }
        sts = UTL_DllInitHead(&sTimerCtx.dllHeader);
        if(sts<0)
        {
            UTL_LockClose(&sTimerCtx.dllLock);
            IENT_LOG_ERROR("UTL_DllInitHead failed,sts [%d].\n",sts);
            goto END_OF_ROUTINE;
        }
        sUtilTimerInit = true;
    }

END_OF_ROUTINE:
    iUTL_TimerLifecycleLockLeave();
    return sts;
}

ENT_PUBLIC MSG_ID_T UTL_TimerCreate(UTL_TIMER_T* pTimer,unsigned int type, int ms,UTL_TIMER_EV_F evCb,void* data)
{
    PTIMER_CTX_T  timerCtx = NULL;
    UINT          fuEvent = TIME_PERIODIC;
    MSG_ID_T      sts = 0;

    if(pTimer)
    {
        *pTimer = NULL;
    }
    if(ms <= 0)
    {
        return ENT_TMR_BAD_ARGUMENT;
    }

    if(!iUTL_TimerLifecycleBeginOp())
    {
        IENT_LOG_ERROR("timer subsystem unavailable or closing\n");
        return ENT_TMR_NOT_INITIALIZED;
    }

    timerCtx = (PTIMER_CTX_T)malloc(sizeof(TIMER_CTX_T));
    if(timerCtx == NULL)
    {
        iUTL_TimerLifecycleEndOp();
        IENT_LOG_ERROR("malloc failed,error [%d]\n",errno);
        return ENT_TMR_ALLOC_FAILED;
    }
    memset(timerCtx,0,sizeof(TIMER_CTX_T));

    timerCtx->tag         = UTL_TIMER_TAG;
    timerCtx->timerType   = type;
    timerCtx->timer_ev_cb = evCb;
    timerCtx->data        = data;
    timerCtx->ms          = ms;
    if(type&UTL_TIMER_E_ONESHOT)
        fuEvent = TIME_ONESHOT;

    UTL_LockEnter(sTimerCtx.dllLock);
    sts = UTL_DllInsHead(&sTimerCtx.dllHeader,(DLL_D_HDR*)timerCtx);
    UTL_LockLeave(sTimerCtx.dllLock);
    if(sts < 0)
    {
        iUTL_TimerLifecycleEndOp();
        free(timerCtx);
        return ENT_TMR_LIST_FAILED;
    }

    timerCtx->timerId = timeSetEvent(ms,0,iUTL_TimerWinCb,(DWORD_PTR)timerCtx,fuEvent|TIME_KILL_SYNCHRONOUS);
    if(timerCtx->timerId == 0)
    {
        DLL_D_HDR* tmpDll = NULL;
        IENT_LOG_ERROR("timeSetEvent failed,error [%d]\n",errno);
        UTL_LockEnter(sTimerCtx.dllLock);
        UTL_DllRemCurr((DLL_D_HDR*)timerCtx,&tmpDll);
        UTL_LockLeave(sTimerCtx.dllLock);
        memset(timerCtx,0,sizeof(TIMER_CTX_T));
        free(timerCtx);
        iUTL_TimerLifecycleEndOp();
        return ENT_TMR_CREATE_FAILED;
    }

    if(pTimer)
    {
        *pTimer = timerCtx;
    }
    iUTL_TimerLifecycleEndOp();
    return ENT_SYS_NORMAL;
}

static MSG_ID_T iUTL_TimerDeleteTimer(UTL_TIMER_T* pTimer)
{
    MSG_ID_T      sts = 0;
    PTIMER_CTX_T  timerCtx = NULL;
    DLL_D_HDR*    tmpDll;
    if(pTimer == NULL)
    {
        IENT_LOG_ERROR("unvalid arg\n");
        return ENT_TMR_BAD_ARGUMENT;
    }

    timerCtx = (PTIMER_CTX_T)*pTimer;
    if(timerCtx == NULL )
    {
        IENT_LOG_WARN("unvalid timer\n");
        return ENT_TMR_BAD_ARGUMENT;
    }

    if(timeKillEvent(timerCtx->timerId)== MMSYSERR_INVALPARAM )
    {
        IENT_LOG_ERROR("timeKillEvent failed,error [%d]\n",errno);
        return ENT_TMR_DELETE_FAILED;
    }
    UTL_LockEnter(sTimerCtx.dllLock);
    sts = UTL_DllRemCurr((DLL_D_HDR*)timerCtx,&tmpDll);
    UTL_LockLeave(sTimerCtx.dllLock);

    memset(timerCtx,0,sizeof(TIMER_CTX_T));
    free(timerCtx);
    *pTimer = NULL;
    if(sts < 0)
    {
        return ENT_TMR_LIST_FAILED;
    }
    return ENT_SYS_NORMAL;
}

ENT_PUBLIC MSG_ID_T UTL_TimerDelete(UTL_TIMER_T* pTimer)
{
    MSG_ID_T sts = 0;

    if(pTimer == NULL)
    {
        IENT_LOG_ERROR("unvalid arg\n");
        return ENT_TMR_BAD_ARGUMENT;
    }

    if(!iUTL_TimerLifecycleBeginOp())
    {
        IENT_LOG_ERROR("uninitialized or closing\n");
        return ENT_TMR_NOT_INITIALIZED;
    }
    sts = iUTL_TimerDeleteTimer(pTimer);
    iUTL_TimerLifecycleEndOp();
    return sts;
}

#elif ENT_TMR_IMPL_LINUX
static long long iUTL_TimerMonotonicNs(void)
{
    struct timespec ts;

    if(clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    {
        IENT_LOG_ERROR("clock_gettime failed,error [%d]->[%s]\n",errno,strerror(errno));
        return 0;
    }

    return (long long)ts.tv_sec * 1000000000LL + (long long)ts.tv_nsec;
}

static BOOL iUTL_TimerRtStopRequested(PTIMER_CTX_T timerCtx)
{
    BOOL stopWorker;

    UTL_LockEnter(timerCtx->cbLock);
    stopWorker = timerCtx->stopWorker;
    UTL_LockLeave(timerCtx->cbLock);
    return stopWorker;
}

static void iUTL_TimerRtRequestStop(PTIMER_CTX_T timerCtx)
{
    UTL_LockEnter(timerCtx->cbLock);
    timerCtx->isEnable = false;
    timerCtx->stopWorker = TRUE;
    timerCtx->stopCallbackWorker = TRUE;
    UTL_LockLeave(timerCtx->cbLock);
    UTL_CVWakeAll(timerCtx->cbCv);
}

static void* iUTL_TimerRtWorker(void* data)
{
    PTIMER_CTX_T timerCtx = (PTIMER_CTX_T)data;

    if(timerCtx == NULL || timerCtx->tag != UTL_TIMER_TAG)
    {
        return NULL;
    }

    timerCtx->next_deadline_ns = iUTL_TimerMonotonicNs() + timerCtx->period_ns;
    while(!iUTL_TimerRtStopRequested(timerCtx))
    {
        long long deadline = timerCtx->next_deadline_ns;
        int sleepFailed = 0;

        while(!iUTL_TimerRtStopRequested(timerCtx))
        {
            struct timespec ts;
            long long now = iUTL_TimerMonotonicNs();
            long long wakeDeadline = deadline;
            int sleepSts = 0;

            if(now == 0)
            {
                sleepFailed = 1;
                break;
            }
            if(now >= deadline)
            {
                break;
            }
            if((deadline - now) > UTL_TIMER_RT_STOP_POLL_NS)
            {
                wakeDeadline = now + UTL_TIMER_RT_STOP_POLL_NS;
            }

            ts.tv_sec = wakeDeadline / 1000000000LL;
            ts.tv_nsec = wakeDeadline % 1000000000LL;

            do
            {
                sleepSts = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
            }
            while(sleepSts == EINTR && !iUTL_TimerRtStopRequested(timerCtx));

            if(sleepSts != 0 && sleepSts != EINTR)
            {
                IENT_LOG_ERROR("clock_nanosleep failed,error [%d]\n",sleepSts);
                sleepFailed = 1;
                break;
            }
        }

        if(sleepFailed || iUTL_TimerRtStopRequested(timerCtx))
        {
            break;
        }

        UTL_LockEnter(timerCtx->cbLock);
        if(timerCtx->stopWorker)
        {
            UTL_LockLeave(timerCtx->cbLock);
            break;
        }
        {
            BOOL needWake = (timerCtx->pendingCallbacks == 0) ? TRUE : FALSE;
            timerCtx->pendingCallbacks++;
            UTL_LockLeave(timerCtx->cbLock);
            if(needWake)
            {
                UTL_CVWake(timerCtx->cbCv);
            }
        }

        if(timerCtx->timerType & UTL_TIMER_E_ONESHOT)
        {
            break;
        }

        timerCtx->next_deadline_ns += timerCtx->period_ns;
    }

    return NULL;
}

static void* iUTL_TimerCallbackWorker(void* data)
{
    PTIMER_CTX_T timerCtx = (PTIMER_CTX_T)data;
    BOOL selfDeleteCleanup = FALSE;

    if(timerCtx == NULL || timerCtx->tag != UTL_TIMER_TAG)
    {
        return NULL;
    }

    for(;;)
    {
        UTL_TIMER_EV_F timerCb = NULL;
        void* timerData = NULL;
        BOOL stopCallbackWorker = FALSE;
        BOOL oneshot = FALSE;
        unsigned int callbackBatch = 0;

        UTL_LockEnter(timerCtx->cbLock);
        while(timerCtx->pendingCallbacks == 0 && !timerCtx->stopCallbackWorker)
        {
            UTL_CVWait(timerCtx->cbCv, timerCtx->cbLock, 0, RW_WRITE_E);
        }
        stopCallbackWorker = timerCtx->stopCallbackWorker;
        if(stopCallbackWorker)
        {
            UTL_LockLeave(timerCtx->cbLock);
            break;
        }

        callbackBatch = timerCtx->pendingCallbacks;
        if(callbackBatch > 64U)
        {
            callbackBatch = 64U;
        }
        timerCtx->pendingCallbacks -= callbackBatch;
        timerCb = timerCtx->timer_ev_cb;
        timerData = timerCtx->data;
        oneshot = (timerCtx->timerType & UTL_TIMER_E_ONESHOT) ? TRUE : FALSE;
        UTL_LockLeave(timerCtx->cbLock);

        while(callbackBatch > 0)
        {
            BOOL shouldStopBatch = FALSE;

            if(timerCb)
            {
                iUTL_TimerInvokeCallback(timerCb, timerData);
            }
            callbackBatch--;

            UTL_LockEnter(timerCtx->cbLock);
            shouldStopBatch = (timerCtx->selfDeleteRequested ||
                               timerCtx->stopCallbackWorker) ? TRUE : FALSE;
            UTL_LockLeave(timerCtx->cbLock);
            if(shouldStopBatch || oneshot)
            {
                break;
            }
        }

        UTL_LockEnter(timerCtx->cbLock);
        selfDeleteCleanup = timerCtx->selfDeleteRequested;
        stopCallbackWorker = timerCtx->stopCallbackWorker;
        UTL_LockLeave(timerCtx->cbLock);
        if(selfDeleteCleanup || stopCallbackWorker || oneshot)
        {
            break;
        }
    }

    UTL_LockEnter(timerCtx->cbLock);
    selfDeleteCleanup = timerCtx->selfDeleteRequested;
    UTL_LockLeave(timerCtx->cbLock);
    if(selfDeleteCleanup)
    {
        UTL_CVClose(&timerCtx->cbCv);
        UTL_LockClose(&timerCtx->cbLock);
        iUTL_TimerTestRtContextDestroyed();
        memset(timerCtx,0,sizeof(TIMER_CTX_T));
        free(timerCtx);
        iUTL_TimerLifecycleEndOp();
    }

    return NULL;
}

static MSG_ID_T iUTL_TimerCreateRt(UTL_TIMER_T* pTimer,unsigned int type,int period_us,UTL_TIMER_EV_F evCb,void* data)
{
    MSG_ID_T      sts = 0;
    PTIMER_CTX_T  timerCtx = NULL;
    DLL_D_HDR*    tmpDll = NULL;
    BOOL          cbWorkerStarted = FALSE;
    BOOL          rtWorkerStarted = FALSE;
    BOOL          listed = FALSE;

    if(pTimer)
    {
        *pTimer = NULL;
    }

    if(period_us <= 0)
    {
        IENT_LOG_ERROR("invalid period_us [%d]\n",period_us);
        return ENT_TMR_BAD_ARGUMENT;
    }

    if(!iUTL_TimerLifecycleBeginOp())
    {
        IENT_LOG_ERROR("timer subsystem unavailable or closing\n");
        return ENT_TMR_NOT_INITIALIZED;
    }

    timerCtx = (PTIMER_CTX_T)malloc(sizeof(TIMER_CTX_T));
    if(timerCtx == NULL)
    {
        iUTL_TimerLifecycleEndOp();
        IENT_LOG_ERROR("malloc failed,error [%d]->[%s]\n",errno,strerror(errno));
        return ENT_TMR_ALLOC_FAILED;
    }
    memset(timerCtx,0,sizeof(TIMER_CTX_T));
    timerCtx->tag         = UTL_TIMER_TAG;
    timerCtx->timerType   = type;
    timerCtx->timer_ev_cb = evCb;
    timerCtx->data        = data;
    timerCtx->isEnable    = true;
    timerCtx->period_ns   = (long long)period_us * 1000LL;
    timerCtx->rtMode      = TRUE;

    sts = UTL_LockInit(&timerCtx->cbLock,"timer_rt_cb");
    if(sts < 0)
    {
        iUTL_TimerLifecycleEndOp();
        free(timerCtx);
        return ENT_TMR_THREAD_FAILED;
    }
    sts = UTL_CVInit(&timerCtx->cbCv,"timer_rt_cb");
    if(sts < 0)
    {
        iUTL_TimerLifecycleEndOp();
        UTL_LockClose(&timerCtx->cbLock);
        free(timerCtx);
        return ENT_TMR_THREAD_FAILED;
    }

    UTL_LockEnter(sTimerCtx.dllLock);
    sts = UTL_DllInsHead(&sTimerCtx.dllHeader,(DLL_D_HDR*)timerCtx);
    UTL_LockLeave(sTimerCtx.dllLock);
    if(sts < 0)
    {
        UTL_CVClose(&timerCtx->cbCv);
        UTL_LockClose(&timerCtx->cbLock);
        free(timerCtx);
        iUTL_TimerLifecycleEndOp();
        return ENT_TMR_LIST_FAILED;
    }
    listed = TRUE;

    if(pthread_create(&timerCtx->cbWorker, NULL, iUTL_TimerCallbackWorker, timerCtx) != 0)
    {
        IENT_LOG_ERROR("pthread_create callback worker failed,error [%d]->[%s]\n",errno,strerror(errno));
        sts = ENT_TMR_THREAD_FAILED;
        goto CREATE_FAILED;
    }
    cbWorkerStarted = TRUE;
    if(pthread_create(&timerCtx->rtWorker, NULL, iUTL_TimerRtWorker, timerCtx) != 0)
    {
        IENT_LOG_ERROR("pthread_create rt worker failed,error [%d]->[%s]\n",errno,strerror(errno));
        sts = ENT_TMR_THREAD_FAILED;
        goto CREATE_FAILED;
    }
    rtWorkerStarted = TRUE;
    iUTL_TimerTestRtContextCreated();

    if(pTimer)
    {
        *pTimer = timerCtx;
    }
    iUTL_TimerLifecycleEndOp();
    return ENT_SYS_NORMAL;

CREATE_FAILED:
    iUTL_TimerRtRequestStop(timerCtx);
    if(rtWorkerStarted)
    {
        pthread_join(timerCtx->rtWorker, NULL);
    }
    if(cbWorkerStarted)
    {
        pthread_join(timerCtx->cbWorker, NULL);
    }
    if(listed)
    {
        UTL_LockEnter(sTimerCtx.dllLock);
        UTL_DllRemCurr((DLL_D_HDR*)timerCtx,&tmpDll);
        UTL_LockLeave(sTimerCtx.dllLock);
    }
    UTL_CVClose(&timerCtx->cbCv);
    UTL_LockClose(&timerCtx->cbLock);
    memset(timerCtx,0,sizeof(TIMER_CTX_T));
    free(timerCtx);
    iUTL_TimerLifecycleEndOp();
    return sts;
}

static MSG_ID_T iUTL_TimerDeleteRt(PTIMER_CTX_T timerCtx)
{
    MSG_ID_T     sts = 0;
    DLL_D_HDR*   tmpDll = NULL;
    BOOL         selfDelete = FALSE;

    if(timerCtx == NULL || timerCtx->tag != UTL_TIMER_TAG)
    {
        IENT_LOG_WARN("unvalid timer\n");
        return ENT_TMR_BAD_ARGUMENT;
    }

    selfDelete = pthread_equal(pthread_self(), timerCtx->cbWorker) ? TRUE : FALSE;
    iUTL_TimerRtRequestStop(timerCtx);
    pthread_join(timerCtx->rtWorker, NULL);

    if(selfDelete)
    {
        UTL_LockEnter(sTimerCtx.dllLock);
        sts = UTL_DllRemCurr((DLL_D_HDR*)timerCtx,&tmpDll);
        UTL_LockLeave(sTimerCtx.dllLock);
        if(sts < 0)
        {
            return ENT_TMR_LIST_FAILED;
        }

        if(pthread_detach(timerCtx->cbWorker) != 0)
        {
            UTL_LockEnter(sTimerCtx.dllLock);
            sts = UTL_DllInsHead(&sTimerCtx.dllHeader,(DLL_D_HDR*)timerCtx);
            UTL_LockLeave(sTimerCtx.dllLock);
            if(sts < 0)
            {
                return ENT_TMR_LIST_FAILED;
            }
            return ENT_TMR_THREAD_FAILED;
        }

        iUTL_TimerLifecycleRetainOp();
        UTL_LockEnter(timerCtx->cbLock);
        timerCtx->selfDeleteRequested = TRUE;
        UTL_LockLeave(timerCtx->cbLock);
        return ENT_SYS_NORMAL;
    }

    pthread_join(timerCtx->cbWorker, NULL);

    UTL_LockEnter(sTimerCtx.dllLock);
    sts = UTL_DllRemCurr((DLL_D_HDR*)timerCtx,&tmpDll);
    UTL_LockLeave(sTimerCtx.dllLock);

    UTL_CVClose(&timerCtx->cbCv);
    UTL_LockClose(&timerCtx->cbLock);
    iUTL_TimerTestRtContextDestroyed();
    memset(timerCtx,0,sizeof(TIMER_CTX_T));
    free(timerCtx);
    if(sts < 0)
    {
        return ENT_TMR_LIST_FAILED;
    }
    return ENT_SYS_NORMAL;
}

ENT_PUBLIC MSG_ID_T  UTL_TimerInit()
{
    MSG_ID_T  sts = 0;

    iUTL_TimerLifecycleLockEnter();
    if(sUtilTimerInit)
    {
        iUTL_TimerLifecycleLockLeave();
        IENT_LOG_WARN("initialized already\n");
        return ENT_SYS_NORMAL;
    }

    sts = UTL_LockInit(&sTimerCtx.dllLock,"UTL_TimerInit");
    if(sts<0)
    {
        IENT_LOG_ERROR("UTL_LockInit failed,sts [%d].\n",sts);
        goto END_OF_ROUTINE;
    }
    sts = UTL_DllInitHead(&sTimerCtx.dllHeader);
    if(sts<0)
    {
        UTL_LockClose(&sTimerCtx.dllLock);
        IENT_LOG_ERROR("UTL_DllInitHead failed,sts [%d].\n",sts);
        goto END_OF_ROUTINE;
    }
    sUtilTimerInit = true;
END_OF_ROUTINE:
    iUTL_TimerLifecycleLockLeave();
    if(sts < 0)
    {
        return ENT_TMR_THREAD_FAILED;
    }
    return ENT_SYS_NORMAL;
}

ENT_PUBLIC MSG_ID_T UTL_TimerCreate(UTL_TIMER_T* pTimer,unsigned int type, int ms,UTL_TIMER_EV_F evCb,void* data)
{
    MSG_ID_T      sts = 0;
    PTIMER_CTX_T  timerCtx = NULL;
    DLL_D_HDR*    tmpDll = NULL;

    if(pTimer)
    {
        *pTimer = NULL;
    }
    if(ms <= 0)
    {
        return ENT_TMR_BAD_ARGUMENT;
    }

    if(type & UTL_TIMER_E_SIGNAL)
    {
        IENT_LOG_WARN("UTL_TIMER_E_SIGNAL uses thread mode on Linux\n");
        type &= ~UTL_TIMER_E_SIGNAL;
    }

    if(!iUTL_TimerLifecycleBeginOp())
    {
        IENT_LOG_ERROR("timer subsystem unavailable or closing\n");
        return ENT_TMR_NOT_INITIALIZED;
    }

    timerCtx = (PTIMER_CTX_T)malloc(sizeof(TIMER_CTX_T));
    if(timerCtx == NULL)
    {
        iUTL_TimerLifecycleEndOp();
        IENT_LOG_ERROR("malloc failed,error [%d]->[%s]\n",errno,strerror(errno));
        return ENT_TMR_ALLOC_FAILED;
    }
    memset(timerCtx,0,sizeof(TIMER_CTX_T));
    timerCtx->tag         = UTL_TIMER_TAG;
    timerCtx->timerType   = type;
    timerCtx->timer_ev_cb = evCb;
    timerCtx->data        = data;
    timerCtx->ms          = ms;
    timerCtx->isEnable    = true;

    sts = UTL_LockInit(&timerCtx->stateLock,"timer_thread_state");
    if(sts < 0)
    {
        free(timerCtx);
        iUTL_TimerLifecycleEndOp();
        return ENT_TMR_THREAD_FAILED;
    }

    {
        UTL_LockEnter(sTimerCtx.dllLock);
        sts = UTL_DllInsHead(&sTimerCtx.dllHeader,(DLL_D_HDR*)timerCtx);
        UTL_LockLeave(sTimerCtx.dllLock);
        if(sts < 0)
        {
            UTL_LockClose(&timerCtx->stateLock);
            free(timerCtx);
            iUTL_TimerLifecycleEndOp();
            return ENT_TMR_LIST_FAILED;
        }

        if(pthread_create(&timerCtx->timerThread, NULL, iUTL_TimerThread, timerCtx) != 0)
        {
            IENT_LOG_ERROR("pthread_create failed,error [%d]->[%s]\n",errno,strerror(errno));
            UTL_LockEnter(sTimerCtx.dllLock);
            UTL_DllRemCurr((DLL_D_HDR*)timerCtx,&tmpDll);
            UTL_LockLeave(sTimerCtx.dllLock);
            UTL_LockClose(&timerCtx->stateLock);
            free(timerCtx);
            iUTL_TimerLifecycleEndOp();
            return ENT_TMR_THREAD_FAILED;
        }

        if(pTimer)
        {
            *pTimer = timerCtx;
        }
        iUTL_TimerLifecycleEndOp();
        return ENT_SYS_NORMAL;
    }
}

ENT_PUBLIC MSG_ID_T UTL_TimerCreateUs(UTL_TIMER_T* pTimer,unsigned int type, int period_us,UTL_TIMER_EV_F evCb,void* data)
{
    return iUTL_TimerCreateRt(pTimer, type, period_us, evCb, data);
}

static MSG_ID_T iUTL_TimerDeleteTimer(UTL_TIMER_T* pTimer)
{
    MSG_ID_T      sts = 0;
    DLL_D_HDR*    tmpDll;
    PTIMER_CTX_T  timerCtx = NULL;

    if(pTimer == NULL)
    {
        IENT_LOG_ERROR("unvalid arg\n");
        return ENT_TMR_BAD_ARGUMENT;
    }

    timerCtx = (PTIMER_CTX_T)*pTimer;
    if(timerCtx == NULL || timerCtx->tag != UTL_TIMER_TAG)
    {
        IENT_LOG_WARN("unvalid timer\n");
        return ENT_TMR_BAD_ARGUMENT;
    }

    if(timerCtx->rtMode)
    {
        sts = iUTL_TimerDeleteRt(timerCtx);
        if(sts < 0)
        {
            return sts;
        }
        *pTimer = NULL;
        return ENT_SYS_NORMAL;
    }

    iUTL_TimerThreadStateStop(timerCtx, FALSE);
    if(pthread_equal(pthread_self(), timerCtx->timerThread))
    {
        int detachSts;

        /*
         * Transfer ownership atomically enough for Close: remove the context
         * while this public Delete still owns a lifecycle operation, then
         * detach the worker. On detach failure restore list ownership so a
         * later external Delete/Close can still join and reclaim the thread.
         */
        UTL_LockEnter(sTimerCtx.dllLock);
        sts = UTL_DllRemCurr((DLL_D_HDR*)timerCtx,&tmpDll);
        UTL_LockLeave(sTimerCtx.dllLock);
        if(sts < 0)
        {
            return ENT_TMR_LIST_FAILED;
        }

        detachSts = pthread_detach(timerCtx->timerThread);
        if(detachSts != 0)
        {
            UTL_LockEnter(sTimerCtx.dllLock);
            sts = UTL_DllInsHead(&sTimerCtx.dllHeader,(DLL_D_HDR*)timerCtx);
            UTL_LockLeave(sTimerCtx.dllLock);
            if(sts < 0)
            {
                return ENT_TMR_LIST_FAILED;
            }

            IENT_LOG_ERROR("pthread_detach failed during timer self-delete,error [%d]->[%s]\n",
                           detachSts,
                           strerror(detachSts));
            return ENT_TMR_THREAD_FAILED;
        }
        iUTL_TimerTestThreadSelfDetached();

        iUTL_TimerLifecycleRetainOp();
        iUTL_TimerThreadStateStop(timerCtx, TRUE);
        *pTimer = NULL;
        return ENT_SYS_NORMAL;
    }

    pthread_join(timerCtx->timerThread, NULL);

    UTL_LockEnter(sTimerCtx.dllLock);
    sts = UTL_DllRemCurr((DLL_D_HDR*)timerCtx,&tmpDll);
    UTL_LockLeave(sTimerCtx.dllLock);
    UTL_LockClose(&timerCtx->stateLock);
    memset(timerCtx,0,sizeof(TIMER_CTX_T));
    free(timerCtx);

    *pTimer = NULL;
    if(sts < 0)
    {
        return ENT_TMR_LIST_FAILED;
    }
    return ENT_SYS_NORMAL;
}

ENT_PUBLIC MSG_ID_T UTL_TimerDelete(UTL_TIMER_T* pTimer)
{
    MSG_ID_T sts = 0;

    if(pTimer == NULL)
    {
        IENT_LOG_ERROR("unvalid arg\n");
        return ENT_TMR_BAD_ARGUMENT;
    }

    if(!iUTL_TimerLifecycleBeginOp())
    {
        IENT_LOG_ERROR("uninitialized or closing\n");
        return ENT_TMR_NOT_INITIALIZED;
    }
    sts = iUTL_TimerDeleteTimer(pTimer);
    iUTL_TimerLifecycleEndOp();
    return sts;
}

#endif

#if !ENT_TMR_IMPL_LINUX
ENT_PUBLIC MSG_ID_T UTL_TimerCreateUs(UTL_TIMER_T* pTimer,unsigned int type, int period_us,UTL_TIMER_EV_F evCb,void* data)
{
    if(pTimer)
    {
        *pTimer = NULL;
    }
    (void)pTimer;
    (void)type;
    (void)period_us;
    (void)evCb;
    (void)data;
    return ENT_TMR_UNSUPPORTED;
}
#endif

#if ENT_TMR_IMPL_POSIX_FALLBACK
ENT_PUBLIC MSG_ID_T  UTL_TimerInit()
{
    MSG_ID_T  sts = 0;

    iUTL_TimerLifecycleLockEnter();
    if(sUtilTimerInit)
    {
        iUTL_TimerLifecycleLockLeave();
        IENT_LOG_WARN("initialized already\n");
        return ENT_SYS_NORMAL;
    }

    sts = UTL_LockInit(&sTimerCtx.dllLock,"UTL_TimerInit");
    if(sts<0)
    {
        IENT_LOG_ERROR("UTL_LockInit failed,sts [%d].\n",sts);
        iUTL_TimerLifecycleLockLeave();
        return ENT_TMR_THREAD_FAILED;
    }
    sts = UTL_DllInitHead(&sTimerCtx.dllHeader);
    if(sts<0)
    {
        UTL_LockClose(&sTimerCtx.dllLock);
        IENT_LOG_ERROR("UTL_DllInitHead failed,sts [%d].\n",sts);
        iUTL_TimerLifecycleLockLeave();
        return ENT_TMR_LIST_FAILED;
    }
    sUtilTimerInit = true;
    iUTL_TimerLifecycleLockLeave();
    return ENT_SYS_NORMAL;
}

ENT_PUBLIC MSG_ID_T UTL_TimerCreate(UTL_TIMER_T* pTimer,unsigned int type, int ms,UTL_TIMER_EV_F evCb,void* data)
{
    MSG_ID_T      sts = 0;
    PTIMER_CTX_T  timerCtx = NULL;
    DLL_D_HDR*    tmpDll = NULL;

    if(pTimer)
    {
        *pTimer = NULL;
    }
    if(ms <= 0)
    {
        return ENT_TMR_BAD_ARGUMENT;
    }

    if(!iUTL_TimerLifecycleBeginOp())
    {
        IENT_LOG_ERROR("timer subsystem unavailable or closing\n");
        return ENT_TMR_NOT_INITIALIZED;
    }

    timerCtx = (PTIMER_CTX_T)malloc(sizeof(TIMER_CTX_T));
    if(timerCtx == NULL)
    {
        iUTL_TimerLifecycleEndOp();
        IENT_LOG_ERROR("malloc failed,error [%d]->[%s]\n",errno,strerror(errno));
        return ENT_TMR_ALLOC_FAILED;
    }
    memset(timerCtx,0,sizeof(TIMER_CTX_T));
    timerCtx->tag         = UTL_TIMER_TAG;
    timerCtx->timerType   = type;
    timerCtx->timer_ev_cb = evCb;
    timerCtx->data        = data;
    timerCtx->ms          = ms;
    timerCtx->isEnable    = true;

    sts = UTL_LockInit(&timerCtx->stateLock,"timer_thread_state");
    if(sts < 0)
    {
        free(timerCtx);
        iUTL_TimerLifecycleEndOp();
        return ENT_TMR_THREAD_FAILED;
    }

    UTL_LockEnter(sTimerCtx.dllLock);
    sts = UTL_DllInsHead(&sTimerCtx.dllHeader,(DLL_D_HDR*)timerCtx);
    UTL_LockLeave(sTimerCtx.dllLock);
    if(sts < 0)
    {
        UTL_LockClose(&timerCtx->stateLock);
        free(timerCtx);
        iUTL_TimerLifecycleEndOp();
        return ENT_TMR_LIST_FAILED;
    }

    if(pthread_create(&timerCtx->timerThread, NULL, iUTL_TimerThread, timerCtx) != 0)
    {
        IENT_LOG_ERROR("pthread_create failed,error [%d]->[%s]\n",errno,strerror(errno));
        UTL_LockEnter(sTimerCtx.dllLock);
        UTL_DllRemCurr((DLL_D_HDR*)timerCtx,&tmpDll);
        UTL_LockLeave(sTimerCtx.dllLock);
        UTL_LockClose(&timerCtx->stateLock);
        free(timerCtx);
        iUTL_TimerLifecycleEndOp();
        return ENT_TMR_THREAD_FAILED;
    }

    if(pTimer)
    {
        *pTimer = timerCtx;
    }
    iUTL_TimerLifecycleEndOp();
    return ENT_SYS_NORMAL;
}

static MSG_ID_T iUTL_TimerDeleteTimer(UTL_TIMER_T* pTimer)
{
    MSG_ID_T      sts = 0;
    DLL_D_HDR*    tmpDll;
    PTIMER_CTX_T  timerCtx = NULL;

    if(pTimer == NULL)
    {
        IENT_LOG_ERROR("unvalid arg\n");
        return ENT_TMR_BAD_ARGUMENT;
    }

    timerCtx = (PTIMER_CTX_T)*pTimer;
    if(timerCtx == NULL || timerCtx->tag != UTL_TIMER_TAG)
    {
        IENT_LOG_WARN("unvalid timer\n");
        return ENT_TMR_BAD_ARGUMENT;
    }

    iUTL_TimerThreadStateStop(timerCtx, FALSE);
    if(pthread_equal(pthread_self(), timerCtx->timerThread))
    {
        iUTL_TimerLifecycleRetainOp();
        iUTL_TimerThreadStateStop(timerCtx, TRUE);
        *pTimer = NULL;
        return ENT_SYS_NORMAL;
    }

    pthread_join(timerCtx->timerThread, NULL);

    UTL_LockEnter(sTimerCtx.dllLock);
    sts = UTL_DllRemCurr((DLL_D_HDR*)timerCtx,&tmpDll);
    UTL_LockLeave(sTimerCtx.dllLock);
    UTL_LockClose(&timerCtx->stateLock);
    memset(timerCtx,0,sizeof(TIMER_CTX_T));

    free(timerCtx);
    *pTimer = NULL;
    if(sts < 0)
    {
        return ENT_TMR_LIST_FAILED;
    }
    return ENT_SYS_NORMAL;
}

ENT_PUBLIC MSG_ID_T UTL_TimerDelete(UTL_TIMER_T* pTimer)
{
    MSG_ID_T sts = 0;

    if(pTimer == NULL)
    {
        IENT_LOG_ERROR("unvalid arg\n");
        return ENT_TMR_BAD_ARGUMENT;
    }

    if(!iUTL_TimerLifecycleBeginOp())
    {
        IENT_LOG_ERROR("uninitialized or closing\n");
        return ENT_TMR_NOT_INITIALIZED;
    }
    sts = iUTL_TimerDeleteTimer(pTimer);
    iUTL_TimerLifecycleEndOp();
    return sts;
}
#endif

ENT_PUBLIC MSG_ID_T  UTL_Sleep(int ms)
{
#ifdef _WIN32
    Sleep(ms);
#else
    iUTL_TimerSleepMs(ms);
#endif
    return ENT_SYS_NORMAL;
}
