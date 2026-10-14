#define _GNU_SOURCE
/* Experimental Linux-only timerfd/epoll/eventfd scalability baseline.
 * Not part of the ent timer runtime or its public API. Timers rearm after
 * callback accounting to retain the existing fixed-delay semantics.
 * This prototype has one dispatcher thread, not a thread per timer. */
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/resource.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

#define TIMER_LIMIT 128
#define SAMPLE_CAP 8192
#define EVENTS 64

typedef struct {
    int fd;
    int active;
    int n;
    uint64_t deadline;
    uint64_t stamps[SAMPLE_CAP];
    uint64_t lateness[SAMPLE_CAP];
} PROBE;

typedef struct {
    int epfd;
    int stopfd;
    int count;
    uint64_t period_ns;
    int worker_error;
    pthread_mutex_t lock;
    PROBE *probes;
} BENCH;

static uint64_t monotonic_ns(void)
{
    struct timespec ts;
    if(clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}
static uint64_t cpu_ns(void)
{
    struct timespec ts;
    if(clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}
static void sleep_ms(int ms)
{
    struct timespec req, rem;
    req.tv_sec = ms / 1000;
    req.tv_nsec = (long)(ms % 1000) * 1000000L;
    while(nanosleep(&req, &rem) != 0 && errno == EINTR) req = rem;
}
static int parse(const char *s, int low, int high, int *out)
{
    char *end = NULL;
    long n;
    if(!s || !*s) return -1;
    errno = 0;
    n = strtol(s, &end, 10);
    if(errno || !end || *end || n < low || n > high) return -1;
    *out = (int)n;
    return 0;
}
static int threads(void)
{
    FILE *f = fopen("/proc/self/status", "r");
    char line[128];
    int n = -1;
    if(!f) return -1;
    while(fgets(line, sizeof(line), f))
        if(sscanf(line, "Threads: %d", &n) == 1) break;
    fclose(f);
    return n;
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
static int arm(PROBE *p)
{
    struct itimerspec req;
    memset(&req, 0, sizeof(req));
    req.it_value.tv_sec = (time_t)(p->deadline / 1000000000ULL);
    req.it_value.tv_nsec = (long)(p->deadline % 1000000000ULL);
    return timerfd_settime(p->fd, TFD_TIMER_ABSTIME, &req, NULL);
}
static void *dispatch(void *arg)
{
    BENCH *b = (BENCH *)arg;
    struct epoll_event ready[EVENTS];
    for(;;)
    {
        int n = epoll_wait(b->epfd, ready, EVENTS, -1);
        int k;
        if(n < 0 && errno == EINTR) continue;
        if(n < 0) { b->worker_error = 1; return NULL; }
        for(k = 0; k < n; ++k)
        {
            uint32_t token = ready[k].data.u32;
            uint64_t expirations = 0;
            PROBE *p;
            uint64_t now;
            if(token == 0) return NULL; /* eventfd stop */
            if(token > (uint32_t)b->count)
            {
                b->worker_error = 1;
                return NULL;
            }
            p = &b->probes[token - 1];
            pthread_mutex_lock(&b->lock);
            if(p->active && p->fd >= 0)
            {
                ssize_t bytes = read(p->fd, &expirations, sizeof(expirations));
                if(bytes == (ssize_t)sizeof(expirations))
                {
                    now = monotonic_ns();
                    if(now == 0)
                    {
                        b->worker_error = 1;
                    }
                    else
                    {
                        if(p->n < SAMPLE_CAP)
                        {
                            p->stamps[p->n] = now;
                            p->lateness[p->n] = now > p->deadline ? now - p->deadline : 0;
                            p->n++;
                        }
                        /* A new timer period begins AFTER handling this callback.
                         * Do not use kernel periodic catch-up semantics. */
                        p->deadline = monotonic_ns() + b->period_ns;
                        if(arm(p) != 0) b->worker_error = 1;
                    }
                }
                else if(bytes < 0 && errno != EAGAIN && errno != EINTR)
                {
                    b->worker_error = 1;
                }
            }
            pthread_mutex_unlock(&b->lock);
            if(b->worker_error) return NULL;
        }
    }
}
static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}
static double percentile(const double *sorted, int count, int pct)
{
    int k = (count * pct + 99) / 100;
    if(k < 1) k = 1;
    if(k > count) k = count;
    return sorted[k - 1];
}
int main(int argc, char **argv)
{
    BENCH b;
    pthread_t worker;
    struct rusage usage;
    struct epoll_event event;
    PROBE *p;
    double *intervals = NULL, *lags = NULL, *deletions = NULL;
    uint64_t wall_a = 0, cpu_a = 0, wall_b = 0, cpu_b = 0;
    uint64_t close_a = 0, close_b = 0;
    int count, period, duration, mode_us;
    int before = -1, active = -1, after = -1;
    int i, j, created = 0, samples = 0, hits = 0, rc = EXIT_FAILURE;
    int mutex_initialized = 0, thread_started = 0;

    if(argc != 5 || (strcmp(argv[1], "ms") && strcmp(argv[1], "us")) ||
       parse(argv[2], 1, TIMER_LIMIT, &count) ||
       parse(argv[3], 1, 10000, &period) ||
       parse(argv[4], 250, 5000, &duration))
    {
        fprintf(stderr, "usage: %s ms|us timers(1..128) period(1..10000) duration_ms(250..5000)\n", argv[0]);
        return EXIT_FAILURE;
    }
    mode_us = strcmp(argv[1], "us") == 0;
    memset(&b, 0, sizeof(b));
    b.epfd = -1; b.stopfd = -1;
    b.count = count;
    b.period_ns = (uint64_t)period * (mode_us ? 1000ULL : 1000000ULL);
    b.probes = (PROBE *)calloc((size_t)count, sizeof(PROBE));
    deletions = (double *)calloc((size_t)count, sizeof(double));
    if(!b.probes || !deletions) goto CLEANUP;
    for(i = 0; i < count; ++i) b.probes[i].fd = -1;
    if(pthread_mutex_init(&b.lock, NULL) != 0) goto CLEANUP;
    mutex_initialized = 1;
    b.epfd = epoll_create1(EPOLL_CLOEXEC);
    b.stopfd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if(b.epfd < 0 || b.stopfd < 0) goto CLEANUP;
    memset(&event, 0, sizeof(event));
    event.events = EPOLLIN;
    event.data.u32 = 0;
    if(epoll_ctl(b.epfd, EPOLL_CTL_ADD, b.stopfd, &event) != 0) goto CLEANUP;

    before = threads();
    wall_a = monotonic_ns(); cpu_a = cpu_ns();
    if(!wall_a || !cpu_a) goto CLEANUP;
    for(i = 0; i < count; ++i)
    {
        p = &b.probes[i];
        p->fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
        if(p->fd < 0) goto CLEANUP;
        created++;
        p->active = 1;
        memset(&event, 0, sizeof(event));
        event.events = EPOLLIN;
        event.data.u32 = (uint32_t)(i + 1);
        if(epoll_ctl(b.epfd, EPOLL_CTL_ADD, p->fd, &event) != 0) goto CLEANUP;
        p->deadline = monotonic_ns() + b.period_ns;
        if(arm(p) != 0) goto CLEANUP;
    }
    if(pthread_create(&worker, NULL, dispatch, &b) != 0) goto CLEANUP;
    thread_started = 1;
    active = threads();
    sleep_ms(duration);

    for(i = 0; i < count; ++i)
    {
        uint64_t start = monotonic_ns(), end;
        if(!start) goto CLEANUP;
        pthread_mutex_lock(&b.lock);
        p = &b.probes[i];
        p->active = 0;
        if(epoll_ctl(b.epfd, EPOLL_CTL_DEL, p->fd, NULL) != 0)
        {
            pthread_mutex_unlock(&b.lock);
            goto CLEANUP;
        }
        if(close(p->fd) != 0)
        {
            pthread_mutex_unlock(&b.lock);
            goto CLEANUP;
        }
        p->fd = -1;
        pthread_mutex_unlock(&b.lock);
        end = monotonic_ns();
        if(end < start) goto CLEANUP;
        deletions[i] = (double)(end - start) / 1000.0;
    }
    /* Compare total dispatcher shutdown to the traditional post-delete Close.
     * These operations have different contracts, so report as diagnostics. */
    close_a = monotonic_ns();
    {
        uint64_t one = 1;
        if(write(b.stopfd, &one, sizeof(one)) != (ssize_t)sizeof(one)) goto CLEANUP;
    }
    pthread_join(worker, NULL);
    thread_started = 0;
    close_b = monotonic_ns();
    wall_b = monotonic_ns(); cpu_b = cpu_ns();
    after = threads_after_settle(before);
    if(b.worker_error || !wall_b || !cpu_b || wall_b <= wall_a || close_b < close_a)
        goto CLEANUP;

    intervals = (double *)malloc((size_t)count * (SAMPLE_CAP - 1) * sizeof(double));
    lags = (double *)malloc((size_t)count * SAMPLE_CAP * sizeof(double));
    if(!intervals || !lags) goto CLEANUP;
    for(i = 0; i < count; ++i)
    {
        p = &b.probes[i];
        hits += p->n;
        for(j = 0; j < p->n; ++j)
        {
            lags[0 + (hits - p->n + j)] = (double)p->lateness[j] / 1000.0;
            if(j > 0)
            {
                double err = ((double)(p->stamps[j] - p->stamps[j - 1]) -
                              (double)b.period_ns) / 1000.0;
                intervals[samples++] = err < 0 ? -err : err;
            }
        }
    }
    if(samples < 2 || hits < 2) goto CLEANUP;
    qsort(intervals, (size_t)samples, sizeof(double), cmp_double);
    qsort(lags, (size_t)hits, sizeof(double), cmp_double);
    qsort(deletions, (size_t)count, sizeof(double), cmp_double);
    memset(&usage, 0, sizeof(usage));
    (void)getrusage(RUSAGE_SELF, &usage);
    printf("timer_scale mode=%s timers=%d period=%d duration_ms=%d hits=%d intervals=%d "
           "abs_interarrival_error_p50_us=%.3f p95_us=%.3f p99_us=%.3f max_us=%.3f "
           "cpu_process_ms=%.3f wall_ms=%.3f cpu_pct_single_core=%.2f "
           "threads_before=%d threads_active=%d threads_after=%d ru_maxrss_kib=%ld "
           "delete_p50_us=%.3f delete_p95_us=%.3f delete_p99_us=%.3f "
           "delete_max_us=%.3f timer_close_us=%.3f "
           "deadline_lag_p50_us=%.3f deadline_lag_p95_us=%.3f deadline_lag_p99_us=%.3f\n",
           argv[1], count, period, duration, hits, samples,
           percentile(intervals, samples, 50), percentile(intervals, samples, 95),
           percentile(intervals, samples, 99), intervals[samples - 1],
           (double)(cpu_b - cpu_a) / 1e6, (double)(wall_b - wall_a) / 1e6,
           100.0 * (double)(cpu_b - cpu_a) / (double)(wall_b - wall_a),
           before, active, after, usage.ru_maxrss,
           percentile(deletions, count, 50), percentile(deletions, count, 95),
           percentile(deletions, count, 99), deletions[count - 1],
           (double)(close_b - close_a) / 1000.0,
           percentile(lags, hits, 50), percentile(lags, hits, 95),
           percentile(lags, hits, 99));
    rc = EXIT_SUCCESS;

CLEANUP:
    if(thread_started)
    {
        uint64_t one = 1;
        if(b.stopfd >= 0) (void)write(b.stopfd, &one, sizeof(one));
        pthread_join(worker, NULL);
    }
    for(i = 0; i < created; ++i)
        if(b.probes[i].fd >= 0) (void)close(b.probes[i].fd);
    if(b.stopfd >= 0) (void)close(b.stopfd);
    if(b.epfd >= 0) (void)close(b.epfd);
    if(mutex_initialized) pthread_mutex_destroy(&b.lock);
    free(b.probes); free(deletions); free(intervals); free(lags);
    return rc;
}
