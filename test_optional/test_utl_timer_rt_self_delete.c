#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ient_runtime.h"
#include "ent_log.h"
#include "ent_msg.h"
#include "ent_utility.h"

ENT_CTX gEntCtx;

int iUTL_TimerTestRtLiveContextCount(void);

MSG_ID_T ENT_LogInit(void) { return 0; }
MSG_ID_T ENT_LogClose(void) { return 0; }
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
MSG_ID_T ENT_LogCloseHandle(ENT_LOG logHandle) { (void)logHandle; return 0; }
MSG_ID_T ENT_LogRaw(ENT_LOG logHandle, const char* format, ...) { (void)logHandle; (void)format; return 0; }
MSG_ID_T ENT_LogFatal(ENT_LOG logHandle, const char* format, ...) { (void)logHandle; (void)format; return 0; }
MSG_ID_T ENT_LogError(ENT_LOG logHandle, const char* format, ...) { (void)logHandle; (void)format; return 0; }
MSG_ID_T ENT_LogWarn(ENT_LOG logHandle, const char* format, ...) { (void)logHandle; (void)format; return 0; }
MSG_ID_T ENT_LogPrint(ENT_LOG logHandle, const char* format, ...) { (void)logHandle; (void)format; return 0; }
MSG_ID_T ENT_LogDebug(ENT_LOG logHandle, const char* format, ...) { (void)logHandle; (void)format; return 0; }

typedef struct
{
    pthread_mutex_t lock;
    pthread_cond_t cv;
    UTL_TIMER_T timer;
    int hits;
    int done;
    MSG_ID_T delete_status;
} RT_SELF_DELETE_PROBE;

static int expect_true(int condition, const char* message)
{
    if(!condition)
    {
        fprintf(stderr, "%s\n", message);
        return 1;
    }
    return 0;
}

static void add_ms(struct timespec* ts, long ms)
{
    ts->tv_sec += ms / 1000;
    ts->tv_nsec += (ms % 1000) * 1000000L;
    if(ts->tv_nsec >= 1000000000L)
    {
        ts->tv_sec += 1;
        ts->tv_nsec -= 1000000000L;
    }
}

static void* self_delete_cb(void* data)
{
    RT_SELF_DELETE_PROBE* probe = (RT_SELF_DELETE_PROBE*)data;
    MSG_ID_T sts = UTL_TimerDelete(&probe->timer);

    pthread_mutex_lock(&probe->lock);
    probe->hits++;
    probe->delete_status = sts;
    probe->done = 1;
    pthread_cond_broadcast(&probe->cv);
    pthread_mutex_unlock(&probe->lock);
    return NULL;
}

static int wait_done(RT_SELF_DELETE_PROBE* probe)
{
    struct timespec deadline;
    int wait_sts = 0;

    if(clock_gettime(CLOCK_REALTIME, &deadline) != 0)
    {
        return 0;
    }
    add_ms(&deadline, 2000);

    pthread_mutex_lock(&probe->lock);
    while(!probe->done && wait_sts == 0)
    {
        wait_sts = pthread_cond_timedwait(&probe->cv, &probe->lock, &deadline);
    }
    pthread_mutex_unlock(&probe->lock);
    return wait_sts == 0;
}

int main(void)
{
#ifdef __linux__
    enum { TIMER_COUNT = 50 };
    int i;
    int contexts = -1;

    if(expect_true(UTL_TimerInit() == ENT_SYS_NORMAL,
                   "UTL_TimerInit should initialize RT self-delete regression test") != 0)
    {
        return EXIT_FAILURE;
    }

    for(i = 0; i < TIMER_COUNT; ++i)
    {
        RT_SELF_DELETE_PROBE probe;
        MSG_ID_T create_sts;

        memset(&probe, 0, sizeof(probe));
        probe.delete_status = -999;
        if(pthread_mutex_init(&probe.lock, NULL) != 0 ||
           pthread_cond_init(&probe.cv, NULL) != 0)
        {
            UTL_TimerClose();
            return EXIT_FAILURE;
        }

        create_sts = UTL_TimerCreateUs(&probe.timer,
                                       UTL_TIMER_E_PERIOD,
                                       1000,
                                       self_delete_cb,
                                       &probe);
        if(expect_true(create_sts == ENT_SYS_NORMAL,
                       "UTL_TimerCreateUs should create each self-delete timer") != 0 ||
           expect_true(wait_done(&probe),
                       "RT self-delete callback should complete within timeout") != 0)
        {
            if(probe.timer != NULL)
            {
                UTL_TimerDelete(&probe.timer);
            }
            pthread_cond_destroy(&probe.cv);
            pthread_mutex_destroy(&probe.lock);
            UTL_TimerClose();
            return EXIT_FAILURE;
        }

        pthread_mutex_lock(&probe.lock);
        if(expect_true(probe.hits == 1,
                       "each RT self-delete timer should fire exactly once") != 0 ||
           expect_true(probe.delete_status == ENT_SYS_NORMAL,
                       "RT callback self-delete should succeed") != 0 ||
           expect_true(probe.timer == NULL,
                       "RT callback self-delete should clear its handle") != 0)
        {
            pthread_mutex_unlock(&probe.lock);
            pthread_cond_destroy(&probe.cv);
            pthread_mutex_destroy(&probe.lock);
            UTL_TimerClose();
            return EXIT_FAILURE;
        }
        pthread_mutex_unlock(&probe.lock);

        pthread_cond_destroy(&probe.cv);
        pthread_mutex_destroy(&probe.lock);
    }

    for(i = 0; i < 2000; ++i)
    {
        contexts = iUTL_TimerTestRtLiveContextCount();
        if(contexts == 0)
        {
            break;
        }
        UTL_Sleep(1);
    }

    if(expect_true(contexts == 0,
                   "RT self-delete contexts must be reclaimed before UTL_TimerClose") != 0)
    {
        UTL_TimerClose();
        return EXIT_FAILURE;
    }

    return expect_true(UTL_TimerClose() == ENT_SYS_NORMAL,
                       "UTL_TimerClose should succeed after already-reclaimed RT timers") == 0
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
#else
    return EXIT_SUCCESS;
#endif
}
