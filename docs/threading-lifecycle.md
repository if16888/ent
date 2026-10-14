# Threading and thread-pool lifecycle semantics

This document records the current lifecycle contract for the low-level thread, lock, condition-variable, and thread-pool APIs.

## Locks and condition variables

### `UTL_LockClose()` / `UTL_CVClose()`

`UTL_LockClose(&lock)` and `UTL_CVClose(&cv)` are the only close APIs.

```c
UTL_LockClose(&lock);
UTL_CVClose(&cv);
```

These APIs validate the pointer-to-handle argument and set the caller-owned handle to `NULL` after successful close.

They do not make it safe to close a lock or condition variable while another thread is actively using it. The caller must still guarantee that no thread is blocked on or holding the object when it is closed.

## Thread handles

### `ENT_ThreadClose()`

`ENT_ThreadClose()` has join-all semantics.

It waits for every still-registered joinable thread under the `ENT_THREAD` service handle to finish, then releases the internal thread records and the service handle.

`ENT_ThreadClose()` also waits for public thread-service calls that have already
entered the service lifecycle guard. This drain guarantee does not make the raw
handle safe for arbitrary close/new-entry races. The owner must prevent new
`ENT_ThreadDetachCreate()`, `ENT_ThreadCreate()`, or `ENT_ThreadWaitById()` calls
once close begins. A copied raw handle is invalid after close returns.

Important behavior:

- it does not cancel running threads;
- it does not interrupt blocked thread functions;
- it has no timeout;
- if a registered thread never exits, `ENT_ThreadClose()` can block indefinitely.
- callers that expose the handle to multiple threads need an owner lock that
  serializes shutdown against new public API entry.

Use `ENT_ThreadWaitById()` when the caller needs to wait for a specific thread earlier in the lifecycle. Use a positive timeout with `ENT_ThreadWaitById()` when the caller needs a bounded wait.

## Thread pool

### `UTL_TPoolSubmit()` (preferred)

Use `UTL_TPoolSubmit(pool, taskCb, endCb, taskData)` for asynchronous tasks.
The result is owned by the library and passed to `endCb` through a borrowed
`MSG_ID_T*` pointer. The pointer is valid only until `endCb` returns.
This avoids the common use-after-return error of passing the address of a
short-lived local variable. `taskData` and callback functions must still
remain valid until completion. Normal pool close drains submitted work.

### `UTL_TPoolAddTask()` (compatibility)

`UTL_TPoolAddTask()` stores the caller-provided `MSG_ID_T* retVal` pointer and writes the task return value to it when the worker runs the task.

`UTL_TPoolInit()` limits the pending queue to
`UTL_TPOOL_DEFAULT_MAX_PENDING_TASKS` (4096) tasks. Use
`UTL_TPoolInitEx(pool, workers, maxPendingTasks)` to choose a positive limit.
When all pending slots are occupied, `UTL_TPoolAddTask()` returns
`ENT_TPL_QUEUE_FULL`; it does not block waiting for capacity. If a worker
fails at runtime, further submissions return `ENT_TPL_WORKER_FAILED`.

The caller must guarantee that:

- `retVal` remains valid until the task has completed;
- `taskData` remains valid until the task callback and optional end callback have completed;
- `taskEndCb`, if provided, can safely observe both `taskData` and `retVal`.

Passing a stack variable as `retVal` is safe only if the stack frame remains alive until task completion or until the pool has been closed after the task has run.

### `UTL_TPoolClose()`

`UTL_TPoolClose()` is a close-and-join operation for the pool workers.

Current semantics:

- new task submissions are rejected once close begins;
- tasks already running are allowed to complete;
- tasks accepted before close begins are drained and their end callbacks run;
- workers are woken and joined;
- the pool handle must not be reused after close returns.

If joining a worker or closing the underlying thread service returns an error,
`UTL_TPoolClose()` returns that error and leaves the caller's pool handle valid
in the `CLOSING` state. Submissions remain rejected; call
`UTL_TPoolClose()` again to retry the unfinished cleanup. A second close that
overlaps an active close attempt returns `ENT_THRD_IN_USE`.

Close waits until all work accepted before shutdown has completed. Like the
existing running-task behavior, this wait has no timeout: a task callback or
end callback that never returns can block `UTL_TPoolClose()` indefinitely.
If a worker failure strands queued tasks, close sets their return values to
`ENT_TPL_WORKER_FAILED`, invokes their end callbacks, reclaims the pool, and
returns `ENT_TPL_WORKER_FAILED` after cleanup. Those callbacks run on the
thread calling `UTL_TPoolClose()` because no worker remains to claim the tasks.

### Concurrent close/add behavior

The pool maintains an internal lifecycle state and active-operation count around task submission.

Once close begins:

- the pool state moves out of `ACTIVE`;
- new `UTL_TPoolAddTask()` calls fail;
- close waits for already-entered add operations to exit before freeing the pool context.

This prevents the pool context from being freed while an accepted task-submission operation is still touching it.

## Recommended usage pattern

```c
UTL_TPOOL pool = NULL;
MSG_ID_T ret = 0;

if(UTL_TPoolInit(&pool, 4) != ENT_SYS_NORMAL)
{
    return;
}

/* Ensure ret remains alive until the task completes. */
UTL_TPoolAddTask(pool, task_cb, task_end_cb, task_data, &ret);

/* Close drains accepted work, rejects new tasks, and releases workers. */
UTL_TPoolClose(&pool);
pool = NULL;
```

## Timers

`UTL_TimerCreate()` uses a fixed-delay model: it waits for the configured
period, runs the callback, then waits for the next period. Callback duration
therefore extends the interval; callbacks from the same timer do not overlap.

On Linux, `UTL_TimerCreateUs()` schedules against monotonic absolute deadlines.
It keeps at most one callback pending. Ticks that arrive while that slot is
occupied are coalesced instead of queued for later catch-up. This bounds
callback backlog, but it does not guarantee a deadline or hard real-time
latency. Measure jitter on the target kernel and CPU with `perf_utl_timer_rt`.
Its optional fifth argument sets a maximum jitter threshold for that run; when
omitted, the tool reports measurements without a pass threshold. No universal
threshold is applied by default CTest.
