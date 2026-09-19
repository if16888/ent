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
    return EXIT_SUCCESS;
}
