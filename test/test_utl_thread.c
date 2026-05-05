#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#ifdef WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <errno.h>
#endif

#include "ient_comm.h"
#include "ient_runtime.h"
#include "ent_msg.h"

ENT_CTX gEntCtx;

#ifndef WIN32
static int s_clock_gettime_call_count = 0;
static struct timespec s_last_timedwait_deadline;
static int s_timedwait_call_count = 0;
static int s_fail_pthread_mutex_init = 0;
static int s_fail_pthread_rwlock_init = 0;
#endif

static int expect_true(int condition, const char* message)
{
    if(!condition)
    {
        fprintf(stderr, "%s\n", message);
        return 1;
    }

    return 0;
}

static void reset_cv_wait_probes(void)
{
#ifndef WIN32
    s_clock_gettime_call_count = 0;
    memset(&s_last_timedwait_deadline, 0, sizeof(s_last_timedwait_deadline));
    s_timedwait_call_count = 0;
#endif
}

MSG_ID_T ENT_LogInit(void)
{
    return 0;
}

MSG_ID_T ENT_LogClose(void)
{
    return 0;
}

MSG_ID_T ENT_LogInitHandle(ENT_LOG* pLogHandle, const char* moduleName, const char* logPath)
{
    (void)pLogHandle;
    (void)moduleName;
    (void)logPath;
    return 0;
}

MSG_ID_T ENT_LogSetOption(ENT_LOG logHandle, ENT_LOG_OPTIONS_E option, const void* arg)
{
    (void)logHandle;
    (void)option;
    (void)arg;
    return 0;
}

MSG_ID_T ENT_LogCloseHandle(ENT_LOG logHandle)
{
    (void)logHandle;
    return 0;
}

MSG_ID_T ENT_LogRaw(ENT_LOG logHandle, const char* format, ...)
{
    (void)logHandle;
    (void)format;
    return 0;
}

MSG_ID_T ENT_LogFatal(ENT_LOG logHandle, const char* format, ...)
{
    (void)logHandle;
    (void)format;
    return 0;
}

MSG_ID_T ENT_LogError(ENT_LOG logHandle, const char* format, ...)
{
    (void)logHandle;
    (void)format;
    return 0;
}

MSG_ID_T ENT_LogWarn(ENT_LOG logHandle, const char* format, ...)
{
    (void)logHandle;
    (void)format;
    return 0;
}

MSG_ID_T ENT_LogPrint(ENT_LOG logHandle, const char* format, ...)
{
    (void)logHandle;
    (void)format;
    return 0;
}

MSG_ID_T ENT_LogDebug(ENT_LOG logHandle, const char* format, ...)
{
    (void)logHandle;
    (void)format;
    return 0;
}

#ifndef WIN32
int clock_gettime(clockid_t clk_id, struct timespec* tp)
{
    (void)clk_id;
    s_clock_gettime_call_count++;
    if(tp != NULL)
    {
        tp->tv_sec = 100;
        tp->tv_nsec = 999500000;
    }
    return 0;
}

int pthread_mutex_init(pthread_mutex_t* mutex, const pthread_mutexattr_t* attr)
{
    static int (*real_pthread_mutex_init)(pthread_mutex_t*, const pthread_mutexattr_t*) = NULL;

    if(s_fail_pthread_mutex_init > 0)
    {
        s_fail_pthread_mutex_init--;
        return EBUSY;
    }

    if(real_pthread_mutex_init == NULL)
    {
        real_pthread_mutex_init = dlsym(RTLD_NEXT, "pthread_mutex_init");
    }

    return real_pthread_mutex_init(mutex, attr);
}

int pthread_rwlock_init(pthread_rwlock_t* lock, const pthread_rwlockattr_t* attr)
{
    static int (*real_pthread_rwlock_init)(pthread_rwlock_t*, const pthread_rwlockattr_t*) = NULL;

    if(s_fail_pthread_rwlock_init > 0)
    {
        s_fail_pthread_rwlock_init--;
        return EBUSY;
    }

    if(real_pthread_rwlock_init == NULL)
    {
        real_pthread_rwlock_init = dlsym(RTLD_NEXT, "pthread_rwlock_init");
    }

    return real_pthread_rwlock_init(lock, attr);
}

int pthread_cond_timedwait(pthread_cond_t* restrict cond,
                           pthread_mutex_t* restrict mutex,
                           const struct timespec* restrict abstime)
{
    (void)cond;
    (void)mutex;
    s_timedwait_call_count++;
    if(abstime != NULL)
    {
        s_last_timedwait_deadline = *abstime;
    }
    return 0;
}
#endif

static int test_lock_init_rejects_null_pointer(void)
{
    return expect_true(UTL_LockInit(NULL, "lock") == ENT_UTHD_INVALID_ARGUMENT,
                       "UTL_LockInit should reject a NULL output pointer");
}

static int test_lock_init_ex_rejects_unknown_type(void)
{
    UTL_LOCK lock = (UTL_LOCK)0x1;

    if(expect_true(UTL_LockInitEx(&lock, "lock", (UTL_LOCK_TYPE_T)99) == ENT_UTHD_INVALID_TYPE,
                   "UTL_LockInitEx should reject an unknown lock type") != 0)
    {
        return 1;
    }

    return expect_true(lock == NULL, "UTL_LockInitEx should clear the output pointer for an unknown type");
}

static int test_lock_init_propagates_mutex_init_failure(void)
{
#ifdef WIN32
    return 0;
#else
    UTL_LOCK lock = (UTL_LOCK)0x1;

    s_fail_pthread_mutex_init = 1;
    if(expect_true(UTL_LockInit(&lock, "mutex") == ENT_UTHD_INIT_FAILED,
                   "UTL_LockInit should propagate pthread_mutex_init failures") != 0)
    {
        return 1;
    }

    return expect_true(lock == NULL, "UTL_LockInit should clear the output pointer when mutex initialization fails");
#endif
}

static int test_rw_lock_init_propagates_rwlock_init_failure(void)
{
#ifdef WIN32
    return 0;
#else
    UTL_LOCK lock = (UTL_LOCK)0x1;

    s_fail_pthread_rwlock_init = 1;
    if(expect_true(UTL_LockInitEx(&lock, "rw", LOCK_RW_E) == ENT_UTHD_INIT_FAILED,
                   "UTL_LockInitEx should propagate pthread_rwlock_init failures") != 0)
    {
        return 1;
    }

    return expect_true(lock == NULL, "UTL_LockInitEx should clear the output pointer when rwlock initialization fails");
#endif
}

static int test_mutex_lock_roundtrip_succeeds(void)
{
    UTL_LOCK lock = NULL;

    if(expect_true(UTL_LockInit(&lock, "mutex") == 0, "UTL_LockInit should create a mutex lock") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_LockEnter(lock) == 0, "UTL_LockEnter should lock a mutex") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_LockLeave(lock) == 0, "UTL_LockLeave should unlock a mutex") != 0)
    {
        return 1;
    }

    return expect_true(UTL_LockClose(&lock) == 0, "UTL_LockClose should release a mutex lock");
}

static int test_rw_lock_read_roundtrip_succeeds(void)
{
    UTL_LOCK lock = NULL;

    if(expect_true(UTL_LockInitEx(&lock, "rw", LOCK_RW_E) == 0, "UTL_LockInitEx should create an rw lock") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_LockEnterEx(lock, RW_READ_E) == 0, "UTL_LockEnterEx should acquire a read lock") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_LockLeaveEx(lock, RW_READ_E) == 0, "UTL_LockLeaveEx should release a read lock") != 0)
    {
        return 1;
    }

    return expect_true(UTL_LockClose(&lock) == 0, "UTL_LockClose should release an rw lock");
}

static int test_rw_lock_requires_explicit_enter_and_leave_mode(void)
{
    UTL_LOCK lock = NULL;

    if(expect_true(UTL_LockInitEx(&lock, "rw", LOCK_RW_E) == 0, "UTL_LockInitEx should create an rw lock for explicit mode checks") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_LockEnter(lock) == ENT_UTHD_RWMODE_REQUIRED,
                   "UTL_LockEnter should reject rw locks unless the caller specifies read or write mode explicitly") != 0)
    {
        UTL_LockClose(&lock);
        return 1;
    }

    if(expect_true(UTL_LockLeave(lock) == ENT_UTHD_RWMODE_REQUIRED,
                   "UTL_LockLeave should reject rw locks unless the caller specifies read or write mode explicitly") != 0)
    {
        UTL_LockClose(&lock);
        return 1;
    }

    return expect_true(UTL_LockClose(&lock) == 0, "UTL_LockClose should release the rw lock after explicit mode checks");
}

static int test_lock_close_closes_and_nulls_handle(void)
{
    UTL_LOCK lock = NULL;

    if(expect_true(UTL_LockInit(&lock, "safe") == ENT_SYS_NORMAL,
                   "UTL_LockInit should create a lock for close") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_LockClose(&lock) == ENT_SYS_NORMAL,
                   "UTL_LockClose should close the lock") != 0)
    {
        return 1;
    }

    if(expect_true(lock == NULL,
                   "UTL_LockClose should clear the caller-owned lock handle") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_LockClose(&lock) == ENT_UTHD_INVALID_ARGUMENT,
                   "UTL_LockClose should reject an already-cleared handle") != 0)
    {
        return 1;
    }

    return expect_true(UTL_LockClose(NULL) == ENT_UTHD_INVALID_ARGUMENT,
                       "UTL_LockClose should reject a NULL handle pointer");
}

static int test_cv_wait_rejects_spin_lock(void)
{
    UTL_LOCK lock = NULL;
    UTL_CV cv = NULL;

    if(expect_true(UTL_LockInitEx(&lock, "spin", LOCK_SPIN_E) == 0, "UTL_LockInitEx should create a spin lock") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_CVInit(&cv, "cv") == 0, "UTL_CVInit should create a condition variable") != 0)
    {
        UTL_LockClose(&lock);
        return 1;
    }

    if(expect_true(UTL_CVWait(cv, lock, 1, RW_WRITE_E) == ENT_UTHD_UNSUPPORTED_LOCK,
                   "UTL_CVWait should reject unsupported spin locks") != 0)
    {
        UTL_CVClose(&cv);
        UTL_LockClose(&lock);
        return 1;
    }

    if(expect_true(UTL_CVClose(&cv) == 0, "UTL_CVClose should release the condition variable") != 0)
    {
        UTL_LockClose(&lock);
        return 1;
    }

    return expect_true(UTL_LockClose(&lock) == 0, "UTL_LockClose should release the spin lock");
}

static int test_cv_close_closes_and_nulls_handle(void)
{
    UTL_CV cv = NULL;

    if(expect_true(UTL_CVInit(&cv, "safe-cv") == ENT_SYS_NORMAL,
                   "UTL_CVInit should create a condition variable for close") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_CVClose(&cv) == ENT_SYS_NORMAL,
                   "UTL_CVClose should close the condition variable") != 0)
    {
        return 1;
    }

    if(expect_true(cv == NULL,
                   "UTL_CVClose should clear the caller-owned condition-variable handle") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_CVClose(&cv) == ENT_UTHD_INVALID_ARGUMENT,
                   "UTL_CVClose should reject an already-cleared handle") != 0)
    {
        return 1;
    }

    return expect_true(UTL_CVClose(NULL) == ENT_UTHD_INVALID_ARGUMENT,
                       "UTL_CVClose should reject a NULL handle pointer");
}

static int test_cv_wake_and_wake_all_reject_null(void)
{
    if(expect_true(UTL_CVWake(NULL) == ENT_UTHD_INVALID_ARGUMENT, "UTL_CVWake should reject NULL") != 0)
    {
        return 1;
    }

    return expect_true(UTL_CVWakeAll(NULL) == ENT_UTHD_INVALID_ARGUMENT, "UTL_CVWakeAll should reject NULL");
}

static int test_cv_wait_uses_monotonic_deadline_and_normalized_timespec(void)
{
#ifdef WIN32
    UTL_LOCK lock = NULL;
    UTL_CV cv = NULL;

    if(expect_true(UTL_LockInit(&lock, "mutex") == 0, "UTL_LockInit should create a mutex for cv wait timing checks") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_CVInit(&cv, "cv") == 0, "UTL_CVInit should create a condition variable for timing checks") != 0)
    {
        UTL_LockClose(&lock);
        return 1;
    }

    if(expect_true(UTL_LockEnter(lock) == 0, "UTL_LockEnter should lock the mutex before waiting on Windows") != 0)
    {
        UTL_CVClose(&cv);
        UTL_LockClose(&lock);
        return 1;
    }

    {
        MSG_ID_T waitSts = UTL_CVWait(cv, lock, 1, RW_WRITE_E);
        if(expect_true(waitSts == 0 || waitSts == ENT_UTHD_WAIT_TIMEOUT,
                       "UTL_CVWait should accept a timed mutex wait or timeout on Windows") != 0)
        {
            UTL_LockLeave(lock);
            UTL_CVClose(&cv);
            UTL_LockClose(&lock);
            return 1;
        }
    }

    if(expect_true(UTL_LockLeave(lock) == 0, "UTL_LockLeave should release the mutex after waiting on Windows") != 0)
    {
        UTL_CVClose(&cv);
        UTL_LockClose(&lock);
        return 1;
    }

    if(expect_true(UTL_CVClose(&cv) == 0, "UTL_CVClose should release the condition variable after timing checks") != 0)
    {
        UTL_LockClose(&lock);
        return 1;
    }

    return expect_true(UTL_LockClose(&lock) == 0, "UTL_LockClose should release the mutex after timing checks");
#else
    UTL_LOCK lock = NULL;
    UTL_CV cv = NULL;

    if(expect_true(UTL_LockInit(&lock, "mutex") == 0, "UTL_LockInit should create a mutex for cv wait timing checks") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_CVInit(&cv, "cv") == 0, "UTL_CVInit should create a condition variable for timing checks") != 0)
    {
        UTL_LockClose(&lock);
        return 1;
    }

    reset_cv_wait_probes();

    if(expect_true(UTL_CVWait(cv, lock, 600, RW_WRITE_E) == 0,
                   "UTL_CVWait should accept a timed mutex wait") != 0)
    {
        UTL_CVClose(&cv);
        UTL_LockClose(&lock);
        return 1;
    }

    if(expect_true(s_clock_gettime_call_count == 1,
                   "UTL_CVWait should capture the current time exactly once for a timed wait") != 0)
    {
        UTL_CVClose(&cv);
        UTL_LockClose(&lock);
        return 1;
    }

    if(expect_true(s_timedwait_call_count == 1,
                   "UTL_CVWait should call pthread_cond_timedwait for timed mutex waits") != 0)
    {
        UTL_CVClose(&cv);
        UTL_LockClose(&lock);
        return 1;
    }

    if(expect_true(s_last_timedwait_deadline.tv_sec == 101 && s_last_timedwait_deadline.tv_nsec == 599500000,
                   "UTL_CVWait should normalize timed wait deadlines when nanoseconds overflow") != 0)
    {
        UTL_CVClose(&cv);
        UTL_LockClose(&lock);
        return 1;
    }

    if(expect_true(UTL_CVClose(&cv) == 0, "UTL_CVClose should release the condition variable after timing checks") != 0)
    {
        UTL_LockClose(&lock);
        return 1;
    }

    return expect_true(UTL_LockClose(&lock) == 0, "UTL_LockClose should release the mutex after timing checks");
#endif
}

int main(void)
{
    int failures = 0;

    failures += test_lock_init_rejects_null_pointer();
    failures += test_lock_init_ex_rejects_unknown_type();
    failures += test_lock_init_propagates_mutex_init_failure();
    failures += test_rw_lock_init_propagates_rwlock_init_failure();
    failures += test_mutex_lock_roundtrip_succeeds();
    failures += test_rw_lock_read_roundtrip_succeeds();
    failures += test_rw_lock_requires_explicit_enter_and_leave_mode();
    failures += test_lock_close_closes_and_nulls_handle();
    failures += test_cv_wait_rejects_spin_lock();
    failures += test_cv_close_closes_and_nulls_handle();
    failures += test_cv_wake_and_wake_all_reject_null();
    failures += test_cv_wait_uses_monotonic_deadline_and_normalized_timespec();

    if(failures != 0)
    {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
