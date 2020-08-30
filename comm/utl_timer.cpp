/*-----------------------------------------------------------------------------
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
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
#endif
#include <stdlib.h>
#include <string.h>
#include "ient_comm.h"
#include "ent_utility.h"

typedef struct
{
    DLL_D_HDR         dllLnk;
    unsigned int      tag;
    void*             data;
    UTL_TIMER_EV_F    timer_ev_cb;
    bool              isEnable;
    int               ms;
    unsigned int      timerType;
#ifdef WIN32
    UINT              timerId;
#else
    timer_t           timerId;
#endif
} TIMER_CTX_T ,*PTIMER_CTX_T;

typedef struct ENT_TH_CTX
{
    UTL_LOCK          dllLock;
    DLL_D_HDR         dllHeader;
}TIMER_TH_CTX;

#define UTL_TIMER_TAG  0xeb90eb90

static bool          sUtilTimerInit;
static TIMER_TH_CTX  sTimerCtx;
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
MSG_ID_T  UTL_TimerClose()
{
    MSG_ID_T     sts = 0;
    DLL_D_HDR*   tmpDll = NULL;
    if(!sUtilTimerInit)
    {
        IENT_LOG_ERROR("uninitialized\n");
        return -1;
    }

    while((sts = UTL_DllNextLe(&sTimerCtx.dllHeader,&tmpDll))==0)
    {
        sts = UTL_TimerDelete((UTL_TIMER_T*)&tmpDll);
        if(sts < 0)
        {
            IENT_LOG_ERROR("UTL_TimerDelete failed,sts [%d]\n",sts);
        }
    }
    UTL_LockClose(sTimerCtx.dllLock);
    sUtilTimerInit = false;
    return 0;
}

#ifdef WIN32
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
MSG_ID_T  UTL_TimerInit()
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
MSG_ID_T UTL_TimerCreate(UTL_TIMER_T* pTimer,unsigned int type, int ms,UTL_TIMER_EV_F evCb,void* data)
{
    PTIMER_CTX_T  timerCtx = NULL;
    UINT          fuEvent = TIME_PERIODIC;
    MSG_ID_T      sts = 0;

    if(!sUtilTimerInit)
    {
        IENT_LOG_ERROR("uninitialized\n");
        return -1;
    }

    timerCtx = (PTIMER_CTX_T)malloc(sizeof(TIMER_CTX_T));
    if(timerCtx == NULL)
    {
        IENT_LOG_ERROR("malloc failed,error [%d]\n",errno);
        return -2;
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
    if(timerCtx->timerId == NULL)
    {
        IENT_LOG_ERROR("timeSetEvent failed,error [%d]\n",errno);
        free(timerCtx);
        return -2;
    }

    UTL_LockEnter(sTimerCtx.dllLock);
    sts = UTL_DllInsHead(&sTimerCtx.dllHeader,(DLL_D_HDR*)timerCtx);
    UTL_LockLeave(sTimerCtx.dllLock);

    if(pTimer)
    {
        *pTimer = timerCtx;
    }
    return 0;
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
MSG_ID_T UTL_TimerDelete(UTL_TIMER_T* pTimer)
{
    MSG_ID_T      sts = 0;
    PTIMER_CTX_T  timerCtx = NULL;
    DLL_D_HDR*    tmpDll;
    if(pTimer == NULL)
    {
        IENT_LOG_ERROR("unvalid arg\n");
        return -1;
    }

    if(!sUtilTimerInit)
    {
        IENT_LOG_ERROR("uninitialized\n");
        return -2;
    }
    timerCtx = (PTIMER_CTX_T)*pTimer;
    if(timerCtx == NULL )
    {
        IENT_LOG_WARN("unvalid timer\n");
        return -3;
    }

    if(timeKillEvent(timerCtx->timerId)== MMSYSERR_INVALPARAM )
    {
        IENT_LOG_ERROR("timeKillEvent failed,error [%d]\n",errno);
        return -4;
    }
    UTL_LockEnter(sTimerCtx.dllLock);
    sts = UTL_DllRemCurr((DLL_D_HDR*)timerCtx,&tmpDll);
    UTL_LockLeave(sTimerCtx.dllLock);

    memset(timerCtx,0,sizeof(TIMER_CTX_T));
    free(timerCtx);
    *pTimer = NULL;
    return 0;
}

#else
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
static void thread_handler(union sigval si)
{
    PTIMER_CTX_T timerId = (PTIMER_CTX_T)si.sival_ptr;
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
MSG_ID_T  UTL_TimerInit()
{
    MSG_ID_T  sts = 0;
    struct sigaction sa;
    if(sUtilTimerInit)
    {
        IENT_LOG_WARN("initialized already\n");
        return 0;
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
        return -1;
    }
    sUtilTimerInit = true;
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
MSG_ID_T UTL_TimerCreate(UTL_TIMER_T* pTimer,unsigned int type, int ms,UTL_TIMER_EV_F evCb,void* data)
{
    MSG_ID_T      sts = 0;
    PTIMER_CTX_T  timerCtx = NULL;
    struct sigevent sev;
    struct itimerspec its;

    if(!sUtilTimerInit)
    {
        IENT_LOG_ERROR("uninitialized\n");
        return -1;
    }

    timerCtx = (PTIMER_CTX_T)malloc(sizeof(TIMER_CTX_T));
    if(timerCtx == NULL)
    {
        IENT_LOG_ERROR("malloc failed,error [%d]->[%s]\n",errno,strerror(errno));
        return -2;
    }
    memset(timerCtx,0,sizeof(TIMER_CTX_T));
    timerCtx->tag         = UTL_TIMER_TAG;
    timerCtx->timerType   = type;
    timerCtx->timer_ev_cb = evCb;
    timerCtx->data        = data;
    timerCtx->ms          = ms;

    /* Create the timer */
    if(type&UTL_TIMER_E_SIGNAL)
    {
        sev.sigev_notify = SIGEV_SIGNAL;
        sev.sigev_signo = SIG_UTL;
        sev.sigev_value.sival_ptr = timerCtx;
    }
    else
    {
        sev.sigev_notify = SIGEV_THREAD;
        sev.sigev_signo = SIG_UTL;
        sev.sigev_value.sival_ptr = timerCtx;
        sev.sigev_notify_function = thread_handler;
        sev.sigev_notify_attributes = NULL;    
    }

    if(timer_create(CLOCKID, &sev, &timerCtx->timerId) == -1)
    {
        IENT_LOG_ERROR("timer_create failed,error [%d]->[%s]\n",errno,strerror(errno));
        free(timerCtx);
        return -1;
    }
    
    IENT_LOG_DEBUG("timer ID is 0x%lx\n", (long) timerCtx->timerId);
    
    /* Start the timer */
    its.it_value.tv_sec  = ms/1000;
    its.it_value.tv_nsec = (ms%1000)*1000000;
    its.it_interval.tv_sec = its.it_value.tv_sec;
    its.it_interval.tv_nsec = its.it_value.tv_nsec;

    if(timer_settime(timerCtx->timerId, 0, &its, NULL) == -1)
    {
        IENT_LOG_ERROR("timer_settime failed,error [%d]->[%s]\n",errno,strerror(errno));
        timer_delete(timerCtx->timerId);
        return -2;
    }
    timerCtx->isEnable = true;

    UTL_LockEnter(sTimerCtx.dllLock);
    sts = UTL_DllInsHead(&sTimerCtx.dllHeader,(DLL_D_HDR*)timerCtx);
    UTL_LockLeave(sTimerCtx.dllLock);

    if(pTimer)
    {
        *pTimer = timerCtx;
    }
    return 0;
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
MSG_ID_T UTL_TimerDelete(UTL_TIMER_T* pTimer)
{
    MSG_ID_T      sts = 0;
    DLL_D_HDR*    tmpDll;
    PTIMER_CTX_T  timerCtx = NULL;
    
    if(pTimer == NULL)
    {
        IENT_LOG_ERROR("unvalid arg\n");
        return -1;
    }

    if(!sUtilTimerInit)
    {
        IENT_LOG_ERROR("uninitialized\n");
        return -2;
    }
    timerCtx = (PTIMER_CTX_T)*pTimer;
    if(timerCtx == NULL || timerCtx->tag != UTL_TIMER_TAG)
    {
        IENT_LOG_WARN("unvalid timer\n");
        return -3;
    }

    if(timer_delete(timerCtx->timerId)==-1)
    {
        IENT_LOG_ERROR("timer_delete failed,error [%d]->[%s]\n",errno,strerror(errno));
        return -4;
    }

    UTL_LockEnter(sTimerCtx.dllLock);
    sts = UTL_DllRemCurr((DLL_D_HDR*)timerCtx,&tmpDll);
    UTL_LockLeave(sTimerCtx.dllLock);
    memset(timerCtx,0,sizeof(TIMER_CTX_T));

    free(timerCtx);
    *pTimer = NULL;
    return 0;
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
MSG_ID_T  UTL_Sleep(int ms)
{
#ifdef WIN32
    Sleep(ms);
#else
    struct timespec tv;
    sigset_t  sigUtl;
    sigfillset(&sigUtl);
    tv.tv_sec  = ms/1000;
    tv.tv_nsec = (ms%1000)*1000000;  
    pselect(0, NULL, NULL, NULL, &tv,&sigUtl);
#endif
    return 0;
}
