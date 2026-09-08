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
    int callback_entered;
    int release_callback;
    int hits;
} RT_TSAN_PROBE;

static int expect_true(int condition, const char* message)
{
    if(!condition)
    {
        fprintf(stderr, "%s\n", message);
        return 1;
    }
    return 0;
}

static void* blocking_cb(void* data)
{
    RT_TSAN_PROBE* probe = (RT_TSAN_PROBE*)data;

    pthread_mutex_lock(&probe->lock);
    probe->callback_entered = 1;
    probe->hits++;
    pthread_cond_broadcast(&probe->cv);
    while(!probe->release_callback)
    {
        pthread_cond_wait(&probe->cv, &probe->lock);
    }
    pthread_mutex_unlock(&probe->lock);
    return NULL;
}

static int wait_for_callback(RT_TSAN_PROBE* probe)
{
    struct timespec deadline;
    int wait_sts = 0;

    if(clock_gettime(CLOCK_REALTIME, &deadline) != 0)
    {
        return 0;
    }
    deadline.tv_sec += 2;

    pthread_mutex_lock(&probe->lock);
    while(!probe->callback_entered && wait_sts == 0)
    {
        wait_sts = pthread_cond_timedwait(&probe->cv, &probe->lock, &deadline);
    }
    pthread_mutex_unlock(&probe->lock);
    return wait_sts == 0;
}

typedef struct
{
    UTL_TIMER_T* timer;
    MSG_ID_T status;
} DELETE_THREAD_CTX;

static void* delete_thread(void* data)
{
    DELETE_THREAD_CTX* ctx = (DELETE_THREAD_CTX*)data;
    ctx->status = UTL_TimerDelete(ctx->timer);
    return NULL;
}

int main(void)
{
#ifdef __linux__
    RT_TSAN_PROBE probe;
    DELETE_THREAD_CTX delete_ctx;
    pthread_t deleter;

    memset(&probe, 0, sizeof(probe));
    memset(&delete_ctx, 0, sizeof(delete_ctx));
    delete_ctx.status = -999;

    if(pthread_mutex_init(&probe.lock, NULL) != 0 ||
       pthread_cond_init(&probe.cv, NULL) != 0)
    {
        return EXIT_FAILURE;
    }

    if(expect_true(UTL_TimerInit() == ENT_SYS_NORMAL,
                   "UTL_TimerInit should initialize RT TSan regression") != 0 ||
       expect_true(UTL_TimerCreateUs(&probe.timer,
                                     UTL_TIMER_E_PERIOD,
                                     1000,
                                     blocking_cb,
                                     &probe) == ENT_SYS_NORMAL,
                   "UTL_TimerCreateUs should create RT TSan regression timer") != 0 ||
       expect_true(wait_for_callback(&probe),
                   "RT callback should enter before concurrent delete") != 0)
    {
        if(probe.timer != NULL)
        {
            UTL_TimerDelete(&probe.timer);
        }
        UTL_TimerClose();
        pthread_cond_destroy(&probe.cv);
        pthread_mutex_destroy(&probe.lock);
        return EXIT_FAILURE;
    }

    delete_ctx.timer = &probe.timer;
    if(expect_true(pthread_create(&deleter, NULL, delete_thread, &delete_ctx) == 0,
                   "delete helper should start") != 0)
    {
        pthread_mutex_lock(&probe.lock);
        probe.release_callback = 1;
        pthread_cond_broadcast(&probe.cv);
        pthread_mutex_unlock(&probe.lock);
        UTL_TimerDelete(&probe.timer);
        UTL_TimerClose();
        pthread_cond_destroy(&probe.cv);
        pthread_mutex_destroy(&probe.lock);
        return EXIT_FAILURE;
    }

    UTL_Sleep(20);
    pthread_mutex_lock(&probe.lock);
    probe.release_callback = 1;
    pthread_cond_broadcast(&probe.cv);
    pthread_mutex_unlock(&probe.lock);

    pthread_join(deleter, NULL);

    if(expect_true(delete_ctx.status == ENT_SYS_NORMAL,
                   "concurrent RT delete should succeed") != 0 ||
       expect_true(probe.timer == NULL,
                   "concurrent RT delete should clear the handle") != 0 ||
       expect_true(probe.hits == 1,
                   "stopped RT timer should not schedule another callback") != 0)
    {
        UTL_TimerClose();
        pthread_cond_destroy(&probe.cv);
        pthread_mutex_destroy(&probe.lock);
        return EXIT_FAILURE;
    }

    if(expect_true(UTL_TimerClose() == ENT_SYS_NORMAL,
                   "UTL_TimerClose should succeed after RT TSan regression") != 0)
    {
        pthread_cond_destroy(&probe.cv);
        pthread_mutex_destroy(&probe.lock);
        return EXIT_FAILURE;
    }

    pthread_cond_destroy(&probe.cv);
    pthread_mutex_destroy(&probe.lock);
#endif
    return EXIT_SUCCESS;
}
