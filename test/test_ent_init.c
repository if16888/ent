#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#ifdef WIN32
#include <Windows.h>
#else
#include <pthread.h>
#include <time.h>
#endif

#include "ient_comm.h"
#include "ient_runtime.h"
#include "ent_init.h"
#include "ent_msg.h"

static UTL_CV s_last_cv = NULL;
static UTL_LOCK s_last_lock = NULL;
static int s_wait_calls = 0;
static int s_auto_stop_after_wait = 0;
static int s_block_wait_mode = 0;
static UTL_CV s_block_wait_cv = NULL;
static volatile int s_block_wait_entered = 0;
static volatile int s_block_wait_released = 0;
static volatile int s_close_thread_started = 0;
static volatile int s_close_wait_entered = 0;
static int s_log_init_calls = 0;
static int s_log_close_handle_calls = 0;
static int s_log_close_calls = 0;
static int s_lock_close_calls = 0;
static int s_cv_close_calls = 0;
static int s_fail_log_init = 0;
static int s_fail_log_init_handle_call = 0;
static int s_log_init_handle_calls = 0;
static int s_fail_log_set_option_call = 0;
static int s_log_set_option_calls = 0;
static int s_fail_lock_init = 0;
static int s_log_error_calls = 0;
static int s_event_counter = 0;
static int s_log_error_order = 0;
static int s_log_close_order = 0;
static int s_mlockall_result = 0;
static int s_mlockall_errno = 0;
static int s_mlockall_calls = 0;
static int s_munlockall_result = 0;
static int s_munlockall_errno = 0;
static int s_munlockall_calls = 0;
static uintptr_t s_next_log_handle = 0x1000;
static uintptr_t s_next_lock_handle = 0x2000;
static uintptr_t s_next_cv_handle = 0x3000;
static ENT_LOG s_ent_log_at_lock_init = NULL;
static const char* s_last_log_init_handle_module = NULL;
static const char* s_last_log_init_handle_path = NULL;
static const char* s_first_log_init_handle_module = NULL;
static const char* s_first_log_init_handle_path = NULL;
static const char* s_second_log_init_handle_module = NULL;
static const char* s_second_log_init_handle_path = NULL;
static ENT_LOG s_last_closed_log_handle = NULL;
static ENT_LOG s_first_closed_log_handle = NULL;
static ENT_LOG s_second_closed_log_handle = NULL;
static ENT_LOG s_last_log_error_handle = NULL;
static ENT_LOG s_last_log_set_option_handle = NULL;
static ENT_LOG_OPTIONS_E s_last_log_set_option = (ENT_LOG_OPTIONS_E)-1;
static int s_last_log_set_level = -1;
static char s_expected_log_path[512];

static const char* expected_log_path_for(const char* work_path)
{
    size_t len = strlen(work_path);

#ifdef WIN32
    if(work_path[len - 1] == '\\' || work_path[len - 1] == '/')
#else
    if(work_path[len - 1] == '/')
#endif
    {
        snprintf(s_expected_log_path, sizeof(s_expected_log_path), "%slog", work_path);
    }
    else
    {
        snprintf(s_expected_log_path, sizeof(s_expected_log_path), "%s%slog", work_path, ENT_FILE_SEP);
    }

    return s_expected_log_path;
}

static void reset_wait_capture(void)
{
    s_last_cv = NULL;
    s_last_lock = NULL;
    s_wait_calls = 0;
    s_auto_stop_after_wait = 0;
    s_block_wait_mode = 0;
    s_block_wait_cv = NULL;
    s_block_wait_entered = 0;
    s_block_wait_released = 0;
    s_close_thread_started = 0;
    s_close_wait_entered = 0;
}

static void enable_auto_stop_after_wait(void)
{
    s_auto_stop_after_wait = 1;
}

static void enable_blocking_wait(UTL_CV cv)
{
    s_block_wait_mode = 1;
    s_block_wait_cv = cv;
    s_block_wait_entered = 0;
    s_block_wait_released = 0;
}

static void iENT_TestSleepMs(unsigned int ms)
{
#ifdef WIN32
    Sleep(ms);
#else
    struct timespec ts;

    ts.tv_sec = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)((ms % 1000u) * 1000000u);
    nanosleep(&ts, NULL);
#endif
}

static void wait_until_blocking_wait_entered(void)
{
    while(!s_block_wait_entered)
    {
        iENT_TestSleepMs(1);
    }
}

static void wait_until_close_thread_started(void)
{
    while(!s_close_thread_started)
    {
        iENT_TestSleepMs(1);
    }
}

static void wait_until_close_wait_entered(void)
{
    while(!s_close_wait_entered)
    {
        iENT_TestSleepMs(1);
    }
}

static void release_blocking_wait(void)
{
    s_block_wait_released = 1;
}

static void reset_close_counters(void)
{
    s_log_close_handle_calls = 0;
    s_log_close_calls = 0;
    s_lock_close_calls = 0;
    s_cv_close_calls = 0;
}

static void reset_log_failures(void)
{
    s_fail_log_init = 0;
    s_fail_log_init_handle_call = 0;
    s_log_init_calls = 0;
    s_log_init_handle_calls = 0;
    s_fail_log_set_option_call = 0;
    s_log_set_option_calls = 0;
    s_fail_lock_init = 0;
    s_log_error_calls = 0;
    s_event_counter = 0;
    s_log_error_order = 0;
    s_log_close_order = 0;
    s_ent_log_at_lock_init = NULL;
    s_last_log_init_handle_module = NULL;
    s_last_log_init_handle_path = NULL;
    s_first_log_init_handle_module = NULL;
    s_first_log_init_handle_path = NULL;
    s_second_log_init_handle_module = NULL;
    s_second_log_init_handle_path = NULL;
    s_last_closed_log_handle = NULL;
    s_first_closed_log_handle = NULL;
    s_second_closed_log_handle = NULL;
    s_last_log_error_handle = NULL;
    s_last_log_set_option_handle = NULL;
    s_last_log_set_option = (ENT_LOG_OPTIONS_E)-1;
    s_last_log_set_level = -1;
    s_mlockall_result = 0;
    s_mlockall_errno = 0;
    s_mlockall_calls = 0;
    s_munlockall_result = 0;
    s_munlockall_errno = 0;
    s_munlockall_calls = 0;
    s_next_log_handle = 0x1000;
    s_next_lock_handle = 0x2000;
    s_next_cv_handle = 0x3000;
}

static int expect_true(int condition, const char* message)
{
    if(!condition)
    {
        fprintf(stderr, "%s\n", message);
        return 1;
    }

    return 0;
}

static void close_handle_if_needed(ENT_HANDLE* handle)
{
    if(handle != NULL && *handle != NULL)
    {
        ENT_Close(handle);
    }
}

typedef struct TEST_RUN_THREAD_CTX
{
    ENT_HANDLE handle;
    MSG_ID_T   ret;
} TEST_RUN_THREAD_CTX;

typedef struct TEST_CLOSE_THREAD_CTX
{
    ENT_HANDLE* handle;
    MSG_ID_T    ret;
} TEST_CLOSE_THREAD_CTX;

#ifdef WIN32
static DWORD WINAPI close_thread_proc(void* arg);
#else
static void* close_thread_proc(void* arg);
#endif

#ifdef WIN32
typedef HANDLE TEST_THREAD;
static DWORD WINAPI run_stop_thread_proc(void* arg)
#else
typedef pthread_t TEST_THREAD;
static void* run_stop_thread_proc(void* arg)
#endif
{
    TEST_RUN_THREAD_CTX* ctx = (TEST_RUN_THREAD_CTX*)arg;

    if(ctx != NULL)
    {
        ctx->ret = ENT_Run(ctx->handle);
    }

#ifdef WIN32
    return 0;
#else
    return NULL;
#endif
}

#ifdef WIN32
static DWORD WINAPI close_thread_proc(void* arg)
#else
static void* close_thread_proc(void* arg)
#endif
{
    TEST_CLOSE_THREAD_CTX* ctx = (TEST_CLOSE_THREAD_CTX*)arg;

    s_close_thread_started = 1;
    if(ctx != NULL)
    {
        ctx->ret = ENT_Close(ctx->handle);
    }

#ifdef WIN32
    return 0;
#else
    return NULL;
#endif
}

static int start_test_thread(TEST_THREAD* th, TEST_RUN_THREAD_CTX* ctx)
{
#ifdef WIN32
    if(th == NULL)
    {
        return -1;
    }

    *th = CreateThread(NULL, 0, run_stop_thread_proc, ctx, 0, NULL);
    return (*th != NULL) ? 0 : -1;
#else
    return pthread_create(th, NULL, run_stop_thread_proc, ctx);
#endif
}

static int start_close_test_thread(TEST_THREAD* th, TEST_CLOSE_THREAD_CTX* ctx)
{
#ifdef WIN32
    if(th == NULL)
    {
        return -1;
    }

    *th = CreateThread(NULL, 0, close_thread_proc, ctx, 0, NULL);
    return (*th != NULL) ? 0 : -1;
#else
    return pthread_create(th, NULL, close_thread_proc, ctx);
#endif
}

static int join_test_thread(TEST_THREAD th)
{
#ifdef WIN32
    if(WaitForSingleObject(th, INFINITE) != WAIT_OBJECT_0)
    {
        return -1;
    }
    CloseHandle(th);
    return 0;
#else
    return pthread_join(th, NULL);
#endif
}

#ifndef WIN32
int mlockall(int flags)
{
    (void)flags;
    s_mlockall_calls++;
    if(s_mlockall_result != 0)
    {
        errno = s_mlockall_errno;
    }
    return s_mlockall_result;
}

int munlockall(void)
{
    s_munlockall_calls++;
    if(s_munlockall_result != 0)
    {
        errno = s_munlockall_errno;
    }
    return s_munlockall_result;
}
#endif

MSG_ID_T ENT_LogInit(void)
{
    s_log_init_calls++;
    if(s_fail_log_init != 0)
    {
        return s_fail_log_init;
    }
    return 0;
}

MSG_ID_T ENT_LogClose(void)
{
    s_log_close_calls++;
    s_log_close_order = ++s_event_counter;
    return 0;
}

MSG_ID_T ENT_LogInitHandle(ENT_LOG* pLogHandle, const char* moduleName, const char* logPath)
{
    s_log_init_handle_calls++;
    s_last_log_init_handle_module = moduleName;
    s_last_log_init_handle_path = logPath;
    if(s_log_init_handle_calls == 1)
    {
        s_first_log_init_handle_module = moduleName;
        s_first_log_init_handle_path = logPath;
    }
    else if(s_log_init_handle_calls == 2)
    {
        s_second_log_init_handle_module = moduleName;
        s_second_log_init_handle_path = logPath;
    }
    if(s_fail_log_init_handle_call == s_log_init_handle_calls)
    {
        return -100 - s_log_init_handle_calls;
    }
    if(pLogHandle != NULL)
    {
        *pLogHandle = (ENT_LOG)(uintptr_t)s_next_log_handle;
        s_next_log_handle += 0x10;
    }
    return 0;
}

MSG_ID_T ENT_LogSetOption(ENT_LOG logHandle, ENT_LOG_OPTIONS_E option, const void* arg)
{
    s_log_set_option_calls++;
    s_last_log_set_option_handle = logHandle;
    s_last_log_set_option = option;
    if(option == ENT_LOG_LEVEL_E && arg != NULL)
    {
        s_last_log_set_level = *((const int*)arg);
    }
    if(s_fail_log_set_option_call == s_log_set_option_calls)
    {
        return -200 - s_log_set_option_calls;
    }
    return 0;
}

MSG_ID_T ENT_LogCloseHandle(ENT_LOG logHandle)
{
    s_last_closed_log_handle = logHandle;
    s_log_close_handle_calls++;
    if(s_log_close_handle_calls == 1)
    {
        s_first_closed_log_handle = logHandle;
    }
    else if(s_log_close_handle_calls == 2)
    {
        s_second_closed_log_handle = logHandle;
    }
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
    s_last_log_error_handle = logHandle;
    s_log_error_calls++;
    s_log_error_order = ++s_event_counter;
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

MSG_ID_T UTL_LockInit(UTL_LOCK* lock, const char* name)
{
    (void)name;
    s_ent_log_at_lock_init = iENT_LogDefaultHandle();
    if(s_fail_lock_init != 0)
    {
        return s_fail_lock_init;
    }
    if(lock != NULL)
    {
        *lock = (UTL_LOCK)(uintptr_t)s_next_lock_handle;
        s_next_lock_handle += 0x10;
    }
    return 0;
}

MSG_ID_T UTL_LockInitEx(UTL_LOCK* lock, const char* name, UTL_LOCK_TYPE_T type)
{
    (void)type;
    return UTL_LockInit(lock, name);
}

MSG_ID_T UTL_LockEnter(UTL_LOCK lock)
{
    (void)lock;
    return 0;
}

MSG_ID_T UTL_LockEnterEx(UTL_LOCK lock, UTL_LOCK_RW_TYPE_T rwType)
{
    (void)lock;
    (void)rwType;
    return 0;
}

MSG_ID_T UTL_LockLeave(UTL_LOCK lock)
{
    (void)lock;
    return 0;
}

MSG_ID_T UTL_LockLeaveEx(UTL_LOCK lock, UTL_LOCK_RW_TYPE_T rwType)
{
    (void)lock;
    (void)rwType;
    return 0;
}

MSG_ID_T UTL_LockClose(UTL_LOCK* lock)
{
    (void)lock;
    s_lock_close_calls++;
    if(lock != NULL)
    {
        *lock = NULL;
    }
    return 0;
}

MSG_ID_T UTL_CVInit(UTL_CV* cv, const char* name)
{
    (void)name;
    if(cv != NULL)
    {
        *cv = (UTL_CV)(uintptr_t)s_next_cv_handle;
        s_next_cv_handle += 0x10;
    }
    return 0;
}

MSG_ID_T UTL_CVClose(UTL_CV* cv)
{
    (void)cv;
    s_cv_close_calls++;
    if(cv != NULL)
    {
        *cv = NULL;
    }
    return 0;
}

MSG_ID_T UTL_CVWait(UTL_CV cv, UTL_LOCK lock, int ms, UTL_LOCK_RW_TYPE_T rwType)
{
    (void)ms;
    (void)rwType;
    s_last_cv = cv;
    s_last_lock = lock;
    s_wait_calls++;
    if(s_block_wait_mode && cv == s_block_wait_cv)
    {
        ENT_CTX* ctx = iENT_RuntimeActiveCtx();

        s_block_wait_entered = 1;
        if(ctx != NULL && ctx->handleState == ENT_HANDLE_STATE_CLOSING_E)
        {
            s_close_wait_entered = 1;
        }
        while(!s_block_wait_released)
        {
            iENT_TestSleepMs(1);
        }
        return 0;
    }
    if(s_auto_stop_after_wait)
    {
        ENT_CTX* ctx = iENT_RuntimeActiveCtx();
        if(ctx != NULL)
        {
            ctx->stopRequested = true;
        }
        s_auto_stop_after_wait = 0;
    }
    return 0;
}

MSG_ID_T UTL_CVWake(UTL_CV cv)
{
    (void)cv;
    return 0;
}

MSG_ID_T UTL_CVWakeAll(UTL_CV cv)
{
    if(s_block_wait_mode && cv == s_block_wait_cv)
    {
        release_blocking_wait();
    }
    return 0;
}

static int test_ent_run_rejects_uninitialized_context(void)
{
    reset_wait_capture();

    if(expect_true(ENT_Run(NULL) == ENT_SYS_RUN_UNINITIALIZED, "ENT_Run should reject an uninitialized context") != 0)
    {
        return 1;
    }

    return expect_true(s_wait_calls == 0, "ENT_Run should not wait when the context is uninitialized");
}

static int test_ent_run_waits_on_cv_with_lock(void)
{
    TEST_THREAD th;
    TEST_RUN_THREAD_CTX threadCtx;
    ENT_HANDLE handle = (ENT_HANDLE)calloc(1, sizeof(*handle));

    if(handle == NULL)
    {
        return 1;
    }

    handle->magic = ENT_HANDLE_MAGIC;
    handle->ctx.isInit = true;
    handle->ctx.entCV = (UTL_CV)0x1234;
    handle->ctx.entLock = (UTL_LOCK)0x5678;
    handle->ctx.running = false;
    handle->ctx.stopRequested = false;
    reset_wait_capture();
    enable_blocking_wait(handle->ctx.entCV);

    threadCtx.handle = handle;
    threadCtx.ret = ENT_SYS_INVALID_ARGUMENT;

    if(expect_true(start_test_thread(&th, &threadCtx) == 0,
                   "thread start should start the ENT_Run worker") != 0)
    {
        free(handle);
        return 1;
    }

    wait_until_blocking_wait_entered();

    if(expect_true(ENT_Stop(handle) == ENT_SYS_NORMAL,
                   "ENT_Stop should wake a running ENT_Run worker") != 0)
    {
        release_blocking_wait();
        join_test_thread(th);
        free(handle);
        return 1;
    }

    if(expect_true(join_test_thread(th) == 0,
                   "thread join should wait for the ENT_Run worker to exit") != 0)
    {
        free(handle);
        return 1;
    }

    if(expect_true(threadCtx.ret == ENT_SYS_NORMAL,
                   "ENT_Run should return NORMAL after ENT_Stop wakes it") != 0)
    {
        free(handle);
        return 1;
    }

    if(expect_true(s_wait_calls == 1, "ENT_Run should call UTL_CVWait exactly once") != 0)
    {
        free(handle);
        return 1;
    }

    if(expect_true(s_last_cv == handle->ctx.entCV, "ENT_Run should pass entCV as the first UTL_CVWait argument") != 0)
    {
        free(handle);
        return 1;
    }

    if(expect_true(s_last_lock == handle->ctx.entLock, "ENT_Run should pass entLock as the second UTL_CVWait argument") != 0)
    {
        free(handle);
        return 1;
    }

    free(handle);
    return 0;
}

static int test_ent_stop_validates_states(void)
{
    ENT_HANDLE handle = NULL;

    reset_wait_capture();
    reset_close_counters();
    reset_log_failures();

    if(expect_true(ENT_Stop(NULL) == ENT_SYS_INVALID_ARGUMENT,
                   "ENT_Stop should reject a NULL handle") != 0)
    {
        return 1;
    }

    if(expect_true(ENT_Init(&handle, "demo", "/tmp/demo", LOG_LEV_WARN_E, ENT_MODE_NORMAL_E) == ENT_SYS_NORMAL,
                   "ENT_Init should succeed before testing ENT_Stop states") != 0)
    {
        return 1;
    }

    if(expect_true(ENT_Stop(handle) == ENT_SYS_NORMAL,
                   "ENT_Stop should stop a running handle the first time") != 0)
    {
        ENT_Close(&handle);
        return 1;
    }

    if(expect_true(ENT_Stop(handle) == ENT_SYS_STOPPED,
                   "ENT_Stop should report a handle that is already stopped") != 0)
    {
        ENT_Close(&handle);
        return 1;
    }

    if(expect_true(ENT_Close(&handle) == ENT_SYS_NORMAL && handle == NULL,
                   "ENT_Close should still succeed after ENT_Stop") != 0)
    {
        return 1;
    }

    return 0;
}

static int test_ent_stop_rejects_stale_handle(void)
{
    ENT_HANDLE handle = NULL;
    ENT_HANDLE staleHandle = NULL;

    reset_wait_capture();
    reset_close_counters();
    reset_log_failures();

    if(expect_true(ENT_Init(&handle, "demo", "/tmp/demo", LOG_LEV_WARN_E, ENT_MODE_NORMAL_E) == ENT_SYS_NORMAL,
                   "ENT_Init should succeed before testing stale ENT_Stop") != 0)
    {
        return 1;
    }

    if(expect_true(handle != NULL, "ENT_Init should return a live handle before stale ENT_Stop") != 0)
    {
        close_handle_if_needed(&handle);
        return 1;
    }

    staleHandle = handle;
    if(expect_true(ENT_Close(&handle) == ENT_SYS_NORMAL && handle == NULL,
                   "ENT_Close should clear the live handle before stale ENT_Stop checks") != 0)
    {
        return 1;
    }

    return expect_true(ENT_Stop(staleHandle) == ENT_SYS_BAD_HANDLE,
                       "ENT_Stop should reject a stale handle");
}

static int test_ent_run_rejects_stale_handle(void)
{
    ENT_HANDLE handle = NULL;
    ENT_HANDLE staleHandle = NULL;

    reset_wait_capture();
    reset_close_counters();
    reset_log_failures();

    if(expect_true(ENT_Init(&handle, "demo", "/tmp/demo", LOG_LEV_WARN_E, ENT_MODE_NORMAL_E) == ENT_SYS_NORMAL,
                   "ENT_Init should succeed before testing stale ENT_Run") != 0)
    {
        return 1;
    }

    if(expect_true(handle != NULL, "ENT_Init should return a live handle before stale ENT_Run") != 0)
    {
        close_handle_if_needed(&handle);
        return 1;
    }

    staleHandle = handle;
    if(expect_true(ENT_Close(&handle) == ENT_SYS_NORMAL && handle == NULL,
                   "ENT_Close should clear the live handle before stale ENT_Run checks") != 0)
    {
        return 1;
    }

    return expect_true(ENT_Run(staleHandle) == ENT_SYS_BAD_HANDLE,
                       "ENT_Run should reject a stale handle");
}

static int test_ent_close_rejects_stale_handle(void)
{
    ENT_HANDLE handle = NULL;
    ENT_HANDLE staleHandle = NULL;

    reset_wait_capture();
    reset_close_counters();
    reset_log_failures();

    if(expect_true(ENT_Init(&handle, "demo", "/tmp/demo", LOG_LEV_WARN_E, ENT_MODE_NORMAL_E) == ENT_SYS_NORMAL,
                   "ENT_Init should succeed before testing stale ENT_Close") != 0)
    {
        return 1;
    }

    if(expect_true(handle != NULL, "ENT_Init should return a live handle before stale ENT_Close") != 0)
    {
        close_handle_if_needed(&handle);
        return 1;
    }

    staleHandle = handle;
    if(expect_true(ENT_Close(&handle) == ENT_SYS_NORMAL && handle == NULL,
                   "ENT_Close should clear the live handle before stale ENT_Close checks") != 0)
    {
        return 1;
    }

    return expect_true(ENT_Close(&staleHandle) == ENT_SYS_BAD_HANDLE,
                       "ENT_Close should reject a stale handle pointer");
}

static int test_ent_run_returns_stopped_when_stop_requested_before_entry(void)
{
    ENT_HANDLE handle = NULL;

    reset_wait_capture();
    reset_close_counters();
    reset_log_failures();

    if(expect_true(ENT_Init(&handle, "demo", "/tmp/demo", LOG_LEV_WARN_E, ENT_MODE_NORMAL_E) == ENT_SYS_NORMAL,
                   "ENT_Init should succeed before testing pre-stopped ENT_Run") != 0)
    {
        return 1;
    }

    if(expect_true(ENT_Stop(handle) == ENT_SYS_NORMAL,
                   "ENT_Stop should succeed before checking the pre-stopped run path") != 0)
    {
        close_handle_if_needed(&handle);
        return 1;
    }

    if(expect_true(ENT_Run(handle) == ENT_SYS_STOPPED,
                   "ENT_Run should report STOPPED when stop was already requested") != 0)
    {
        close_handle_if_needed(&handle);
        return 1;
    }

    return expect_true(ENT_Close(&handle) == ENT_SYS_NORMAL && handle == NULL,
                       "ENT_Close should still succeed after a pre-stopped ENT_Run");
}

static int test_ent_set_rt_attributes_rejects_stale_handle(void)
{
    ENT_HANDLE handle = NULL;
    ENT_HANDLE staleHandle = NULL;

    reset_wait_capture();
    reset_close_counters();
    reset_log_failures();

    if(expect_true(ENT_Init(&handle, "demo", "/tmp/demo", LOG_LEV_WARN_E, ENT_MODE_NORMAL_E) == ENT_SYS_NORMAL,
                   "ENT_Init should succeed before testing stale ENT_SetRtAttributes") != 0)
    {
        return 1;
    }

    if(expect_true(handle != NULL, "ENT_Init should return a live handle before stale ENT_SetRtAttributes") != 0)
    {
        close_handle_if_needed(&handle);
        return 1;
    }

    staleHandle = handle;
    if(expect_true(ENT_Close(&handle) == ENT_SYS_NORMAL && handle == NULL,
                   "ENT_Close should clear the live handle before stale ENT_SetRtAttributes checks") != 0)
    {
        return 1;
    }

    return expect_true(ENT_SetRtAttributes(staleHandle, -1, ENT_RT_POLICY_OTHER_E, 0) == ENT_SYS_BAD_HANDLE,
                       "ENT_SetRtAttributes should reject a stale handle");
}

static int test_ent_run_rejects_closed_handle(void)
{
    ENT_HANDLE handle = NULL;

    reset_wait_capture();
    reset_close_counters();
    reset_log_failures();

    if(expect_true(ENT_Init(&handle, "demo", "/tmp/demo", LOG_LEV_WARN_E, ENT_MODE_NORMAL_E) == ENT_SYS_NORMAL,
                   "ENT_Init should succeed before testing close/run rejection") != 0)
    {
        return 1;
    }

    if(expect_true(ENT_Close(&handle) == ENT_SYS_NORMAL && handle == NULL,
                   "ENT_Close should clear the handle before close/run rejection checks") != 0)
    {
        return 1;
    }

    return expect_true(ENT_Run(handle) == ENT_SYS_RUN_UNINITIALIZED,
                       "ENT_Run should reject a closed handle");
}

static int test_ent_close_stops_running_handle(void)
{
    TEST_THREAD th;
    TEST_RUN_THREAD_CTX threadCtx;
    ENT_HANDLE handle = (ENT_HANDLE)calloc(1, sizeof(*handle));

    if(handle == NULL)
    {
        return 1;
    }

    handle->magic = ENT_HANDLE_MAGIC;
    handle->ctx.isInit = true;
    handle->ctx.entCV = (UTL_CV)0x2233;
    handle->ctx.entLock = (UTL_LOCK)0x6677;
    handle->ctx.running = false;
    handle->ctx.stopRequested = false;
    reset_wait_capture();
    enable_blocking_wait(handle->ctx.entCV);

    threadCtx.handle = handle;
    threadCtx.ret = ENT_SYS_INVALID_ARGUMENT;

    if(expect_true(start_test_thread(&th, &threadCtx) == 0,
                   "thread start should start the ENT_Run worker for close testing") != 0)
    {
        free(handle);
        return 1;
    }

    wait_until_blocking_wait_entered();

    if(expect_true(ENT_Close(&handle) == ENT_SYS_NORMAL,
                   "ENT_Close should stop and close a running handle") != 0)
    {
        release_blocking_wait();
        join_test_thread(th);
        return 1;
    }

    if(expect_true(handle == NULL, "ENT_Close should clear the handle after stopping it") != 0)
    {
        release_blocking_wait();
        join_test_thread(th);
        return 1;
    }

    if(expect_true(join_test_thread(th) == 0,
                   "thread join should wait for the ENT_Run worker to exit after close") != 0)
    {
        return 1;
    }

    if(expect_true(threadCtx.ret == ENT_SYS_NORMAL,
                   "ENT_Run should return NORMAL after ENT_Close stops it") != 0)
    {
        return 1;
    }

    if(expect_true(s_wait_calls >= 1, "ENT_Close should wait at least once to release the running worker") != 0)
    {
        return 1;
    }

    return 0;
}

static int test_ent_close_waits_for_running_worker_before_free(void)
{
    TEST_THREAD runTh;
    TEST_THREAD closeTh;
    TEST_RUN_THREAD_CTX runCtx;
    TEST_CLOSE_THREAD_CTX closeCtx;
    ENT_HANDLE handle = (ENT_HANDLE)calloc(1, sizeof(*handle));
    ENT_HANDLE rawHandle = handle;
    int failed = 0;

    if(handle == NULL)
    {
        return 1;
    }

    handle->magic = ENT_HANDLE_MAGIC;
    handle->ctx.isInit = true;
    handle->ctx.entCV = (UTL_CV)0x3344;
    handle->ctx.entLock = (UTL_LOCK)0x7788;
    handle->ctx.running = false;
    handle->ctx.stopRequested = false;
    handle->ctx.handleState = ENT_HANDLE_STATE_ACTIVE_E;
    handle->ctx.activeCalls = 0u;
    reset_wait_capture();
    enable_blocking_wait(handle->ctx.entCV);

    runCtx.handle = handle;
    runCtx.ret = ENT_SYS_INVALID_ARGUMENT;
    closeCtx.handle = &handle;
    closeCtx.ret = ENT_SYS_INVALID_ARGUMENT;

    if(expect_true(start_test_thread(&runTh, &runCtx) == 0,
                   "thread start should start the ENT_Run worker for close-wait testing") != 0)
    {
        free(rawHandle);
        return 1;
    }

    wait_until_blocking_wait_entered();

    if(expect_true(start_close_test_thread(&closeTh, &closeCtx) == 0,
                   "thread start should start the ENT_Close worker") != 0)
    {
        release_blocking_wait();
        join_test_thread(runTh);
        free(rawHandle);
        return 1;
    }

    wait_until_close_thread_started();
    wait_until_close_wait_entered();

    if(expect_true(ENT_Run(handle) != ENT_SYS_NORMAL,
                   "ENT_Run should not succeed while close is in progress") != 0)
    {
        failed = 1;
    }

    release_blocking_wait();

    if(expect_true(join_test_thread(runTh) == 0,
                   "thread join should wait for the ENT_Run worker to exit after close wait") != 0)
    {
        failed = 1;
    }

    if(expect_true(join_test_thread(closeTh) == 0,
                   "thread join should wait for the ENT_Close worker to exit") != 0)
    {
        failed = 1;
    }

    if(expect_true(runCtx.ret == ENT_SYS_NORMAL,
                   "ENT_Run should return NORMAL after ENT_Close stops it") != 0)
    {
        failed = 1;
    }

    if(expect_true(closeCtx.ret == ENT_SYS_NORMAL,
                   "ENT_Close should return NORMAL after waiting for the running worker") != 0)
    {
        failed = 1;
    }

    if(expect_true(handle == NULL,
                   "ENT_Close should clear the handle after waiting for the running worker") != 0)
    {
        failed = 1;
    }

    free(rawHandle);
    return failed;
}

static int test_ent_close_clears_handle_instances(void)
{
    ENT_HANDLE handle = (ENT_HANDLE)calloc(1, sizeof(*handle));

    if(handle == NULL)
    {
        return 1;
    }

    handle->magic = ENT_HANDLE_MAGIC;
    handle->ctx.isInit = true;
    handle->ctx.entLog = (ENT_LOG)0x11;
    handle->ctx.entLock = (UTL_LOCK)0x22;
    handle->ctx.entCV = (UTL_CV)0x33;
    handle->ctx.running = false;
    handle->ctx.stopRequested = false;
    reset_close_counters();

    if(expect_true(ENT_Close(&handle) == ENT_SYS_NORMAL, "ENT_Close should succeed for an initialized context") != 0)
    {
        return 1;
    }

    if(expect_true(handle == NULL, "ENT_Close should clear the handle") != 0)
    {
        return 1;
    }

    if(expect_true(ENT_Run(handle) == ENT_SYS_RUN_UNINITIALIZED,
                   "ENT_Run should reject a handle cleared by ENT_Close") != 0)
    {
        return 1;
    }

    if(expect_true(s_cv_close_calls == 1, "ENT_Close should close the condition variable exactly once") != 0)
    {
        return 1;
    }

    if(expect_true(s_lock_close_calls == 1, "ENT_Close should close the lock exactly once") != 0)
    {
        return 1;
    }

    if(expect_true(s_log_close_handle_calls == 1, "ENT_Close should close the entity log handle exactly once") != 0)
    {
        return 1;
    }

    if(expect_true(s_log_close_calls == 0, "ENT_Close should not close log service when ENT_LogInit was never acquired") != 0)
    {
        return 1;
    }

    if(expect_true(s_first_closed_log_handle == (ENT_LOG)0x11 && s_second_closed_log_handle == NULL,
                   "ENT_Close should close the entity handle exactly once") != 0)
    {
        return 1;
    }

    return expect_true(s_last_closed_log_handle == (ENT_LOG)0x11,
                       "ENT_Close should close the entity log handle that was attached to the context");
}

static int test_ent_init_closes_logging_when_entity_log_level_setup_fails(void)
{
    ENT_HANDLE handle = NULL;
    reset_close_counters();
    reset_log_failures();
    s_fail_log_set_option_call = 1;

    if(expect_true(ENT_Init(&handle, "demo", "/tmp/demo", LOG_LEV_WARN_E, ENT_MODE_NORMAL_E) == ENT_INIT_ENTITY_LEVELFAIL,
                   "ENT_Init should report entity log level setup failure") != 0)
    {
        return 1;
    }

    if(expect_true(handle == NULL, "ENT_Init should leave the handle NULL on failure") != 0)
    {
        return 1;
    }

    if(expect_true(s_log_close_handle_calls == 1,
                   "ENT_Init should close the entity log handle when entity log setup fails") != 0)
    {
        return 1;
    }

    return expect_true(s_log_close_calls == 1,
                       "ENT_Init should close logging when entity log setup fails");
}

static int test_ent_init_logs_before_tearing_down_logging_when_lock_init_fails(void)
{
    ENT_HANDLE handle = NULL;
    reset_close_counters();
    reset_log_failures();
    s_fail_lock_init = -1;

    if(expect_true(ENT_Init(&handle, "demo", "/tmp/demo", LOG_LEV_WARN_E, ENT_MODE_NORMAL_E) == ENT_INIT_LOCK_INITFAIL,
                   "ENT_Init should report lock initialization failure") != 0)
    {
        return 1;
    }

    if(expect_true(handle == NULL, "ENT_Init should clear the handle on lock init failure") != 0)
    {
        return 1;
    }

    if(expect_true(s_log_error_calls == 1,
                   "ENT_Init should emit one internal error log before tearing logging down") != 0)
    {
        return 1;
    }

    if(expect_true(s_last_log_error_handle == s_ent_log_at_lock_init,
                   "ENT_Init should log with the entity handle while it is still valid") != 0)
    {
        return 1;
    }

    if(expect_true(s_ent_log_at_lock_init != NULL,
                   "ENT_Init should have an entity log handle before lock initialization runs") != 0)
    {
        return 1;
    }

    if(expect_true(s_log_error_order != 0 && s_log_close_order != 0 && s_log_error_order < s_log_close_order,
                   "ENT_Init should log the lock failure before closing the logging subsystem") != 0)
    {
        return 1;
    }

    if(expect_true(s_log_close_handle_calls == 1,
                   "ENT_Init should close the entity log handle after lock failure") != 0)
    {
        return 1;
    }

    return expect_true(s_log_close_calls == 1,
                       "ENT_Init should still close logging after logging the lock failure");
}

static int test_ent_init_builds_paths_without_trailing_separator(void)
{
    const char* expected_log_path = expected_log_path_for("/tmp/demo");
    ENT_HANDLE handle = NULL;

    reset_close_counters();
    reset_log_failures();

    if(expect_true(ENT_Init(&handle, "demo", "/tmp/demo", LOG_LEV_WARN_E, ENT_MODE_NORMAL_E) == ENT_SYS_NORMAL,
                   "ENT_Init should succeed for a normal work path") != 0)
    {
        return 1;
    }

    if(expect_true(handle != NULL, "ENT_Init should return a handle") != 0)
    {
        close_handle_if_needed(&handle);
        return 1;
    }

    if(expect_true(strcmp(handle->ctx.workPath, "/tmp/demo") == 0,
                   "ENT_Init should retain the original workPath") != 0)
    {
        close_handle_if_needed(&handle);
        return 1;
    }

    if(expect_true(strcmp(handle->ctx.entName, "demo") == 0,
                   "ENT_Init should retain the original entName") != 0)
    {
        close_handle_if_needed(&handle);
        return 1;
    }

    if(expect_true(strcmp(handle->ctx.logPath, expected_log_path) == 0,
                   "ENT_Init should append /log when workPath has no trailing separator") != 0)
    {
        close_handle_if_needed(&handle);
        return 1;
    }

    if(expect_true(strcmp(handle->ctx.logName, "ent_demo") == 0,
                   "ENT_Init should prefix the log name with ent_") != 0)
    {
        close_handle_if_needed(&handle);
        return 1;
    }

    if(expect_true(s_log_init_handle_calls == 1,
                   "ENT_Init should initialize the entity log handle") != 0)
    {
        close_handle_if_needed(&handle);
        return 1;
    }

    if(expect_true(strcmp(s_first_log_init_handle_module, "ent_demo") == 0,
                   "ENT_Init should initialize the entity logger with the prefixed log name") != 0)
    {
        close_handle_if_needed(&handle);
        return 1;
    }

    if(expect_true(strcmp(s_first_log_init_handle_path, expected_log_path) == 0,
                   "ENT_Init should initialize the entity logger with the computed log path") != 0)
    {
        close_handle_if_needed(&handle);
        return 1;
    }

    if(expect_true(s_log_set_option_calls == 1,
                   "ENT_Init should set log level for the entity logger") != 0)
    {
        close_handle_if_needed(&handle);
        return 1;
    }

    if(expect_true(s_last_log_set_option_handle == handle->ctx.entLog,
                   "ENT_Init should set log option on the entity handle") != 0)
    {
        close_handle_if_needed(&handle);
        return 1;
    }

    if(expect_true(s_last_log_set_option == ENT_LOG_LEVEL_E && s_last_log_set_level == LOG_LEV_WARN_E,
                   "ENT_Init should set ENT_LOG_LEVEL_E to requested log level") != 0)
    {
        close_handle_if_needed(&handle);
        return 1;
    }

    if(expect_true(handle->ctx.entLog == s_ent_log_at_lock_init,
                   "ENT_Init should retain the entity log handle inside the returned context") != 0)
    {
        close_handle_if_needed(&handle);
        return 1;
    }

    if(expect_true(ENT_Close(&handle) == ENT_SYS_NORMAL && handle == NULL,
                   "ENT_Close should succeed after a successful ENT_Init") != 0)
    {
        return 1;
    }

    return expect_true(handle == NULL && s_log_close_handle_calls == 1,
                       "ENT_Close should close the entity log handle after a successful ENT_Init");
}

static int test_ent_init_builds_paths_with_trailing_separator(void)
{
    const char* expected_log_path = expected_log_path_for("/tmp/demo/");
    ENT_HANDLE handle = NULL;

    reset_close_counters();
    reset_log_failures();

    if(expect_true(ENT_Init(&handle, "demo", "/tmp/demo/", LOG_LEV_WARN_E, ENT_MODE_NORMAL_E) == ENT_SYS_NORMAL,
                   "ENT_Init should succeed for a work path that already ends with a separator") != 0)
    {
        return 1;
    }

    if(expect_true(handle != NULL, "ENT_Init should return a handle") != 0)
    {
        close_handle_if_needed(&handle);
        return 1;
    }

    if(expect_true(strcmp(handle->ctx.logPath, expected_log_path) == 0,
                   "ENT_Init should not duplicate the separator before log") != 0)
    {
        close_handle_if_needed(&handle);
        return 1;
    }

    if(expect_true(strcmp(s_first_log_init_handle_path, expected_log_path) == 0,
                   "The entity logger should receive the normalized log path") != 0)
    {
        close_handle_if_needed(&handle);
        return 1;
    }

    if(expect_true(s_log_init_handle_calls == 1,
                   "ENT_Init should initialize the entity logger once") != 0)
    {
        close_handle_if_needed(&handle);
        return 1;
    }

    return expect_true(ENT_Close(&handle) == ENT_SYS_NORMAL && handle == NULL,
                       "ENT_Close should succeed after trailing-separator initialization");
}

static int test_ent_init_rejects_empty_name_or_work_path(void)
{
    MSG_ID_T sts = 0;
    ENT_HANDLE handle = NULL;

    reset_close_counters();
    reset_log_failures();

    sts = ENT_Init(&handle, "", "/tmp/demo", LOG_LEV_WARN_E, ENT_MODE_NORMAL_E);
    if(expect_true(sts == ENT_INIT_INVALID_ARGUMENT,
                   "ENT_Init should reject an empty entity name") != 0)
    {
        return 1;
    }

    if(expect_true(handle == NULL, "ENT_Init should keep the handle NULL after rejecting an empty entity name") != 0)
    {
        return 1;
    }

    sts = ENT_Init(&handle, "demo", "", LOG_LEV_WARN_E, ENT_MODE_NORMAL_E);
    if(expect_true(sts == ENT_INIT_INVALID_ARGUMENT,
                   "ENT_Init should reject an empty workPath") != 0)
    {
        return 1;
    }

    if(expect_true(handle == NULL, "ENT_Init should keep the handle NULL after rejecting an empty workPath") != 0)
    {
        return 1;
    }

    return expect_true(s_log_init_handle_calls == 0,
                       "ENT_Init should fail before touching logging for empty input");
}

static int test_ent_init_rejects_double_init_same_handle(void)
{
    ENT_HANDLE handle = NULL;
    MSG_ID_T sts = ENT_SYS_NORMAL;

    reset_close_counters();
    reset_log_failures();

    sts = ENT_Init(&handle, "demo", "/tmp/demo", LOG_LEV_WARN_E, ENT_MODE_NORMAL_E);
    if(expect_true(sts == ENT_SYS_NORMAL,
                   "ENT_Init should succeed before checking double-init rejection") != 0)
    {
        return 1;
    }

    if(expect_true(handle != NULL, "ENT_Init should return a live handle") != 0)
    {
        close_handle_if_needed(&handle);
        return 1;
    }

    sts = ENT_Init(&handle, "demo2", "/tmp/demo2", LOG_LEV_WARN_E, ENT_MODE_NORMAL_E);
    if(expect_true(sts == ENT_SYS_ALREADY_INITIALIZED,
                   "ENT_Init should reject reinitializing the same handle pointer") != 0)
    {
        close_handle_if_needed(&handle);
        return 1;
    }

    if(expect_true(handle != NULL, "ENT_Init should not overwrite an already initialized handle") != 0)
    {
        close_handle_if_needed(&handle);
        return 1;
    }

    return expect_true(ENT_Close(&handle) == ENT_SYS_NORMAL && handle == NULL,
                       "ENT_Close should still succeed after rejecting a double init");
}

static int test_ent_init_realtime_mode_can_degrade_to_normal(void)
{
    ENT_HANDLE handle = NULL;
    reset_close_counters();
    reset_log_failures();

#ifndef WIN32
    s_mlockall_result = -1;
    s_mlockall_errno = EPERM;
#endif

    if(expect_true(ENT_Init(&handle, "demo", "/tmp/demo", LOG_LEV_WARN_E, ENT_MODE_REALTIME_E) == ENT_SYS_NORMAL,
                   "ENT_Init should still succeed when realtime mode degrades to normal mode") != 0)
    {
        return 1;
    }

    if(expect_true(handle != NULL && handle->ctx.rtRequested == true,
                   "ENT_Init should persist that realtime mode was requested") != 0)
    {
        close_handle_if_needed(&handle);
        return 1;
    }

    if(expect_true(handle->ctx.rtEnabled == false,
                   "ENT_Init should report degraded normal mode when realtime is not applied") != 0)
    {
        close_handle_if_needed(&handle);
        return 1;
    }

#ifndef WIN32
    if(expect_true(handle->ctx.rtLastError == EPERM, "ENT_Init should preserve the mlockall errno when realtime degrades") != 0)
    {
        close_handle_if_needed(&handle);
        return 1;
    }

    if(expect_true(s_mlockall_calls == 1, "ENT_Init should attempt mlockall once for realtime mode") != 0)
    {
        close_handle_if_needed(&handle);
        return 1;
    }
#endif

    return expect_true(ENT_Close(&handle) == ENT_SYS_NORMAL && handle == NULL,
                       "ENT_Close should succeed after realtime degrade initialization");
}

static int test_ent_set_rt_attributes_rejects_uninitialized_context(void)
{
    return expect_true(ENT_SetRtAttributes(NULL, -1, ENT_RT_POLICY_OTHER_E, 0) == ENT_RT_NOT_INITIALIZED,
                       "ENT_SetRtAttributes should reject an uninitialized context");
}

static int test_ent_set_rt_attributes_allows_noop_after_init(void)
{
    ENT_HANDLE handle = NULL;
    reset_close_counters();
    reset_log_failures();

    if(expect_true(ENT_Init(&handle, "demo", "/tmp/demo", LOG_LEV_WARN_E, ENT_MODE_REALTIME_E) == ENT_SYS_NORMAL,
                   "ENT_Init should succeed before applying RT attributes") != 0)
    {
        return 1;
    }

    if(expect_true(ENT_SetRtAttributes(handle, -1, ENT_RT_POLICY_OTHER_E, 0) == ENT_SYS_NORMAL,
                   "ENT_SetRtAttributes should allow noop configuration after init") != 0)
    {
        close_handle_if_needed(&handle);
        return 1;
    }

    return expect_true(ENT_Close(&handle) == ENT_SYS_NORMAL && handle == NULL,
                       "ENT_Close should succeed after ENT_SetRtAttributes noop");
}

static int test_ent_set_rt_attributes_rejects_normal_mode(void)
{
    ENT_HANDLE handle = NULL;
    reset_close_counters();
    reset_log_failures();

    if(expect_true(ENT_Init(&handle, "demo", "/tmp/demo", LOG_LEV_WARN_E, ENT_MODE_NORMAL_E) == ENT_SYS_NORMAL,
                   "ENT_Init should succeed in normal mode before checking rt attribute rejection") != 0)
    {
        return 1;
    }

    if(expect_true(ENT_SetRtAttributes(handle, 0, ENT_RT_POLICY_FIFO_E, 1) == ENT_RT_NOTRT,
                   "ENT_SetRtAttributes should reject realtime attributes when init mode is normal") != 0)
    {
        close_handle_if_needed(&handle);
        return 1;
    }

    if(expect_true(handle->ctx.rtCpu == -1 && handle->ctx.rtPolicy == ENT_RT_POLICY_OTHER_E && handle->ctx.rtPriority == 0,
                   "ENT_SetRtAttributes should leave rt settings unchanged when mode is normal") != 0)
    {
        close_handle_if_needed(&handle);
        return 1;
    }

    return expect_true(ENT_Close(&handle) == ENT_SYS_NORMAL && handle == NULL,
                       "ENT_Close should succeed after normal-mode rt rejection");
}

static int test_handle_instance_uses_isolated_context(void)
{
    ENT_HANDLE handle = NULL;
    MSG_ID_T sts = ENT_SYS_NORMAL;

    memset(&gEntCtx, 0, sizeof(gEntCtx));
    reset_close_counters();
    reset_log_failures();

    sts = ENT_Init(&handle, "demo", "/tmp/demo", LOG_LEV_WARN_E, ENT_MODE_NORMAL_E);
    if(expect_true(sts == ENT_SYS_NORMAL,
                   "ENT_Init should initialize an isolated handle context") != 0)
    {
        return 1;
    }

    if(expect_true(handle != NULL, "ENT_Init should return a handle") != 0)
    {
        ENT_Close(&handle);
        return 1;
    }

    if(expect_true(gEntCtx.isInit == false,
                   "ENT_Init should not initialize the global context") != 0)
    {
        ENT_Close(&handle);
        return 1;
    }

    if(expect_true(ENT_SetRtAttributes(handle, 0, ENT_RT_POLICY_FIFO_E, 1) == ENT_RT_NOTRT,
                   "ENT_SetRtAttributes should reuse handle-specific initialization state") != 0)
    {
        ENT_Close(&handle);
        return 1;
    }

    if(expect_true(ENT_Close(&handle) == ENT_SYS_NORMAL,
                   "ENT_Close should clean up an isolated handle context") != 0)
    {
        return 1;
    }

    return expect_true(gEntCtx.isInit == false,
                       "ENT_Close should leave the global context untouched");
}

static int test_handle_instances_can_run_and_close_independently(void)
{
    ENT_HANDLE handleA = NULL;
    ENT_HANDLE handleB = NULL;
    UTL_CV handleACv = NULL;
    UTL_CV handleBCv = NULL;
    UTL_LOCK handleALock = NULL;
    UTL_LOCK handleBLock = NULL;
    int failed = 0;

    memset(&gEntCtx, 0, sizeof(gEntCtx));
    reset_close_counters();
    reset_log_failures();
    reset_wait_capture();

    if(expect_true(ENT_Init(&handleA, "demo_a", "/tmp/demo_a", LOG_LEV_WARN_E, ENT_MODE_NORMAL_E) == ENT_SYS_NORMAL,
                   "ENT_Init should initialize handle A") != 0)
    {
        return 1;
    }

    if(expect_true(ENT_Init(&handleB, "demo_b", "/tmp/demo_b", LOG_LEV_WARN_E, ENT_MODE_NORMAL_E) == ENT_SYS_NORMAL,
                   "ENT_Init should initialize handle B") != 0)
    {
        failed = 1;
        goto CLEANUP;
    }

    if(expect_true(s_log_init_calls == 1,
                   "Multiple handle instances should share one logging service initialization") != 0)
    {
        failed = 1;
        goto CLEANUP;
    }

    if(expect_true(gEntCtx.isInit == false,
                   "Initializing isolated handles should not initialize the global context") != 0)
    {
        failed = 1;
        goto CLEANUP;
    }

    enable_auto_stop_after_wait();
    if(expect_true(ENT_Run(handleA) == ENT_SYS_NORMAL,
                   "ENT_Run should return NORMAL for handle A after one wait and stop wake-up") != 0)
    {
        failed = 1;
        goto CLEANUP;
    }
    handleACv = s_last_cv;
    handleALock = s_last_lock;

    if(expect_true(handleACv != NULL && handleALock != NULL,
                   "Handle A should use non-null lock/CV handles") != 0)
    {
        failed = 1;
        goto CLEANUP;
    }

    enable_auto_stop_after_wait();
    if(expect_true(ENT_Run(handleB) == ENT_SYS_NORMAL,
                   "ENT_Run should return NORMAL for handle B after one wait and stop wake-up") != 0)
    {
        failed = 1;
        goto CLEANUP;
    }
    handleBCv = s_last_cv;
    handleBLock = s_last_lock;

    if(expect_true(handleBCv != NULL && handleBLock != NULL,
                   "Handle B should use non-null lock/CV handles") != 0)
    {
        failed = 1;
        goto CLEANUP;
    }

    if(expect_true(handleACv != handleBCv && handleALock != handleBLock,
                   "Distinct handle instances should use distinct lock/CV handles") != 0)
    {
        failed = 1;
        goto CLEANUP;
    }

    if(expect_true(ENT_Close(&handleA) == ENT_SYS_NORMAL,
                   "Closing handle A should succeed while handle B remains active") != 0)
    {
        failed = 1;
        goto CLEANUP;
    }
    handleA = NULL;

    if(expect_true(s_log_close_calls == 0,
                   "Closing one handle instance should not close the shared logging service") != 0)
    {
        failed = 1;
        goto CLEANUP;
    }

    if(expect_true(s_log_close_handle_calls == 1,
                   "Closing one handle instance should close only its own entity log handle") != 0)
    {
        failed = 1;
        goto CLEANUP;
    }

    if(expect_true(ENT_Close(&handleB) == ENT_SYS_NORMAL,
                   "Closing handle B should succeed") != 0)
    {
        failed = 1;
        goto CLEANUP;
    }
    handleB = NULL;

    if(expect_true(s_log_close_calls == 1,
                   "Closing the final handle instance should close the shared logging service exactly once") != 0)
    {
        failed = 1;
        goto CLEANUP;
    }

    if(expect_true(s_log_close_handle_calls == 2,
                   "Each handle instance should close exactly one entity log handle") != 0)
    {
        failed = 1;
        goto CLEANUP;
    }

    if(expect_true(gEntCtx.isInit == false,
                   "Closing isolated handles should leave the global context untouched") != 0)
    {
        failed = 1;
        goto CLEANUP;
    }

CLEANUP:
    if(handleA != NULL)
    {
        ENT_Close(&handleA);
    }
    if(handleB != NULL)
    {
        ENT_Close(&handleB);
    }

    return failed;
}

static int test_handle_second_instance_lock_init_failure_keeps_first_alive(void)
{
    ENT_HANDLE handleA = NULL;
    ENT_HANDLE handleB = NULL;
    UTL_CV handleACv = NULL;
    UTL_LOCK handleALock = NULL;
    int failed = 0;

    memset(&gEntCtx, 0, sizeof(gEntCtx));
    reset_close_counters();
    reset_log_failures();
    reset_wait_capture();

    if(expect_true(ENT_Init(&handleA, "demo_a", "/tmp/demo_a", LOG_LEV_WARN_E, ENT_MODE_NORMAL_E) == ENT_SYS_NORMAL,
                   "Handle A should initialize before testing handle B lock failure") != 0)
    {
        return 1;
    }

    s_fail_lock_init = -1;
    if(expect_true(ENT_Init(&handleB, "demo_b", "/tmp/demo_b", LOG_LEV_WARN_E, ENT_MODE_NORMAL_E) == ENT_INIT_LOCK_INITFAIL,
                   "Handle B should report lock initialization failure") != 0)
    {
        failed = 1;
        goto CLEANUP;
    }
    s_fail_lock_init = 0;

    if(expect_true(handleB == NULL,
                   "Handle B should remain NULL when initialization fails") != 0)
    {
        failed = 1;
        goto CLEANUP;
    }

    if(expect_true(s_log_init_calls == 1,
                   "Second handle failure should not reinitialize the shared logging service") != 0)
    {
        failed = 1;
        goto CLEANUP;
    }

    if(expect_true(s_log_close_calls == 0,
                   "Second handle failure should not close the shared logging service while handle A is alive") != 0)
    {
        failed = 1;
        goto CLEANUP;
    }

    if(expect_true(s_log_close_handle_calls == 1,
                   "Failed handle B init should close only its own entity log handle") != 0)
    {
        failed = 1;
        goto CLEANUP;
    }

    reset_wait_capture();
    enable_auto_stop_after_wait();
    if(expect_true(ENT_Run(handleA) == ENT_SYS_NORMAL,
                   "Handle A should return NORMAL after handle B lock-init failure stop wake-up") != 0)
    {
        failed = 1;
        goto CLEANUP;
    }
    handleACv = s_last_cv;
    handleALock = s_last_lock;

    if(expect_true(handleACv != NULL && handleALock != NULL,
                   "Handle A should still provide valid lock/CV handles after handle B failure") != 0)
    {
        failed = 1;
        goto CLEANUP;
    }

    if(expect_true(ENT_Close(&handleA) == ENT_SYS_NORMAL,
                   "Handle A should still close cleanly after handle B lock-init failure") != 0)
    {
        failed = 1;
        goto CLEANUP;
    }
    handleA = NULL;

    if(expect_true(s_log_close_calls == 1,
                   "Closing handle A after handle B failure should close shared logging exactly once") != 0)
    {
        failed = 1;
        goto CLEANUP;
    }

    if(expect_true(s_log_close_handle_calls == 2,
                   "Handle B failure plus handle A close should close two entity log handles total") != 0)
    {
        failed = 1;
        goto CLEANUP;
    }

CLEANUP:
    if(handleA != NULL)
    {
        ENT_Close(&handleA);
    }
    if(handleB != NULL)
    {
        ENT_Close(&handleB);
    }

    return failed;
}

static int test_handle_second_instance_log_option_failure_keeps_first_alive(void)
{
    ENT_HANDLE handleA = NULL;
    ENT_HANDLE handleB = NULL;
    int failed = 0;

    memset(&gEntCtx, 0, sizeof(gEntCtx));
    reset_close_counters();
    reset_log_failures();
    reset_wait_capture();

    if(expect_true(ENT_Init(&handleA, "demo_a", "/tmp/demo_a", LOG_LEV_WARN_E, ENT_MODE_NORMAL_E) == ENT_SYS_NORMAL,
                   "Handle A should initialize before testing handle B log-option failure") != 0)
    {
        return 1;
    }

    s_fail_log_set_option_call = 2;
    if(expect_true(ENT_Init(&handleB, "demo_b", "/tmp/demo_b", LOG_LEV_WARN_E, ENT_MODE_NORMAL_E) == ENT_INIT_ENTITY_LEVELFAIL,
                   "Handle B should report entity log level setup failure") != 0)
    {
        failed = 1;
        goto CLEANUP;
    }
    s_fail_log_set_option_call = 0;

    if(expect_true(handleB == NULL,
                   "Handle B should remain NULL when log option setup fails") != 0)
    {
        failed = 1;
        goto CLEANUP;
    }

    if(expect_true(s_log_init_calls == 1,
                   "Second handle log-option failure should not reinitialize shared logging") != 0)
    {
        failed = 1;
        goto CLEANUP;
    }

    if(expect_true(s_log_close_calls == 0,
                   "Second handle log-option failure should not close shared logging while handle A is alive") != 0)
    {
        failed = 1;
        goto CLEANUP;
    }

    if(expect_true(s_log_close_handle_calls == 1,
                   "Failed handle B log setup should close only its own entity log handle") != 0)
    {
        failed = 1;
        goto CLEANUP;
    }

    reset_wait_capture();
    enable_auto_stop_after_wait();
    if(expect_true(ENT_Run(handleA) == ENT_SYS_NORMAL,
                   "Handle A should return NORMAL after handle B log-option failure stop wake-up") != 0)
    {
        failed = 1;
        goto CLEANUP;
    }

    if(expect_true(ENT_Close(&handleA) == ENT_SYS_NORMAL,
                   "Handle A should still close cleanly after handle B log-option failure") != 0)
    {
        failed = 1;
        goto CLEANUP;
    }
    handleA = NULL;

    if(expect_true(s_log_close_calls == 1,
                   "Closing handle A after handle B log-option failure should close shared logging exactly once") != 0)
    {
        failed = 1;
        goto CLEANUP;
    }

    if(expect_true(s_log_close_handle_calls == 2,
                   "Handle B log-option failure plus handle A close should close two entity log handles total") != 0)
    {
        failed = 1;
        goto CLEANUP;
    }

CLEANUP:
    if(handleA != NULL)
    {
        ENT_Close(&handleA);
    }
    if(handleB != NULL)
    {
        ENT_Close(&handleB);
    }

    return failed;
}

int main(void)
{
    int failures = 0;

    failures += test_ent_run_rejects_uninitialized_context();
    failures += test_ent_run_waits_on_cv_with_lock();
    failures += test_ent_close_clears_handle_instances();
    failures += test_ent_close_stops_running_handle();
    failures += test_ent_init_closes_logging_when_entity_log_level_setup_fails();
    failures += test_ent_init_logs_before_tearing_down_logging_when_lock_init_fails();
    failures += test_ent_init_builds_paths_without_trailing_separator();
    failures += test_ent_init_builds_paths_with_trailing_separator();
    failures += test_ent_init_rejects_empty_name_or_work_path();
    failures += test_ent_init_rejects_double_init_same_handle();
    failures += test_ent_init_realtime_mode_can_degrade_to_normal();
    failures += test_ent_set_rt_attributes_rejects_uninitialized_context();
    failures += test_ent_set_rt_attributes_allows_noop_after_init();
    failures += test_ent_set_rt_attributes_rejects_normal_mode();
    failures += test_ent_stop_rejects_stale_handle();
    failures += test_ent_run_rejects_stale_handle();
    failures += test_ent_close_rejects_stale_handle();
    failures += test_ent_run_returns_stopped_when_stop_requested_before_entry();
    failures += test_ent_set_rt_attributes_rejects_stale_handle();
    failures += test_ent_close_waits_for_running_worker_before_free();
    failures += test_handle_instance_uses_isolated_context();
    failures += test_handle_instances_can_run_and_close_independently();
    failures += test_handle_second_instance_lock_init_failure_keeps_first_alive();
    failures += test_handle_second_instance_log_option_failure_keeps_first_alive();

    if(failures != 0)
    {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
