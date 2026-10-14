#define _POSIX_C_SOURCE 200809L
/* Experimental bounded callback-executor contract MODEL, Linux-only.
 * No production timer integration, timerfd backend, or public API. */
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define TIMER_SLOTS 4
#define WORKERS 2
#define QUEUE_CAP 1

typedef void (*callback_t)(void *);
typedef enum {
    SUBMIT_OK = 0,
    SUBMIT_COALESCED,
    SUBMIT_FULL,
    SUBMIT_CLOSING,
    SUBMIT_CANCELLED
} SUBMIT_STATUS;

typedef struct {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    pthread_t workers[WORKERS];
    int worker_count;
    int closing;
    int joined;
    int queued[QUEUE_CAP];
    int pending;
    int busy[TIMER_SLOTS];
    int running[TIMER_SLOTS];
    int cancelled[TIMER_SLOTS];
    callback_t callbacks[TIMER_SLOTS];
    void *arguments[TIMER_SLOTS];
    unsigned long coalesced;
    unsigned long overflow;
    unsigned long discarded_on_close;
} EXEC;

typedef struct {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    int entered;
    int released;
    int calls;
} GATE;

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

static int threads_after_settle(int expected)
{
    int i;
    struct timespec delay = {0, 5000000L};
    int observed = thread_count();
    for(i = 0; observed > expected && i < 20; ++i)
    {
        nanosleep(&delay, NULL);
        observed = thread_count();
    }
    return observed;
}

static int flag_wait(pthread_mutex_t *mu, pthread_cond_t *cv, int *flag)
{
    struct timespec until;
    int status = 0;
    if(clock_gettime(CLOCK_REALTIME, &until) != 0) return -1;
    until.tv_sec += 3;
    pthread_mutex_lock(mu);
    while(!*flag && status == 0)
        status = pthread_cond_timedwait(cv, mu, &until);
    status = *flag ? 0 : -1;
    pthread_mutex_unlock(mu);
    return status;
}

static void *worker_fn(void *arg)
{
    EXEC *exec = arg;
    for(;;)
    {
        int id;
        callback_t callback;
        void *data;
        pthread_mutex_lock(&exec->mu);
        while(!exec->closing && exec->pending == 0)
            pthread_cond_wait(&exec->cv, &exec->mu);
        if(exec->closing)
        {
            pthread_mutex_unlock(&exec->mu);
            return NULL;
        }
        id = exec->queued[0];
        exec->pending--;
        if(exec->pending > 0)
            memmove(exec->queued, exec->queued + 1,
                    (size_t)exec->pending * sizeof(exec->queued[0]));
        exec->running[id] = 1;
        callback = exec->callbacks[id];
        data = exec->arguments[id];
        pthread_cond_broadcast(&exec->cv);
        pthread_mutex_unlock(&exec->mu);

        callback(data);

        pthread_mutex_lock(&exec->mu);
        exec->running[id] = 0;
        exec->busy[id] = 0;
        pthread_cond_broadcast(&exec->cv);
        pthread_mutex_unlock(&exec->mu);
    }
}

static int is_worker(EXEC *exec)
{
    int i;
    for(i = 0; i < exec->worker_count; ++i)
        if(pthread_equal(pthread_self(), exec->workers[i])) return 1;
    return 0;
}

static int exec_init(EXEC *exec)
{
    int i;
    memset(exec, 0, sizeof(*exec));
    if(pthread_mutex_init(&exec->mu, NULL) != 0) return -1;
    if(pthread_cond_init(&exec->cv, NULL) != 0)
    {
        pthread_mutex_destroy(&exec->mu);
        return -1;
    }
    for(i = 0; i < WORKERS; ++i)
    {
        if(pthread_create(&exec->workers[i], NULL, worker_fn, exec) != 0)
        {
            pthread_mutex_lock(&exec->mu);
            exec->closing = 1;
            pthread_cond_broadcast(&exec->cv);
            pthread_mutex_unlock(&exec->mu);
            while(exec->worker_count > 0)
                pthread_join(exec->workers[--exec->worker_count], NULL);
            pthread_cond_destroy(&exec->cv);
            pthread_mutex_destroy(&exec->mu);
            return -1;
        }
        exec->worker_count++;
    }
    return 0;
}

static SUBMIT_STATUS exec_submit(EXEC *exec, int id, callback_t cb, void *arg)
{
    SUBMIT_STATUS result;
    if(id < 0 || id >= TIMER_SLOTS || cb == NULL) return SUBMIT_CLOSING;
    pthread_mutex_lock(&exec->mu);
    if(exec->closing) result = SUBMIT_CLOSING;
    else if(exec->cancelled[id]) result = SUBMIT_CANCELLED;
    else if(exec->busy[id])
    {
        ++exec->coalesced;
        result = SUBMIT_COALESCED;
    }
    else if(exec->pending == QUEUE_CAP)
    {
        ++exec->overflow;
        result = SUBMIT_FULL;
    }
    else
    {
        exec->queued[exec->pending++] = id;
        exec->busy[id] = 1;
        exec->callbacks[id] = cb;
        exec->arguments[id] = arg;
        pthread_cond_signal(&exec->cv);
        result = SUBMIT_OK;
    }
    pthread_mutex_unlock(&exec->mu);
    return result;
}

/* External callers only: cancellation removes pending jobs and waits for any
 * running callback. The registered id cannot be resubmitted afterwards. */
static int exec_cancel(EXEC *exec, int id)
{
    int i, j;
    if(id < 0 || id >= TIMER_SLOTS) return EINVAL;
    if(is_worker(exec)) return EDEADLK;
    pthread_mutex_lock(&exec->mu);
    if(exec->closing)
    {
        pthread_mutex_unlock(&exec->mu);
        return ESHUTDOWN;
    }
    exec->cancelled[id] = 1;
    for(i = 0; i < exec->pending; ++i)
    {
        if(exec->queued[i] != id) continue;
        for(j = i + 1; j < exec->pending; ++j)
            exec->queued[j - 1] = exec->queued[j];
        exec->pending--;
        exec->busy[id] = 0;
        break;
    }
    pthread_cond_broadcast(&exec->cv);
    while(exec->running[id])
        pthread_cond_wait(&exec->cv, &exec->mu);
    pthread_mutex_unlock(&exec->mu);
    return 0;
}

/* Shutdown rejects new work, drops all pending callbacks, waits for running
 * callbacks, joins the workers. Only one external closer is permitted. */
static int exec_close(EXEC *exec)
{
    int i;
    if(is_worker(exec)) return EDEADLK;
    pthread_mutex_lock(&exec->mu);
    if(exec->closing)
    {
        pthread_mutex_unlock(&exec->mu);
        return EALREADY;
    }
    exec->closing = 1;
    exec->discarded_on_close += (unsigned long)exec->pending;
    for(i = 0; i < exec->pending; ++i)
        exec->busy[exec->queued[i]] = 0;
    exec->pending = 0;
    pthread_cond_broadcast(&exec->cv);
    pthread_mutex_unlock(&exec->mu);
    for(i = 0; i < exec->worker_count; ++i)
        pthread_join(exec->workers[i], NULL);
    exec->joined = 1;
    return 0;
}

static void exec_destroy(EXEC *exec)
{
    if(!exec->joined)
    {
        fprintf(stderr, "executor model: destroy without joined workers\n");
        abort();
    }
    pthread_cond_destroy(&exec->cv);
    pthread_mutex_destroy(&exec->mu);
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

static void gate_open(GATE *gate)
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

static void blocked_cb(void *data)
{
    GATE *gate = data;
    pthread_mutex_lock(&gate->mu);
    gate->calls++;
    gate->entered = 1;
    pthread_cond_broadcast(&gate->cv);
    while(!gate->released)
        pthread_cond_wait(&gate->cv, &gate->mu);
    pthread_mutex_unlock(&gate->mu);
}

static void signal_cb(void *data)
{
    GATE *gate = data;
    pthread_mutex_lock(&gate->mu);
    gate->calls++;
    gate->entered = 1;
    pthread_cond_broadcast(&gate->cv);
    pthread_mutex_unlock(&gate->mu);
}

typedef struct {
    EXEC *exec;
    int timer;
    int result;
    int done;
    pthread_mutex_t mu;
    pthread_cond_t cv;
} ASYNC_OP;

static int async_init(ASYNC_OP *op, EXEC *exec, int timer)
{
    memset(op, 0, sizeof(*op));
    op->exec = exec;
    op->timer = timer;
    if(pthread_mutex_init(&op->mu, NULL) != 0) return -1;
    if(pthread_cond_init(&op->cv, NULL) != 0)
    {
        pthread_mutex_destroy(&op->mu);
        return -1;
    }
    return 0;
}

static void async_destroy(ASYNC_OP *op)
{
    pthread_cond_destroy(&op->cv);
    pthread_mutex_destroy(&op->mu);
}

static void *cancel_fn(void *data)
{
    ASYNC_OP *op = data;
    int result = exec_cancel(op->exec, op->timer);
    pthread_mutex_lock(&op->mu);
    op->result = result;
    op->done = 1;
    pthread_cond_broadcast(&op->cv);
    pthread_mutex_unlock(&op->mu);
    return NULL;
}

static void *close_fn(void *data)
{
    ASYNC_OP *op = data;
    int result = exec_close(op->exec);
    pthread_mutex_lock(&op->mu);
    op->result = result;
    op->done = 1;
    pthread_cond_broadcast(&op->cv);
    pthread_mutex_unlock(&op->mu);
    return NULL;
}

static int wait_cancelled(EXEC *exec, int id)
{
    return flag_wait(&exec->mu, &exec->cv, &exec->cancelled[id]);
}
static int wait_closing(EXEC *exec)
{
    return flag_wait(&exec->mu, &exec->cv, &exec->closing);
}

/* A slow callback cannot block an unrelated timer while one worker is free. */
static int test_slow_callback_isolation(void)
{
    EXEC exec;
    GATE slow, fast;
    int ok = 0;
    if(gate_init(&slow) != 0) return -1;
    if(gate_init(&fast) != 0) { gate_destroy(&slow); return -1; }
    if(exec_init(&exec) != 0) goto GATES;
    if(exec_submit(&exec, 0, blocked_cb, &slow) != SUBMIT_OK ||
       flag_wait(&slow.mu, &slow.cv, &slow.entered) != 0 ||
       exec_submit(&exec, 0, blocked_cb, &slow) != SUBMIT_COALESCED ||
       exec_submit(&exec, 1, signal_cb, &fast) != SUBMIT_OK ||
       flag_wait(&fast.mu, &fast.cv, &fast.entered) != 0 ||
       fast.calls != 1 || exec.coalesced != 1)
        goto CLEANUP;
    ok = 1;
CLEANUP:
    gate_open(&slow);
    if(exec_close(&exec) != 0) ok = 0;
    exec_destroy(&exec);
GATES:
    gate_destroy(&fast);
    gate_destroy(&slow);
    return ok ? 0 : -1;
}

static int test_cancel_running_and_reject_resubmit(void)
{
    EXEC exec;
    GATE gate;
    ASYNC_OP op;
    pthread_t canceller;
    int ok = 0, thread_started = 0;
    if(gate_init(&gate) != 0) return -1;
    if(exec_init(&exec) != 0) goto GATE_ONLY;
    if(async_init(&op, &exec, 0) != 0) goto CLEANUP;
    if(exec_submit(&exec, 0, blocked_cb, &gate) != SUBMIT_OK ||
       flag_wait(&gate.mu, &gate.cv, &gate.entered) != 0 ||
       pthread_create(&canceller, NULL, cancel_fn, &op) != 0)
        goto ASYNC;
    thread_started = 1;
    if(wait_cancelled(&exec, 0) != 0 ||
       exec_submit(&exec, 0, blocked_cb, &gate) != SUBMIT_CANCELLED)
        goto ASYNC;
    pthread_mutex_lock(&op.mu);
    if(op.done) { pthread_mutex_unlock(&op.mu); goto ASYNC; }
    pthread_mutex_unlock(&op.mu);
    ok = 1;
ASYNC:
    gate_open(&gate);
    if(thread_started)
    {
        pthread_join(canceller, NULL);
        if(op.result != 0 || !op.done) ok = 0;
    }
    async_destroy(&op);
CLEANUP:
    if(exec_close(&exec) != 0) ok = 0;
    exec_destroy(&exec);
GATE_ONLY:
    gate_destroy(&gate);
    return ok ? 0 : -1;
}

static int test_queue_full_cancel_and_close_drop(void)
{
    EXEC exec;
    GATE first, second, queued, rejected;
    ASYNC_OP closer;
    pthread_t closer_thread;
    int ok = 0, started = 0;
    if(gate_init(&first) != 0) return -1;
    if(gate_init(&second) != 0) goto FIRST;
    if(gate_init(&queued) != 0) goto SECOND;
    if(gate_init(&rejected) != 0) goto QUEUED;
    if(exec_init(&exec) != 0) goto GATES;
    if(async_init(&closer, &exec, -1) != 0) goto CLOSE_DIRECT;
    if(exec_submit(&exec, 0, blocked_cb, &first) != SUBMIT_OK ||
       flag_wait(&first.mu, &first.cv, &first.entered) != 0 ||
       exec_submit(&exec, 1, blocked_cb, &second) != SUBMIT_OK ||
       flag_wait(&second.mu, &second.cv, &second.entered) != 0 ||
       exec_submit(&exec, 2, signal_cb, &queued) != SUBMIT_OK ||
       exec_submit(&exec, 2, signal_cb, &queued) != SUBMIT_COALESCED ||
       exec_submit(&exec, 3, signal_cb, &rejected) != SUBMIT_FULL ||
       exec_cancel(&exec, 2) != 0 ||
       exec_submit(&exec, 3, signal_cb, &rejected) != SUBMIT_OK ||
       pthread_create(&closer_thread, NULL, close_fn, &closer) != 0)
        goto OP;
    started = 1;
    if(wait_closing(&exec) != 0 ||
       exec_submit(&exec, 2, signal_cb, &queued) != SUBMIT_CLOSING ||
       exec.overflow != 1 || exec.coalesced != 1)
        goto OP;
    pthread_mutex_lock(&closer.mu);
    if(closer.done) { pthread_mutex_unlock(&closer.mu); goto OP; }
    pthread_mutex_unlock(&closer.mu);
    ok = 1;
OP:
    gate_open(&first);
    gate_open(&second);
    if(started)
    {
        pthread_join(closer_thread, NULL);
        if(closer.result != 0 || !closer.done ||
           exec.discarded_on_close != 1 || queued.calls != 0 || rejected.calls != 0)
            ok = 0;
    }
    else if(exec_close(&exec) != 0) ok = 0;
    async_destroy(&closer);
    exec_destroy(&exec);
    goto GATES;
CLOSE_DIRECT:
    gate_open(&first);
    gate_open(&second);
    if(exec_close(&exec) != 0) ok = 0;
    exec_destroy(&exec);
GATES:
    gate_destroy(&rejected);
QUEUED:
    gate_destroy(&queued);
SECOND:
    gate_destroy(&second);
FIRST:
    gate_destroy(&first);
    return ok ? 0 : -1;
}

typedef struct {
    EXEC *exec;
    GATE finished;
    int close_result;
    int cancel_result;
} REENTRANT;
static void callback_reentrant(void *data)
{
    REENTRANT *probe = data;
    probe->close_result = exec_close(probe->exec);
    probe->cancel_result = exec_cancel(probe->exec, 0);
    signal_cb(&probe->finished);
}
static int test_worker_reentrant_shutdown_rejected(void)
{
    EXEC exec;
    REENTRANT probe;
    int ok = 0;
    memset(&probe, 0, sizeof(probe));
    if(gate_init(&probe.finished) != 0) return -1;
    if(exec_init(&exec) != 0) goto GATE;
    probe.exec = &exec;
    if(exec_submit(&exec, 0, callback_reentrant, &probe) == SUBMIT_OK &&
       flag_wait(&probe.finished.mu, &probe.finished.cv,
                 &probe.finished.entered) == 0 &&
       probe.close_result == EDEADLK && probe.cancel_result == EDEADLK)
        ok = 1;
    if(exec_close(&exec) != 0) ok = 0;
    exec_destroy(&exec);
GATE:
    gate_destroy(&probe.finished);
    return ok ? 0 : -1;
}

int main(void)
{
    int i, before = thread_count();
    if(before < 1) return EXIT_FAILURE;
    for(i = 0; i < 20; ++i)
    {
        if(test_slow_callback_isolation() != 0)
        {
            fprintf(stderr, "executor: slow isolation failed (iteration %d)\n", i);
            return EXIT_FAILURE;
        }
        if(test_cancel_running_and_reject_resubmit() != 0)
        {
            fprintf(stderr, "executor: active cancel failed (iteration %d)\n", i);
            return EXIT_FAILURE;
        }
        if(test_queue_full_cancel_and_close_drop() != 0)
        {
            fprintf(stderr, "executor: bounded queue/close failed (iteration %d)\n", i);
            return EXIT_FAILURE;
        }
        if(test_worker_reentrant_shutdown_rejected() != 0)
        {
            fprintf(stderr, "executor: reentrant safety failed (iteration %d)\n", i);
            return EXIT_FAILURE;
        }
        if(threads_after_settle(before) != before)
        {
            fprintf(stderr, "executor: leaked worker threads in iteration %d\n", i);
            return EXIT_FAILURE;
        }
    }
    puts("executor model: 20x isolation, coalescing, overflow, cancel, close, reentrancy and thread cleanup PASS");
    return EXIT_SUCCESS;
}
