#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ient_runtime.h"
#include "ent_log.h"
#include "ent_msg.h"
#include "ent_utility.h"

ENT_CTX gEntCtx;

typedef struct
{
    UTL_LOCK lock;
    UTL_CV   cv;
    int      started;
    int      completed;
    int      end_completed;
    int      expected;
    int      ret_sum;
    int      observed_ret_sum;
    int      slow_started;
} TPOOL_INTEGRATION_PROBE;

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
    (void)moduleName;
    (void)logPath;
    if(pLogHandle != NULL)
    {
        *pLogHandle = (ENT_LOG)0x1;
    }
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

static MSG_ID_T probe_init(TPOOL_INTEGRATION_PROBE* probe, int expected)
{
    memset(probe, 0, sizeof(*probe));
    probe->expected = expected;
    if(UTL_LockInit(&probe->lock, "tpool_integration_probe") != ENT_SYS_NORMAL)
    {
        return -1;
    }
    if(UTL_CVInit(&probe->cv, "tpool_integration_probe") != ENT_SYS_NORMAL)
    {
        UTL_LockClose(&probe->lock);
        return -1;
    }
    return ENT_SYS_NORMAL;
}

static void probe_close(TPOOL_INTEGRATION_PROBE* probe)
{
    if(probe->cv != NULL)
    {
        UTL_CVClose(&probe->cv);
    }
    if(probe->lock != NULL)
    {
        UTL_LockClose(&probe->lock);
    }
}

static int wait_for_completion(TPOOL_INTEGRATION_PROBE* probe, int expected, int timeoutMs)
{
    int remainingMs = timeoutMs;
    int done = 0;

    UTL_LockEnter(probe->lock);
    while(probe->end_completed < expected && remainingMs > 0)
    {
        MSG_ID_T sts = UTL_CVWait(probe->cv, probe->lock, 50, RW_WRITE_E);
        if(sts != ENT_SYS_NORMAL && sts != ENT_UTHD_WAIT_TIMEOUT)
        {
            break;
        }
        remainingMs -= 50;
    }
    done = (probe->end_completed >= expected);
    UTL_LockLeave(probe->lock);
    return done;
}

static int wait_for_slow_start(TPOOL_INTEGRATION_PROBE* probe, int timeoutMs)
{
    int remainingMs = timeoutMs;
    int started = 0;

    UTL_LockEnter(probe->lock);
    while(probe->slow_started == 0 && remainingMs > 0)
    {
        MSG_ID_T sts = UTL_CVWait(probe->cv, probe->lock, 50, RW_WRITE_E);
        if(sts != ENT_SYS_NORMAL && sts != ENT_UTHD_WAIT_TIMEOUT)
        {
            break;
        }
        remainingMs -= 50;
    }
    started = probe->slow_started;
    UTL_LockLeave(probe->lock);
    return started;
}

static MSG_ID_T counting_task_cb(void* data)
{
    TPOOL_INTEGRATION_PROBE* probe = (TPOOL_INTEGRATION_PROBE*)data;

    UTL_LockEnter(probe->lock);
    probe->started++;
    UTL_LockLeave(probe->lock);

    return 10;
}

static MSG_ID_T counting_task_end_cb(void* data, MSG_ID_T* retVal)
{
    TPOOL_INTEGRATION_PROBE* probe = (TPOOL_INTEGRATION_PROBE*)data;

    UTL_LockEnter(probe->lock);
    probe->completed++;
    probe->end_completed++;
    if(retVal != NULL)
    {
        probe->ret_sum += *retVal;
        probe->observed_ret_sum += *retVal;
    }
    UTL_CVWakeAll(probe->cv);
    UTL_LockLeave(probe->lock);

    return ENT_SYS_NORMAL;
}

static MSG_ID_T slow_task_cb(void* data)
{
    TPOOL_INTEGRATION_PROBE* probe = (TPOOL_INTEGRATION_PROBE*)data;

    UTL_LockEnter(probe->lock);
    probe->slow_started = 1;
    probe->started++;
    UTL_CVWakeAll(probe->cv);
    UTL_LockLeave(probe->lock);

    UTL_Sleep(150);

    UTL_LockEnter(probe->lock);
    probe->completed++;
    UTL_LockLeave(probe->lock);

    return 7;
}

static MSG_ID_T slow_task_end_cb(void* data, MSG_ID_T* retVal)
{
    TPOOL_INTEGRATION_PROBE* probe = (TPOOL_INTEGRATION_PROBE*)data;

    UTL_LockEnter(probe->lock);
    probe->end_completed++;
    if(retVal != NULL)
    {
        probe->ret_sum += *retVal;
    }
    UTL_CVWakeAll(probe->cv);
    UTL_LockLeave(probe->lock);

    return ENT_SYS_NORMAL;
}

static int test_real_tpool_executes_multiple_tasks(void)
{
    enum { TASK_COUNT = 8 };
    UTL_TPOOL pool = NULL;
    TPOOL_INTEGRATION_PROBE probe;
    MSG_ID_T retVals[TASK_COUNT];
    int failures = 0;

    if(expect_true(probe_init(&probe, TASK_COUNT) == ENT_SYS_NORMAL,
                   "probe_init should create lock and condition variable") != 0)
    {
        return 1;
    }

    for(int i = 0; i < TASK_COUNT; ++i)
    {
        retVals[i] = -1;
    }

    if(expect_true(UTL_TPoolInit(&pool, 2) == ENT_SYS_NORMAL,
                   "UTL_TPoolInit should create a real thread-backed pool") != 0)
    {
        probe_close(&probe);
        return 1;
    }

    for(int i = 0; i < TASK_COUNT; ++i)
    {
        if(expect_true(UTL_TPoolAddTask(pool,
                                        counting_task_cb,
                                        counting_task_end_cb,
                                        &probe,
                                        &retVals[i]) == ENT_SYS_NORMAL,
                       "UTL_TPoolAddTask should accept real-thread integration tasks") != 0)
        {
            failures++;
            break;
        }
    }

    if(failures == 0 && expect_true(wait_for_completion(&probe, TASK_COUNT, 3000) == 1,
                                    "real thread pool should complete all submitted tasks") != 0)
    {
        failures++;
    }

    if(failures == 0 && expect_true(probe.started == TASK_COUNT &&
                                    probe.completed == TASK_COUNT &&
                                    probe.end_completed == TASK_COUNT,
                                    "all callbacks and end callbacks should run exactly once") != 0)
    {
        failures++;
    }

    if(failures == 0 && expect_true(probe.ret_sum == TASK_COUNT * 10,
                                    "end callbacks should observe all task return values") != 0)
    {
        failures++;
    }

    if(expect_true(UTL_TPoolClose(pool) == ENT_SYS_NORMAL,
                   "UTL_TPoolClose should close a real thread-backed pool") != 0)
    {
        failures++;
    }

    probe_close(&probe);
    return failures == 0 ? 0 : 1;
}

static int test_real_tpool_close_waits_for_running_task(void)
{
    UTL_TPOOL pool = NULL;
    TPOOL_INTEGRATION_PROBE probe;
    MSG_ID_T retVal = -1;
    int failures = 0;

    if(expect_true(probe_init(&probe, 1) == ENT_SYS_NORMAL,
                   "probe_init should create lock and condition variable for slow task") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_TPoolInit(&pool, 1) == ENT_SYS_NORMAL,
                   "UTL_TPoolInit should create a one-worker real pool") != 0)
    {
        probe_close(&probe);
        return 1;
    }

    if(expect_true(UTL_TPoolAddTask(pool, slow_task_cb, slow_task_end_cb, &probe, &retVal) == ENT_SYS_NORMAL,
                   "UTL_TPoolAddTask should accept a running close test task") != 0)
    {
        UTL_TPoolClose(pool);
        probe_close(&probe);
        return 1;
    }

    if(expect_true(wait_for_slow_start(&probe, 3000) == 1,
                   "slow task should start before close is called") != 0)
    {
        UTL_TPoolClose(pool);
        probe_close(&probe);
        return 1;
    }

    if(expect_true(UTL_TPoolClose(pool) == ENT_SYS_NORMAL,
                   "UTL_TPoolClose should wait for the running real task") != 0)
    {
        failures++;
    }

    if(failures == 0 && expect_true(probe.completed == 1 && probe.end_completed == 1 && retVal == 7,
                                    "running task and end callback should complete before close returns") != 0)
    {
        failures++;
    }

    probe_close(&probe);
    return failures == 0 ? 0 : 1;
}

static int test_real_tpool_rejects_add_after_close(void)
{
    UTL_TPOOL pool = NULL;
    TPOOL_INTEGRATION_PROBE probe;
    MSG_ID_T retVal = -1;
    MSG_ID_T sts = ENT_SYS_NORMAL;

    if(expect_true(probe_init(&probe, 0) == ENT_SYS_NORMAL,
                   "probe_init should create lock and condition variable for post-close add") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_TPoolInit(&pool, 1) == ENT_SYS_NORMAL,
                   "UTL_TPoolInit should create a pool for post-close add rejection") != 0)
    {
        probe_close(&probe);
        return 1;
    }

    if(expect_true(UTL_TPoolClose(pool) == ENT_SYS_NORMAL,
                   "UTL_TPoolClose should close the post-close add pool") != 0)
    {
        probe_close(&probe);
        return 1;
    }

    sts = UTL_TPoolAddTask(pool, counting_task_cb, counting_task_end_cb, &probe, &retVal);
    probe_close(&probe);
    return expect_true(sts != ENT_SYS_NORMAL,
                       "UTL_TPoolAddTask should reject a pool handle after close without touching freed memory");
}

int main(void)
{
    int failures = 0;

    failures += test_real_tpool_executes_multiple_tasks();
    failures += test_real_tpool_close_waits_for_running_task();
    failures += test_real_tpool_rejects_add_after_close();

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
