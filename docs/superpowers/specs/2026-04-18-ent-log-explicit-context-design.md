# ENT Log Explicit Context Design

## 1. Goal

Keep the existing `ENT_Log*` / `IENT_LOG_*` convenience API, but move the implementation onto an explicit log context so the default handle becomes just one instance rather than the only instance.

This design keeps the simple call path for current users and removes the hidden dependency on `gEntCtx.entLog` from the core implementation.

## 2. Problem Statement

`comm/ent_log.c` currently mixes several responsibilities:

- global runtime ownership
- default log handle lifecycle
- private handle lifecycle
- option dispatch
- buffered/file writing
- compatibility behavior for the `NULL` handle path

That structure keeps the public API simple, but it also makes the module hard to reason about:

- the default log handle is a special case inside almost every function
- internal code depends on `gEntCtx.entLog` through macros instead of an explicit dependency
- the lifecycle of the default handle and private handles is implemented in the same file
- tests and embedded callers cannot isolate log state cleanly

The goal of this refactor is not to remove the default handle. The goal is to make the default handle an explicit instance internally so the module can support both simple usage and isolated usage without hidden coupling.

## 3. Recommended Approach

Use **explicit context + compatibility layer**.

### Why this approach

- The default path stays simple for current callers.
- Private log instances become first-class and testable.
- Internal code can stop depending on `gEntCtx.entLog` directly.
- The change can be staged without breaking the existing public surface.

### Why not only split files

Splitting `ent_log.c` into multiple files without changing the ownership model would reduce file size, but it would not fix the hidden global-state problem.

### Why not switch everything immediately

A full API break would force every caller to migrate at once. That is unnecessary because the current default-handle model is useful for simple cases.

## 4. Target Architecture

### 4.1 Internal Context Model

Introduce an internal log context type, for example `ENT_LOG_CTX`, that owns:

- the default handle
- the default state flags
- the default lock
- the active output destination
- path and module name storage
- buffer configuration
- flush and rotation settings

The public handle type stays the same. The default handle becomes one instance inside the context instead of a separate ad hoc path.

### 4.2 File Split

Split the current monolithic implementation into focused units:

- `comm/ent_log_core.c`
  - context creation and teardown
  - default handle routing
  - handle validation
  - dispatch glue for old and new entry points

- `comm/ent_log_writer.c`
  - file writes
  - stdout/stderr writes if present
  - buffering and flush behavior
  - low-level output helpers

- `comm/ent_log_options.c`
  - `ENT_LogSetOption`
  - option-to-state mapping
  - option validation

- `comm/ent_log_compat.c`
  - old `ENT_Log*` wrappers
  - `IENT_LOG_*` macro support path
  - `NULL` handle compatibility for the default instance

The public header remains `inc/ent_log.h`. It gains new explicit-context declarations, but the existing declarations stay valid.

### 4.3 Public API Shape

Keep these APIs working:

- `ENT_LogInit`
- `ENT_LogClose`
- `ENT_LogInitHandle`
- `ENT_LogSetOption`
- `ENT_LogCloseHandle`
- `ENT_LogRaw`
- `ENT_LogFatal`
- `ENT_LogError`
- `ENT_LogWarn`
- `ENT_LogPrint`
- `ENT_LogDebug`

Add explicit-context APIs alongside them, for example:

- `ENT_LogCtxInit`
- `ENT_LogCtxClose`
- `ENT_LogCtxInitHandle`
- `ENT_LogCtxSetOption`
- `ENT_LogCtxCloseHandle`
- `ENT_LogCtxRaw`
- `ENT_LogCtxFatal`
- `ENT_LogCtxError`
- `ENT_LogCtxWarn`
- `ENT_LogCtxPrint`
- `ENT_LogCtxDebug`

The exact names should follow the repository naming pattern, but the rule is simple: the new API must make the context argument explicit, while the old API remains a convenience wrapper around the default context.

### 4.4 Macro Compatibility

`IENT_LOG_*` macros should continue to work without requiring callers to pass a context explicitly.

Internally, the macros should stop reaching directly into `gEntCtx.entLog`. Instead, they should resolve the default log instance through a small accessor in the log core layer.

That keeps the macro surface unchanged while removing the strongest global coupling.

## 5. Runtime Model

### Default usage

The default instance remains the fast path:

1. `ENT_LogInit` initializes the log subsystem.
2. `ENT_LogInitHandle(NULL, ...)` creates or reuses the default handle.
3. Existing code continues to call `ENT_LogPrint(NULL, ...)` or `IENT_LOG_PRINT(...)`.
4. The core routes those calls to the default context.

This preserves the low-complexity usage model that exists today.

### Explicit usage

An advanced caller can create a private context and keep it isolated:

1. Create a context.
2. Initialize a private handle or handles on that context.
3. Set independent output path, level, buffering, and flush policy.
4. Tear the context down without affecting the default instance.

That is the main new capability: the default model stays simple, but it is no longer the only model.

Current ownership contract:

- A handle is owned by a context only when it is created by `ENT_LogCtxInitHandle()` for that context.
- `ENT_LogCtxSetOption()`, `ENT_LogCtxCloseHandle()`, and `ENT_LogCtxRaw()` / level-specific `ENT_LogCtx*()` require a handle owned by the same context.
- An empty context must not proxy the default handle or operate on a handle owned by another context.
- Context APIs reject foreign handles, non-context explicit handles, and the `NULL` default-handle proxy with `ENT_LOG_BAD_HANDLE`.
- The compatibility `ENT_Log*()` APIs remain the path for default-handle usage and handles created by `ENT_LogInitHandle()`.

## 6. Migration Plan

### Phase 1: Introduce the context layer

- add the internal context type
- move default-instance ownership into the context
- preserve the old public API as wrappers
- keep `NULL` meaning the default instance

### Phase 2: Split the implementation files

- move writer logic into `ent_log_writer.c`
- move option logic into `ent_log_options.c`
- move compatibility wrappers into `ent_log_compat.c`
- keep `ent_log_core.c` small and focused

### Phase 3: Reduce direct global dependencies

- replace direct `gEntCtx.entLog` access in internal macros and helper paths
- route internal log use through the default-context accessor
- keep the default instance behavior unchanged for callers

### Phase 4: Optional follow-up cleanup

If the explicit-context API proves stable, future work can:

- document the new API as the preferred path for embedded or multi-instance callers
- de-emphasize the wrapper-based entry points in new code
- eventually narrow the compatibility layer if the codebase no longer needs it

## 7. Testing Strategy

Add or adjust tests in `test/test_ent_log.c` to cover:

- default instance still works with `NULL` handles
- private handles remain independent from the default instance
- closing one instance does not disturb another instance
- option changes are isolated to the target instance
- buffer and flush behavior still match the current contract

The important verification rule is: the new tests must prove both compatibility and isolation.

## 8. Risks

- The log module is a shared dependency for many other modules, so macro compatibility must stay intact during the first phase.
- If the compatibility wrappers are too thin or too clever, debugging gets harder instead of easier.
- Moving buffering and flush logic into a separate file is only valuable if the context ownership is already clear.

## 9. Non-Goals

- Do not remove the default log path in this phase.
- Do not force `db`, `script`, or `thread` callers to migrate immediately.
- Do not change log formatting strings unless a specific bug requires it.
- Do not introduce a second logging subsystem.

