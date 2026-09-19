#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ient_comm.h"
#include "ient_runtime.h"
#include "ent_msg.h"
#include "ent_utility.h"

ENT_CTX gEntCtx;

MSG_ID_T ENT_LogInit(void) { return ENT_SYS_NORMAL; }
MSG_ID_T ENT_LogClose(void) { return ENT_SYS_NORMAL; }
MSG_ID_T ENT_LogInitHandle(ENT_LOG* pLogHandle, const char* moduleName, const char* logPath)
{
    (void)moduleName;
    (void)logPath;
    if(pLogHandle != NULL)
    {
        *pLogHandle = (ENT_LOG)0x1;
    }
    return ENT_SYS_NORMAL;
}
MSG_ID_T ENT_LogSetOption(ENT_LOG logHandle, ENT_LOG_OPTIONS_E option, const void* arg)
{
    (void)logHandle;
    (void)option;
    (void)arg;
    return ENT_SYS_NORMAL;
}
MSG_ID_T ENT_LogCloseHandle(ENT_LOG logHandle) { (void)logHandle; return ENT_SYS_NORMAL; }
MSG_ID_T ENT_LogRaw(ENT_LOG logHandle, const char* format, ...) { (void)logHandle; (void)format; return ENT_SYS_NORMAL; }
MSG_ID_T ENT_LogFatal(ENT_LOG logHandle, const char* format, ...) { (void)logHandle; (void)format; return ENT_SYS_NORMAL; }
MSG_ID_T ENT_LogError(ENT_LOG logHandle, const char* format, ...) { (void)logHandle; (void)format; return ENT_SYS_NORMAL; }
MSG_ID_T ENT_LogWarn(ENT_LOG logHandle, const char* format, ...) { (void)logHandle; (void)format; return ENT_SYS_NORMAL; }
MSG_ID_T ENT_LogPrint(ENT_LOG logHandle, const char* format, ...) { (void)logHandle; (void)format; return ENT_SYS_NORMAL; }
MSG_ID_T ENT_LogDebug(ENT_LOG logHandle, const char* format, ...) { (void)logHandle; (void)format; return ENT_SYS_NORMAL; }

int iUTL_TimerTestLifecycleOpCount(void);
int iUTL_TimerTestClosing(void);
int iUTL_TimerTestThreadSelfDetachCount(void);

typedef struct TIMER_THREAD_SAFETY_PROBE
{
    pthread_mutex_t lock;
    pthread_cond_t cv;
    int hits;
} TIMER_THREAD_SAFETY_PROBE;

static int probe_init(TIMER_THREAD_SAFETY_PROBE* probe)
{
    memset(probe, 0, sizeof(*probe));
    if(pthread_mutex_init(&probe->lock, NULL) != 0)
    {
        return -1;
    }
    if(pthread_cond_init(&probe->cv, NULL) != 0)
    {
        pthread_mutex_destroy(&probe->lock);
        return -1;
    }
    return 0;
}

static void probe_destroy(TIMER_THREAD_SAFETY_PROBE* probe)
{
    pthread_cond_destroy(&probe->cv);
    pthread_mutex_destroy(&probe->lock);
}

static void* timer_cb(void* data)
{
    TIMER_THREAD_SAFETY_PROBE* probe = (TIMER_THREAD_SAFETY_PROBE*)data;

    pthread_mutex_lock(&probe->lock);
    probe->hits++;
    pthread_cond_broadcast(&probe->cv);
    pthread_mutex_unlock(&probe->lock);
    return NULL;
}

static int probe_wait_for_hit(TIMER_THREAD_SAFETY_PROBE* probe)
{
    struct timespec deadline;
    int rc = 0;

    if(clock_gettime(CLOCK_REALTIME, &deadline) != 0)
    {
        return -1;
    }
    deadline.tv_sec += 2;

    pthread_mutex_lock(&probe->lock);
    while(probe->hits == 0)
    {
        rc = pthread_cond_timedwait(&probe->cv, &probe->lock, &deadline);
        if(rc != 0)
        {
            pthread_mutex_unlock(&probe->lock);
            return -1;
        }
    }
    pthread_mutex_unlock(&probe->lock);
    return 0;
}

static int probe_hits(TIMER_THREAD_SAFETY_PROBE* probe)
{
    int hits;

    pthread_mutex_lock(&probe->lock);
    hits = probe->hits;
    pthread_mutex_unlock(&probe->lock);
    return hits;
}

static void sleep_ms(int ms)
{
    struct timespec req;

    req.tv_sec = ms / 1000;
    req.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&req, NULL);
}

typedef struct TIMER_SELF_DELETE_CLOSE_PROBE
{
    pthread_mutex_t lock;
    pthread_cond_t cv;
    UTL_TIMER_T timer;
    int delete_returned;
    int release_callback;
    int close_done;
    MSG_ID_T delete_status;
    MSG_ID_T close_status;
} TIMER_SELF_DELETE_CLOSE_PROBE;

static void* self_delete_cb(void* data)
{
    TIMER_SELF_DELETE_CLOSE_PROBE* probe = (TIMER_SELF_DELETE_CLOSE_PROBE*)data;
    MSG_ID_T sts = UTL_TimerDelete(&probe->timer);

    pthread_mutex_lock(&probe->lock);
    probe->delete_status = sts;
    probe->delete_returned = 1;
    pthread_cond_broadcast(&probe->cv);
    while(!probe->release_callback)
    {
        pthread_cond_wait(&probe->cv, &probe->lock);
    }
    pthread_mutex_unlock(&probe->lock);
    return NULL;
}

static void* close_thread(void* data)
{
    TIMER_SELF_DELETE_CLOSE_PROBE* probe = (TIMER_SELF_DELETE_CLOSE_PROBE*)data;
    MSG_ID_T sts = UTL_TimerClose();

    pthread_mutex_lock(&probe->lock);
    probe->close_status = sts;
    probe->close_done = 1;
    pthread_cond_broadcast(&probe->cv);
    pthread_mutex_unlock(&probe->lock);
    return NULL;
}

static int wait_flag(pthread_mutex_t* lock,
                     pthread_cond_t* cv,
                     int* flag)
{
    struct timespec deadline;
    int rc = 0;

    if(clock_gettime(CLOCK_REALTIME, &deadline) != 0)
    {
        return -1;
    }
    deadline.tv_sec += 2;

    pthread_mutex_lock(lock);
    while(!*flag)
    {
        rc = pthread_cond_timedwait(cv, lock, &deadline);
        if(rc != 0)
        {
            pthread_mutex_unlock(lock);
            return -1;
        }
    }
    pthread_mutex_unlock(lock);
    return 0;
}

static int test_self_delete_close_waits_for_cleanup(void)
{
    TIMER_SELF_DELETE_CLOSE_PROBE probe;
    pthread_t closer;
    int closer_started = 0;
    int rc = 1;
    int i;

    memset(&probe, 0, sizeof(probe));
    probe.delete_status = -999;
    probe.close_status = -999;
    if(pthread_mutex_init(&probe.lock, NULL) != 0)
    {
        return 1;
    }
    if(pthread_cond_init(&probe.cv, NULL) != 0)
    {
        pthread_mutex_destroy(&probe.lock);
        return 1;
    }

    if(UTL_TimerInit() != ENT_SYS_NORMAL)
    {
        goto CLEANUP;
    }
    if(UTL_TimerCreate(&probe.timer,
                       UTL_TIMER_E_PERIOD,
                       1,
                       self_delete_cb,
                       &probe) != ENT_SYS_NORMAL)
    {
        UTL_TimerClose();
        goto CLEANUP;
    }
    if(wait_flag(&probe.lock, &probe.cv, &probe.delete_returned) != 0)
    {
        goto RELEASE_CALLBACK;
    }

    if(probe.delete_status != ENT_SYS_NORMAL ||
       probe.timer != NULL ||
       iUTL_TimerTestLifecycleOpCount() != 1)
    {
        goto RELEASE_CALLBACK;
    }

    if(pthread_create(&closer, NULL, close_thread, &probe) != 0)
    {
        goto RELEASE_CALLBACK;
    }
    closer_started = 1;

    for(i = 0; i < 2000 && !iUTL_TimerTestClosing(); ++i)
    {
        sleep_ms(1);
    }
    if(!iUTL_TimerTestClosing())
    {
        goto RELEASE_CALLBACK;
    }

    pthread_mutex_lock(&probe.lock);
    if(probe.close_done)
    {
        pthread_mutex_unlock(&probe.lock);
        goto RELEASE_CALLBACK;
    }
    pthread_mutex_unlock(&probe.lock);

    if(iUTL_TimerTestLifecycleOpCount() != 1)
    {
        goto RELEASE_CALLBACK;
    }

    rc = 0;

RELEASE_CALLBACK:
    pthread_mutex_lock(&probe.lock);
    probe.release_callback = 1;
    pthread_cond_broadcast(&probe.cv);
    pthread_mutex_unlock(&probe.lock);

    if(closer_started)
    {
        pthread_join(closer, NULL);
        if(probe.close_status != ENT_SYS_NORMAL ||
           iUTL_TimerTestLifecycleOpCount() != 0 ||
           iUTL_TimerTestClosing())
        {
            rc = 1;
        }
    }
    else
    {
        UTL_TimerClose();
        rc = 1;
    }

CLEANUP:
    pthread_cond_destroy(&probe.cv);
    pthread_mutex_destroy(&probe.lock);
    return rc;
}

typedef struct TIMER_SELF_DELETE_RESOURCE_PROBE
{
    pthread_mutex_t lock;
    pthread_cond_t cv;
    UTL_TIMER_T timer;
    pthread_t callback_thread;
    int callback_thread_valid;
    int callback_done;
    MSG_ID_T delete_status;
} TIMER_SELF_DELETE_RESOURCE_PROBE;

static void* self_delete_resource_cb(void* data)
{
    TIMER_SELF_DELETE_RESOURCE_PROBE* probe = (TIMER_SELF_DELETE_RESOURCE_PROBE*)data;
    MSG_ID_T sts;

    pthread_mutex_lock(&probe->lock);
    probe->callback_thread = pthread_self();
    probe->callback_thread_valid = 1;
    pthread_mutex_unlock(&probe->lock);

    sts = UTL_TimerDelete(&probe->timer);

    pthread_mutex_lock(&probe->lock);
    probe->delete_status = sts;
    probe->callback_done = 1;
    pthread_cond_broadcast(&probe->cv);
    pthread_mutex_unlock(&probe->lock);
    return NULL;
}

static int test_self_delete_reclaims_pthread_resource_before_close(void)
{
    enum { SELF_DELETE_ITERATIONS = 32 };
    TIMER_SELF_DELETE_RESOURCE_PROBE probe;
    int detach_count_before;
    int iteration;
    int i;
    int rc = 1;

    memset(&probe, 0, sizeof(probe));
    probe.delete_status = -999;
    if(pthread_mutex_init(&probe.lock, NULL) != 0)
    {
        return 1;
    }
    if(pthread_cond_init(&probe.cv, NULL) != 0)
    {
        pthread_mutex_destroy(&probe.lock);
        return 1;
    }

    if(UTL_TimerInit() != ENT_SYS_NORMAL)
    {
        goto CLEANUP;
    }
    detach_count_before = iUTL_TimerTestThreadSelfDetachCount();

    for(iteration = 0; iteration < SELF_DELETE_ITERATIONS; ++iteration)
    {
        pthread_mutex_lock(&probe.lock);
        probe.timer = NULL;
        memset(&probe.callback_thread, 0, sizeof(probe.callback_thread));
        probe.callback_thread_valid = 0;
        probe.callback_done = 0;
        probe.delete_status = -999;
        pthread_mutex_unlock(&probe.lock);

        if(UTL_TimerCreate(&probe.timer,
                           UTL_TIMER_E_PERIOD,
                           1,
                           self_delete_resource_cb,
                           &probe) != ENT_SYS_NORMAL)
        {
            fprintf(stderr, "self-delete resource timer create failed at iteration %d\n", iteration);
            goto CLOSE_TIMER;
        }

        if(wait_flag(&probe.lock, &probe.cv, &probe.callback_done) != 0)
        {
            fprintf(stderr, "self-delete resource callback timed out at iteration %d\n", iteration);
            goto CLOSE_TIMER;
        }
        if(probe.delete_status != ENT_SYS_NORMAL || probe.timer != NULL)
        {
            fprintf(stderr, "self-delete resource ownership failed at iteration %d\n", iteration);
            goto CLOSE_TIMER;
        }

        for(i = 0; i < 2000 && iUTL_TimerTestLifecycleOpCount() != 0; ++i)
        {
            sleep_ms(1);
        }
        if(iUTL_TimerTestLifecycleOpCount() != 0)
        {
            fprintf(stderr, "self-delete cleanup did not finish at iteration %d\n", iteration);
            goto CLOSE_TIMER;
        }

        /*
         * The counter advances only after pthread_detach() succeeds. Requiring
         * one new detach commit for every self-delete iteration proves that
         * joinable pthread resources are not abandoned behind an unlisted
         * timer context.
         */
        if(iUTL_TimerTestThreadSelfDetachCount() != detach_count_before + iteration + 1)
        {
            fprintf(stderr, "self-delete did not commit pthread detach at iteration %d\n", iteration);
            goto CLOSE_TIMER;
        }

        if(!probe.callback_thread_valid)
        {
            fprintf(stderr, "self-delete callback thread identity missing at iteration %d\n", iteration);
            goto CLOSE_TIMER;
        }
        if(pthread_join(probe.callback_thread, NULL) == 0)
        {
            fprintf(stderr, "self-delete left a joinable pthread resource at iteration %d\n", iteration);
            goto CLOSE_TIMER;
        }
    }

    if(UTL_TimerClose() != ENT_SYS_NORMAL)
    {
        goto CLEANUP;
    }

    rc = 0;
    goto CLEANUP;

CLOSE_TIMER:
    if(probe.timer != NULL)
    {
        (void)UTL_TimerDelete(&probe.timer);
    }
    (void)UTL_TimerClose();

CLEANUP:
    pthread_cond_destroy(&probe.cv);
    pthread_mutex_destroy(&probe.lock);
    return rc;
}

int main(void)
{
    enum { ITERATIONS = 100 };
    TIMER_THREAD_SAFETY_PROBE probe;
    int i;

    if(probe_init(&probe) != 0)
    {
        fprintf(stderr, "probe_init failed\n");
        return EXIT_FAILURE;
    }
    if(UTL_TimerInit() != ENT_SYS_NORMAL)
    {
        fprintf(stderr, "UTL_TimerInit failed\n");
        probe_destroy(&probe);
        return EXIT_FAILURE;
    }

    for(i = 0; i < ITERATIONS; ++i)
    {
        UTL_TIMER_T timer = NULL;
        int after_delete;

        pthread_mutex_lock(&probe.lock);
        probe.hits = 0;
        pthread_mutex_unlock(&probe.lock);

        if(UTL_TimerCreate(&timer, UTL_TIMER_E_PERIOD, 1, timer_cb, &probe) != ENT_SYS_NORMAL)
        {
            fprintf(stderr, "UTL_TimerCreate failed at iteration %d\n", i);
            UTL_TimerClose();
            probe_destroy(&probe);
            return EXIT_FAILURE;
        }
        if(probe_wait_for_hit(&probe) != 0)
        {
            fprintf(stderr, "timer did not fire at iteration %d\n", i);
            UTL_TimerDelete(&timer);
            UTL_TimerClose();
            probe_destroy(&probe);
            return EXIT_FAILURE;
        }
        if(UTL_TimerDelete(&timer) != ENT_SYS_NORMAL || timer != NULL)
        {
            fprintf(stderr, "UTL_TimerDelete failed at iteration %d\n", i);
            UTL_TimerClose();
            probe_destroy(&probe);
            return EXIT_FAILURE;
        }

        after_delete = probe_hits(&probe);
        sleep_ms(2);
        if(probe_hits(&probe) != after_delete)
        {
            fprintf(stderr, "callback ran after delete at iteration %d\n", i);
            UTL_TimerClose();
            probe_destroy(&probe);
            return EXIT_FAILURE;
        }
    }

    if(UTL_TimerClose() != ENT_SYS_NORMAL)
    {
        fprintf(stderr, "UTL_TimerClose failed\n");
        probe_destroy(&probe);
        return EXIT_FAILURE;
    }

    probe_destroy(&probe);

    if(test_self_delete_close_waits_for_cleanup() != 0)
    {
        fprintf(stderr, "self-delete close lifecycle probe failed\n");
        return EXIT_FAILURE;
    }

    if(test_self_delete_reclaims_pthread_resource_before_close() != 0)
    {
        fprintf(stderr, "self-delete pthread resource probe failed\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
