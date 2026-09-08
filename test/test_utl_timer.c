#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifndef _WIN32
#include <pthread.h>
#endif

#include "ient_comm.h"
#include "ient_runtime.h"
#include "ent_msg.h"
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

#ifdef __linux__
static long long monotonic_ns(void)
{
    struct timespec ts;

    if(clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    {
        return 0;
    }

    return (long long)ts.tv_sec * 1000000000LL + (long long)ts.tv_nsec;
}
#endif

typedef struct
{
    volatile int hits;
    volatile int in_callback;
    volatile int reentry_hits;
} TIMER_SLOW_PROBE;

typedef struct
{
    volatile int in_callback;
    volatile int callback_done;
    volatile MSG_ID_T create_status;
    volatile MSG_ID_T delete_status;
} TIMER_REENTRANT_PROBE;

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

static void* reentrant_timer_api_cb(void* data)
{
    TIMER_REENTRANT_PROBE* probe = (TIMER_REENTRANT_PROBE*)data;
    UTL_TIMER_T nested_timer = NULL;
    int nested_hits = 0;

    probe->in_callback = 1;
    UTL_Sleep(20);
    probe->create_status = UTL_TimerCreate(&nested_timer,
                                           UTL_TIMER_E_ONESHOT,
                                           1000,
                                           timer_cb,
                                           &nested_hits);
    if(probe->create_status == 0)
    {
        probe->delete_status = UTL_TimerDelete(&nested_timer);
    }
    probe->callback_done = 1;
    return NULL;
}

#ifndef _WIN32
typedef struct
{
    UTL_TIMER_T timer;
    volatile int hits;
    volatile int delete_status;
} TIMER_SELF_DELETE_PROBE;

static void* self_delete_timer_cb(void* data)
{
    TIMER_SELF_DELETE_PROBE* probe = (TIMER_SELF_DELETE_PROBE*)data;

    probe->hits++;
    probe->delete_status = UTL_TimerDelete(&probe->timer);
    return NULL;
}

typedef struct
{
    volatile MSG_ID_T close_status;
} TIMER_CLOSE_THREAD_PROBE;

static void* timer_close_thread(void* data)
{
    TIMER_CLOSE_THREAD_PROBE* probe = (TIMER_CLOSE_THREAD_PROBE*)data;

    UTL_Sleep(5);
    probe->close_status = UTL_TimerClose();
    return NULL;
}
#endif

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

static int test_timer_delete_allows_reentrant_callback_timer_api(void)
{
    UTL_TIMER_T timer = NULL;
    TIMER_REENTRANT_PROBE probe;
    int i = 0;

    memset(&probe, 0, sizeof(probe));
    probe.create_status = -999;
    probe.delete_status = -999;

    if(expect_true(UTL_TimerInit() == 0, "UTL_TimerInit should initialize for reentrant callback test") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_TimerCreate(&timer, UTL_TIMER_E_ONESHOT, 5, reentrant_timer_api_cb, &probe) == 0,
                   "UTL_TimerCreate should create a timer for reentrant callback test") != 0)
    {
        UTL_TimerClose();
        return 1;
    }

    for(i = 0; i < 100 && !probe.in_callback; ++i)
    {
        UTL_Sleep(1);
    }

    if(expect_true(probe.in_callback == 1, "reentrant callback should start before delete") != 0)
    {
        UTL_TimerClose();
        return 1;
    }

    if(expect_true(UTL_TimerDelete(&timer) == 0, "UTL_TimerDelete should wait for reentrant callback without deadlock") != 0)
    {
        UTL_TimerClose();
        return 1;
    }

    if(expect_true(probe.callback_done == 1, "reentrant callback should complete during delete") != 0)
    {
        UTL_TimerClose();
        return 1;
    }

    if(expect_true(probe.create_status == 0, "reentrant callback should be able to create a timer") != 0)
    {
        UTL_TimerClose();
        return 1;
    }

    if(expect_true(probe.delete_status == 0, "reentrant callback should be able to delete its nested timer") != 0)
    {
        UTL_TimerClose();
        return 1;
    }

    return expect_true(UTL_TimerClose() == 0, "UTL_TimerClose should succeed after reentrant callback test");
}

static int test_timer_rejects_uninitialized_use(void)
{
    UTL_TIMER_T timer = NULL;

    if(expect_true(UTL_TimerClose() == ENT_TMR_NOT_INITIALIZED, "UTL_TimerClose should reject use before initialization") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_TimerCreate(&timer, UTL_TIMER_E_ONESHOT, 10, timer_cb, NULL) == ENT_TMR_NOT_INITIALIZED,
                   "UTL_TimerCreate should reject use before initialization") != 0)
    {
        return 1;
    }

    return expect_true(UTL_TimerDelete(&timer) == ENT_TMR_NOT_INITIALIZED,
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
    int afterDelete = 0;

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
    afterDelete = hits;

    UTL_Sleep(80);

    if(expect_true(hits == afterDelete, "A deleted periodic timer should stop firing") != 0)
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
    if(expect_true(UTL_TimerCreateUs(&timer, UTL_TIMER_E_ONESHOT, 1000, timer_cb, &hits) == ENT_TMR_UNSUPPORTED,
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

static int test_timer_create_us_delete_does_not_wait_full_period(void)
{
#ifdef __linux__
    UTL_TIMER_T timer = NULL;
    long long before_delete_ns = 0;
    long long after_delete_ns = 0;
    long long elapsed_ms = 0;
    int hits = 0;

    if(expect_true(UTL_TimerInit() == 0, "UTL_TimerInit should initialize before RT delete latency test") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_TimerCreateUs(&timer, UTL_TIMER_E_ONESHOT, 200000, timer_cb, &hits) == 0,
                   "UTL_TimerCreateUs should create a long-period RT timer for delete latency test") != 0)
    {
        UTL_TimerClose();
        return 1;
    }

    before_delete_ns = monotonic_ns();
    if(expect_true(before_delete_ns != 0, "monotonic clock should be available for RT delete latency test") != 0)
    {
        UTL_TimerDelete(&timer);
        UTL_TimerClose();
        return 1;
    }

    if(expect_true(UTL_TimerDelete(&timer) == 0, "UTL_TimerDelete should succeed during RT delete latency test") != 0)
    {
        UTL_TimerClose();
        return 1;
    }

    after_delete_ns = monotonic_ns();
    if(expect_true(after_delete_ns != 0, "monotonic clock should still be available after RT delete") != 0)
    {
        UTL_TimerClose();
        return 1;
    }

    elapsed_ms = (after_delete_ns - before_delete_ns) / 1000000LL;
    if(expect_true(elapsed_ms < 80, "UTL_TimerDelete should not wait for the full RT period") != 0)
    {
        UTL_TimerClose();
        return 1;
    }

    return expect_true(UTL_TimerClose() == 0, "UTL_TimerClose should succeed after RT delete latency test");
#else
    return 0;
#endif
}

static int test_timer_create_us_periodic_timer_fires_on_linux(void)
{
#ifdef __linux__
    UTL_TIMER_T timer = NULL;
    int hits = 0;
    int before_delete = 0;

    if(expect_true(UTL_TimerInit() == 0, "UTL_TimerInit should initialize before Linux RT periodic timer test") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_TimerCreateUs(&timer, UTL_TIMER_E_PERIOD, 5000, timer_cb, &hits) == 0,
                   "UTL_TimerCreateUs should create a Linux RT periodic timer") != 0)
    {
        UTL_TimerClose();
        return 1;
    }

    UTL_Sleep(40);
    before_delete = hits;

    if(expect_true(before_delete >= 3, "A Linux RT periodic timer should fire multiple times") != 0)
    {
        UTL_TimerDelete(&timer);
        UTL_TimerClose();
        return 1;
    }

    if(expect_true(UTL_TimerDelete(&timer) == 0, "UTL_TimerDelete should stop a Linux RT periodic timer") != 0)
    {
        UTL_TimerClose();
        return 1;
    }

    UTL_Sleep(20);

    if(expect_true(hits == before_delete, "A deleted Linux RT periodic timer should stop firing") != 0)
    {
        UTL_TimerClose();
        return 1;
    }

    return expect_true(UTL_TimerClose() == 0, "UTL_TimerClose should succeed after Linux RT periodic timer test");
#else
    return 0;
#endif
}

static int test_timer_create_us_callback_can_self_delete_safely(void)
{
#ifdef __linux__
    TIMER_SELF_DELETE_PROBE probe;

    memset(&probe, 0, sizeof(probe));
    probe.delete_status = -999;

    if(expect_true(UTL_TimerInit() == 0, "UTL_TimerInit should initialize before RT self-delete callback test") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_TimerCreateUs(&probe.timer,
                                     UTL_TIMER_E_PERIOD,
                                     5000,
                                     self_delete_timer_cb,
                                     &probe) == 0,
                   "UTL_TimerCreateUs should create a Linux RT timer for self-delete callback test") != 0)
    {
        UTL_TimerClose();
        return 1;
    }

    UTL_Sleep(80);

    if(expect_true(probe.hits == 1, "RT self-delete callback timer should fire exactly once") != 0)
    {
        UTL_TimerClose();
        return 1;
    }

    if(expect_true(probe.delete_status == 0, "RT self-delete callback should be able to delete its own timer") != 0)
    {
        UTL_TimerClose();
        return 1;
    }

    if(expect_true(probe.timer == NULL, "RT self-delete callback should clear the timer handle") != 0)
    {
        UTL_TimerClose();
        return 1;
    }

    return expect_true(UTL_TimerClose() == 0,
                       "UTL_TimerClose should reclaim a self-deleted Linux RT timer");
#else
    return 0;
#endif
}

#ifndef _WIN32
static int test_timer_callback_can_self_delete_safely(void)
{
    TIMER_SELF_DELETE_PROBE probe;

    memset(&probe, 0, sizeof(probe));
    probe.delete_status = -999;

    if(expect_true(UTL_TimerInit() == 0, "UTL_TimerInit should initialize before self-delete callback test") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_TimerCreate(&probe.timer, UTL_TIMER_E_PERIOD, 10, self_delete_timer_cb, &probe) == 0,
                   "UTL_TimerCreate should create a timer for self-delete callback test") != 0)
    {
        UTL_TimerClose();
        return 1;
    }

    UTL_Sleep(80);

    if(expect_true(probe.hits == 1, "self-delete callback timer should fire exactly once") != 0)
    {
        UTL_TimerClose();
        return 1;
    }

    if(expect_true(probe.delete_status == 0, "self-delete callback should be able to delete its own timer") != 0)
    {
        UTL_TimerClose();
        return 1;
    }

    if(expect_true(probe.timer == NULL, "self-delete callback should clear the timer handle") != 0)
    {
        UTL_TimerClose();
        return 1;
    }

    return expect_true(UTL_TimerClose() == 0, "UTL_TimerClose should succeed after self-delete callback test");
}

static int test_timer_create_is_rejected_while_close_progresses(void)
{
    UTL_TIMER_T timer = NULL;
    UTL_TIMER_T rejected_timer = NULL;
    TIMER_SLOW_PROBE slow_probe;
    TIMER_CLOSE_THREAD_PROBE close_probe;
    pthread_t closer;
    int observed_rejection = 0;
    int i = 0;

    memset(&slow_probe, 0, sizeof(slow_probe));
    memset(&close_probe, 0, sizeof(close_probe));
    close_probe.close_status = -999;

    if(expect_true(UTL_TimerInit() == 0, "UTL_TimerInit should initialize before close/create race test") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_TimerCreate(&timer, UTL_TIMER_E_PERIOD, 5, slow_timer_cb, &slow_probe) == 0,
                   "UTL_TimerCreate should create a slow periodic timer for close/create race test") != 0)
    {
        UTL_TimerClose();
        return 1;
    }

    UTL_Sleep(15);

    if(expect_true(pthread_create(&closer, NULL, timer_close_thread, &close_probe) == 0,
                   "close helper thread should start") != 0)
    {
        UTL_TimerDelete(&timer);
        UTL_TimerClose();
        return 1;
    }

    for(i = 0; i < 200; ++i)
    {
        MSG_ID_T sts = UTL_TimerCreate(&rejected_timer, UTL_TIMER_E_ONESHOT, 10, timer_cb, NULL);
        if(sts == ENT_TMR_NOT_INITIALIZED)
        {
            observed_rejection = 1;
            break;
        }
        if(sts == 0)
        {
            UTL_TimerDelete(&rejected_timer);
            rejected_timer = NULL;
        }
        UTL_Sleep(1);
    }

    pthread_join(closer, NULL);
    timer = NULL;

    if(expect_true(close_probe.close_status == 0, "UTL_TimerClose should succeed in helper thread") != 0)
    {
        return 1;
    }

    return expect_true(observed_rejection == 1,
                       "UTL_TimerCreate should be rejected once timer close has started");
}
#endif

int main(void)
{
    int failures = 0;

    failures += test_timer_rejects_uninitialized_use();
    failures += test_timer_init_and_close_are_idempotent();
    failures += test_oneshot_timer_fires_once();
    failures += test_periodic_timer_fires_until_deleted();
    failures += test_periodic_timer_remains_stable_with_slow_callback();
    failures += test_timer_delete_allows_reentrant_callback_timer_api();
    failures += test_timer_create_us_has_consistent_failure_contract();
    failures += test_timer_create_us_delete_does_not_wait_full_period();
    failures += test_timer_create_us_periodic_timer_fires_on_linux();
    failures += test_timer_create_us_callback_can_self_delete_safely();
#ifndef _WIN32
    failures += test_timer_callback_can_self_delete_safely();
    failures += test_timer_create_is_rejected_while_close_progresses();
#endif

    if(failures != 0)
    {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
