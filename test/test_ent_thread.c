#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ient_comm.h"
#include "ent_thread.h"

ENT_CTX gEntCtx;

typedef struct TEST_BAD_THREAD_CTX
{
    unsigned int tag;
} TEST_BAD_THREAD_CTX;

static int s_lock_depth = 0;
static int s_join_seen_inside_lock = 0;
static int s_cancel_seen_inside_lock = 0;

static int expect_true(int condition, const char* message)
{
    if(!condition)
    {
        fprintf(stderr, "%s\n", message);
        return 1;
    }

    return 0;
}

static void reset_thread_lock_probes(void)
{
    s_lock_depth = 0;
    s_join_seen_inside_lock = 0;
    s_cancel_seen_inside_lock = 0;
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
    if(s_lock_depth > 0)
    {
        s_join_seen_inside_lock = 1;
    }
    return 0;
}

int pthread_cancel(pthread_t thread)
{
    (void)thread;
    if(s_lock_depth > 0)
    {
        s_cancel_seen_inside_lock = 1;
    }
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t* mutex)
{
    (void)mutex;
    s_lock_depth++;
    return 0;
}

int pthread_mutex_unlock(pthread_mutex_t* mutex)
{
    (void)mutex;
    s_lock_depth--;
    return 0;
}

static void* quick_thread(void* data)
{
    return data;
}

static void* sleepy_thread(void* data)
{
    UTL_Sleep(50);
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

    reset_thread_lock_probes();

    if(expect_true(ENT_ThreadWaitById(&tid, handle, 0) == 0,
                   "ENT_ThreadWaitById should join a finished thread") != 0)
    {
        ENT_ThreadClose(handle);
        return 1;
    }

    if(expect_true(s_join_seen_inside_lock == 0,
                   "ENT_ThreadWaitById should not call pthread_join while dllLock is held") != 0)
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

static int test_thread_close_joins_outside_dll_lock(void)
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

    reset_thread_lock_probes();

    if(expect_true(ENT_ThreadClose(handle) == 0, "ENT_ThreadClose should close a populated thread context") != 0)
    {
        return 1;
    }

    if(expect_true(s_cancel_seen_inside_lock == 0,
                   "ENT_ThreadClose should not call pthread_cancel while dllLock is held") != 0)
    {
        return 1;
    }

    return expect_true(s_join_seen_inside_lock == 0,
                       "ENT_ThreadClose should not call pthread_join while dllLock is held");
}

int main(void)
{
    int failures = 0;

    failures += test_thread_init_rejects_null_pointer();
    failures += test_thread_detach_create_rejects_invalid_handle();
    failures += test_thread_create_wait_and_close_roundtrip();
    failures += test_thread_wait_timeout_returns_retry_signal();
    failures += test_thread_close_joins_outside_dll_lock();

    if(failures != 0)
    {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
