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
#include <time.h>
#endif
#include <stdlib.h>
#include <string.h>
#include "ient_comm.h"
#include "ent_msg.h"
#include "ent_utility.h"

#if !defined(WIN32) && defined(__linux__)
#define ENT_HAS_PTHREAD_SPINLOCK 1
#else
#define ENT_HAS_PTHREAD_SPINLOCK 0
#endif

#ifndef WIN32
static clockid_t iUTL_CVClockId(void)
{
#if defined(__linux__)
    return CLOCK_MONOTONIC;
#else
    return CLOCK_REALTIME;
#endif
}
#endif

typedef struct
{
    UTL_LOCK_TYPE_T  lockType;
    union
    {
#ifdef WIN32
        CRITICAL_SECTION   cs;
        SRWLOCK            rw;
        CRITICAL_SECTION   spin;
#elif ENT_HAS_PTHREAD_SPINLOCK
        pthread_mutex_t    cs;
        pthread_rwlock_t   rw;
        pthread_spinlock_t spin;
#else
        pthread_mutex_t    cs;
        pthread_rwlock_t   rw;
        pthread_mutex_t    spin;
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
    char* cvName;
} UTL_TH_CV;

static MSG_ID_T iUTL_MapLockSts(MSG_ID_T sts)
{
    if(sts >= 0)
    {
        return ENT_SYS_NORMAL;
    }
    if(sts == -1)
    {
        return ENT_UTHD_INVALID_ARGUMENT;
    }
    if(sts == -2)
    {
        return ENT_UTHD_ALLOC_FAILED;
    }
    if(sts == -3)
    {
        return ENT_UTHD_INIT_FAILED;
    }
    if(sts == -4)
    {
        return ENT_UTHD_INVALID_TYPE;
    }
    if(sts == -5)
    {
        return ENT_UTHD_RWMODE_REQUIRED;
    }
    if(sts == -6)
    {
        return ENT_UTHD_LOCK_FAILED;
    }
    return ENT_UTHD_LOCK_FAILED;
}

static MSG_ID_T iUTL_MapCvSts(MSG_ID_T sts)
{
    if(sts >= 0)
    {
        return ENT_SYS_NORMAL;
    }
    if(sts == -1)
    {
        return ENT_UTHD_INVALID_ARGUMENT;
    }
    if(sts == -2 || sts == -3)
    {
        return ENT_UTHD_UNSUPPORTED_LOCK;
    }
    if(sts == -4)
    {
        return ENT_UTHD_CLOCK_FAILED;
    }
    if(sts == -5)
    {
        return ENT_UTHD_WAIT_TIMEOUT;
    }
    return ENT_UTHD_WAIT_FAILED;
}

ENT_PUBLIC MSG_ID_T UTL_LockInit(UTL_LOCK* lock,const char* name)
{
    if(lock == NULL)
    {
        IENT_LOG_ERROR("lock handle is null\n");
        return ENT_UTHD_INVALID_ARGUMENT;
    }
    *lock = NULL;

    UTL_TH_LOCK* tmp = (UTL_TH_LOCK*)malloc(sizeof(UTL_TH_LOCK));
    if(tmp == NULL)
    {
        IENT_LOG_ERROR("lock malloc is null\n");
        return ENT_UTHD_ALLOC_FAILED;
    }

    tmp->lockType = LOCK_MUTEX_E;
#ifdef WIN32
    tmp->lockName = (name == NULL) ? NULL : _strdup(name);
    InitializeCriticalSection(&tmp->lock.cs);
#else
    int s;
    tmp->lockName = (name == NULL) ? NULL : strdup(name);
    s = pthread_mutex_init(&tmp->lock.cs,NULL);
    if(s != 0)
    {
        IENT_LOG_ERROR("pthread_mutex_init failed,error [%d]->[%s]\n",s,strerror(s));
        if(tmp->lockName) free(tmp->lockName);
        free(tmp);
        return ENT_UTHD_INIT_FAILED;
    }
#endif
    *lock = tmp;
    return ENT_SYS_NORMAL;
}

static MSG_ID_T iUTL_LockInitRW(UTL_LOCK* lock,const char* name)
{
    UTL_TH_LOCK* tmp = (UTL_TH_LOCK*)malloc(sizeof(UTL_TH_LOCK));
    if(tmp == NULL)
    {
        IENT_LOG_ERROR("lock malloc is null\n");
        return -2;
    }
    tmp->lockType = LOCK_RW_E;
#ifdef WIN32
    tmp->lockName = (name == NULL) ? NULL : _strdup(name);
    InitializeSRWLock(&tmp->lock.rw);
#else
    int s;
    tmp->lockName = (name == NULL) ? NULL : strdup(name);
    s = pthread_rwlock_init(&tmp->lock.rw,NULL);
    if(s != 0)
    {
        IENT_LOG_ERROR("pthread_rwlock_init failed,error [%d]->[%s]\n",s,strerror(s));
        if(tmp->lockName) free(tmp->lockName);
        free(tmp);
        *lock = NULL;
        return -3;
    }
#endif
    *lock = tmp;
    return 0;
}

static MSG_ID_T iUTL_LockInitSpin(UTL_LOCK* lock,const char* name)
{
    UTL_TH_LOCK* tmp = (UTL_TH_LOCK*)malloc(sizeof(UTL_TH_LOCK));
    if(tmp == NULL)
    {
        IENT_LOG_ERROR("lock malloc is null\n");
        return -2;
    }
    tmp->lockType = LOCK_SPIN_E;
#ifdef WIN32
    tmp->lockName = (name == NULL) ? NULL : _strdup(name);
    if(!InitializeCriticalSectionAndSpinCount(&tmp->lock.spin,4000))
    {
        IENT_LOG_ERROR("init failed,error [%d]\n",GetLastError());
        if(tmp->lockName) free(tmp->lockName);
        free(tmp);
        *lock = NULL;
        return -3;
    }
#else
    int s;
    tmp->lockName = (name == NULL) ? NULL : strdup(name);
#if ENT_HAS_PTHREAD_SPINLOCK
    s = pthread_spin_init(&tmp->lock.spin,PTHREAD_PROCESS_PRIVATE);
#else
    s = pthread_mutex_init(&tmp->lock.spin,NULL);
#endif
    if(s != 0)
    {
        IENT_LOG_ERROR("spin lock init failed,error [%d]->[%s]\n",s,strerror(s));
        if(tmp->lockName) free(tmp->lockName);
        free(tmp);
        *lock = NULL;
        return -3;
    }
#endif
    *lock = tmp;
    return 0;
}

ENT_PUBLIC MSG_ID_T UTL_LockInitEx(UTL_LOCK* lock,const char* name,UTL_LOCK_TYPE_T type)
{
    MSG_ID_T sts = 0;
    if(lock == NULL)
    {
        IENT_LOG_ERROR("lock handle is null\n");
        return ENT_UTHD_INVALID_ARGUMENT;
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
            sts = -4;
            break;
    }

    return iUTL_MapLockSts(sts);
}

static MSG_ID_T iUTL_LockEnterMutex(UTL_LOCK lock)
{
    UTL_TH_LOCK* tmp = (UTL_TH_LOCK*)lock;
#ifdef WIN32
    EnterCriticalSection(&tmp->lock.cs);
    return 0;
#else
    int s = pthread_mutex_lock(&tmp->lock.cs);
    if(s != 0)
    {
        IENT_LOG_ERROR("pthread_mutex_lock failed,error [%d]->[%s]\n",s,strerror(s));
        return -6;
    }
    return 0;
#endif
}

static MSG_ID_T iUTL_LockEnterRW(UTL_LOCK lock,UTL_LOCK_RW_TYPE_T rwType)
{
    UTL_TH_LOCK* tmp = (UTL_TH_LOCK*)lock;
#ifdef WIN32
    switch(rwType)
    {
        case RW_WRITE_E:
            AcquireSRWLockExclusive(&tmp->lock.rw);
            return 0;
        case RW_READ_E:
            AcquireSRWLockShared(&tmp->lock.rw);
            return 0;
        default:
            return -1;
    }
#else
    int s = 0;
    switch(rwType)
    {
        case RW_WRITE_E:
            s = pthread_rwlock_wrlock(&tmp->lock.rw);
            break;
        case RW_READ_E:
            s = pthread_rwlock_rdlock(&tmp->lock.rw);
            break;
        default:
            return -1;
    }
    if(s != 0)
    {
        IENT_LOG_ERROR("pthread_rwlock_*lock failed,error [%d]->[%s]\n",s,strerror(s));
        return -6;
    }
    return 0;
#endif
}

static MSG_ID_T iUTL_LockEnterSpin(UTL_LOCK lock)
{
    UTL_TH_LOCK* tmp = (UTL_TH_LOCK*)lock;
#ifdef WIN32
    EnterCriticalSection(&tmp->lock.spin);
    return 0;
#else
    int s = 0;
#if ENT_HAS_PTHREAD_SPINLOCK
    s = pthread_spin_lock(&tmp->lock.spin);
#else
    s = pthread_mutex_lock(&tmp->lock.spin);
#endif
    if(s != 0)
    {
        IENT_LOG_ERROR("spin lock enter failed,error [%d]->[%s]\n",s,strerror(s));
        return -6;
    }
    return 0;
#endif
}

ENT_PUBLIC MSG_ID_T UTL_LockEnter(UTL_LOCK lock)
{
    MSG_ID_T sts = 0;
    if(lock == NULL)
    {
        IENT_LOG_ERROR("lock is null\n");
        return ENT_UTHD_INVALID_ARGUMENT;
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
            IENT_LOG_ERROR("rw lock requires explicit enter mode\n");
            sts = -5;
            break;
        default:
            IENT_LOG_ERROR("unknow lock type [%d]\n",tmp->lockType);
            sts = -4;
            break;
    }

    return iUTL_MapLockSts(sts);
}

ENT_PUBLIC MSG_ID_T UTL_LockEnterEx(UTL_LOCK lock,UTL_LOCK_RW_TYPE_T rwType)
{
    MSG_ID_T sts = 0;
    if(lock == NULL)
    {
        IENT_LOG_ERROR("lock is null\n");
        return ENT_UTHD_INVALID_ARGUMENT;
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
            sts = -4;
            break;
    }
    return iUTL_MapLockSts(sts);
}

static MSG_ID_T iUTL_LockLeaveMutex(UTL_LOCK lock)
{
    UTL_TH_LOCK* tmp = (UTL_TH_LOCK*)lock;
#ifdef WIN32
    LeaveCriticalSection(&tmp->lock.cs);
    return 0;
#else
    int s = pthread_mutex_unlock(&tmp->lock.cs);
    if(s != 0)
    {
        IENT_LOG_ERROR("pthread_mutex_unlock failed,error [%d]->[%s]\n",s,strerror(s));
        return -6;
    }
    return 0;
#endif
}

static MSG_ID_T iUTL_LockLeaveRW(UTL_LOCK lock,UTL_LOCK_RW_TYPE_T rwType)
{
    UTL_TH_LOCK* tmp = (UTL_TH_LOCK*)lock;
#ifdef WIN32
    switch(rwType)
    {
        case RW_WRITE_E:
            ReleaseSRWLockExclusive(&tmp->lock.rw);
            return 0;
        case RW_READ_E:
            ReleaseSRWLockShared(&tmp->lock.rw);
            return 0;
        default:
            IENT_LOG_ERROR("invalid rw mode [%d]\n",rwType);
            return -1;
    }
#else
    int s;
    switch(rwType)
    {
        case RW_WRITE_E:
        case RW_READ_E:
            s = pthread_rwlock_unlock(&tmp->lock.rw);
            break;
        default:
            IENT_LOG_ERROR("invalid rw mode [%d]\n",rwType);
            return -1;
    }
    if(s != 0)
    {
        IENT_LOG_ERROR("pthread_rwlock_unlock failed,error [%d]->[%s]\n",s,strerror(s));
        return -6;
    }
    return 0;
#endif
}

static MSG_ID_T iUTL_LockLeaveSpin(UTL_LOCK lock)
{
    UTL_TH_LOCK* tmp = (UTL_TH_LOCK*)lock;
#ifdef WIN32
    LeaveCriticalSection(&tmp->lock.spin);
    return 0;
#else
    int s = 0;
#if ENT_HAS_PTHREAD_SPINLOCK
    s = pthread_spin_unlock(&tmp->lock.spin);
#else
    s = pthread_mutex_unlock(&tmp->lock.spin);
#endif
    if(s != 0)
    {
        IENT_LOG_ERROR("spin lock leave failed,error [%d]->[%s]\n",s,strerror(s));
        return -6;
    }
    return 0;
#endif
}

ENT_PUBLIC MSG_ID_T UTL_LockLeave(UTL_LOCK lock)
{
    MSG_ID_T sts = 0;
    if(lock == NULL)
    {
        IENT_LOG_ERROR("lock is null\n");
        return ENT_UTHD_INVALID_ARGUMENT;
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
            IENT_LOG_ERROR("rw lock requires explicit leave mode\n");
            sts = -5;
            break;
        default:
            IENT_LOG_ERROR("unknow lock type [%d]\n",tmp->lockType);
            sts = -4;
            break;
    }
    return iUTL_MapLockSts(sts);
}

ENT_PUBLIC MSG_ID_T UTL_LockLeaveEx(UTL_LOCK lock,UTL_LOCK_RW_TYPE_T rwType)
{
    MSG_ID_T sts = 0;
    if(lock == NULL)
    {
        IENT_LOG_ERROR("lock is null\n");
        return ENT_UTHD_INVALID_ARGUMENT;
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
            sts = -4;
            break;
    }
    return iUTL_MapLockSts(sts);
}

static MSG_ID_T iUTL_LockCloseMutex(UTL_LOCK lock)
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

static MSG_ID_T iUTL_LockCloseRW(UTL_LOCK lock)
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

static MSG_ID_T iUTL_LockCloseSpin(UTL_LOCK lock)
{
    UTL_TH_LOCK* tmp = (UTL_TH_LOCK*)lock;
#ifdef WIN32
    DeleteCriticalSection(&tmp->lock.spin);
#else
#if ENT_HAS_PTHREAD_SPINLOCK
    pthread_spin_destroy(&tmp->lock.spin);
#else
    pthread_mutex_destroy(&tmp->lock.spin);
#endif
#endif
    if(tmp->lockName)
    {
        free(tmp->lockName);
    }
    free(tmp);
    return 0;
}

ENT_PUBLIC MSG_ID_T UTL_LockClose(UTL_LOCK lock)
{
    MSG_ID_T sts = 0;
    if(lock == NULL)
    {
        IENT_LOG_ERROR("lock handle is null\n");
        return ENT_UTHD_INVALID_ARGUMENT;
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
            sts = -4;
            break;
    }

    return iUTL_MapLockSts(sts);
}

ENT_PUBLIC MSG_ID_T UTL_LockCloseSafe(UTL_LOCK* lock)
{
    MSG_ID_T sts;
    if(lock == NULL || *lock == NULL)
    {
        return ENT_UTHD_INVALID_ARGUMENT;
    }
    sts = UTL_LockClose(*lock);
    if(sts == ENT_SYS_NORMAL)
    {
        *lock = NULL;
    }
    return sts;
}

ENT_PUBLIC MSG_ID_T UTL_CVInit(UTL_CV* cv,const char* name)
{
    if(cv == NULL)
    {
        IENT_LOG_ERROR("cv handle is null\n");
        return ENT_UTHD_INVALID_ARGUMENT;
    }

    UTL_TH_CV* tmp = (UTL_TH_CV*)malloc(sizeof(UTL_TH_CV));
    if(tmp == NULL)
    {
        IENT_LOG_ERROR("cv malloc is null\n");
        return ENT_UTHD_ALLOC_FAILED;
    }

#ifdef WIN32
    tmp->cvName = (name == NULL) ? NULL : _strdup(name);
    InitializeConditionVariable(&tmp->cv);
#else
    pthread_condattr_t cvAttr;
    int s;
    tmp->cvName = (name == NULL) ? NULL : strdup(name);
    s = pthread_condattr_init(&cvAttr);
    if(s != 0)
    {
        IENT_LOG_ERROR("pthread_condattr_init failed,error [%d]->[%s]\n",s,strerror(s));
        if(tmp->cvName) free(tmp->cvName);
        free(tmp);
        return ENT_UTHD_INIT_FAILED;
    }
#if defined(__linux__)
    s = pthread_condattr_setclock(&cvAttr,iUTL_CVClockId());
    if(s != 0)
    {
        IENT_LOG_ERROR("pthread_condattr_setclock failed,error [%d]->[%s]\n",s,strerror(s));
        pthread_condattr_destroy(&cvAttr);
        if(tmp->cvName) free(tmp->cvName);
        free(tmp);
        return ENT_UTHD_INIT_FAILED;
    }
#endif
    s = pthread_cond_init(&tmp->cv,&cvAttr);
    pthread_condattr_destroy(&cvAttr);
    if(s != 0)
    {
        IENT_LOG_ERROR("pthread_cond_init failed,error [%d]->[%s]\n",s,strerror(s));
        if(tmp->cvName) free(tmp->cvName);
        free(tmp);
        return ENT_UTHD_INIT_FAILED;
    }
#endif
    *cv = tmp;
    return ENT_SYS_NORMAL;
}

ENT_PUBLIC MSG_ID_T UTL_CVClose(UTL_CV cv)
{
    if(cv == NULL)
    {
        IENT_LOG_ERROR("cv handle is null\n");
        return ENT_UTHD_INVALID_ARGUMENT;
    }

    UTL_TH_CV* tmp = (UTL_TH_CV*)cv;
#ifdef WIN32
#else
    pthread_cond_destroy(&tmp->cv);
#endif
    if(tmp->cvName)
    {
        free(tmp->cvName);
    }
    free(tmp);
    return ENT_SYS_NORMAL;
}

ENT_PUBLIC MSG_ID_T UTL_CVCloseSafe(UTL_CV* cv)
{
    MSG_ID_T sts;
    if(cv == NULL || *cv == NULL)
    {
        return ENT_UTHD_INVALID_ARGUMENT;
    }
    sts = UTL_CVClose(*cv);
    if(sts == ENT_SYS_NORMAL)
    {
        *cv = NULL;
    }
    return sts;
}

static MSG_ID_T iUTL_CVWaitMutex(UTL_TH_CV* cvCtx,UTL_TH_LOCK* lockCtx,int ms)
{
#ifdef WIN32
    BOOL ok = SleepConditionVariableCS(&cvCtx->cv,&lockCtx->lock.cs,ms > 0 ? (DWORD)ms : INFINITE);
    if(ok)
    {
        return 0;
    }
    DWORD err = GetLastError();
    if(err == ERROR_TIMEOUT)
    {
        return -5;
    }
    IENT_LOG_ERROR("SleepConditionVariableCS failed,error [%lu]\n",(unsigned long)err);
    return -6;
#else
    if(ms <= 0)
    {
        int s = pthread_cond_wait(&cvCtx->cv,&lockCtx->lock.cs);
        if(s != 0)
        {
            IENT_LOG_ERROR("pthread_cond_wait failed,error [%d]->[%s]\n",s,strerror(s));
            return -6;
        }
        return 0;
    }
    else
    {
        struct timespec cvTm;
        int s;
        if(clock_gettime(iUTL_CVClockId(), &cvTm) == -1)
        {
            IENT_LOG_ERROR("clock_gettime failed,error [%d]->[%s]\n",errno,strerror(errno));
            return -4;
        }
        cvTm.tv_sec += ms / 1000;
        cvTm.tv_nsec += (ms % 1000) * 1000000;
        if(cvTm.tv_nsec >= 1000000000L)
        {
            cvTm.tv_sec += cvTm.tv_nsec / 1000000000L;
            cvTm.tv_nsec = cvTm.tv_nsec % 1000000000L;
        }
        s = pthread_cond_timedwait(&cvCtx->cv,&lockCtx->lock.cs,&cvTm);
        if(s == 0)
        {
            return 0;
        }
        if(s == ETIMEDOUT)
        {
            return -5;
        }
        IENT_LOG_ERROR("pthread_cond_timedwait failed,error [%d]->[%s]\n",s,strerror(s));
        return -6;
    }
#endif
}

static MSG_ID_T iUTL_CVWaitRW(UTL_TH_CV* cvCtx,UTL_TH_LOCK* lockCtx,int ms,UTL_LOCK_RW_TYPE_T rwType)
{
#ifdef WIN32
    ULONG flag = 0;
    BOOL ok;

    if(rwType == RW_READ_E)
    {
        flag = CONDITION_VARIABLE_LOCKMODE_SHARED;
    }
    else if(rwType != RW_WRITE_E)
    {
        return -1;
    }

    ok = SleepConditionVariableSRW(&cvCtx->cv,&lockCtx->lock.rw,ms > 0 ? (DWORD)ms : INFINITE,flag);
    if(ok)
    {
        return 0;
    }
    if(GetLastError() == ERROR_TIMEOUT)
    {
        return -5;
    }
    return -6;
#else
    (void)cvCtx;
    (void)lockCtx;
    (void)ms;
    (void)rwType;
    IENT_LOG_ERROR("unsupported rw lock type\n");
    return -2;
#endif
}

ENT_PUBLIC MSG_ID_T UTL_CVWait(UTL_CV cv,UTL_LOCK lock,int ms,UTL_LOCK_RW_TYPE_T rwType)
{
    MSG_ID_T sts = 0;
    if(cv == NULL || lock == NULL)
    {
        IENT_LOG_ERROR("cv handle or lock handle is null\n");
        return ENT_UTHD_INVALID_ARGUMENT;
    }

    UTL_TH_CV* cvCtx = (UTL_TH_CV*)cv;
    UTL_TH_LOCK* lockCtx = (UTL_TH_LOCK*)lock;

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
            return ENT_UTHD_UNSUPPORTED_LOCK;
    }
    return iUTL_MapCvSts(sts);
}

ENT_PUBLIC MSG_ID_T UTL_CVWake(UTL_CV cv)
{
    if(cv == NULL)
    {
        IENT_LOG_ERROR("cv handle is null\n");
        return ENT_UTHD_INVALID_ARGUMENT;
    }

    UTL_TH_CV* cvCtx = (UTL_TH_CV*)cv;
#ifdef WIN32
    WakeConditionVariable(&cvCtx->cv);
#else
    pthread_cond_signal(&cvCtx->cv);
#endif
    return ENT_SYS_NORMAL;
}

ENT_PUBLIC MSG_ID_T UTL_CVWakeAll(UTL_CV cv)
{
    if(cv == NULL)
    {
        IENT_LOG_ERROR("cv handle is null\n");
        return ENT_UTHD_INVALID_ARGUMENT;
    }

    UTL_TH_CV* cvCtx = (UTL_TH_CV*)cv;
#ifdef WIN32
    WakeAllConditionVariable(&cvCtx->cv);
#else
    pthread_cond_broadcast(&cvCtx->cv);
#endif
    return ENT_SYS_NORMAL;
}
