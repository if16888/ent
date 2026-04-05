#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ient_comm.h"
#include "ent_thread.h"

ENT_CTX gEntCtx;

typedef struct TEST_BAD_THREAD_CTX
{
    unsigned int tag;
} TEST_BAD_THREAD_CTX;

static int s_cancel_call_count = 0;

static int expect_true(int condition, const char* message)
{
    if(!condition)
    {
        fprintf(stderr, "%s\n", message);
        return 1;
    }

    return 0;
}

static void reset_thread_probes(void)
{
    s_cancel_call_count = 0;
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

int pthread_join(pthread_t thread, void** retval)
{
    (void)thread;
    if(retval != NULL)
    {
        *retval = NULL;
    }
    return 0;
}

int pthread_cancel(pthread_t thread)
{
    (void)thread;
    s_cancel_call_count++;
    return 0;
}

static void* quick_thread(void* data)
{
    return data;
}

static void* sleepy_thread(void* data)
{
    struct timespec ts;

    ts.tv_sec = 0;
    ts.tv_nsec = 50 * 1000000L;
    nanosleep(&ts, NULL);
    return data;
}

static void* short_lived_thread(void* data)
{
    struct timespec ts;

    ts.tv_sec = 0;
    ts.tv_nsec = 5 * 1000000L;
    nanosleep(&ts, NULL);
    return data;
}

static int test_thread_init_rejects_null_pointer(void)
{
    return expect_true(ENT_ThreadInit(NULL) == -1, "ENT_ThreadInit should reject a NULL output pointer");
}

static int test_thread_detach_create_rejects_invalid_handle(void)
{
    TEST_BAD_THREAD_CTX badHandle;
    MSG_ID_T sts = 0;

    memset(&badHandle, 0, sizeof(badHandle));
    badHandle.tag = 0x12345678;

    sts = ENT_ThreadDetachCreate((ENT_THREAD)&badHandle, quick_thread, NULL);
    if(expect_true(sts == -2, "ENT_ThreadDetachCreate should reject an invalid thread handle") != 0)
    {
        return 1;
    }

    return 0;
}

static int test_thread_create_wait_and_close_roundtrip(void)
{
    ENT_THREAD handle = NULL;
    ENT_THREAD_ID tid = NULL;
    int value = 7;

    if(expect_true(ENT_ThreadInit(&handle) == 0, "ENT_ThreadInit should create a thread context") != 0)
    {
        return 1;
    }

    if(expect_true(ENT_ThreadCreate(&tid, handle, quick_thread, &value) == 0,
                   "ENT_ThreadCreate should create a joinable thread") != 0)
    {
        ENT_ThreadClose(handle);
        return 1;
    }

    if(expect_true(tid != NULL, "ENT_ThreadCreate should populate tid when requested") != 0)
    {
        ENT_ThreadClose(handle);
        return 1;
    }

    reset_thread_probes();

    if(expect_true(ENT_ThreadWaitById(&tid, handle, 0) == 0,
                   "ENT_ThreadWaitById should join a finished thread") != 0)
    {
        ENT_ThreadClose(handle);
        return 1;
    }

    if(expect_true(tid == NULL, "ENT_ThreadWaitById should clear tid after joining") != 0)
    {
        ENT_ThreadClose(handle);
        return 1;
    }

    return expect_true(ENT_ThreadClose(handle) == 0, "ENT_ThreadClose should release the thread context");
}

static int test_thread_wait_timeout_returns_retry_signal(void)
{
    ENT_THREAD handle = NULL;
    ENT_THREAD_ID tid = NULL;
    int value = 9;
    MSG_ID_T sts = 0;

    if(expect_true(ENT_ThreadInit(&handle) == 0, "ENT_ThreadInit should create a thread context") != 0)
    {
        return 1;
    }

    if(expect_true(ENT_ThreadCreate(&tid, handle, sleepy_thread, &value) == 0,
                   "ENT_ThreadCreate should create a thread for timeout checks") != 0)
    {
        ENT_ThreadClose(handle);
        return 1;
    }

    sts = ENT_ThreadWaitById(&tid, handle, 1);
    if(expect_true(sts == 1, "ENT_ThreadWaitById should return 1 when the thread is still running") != 0)
    {
        ENT_ThreadClose(handle);
        return 1;
    }

    if(expect_true(tid != NULL, "ENT_ThreadWaitById should preserve tid after a timeout") != 0)
    {
        ENT_ThreadClose(handle);
        return 1;
    }

    if(expect_true(ENT_ThreadWaitById(&tid, handle, 0) == 0,
                   "ENT_ThreadWaitById should succeed on a later blocking wait") != 0)
    {
        ENT_ThreadClose(handle);
        return 1;
    }

    return expect_true(ENT_ThreadClose(handle) == 0, "ENT_ThreadClose should release the thread context after timeout handling");
}

static int test_thread_wait_returns_success_when_thread_finishes_before_timeout(void)
{
    ENT_THREAD handle = NULL;
    ENT_THREAD_ID tid = NULL;
    int value = 13;

    if(expect_true(ENT_ThreadInit(&handle) == 0, "ENT_ThreadInit should create a thread context for early completion checks") != 0)
    {
        return 1;
    }

    if(expect_true(ENT_ThreadCreate(&tid, handle, short_lived_thread, &value) == 0,
                   "ENT_ThreadCreate should create a short-lived thread before timeout checks") != 0)
    {
        ENT_ThreadClose(handle);
        return 1;
    }

    reset_thread_probes();
    if(expect_true(ENT_ThreadWaitById(&tid, handle, 100) == 0,
                   "ENT_ThreadWaitById should succeed when the thread finishes before the timeout expires") != 0)
    {
        ENT_ThreadClose(handle);
        return 1;
    }

    if(expect_true(tid == NULL,
                   "ENT_ThreadWaitById should clear tid when the thread finishes before timeout") != 0)
    {
        ENT_ThreadClose(handle);
        return 1;
    }

    return expect_true(ENT_ThreadClose(handle) == 0, "ENT_ThreadClose should release the thread context after early completion checks");
}

static int test_thread_close_releases_thread_context(void)
{
    ENT_THREAD handle = NULL;
    ENT_THREAD_ID tid = NULL;
    int value = 11;

    if(expect_true(ENT_ThreadInit(&handle) == 0, "ENT_ThreadInit should create a thread context for close testing") != 0)
    {
        return 1;
    }

    if(expect_true(ENT_ThreadCreate(&tid, handle, sleepy_thread, &value) == 0,
                   "ENT_ThreadCreate should create a thread before close testing") != 0)
    {
        ENT_ThreadClose(handle);
        return 1;
    }

    reset_thread_probes();

    if(expect_true(ENT_ThreadClose(handle) == 0, "ENT_ThreadClose should close a populated thread context") != 0)
    {
        return 1;
    }
    return 0;
}

static int test_thread_close_does_not_call_pthread_cancel(void)
{
    ENT_THREAD handle = NULL;
    ENT_THREAD_ID tid = NULL;
    int value = 15;

    if(expect_true(ENT_ThreadInit(&handle) == 0, "ENT_ThreadInit should create a thread context for cancel checks") != 0)
    {
        return 1;
    }

    if(expect_true(ENT_ThreadCreate(&tid, handle, sleepy_thread, &value) == 0,
                   "ENT_ThreadCreate should create a thread before cancel checks") != 0)
    {
        ENT_ThreadClose(handle);
        return 1;
    }

    reset_thread_probes();

    if(expect_true(ENT_ThreadClose(handle) == 0, "ENT_ThreadClose should close a populated thread context without cancellation") != 0)
    {
        return 1;
    }

    return expect_true(s_cancel_call_count == 0,
                       "ENT_ThreadClose should not call pthread_cancel during normal close");
}

int main(void)
{
    int failures = 0;

    failures += test_thread_init_rejects_null_pointer();
    failures += test_thread_detach_create_rejects_invalid_handle();
    failures += test_thread_create_wait_and_close_roundtrip();
    failures += test_thread_wait_timeout_returns_retry_signal();
    failures += test_thread_wait_returns_success_when_thread_finishes_before_timeout();
    failures += test_thread_close_releases_thread_context();
    failures += test_thread_close_does_not_call_pthread_cancel();

    if(failures != 0)
    {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
