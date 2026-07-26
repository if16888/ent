#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <time.h>
#endif

#include "ient_comm.h"
#include "ient_runtime.h"
#include "ent_msg.h"
#include "ent_utility.h"

ENT_CTX gEntCtx;

typedef struct
{
    UTL_LOCK lock;
    UTL_CV cv;
    volatile int ready;
    volatile MSG_ID_T wait_status;
} CV_WAKE_PROBE;

static int expect_true(int condition, const char* message)
{
    if(!condition)
    {
        fprintf(stderr, "%s\n", message);
        return 1;
    }
    return 0;
}

static void sleep_ms(int ms)
{
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
#endif
}

MSG_ID_T ENT_LogInit(void){return 0;}
MSG_ID_T ENT_LogClose(void){return 0;}
MSG_ID_T ENT_LogInitHandle(ENT_LOG* pLogHandle, const char* moduleName, const char* logPath){(void)pLogHandle;(void)moduleName;(void)logPath;return 0;}
MSG_ID_T ENT_LogSetOption(ENT_LOG logHandle, ENT_LOG_OPTIONS_E option, const void* arg){(void)logHandle;(void)option;(void)arg;return 0;}
MSG_ID_T ENT_LogCloseHandle(ENT_LOG logHandle){(void)logHandle;return 0;}
MSG_ID_T ENT_LogRaw(ENT_LOG logHandle, const char* format, ...){(void)logHandle;(void)format;return 0;}
MSG_ID_T ENT_LogFatal(ENT_LOG logHandle, const char* format, ...){(void)logHandle;(void)format;return 0;}
MSG_ID_T ENT_LogError(ENT_LOG logHandle, const char* format, ...){(void)logHandle;(void)format;return 0;}
MSG_ID_T ENT_LogWarn(ENT_LOG logHandle, const char* format, ...){(void)logHandle;(void)format;return 0;}
MSG_ID_T ENT_LogPrint(ENT_LOG logHandle, const char* format, ...){(void)logHandle;(void)format;return 0;}
MSG_ID_T ENT_LogDebug(ENT_LOG logHandle, const char* format, ...){(void)logHandle;(void)format;return 0;}

#ifdef _WIN32
static DWORD WINAPI cv_wait_thread(LPVOID data)
#else
static void* cv_wait_thread(void* data)
#endif
{
    CV_WAKE_PROBE* probe = (CV_WAKE_PROBE*)data;
    probe->wait_status = UTL_LockEnter(probe->lock);
    if(probe->wait_status != ENT_SYS_NORMAL)
    {
#ifdef _WIN32
        return 0;
#else
        return NULL;
#endif
    }
    while(!probe->ready)
    {
        probe->wait_status = UTL_CVWait(probe->cv, probe->lock, 0, RW_WRITE_E);
        if(probe->wait_status != ENT_SYS_NORMAL)
        {
            UTL_LockLeave(probe->lock);
#ifdef _WIN32
            return 0;
#else
            return NULL;
#endif
        }
    }
    probe->wait_status = UTL_LockLeave(probe->lock);
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

static int test_rw_lock_requires_explicit_mode(void)
{
    UTL_LOCK lock = NULL;
    if(expect_true(UTL_LockInitEx(&lock, "rw", LOCK_RW_E) == ENT_SYS_NORMAL,
                   "UTL_LockInitEx should create an rw lock") != 0)
    {
        return 1;
    }
    if(expect_true(UTL_LockEnter(lock) == ENT_UTHD_RWMODE_REQUIRED,
                   "UTL_LockEnter should reject rw locks without explicit mode") != 0)
    {
        UTL_LockClose(&lock);
        return 1;
    }
    if(expect_true(UTL_LockLeave(lock) == ENT_UTHD_RWMODE_REQUIRED,
                   "UTL_LockLeave should reject rw locks without explicit mode") != 0)
    {
        UTL_LockClose(&lock);
        return 1;
    }
    return expect_true(UTL_LockClose(&lock) == ENT_SYS_NORMAL,
                       "UTL_LockClose should release the rw lock");
}

static int test_cv_wait_rejects_spin_lock(void)
{
    UTL_LOCK lock = NULL;
    UTL_CV cv = NULL;
    if(expect_true(UTL_LockInitEx(&lock, "spin", LOCK_SPIN_E) == ENT_SYS_NORMAL,
                   "UTL_LockInitEx should create a spin lock") != 0)
    {
        return 1;
    }
    if(expect_true(UTL_CVInit(&cv, "cv") == ENT_SYS_NORMAL,
                   "UTL_CVInit should create a condition variable") != 0)
    {
        UTL_LockClose(&lock);
        return 1;
    }
    if(expect_true(UTL_CVWait(cv, lock, 1, RW_WRITE_E) == ENT_UTHD_UNSUPPORTED_LOCK,
                   "UTL_CVWait should reject spin locks") != 0)
    {
        UTL_CVClose(&cv);
        UTL_LockClose(&lock);
        return 1;
    }
    UTL_CVClose(&cv);
    return expect_true(UTL_LockClose(&lock) == ENT_SYS_NORMAL,
                       "UTL_LockClose should release the spin lock");
}

static int test_cv_wait_timeout_semantics(void)
{
    UTL_LOCK lock = NULL;
    UTL_CV cv = NULL;
    MSG_ID_T wait_sts = ENT_SYS_NORMAL;
    if(expect_true(UTL_LockInit(&lock, "mutex") == ENT_SYS_NORMAL,
                   "UTL_LockInit should create a mutex for timeout test") != 0)
    {
        return 1;
    }
    if(expect_true(UTL_CVInit(&cv, "cv") == ENT_SYS_NORMAL,
                   "UTL_CVInit should create a cv for timeout test") != 0)
    {
        UTL_LockClose(&lock);
        return 1;
    }
    if(expect_true(UTL_LockEnter(lock) == ENT_SYS_NORMAL,
                   "UTL_LockEnter should acquire mutex before timed wait") != 0)
    {
        UTL_CVClose(&cv);
        UTL_LockClose(&lock);
        return 1;
    }
    wait_sts = UTL_CVWait(cv, lock, 20, RW_WRITE_E);
    if(expect_true(wait_sts == ENT_UTHD_WAIT_TIMEOUT,
                   "UTL_CVWait should return timeout semantics") != 0)
    {
        UTL_LockLeave(lock);
        UTL_CVClose(&cv);
        UTL_LockClose(&lock);
        return 1;
    }
    UTL_LockLeave(lock);
    UTL_CVClose(&cv);
    return expect_true(UTL_LockClose(&lock) == ENT_SYS_NORMAL,
                       "UTL_LockClose should release the mutex after timeout test");
}

static int test_cv_wait_and_wake_roundtrip(void)
{
    UTL_LOCK lock = NULL;
    UTL_CV cv = NULL;
    CV_WAKE_PROBE probe;
    memset(&probe, 0, sizeof(probe));
    probe.wait_status = -999;
    if(expect_true(UTL_LockInit(&lock, "mutex") == ENT_SYS_NORMAL,
                   "UTL_LockInit should create a mutex for wake test") != 0)
    {
        return 1;
    }
    if(expect_true(UTL_CVInit(&cv, "cv") == ENT_SYS_NORMAL,
                   "UTL_CVInit should create a cv for wake test") != 0)
    {
        UTL_LockClose(&lock);
        return 1;
    }
    probe.lock = lock;
    probe.cv = cv;
#ifdef _WIN32
    {
        HANDLE th = CreateThread(NULL, 0, cv_wait_thread, &probe, 0, NULL);
        if(expect_true(th != NULL, "cv wait helper thread should start on Windows") != 0)
        {
            UTL_CVClose(&cv);
            UTL_LockClose(&lock);
            return 1;
        }
        sleep_ms(20);
        UTL_LockEnter(lock);
        probe.ready = 1;
        UTL_CVWake(cv);
        UTL_LockLeave(lock);
        WaitForSingleObject(th, INFINITE);
        CloseHandle(th);
    }
#else
    {
        pthread_t th;
        if(expect_true(pthread_create(&th, NULL, cv_wait_thread, &probe) == 0,
                       "cv wait helper thread should start on POSIX") != 0)
        {
            UTL_CVClose(&cv);
            UTL_LockClose(&lock);
            return 1;
        }
        sleep_ms(20);
        UTL_LockEnter(lock);
        probe.ready = 1;
        UTL_CVWake(cv);
        UTL_LockLeave(lock);
        pthread_join(th, NULL);
    }
#endif
    if(expect_true(probe.wait_status == ENT_SYS_NORMAL,
                   "helper thread should complete cv wait/wake roundtrip") != 0)
    {
        UTL_CVClose(&cv);
        UTL_LockClose(&lock);
        return 1;
    }
    UTL_CVClose(&cv);
    return expect_true(UTL_LockClose(&lock) == ENT_SYS_NORMAL,
                       "UTL_LockClose should release the mutex after wake test");
}

int main(void)
{
    int failures = 0;
    failures += test_rw_lock_requires_explicit_mode();
    failures += test_cv_wait_rejects_spin_lock();
    failures += test_cv_wait_timeout_semantics();
    failures += test_cv_wait_and_wake_roundtrip();
    if(failures != 0)
    {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
