# Log Return Codes

The log module now uses the repository-wide `msg/ent.msg` message-code system. Its return codes are generated from the `LOG` submodule and exposed through `ent_msg_gen.h`.

## Quick Reference

| Code | Meaning |
|---|---|
| `ENT_SYS_NORMAL` / `ENT_LOG_OK` | success |
| `ENT_LOG_NON_FATAL` | no-op / filtered by level |
| `ENT_LOG_BAD_ARGUMENT` | invalid argument |
| `ENT_LOG_NOT_INITIALIZED` | log service not initialized |
| `ENT_LOG_BAD_HANDLE` | invalid log handle or context |
| `ENT_LOG_IN_USE` | handle/service closing or in use |
| `ENT_LOG_ALLOC_FAILED` | allocation failed |
| `ENT_LOG_PATH_FAILED` | log path invalid or creation failed |
| `ENT_LOG_FORMAT_FAILED` | format/prefix generation failed |
| `ENT_LOG_THREAD_FAILED` | buffer thread operation failed |
| `ENT_LOG_IO_FAILED` | file IO failed |
| `ENT_LOG_QUEUE_FULL` | buffered log record rejected because the configured byte limit is full |

Buffered handles have an 8 MiB default queue limit. Set
`ENT_LOG_BUFFER_LIMIT_E` to a positive `size_t` value to change it. A record
that would exceed the limit is rejected with `ENT_LOG_QUEUE_FULL`; it is not
written synchronously as a fallback. `ENT_LogGetBufferStats()` and
`ENT_LogCtxGetBufferStats()` report the current pending byte count, configured
limit, and cumulative rejected-message count.

## Function Semantics

| API | Success | Non-fatal | Bad argument | Not initialized | Bad handle | In use | Alloc failed | Path failed | Format failed | Thread failed | IO failed |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `ENT_LogInit()` | `ENT_SYS_NORMAL` | `ENT_LOG_NON_FATAL` when already initialized | N/A | N/A | N/A | N/A | N/A | N/A | N/A | N/A | N/A |
| `ENT_LogClose()` | `ENT_SYS_NORMAL` | N/A | N/A | `ENT_LOG_NOT_INITIALIZED` | N/A | `ENT_LOG_IN_USE` when live handles or explicit contexts still exist | N/A | N/A | N/A | N/A | N/A |
| `ENT_LogInitHandle()` | `ENT_SYS_NORMAL` | `ENT_LOG_NON_FATAL` when the default handle is already open | `ENT_LOG_BAD_ARGUMENT` for missing input or invalid option state | `ENT_LOG_NOT_INITIALIZED` | N/A | N/A | `ENT_LOG_ALLOC_FAILED` | `ENT_LOG_PATH_FAILED` | N/A | N/A | N/A |
| `ENT_LogCloseHandle()` | `ENT_SYS_NORMAL` | N/A | N/A | `ENT_LOG_NOT_INITIALIZED` | `ENT_LOG_BAD_HANDLE` for invalid or already closed handles | `ENT_LOG_IN_USE` for an already-closing handle | N/A | N/A | N/A | `ENT_LOG_THREAD_FAILED` if buffer thread join fails | `ENT_LOG_IO_FAILED` for file close/flush failures |
| `ENT_LogSetOption()` | `ENT_SYS_NORMAL` | N/A | `ENT_LOG_BAD_ARGUMENT` for bad arguments or invalid option values | `ENT_LOG_NOT_INITIALIZED` | `ENT_LOG_BAD_HANDLE` for invalid handles | `ENT_LOG_IN_USE` when the handle is not `ACTIVE` | `ENT_LOG_ALLOC_FAILED` when option processing allocates memory and that allocation fails | `ENT_LOG_PATH_FAILED` when path validation or creation fails | N/A | `ENT_LOG_THREAD_FAILED` when enabling the buffer thread fails | `ENT_LOG_IO_FAILED` for file option IO failures |
| `ENT_LogRaw()` | `ENT_SYS_NORMAL` | N/A | `ENT_LOG_BAD_ARGUMENT` for malformed format input | `ENT_LOG_NOT_INITIALIZED` | `ENT_LOG_BAD_HANDLE` for invalid handles | `ENT_LOG_IN_USE` when the handle is closing | `ENT_LOG_ALLOC_FAILED` when format expansion allocates memory and fails | N/A | `ENT_LOG_FORMAT_FAILED` for format expansion failures | N/A | `ENT_LOG_IO_FAILED` for write/flush failures |
| `ENT_LogFatal()` / `ENT_LogError()` / `ENT_LogWarn()` / `ENT_LogPrint()` / `ENT_LogDebug()` | `ENT_SYS_NORMAL` | `ENT_LOG_NON_FATAL` when filtered by log level | `ENT_LOG_BAD_ARGUMENT` for malformed format input | `ENT_LOG_NOT_INITIALIZED` | `ENT_LOG_BAD_HANDLE` for invalid handles | `ENT_LOG_IN_USE` when the handle is closing | `ENT_LOG_ALLOC_FAILED` when format expansion or line buffering fails | N/A | `ENT_LOG_FORMAT_FAILED` for prefix or message format failures | N/A | `ENT_LOG_IO_FAILED` for write/flush failures |
| `ENT_LogCtxInit()` | `ENT_SYS_NORMAL` | N/A | `ENT_LOG_BAD_ARGUMENT` for missing context pointer | `ENT_LOG_NOT_INITIALIZED` | N/A | N/A | `ENT_LOG_ALLOC_FAILED` | `ENT_LOG_PATH_FAILED` | N/A | N/A | N/A |
| `ENT_LogCtxClose()` | `ENT_SYS_NORMAL` | N/A | N/A | `ENT_LOG_NOT_INITIALIZED` | `ENT_LOG_BAD_HANDLE` for invalid context | `ENT_LOG_IN_USE` when the owned handle is closing | N/A | N/A | N/A | `ENT_LOG_THREAD_FAILED` if the owned buffer thread cannot stop cleanly | `ENT_LOG_IO_FAILED` for file close/flush failures |
| `ENT_LogCtxInitHandle()` | `ENT_SYS_NORMAL` | Propagates `ENT_LOG_NON_FATAL` from `ENT_LogInitHandle()` | `ENT_LOG_BAD_ARGUMENT` for missing arguments | `ENT_LOG_NOT_INITIALIZED` | `ENT_LOG_BAD_HANDLE` for invalid context or duplicate context handle | N/A | `ENT_LOG_ALLOC_FAILED` | `ENT_LOG_PATH_FAILED` | N/A | N/A | N/A |
| `ENT_LogCtxSetOption()` | `ENT_SYS_NORMAL` | N/A | `ENT_LOG_BAD_ARGUMENT` for missing arguments | `ENT_LOG_NOT_INITIALIZED` | `ENT_LOG_BAD_HANDLE` for invalid context/handle relationship | `ENT_LOG_IN_USE` when the handle is not `ACTIVE` | `ENT_LOG_ALLOC_FAILED` | `ENT_LOG_PATH_FAILED` | N/A | `ENT_LOG_THREAD_FAILED` | `ENT_LOG_IO_FAILED` |
| `ENT_LogCtxCloseHandle()` | `ENT_SYS_NORMAL` | N/A | N/A | `ENT_LOG_NOT_INITIALIZED` | `ENT_LOG_BAD_HANDLE` for invalid context/handle relationship | `ENT_LOG_IN_USE` when the handle is already closing | N/A | N/A | N/A | `ENT_LOG_THREAD_FAILED` | `ENT_LOG_IO_FAILED` |
| `ENT_LogCtxRaw()` / level-specific `ENT_LogCtx*()` | `ENT_SYS_NORMAL` | `ENT_LOG_NON_FATAL` when filtered by log level | `ENT_LOG_BAD_ARGUMENT` for malformed format input | `ENT_LOG_NOT_INITIALIZED` | `ENT_LOG_BAD_HANDLE` for invalid context/handle relationship | `ENT_LOG_IN_USE` when the handle is closing | `ENT_LOG_ALLOC_FAILED` when format expansion or line buffering fails | N/A | `ENT_LOG_FORMAT_FAILED` for format/prefix failures | N/A | `ENT_LOG_IO_FAILED` for write/flush failures |

`ENT_LogGetBufferStats()` returns `ENT_LOG_BAD_ARGUMENT` for a NULL output
pointer and otherwise follows the handle lifecycle errors above.
`ENT_LogCtxGetBufferStats()` also rejects handles not owned by the supplied
context with `ENT_LOG_BAD_HANDLE`.

## Context Handle Ownership

`ENT_LOG_CTX` APIs enforce same-context handle ownership:

- `ENT_LogCtxInitHandle(ctx, &handle, ...)` reserves the empty context before
  allocating a handle. Concurrent initializers are rejected with
  `ENT_LOG_BAD_HANDLE`; a failed initializer releases its reservation for retry.
- Explicit handle output slots are output-only and cleared on failure. Close an
  existing handle before reusing its variable.
- `ENT_LogCtxSetOption(ctx, handle, ...)`, `ENT_LogCtxCloseHandle(ctx, handle)`, and `ENT_LogCtxRaw()` / level-specific `ENT_LogCtx*()` only accept handles owned by the same `ctx`.
- An empty context has no owned handle and cannot proxy the default handle or another context's handle.
- Passing a foreign context-owned handle, a non-context explicit handle, or `NULL` default-handle proxy to a context API returns `ENT_LOG_BAD_HANDLE`.
- Use the non-context `ENT_LogSetOption()`, `ENT_LogCloseHandle()`, and `ENT_LogRaw()` / level-specific `ENT_Log*()` APIs for the default handle and handles created by `ENT_LogInitHandle()`.
- `ENT_LogCtxClose(ctx)` closes the context and automatically closes its live context-owned handle before releasing the context.
- `ENT_LogCtxClose(ctx)` waits for context calls that already entered the lifecycle guard. The owner must prevent new context calls once close begins; stale raw context copies have no callable contract.
- A buffer-thread teardown failure retains the handle in `CLOSING` for retry.
  File I/O errors may be reported after the handle has already been released.
  `ENT_LogCtxClose` also releases the context after such a completed I/O failure;
  do not retry `ENT_LOG_IO_FAILED` on the old context pointer.

## Lifecycle Summary

- `ACTIVE`: writes and option updates are allowed.
- `CLOSING`: new writers and option updates are rejected with `ENT_LOG_IN_USE`.
- `CLOSED`: the handle is invalid and must not be reused.
- Service close must happen after all live handles have been closed.

## Notes

- `ENT_LOG_OK` exists as the `LOG` submodule success symbol, but the public API success path continues to use `ENT_SYS_NORMAL`.
- Legacy log return macros are no longer used.

## Migrating context-close callers

Do not treat every negative close result as retryable. First stop new users and
retain exclusive ownership of the context. Callers of the raw by-value API must
clear their owner variable on `ENT_SYS_NORMAL` **or** `ENT_LOG_IO_FAILED`, because
both complete context reclamation. Incomplete worker shutdown returns
`ENT_LOG_THREAD_FAILED` and keeps the context valid for a later retry. `IN_USE`
requires synchronization with the thread owning close; `BAD_HANDLE` requires
ownership diagnosis. Neither is a reason for a blind retry loop.

The compiled [owner helper](../example/log_ctx_close.h) wraps the existing API:

```c
status = iExample_LogCtxClose(&ctx);
if(status == ENT_LOG_IO_FAILED) {
    /* Report possible log data loss. ctx is NULL; never retry the old pointer. */
} else if(status == ENT_LOG_THREAD_FAILED) {
    /* ctx is retained. Resolve the worker failure before a later retry. */
}
```

The helper is example code, not a new public API. A second helper call on its
cleared slot is harmless; this does not make saved raw aliases callable. Discard
the context-owned log handle and all context aliases after completed close.
[example03_log_ctx_close.c](../example/example03_log_ctx_close.c) is built and run
as a smoke test when examples/testing are enabled. The log regression suite uses
the same helper for both terminal I/O failure and injected retryable worker-stop
failure, and checks service ownership is fully released only after completion.

For the reusable owner procedure and its module-specific limits, see
[close ownership](architecture/handle-lifecycle.md#可复用的关闭所有权模式).

## Calendar and path semantics

POSIX Linux buffered flush waits use a monotonic-clock condition variable,
rather than a realtime-clock absolute deadline. Log filenames and rotation
are based on local calendar dates; DST transitions are not assumed to be
exactly 86,400 seconds. Updating `ENT_LOG_PATH_E` commits a new directory
for **subsequent** writes: the next write rolls the previously open file
and opens the current day's file under the new directory. Pending buffered
records may still be processed according to the asynchronous queue's normal
ordering. File open/close failures are returned through existing logging
status codes; no new public API or ABI is introduced.
