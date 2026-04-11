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
#include "windows.h"
#else
#include <pthread.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/time.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#endif
#include <stdlib.h>
#include <string.h>
#include "ient_comm.h"
#include "ent_msg.h"
#include "ent_utility.h"

#ifdef WIN32
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
    timer_t           timerId;
    pthread_t         timerThread;
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

#if ENT_TMR_IMPL_LINUX
static long long iUTL_TimerMonotonicNs(void);
static void* iUTL_TimerRtWorker(void* data);
static void* iUTL_TimerCallbackWorker(void* data);
static MSG_ID_T iUTL_TimerCreateRt(UTL_TIMER_T* pTimer,unsigned int type,int period_us,UTL_TIMER_EV_F evCb,void* data);
static MSG_ID_T iUTL_TimerDeleteRt(PTIMER_CTX_T timerCtx);
#endif

#if ENT_TMR_IMPL_LINUX || ENT_TMR_IMPL_POSIX_FALLBACK
static void* iUTL_TimerThread(void* data);
#endif

#ifndef WIN32
static void iUTL_TimerSleepMs(int ms)
{
    struct timespec tv;
    sigset_t  sigUtl;
    sigfillset(&sigUtl);
    tv.tv_sec  = ms/1000;
    tv.tv_nsec = (ms%1000)*1000000;
    pselect(0, NULL, NULL, NULL, &tv, &sigUtl);
}
#endif

#if ENT_TMR_IMPL_LINUX || ENT_TMR_IMPL_POSIX_FALLBACK
static void* iUTL_TimerThread(void* data)
{
    PTIMER_CTX_T timerCtx = (PTIMER_CTX_T)data;

    if(timerCtx == NULL || timerCtx->tag != UTL_TIMER_TAG)
    {
        return NULL;
    }

    while(timerCtx->isEnable)
    {
        iUTL_TimerSleepMs(timerCtx->ms);
        if(!timerCtx->isEnable)
        {
            break;
        }

        if(timerCtx->timer_ev_cb)
        {
            timerCtx->timer_ev_cb(timerCtx->data);
        }

        if(timerCtx->timerType & UTL_TIMER_E_ONESHOT)
        {
            timerCtx->isEnable = false;
            break;
        }
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
    if(!sUtilTimerInit)
    {
        IENT_LOG_ERROR("uninitialized\n");
        return ENT_TMR_NOT_INITIALIZED;
    }

    while((sts = UTL_DllNextLe(&sTimerCtx.dllHeader,&tmpDll)) == ENT_SYS_NORMAL)
    {
        sts = UTL_TimerDelete((UTL_TIMER_T*)&tmpDll);
        if(sts < 0)
        {
            IENT_LOG_ERROR("UTL_TimerDelete failed,sts [%d]\n",sts);
        }
    }
    UTL_LockClose(sTimerCtx.dllLock);
    sUtilTimerInit = false;
    return ENT_SYS_NORMAL;
}

#if ENT_TMR_IMPL_WINDOWS
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :iUTL_TimerWinCb
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
static void CALLBACK iUTL_TimerWinCb(UINT uTimerID, UINT uMsg, DWORD_PTR dwUser, DWORD_PTR dw1, DWORD_PTR dw2)
{
    PTIMER_CTX_T timerCtx = (PTIMER_CTX_T)dwUser;
    if(timerCtx == NULL || timerCtx->tag!=UTL_TIMER_TAG)
    {
        IENT_LOG_ERROR("unvalid arg\n");
        return;
    }

    if(timerCtx->timer_ev_cb)
    {
        timerCtx->timer_ev_cb(timerCtx->data);
    }
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :UTL_TimerInit
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
ENT_PUBLIC MSG_ID_T  UTL_TimerInit()
{
    MSG_ID_T  sts = 0;
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
            UTL_LockClose(sTimerCtx.dllLock);
            IENT_LOG_ERROR("UTL_DllInitHead failed,sts [%d].\n",sts);
            goto END_OF_ROUTINE;
        }
        sUtilTimerInit = true;
    }

END_OF_ROUTINE:
    return sts;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :UTL_TimerCreate
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
ENT_PUBLIC MSG_ID_T UTL_TimerCreate(UTL_TIMER_T* pTimer,unsigned int type, int ms,UTL_TIMER_EV_F evCb,void* data)
{
    PTIMER_CTX_T  timerCtx = NULL;
    UINT          fuEvent = TIME_PERIODIC;
    MSG_ID_T      sts = 0;

    if(pTimer)
    {
        *pTimer = NULL;
    }

    if(!sUtilTimerInit)
    {
        IENT_LOG_ERROR("uninitialized\n");
        return ENT_TMR_NOT_INITIALIZED;
    }

    timerCtx = (PTIMER_CTX_T)malloc(sizeof(TIMER_CTX_T));
    if(timerCtx == NULL)
    {
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

    timerCtx->timerId = timeSetEvent(ms,0,iUTL_TimerWinCb,(DWORD_PTR)timerCtx,fuEvent|TIME_KILL_SYNCHRONOUS);
    if(timerCtx->timerId == 0)
    {
        IENT_LOG_ERROR("timeSetEvent failed,error [%d]\n",errno);
        free(timerCtx);
        return ENT_TMR_CREATE_FAILED;
    }

    UTL_LockEnter(sTimerCtx.dllLock);
    sts = UTL_DllInsHead(&sTimerCtx.dllHeader,(DLL_D_HDR*)timerCtx);
    UTL_LockLeave(sTimerCtx.dllLock);

    if(sts < 0)
    {
        timeKillEvent(timerCtx->timerId);
        memset(timerCtx,0,sizeof(TIMER_CTX_T));
        free(timerCtx);
        return ENT_TMR_LIST_FAILED;
    }

    if(pTimer)
    {
        *pTimer = timerCtx;
    }
    return ENT_SYS_NORMAL;
}

/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :UTL_TimerDelete
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
ENT_PUBLIC MSG_ID_T UTL_TimerDelete(UTL_TIMER_T* pTimer)
{
    MSG_ID_T      sts = 0;
    PTIMER_CTX_T  timerCtx = NULL;
    DLL_D_HDR*    tmpDll;
    if(pTimer == NULL)
    {
        IENT_LOG_ERROR("unvalid arg\n");
        return ENT_TMR_BAD_ARGUMENT;
    }

    if(!sUtilTimerInit)
    {
        IENT_LOG_ERROR("uninitialized\n");
        return ENT_TMR_NOT_INITIALIZED;
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

#elif ENT_TMR_IMPL_LINUX
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :timer_handler
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
#define CLOCKID CLOCK_REALTIME
#define SIG_UTL SIGRTMIN
static void timer_handler(int sig, siginfo_t *si, void *uc)
{
    PTIMER_CTX_T timerId = (PTIMER_CTX_T)si->si_value.sival_ptr;
    if(timerId == NULL || timerId->isEnable==false || timerId->tag!=UTL_TIMER_TAG)
    {
        return;
    }

    if(timerId->timer_ev_cb)
    {
        timerId->timer_ev_cb(timerId->data);
    }

    if(timerId->timerType&UTL_TIMER_E_ONESHOT)
    {
        UTL_TimerDelete((UTL_TIMER_T*)&timerId);
    }
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :timer_handler
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

static void* iUTL_TimerRtWorker(void* data)
{
    PTIMER_CTX_T timerCtx = (PTIMER_CTX_T)data;

    if(timerCtx == NULL || timerCtx->tag != UTL_TIMER_TAG)
    {
        return NULL;
    }

    timerCtx->next_deadline_ns = iUTL_TimerMonotonicNs() + timerCtx->period_ns;
    while(!timerCtx->stopWorker)
    {
        long long deadline = timerCtx->next_deadline_ns;
        int sleepFailed = 0;

        while(!timerCtx->stopWorker)
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
            while(sleepSts == EINTR && !timerCtx->stopWorker);

            if(sleepSts != 0 && sleepSts != EINTR)
            {
                IENT_LOG_ERROR("clock_nanosleep failed,error [%d]\n",sleepSts);
                sleepFailed = 1;
                break;
            }
        }

        if(sleepFailed)
        {
            break;
        }

        if(timerCtx->stopWorker)
        {
            break;
        }

        UTL_LockEnter(timerCtx->cbLock);
        BOOL needWake = (timerCtx->pendingCallbacks == 0) ? TRUE : FALSE;
        timerCtx->pendingCallbacks++;
        UTL_LockLeave(timerCtx->cbLock);
        if(needWake)
        {
            UTL_CVWake(timerCtx->cbCv);
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
            if(timerCb)
            {
                timerCb(timerData);
            }
            callbackBatch--;
            if(oneshot)
            {
                break;
            }
        }

        if(oneshot)
        {
            break;
        }
    }

    return NULL;
}

static MSG_ID_T iUTL_TimerCreateRt(UTL_TIMER_T* pTimer,unsigned int type,int period_us,UTL_TIMER_EV_F evCb,void* data)
{
    MSG_ID_T      sts = 0;
    PTIMER_CTX_T  timerCtx = NULL;

    if(pTimer)
    {
        *pTimer = NULL;
    }

    if(!sUtilTimerInit)
    {
        IENT_LOG_ERROR("uninitialized\n");
        return ENT_TMR_NOT_INITIALIZED;
    }
    if(period_us <= 0)
    {
        IENT_LOG_ERROR("invalid period_us [%d]\n",period_us);
        return ENT_TMR_BAD_ARGUMENT;
    }

    timerCtx = (PTIMER_CTX_T)malloc(sizeof(TIMER_CTX_T));
    if(timerCtx == NULL)
    {
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
        free(timerCtx);
        return ENT_TMR_THREAD_FAILED;
    }
    sts = UTL_CVInit(&timerCtx->cbCv,"timer_rt_cb");
    if(sts < 0)
    {
        UTL_LockClose(timerCtx->cbLock);
        free(timerCtx);
        return ENT_TMR_THREAD_FAILED;
    }

    if(pthread_create(&timerCtx->cbWorker, NULL, iUTL_TimerCallbackWorker, timerCtx) != 0)
    {
        IENT_LOG_ERROR("pthread_create callback worker failed,error [%d]->[%s]\n",errno,strerror(errno));
        UTL_CVClose(timerCtx->cbCv);
        UTL_LockClose(timerCtx->cbLock);
        free(timerCtx);
        return ENT_TMR_THREAD_FAILED;
    }
    if(pthread_create(&timerCtx->rtWorker, NULL, iUTL_TimerRtWorker, timerCtx) != 0)
    {
        IENT_LOG_ERROR("pthread_create rt worker failed,error [%d]->[%s]\n",errno,strerror(errno));
        timerCtx->stopCallbackWorker = TRUE;
        UTL_CVWakeAll(timerCtx->cbCv);
        pthread_join(timerCtx->cbWorker, NULL);
        UTL_CVClose(timerCtx->cbCv);
        UTL_LockClose(timerCtx->cbLock);
        free(timerCtx);
        return ENT_TMR_THREAD_FAILED;
    }

    UTL_LockEnter(sTimerCtx.dllLock);
    sts = UTL_DllInsHead(&sTimerCtx.dllHeader,(DLL_D_HDR*)timerCtx);
    UTL_LockLeave(sTimerCtx.dllLock);
    if(sts < 0)
    {
        timerCtx->stopWorker = TRUE;
        timerCtx->stopCallbackWorker = TRUE;
        UTL_CVWakeAll(timerCtx->cbCv);
        pthread_join(timerCtx->rtWorker, NULL);
        pthread_join(timerCtx->cbWorker, NULL);
        UTL_CVClose(timerCtx->cbCv);
        UTL_LockClose(timerCtx->cbLock);
        free(timerCtx);
        return ENT_TMR_LIST_FAILED;
    }

    if(pTimer)
    {
        *pTimer = timerCtx;
    }
    return ENT_SYS_NORMAL;
}

static MSG_ID_T iUTL_TimerDeleteRt(PTIMER_CTX_T timerCtx)
{
    MSG_ID_T     sts = 0;
    DLL_D_HDR*   tmpDll = NULL;

    if(timerCtx == NULL || timerCtx->tag != UTL_TIMER_TAG)
    {
        IENT_LOG_WARN("unvalid timer\n");
        return ENT_TMR_BAD_ARGUMENT;
    }

    timerCtx->isEnable = false;
    timerCtx->stopWorker = TRUE;
    timerCtx->stopCallbackWorker = TRUE;
    UTL_CVWakeAll(timerCtx->cbCv);

    pthread_join(timerCtx->rtWorker, NULL);
    pthread_join(timerCtx->cbWorker, NULL);

    UTL_LockEnter(sTimerCtx.dllLock);
    sts = UTL_DllRemCurr((DLL_D_HDR*)timerCtx,&tmpDll);
    UTL_LockLeave(sTimerCtx.dllLock);

    UTL_CVClose(timerCtx->cbCv);
    UTL_LockClose(timerCtx->cbLock);
    memset(timerCtx,0,sizeof(TIMER_CTX_T));
    free(timerCtx);
    if(sts < 0)
    {
        return ENT_TMR_LIST_FAILED;
    }
    return ENT_SYS_NORMAL;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :UTL_TimerInit
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
ENT_PUBLIC MSG_ID_T  UTL_TimerInit()
{
    MSG_ID_T  sts = 0;
    struct sigaction sa;
    if(sUtilTimerInit)
    {
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
        UTL_LockClose(sTimerCtx.dllLock);
        IENT_LOG_ERROR("UTL_DllInitHead failed,sts [%d].\n",sts);
        goto END_OF_ROUTINE;
    }
    /* Establish handler for timer signal */
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = timer_handler;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIG_UTL, &sa, NULL) == -1)
    {
        UTL_LockClose(sTimerCtx.dllLock);
        IENT_LOG_ERROR("sigaction failed,error [%d]->[%s]\n",errno,strerror(errno));
        return ENT_TMR_CREATE_FAILED;
    }
    sUtilTimerInit = true;
END_OF_ROUTINE:
    if(sts < 0)
    {
        return ENT_TMR_THREAD_FAILED;
    }
    return ENT_SYS_NORMAL;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :UTL_TimerCreate
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
ENT_PUBLIC MSG_ID_T UTL_TimerCreate(UTL_TIMER_T* pTimer,unsigned int type, int ms,UTL_TIMER_EV_F evCb,void* data)
{
    MSG_ID_T      sts = 0;
    PTIMER_CTX_T  timerCtx = NULL;
    struct sigevent sev;
    struct itimerspec its;

    if(pTimer)
    {
        *pTimer = NULL;
    }

    if(!sUtilTimerInit)
    {
        IENT_LOG_ERROR("uninitialized\n");
        return ENT_TMR_NOT_INITIALIZED;
    }

    timerCtx = (PTIMER_CTX_T)malloc(sizeof(TIMER_CTX_T));
    if(timerCtx == NULL)
    {
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

    if(!(type&UTL_TIMER_E_SIGNAL))
    {
        if(pthread_create(&timerCtx->timerThread, NULL, iUTL_TimerThread, timerCtx) != 0)
        {
            IENT_LOG_ERROR("pthread_create failed,error [%d]->[%s]\n",errno,strerror(errno));
            free(timerCtx);
            return ENT_TMR_THREAD_FAILED;
        }

        UTL_LockEnter(sTimerCtx.dllLock);
        sts = UTL_DllInsHead(&sTimerCtx.dllHeader,(DLL_D_HDR*)timerCtx);
        UTL_LockLeave(sTimerCtx.dllLock);
        if(sts < 0)
        {
            timerCtx->isEnable = false;
            pthread_join(timerCtx->timerThread, NULL);
            free(timerCtx);
            return ENT_TMR_LIST_FAILED;
        }

        if(pTimer)
        {
            *pTimer = timerCtx;
        }
        return ENT_SYS_NORMAL;
    }

    /* Create the timer */
    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = SIG_UTL;
    sev.sigev_value.sival_ptr = timerCtx;

    if(timer_create(CLOCKID, &sev, &timerCtx->timerId) == -1)
    {
        IENT_LOG_ERROR("timer_create failed,error [%d]->[%s]\n",errno,strerror(errno));
        free(timerCtx);
        return ENT_TMR_CREATE_FAILED;
    }
    
    IENT_LOG_DEBUG("timer ID is 0x%lx\n", (long) timerCtx->timerId);
    
    /* Start the timer */
    its.it_value.tv_sec  = ms/1000;
    its.it_value.tv_nsec = (ms%1000)*1000000;
    if(type&UTL_TIMER_E_ONESHOT)
    {
        its.it_interval.tv_sec = 0;
        its.it_interval.tv_nsec = 0;
    }
    else
    {
        its.it_interval.tv_sec = its.it_value.tv_sec;
        its.it_interval.tv_nsec = its.it_value.tv_nsec;
    }

    if(timer_settime(timerCtx->timerId, 0, &its, NULL) == -1)
    {
        IENT_LOG_ERROR("timer_settime failed,error [%d]->[%s]\n",errno,strerror(errno));
        timer_delete(timerCtx->timerId);
        free(timerCtx);
        return ENT_TMR_START_FAILED;
    }
    UTL_LockEnter(sTimerCtx.dllLock);
    sts = UTL_DllInsHead(&sTimerCtx.dllHeader,(DLL_D_HDR*)timerCtx);
    UTL_LockLeave(sTimerCtx.dllLock);

    if(sts < 0)
    {
        timer_delete(timerCtx->timerId);
        free(timerCtx);
        return ENT_TMR_LIST_FAILED;
    }

    if(pTimer)
    {
        *pTimer = timerCtx;
    }
    return ENT_SYS_NORMAL;
}

ENT_PUBLIC MSG_ID_T UTL_TimerCreateUs(UTL_TIMER_T* pTimer,unsigned int type, int period_us,UTL_TIMER_EV_F evCb,void* data)
{
    return iUTL_TimerCreateRt(pTimer, type, period_us, evCb, data);
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :UTL_TimerDelete
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
ENT_PUBLIC MSG_ID_T UTL_TimerDelete(UTL_TIMER_T* pTimer)
{
    MSG_ID_T      sts = 0;
    DLL_D_HDR*    tmpDll;
    PTIMER_CTX_T  timerCtx = NULL;
    
    if(pTimer == NULL)
    {
        IENT_LOG_ERROR("unvalid arg\n");
        return ENT_TMR_BAD_ARGUMENT;
    }

    if(!sUtilTimerInit)
    {
        IENT_LOG_ERROR("uninitialized\n");
        return ENT_TMR_NOT_INITIALIZED;
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

    if(!(timerCtx->timerType&UTL_TIMER_E_SIGNAL))
    {
        timerCtx->isEnable = false;
        if(!pthread_equal(pthread_self(), timerCtx->timerThread))
        {
            pthread_join(timerCtx->timerThread, NULL);
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

    if(timer_delete(timerCtx->timerId)==-1)
    {
        IENT_LOG_ERROR("timer_delete failed,error [%d]->[%s]\n",errno,strerror(errno));
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
    if(sUtilTimerInit)
    {
        IENT_LOG_WARN("initialized already\n");
        return ENT_SYS_NORMAL;
    }

    sts = UTL_LockInit(&sTimerCtx.dllLock,"UTL_TimerInit");
    if(sts<0)
    {
        IENT_LOG_ERROR("UTL_LockInit failed,sts [%d].\n",sts);
        return ENT_TMR_THREAD_FAILED;
    }
    sts = UTL_DllInitHead(&sTimerCtx.dllHeader);
    if(sts<0)
    {
        UTL_LockClose(sTimerCtx.dllLock);
        IENT_LOG_ERROR("UTL_DllInitHead failed,sts [%d].\n",sts);
        return ENT_TMR_LIST_FAILED;
    }
    sUtilTimerInit = true;
    return ENT_SYS_NORMAL;
}

ENT_PUBLIC MSG_ID_T UTL_TimerCreate(UTL_TIMER_T* pTimer,unsigned int type, int ms,UTL_TIMER_EV_F evCb,void* data)
{
    MSG_ID_T      sts = 0;
    PTIMER_CTX_T  timerCtx = NULL;

    if(pTimer)
    {
        *pTimer = NULL;
    }

    if(!sUtilTimerInit)
    {
        IENT_LOG_ERROR("uninitialized\n");
        return ENT_TMR_NOT_INITIALIZED;
    }

    timerCtx = (PTIMER_CTX_T)malloc(sizeof(TIMER_CTX_T));
    if(timerCtx == NULL)
    {
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

    if(pthread_create(&timerCtx->timerThread, NULL, iUTL_TimerThread, timerCtx) != 0)
    {
        IENT_LOG_ERROR("pthread_create failed,error [%d]->[%s]\n",errno,strerror(errno));
        free(timerCtx);
        return ENT_TMR_THREAD_FAILED;
    }

    UTL_LockEnter(sTimerCtx.dllLock);
    sts = UTL_DllInsHead(&sTimerCtx.dllHeader,(DLL_D_HDR*)timerCtx);
    UTL_LockLeave(sTimerCtx.dllLock);

    if(pTimer)
    {
        *pTimer = timerCtx;
    }
    if(sts < 0)
    {
        timerCtx->isEnable = false;
        pthread_join(timerCtx->timerThread, NULL);
        free(timerCtx);
        return ENT_TMR_LIST_FAILED;
    }
    return ENT_SYS_NORMAL;
}

ENT_PUBLIC MSG_ID_T UTL_TimerDelete(UTL_TIMER_T* pTimer)
{
    MSG_ID_T      sts = 0;
    DLL_D_HDR*    tmpDll;
    PTIMER_CTX_T  timerCtx = NULL;

    if(pTimer == NULL)
    {
        IENT_LOG_ERROR("unvalid arg\n");
        return ENT_TMR_BAD_ARGUMENT;
    }

    if(!sUtilTimerInit)
    {
        IENT_LOG_ERROR("uninitialized\n");
        return ENT_TMR_NOT_INITIALIZED;
    }
    timerCtx = (PTIMER_CTX_T)*pTimer;
    if(timerCtx == NULL || timerCtx->tag != UTL_TIMER_TAG)
    {
        IENT_LOG_WARN("unvalid timer\n");
        return ENT_TMR_BAD_ARGUMENT;
    }

    timerCtx->isEnable = false;
    if(!pthread_equal(pthread_self(), timerCtx->timerThread))
    {
        pthread_join(timerCtx->timerThread, NULL);
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
#endif

/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :UTL_Sleep
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
ENT_PUBLIC MSG_ID_T  UTL_Sleep(int ms)
{
#ifdef WIN32
    Sleep(ms);
#else
    iUTL_TimerSleepMs(ms);
#endif
    return ENT_SYS_NORMAL;
}
