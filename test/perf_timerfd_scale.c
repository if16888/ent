#define _GNU_SOURCE
/* Experimental Linux-only timerfd/epoll/eventfd scalability baseline.
 * Not part of the ent timer runtime or its public API. Timers rearm after
 * callback accounting to retain the existing fixed-delay semantics.
 * This prototype has one dispatcher thread, not a thread per timer. */
#include <dirent.h>
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
    /* Test-only injected callback gate. Never active during benchmarks. */
    int hol_test;
    int hol_entered;
    int hol_release;
    int hol_other_seen;
    pthread_mutex_t hol_lock;
    pthread_cond_t hol_cv;
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
static int open_fds(void)
{
    DIR *dir = opendir("/proc/self/fd");
    struct dirent *entry;
    int count = 0;
    if(dir == NULL) return -1;
    while((entry = readdir(dir)) != NULL)
    {
        if(strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
            count++;
    }
    closedir(dir);
    return count;
}

static int wait_hol(BENCH *b, int *flag)
{
    struct timespec until;
    int status = 0;
    if(clock_gettime(CLOCK_REALTIME, &until) != 0) return -1;
    until.tv_sec += 2;
    pthread_mutex_lock(&b->hol_lock);
    while(!*flag)
    {
        status = pthread_cond_timedwait(&b->hol_cv, &b->hol_lock, &until);
        if(status != 0) break;
    }
    status = *flag ? 0 : -1;
    pthread_mutex_unlock(&b->hol_lock);
    return status;
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
                        /*
                         * A deterministic, test-only stand-in for a callback
                         * blocked inside a single shared dispatcher. The
                         * owner arms timer 2 while timer 1 is blocked, then
                         * verifies timer 2 could not be serviced on time.
                         * Benchmark mode never enters either test branch.
                         */
                        if(b->hol_test && token == 1)
                        {
                            pthread_mutex_lock(&b->hol_lock);
                            if(!b->hol_entered)
                            {
                                b->hol_entered = 1;
                                pthread_cond_broadcast(&b->hol_cv);
                                while(!b->hol_release)
                                    pthread_cond_wait(&b->hol_cv, &b->hol_lock);
                            }
                            pthread_mutex_unlock(&b->hol_lock);
                        }
                        if(b->hol_test && token == 2)
                        {
                            pthread_mutex_lock(&b->hol_lock);
                            b->hol_other_seen = 1;
                            pthread_cond_broadcast(&b->hol_cv);
                            pthread_mutex_unlock(&b->hol_lock);
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
/*
 * Diagnose head-of-line blocking in the existing *benchmark prototype*.
 * This is not a production callback API, nor a lifecycle-equivalence test.
 * Timer 1 is paused inside dispatch; only then do we arm timer 2. We expect
 * timer 2 to expire but not be observed until timer 1's gate is released.
 */
static int test_dispatch_head_of_line(void)
{
    BENCH b;
    PROBE probes[2];
    struct epoll_event event;
    pthread_t worker;
    uint64_t delayed_ns = 0;
    int first_fds = open_fds();
    int first_threads = threads();
    int last_fds, last_threads;
    int lock_init = 0, hol_lock_init = 0, hol_cv_init = 0;
    int started = 0, success = 0;
    int i;

    memset(&b, 0, sizeof(b));
    memset(probes, 0, sizeof(probes));
    b.epfd = -1;
    b.stopfd = -1;
    b.count = 2;
    b.period_ns = 1000000ULL;
    b.probes = probes;
    b.hol_test = 1;
    for(i = 0; i < 2; ++i) probes[i].fd = -1;
    if(first_fds < 0 || first_threads < 0) goto CLEANUP;
    if(pthread_mutex_init(&b.lock, NULL) != 0) goto CLEANUP;
    lock_init = 1;
    if(pthread_mutex_init(&b.hol_lock, NULL) != 0) goto CLEANUP;
    hol_lock_init = 1;
    if(pthread_cond_init(&b.hol_cv, NULL) != 0) goto CLEANUP;
    hol_cv_init = 1;

    b.epfd = epoll_create1(EPOLL_CLOEXEC);
    b.stopfd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if(b.epfd < 0 || b.stopfd < 0) goto CLEANUP;
    memset(&event, 0, sizeof(event));
    event.events = EPOLLIN;
    event.data.u32 = 0;
    if(epoll_ctl(b.epfd, EPOLL_CTL_ADD, b.stopfd, &event) != 0) goto CLEANUP;
    for(i = 0; i < 2; ++i)
    {
        PROBE *p = &probes[i];
        p->fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
        if(p->fd < 0) goto CLEANUP;
        p->active = 1;
        memset(&event, 0, sizeof(event));
        event.events = EPOLLIN;
        event.data.u32 = (uint32_t)(i + 1);
        if(epoll_ctl(b.epfd, EPOLL_CTL_ADD, p->fd, &event) != 0) goto CLEANUP;
    }
    /* Only timer 1 is armed initially. Timer 2 cannot precede its gate. */
    probes[0].deadline = monotonic_ns() + b.period_ns;
    if(arm(&probes[0]) != 0) goto CLEANUP;
    if(pthread_create(&worker, NULL, dispatch, &b) != 0) goto CLEANUP;
    started = 1;
    if(wait_hol(&b, &b.hol_entered) != 0)
    {
        fprintf(stderr, "timerfd HOL: timer 1 did not enter callback gate\n");
        goto CLEANUP;
    }
    probes[1].deadline = monotonic_ns() + b.period_ns;
    if(arm(&probes[1]) != 0) goto CLEANUP;
    sleep_ms(30);

    pthread_mutex_lock(&b.hol_lock);
    /* Timer 2 cannot have been processed while the only worker is gated. */
    if(b.hol_other_seen)
    {
        pthread_mutex_unlock(&b.hol_lock);
        fprintf(stderr, "timerfd HOL: unrelated timer ran during gated callback\n");
        goto CLEANUP;
    }
    b.hol_release = 1;
    pthread_cond_broadcast(&b.hol_cv);
    pthread_mutex_unlock(&b.hol_lock);
    if(wait_hol(&b, &b.hol_other_seen) != 0)
    {
        fprintf(stderr, "timerfd HOL: timer 2 was not processed after release\n");
        goto CLEANUP;
    }
    success = 1;

CLEANUP:
    if(started)
    {
        uint64_t one = 1;
        pthread_mutex_lock(&b.hol_lock);
        b.hol_release = 1;
        pthread_cond_broadcast(&b.hol_cv);
        pthread_mutex_unlock(&b.hol_lock);
        if(b.stopfd >= 0 && write(b.stopfd, &one, sizeof(one)) != (ssize_t)sizeof(one))
        {
            fprintf(stderr, "timerfd HOL: failed to signal worker stop\n");
        }
        pthread_join(worker, NULL);
        if(b.worker_error || probes[1].n == 0)
            success = 0;
        else
            delayed_ns = probes[1].lateness[0];
    }
    for(i = 0; i < 2; ++i)
        if(probes[i].fd >= 0) (void)close(probes[i].fd);
    if(b.stopfd >= 0) (void)close(b.stopfd);
    if(b.epfd >= 0) (void)close(b.epfd);
    if(hol_cv_init) pthread_cond_destroy(&b.hol_cv);
    if(hol_lock_init) pthread_mutex_destroy(&b.hol_lock);
    if(lock_init) pthread_mutex_destroy(&b.lock);
    last_fds = open_fds();
    last_threads = threads_after_settle(first_threads);
    if(first_fds != last_fds || first_threads != last_threads)
    {
        fprintf(stderr, "timerfd HOL: resource leak: fd %d->%d, threads %d->%d\n",
                first_fds, last_fds, first_threads, last_threads);
        success = 0;
    }
    if(delayed_ns < 15000000ULL)
    {
        fprintf(stderr, "timerfd HOL: expected 15ms+ stall, got %.3fms\n",
                (double)delayed_ns / 1e6);
        success = 0;
    }
    if(success)
        printf("timerfd HOL reproduced: other_timer_lag_ms=%.3f fds=%d threads=%d\n",
               (double)delayed_ns / 1e6, last_fds, last_threads);
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
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

    if(argc == 2 && strcmp(argv[1], "--selftest-hol") == 0)
        return test_dispatch_head_of_line();

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
        if(b.stopfd >= 0 && write(b.stopfd, &one, sizeof(one)) != (ssize_t)sizeof(one))
        {
            fprintf(stderr, "timerfd scale: failed to signal worker stop\n");
        }
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
