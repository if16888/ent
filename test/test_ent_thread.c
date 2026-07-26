#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

#include "ient_comm.h"
#include "ient_runtime.h"
#include "ent_msg.h"
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

/* ---------------------------------------------------------------------------
 * Thread-safe one-shot completion event.
 * publish() writes the result under lock, sets completed=1, and signals.
 * wait()    blocks until completed==1 under lock, then returns the result.
 * No volatile, no polling, no fixed sleep.
 * --------------------------------------------------------------------------- */
typedef struct TEST_RESULT_EVENT
{
#ifdef _WIN32
    CRITICAL_SECTION lock;
    CONDITION_VARIABLE cv;
#else
    pthread_mutex_t lock;
    pthread_cond_t  cv;
#endif
    int        completed;
    MSG_ID_T   result;
} TEST_RESULT_EVENT;

static int test_result_event_init(TEST_RESULT_EVENT* ev)
{
    if(ev == NULL)
        return -1;
    ev->completed = 0;
    ev->result    = 0;
#ifdef _WIN32
    InitializeCriticalSection(&ev->lock);
    InitializeConditionVariable(&ev->cv);
    return 0;
#else
    {
        int s = pthread_mutex_init(&ev->lock, NULL);
        if(s != 0)
            return s;
        s = pthread_cond_init(&ev->cv, NULL);
        if(s != 0)
        {
            pthread_mutex_destroy(&ev->lock);
            return s;
        }
        return 0;
    }
#endif
}

static void test_result_event_destroy(TEST_RESULT_EVENT* ev)
{
    if(ev == NULL)
        return;
#ifdef _WIN32
    DeleteCriticalSection(&ev->lock);
#else
    pthread_cond_destroy(&ev->cv);
    pthread_mutex_destroy(&ev->lock);
#endif
}

static void test_result_event_publish(TEST_RESULT_EVENT* ev, MSG_ID_T result)
{
#ifdef _WIN32
    EnterCriticalSection(&ev->lock);
    ev->result    = result;
    ev->completed = 1;
    LeaveCriticalSection(&ev->lock);
    WakeConditionVariable(&ev->cv);
#else
    pthread_mutex_lock(&ev->lock);
    ev->result    = result;
    ev->completed = 1;
    pthread_cond_broadcast(&ev->cv);
    pthread_mutex_unlock(&ev->lock);
#endif
}

static MSG_ID_T test_result_event_wait(TEST_RESULT_EVENT* ev)
{
    MSG_ID_T r;
#ifdef _WIN32
    EnterCriticalSection(&ev->lock);
    while(!ev->completed)
        SleepConditionVariableCS(&ev->cv, &ev->lock, INFINITE);
    r = ev->result;
    LeaveCriticalSection(&ev->lock);
#else
    pthread_mutex_lock(&ev->lock);
    while(!ev->completed)
        pthread_cond_wait(&ev->cv, &ev->lock);
    r = ev->result;
    pthread_mutex_unlock(&ev->lock);
#endif
    return r;
}

typedef struct TEST_SELF_CLOSE_CTX
{
    ENT_THREAD       handle;
    TEST_RESULT_EVENT completed;
} TEST_SELF_CLOSE_CTX;

typedef struct TEST_SELF_WAIT_CTX
{
    ENT_THREAD       handle;
    ENT_THREAD_ID    tid;
    TEST_RESULT_EVENT completed;
} TEST_SELF_WAIT_CTX;

typedef struct TEST_TID_PUBLICATION_CTX
{
    ENT_THREAD_ID* tid;
    int            sawPublishedTid;
    TEST_RESULT_EVENT completed;
} TEST_TID_PUBLICATION_CTX;

#ifdef _WIN32
static DWORD WINAPI tid_publication_thread(void* data)
#else
static void* tid_publication_thread(void* data)
#endif
{
    TEST_TID_PUBLICATION_CTX* ctx = (TEST_TID_PUBLICATION_CTX*)data;

    ctx->sawPublishedTid = (ctx->tid != NULL && *ctx->tid != NULL);
    test_result_event_publish(&ctx->completed, ctx->sawPublishedTid);
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

#ifdef _WIN32
static DWORD WINAPI self_close_thread(void* data)
#else
static void* self_close_thread(void* data)
#endif
{
    TEST_SELF_CLOSE_CTX* ctx = (TEST_SELF_CLOSE_CTX*)data;
    MSG_ID_T rc = ENT_ThreadClose(ctx->handle);
    test_result_event_publish(&ctx->completed, rc);
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

#ifdef _WIN32
static DWORD WINAPI self_wait_thread(void* data)
#else
static void* self_wait_thread(void* data)
#endif
{
    TEST_SELF_WAIT_CTX* ctx = (TEST_SELF_WAIT_CTX*)data;
    /* ctx->tid was written by ENT_ThreadCreate before the worker was
     * allowed to start (production registration barrier), so no sleep
     * is needed here.  See comm/ent_thread.c: registered flag + condvar
     * (Linux) and CREATE_SUSPENDED + ResumeThread (Windows). */
    MSG_ID_T rc = ENT_ThreadWaitById(&ctx->tid, ctx->handle, 0);
    test_result_event_publish(&ctx->completed, rc);
#ifdef _WIN32
    return 0;
#else
    return NULL;
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

#ifndef _WIN32
int pthread_cancel(pthread_t thread)
{
    (void)thread;
    s_cancel_call_count++;
    return 0;
}
#endif

#ifdef _WIN32
static DWORD WINAPI quick_thread(void* data)
#else
static void* quick_thread(void* data)
#endif
{
#ifdef _WIN32
    return (DWORD)(ULONG_PTR)data;
#else
    return data;
#endif
}

#ifdef _WIN32
static DWORD WINAPI sleepy_thread(void* data)
{
    Sleep(50);
    return (DWORD)(ULONG_PTR)data;
}

static DWORD WINAPI short_lived_thread(void* data)
{
    Sleep(5);
    return (DWORD)(ULONG_PTR)data;
}
#else
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
#endif

static int test_thread_init_rejects_null_pointer(void)
{
    return expect_true(ENT_ThreadInit(NULL) == ENT_THRD_INVALID_ARGUMENT, "ENT_ThreadInit should reject a NULL output pointer");
}

static int test_thread_detach_create_rejects_invalid_handle(void)
{
    TEST_BAD_THREAD_CTX badHandle;
    MSG_ID_T sts = 0;

    memset(&badHandle, 0, sizeof(badHandle));
    badHandle.tag = 0x12345678;

    sts = ENT_ThreadDetachCreate((ENT_THREAD)&badHandle, quick_thread, NULL);
    if(expect_true(sts == ENT_THRD_INVALID_HANDLE, "ENT_ThreadDetachCreate should reject an invalid thread handle") != 0)
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

static int test_thread_publishes_tid_before_callback(void)
{
    ENT_THREAD handle = NULL;
    ENT_THREAD_ID tid = NULL;
    TEST_TID_PUBLICATION_CTX ctx;

    memset(&ctx, 0, sizeof(ctx));
    ctx.tid = &tid;
    if(expect_true(test_result_event_init(&ctx.completed) == 0,
                   "publication event should initialize") != 0)
    {
        return 1;
    }
    if(expect_true(ENT_ThreadInit(&handle) == ENT_SYS_NORMAL,
                   "ENT_ThreadInit should create a context for tid publication checks") != 0)
    {
        test_result_event_destroy(&ctx.completed);
        return 1;
    }
    if(expect_true(ENT_ThreadCreate(&tid, handle, tid_publication_thread, &ctx) == ENT_SYS_NORMAL,
                   "ENT_ThreadCreate should publish a tid before starting the callback") != 0)
    {
        ENT_ThreadClose(handle);
        test_result_event_destroy(&ctx.completed);
        return 1;
    }
    test_result_event_wait(&ctx.completed);
    if(expect_true(ENT_ThreadWaitById(&tid, handle, 0) == ENT_SYS_NORMAL,
                   "ENT_ThreadWaitById should join the publication worker") != 0)
    {
        ENT_ThreadClose(handle);
        test_result_event_destroy(&ctx.completed);
        return 1;
    }
    if(expect_true(ctx.sawPublishedTid != 0,
                   "thread callback should observe the published tid") != 0)
    {
        ENT_ThreadClose(handle);
        test_result_event_destroy(&ctx.completed);
        return 1;
    }
    if(expect_true(ENT_ThreadClose(handle) == ENT_SYS_NORMAL,
                   "ENT_ThreadClose should release the publication context") != 0)
    {
        test_result_event_destroy(&ctx.completed);
        return 1;
    }
    test_result_event_destroy(&ctx.completed);
    return 0;
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
    if(expect_true(sts == ENT_THRD_WAIT_TIMEOUT, "ENT_ThreadWaitById should return timeout when the thread is still running") != 0)
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
#ifdef _WIN32
    return 0;
#else
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
#endif
}

static int test_thread_close_rejects_self_close(void)
{
    ENT_THREAD handle = NULL;
    TEST_SELF_CLOSE_CTX ctx;
    MSG_ID_T rc;

    memset(&ctx, 0, sizeof(ctx));
    if(test_result_event_init(&ctx.completed) != 0)
    {
        fprintf(stderr, "test_result_event_init failed\n");
        return 1;
    }
    if(expect_true(ENT_ThreadInit(&handle) == ENT_SYS_NORMAL,
                   "ENT_ThreadInit should create a context for self-close checks") != 0)
    {
        test_result_event_destroy(&ctx.completed);
        return 1;
    }
    ctx.handle = handle;
    if(expect_true(ENT_ThreadCreate(NULL, handle, self_close_thread, &ctx) == ENT_SYS_NORMAL,
                   "ENT_ThreadCreate should create a worker for self-close checks") != 0)
    {
        ENT_ThreadClose(handle);
        test_result_event_destroy(&ctx.completed);
        return 1;
    }
    /* Wait for the worker to publish its result before touching ctx or handle. */
    rc = test_result_event_wait(&ctx.completed);
    if(expect_true(rc == ENT_THRD_IN_USE,
                   "ENT_ThreadClose should reject a close requested by its own worker") != 0)
    {
        ENT_ThreadClose(handle);
        test_result_event_destroy(&ctx.completed);
        return 1;
    }
    /* Worker has returned; now the main thread can safely close the context. */
    rc = ENT_ThreadClose(handle);
    test_result_event_destroy(&ctx.completed);
    return expect_true(rc == ENT_SYS_NORMAL,
                       "ENT_ThreadClose should succeed after the self-close worker exits");
}

static int test_thread_wait_rejects_self_wait(void)
{
    ENT_THREAD handle = NULL;
    TEST_SELF_WAIT_CTX ctx;
    MSG_ID_T rc;

    memset(&ctx, 0, sizeof(ctx));
    if(test_result_event_init(&ctx.completed) != 0)
    {
        fprintf(stderr, "test_result_event_init failed\n");
        return 1;
    }
    if(expect_true(ENT_ThreadInit(&handle) == ENT_SYS_NORMAL,
                   "ENT_ThreadInit should create a context for self-wait checks") != 0)
    {
        test_result_event_destroy(&ctx.completed);
        return 1;
    }
    ctx.handle = handle;
    /* ENT_ThreadCreate writes ctx.tid under the production registration
     * barrier before the worker is allowed to run, so the worker can
     * safely read ctx.tid without an extra sleep or barrier here. */
    if(expect_true(ENT_ThreadCreate(&ctx.tid, handle, self_wait_thread, &ctx) == ENT_SYS_NORMAL,
                   "ENT_ThreadCreate should create a worker for self-wait checks") != 0)
    {
        ENT_ThreadClose(handle);
        test_result_event_destroy(&ctx.completed);
        return 1;
    }
    /* Block until the worker has published its result. */
    rc = test_result_event_wait(&ctx.completed);
    if(expect_true(rc == ENT_THRD_IN_USE,
                   "ENT_ThreadWaitById should reject a worker waiting on itself") != 0)
    {
        ENT_ThreadClose(handle);
        test_result_event_destroy(&ctx.completed);
        return 1;
    }
    rc = ENT_ThreadClose(handle);
    test_result_event_destroy(&ctx.completed);
    return expect_true(rc == ENT_SYS_NORMAL,
                       "ENT_ThreadClose should join the worker after self-wait rejection");
}

int main(void)
{
    int failures = 0;

    failures += test_thread_init_rejects_null_pointer();
    failures += test_thread_detach_create_rejects_invalid_handle();
    failures += test_thread_create_wait_and_close_roundtrip();
    failures += test_thread_publishes_tid_before_callback();
    failures += test_thread_wait_timeout_returns_retry_signal();
    failures += test_thread_wait_returns_success_when_thread_finishes_before_timeout();
    failures += test_thread_close_releases_thread_context();
    failures += test_thread_close_does_not_call_pthread_cancel();
    failures += test_thread_close_rejects_self_close();
    failures += test_thread_wait_rejects_self_wait();

    if(failures != 0)
    {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
