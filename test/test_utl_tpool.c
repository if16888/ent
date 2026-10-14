#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

#include "ient_comm.h"
#include "ient_runtime.h"
#include "ent_msg.h"
#include "ent_thread.h"
#include "ent_utility.h"

ENT_CTX gEntCtx;

static int s_fail_thread_create = 0;
static int s_fail_thread_create_on_call = 0;
static int s_thread_create_calls = 0;
static int s_thread_close_calls = 0;
static int s_thread_wait_calls = 0;
static int s_use_real_threads = 0;

#ifdef ENT_TPOOL_TEST_HOOKS
extern void UTL_TPoolTestSetThreadListInsertFailure(BOOL enable);
extern void UTL_TPoolTestSetWorkerWaitFailure(UTL_TPOOL pool, BOOL enable);
extern void UTL_TPoolTestSetTaskRemoveFailure(UTL_TPOOL pool, BOOL enable);
extern BOOL UTL_TPoolTestWorkerFailed(UTL_TPOOL pool);
#endif

typedef struct
{
#ifdef _WIN32
    HANDLE thread;
#else
    pthread_t thread;
#endif
    int joined;
} TEST_THREAD_ID;

#ifdef _WIN32
typedef struct
{
    PTHREAD_START_ROUTINE thProc;
    void* thData;
} TEST_THREAD_START_DATA;

static DWORD WINAPI test_thread_start(LPVOID data)
{
    TEST_THREAD_START_DATA* startData = (TEST_THREAD_START_DATA*)data;
    PTHREAD_START_ROUTINE thProc = startData->thProc;
    void* thData = startData->thData;
    free(startData);
    (void)thProc(thData);
    return 0;
}
#endif

typedef struct
{
    volatile int task_hits;
    volatile int end_hits;
    volatile int task_started;
    MSG_ID_T observed_ret;
} TPOOL_TEST_PROBE;

static void reset_thread_counters(void)
{
    s_fail_thread_create = 0;
    s_fail_thread_create_on_call = 0;
    s_thread_create_calls = 0;
    s_thread_close_calls = 0;
    s_thread_wait_calls = 0;
    s_use_real_threads = 0;
#ifdef ENT_TPOOL_TEST_HOOKS
    UTL_TPoolTestSetThreadListInsertFailure(FALSE);
#endif
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

MSG_ID_T ENT_ThreadInit(ENT_THREAD* pthHandle)
{
    if(pthHandle == NULL)
    {
        return -1;
    }

    *pthHandle = malloc(1);
    if(*pthHandle == NULL)
    {
        return -2;
    }

    return 0;
}

MSG_ID_T ENT_ThreadDetachCreate(ENT_THREAD handle, PTHREAD_START_ROUTINE thProc, void* thData)
{
    (void)handle;
    (void)thProc;
    (void)thData;
    return 0;
}

MSG_ID_T ENT_ThreadCreate(ENT_THREAD_ID* tid, ENT_THREAD handle, PTHREAD_START_ROUTINE thProc, void* thData)
{
    TEST_THREAD_ID* threadId = NULL;
    (void)handle;
    s_thread_create_calls++;
    if(tid != NULL)
    {
        *tid = NULL;
    }

    if(s_fail_thread_create)
    {
        return -5;
    }

    if(s_fail_thread_create_on_call != 0 && s_thread_create_calls == s_fail_thread_create_on_call)
    {
        return -5;
    }

    if(tid != NULL)
    {
        if(!s_use_real_threads)
        {
            *tid = malloc(1);
            if(*tid == NULL)
            {
                return -6;
            }
            return 0;
        }

        threadId = (TEST_THREAD_ID*)calloc(1, sizeof(TEST_THREAD_ID));
        if(threadId == NULL)
        {
            return -6;
        }

#ifdef _WIN32
        {
            TEST_THREAD_START_DATA* startData =
                (TEST_THREAD_START_DATA*)calloc(1, sizeof(TEST_THREAD_START_DATA));
            if(startData == NULL)
            {
                free(threadId);
                return -6;
            }
            startData->thProc = thProc;
            startData->thData = thData;
            threadId->thread = CreateThread(NULL, 0, test_thread_start, startData, 0, NULL);
            if(threadId->thread == NULL)
            {
                free(startData);
                free(threadId);
                return -7;
            }
        }
#else
        if(pthread_create(&threadId->thread, NULL, thProc, thData) != 0)
        {
            free(threadId);
            return -7;
        }
#endif

        *tid = (ENT_THREAD_ID)threadId;
    }
    return 0;
}

MSG_ID_T ENT_ThreadWaitById(ENT_THREAD_ID* tid, ENT_THREAD handle, int ms)
{
    TEST_THREAD_ID* threadId = NULL;
    (void)handle;
    (void)ms;
    s_thread_wait_calls++;
    if(tid != NULL && *tid != NULL)
    {
        if(!s_use_real_threads)
        {
            free(*tid);
        }
        else
        {
            threadId = (TEST_THREAD_ID*)(*tid);
            if(!threadId->joined)
            {
#ifdef _WIN32
                WaitForSingleObject(threadId->thread, INFINITE);
                CloseHandle(threadId->thread);
#else
                pthread_join(threadId->thread, NULL);
#endif
                threadId->joined = 1;
            }
            free(threadId);
        }
        *tid = NULL;
    }
    return 0;
}

MSG_ID_T ENT_ThreadClose(ENT_THREAD handle)
{
    s_thread_close_calls++;
    free(handle);
    return 0;
}

MSG_ID_T UTL_Sleep(int ms)
{
    if(ms > 0)
    {
#ifdef _WIN32
        Sleep((DWORD)ms);
#else
        usleep((useconds_t)ms * 1000U);
#endif
    }
    return 0;
}

static MSG_ID_T test_task_cb(void* data)
{
    TPOOL_TEST_PROBE* probe = (TPOOL_TEST_PROBE*)data;
    probe->task_started = 1;
    probe->task_hits++;
    return 42;
}

static MSG_ID_T test_task_end_cb(void* data, MSG_ID_T* retVal)
{
    TPOOL_TEST_PROBE* probe = (TPOOL_TEST_PROBE*)data;
    probe->end_hits++;
    if(retVal != NULL)
    {
        probe->observed_ret = *retVal;
    }
    return 0;
}

static MSG_ID_T slow_task_cb(void* data)
{
    TPOOL_TEST_PROBE* probe = (TPOOL_TEST_PROBE*)data;
    probe->task_started = 1;
    UTL_Sleep(100);
    probe->task_hits++;
    return 7;
}

static int test_tpool_init_rejects_null_output_pointer(void)
{
    if(expect_true(UTL_TPoolInit(NULL, 1) == ENT_TPL_BAD_ARGUMENT,
                   "UTL_TPoolInit should reject a NULL output pointer") != 0)
    {
        return 1;
    }
    return expect_true(UTL_TPoolInitEx(NULL, 1, 1) == ENT_TPL_BAD_ARGUMENT,
                       "UTL_TPoolInitEx should reject a NULL output pointer");
}

static int test_tpool_init_ex_rejects_invalid_capacity(void)
{
    UTL_TPOOL pool = (UTL_TPOOL)0x1;

    if(expect_true(UTL_TPoolInitEx(&pool, 1, 0) == ENT_TPL_BAD_ARGUMENT,
                   "UTL_TPoolInitEx should reject a non-positive pending-task limit") != 0)
    {
        return 1;
    }
    return expect_true(pool == NULL,
                       "UTL_TPoolInitEx should clear the output handle for an invalid capacity");
}

static int test_tpool_add_task_rejects_invalid_arguments(void)
{
    MSG_ID_T retVal = 0;

    if(expect_true(UTL_TPoolAddTask(NULL, NULL, NULL, NULL, NULL) == ENT_TPL_BAD_ARGUMENT,
                   "UTL_TPoolAddTask should reject a NULL pool and callback") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_TPoolAddTask(NULL, (UTL_TP_TASK_F)1, NULL, NULL, &retVal) == ENT_TPL_BAD_ARGUMENT,
                   "UTL_TPoolAddTask should reject a NULL pool") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_TPoolAddTask((UTL_TPOOL)1, (UTL_TP_TASK_F)1, NULL, NULL, NULL) == ENT_TPL_BAD_ARGUMENT,
                   "UTL_TPoolAddTask should reject a NULL return-value pointer") != 0)
    {
        return 1;
    }

    return expect_true(UTL_TPoolAddTask((UTL_TPOOL)1, NULL, NULL, NULL, &retVal) == ENT_TPL_BAD_ARGUMENT,
                       "UTL_TPoolAddTask should reject a NULL task callback");
}

static int test_tpool_init_fails_when_no_worker_threads_start(void)
{
    UTL_TPOOL pool = NULL;

    reset_thread_counters();
    s_fail_thread_create = 1;

    if(expect_true(UTL_TPoolInit(&pool, 2) == ENT_TPL_WORKER_CREATEFAIL,
                   "UTL_TPoolInit should fail when no worker threads can be created") != 0)
    {
        s_fail_thread_create = 0;
        return 1;
    }

    s_fail_thread_create = 0;

    if(expect_true(pool == NULL, "UTL_TPoolInit should leave pool as NULL when startup fails") != 0)
    {
        return 1;
    }

    if(expect_true(s_thread_create_calls == 2, "UTL_TPoolInit should attempt to create each requested worker") != 0)
    {
        return 1;
    }

    return expect_true(s_thread_close_calls == 1,
                       "UTL_TPoolInit should close the thread context when worker startup fails");
}

static int test_tpool_init_cleans_up_when_worker_list_insert_fails(void)
{
    UTL_TPOOL pool = NULL;

    reset_thread_counters();
    s_use_real_threads = 1;
#ifdef ENT_TPOOL_TEST_HOOKS
    UTL_TPoolTestSetThreadListInsertFailure(TRUE);
#endif

    if(expect_true(UTL_TPoolInit(&pool, 1) == ENT_TPL_WORKER_CREATEFAIL,
                   "UTL_TPoolInit should fail when the started worker cannot be inserted into the thread list") != 0)
    {
#ifdef ENT_TPOOL_TEST_HOOKS
        UTL_TPoolTestSetThreadListInsertFailure(FALSE);
#endif
        s_use_real_threads = 0;
        return 1;
    }

    if(expect_true(pool == NULL, "UTL_TPoolInit should leave pool as NULL when thread list insertion fails") != 0)
    {
#ifdef ENT_TPOOL_TEST_HOOKS
        UTL_TPoolTestSetThreadListInsertFailure(FALSE);
#endif
        s_use_real_threads = 0;
        return 1;
    }

    if(expect_true(s_thread_create_calls == 1, "UTL_TPoolInit should create exactly one worker for insert-failure coverage") != 0)
    {
#ifdef ENT_TPOOL_TEST_HOOKS
        UTL_TPoolTestSetThreadListInsertFailure(FALSE);
#endif
        s_use_real_threads = 0;
        return 1;
    }

    if(expect_true(s_thread_wait_calls == 1, "UTL_TPoolInit should wait for the created worker before cleaning up") != 0)
    {
#ifdef ENT_TPOOL_TEST_HOOKS
        UTL_TPoolTestSetThreadListInsertFailure(FALSE);
#endif
        s_use_real_threads = 0;
        return 1;
    }

    if(expect_true(s_thread_close_calls == 1,
                   "UTL_TPoolInit should close the shared thread context after insert failure") != 0)
    {
#ifdef ENT_TPOOL_TEST_HOOKS
        UTL_TPoolTestSetThreadListInsertFailure(FALSE);
#endif
        s_use_real_threads = 0;
        return 1;
    }

#ifdef ENT_TPOOL_TEST_HOOKS
    UTL_TPoolTestSetThreadListInsertFailure(FALSE);
#endif
    s_use_real_threads = 0;
    return 0;
}

static int test_tpool_init_uses_default_worker_count_for_non_positive_input(void)
{
    UTL_TPOOL pool = NULL;

    reset_thread_counters();

    if(expect_true(UTL_TPoolInit(&pool, 0) == 0,
                   "UTL_TPoolInit should use the default worker count when num <= 0") != 0)
    {
        return 1;
    }

    if(expect_true(pool != NULL, "UTL_TPoolInit should return a pool when default workers start") != 0)
    {
        UTL_TPoolClose(&pool);
        return 1;
    }

    if(expect_true(s_thread_create_calls == 8,
                   "UTL_TPoolInit should attempt to create the default number of workers") != 0)
    {
        UTL_TPoolClose(&pool);
        return 1;
    }

    if(expect_true(UTL_TPoolClose(&pool) == 0, "UTL_TPoolClose should close a pool created with default workers") != 0)
    {
        return 1;
    }

    if(expect_true(s_thread_wait_calls == 8,
                   "UTL_TPoolClose should wait for each started default worker") != 0)
    {
        return 1;
    }

    return expect_true(s_thread_close_calls == 1,
                       "UTL_TPoolClose should close the shared thread context once");
}

static int test_tpool_close_reclaims_partially_started_workers(void)
{
    UTL_TPOOL pool = NULL;

    reset_thread_counters();
    s_fail_thread_create_on_call = 2;

    if(expect_true(UTL_TPoolInit(&pool, 3) == 0,
                   "UTL_TPoolInit should succeed when at least one worker starts") != 0)
    {
        s_fail_thread_create_on_call = 0;
        return 1;
    }

    s_fail_thread_create_on_call = 0;

    if(expect_true(pool != NULL, "UTL_TPoolInit should return a pool after partial startup success") != 0)
    {
        UTL_TPoolClose(&pool);
        return 1;
    }

    if(expect_true(s_thread_create_calls == 3,
                   "UTL_TPoolInit should still attempt each requested worker even after an intermediate failure") != 0)
    {
        UTL_TPoolClose(&pool);
        return 1;
    }

    if(expect_true(UTL_TPoolClose(&pool) == 0, "UTL_TPoolClose should close a partially started pool") != 0)
    {
        return 1;
    }

    if(expect_true(s_thread_wait_calls == 2,
                   "UTL_TPoolClose should wait only for the workers that actually started") != 0)
    {
        return 1;
    }

    return expect_true(s_thread_close_calls == 1,
                       "UTL_TPoolClose should close the shared thread context after partial startup");
}

static int test_tpool_close_rejects_null_pool(void)
{
    return expect_true(UTL_TPoolClose(NULL) == ENT_TPL_BAD_ARGUMENT,
                       "UTL_TPoolClose should reject a NULL pool handle");
}

static int test_tpool_executes_task_and_end_callback(void)
{
    UTL_TPOOL pool = NULL;
    TPOOL_TEST_PROBE probe;
    MSG_ID_T retVal = -1;

    memset(&probe, 0, sizeof(probe));
    reset_thread_counters();
    s_use_real_threads = 1;

    if(expect_true(UTL_TPoolInit(&pool, 1) == 0,
                   "UTL_TPoolInit should create a worker for task execution") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_TPoolAddTask(pool, test_task_cb, test_task_end_cb, &probe, &retVal) == 0,
                   "UTL_TPoolAddTask should accept a simple task") != 0)
    {
        UTL_TPoolClose(&pool);
        return 1;
    }

    if(expect_true(UTL_TPoolClose(&pool) == 0,
                   "UTL_TPoolClose should drain an accepted task and join its worker") != 0)
    {
        s_use_real_threads = 0;
        return 1;
    }

    s_use_real_threads = 0;
    return expect_true(probe.task_hits == 1 && probe.end_hits == 1 &&
                       retVal == 42 && probe.observed_ret == 42,
                       "task and end callbacks should complete exactly once before close returns");
}

static int test_tpool_submit_owns_result_storage(void)
{
    UTL_TPOOL pool = NULL;
    TPOOL_TEST_PROBE probe;
    memset(&probe, 0, sizeof(probe));
    reset_thread_counters();
    s_use_real_threads = 1;
    if(expect_true(UTL_TPoolInit(&pool, 1) == ENT_SYS_NORMAL,
                   "safe submit should create one worker") != 0)
    {
        s_use_real_threads = 0;
        return 1;
    }
    if(expect_true(UTL_TPoolSubmit(pool, test_task_cb, test_task_end_cb, &probe) == ENT_SYS_NORMAL,
                   "safe submit should not require caller result storage") != 0)
    {
        UTL_TPoolClose(&pool);
        s_use_real_threads = 0;
        return 1;
    }
    if(expect_true(UTL_TPoolClose(&pool) == ENT_SYS_NORMAL,
                   "safe submit should drain work") != 0)
    {
        s_use_real_threads = 0;
        return 1;
    }
    s_use_real_threads = 0;
    return expect_true(probe.task_hits == 1 && probe.end_hits == 1 && probe.observed_ret == 42,
                       "end callback should observe task-owned result");
}

static int test_tpool_close_waits_for_running_task_completion(void)
{
    UTL_TPOOL pool = NULL;
    TPOOL_TEST_PROBE probe;
    MSG_ID_T retVal = -1;

    memset(&probe, 0, sizeof(probe));
    reset_thread_counters();
    s_use_real_threads = 1;

    if(expect_true(UTL_TPoolInit(&pool, 1) == 0,
                   "UTL_TPoolInit should create a worker for running-task close checks") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_TPoolAddTask(pool, slow_task_cb, test_task_end_cb, &probe, &retVal) == 0,
                   "UTL_TPoolAddTask should accept the slow task for close checks") != 0)
    {
        UTL_TPoolClose(&pool);
        return 1;
    }

    UTL_Sleep(10);
    if(expect_true(UTL_TPoolClose(&pool) == 0,
                   "UTL_TPoolClose should wait for accepted slow work to finish without deadlock") != 0)
    {
        s_use_real_threads = 0;
        return 1;
    }

    s_use_real_threads = 0;
    return expect_true(probe.task_hits == 1 && probe.end_hits == 1 && retVal == 7,
                       "slow task should complete and report its return value before close finishes");
}

static int test_tpool_close_wakes_idle_workers(void)
{
    reset_thread_counters();
    s_use_real_threads = 1;

    for(int i = 0; i < 20; ++i)
    {
        UTL_TPOOL pool = NULL;

        if(expect_true(UTL_TPoolInit(&pool, 1) == 0,
                       "UTL_TPoolInit should create a worker for idle-close checks") != 0)
        {
            s_use_real_threads = 0;
            return 1;
        }

        UTL_Sleep(10);

        if(expect_true(UTL_TPoolClose(&pool) == 0,
                       "UTL_TPoolClose should wake and join an idle waiting worker without deadlock") != 0)
        {
            s_use_real_threads = 0;
            return 1;
        }
    }

    s_use_real_threads = 0;
    return expect_true(s_thread_wait_calls == 20,
                       "UTL_TPoolClose should wait for each idle worker exactly once");
}

static int test_tpool_reports_runtime_worker_failure(void)
{
#ifdef ENT_TPOOL_TEST_HOOKS
    UTL_TPOOL pool = NULL;
    TPOOL_TEST_PROBE probe;
    MSG_ID_T retVal = -1;
    MSG_ID_T addStatus = ENT_SYS_NORMAL;
    int attempts = 0;

    reset_thread_counters();
    memset(&probe, 0, sizeof(probe));
    s_use_real_threads = 1;
    if(expect_true(UTL_TPoolInit(&pool, 1) == ENT_SYS_NORMAL,
                   "UTL_TPoolInit should start a worker for runtime failure testing") != 0)
    {
        s_use_real_threads = 0;
        return 1;
    }
    UTL_TPoolTestSetWorkerWaitFailure(pool, TRUE);

    for(attempts = 0; attempts < 100 && !UTL_TPoolTestWorkerFailed(pool); ++attempts)
    {
        UTL_Sleep(1);
    }
    if(expect_true(UTL_TPoolTestWorkerFailed(pool),
                   "worker wait failure should transition the pool into failed state") != 0)
    {
        UTL_TPoolClose(&pool);
        s_use_real_threads = 0;
        return 1;
    }

    addStatus = UTL_TPoolAddTask(pool, test_task_cb, NULL, &probe, &retVal);
    if(expect_true(addStatus == ENT_TPL_WORKER_FAILED,
                   "task submission should report a failed worker instead of accepting stranded work") != 0)
    {
        UTL_TPoolClose(&pool);
        s_use_real_threads = 0;
        return 1;
    }
    if(expect_true(UTL_TPoolClose(&pool) == ENT_TPL_WORKER_FAILED,
                   "close should report the worker failure after reclaiming the pool") != 0 ||
       expect_true(pool == NULL,
                   "close should clear the pool handle even when it reports worker failure") != 0)
    {
        s_use_real_threads = 0;
        return 1;
    }

    s_use_real_threads = 0;
    return 0;
#else
    return 0;
#endif
}

static int test_tpool_close_completes_stranded_task_with_worker_error(void)
{
#ifdef ENT_TPOOL_TEST_HOOKS
    UTL_TPOOL pool = NULL;
    TPOOL_TEST_PROBE probe;
    MSG_ID_T retVal = -1;
    MSG_ID_T addStatus = ENT_SYS_NORMAL;
    MSG_ID_T closeStatus = ENT_SYS_NORMAL;
    int attempts = 0;

    reset_thread_counters();
    memset(&probe, 0, sizeof(probe));
    s_use_real_threads = 1;
    if(expect_true(UTL_TPoolInit(&pool, 1) == ENT_SYS_NORMAL,
                   "UTL_TPoolInit should create a worker for stranded-task failure testing") != 0)
    {
        s_use_real_threads = 0;
        return 1;
    }

    UTL_TPoolTestSetTaskRemoveFailure(pool, TRUE);
    addStatus = UTL_TPoolAddTask(pool, test_task_cb, test_task_end_cb, &probe, &retVal);
    if(expect_true(addStatus == ENT_SYS_NORMAL,
                   "pool should accept a task before the worker removal failure is triggered") != 0)
    {
        UTL_TPoolClose(&pool);
        s_use_real_threads = 0;
        return 1;
    }
    for(attempts = 0; attempts < 100 && !UTL_TPoolTestWorkerFailed(pool); ++attempts)
    {
        UTL_Sleep(1);
    }
    if(expect_true(UTL_TPoolTestWorkerFailed(pool),
                   "failed queue removal should transition the worker pool to failed state") != 0)
    {
        UTL_TPoolClose(&pool);
        s_use_real_threads = 0;
        return 1;
    }

    closeStatus = UTL_TPoolClose(&pool);
    s_use_real_threads = 0;
    return expect_true(closeStatus == ENT_TPL_WORKER_FAILED && pool == NULL &&
                       retVal == ENT_TPL_WORKER_FAILED && probe.task_hits == 0 &&
                       probe.end_hits == 1 && probe.observed_ret == ENT_TPL_WORKER_FAILED,
                       "close should report and complete a stranded task through its end callback");
#else
    return 0;
#endif
}

int main(void)
{
    int failures = 0;

    failures += test_tpool_init_rejects_null_output_pointer();
    failures += test_tpool_init_ex_rejects_invalid_capacity();
    failures += test_tpool_add_task_rejects_invalid_arguments();
    failures += test_tpool_init_fails_when_no_worker_threads_start();
    failures += test_tpool_init_cleans_up_when_worker_list_insert_fails();
    failures += test_tpool_init_uses_default_worker_count_for_non_positive_input();
    failures += test_tpool_close_reclaims_partially_started_workers();
    failures += test_tpool_close_rejects_null_pool();
    failures += test_tpool_executes_task_and_end_callback();
    failures += test_tpool_submit_owns_result_storage();
    failures += test_tpool_close_waits_for_running_task_completion();
    failures += test_tpool_close_wakes_idle_workers();
    failures += test_tpool_reports_runtime_worker_failure();
    failures += test_tpool_close_completes_stranded_task_with_worker_error();

    if(failures != 0)
    {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
