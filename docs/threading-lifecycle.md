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

### `UTL_TPoolAddTask()`

`UTL_TPoolAddTask()` stores the caller-provided `MSG_ID_T* retVal` pointer and writes the task return value to it when the worker runs the task.

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
- workers are woken and joined;
- queued tasks that have not started are discarded during pool cleanup;
- end callbacks are not called for discarded queued tasks;
- the pool handle must not be reused after close returns.

This is intentionally not a full drain operation. The close operation guarantees that running tasks finish and the worker threads are joined; it does not guarantee that all queued tasks will execute.

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

/* Close waits for running tasks, rejects new tasks, and releases workers. */
UTL_TPoolClose(pool);
pool = NULL;
```

For workloads requiring guaranteed execution of all queued tasks, introduce an explicit drain API or an application-level shutdown protocol before calling `UTL_TPoolClose()`.
