#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ient_comm.h"
#include "ent_thread.h"
#include "ent_utility.h"

ENT_CTX gEntCtx;

static int s_fail_thread_create = 0;
static int s_fail_thread_create_on_call = 0;
static int s_thread_create_calls = 0;
static int s_thread_close_calls = 0;
static int s_thread_wait_calls = 0;

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
    (void)handle;
    (void)thProc;
    (void)thData;
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
        *tid = malloc(1);
    }
    return 0;
}

MSG_ID_T ENT_ThreadWaitById(ENT_THREAD_ID* tid, ENT_THREAD handle, int ms)
{
    (void)handle;
    (void)ms;
    s_thread_wait_calls++;
    if(tid != NULL && *tid != NULL)
    {
        free(*tid);
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

static int test_tpool_init_rejects_null_output_pointer(void)
{
    return expect_true(UTL_TPoolInit(NULL, 1) == -1,
                       "UTL_TPoolInit should reject a NULL output pointer");
}

static int test_tpool_add_task_rejects_invalid_arguments(void)
{
    MSG_ID_T retVal = 0;

    if(expect_true(UTL_TPoolAddTask(NULL, NULL, NULL, NULL, NULL) == -1,
                   "UTL_TPoolAddTask should reject a NULL pool and callback") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_TPoolAddTask(NULL, (UTL_TP_TASK_F)1, NULL, NULL, &retVal) == -1,
                   "UTL_TPoolAddTask should reject a NULL pool") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_TPoolAddTask((UTL_TPOOL)1, (UTL_TP_TASK_F)1, NULL, NULL, NULL) == -1,
                   "UTL_TPoolAddTask should reject a NULL return-value pointer") != 0)
    {
        return 1;
    }

    return expect_true(UTL_TPoolAddTask((UTL_TPOOL)1, NULL, NULL, NULL, &retVal) == -1,
                       "UTL_TPoolAddTask should reject a NULL task callback");
}

static int test_tpool_init_fails_when_no_worker_threads_start(void)
{
    UTL_TPOOL pool = NULL;

    s_fail_thread_create = 1;
    s_fail_thread_create_on_call = 0;
    s_thread_create_calls = 0;
    s_thread_close_calls = 0;
    s_thread_wait_calls = 0;

    if(expect_true(UTL_TPoolInit(&pool, 2) == -6,
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

static int test_tpool_init_uses_default_worker_count_for_non_positive_input(void)
{
    UTL_TPOOL pool = NULL;

    s_fail_thread_create = 0;
    s_fail_thread_create_on_call = 0;
    s_thread_create_calls = 0;
    s_thread_close_calls = 0;
    s_thread_wait_calls = 0;

    if(expect_true(UTL_TPoolInit(&pool, 0) == 0,
                   "UTL_TPoolInit should use the default worker count when num <= 0") != 0)
    {
        return 1;
    }

    if(expect_true(pool != NULL, "UTL_TPoolInit should return a pool when default workers start") != 0)
    {
        UTL_TPoolClose(pool);
        return 1;
    }

    if(expect_true(s_thread_create_calls == 8,
                   "UTL_TPoolInit should attempt to create the default number of workers") != 0)
    {
        UTL_TPoolClose(pool);
        return 1;
    }

    if(expect_true(UTL_TPoolClose(pool) == 0, "UTL_TPoolClose should close a pool created with default workers") != 0)
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

    s_fail_thread_create = 0;
    s_fail_thread_create_on_call = 2;
    s_thread_create_calls = 0;
    s_thread_close_calls = 0;
    s_thread_wait_calls = 0;

    if(expect_true(UTL_TPoolInit(&pool, 3) == 0,
                   "UTL_TPoolInit should succeed when at least one worker starts") != 0)
    {
        s_fail_thread_create_on_call = 0;
        return 1;
    }

    s_fail_thread_create_on_call = 0;

    if(expect_true(pool != NULL, "UTL_TPoolInit should return a pool after partial startup success") != 0)
    {
        UTL_TPoolClose(pool);
        return 1;
    }

    if(expect_true(s_thread_create_calls == 3,
                   "UTL_TPoolInit should still attempt each requested worker even after an intermediate failure") != 0)
    {
        UTL_TPoolClose(pool);
        return 1;
    }

    if(expect_true(UTL_TPoolClose(pool) == 0, "UTL_TPoolClose should close a partially started pool") != 0)
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
    return expect_true(UTL_TPoolClose(NULL) == -1,
                       "UTL_TPoolClose should reject a NULL pool handle");
}

int main(void)
{
    int failures = 0;

    failures += test_tpool_init_rejects_null_output_pointer();
    failures += test_tpool_add_task_rejects_invalid_arguments();
    failures += test_tpool_init_fails_when_no_worker_threads_start();
    failures += test_tpool_init_uses_default_worker_count_for_non_positive_input();
    failures += test_tpool_close_reclaims_partially_started_workers();
    failures += test_tpool_close_rejects_null_pool();

    if(failures != 0)
    {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
