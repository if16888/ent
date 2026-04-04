#define _GNU_SOURCE

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ient_comm.h"
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

typedef struct
{
    long long* stamps_ns;
    int sample_count;
    volatile int hits;
} PERF_RT_PROBE;

static int parse_int_arg(const char* arg, int default_value, int* ok)
{
    char* end = NULL;
    long value = 0;

    if(arg == NULL)
    {
        return default_value;
    }

    errno = 0;
    value = strtol(arg, &end, 10);
    if(errno != 0 || end == arg || *end != '\0')
    {
        if(ok != NULL)
        {
            *ok = 0;
        }
        return default_value;
    }

    if(value < -2147483647L - 1L || value > 2147483647L)
    {
        if(ok != NULL)
        {
            *ok = 0;
        }
        return default_value;
    }

    return (int)value;
}

static const char* policy_name(int policy)
{
    switch(policy)
    {
        case SCHED_FIFO:
            return "SCHED_FIFO";
        case SCHED_RR:
            return "SCHED_RR";
        case SCHED_OTHER:
            return "SCHED_OTHER";
        default:
            return "UNKNOWN";
    }
}

static int compare_double(const void* lhs, const void* rhs)
{
    const double a = *(const double*)lhs;
    const double b = *(const double*)rhs;

    if(a < b) return -1;
    if(a > b) return 1;
    return 0;
}

static double percentile_from_sorted(const double* sorted, int count, double percentile)
{
    double desired_rank = (percentile * (double)count) / 100.0;
    int rank = (int)desired_rank;

    if((double)rank < desired_rank)
    {
        rank += 1;
    }
    if(rank < 1)
    {
        rank = 1;
    }
    if(rank > count)
    {
        rank = count;
    }
    return sorted[rank - 1];
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

static void* perf_rt_timer_cb(void* data)
{
    PERF_RT_PROBE* probe = (PERF_RT_PROBE*)data;
    int idx = __sync_fetch_and_add(&probe->hits, 1);

    if(idx < probe->sample_count)
    {
        probe->stamps_ns[idx] = monotonic_ns();
    }

    return NULL;
}
#endif

int main(int argc, char** argv)
{
    int parse_ok = 1;
    int target_period_us = 1000;
    int sample_count = 100000;
    int requested_cpu = 0;
    int requested_priority = 80;
    int requested_policy = SCHED_FIFO;

    UTL_TIMER_T timer = NULL;
    PERF_RT_PROBE probe;
    int timer_inited = 0;
    int timer_created = 0;
    int affinity_set = 0;
    int sched_set = 0;
    int actual_policy = SCHED_OTHER;
    int actual_priority = 0;
    int measured_intervals = 0;
    double* jitters = NULL;
    int over_100us_count = 0;
    double avg_period_us = 0.0;
    double min_period_us = 0.0;
    double max_period_us = 0.0;
    double avg_jitter_us = 0.0;
    double max_jitter_us = 0.0;
    double over_100us_ratio = 0.0;
    double p99_jitter_us = 0.0;
    double p999_jitter_us = 0.0;
    int exit_code = EXIT_FAILURE;

    memset(&probe, 0, sizeof(probe));

    if(argc > 1)
    {
        target_period_us = parse_int_arg(argv[1], target_period_us, &parse_ok);
    }
    if(argc > 2)
    {
        sample_count = parse_int_arg(argv[2], sample_count, &parse_ok);
    }
    if(argc > 3)
    {
        requested_cpu = parse_int_arg(argv[3], requested_cpu, &parse_ok);
    }
    if(argc > 4)
    {
        requested_priority = parse_int_arg(argv[4], requested_priority, &parse_ok);
    }

    printf("env target_period_us=%d samples=%d requested_cpu=%d requested_policy=%s requested_priority=%d\n",
           target_period_us,
           sample_count,
           requested_cpu,
           policy_name(requested_policy),
           requested_priority);

    if(!parse_ok)
    {
        fprintf(stderr, "invalid numeric argument\n");
        return EXIT_FAILURE;
    }

#ifndef __linux__
    fprintf(stderr, "perf_utl_timer_rt requires Linux\n");
    return EXIT_FAILURE;
#else
    if(requested_cpu >= 0 && requested_cpu < CPU_SETSIZE)
    {
        cpu_set_t requested_mask;
        cpu_set_t actual_mask;

        CPU_ZERO(&requested_mask);
        CPU_SET(requested_cpu, &requested_mask);
        if(pthread_setaffinity_np(pthread_self(), sizeof(requested_mask), &requested_mask) == 0)
        {
            affinity_set = 1;
        }
        if(pthread_getaffinity_np(pthread_self(), sizeof(actual_mask), &actual_mask) == 0)
        {
            affinity_set = affinity_set && CPU_ISSET(requested_cpu, &actual_mask);
        }
    }

    {
        struct sched_param sched_param;

        memset(&sched_param, 0, sizeof(sched_param));
        sched_param.sched_priority = requested_priority;
        if(pthread_setschedparam(pthread_self(), requested_policy, &sched_param) == 0)
        {
            sched_set = 1;
        }
        if(pthread_getschedparam(pthread_self(), &actual_policy, &sched_param) == 0)
        {
            actual_priority = sched_param.sched_priority;
        }
    }

    printf("runtime affinity_set=%d sched_set=%d actual_policy=%s actual_priority=%d timer_path=rt_us\n",
           affinity_set,
           sched_set,
           policy_name(actual_policy),
           actual_priority);

    if(sample_count < 2)
    {
        fprintf(stderr, "sample_count must be at least 2\n");
        return EXIT_FAILURE;
    }
    if(target_period_us <= 0)
    {
        fprintf(stderr, "target_period_us must be positive\n");
        return EXIT_FAILURE;
    }

    probe.sample_count = sample_count;
    probe.stamps_ns = (long long*)calloc((size_t)sample_count, sizeof(long long));
    if(probe.stamps_ns == NULL)
    {
        fprintf(stderr, "failed to allocate stamp buffer\n");
        return EXIT_FAILURE;
    }

    if(UTL_TimerInit() != 0)
    {
        fprintf(stderr, "UTL_TimerInit failed\n");
        goto CLEANUP;
    }
    timer_inited = 1;

    if(UTL_TimerCreateUs(&timer, UTL_TIMER_E_PERIOD, target_period_us, perf_rt_timer_cb, &probe) != 0)
    {
        fprintf(stderr, "UTL_TimerCreateUs failed\n");
        goto CLEANUP;
    }
    timer_created = 1;

    while(probe.hits < sample_count)
    {
        UTL_Sleep(1);
    }

    if(UTL_TimerDelete(&timer) != 0)
    {
        fprintf(stderr, "UTL_TimerDelete failed\n");
        goto CLEANUP;
    }
    timer_created = 0;

    if(UTL_TimerClose() != 0)
    {
        fprintf(stderr, "UTL_TimerClose failed\n");
        timer_inited = 0;
        goto CLEANUP;
    }
    timer_inited = 0;

    if(probe.hits < sample_count)
    {
        fprintf(stderr, "sampling did not reach requested hit count\n");
        goto CLEANUP;
    }

    measured_intervals = sample_count - 1;
    if(measured_intervals < 1)
    {
        fprintf(stderr, "insufficient samples for statistics\n");
        goto CLEANUP;
    }

    jitters = (double*)calloc((size_t)measured_intervals, sizeof(double));
    if(jitters == NULL)
    {
        fprintf(stderr, "failed to allocate stats buffer\n");
        goto CLEANUP;
    }

    {
        int i = 0;
        double total_period_us = 0.0;
        double total_jitter_us = 0.0;

        for(i = 0; i < measured_intervals; ++i)
        {
            double period_us = ((double)(probe.stamps_ns[i + 1] - probe.stamps_ns[i])) / 1000.0;
            double jitter_us = period_us - (double)target_period_us;

            if(i == 0 || period_us < min_period_us)
            {
                min_period_us = period_us;
            }
            if(i == 0 || period_us > max_period_us)
            {
                max_period_us = period_us;
            }
            if(jitter_us < 0.0)
            {
                jitter_us = -jitter_us;
            }

            total_period_us += period_us;
            total_jitter_us += jitter_us;
            if(jitter_us > max_jitter_us)
            {
                max_jitter_us = jitter_us;
            }
            if(jitter_us > 100.0)
            {
                over_100us_count += 1;
            }

            jitters[i] = jitter_us;
        }

        avg_period_us = total_period_us / (double)measured_intervals;
        avg_jitter_us = total_jitter_us / (double)measured_intervals;
        over_100us_ratio = (double)over_100us_count / (double)measured_intervals;
    }

    qsort(jitters, (size_t)measured_intervals, sizeof(double), compare_double);
    p99_jitter_us = percentile_from_sorted(jitters, measured_intervals, 99.0);
    p999_jitter_us = percentile_from_sorted(jitters, measured_intervals, 99.9);

    printf("stats avg_period_us=%.3f min_period_us=%.3f max_period_us=%.3f avg_jitter_us=%.3f max_jitter_us=%.3f over_100us_count=%d over_100us_ratio=%.6f\n",
           avg_period_us,
           min_period_us,
           max_period_us,
           avg_jitter_us,
           max_jitter_us,
           over_100us_count,
           over_100us_ratio);

    printf("result target_period_us=%d samples=%d avg_period_us=%.3f min_period_us=%.3f max_period_us=%.3f avg_jitter_us=%.3f max_jitter_us=%.3f p99_jitter_us=%.3f p999_jitter_us=%.3f over_100us_count=%d over_100us_ratio=%.6f\n",
           target_period_us,
           measured_intervals,
           avg_period_us,
           min_period_us,
           max_period_us,
           avg_jitter_us,
           max_jitter_us,
           p99_jitter_us,
           p999_jitter_us,
           over_100us_count,
           over_100us_ratio);

    exit_code = EXIT_SUCCESS;

CLEANUP:
    if(timer_created)
    {
        UTL_TimerDelete(&timer);
        timer_created = 0;
    }
    if(timer_inited)
    {
        UTL_TimerClose();
        timer_inited = 0;
    }
    free(probe.stamps_ns);
    free(jitters);
    return exit_code;
#endif
}
