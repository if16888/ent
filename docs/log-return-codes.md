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

## Context Handle Ownership

`ENT_LOG_CTX` APIs enforce same-context handle ownership:

- `ENT_LogCtxInitHandle(ctx, &handle, ...)` creates a handle owned by `ctx`.
- `ENT_LogCtxSetOption(ctx, handle, ...)`, `ENT_LogCtxCloseHandle(ctx, handle)`, and `ENT_LogCtxRaw()` / level-specific `ENT_LogCtx*()` only accept handles owned by the same `ctx`.
- An empty context has no owned handle and cannot proxy the default handle or another context's handle.
- Passing a foreign context-owned handle, a non-context explicit handle, or `NULL` default-handle proxy to a context API returns `ENT_LOG_BAD_HANDLE`.
- Use the non-context `ENT_LogSetOption()`, `ENT_LogCloseHandle()`, and `ENT_LogRaw()` / level-specific `ENT_Log*()` APIs for the default handle and handles created by `ENT_LogInitHandle()`.
- `ENT_LogCtxClose(ctx)` closes the context and automatically closes its live context-owned handle before releasing the context.
- `ENT_LogCtxClose(ctx)` waits for context calls that already entered the lifecycle guard. The owner must prevent new context calls once close begins; stale raw context copies have no callable contract.
- If handle close reports a buffer-thread or file-close failure, the handle remains in `CLOSING` and a later close call may retry cleanup. New writes remain rejected while cleanup is incomplete.

## Lifecycle Summary

- `ACTIVE`: writes and option updates are allowed.
- `CLOSING`: new writers and option updates are rejected with `ENT_LOG_IN_USE`.
- `CLOSED`: the handle is invalid and must not be reused.
- Service close must happen after all live handles have been closed.

## Notes

- `ENT_LOG_OK` exists as the `LOG` submodule success symbol, but the public API success path continues to use `ENT_SYS_NORMAL`.
- Legacy log return macros are no longer used.
