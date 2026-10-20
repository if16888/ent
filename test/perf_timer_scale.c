#define _POSIX_C_SOURCE 200809L
/* Observational Linux scaling test. Inter-arrival errors and (for regular
 * millisecond timers only) test-hook absolute-deadline lags. No timing gates. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>
#include "ient_comm.h"
#include "ient_runtime.h"
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


#define TIMER_LIMIT 128
#define SAMPLES_PER_TIMER 8192
/* Compiled only in this benchmarking target with ENT_TIMER_TEST_HOOKS.
 * The production library does not expose a deadline introspection API. */
long long iUTL_TimerTestCallbackDeadlineNs(void);
typedef struct {
    long long stamps[SAMPLES_PER_TIMER];
    long long deadline_lag_ns[SAMPLES_PER_TIMER];
    int n;
} TIMER_PROBE;

static long long ns(clockid_t clock_id) {
    struct timespec t;
    return clock_gettime(clock_id, &t) == 0 ?
        (long long)t.tv_sec * 1000000000LL + t.tv_nsec : -1;
}
static int parse(const char* s, int lo, int hi, int* out) {
    char* e = NULL;
    long n;
    errno = 0;
    if(s == NULL || *s == 0) return -1;
    n = strtol(s, &e, 10);
    if(errno || *e != 0 || n < lo || n > hi) return -1;
    *out = (int)n;
    return 0;
}
static int threads(void) {
    FILE* f = fopen("/proc/self/status", "r");
    char line[128];
    int count = -1;
    if(!f) return -1;
    while(fgets(line, sizeof(line), f))
        if(sscanf(line, "Threads: %d", &count) == 1) break;
    fclose(f);
    return count;
}
/* Immediately after join, /proc may still count a departing task briefly.
 * Allow up to 100 ms for the thread-count snapshot to settle. This wait
 * happens outside the measured CPU/wall benchmark interval. */
static int threads_after_settle(int expected)
{
    struct timespec pause = {0, 5000000L};
    int n = threads();
    int retry;
    for(retry = 0; expected >= 0 && n > expected && retry < 20; ++retry)
    {
        (void)nanosleep(&pause, NULL);
        n = threads();
    }
    return n;
}
static void* callback(void* data) {
    TIMER_PROBE* p = (TIMER_PROBE*)data;
    /* Timer runtime serializes callback invocations for a given timer. */
    if(p->n < SAMPLES_PER_TIMER) {
        long long stamp = ns(CLOCK_MONOTONIC);
        long long deadline = iUTL_TimerTestCallbackDeadlineNs();
        p->stamps[p->n] = stamp;
        p->deadline_lag_ns[p->n] =
            (deadline > 0 && stamp >= deadline) ? stamp - deadline : -1;
        p->n++;
    }
    return NULL;
}
static int cmp(const void* a, const void* b) {
    double x = *(const double*)a, y = *(const double*)b;
    return (x > y) - (x < y);
}
static double pct(double* sorted, int n, int p) {
    int index = (n * p + 99) / 100;
    if(index < 1) index = 1;
    if(index > n) index = n;
    return sorted[index - 1];
}

int main(int argc, char** argv) {
    int count, period, duration, mode_us, i, k, started = 0, open = 0;
    int before, active, after, sample_count = 0, total_hits = 0, lag_count = 0;
    int status = EXIT_FAILURE;
    long long base_ns, wall_a, wall_b, cpu_a, cpu_b;
    long long close_a, close_b;
    UTL_TIMER_T* timers = NULL;
    TIMER_PROBE* probes = NULL;
    double* deviation_us = NULL;
    double* lag_us = NULL;
    double* deletion_us = NULL;
    struct rusage resource;
    struct rusage resource_a;

    mode_us = argc == 5 && strcmp(argv[1], "us") == 0;
    if(argc != 5 || (strcmp(argv[1], "ms") != 0 && !mode_us) ||
       parse(argv[2], 1, TIMER_LIMIT, &count) ||
       parse(argv[3], 1, mode_us ? 1000000 : 10000, &period) ||
       parse(argv[4], 250, 5000, &duration)) {
        fprintf(stderr, "usage: %s ms|us timers(1..128) period(ms:1..10000 us:1..1000000) duration_ms(250..5000)\n", argv[0]);
        return EXIT_FAILURE;
    }
    base_ns = (long long)period * (mode_us ? 1000LL : 1000000LL);
    timers = (UTL_TIMER_T*)calloc((size_t)count, sizeof(*timers));
    probes = (TIMER_PROBE*)calloc((size_t)count, sizeof(*probes));
    deletion_us = (double*)calloc((size_t)count, sizeof(*deletion_us));
    if(timers == NULL || probes == NULL || deletion_us == NULL) goto CLEANUP;
    before = threads();
    if(UTL_TimerInit() != ENT_SYS_NORMAL) goto CLEANUP;
    started = 1;
    wall_a = ns(CLOCK_MONOTONIC);
    cpu_a = ns(CLOCK_PROCESS_CPUTIME_ID);
    if(wall_a < 0 || cpu_a < 0) goto CLEANUP;
    memset(&resource_a, 0, sizeof(resource_a));
    if(getrusage(RUSAGE_SELF, &resource_a) != 0) goto CLEANUP;
    for(i = 0; i < count; ++i) {
        MSG_ID_T rc = mode_us ?
            UTL_TimerCreateUs(&timers[i], UTL_TIMER_E_PERIOD, period, callback, &probes[i]) :
            UTL_TimerCreate(&timers[i], UTL_TIMER_E_PERIOD, period, callback, &probes[i]);
        if(rc != ENT_SYS_NORMAL) {
            fprintf(stderr, "timer creation failed timer=%d code=%d\n", i, rc);
            goto CLEANUP;
        }
        open++;
    }
    active = threads();
    UTL_Sleep(duration);
    for(i = 0; i < open; ++i) {
        long long deletion_a = ns(CLOCK_MONOTONIC);
        long long deletion_b;
        if(deletion_a < 0 || UTL_TimerDelete(&timers[i]) != ENT_SYS_NORMAL)
            goto CLEANUP;
        deletion_b = ns(CLOCK_MONOTONIC);
        if(deletion_b < deletion_a) goto CLEANUP;
        deletion_us[i] = (double)(deletion_b - deletion_a) / 1000.0;
    }
    close_a = ns(CLOCK_MONOTONIC);
    if(close_a < 0 || UTL_TimerClose() != ENT_SYS_NORMAL) goto CLEANUP;
    started = 0;
    close_b = ns(CLOCK_MONOTONIC);
    cpu_b = ns(CLOCK_PROCESS_CPUTIME_ID);
    wall_b = ns(CLOCK_MONOTONIC);
    if(close_b < close_a || cpu_b < 0 || wall_b <= wall_a) goto CLEANUP;
    after = threads_after_settle(before);
    deviation_us = (double*)malloc((size_t)count * (SAMPLES_PER_TIMER - 1) * sizeof(double));
    if(!mode_us)
        lag_us = (double*)malloc((size_t)count * SAMPLES_PER_TIMER * sizeof(double));
    if(!deviation_us || (!mode_us && !lag_us)) goto CLEANUP;
    for(i = 0; i < count; ++i) {
        TIMER_PROBE* p = &probes[i];
        total_hits += p->n;
        if(!mode_us) for(k = 0; k < p->n; ++k) {
            if(p->deadline_lag_ns[k] >= 0)
                lag_us[lag_count++] = (double)p->deadline_lag_ns[k] / 1000.0;
        }
        for(k = 1; k < p->n; ++k) {
            double err = (double)(p->stamps[k] - p->stamps[k-1] - base_ns) / 1000.0;
            if(err < 0) err = -err;
            deviation_us[sample_count++] = err;
        }
    }
    if(sample_count < 2 || (!mode_us && lag_count != total_hits)) {
        fprintf(stderr, "insufficient samples or missing deadline trace: intervals=%d lags=%d hits=%d\n",
                sample_count, lag_count, total_hits);
        goto CLEANUP;
    }
    qsort(deviation_us, (size_t)sample_count, sizeof(double), cmp);
    if(!mode_us) qsort(lag_us, (size_t)lag_count, sizeof(double), cmp);
    qsort(deletion_us, (size_t)count, sizeof(double), cmp);
    memset(&resource, 0, sizeof(resource));
    (void)getrusage(RUSAGE_SELF, &resource);
    printf("timer_scale mode=%s timers=%d period=%d duration_ms=%d hits=%d intervals=%d "
           "abs_interarrival_error_p50_us=%.3f p95_us=%.3f p99_us=%.3f max_us=%.3f "
           "cpu_process_ms=%.3f wall_ms=%.3f cpu_pct_single_core=%.2f "
           "threads_before=%d threads_active=%d threads_after=%d ru_maxrss_kib=%ld "
           "voluntary_ctxt_switches=%ld involuntary_ctxt_switches=%ld "
           "delete_p50_us=%.3f delete_p95_us=%.3f delete_p99_us=%.3f "
           "delete_max_us=%.3f timer_close_us=%.3f",
           argv[1], count, period, duration, total_hits, sample_count,
           pct(deviation_us, sample_count, 50),
           pct(deviation_us, sample_count, 95),
           pct(deviation_us, sample_count, 99),
           deviation_us[sample_count-1],
           (double)(cpu_b-cpu_a) / 1e6, (double)(wall_b-wall_a) / 1e6,
           100.0 * (double)(cpu_b-cpu_a) / (double)(wall_b-wall_a),
           before, active, after, resource.ru_maxrss,
           (long)(resource.ru_nvcsw - resource_a.ru_nvcsw),
           (long)(resource.ru_nivcsw - resource_a.ru_nivcsw),
           pct(deletion_us, count, 50), pct(deletion_us, count, 95),
           pct(deletion_us, count, 99), deletion_us[count - 1],
           (double)(close_b - close_a) / 1000.0);
    if(!mode_us) {
        printf(" deadline_lag_p50_us=%.3f deadline_lag_p95_us=%.3f deadline_lag_p99_us=%.3f",
               pct(lag_us, lag_count, 50), pct(lag_us, lag_count, 95),
               pct(lag_us, lag_count, 99));
    }
    printf("\n");
    status = EXIT_SUCCESS;
CLEANUP:
    if(timers) for(i = 0; i < open; ++i)
        if(timers[i] && UTL_TimerDelete(&timers[i]) != ENT_SYS_NORMAL) status = EXIT_FAILURE;
    if(started && UTL_TimerClose() != ENT_SYS_NORMAL) status = EXIT_FAILURE;
    free(timers);
    free(probes);
    free(deviation_us);
    free(lag_us);
    free(deletion_us);
    return status;
}
