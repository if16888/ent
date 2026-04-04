#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ient_comm.h"
#include "ent_utility.h"

ENT_CTX gEntCtx;

static volatile int s_timer_hits = 0;

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

static void* timer_cb(void* data)
{
    int* hits = (int*)data;
    (*hits)++;
    s_timer_hits = *hits;
    return NULL;
}

typedef struct
{
    volatile int hits;
    volatile int in_callback;
    volatile int reentry_hits;
} TIMER_SLOW_PROBE;

static void* slow_timer_cb(void* data)
{
    TIMER_SLOW_PROBE* probe = (TIMER_SLOW_PROBE*)data;

    if(probe->in_callback)
    {
        probe->reentry_hits++;
    }

    probe->in_callback = 1;
    probe->hits++;
    UTL_Sleep(60);
    probe->in_callback = 0;
    return NULL;
}

static int test_periodic_timer_remains_stable_with_slow_callback(void)
{
    UTL_TIMER_T timer = NULL;
    TIMER_SLOW_PROBE probe;

    memset(&probe, 0, sizeof(probe));

    if(expect_true(UTL_TimerInit() == 0, "UTL_TimerInit should initialize for slow callback test") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_TimerCreate(&timer, UTL_TIMER_E_PERIOD, 20, slow_timer_cb, &probe) == 0,
                   "UTL_TimerCreate should create a periodic timer for slow callback test") != 0)
    {
        UTL_TimerClose();
        return 1;
    }

    UTL_Sleep(220);

    if(expect_true(probe.hits >= 2, "slow periodic timer should still fire") != 0)
    {
        UTL_TimerDelete(&timer);
        UTL_TimerClose();
        return 1;
    }

    if(expect_true(probe.hits <= 8, "slow periodic timer should not spin out of control") != 0)
    {
        UTL_TimerDelete(&timer);
        UTL_TimerClose();
        return 1;
    }

    if(expect_true(probe.reentry_hits == 0, "slow periodic timer should not reenter callback") != 0)
    {
        UTL_TimerDelete(&timer);
        UTL_TimerClose();
        return 1;
    }

    if(expect_true(UTL_TimerDelete(&timer) == 0, "UTL_TimerDelete should succeed after slow callback test") != 0)
    {
        UTL_TimerClose();
        return 1;
    }

    return expect_true(UTL_TimerClose() == 0, "UTL_TimerClose should succeed after slow callback test");
}

static int test_timer_rejects_uninitialized_use(void)
{
    UTL_TIMER_T timer = NULL;

    if(expect_true(UTL_TimerClose() == -1, "UTL_TimerClose should reject use before initialization") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_TimerCreate(&timer, UTL_TIMER_E_ONESHOT, 10, timer_cb, NULL) == -1,
                   "UTL_TimerCreate should reject use before initialization") != 0)
    {
        return 1;
    }

    return expect_true(UTL_TimerDelete(&timer) == -2,
                       "UTL_TimerDelete should reject use before initialization");
}

static int test_timer_init_and_close_are_idempotent(void)
{
    if(expect_true(UTL_TimerInit() == 0, "UTL_TimerInit should initialize the timer subsystem") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_TimerInit() == 0, "UTL_TimerInit should allow repeated initialization") != 0)
    {
        UTL_TimerClose();
        return 1;
    }

    return expect_true(UTL_TimerClose() == 0, "UTL_TimerClose should shut down the timer subsystem");
}

static int test_oneshot_timer_fires_once(void)
{
    UTL_TIMER_T timer = NULL;
    int hits = 0;

    if(expect_true(UTL_TimerInit() == 0, "UTL_TimerInit should initialize before creating a timer") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_TimerCreate(&timer, UTL_TIMER_E_ONESHOT, 20, timer_cb, &hits) == 0,
                   "UTL_TimerCreate should create a oneshot timer") != 0)
    {
        UTL_TimerClose();
        return 1;
    }

    UTL_Sleep(100);

    if(expect_true(hits == 1, "A oneshot timer should fire exactly once") != 0)
    {
        UTL_TimerClose();
        return 1;
    }

    return expect_true(UTL_TimerClose() == 0, "UTL_TimerClose should clean up a oneshot timer");
}

static int test_periodic_timer_fires_until_deleted(void)
{
    UTL_TIMER_T timer = NULL;
    int hits = 0;
    int beforeDelete = 0;

    if(expect_true(UTL_TimerInit() == 0, "UTL_TimerInit should initialize before creating a periodic timer") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_TimerCreate(&timer, UTL_TIMER_E_PERIOD, 20, timer_cb, &hits) == 0,
                   "UTL_TimerCreate should create a periodic timer") != 0)
    {
        UTL_TimerClose();
        return 1;
    }

    UTL_Sleep(90);
    beforeDelete = hits;

    if(expect_true(beforeDelete >= 2, "A periodic timer should fire multiple times before deletion") != 0)
    {
        UTL_TimerClose();
        return 1;
    }

    if(expect_true(UTL_TimerDelete(&timer) == 0, "UTL_TimerDelete should stop a periodic timer") != 0)
    {
        UTL_TimerClose();
        return 1;
    }

    UTL_Sleep(80);

    if(expect_true(hits == beforeDelete, "A deleted periodic timer should stop firing") != 0)
    {
        UTL_TimerClose();
        return 1;
    }

    return expect_true(UTL_TimerClose() == 0, "UTL_TimerClose should succeed after deleting a periodic timer");
}

static int test_timer_create_us_has_consistent_failure_contract(void)
{
    UTL_TIMER_T timer = (UTL_TIMER_T)0x1;
    int hits = 0;

    if(expect_true(UTL_TimerInit() == 0, "UTL_TimerInit should initialize before using UTL_TimerCreateUs") != 0)
    {
        return 1;
    }

#ifdef __linux__
    if(expect_true(UTL_TimerCreateUs(&timer, UTL_TIMER_E_ONESHOT, 1000, timer_cb, &hits) == 0,
                   "UTL_TimerCreateUs should create a Linux RT oneshot timer") != 0)
    {
        UTL_TimerClose();
        return 1;
    }

    UTL_Sleep(30);

    if(expect_true(hits == 1, "A Linux RT oneshot timer should fire exactly once") != 0)
    {
        UTL_TimerDelete(&timer);
        UTL_TimerClose();
        return 1;
    }

    if(expect_true(UTL_TimerDelete(&timer) == 0, "UTL_TimerDelete should delete a Linux RT oneshot timer") != 0)
    {
        UTL_TimerClose();
        return 1;
    }
#else
    if(expect_true(UTL_TimerCreateUs(&timer, UTL_TIMER_E_ONESHOT, 1000, timer_cb, &hits) == -1,
                   "UTL_TimerCreateUs should report unsupported platforms") != 0)
    {
        UTL_TimerClose();
        return 1;
    }

    if(expect_true(timer == NULL, "UTL_TimerCreateUs should clear the output timer on failure") != 0)
    {
        UTL_TimerClose();
        return 1;
    }
#endif

    return expect_true(UTL_TimerClose() == 0, "UTL_TimerClose should succeed after UTL_TimerCreateUs coverage");
}

int main(void)
{
    int failures = 0;

    failures += test_timer_rejects_uninitialized_use();
    failures += test_timer_init_and_close_are_idempotent();
    failures += test_oneshot_timer_fires_once();
    failures += test_periodic_timer_fires_until_deleted();
    failures += test_periodic_timer_remains_stable_with_slow_callback();
    failures += test_timer_create_us_has_consistent_failure_contract();

    if(failures != 0)
    {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
