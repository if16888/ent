/*-----------------------------------------------------------------------------
 *   Copyright 2019 Fei Li<if16888@foxmail.com>
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
#endif
#include <stdlib.h>
#include <string.h>
#include "ient_comm.h"
#include "ent_utility.h"


typedef struct 
{
    UTL_LOCK_TYPE_T  lockType;
    union
    {
#ifdef WIN32
        CRITICAL_SECTION   cs;
        SRWLOCK            rw;
        CRITICAL_SECTION   spin;
#else
        pthread_mutex_t    cs;
        pthread_rwlock_t   rw;
        pthread_spinlock_t spin;
#endif
    } lock;
    char* lockName;
} UTL_TH_LOCK;

typedef struct
{
#ifdef WIN32
    CONDITION_VARIABLE    cv;
#else
    pthread_cond_t        cv;
#endif
    char*  cvName;
}UTL_TH_CV;

/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :UTL_LockInit
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
MSG_ID_T  UTL_LockInit(UTL_LOCK* lock,const char* name)
{
    if(lock==NULL)
    {
        IENT_LOG_ERROR("lock handle is null\n");
        return -1;
    }

    UTL_TH_LOCK* tmp = (UTL_TH_LOCK*)malloc(sizeof(UTL_TH_LOCK));
    if(tmp==NULL)
    {
        IENT_LOG_ERROR("lock malloc is null\n");
        return -2;
    }
    
    tmp->lockType = LOCK_MUTEX_E;
#ifdef WIN32
    if(name == NULL)
        tmp->lockName = NULL;
    else
        tmp->lockName = _strdup(name);
    InitializeCriticalSection(&tmp->lock.cs);
#else
    if(name == NULL)
        tmp->lockName = NULL;
    else
        tmp->lockName = strdup(name);
    pthread_mutex_init(&tmp->lock.cs,NULL);
#endif
    *lock = tmp;
    return 0;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :iUTL_LockInitRW
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
static MSG_ID_T  iUTL_LockInitRW(UTL_LOCK* lock,const char* name)
{
    UTL_TH_LOCK* tmp = (UTL_TH_LOCK*)malloc(sizeof(UTL_TH_LOCK));
    if(tmp==NULL)
    {
        IENT_LOG_ERROR("lock malloc is null\n");
        return -2;
    }
    tmp->lockType = LOCK_RW_E;
#ifdef WIN32
    if(name == NULL)
        tmp->lockName = NULL;
    else
        tmp->lockName = _strdup(name);
    InitializeSRWLock(&tmp->lock.rw);
#else
    if(name == NULL)
        tmp->lockName = NULL;
    else
        tmp->lockName = strdup(name);
    pthread_rwlock_init(&tmp->lock.rw,NULL);
#endif
    *lock = tmp;
    return 0;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :iUTL_LockInitSpin
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
static MSG_ID_T  iUTL_LockInitSpin(UTL_LOCK* lock,const char* name)
{
    UTL_TH_LOCK* tmp = (UTL_TH_LOCK*)malloc(sizeof(UTL_TH_LOCK));
    if(tmp==NULL)
    {
        IENT_LOG_ERROR("lock malloc is null\n");
        return -2;
    }
    tmp->lockType = LOCK_SPIN_E;
#ifdef WIN32
    if(name == NULL)
        tmp->lockName = NULL;
    else
        tmp->lockName = _strdup(name);
    if(!InitializeCriticalSectionAndSpinCount(&tmp->lock.spin,4000))
    {
        IENT_LOG_ERROR("init failed,error [%d]\n",GetLastError());
        if(tmp->lockName) free(tmp->lockName);
        free(tmp);
        *lock = NULL;
        return -3;
    }
#else
    if(name == NULL)
        tmp->lockName = NULL;
    else
        tmp->lockName = strdup(name);
    pthread_spin_init(&tmp->lock.spin,PTHREAD_PROCESS_PRIVATE);
#endif
    *lock = tmp;
    return 0;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :UTL_LockInitEx
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
MSG_ID_T  UTL_LockInitEx(UTL_LOCK* lock,const char* name,UTL_LOCK_TYPE_T type)
{
    MSG_ID_T sts = 0;
    if(lock==NULL)
    {
        IENT_LOG_ERROR("lock handle is null\n");
        return -1;
    }
    switch(type)
    {
        case LOCK_MUTEX_E:
            sts = UTL_LockInit(lock,name);
            break;

        case LOCK_RW_E:
            sts = iUTL_LockInitRW(lock,name);
            break;

        case LOCK_SPIN_E:
            sts = iUTL_LockInitSpin(lock,name);
            break;

        default:
            IENT_LOG_ERROR("unknow lock type [%d]\n",type);
            *lock = NULL;
            sts =-2;
            break;
    }

    return sts;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :iUTL_LockEnterMutex
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
static MSG_ID_T  iUTL_LockEnterMutex(UTL_LOCK lock)
{
    UTL_TH_LOCK* tmp = (UTL_TH_LOCK*)lock;

    //ENT_LOG_DEBUG("lock enter [%p]->[%s]\n",lock,tmp->lockName);
#ifdef WIN32
    EnterCriticalSection(&tmp->lock.cs);
#else
    pthread_mutex_lock(&tmp->lock.cs);
#endif
    return 0;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :iUTL_LockEnterRW
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
static MSG_ID_T  iUTL_LockEnterRW(UTL_LOCK lock,UTL_LOCK_RW_TYPE_T rwType)
{
    MSG_ID_T  sts = 0;
    UTL_TH_LOCK* tmp = (UTL_TH_LOCK*)lock;

    switch(rwType)
    {
        case RW_WRITE_E:
#ifdef WIN32
            AcquireSRWLockExclusive(&tmp->lock.rw);
#else
            pthread_rwlock_wrlock(&tmp->lock.rw);
#endif
            break;

        case RW_READ_E:
#ifdef WIN32
            AcquireSRWLockShared(&tmp->lock.rw);
#else
            pthread_rwlock_rdlock(&tmp->lock.rw);
#endif
            break;

        default:
            sts = -1;
            break;
    }
    return sts;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :iUTL_LockEnterSpin
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
static MSG_ID_T  iUTL_LockEnterSpin(UTL_LOCK lock)
{
    UTL_TH_LOCK* tmp = (UTL_TH_LOCK*)lock;

#ifdef WIN32
    EnterCriticalSection(&tmp->lock.spin);
#else
    pthread_spin_lock(&tmp->lock.spin);
#endif
    return 0;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :UTL_LockEnter
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
MSG_ID_T  UTL_LockEnter(UTL_LOCK lock)
{
    MSG_ID_T  sts = 0;
    if(lock==NULL)
    {
        IENT_LOG_ERROR("lock is null\n");
        return -1;
    }
    UTL_TH_LOCK* tmp = (UTL_TH_LOCK*)lock;

    switch(tmp->lockType)
    {
        case LOCK_MUTEX_E:
            sts = iUTL_LockEnterMutex(lock);
            break;

        case LOCK_SPIN_E:
            sts = iUTL_LockEnterSpin(lock);
            break;

        case LOCK_RW_E:
            sts = iUTL_LockEnterRW(lock,RW_WRITE_E);
            break;

        default:
            IENT_LOG_ERROR("unknow lock type [%d]\n",tmp->lockType);
            sts =-2;
            break;
    }
    
    return sts;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :UTL_LockEnterEx
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
MSG_ID_T  UTL_LockEnterEx(UTL_LOCK lock,UTL_LOCK_RW_TYPE_T rwType)
{
    MSG_ID_T  sts = 0;
    if(lock==NULL)
    {
        IENT_LOG_ERROR("lock is null\n");
        return -1;
    }
    UTL_TH_LOCK* tmp = (UTL_TH_LOCK*)lock;
    
    switch(tmp->lockType)
    {
        case LOCK_MUTEX_E:
            sts = iUTL_LockEnterMutex(lock);
            break;

        case LOCK_SPIN_E:
            sts = iUTL_LockEnterSpin(lock);
            break;

        case LOCK_RW_E:
            sts = iUTL_LockEnterRW(lock,rwType);
            break;

        default:
            IENT_LOG_ERROR("unknow lock type [%d]\n",tmp->lockType);
            sts =-2;
            break;
    }
    return sts;
}

/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :iUTL_LockLeaveMutex
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
static MSG_ID_T  iUTL_LockLeaveMutex(UTL_LOCK lock)
{
    UTL_TH_LOCK* tmp = (UTL_TH_LOCK*)lock;
#ifdef WIN32
    LeaveCriticalSection(&tmp->lock.cs);
#else
    pthread_mutex_unlock(&tmp->lock.cs);
#endif
    //ENT_LOG_DEBUG("lock leave [%p]->[%s]\n",lock,tmp->lockName);
    return 0;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :iUTL_LockLeaveRW
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
static MSG_ID_T  iUTL_LockLeaveRW(UTL_LOCK lock,UTL_LOCK_RW_TYPE_T rwType)
{
    MSG_ID_T  sts = 0;
    UTL_TH_LOCK* tmp = (UTL_TH_LOCK*)lock;

    switch(rwType)
    {
        case RW_WRITE_E:
#ifdef WIN32
            ReleaseSRWLockExclusive(&tmp->lock.rw);
#else
            pthread_rwlock_unlock(&tmp->lock.rw);
#endif
            break;

        case RW_READ_E:
#ifdef WIN32
            ReleaseSRWLockShared(&tmp->lock.rw);
#else
            pthread_rwlock_unlock(&tmp->lock.rw);
#endif
            break;

        default:
            IENT_LOG_ERROR("unknow lock type [%d]\n",tmp->lockType);
            sts = -1;
            break;
    }
    return sts;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :iUTL_LockLeaveSpin
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
static MSG_ID_T  iUTL_LockLeaveSpin(UTL_LOCK lock)
{
    UTL_TH_LOCK* tmp = (UTL_TH_LOCK*)lock;

#ifdef WIN32
    LeaveCriticalSection(&tmp->lock.spin);
#else
    pthread_spin_unlock(&tmp->lock.spin);
#endif
    return 0;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :UTL_LockLeave
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
MSG_ID_T  UTL_LockLeave(UTL_LOCK lock)
{
    MSG_ID_T  sts = 0;
    if(lock==NULL)
    {
        IENT_LOG_ERROR("lock is null\n");
        return -1;
    }
    UTL_TH_LOCK* tmp = (UTL_TH_LOCK*)lock;
    
    switch(tmp->lockType)
    {
        case LOCK_MUTEX_E:
            sts = iUTL_LockLeaveMutex(lock);
            break;

        case LOCK_SPIN_E:
            sts = iUTL_LockLeaveSpin(lock);
            break;

        case LOCK_RW_E:
            sts = iUTL_LockLeaveRW(lock,RW_WRITE_E);
            break;

        default:
            IENT_LOG_ERROR("unknow lock type [%d]\n",tmp->lockType);
            sts =-2;
            break;
    }
    return sts;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :UTL_LockLeaveEx
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
MSG_ID_T  UTL_LockLeaveEx(UTL_LOCK lock,UTL_LOCK_RW_TYPE_T rwType)
{
    MSG_ID_T  sts = 0;
    if(lock==NULL)
    {
        IENT_LOG_ERROR("lock is null\n");
        return -1;
    }
    UTL_TH_LOCK* tmp = (UTL_TH_LOCK*)lock;
    
    switch(tmp->lockType)
    {
        case LOCK_MUTEX_E:
            sts = iUTL_LockLeaveMutex(lock);
            break;

        case LOCK_SPIN_E:
            sts = iUTL_LockLeaveSpin(lock);
            break;

        case LOCK_RW_E:
            sts = iUTL_LockLeaveRW(lock,rwType);
            break;

        default:
            IENT_LOG_ERROR("unknow lock type [%d]\n",tmp->lockType);
            sts =-2;
            break;
    }
    return sts;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :iUTL_LockCloseMutex
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
static MSG_ID_T  iUTL_LockCloseMutex(UTL_LOCK lock)
{
    UTL_TH_LOCK* tmp = (UTL_TH_LOCK*)lock;
#ifdef WIN32
    DeleteCriticalSection(&tmp->lock.cs);
#else
    pthread_mutex_destroy(&tmp->lock.cs);
#endif
    if(tmp->lockName)
    {
        free(tmp->lockName);
    }
    free(tmp);
    return 0;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :UTL_LockClose
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
static MSG_ID_T  iUTL_LockCloseRW(UTL_LOCK lock)
{
    UTL_TH_LOCK* tmp = (UTL_TH_LOCK*)lock;
#ifdef WIN32
#else
    pthread_rwlock_destroy(&tmp->lock.rw);
#endif
    if(tmp->lockName)
    {
        free(tmp->lockName);
    }
    free(tmp);
    return 0;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :iUTL_LockCloseSpin
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
static MSG_ID_T  iUTL_LockCloseSpin(UTL_LOCK lock)
{
    UTL_TH_LOCK* tmp = (UTL_TH_LOCK*)lock;
#ifdef WIN32
    DeleteCriticalSection(&tmp->lock.spin);
#else
    pthread_spin_destroy(&tmp->lock.spin);
#endif
    if(tmp->lockName)
    {
        free(tmp->lockName);
    }
    free(tmp);
    return 0;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :UTL_LockClose
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
MSG_ID_T  UTL_LockClose(UTL_LOCK lock)
{
    MSG_ID_T  sts = 0;
    if(lock==NULL)
    {
        IENT_LOG_ERROR("lock handle is null\n");
        return -1;
    }

    UTL_TH_LOCK* tmp = (UTL_TH_LOCK*)lock;

    switch(tmp->lockType)
    {
        case LOCK_MUTEX_E:
            sts = iUTL_LockCloseMutex(lock);
            break;

        case LOCK_RW_E:
            sts = iUTL_LockCloseRW(lock);
            break;

        case LOCK_SPIN_E:
            sts = iUTL_LockCloseSpin(lock);
            break;

        default:
            IENT_LOG_ERROR("unknow lock type [%d]\n",tmp->lockType);
            sts =-2;
            break;
    }

    return sts;
}


/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :UTL_CVInit
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
MSG_ID_T  UTL_CVInit(UTL_CV* cv,const char* name)
{
    if(cv==NULL)
    {
        IENT_LOG_ERROR("cv handle is null\n");
        return -1;
    }

    UTL_TH_CV* tmp = (UTL_TH_CV*)malloc(sizeof(UTL_TH_CV));
    if(tmp==NULL)
    {
        IENT_LOG_ERROR("cv malloc is null\n");
        return -2;
    }
    
#ifdef WIN32
    if(name == NULL)
        tmp->cvName = NULL;
    else
        tmp->cvName = _strdup(name);
    InitializeConditionVariable(&tmp->cv);
#else
    if(name == NULL)
        tmp->cvName = NULL;
    else
        tmp->cvName = strdup(name);
    pthread_cond_init(&tmp->cv,NULL);
#endif
    *cv = tmp;
    return 0;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :UTL_CVClose
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
MSG_ID_T  UTL_CVClose(UTL_CV cv)
{
    if(cv==NULL)
    {
        IENT_LOG_ERROR("cv handle is null\n");
        return -1;
    }

    UTL_TH_CV* tmp = (UTL_TH_CV*)cv;
    
#ifdef WIN32
    //
#else
    pthread_cond_destroy(&tmp->cv);
#endif
    if(tmp->cvName)
    {
        free(tmp->cvName);
    }
    free(tmp);
    return 0;
}

static MSG_ID_T iUTL_CVWaitMutex(UTL_TH_CV* cvCtx,UTL_TH_LOCK* lockCtx,int ms)
{
#ifdef WIN32
    SleepConditionVariableCS(&cvCtx->cv,&lockCtx->lock.cs,ms>0?ms:INFINITE);
#else
    if(ms<=0)
    {
        pthread_cond_wait(&cvCtx->cv,&lockCtx->lock.cs);
    }
    else
    {
        timespec cvTm;
        if (clock_gettime(CLOCK_REALTIME, &cvTm) == -1)
        {
            IENT_LOG_ERROR("clock_gettime failed,error [%d]->[%s]\n",errno,strerror(errno));
            return -4;
        }
        cvTm.tv_sec += ms/1000;
        cvTm.tv_nsec += (ms%1000)*1000000;
        pthread_cond_timedwait(&cvCtx->cv,&lockCtx->lock.cs,&cvTm);
    }  
#endif
    return 0;
}

static MSG_ID_T iUTL_CVWaitRW(UTL_TH_CV* cvCtx,UTL_TH_LOCK* lockCtx,int ms,UTL_LOCK_RW_TYPE_T rwType)
{
#ifdef WIN32
    ULONG  flag = 0;
    if(rwType==RW_WRITE_E)
    {
        flag = CONDITION_VARIABLE_LOCKMODE_SHARED;
    }
    SleepConditionVariableSRW(&cvCtx->cv,&lockCtx->lock.rw,ms>0?ms:INFINITE,flag);
#else
    IENT_LOG_ERROR("unsupported rw lock type\n");
    return -2;
#endif
    return 0;
}

/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :UTL_CVWait
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
MSG_ID_T  UTL_CVWait(UTL_CV cv,UTL_LOCK lock,int ms,UTL_LOCK_RW_TYPE_T rwType)
{
    MSG_ID_T  sts = 0;
    if(cv==NULL || lock==NULL)
    {
        IENT_LOG_ERROR("cv handle is null\n");
        return -1;
    }

    UTL_TH_CV* cvCtx = (UTL_TH_CV*)cv;
    UTL_TH_LOCK* lockCtx = (UTL_TH_LOCK*)lock;
    int  ctxMs = 0;

    switch(lockCtx->lockType)
    {
        case LOCK_MUTEX_E:
            sts = iUTL_CVWaitMutex(cvCtx,lockCtx,ms);
            break;

        case LOCK_RW_E:
            sts = iUTL_CVWaitRW(cvCtx,lockCtx,ms,rwType);
            break;

        default:
            IENT_LOG_ERROR("unsupported lock type\n");
            return -3;
            break;
    }
    return sts;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :UTL_CVWake
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
MSG_ID_T  UTL_CVWake(UTL_CV cv)
{
    MSG_ID_T  sts = 0;
    if(cv==NULL)
    {
        IENT_LOG_ERROR("cv handle is null\n");
        return -1;
    }

    UTL_TH_CV* cvCtx = (UTL_TH_CV*)cv;
    
#ifdef WIN32
    WakeConditionVariable(&cvCtx->cv);
#else
    pthread_cond_signal(&cvCtx->cv);
#endif
    
    return 0;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :UTL_CVWakeAll
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
MSG_ID_T  UTL_CVWakeAll(UTL_CV cv)
{
    MSG_ID_T  sts = 0;
    if(cv==NULL)
    {
        IENT_LOG_ERROR("cv handle is null\n");
        return -1;
    }

    UTL_TH_CV* cvCtx = (UTL_TH_CV*)cv;
    
#ifdef WIN32
    WakeAllConditionVariable(&cvCtx->cv);
#else
    pthread_cond_broadcast(&cvCtx->cv);
#endif
    
    return 0;
}
