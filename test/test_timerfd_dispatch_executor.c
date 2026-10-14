#define _GNU_SOURCE
/* Experimental Linux-only end-to-end timerfd -> epoll -> bounded callback
 * workers regression. This is a test fixture, NOT a timer API or backend. */
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

#define TIMER_IDS 4
#define WORKER_COUNT 2
#define QUEUE_SIZE 1
#define ITERATIONS 20

typedef void (*callback_fn)(void *);
typedef enum {
    DISPATCH_ACCEPTED = 0,
    DISPATCH_COALESCED,
    DISPATCH_FULL,
    DISPATCH_CANCELLED,
    DISPATCH_CLOSING
} DISPATCH_STATUS;

typedef struct {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    pthread_t workers[WORKER_COUNT];
    pthread_t dispatcher;
    int worker_started;
    int dispatcher_started;
    int epfd;
    int stopfd;
    int timers[TIMER_IDS];
    int closing;
    int pending;
    int queued;
    int busy[TIMER_IDS];
    int running[TIMER_IDS];
    int cancelled[TIMER_IDS];
    int observations[TIMER_IDS];
    DISPATCH_STATUS last_status[TIMER_IDS];
    callback_fn callbacks[TIMER_IDS];
    void *args[TIMER_IDS];
    unsigned long coalesced, overflow, cancelled_dispatches;
    int dispatcher_error;
} CORE;

typedef struct {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    int entered;
    int released;
    int calls;
} GATE;

static int fd_count(void)
{
    DIR *d = opendir("/proc/self/fd");
    struct dirent *entry;
    int count = 0;
    if(d == NULL) return -1;
    while((entry = readdir(d)) != NULL)
        if(strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
            ++count;
    closedir(d);
    return count;
}
static int thread_count(void)
{
    FILE *f = fopen("/proc/self/status", "r");
    char line[128];
    int count = -1;
    if(f == NULL) return -1;
    while(fgets(line, sizeof(line), f) != NULL)
        if(sscanf(line, "Threads: %d", &count) == 1) break;
    fclose(f);
    return count;
}
static int threads_after_settle(int baseline)
{
    struct timespec pause = {0, 5000000L};
    int i, count = thread_count();
    for(i = 0; i < 20 && count > baseline; ++i)
    {
        nanosleep(&pause, NULL);
        count = thread_count();
    }
    return count;
}
static int timed_wait(pthread_mutex_t *mu, pthread_cond_t *cv, int *value,
                      int target)
{
    struct timespec deadline;
    int ret = 0;
    if(clock_gettime(CLOCK_REALTIME, &deadline) != 0) return -1;
    deadline.tv_sec += 3;
    pthread_mutex_lock(mu);
    while(*value < target && ret == 0)
        ret = pthread_cond_timedwait(cv, mu, &deadline);
    ret = *value >= target ? 0 : -1;
    pthread_mutex_unlock(mu);
    return ret;
}
static int gate_init(GATE *gate)
{
    memset(gate, 0, sizeof(*gate));
    if(pthread_mutex_init(&gate->mu, NULL) != 0) return -1;
    if(pthread_cond_init(&gate->cv, NULL) != 0)
    {
        pthread_mutex_destroy(&gate->mu);
        return -1;
    }
    return 0;
}
static void gate_release(GATE *gate)
{
    pthread_mutex_lock(&gate->mu);
    gate->released = 1;
    pthread_cond_broadcast(&gate->cv);
    pthread_mutex_unlock(&gate->mu);
}
static void gate_destroy(GATE *gate)
{
    pthread_cond_destroy(&gate->cv);
    pthread_mutex_destroy(&gate->mu);
}
static void gated_callback(void *arg)
{
    GATE *gate = arg;
    pthread_mutex_lock(&gate->mu);
    gate->entered = 1;
    ++gate->calls;
    pthread_cond_broadcast(&gate->cv);
    while(!gate->released)
        pthread_cond_wait(&gate->cv, &gate->mu);
    pthread_mutex_unlock(&gate->mu);
}
static void immediate_callback(void *arg)
{
    GATE *gate = arg;
    pthread_mutex_lock(&gate->mu);
    gate->entered = 1;
    ++gate->calls;
    pthread_cond_broadcast(&gate->cv);
    pthread_mutex_unlock(&gate->mu);
}
static void *worker_main(void *arg)
{
    CORE *core = arg;
    for(;;)
    {
        int id;
        callback_fn cb;
        void *cb_arg;
        pthread_mutex_lock(&core->mu);
        while(!core->closing && !core->pending)
            pthread_cond_wait(&core->cv, &core->mu);
        if(core->closing)
        {
            pthread_mutex_unlock(&core->mu);
            return NULL;
        }
        id = core->queued;
        core->pending = 0;
        core->running[id] = 1;
        cb = core->callbacks[id];
        cb_arg = core->args[id];
        pthread_cond_broadcast(&core->cv);
        pthread_mutex_unlock(&core->mu);
        cb(cb_arg);
        pthread_mutex_lock(&core->mu);
        core->running[id] = 0;
        core->busy[id] = 0;
        pthread_cond_broadcast(&core->cv);
        pthread_mutex_unlock(&core->mu);
    }
}
static void record_expiration(CORE *core, int id)
{
    DISPATCH_STATUS status;
    pthread_mutex_lock(&core->mu);
    if(core->closing) status = DISPATCH_CLOSING;
    else if(core->cancelled[id])
    {
        status = DISPATCH_CANCELLED;
        ++core->cancelled_dispatches;
    }
    else if(core->busy[id])
    {
        status = DISPATCH_COALESCED;
        ++core->coalesced;
    }
    else if(core->pending == QUEUE_SIZE)
    {
        status = DISPATCH_FULL;
        ++core->overflow;
    }
    else
    {
        core->queued = id;
        core->pending = 1;
        core->busy[id] = 1;
        status = DISPATCH_ACCEPTED;
        pthread_cond_signal(&core->cv);
    }
    core->last_status[id] = status;
    ++core->observations[id];
    pthread_cond_broadcast(&core->cv);
    pthread_mutex_unlock(&core->mu);
}
static void *dispatcher_main(void *arg)
{
    CORE *core = arg;
    struct epoll_event events[TIMER_IDS + 1];
    for(;;)
    {
        int i, count = epoll_wait(core->epfd, events, TIMER_IDS + 1, -1);
        if(count < 0 && errno == EINTR) continue;
        if(count < 0) break;
        for(i = 0; i < count; ++i)
        {
            uint64_t expirations;
            unsigned int tag = events[i].data.u32;
            if(tag == 0) return NULL;
            if(tag > TIMER_IDS ||
               read(core->timers[tag - 1], &expirations, sizeof(expirations)) !=
                   (ssize_t)sizeof(expirations))
                goto ERROR;
            record_expiration(core, (int)tag - 1);
        }
    }
ERROR:
    pthread_mutex_lock(&core->mu);
    core->dispatcher_error = 1;
    pthread_cond_broadcast(&core->cv);
    pthread_mutex_unlock(&core->mu);
    return NULL;
}
static void core_shutdown(CORE *core)
{
    int i;
    if(core->dispatcher_started)
    {
        uint64_t one = 1;
        ssize_t status;
        do status = write(core->stopfd, &one, sizeof(one));
        while(status < 0 && errno == EINTR);
        if(status != (ssize_t)sizeof(one))
            fprintf(stderr, "timerfd experiment: wakeup failed\n");
        pthread_join(core->dispatcher, NULL);
    }
    pthread_mutex_lock(&core->mu);
    core->closing = 1;
    core->pending = 0;
    pthread_cond_broadcast(&core->cv);
    pthread_mutex_unlock(&core->mu);
    for(i = 0; i < core->worker_started; ++i)
        pthread_join(core->workers[i], NULL);
    for(i = 0; i < TIMER_IDS; ++i)
        if(core->timers[i] >= 0) close(core->timers[i]);
    if(core->stopfd >= 0) close(core->stopfd);
    if(core->epfd >= 0) close(core->epfd);
    pthread_cond_destroy(&core->cv);
    pthread_mutex_destroy(&core->mu);
}
static int core_init(CORE *core, callback_fn *callbacks, void **args)
{
    int i;
    struct epoll_event ev;
    memset(core, 0, sizeof(*core));
    core->epfd = -1;
    core->stopfd = -1;
    for(i = 0; i < TIMER_IDS; ++i) core->timers[i] = -1;
    if(pthread_mutex_init(&core->mu, NULL) != 0) return -1;
    if(pthread_cond_init(&core->cv, NULL) != 0)
    {
        pthread_mutex_destroy(&core->mu);
        return -1;
    }
    core->epfd = epoll_create1(EPOLL_CLOEXEC);
    if(core->epfd < 0) goto FAIL;
    core->stopfd = eventfd(0, EFD_CLOEXEC);
    if(core->stopfd < 0) goto FAIL;
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.u32 = 0;
    if(epoll_ctl(core->epfd, EPOLL_CTL_ADD, core->stopfd, &ev) != 0)
        goto FAIL;
    for(i = 0; i < TIMER_IDS; ++i)
    {
        core->callbacks[i] = callbacks[i];
        core->args[i] = args[i];
        core->timers[i] = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
        if(core->timers[i] < 0) goto FAIL;
        ev.data.u32 = (unsigned int)i + 1;
        if(epoll_ctl(core->epfd, EPOLL_CTL_ADD, core->timers[i], &ev) != 0)
            goto FAIL;
    }
    for(i = 0; i < WORKER_COUNT; ++i)
    {
        if(pthread_create(&core->workers[i], NULL, worker_main, core) != 0)
            goto FAIL;
        ++core->worker_started;
    }
    if(pthread_create(&core->dispatcher, NULL, dispatcher_main, core) != 0)
        goto FAIL;
    core->dispatcher_started = 1;
    return 0;
FAIL:
    core_shutdown(core);
    return -1;
}
static int core_arm(CORE *core, int id)
{
    struct itimerspec spec;
    if(id < 0 || id >= TIMER_IDS) return -1;
    memset(&spec, 0, sizeof(spec));
    spec.it_value.tv_nsec = 2000000L; /* one shot, 2ms */
    return timerfd_settime(core->timers[id], 0, &spec, NULL);
}
static int wait_observation(CORE *core, int id, int target,
                            DISPATCH_STATUS expected)
{
    int status = timed_wait(&core->mu, &core->cv, &core->observations[id], target);
    if(status != 0) return -1;
    pthread_mutex_lock(&core->mu);
    status = (core->observations[id] == target &&
              core->last_status[id] == expected && !core->dispatcher_error) ? 0 : -1;
    pthread_mutex_unlock(&core->mu);
    return status;
}
static int wait_idle(CORE *core, int id)
{
    struct timespec deadline;
    int rc = 0;
    if(clock_gettime(CLOCK_REALTIME, &deadline) != 0) return -1;
    deadline.tv_sec += 3;
    pthread_mutex_lock(&core->mu);
    while(core->busy[id] && rc == 0)
        rc = pthread_cond_timedwait(&core->cv, &core->mu, &deadline);
    rc = core->busy[id] ? -1 : 0;
    pthread_mutex_unlock(&core->mu);
    return rc;
}
static int cancel_pending(CORE *core, int id)
{
    pthread_mutex_lock(&core->mu);
    if(core->running[id] || core->closing)
    {
        pthread_mutex_unlock(&core->mu);
        return -1;
    }
    core->cancelled[id] = 1;
    if(core->pending && core->queued == id)
    {
        core->pending = 0;
        core->busy[id] = 0;
    }
    pthread_mutex_unlock(&core->mu);
    return 0;
}
static int test_case(void)
{
    CORE core;
    GATE gates[TIMER_IDS];
    callback_fn callbacks[TIMER_IDS] = {
        gated_callback, immediate_callback, gated_callback, immediate_callback
    };
    void *args[TIMER_IDS];
    int i, result = -1;
    for(i = 0; i < TIMER_IDS; ++i)
    {
        if(gate_init(&gates[i]) != 0)
        {
            while(i > 0) gate_destroy(&gates[--i]);
            return -1;
        }
        args[i] = &gates[i];
    }
    if(core_init(&core, callbacks, args) != 0) goto GATES;
    /* Timer 0's worker blocks; timer 1 must complete via worker 2. */
    if(core_arm(&core, 0) != 0 ||
       wait_observation(&core, 0, 1, DISPATCH_ACCEPTED) != 0 ||
       timed_wait(&gates[0].mu, &gates[0].cv, &gates[0].entered, 1) != 0 ||
       core_arm(&core, 1) != 0 ||
       wait_observation(&core, 1, 1, DISPATCH_ACCEPTED) != 0 ||
       timed_wait(&gates[1].mu, &gates[1].cv, &gates[1].entered, 1) != 0 ||
       wait_idle(&core, 1) != 0)
       goto DONE;
    /* Both workers now blocked. Queue size remains exactly one. */
    if(core_arm(&core, 2) != 0 ||
       wait_observation(&core, 2, 1, DISPATCH_ACCEPTED) != 0 ||
       timed_wait(&gates[2].mu, &gates[2].cv, &gates[2].entered, 1) != 0 ||
       core_arm(&core, 3) != 0 ||
       wait_observation(&core, 3, 1, DISPATCH_ACCEPTED) != 0 ||
       core_arm(&core, 1) != 0 ||
       wait_observation(&core, 1, 2, DISPATCH_FULL) != 0 ||
       core_arm(&core, 0) != 0 ||
       wait_observation(&core, 0, 2, DISPATCH_COALESCED) != 0 ||
       cancel_pending(&core, 3) != 0 ||
       core_arm(&core, 3) != 0 ||
       wait_observation(&core, 3, 2, DISPATCH_CANCELLED) != 0)
       goto DONE;
    if(core.coalesced != 1 || core.overflow != 1 ||
       core.cancelled_dispatches != 1 || gates[1].calls != 1 ||
       gates[3].calls != 0 || core.pending != 0)
       goto DONE;
    result = 0;
DONE:
    gate_release(&gates[0]);
    gate_release(&gates[2]);
    core_shutdown(&core);
    if(gates[0].calls != 1 || gates[2].calls != 1 ||
       gates[1].calls != 1 || gates[3].calls != 0)
       result = -1;
GATES:
    for(i = 0; i < TIMER_IDS; ++i) gate_destroy(&gates[i]);
    return result;
}
int main(void)
{
    int i;
    const int fds = fd_count(), threads = thread_count();
    if(fds < 1 || threads < 1) return EXIT_FAILURE;
    for(i = 0; i < ITERATIONS; ++i)
    {
        if(test_case() != 0)
        {
            fprintf(stderr, "timerfd integrated regression failed in iteration %d\n", i);
            return EXIT_FAILURE;
        }
        if(fd_count() != fds || threads_after_settle(threads) != threads)
        {
            fprintf(stderr, "timerfd integrated resources leaked in iteration %d\n", i);
            return EXIT_FAILURE;
        }
    }
    puts("timerfd integrated fixture: 20x real expiry, callback isolation, saturation, coalescing, cancellation and resource cleanup PASS");
    return EXIT_SUCCESS;
}
