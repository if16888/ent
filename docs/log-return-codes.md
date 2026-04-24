# Log API return-code quick reference

The log module currently uses the legacy numeric return-value style, exposed through the public `ENT_LOG_RC_*` macros in `inc/ent_log.h`.

| Macro | Value | Meaning |
|---|---:|---|
| `ENT_LOG_RC_OK` | `0` | Success. |
| `ENT_LOG_RC_NON_FATAL` | `1` | Non-fatal / no-op state, such as already initialized, already opened, or log filtered by level. |
| `ENT_LOG_RC_ERROR` | `-1` | Generic failure, including bad arguments, uninitialized service, allocation failure, path creation failure, or runtime failure. |
| `ENT_LOG_RC_INVALID_HANDLE` | `-2` | Invalid log handle or invalid log context. |
| `ENT_LOG_RC_IN_USE` | `-3` | Busy / in-use / closing state conflict. |

## Function-level semantics

| API | Success | Non-fatal | Generic failure | Invalid handle/context | Busy / in-use |
|---|---:|---:|---:|---:|---:|
| `ENT_LogInit()` | `ENT_LOG_RC_OK` | `ENT_LOG_RC_NON_FATAL` when already initialized | Usually not expected unless platform init fails in future changes | N/A | N/A |
| `ENT_LogClose()` | `ENT_LOG_RC_OK` | N/A | `ENT_LOG_RC_ERROR` when service is not initialized | N/A | `ENT_LOG_RC_IN_USE` when live handles still exist |
| `ENT_LogInitHandle()` | `ENT_LOG_RC_OK` | `ENT_LOG_RC_NON_FATAL` when the default handle is already open | `ENT_LOG_RC_ERROR` for uninitialized service, allocation failure, or invalid path | N/A | N/A |
| `ENT_LogCloseHandle()` | `ENT_LOG_RC_OK` | N/A | `ENT_LOG_RC_ERROR` when service is not initialized | `ENT_LOG_RC_INVALID_HANDLE` for invalid or already closed handles | `ENT_LOG_RC_IN_USE` for an already-closing handle |
| `ENT_LogSetOption()` | `ENT_LOG_RC_OK` | N/A | `ENT_LOG_RC_ERROR` for bad arguments or invalid option values | `ENT_LOG_RC_INVALID_HANDLE` for invalid handles | `ENT_LOG_RC_IN_USE` when the handle is not `ACTIVE` |
| `ENT_LogRaw()` | `ENT_LOG_RC_OK` | N/A | `ENT_LOG_RC_ERROR` for uninitialized service or formatting/runtime failure | `ENT_LOG_RC_INVALID_HANDLE` for invalid handles | `ENT_LOG_RC_IN_USE` when the handle is closing |
| `ENT_LogFatal()` / `ENT_LogError()` / `ENT_LogWarn()` / `ENT_LogPrint()` / `ENT_LogDebug()` | `ENT_LOG_RC_OK` | `ENT_LOG_RC_NON_FATAL` when filtered by log level | `ENT_LOG_RC_ERROR` for uninitialized service or formatting/runtime failure | `ENT_LOG_RC_INVALID_HANDLE` for invalid handles | `ENT_LOG_RC_IN_USE` when the handle is closing |
| `ENT_LogCtxInit()` | `ENT_LOG_RC_OK` | N/A | `ENT_LOG_RC_ERROR` for uninitialized service, bad argument, or allocation failure | N/A | N/A |
| `ENT_LogCtxClose()` | `ENT_LOG_RC_OK` | N/A | `ENT_LOG_RC_ERROR` when service is not initialized | `ENT_LOG_RC_INVALID_HANDLE` for invalid context | Propagates `ENT_LogCloseHandle()` busy return if owned handle is closing |
| `ENT_LogCtxInitHandle()` | `ENT_LOG_RC_OK` | Propagates `ENT_LogInitHandle()` non-fatal return | `ENT_LOG_RC_ERROR` for uninitialized service, bad argument, allocation failure, or invalid path | `ENT_LOG_RC_INVALID_HANDLE` for invalid context or duplicate context handle | N/A |
| `ENT_LogCtxSetOption()` | `ENT_LOG_RC_OK` | N/A | `ENT_LOG_RC_ERROR` for uninitialized service or bad option arguments | `ENT_LOG_RC_INVALID_HANDLE` for invalid context/handle relationship | `ENT_LOG_RC_IN_USE` when the handle is not `ACTIVE` |
| `ENT_LogCtxCloseHandle()` | `ENT_LOG_RC_OK` | N/A | `ENT_LOG_RC_ERROR` when service is not initialized | `ENT_LOG_RC_INVALID_HANDLE` for invalid context/handle relationship | `ENT_LOG_RC_IN_USE` when the handle is already closing |
| `ENT_LogCtxRaw()` / level-specific `ENT_LogCtx*()` | `ENT_LOG_RC_OK` | `ENT_LOG_RC_NON_FATAL` when filtered by log level | `ENT_LOG_RC_ERROR` for uninitialized service or formatting/runtime failure | `ENT_LOG_RC_INVALID_HANDLE` for invalid context/handle relationship | `ENT_LOG_RC_IN_USE` when the handle is closing |

## Lifecycle summary

- `ACTIVE`: write and option updates are allowed.
- `CLOSING`: new writers and option updates are rejected with `ENT_LOG_RC_IN_USE`.
- `CLOSED`: the handle is invalid and must not be reused.
- Service close must happen after all live handles have been closed.

## Future direction

This document describes the current transitional state. A later migration can map these log return codes into the repository-wide `msg/ent.msg` generated message-code system.