# Handle Lifecycle Audit

This document audits the public handle and context lifecycle patterns in `ent` and
identifies the residual raw-pointer / close / concurrent-entry risks that are still
visible after the ENT-013 and ENT-014 close-loop work.

The goal is not to change APIs here. The goal is to make the current lifecycle model
explicit, rank the remaining risks, and point at the runtime-owner migration path
described in [`runtime-resource-ownership.md`](runtime-resource-ownership.md).

## Executive Summary

The repository has converged on a few different lifecycle patterns:

- `ENT_HANDLE` is now the runtime/root instance boundary for `ENT_Init` / `ENT_Run`
  / `ENT_Stop` / `ENT_Close`.
- `ENT_LOG` and `ENT_LOG_CTX` already separate service-level and context-level
  ownership, with state and active-writer accounting.
- `DB_HANDLE` uses state plus active-operation counting and is the strongest example
  of a classic close-and-drain pattern.
- `UTL_TPOOL` is the clearest reference implementation for registry + state +
  activeOps + draining close.
- `UTL_TIMER` has explicit service lifecycle state and worker/callback draining, but
  still has more moving parts than the tpool model.
- `ENT_THREAD` is a raw service handle with join/close semantics and no runtime owner.
- `ENT_SharedMap` is a bare pointer handle with no state machine, registry, or active
  operation accounting.
- `UTL_LOCK`, `UTL_CV`, and socket descriptors are still caller-owned primitives rather
  than runtime-owned resources.

The biggest remaining design gaps are:

1. `ENT_HANDLE` still has a Validate -> BeginCall race window and no registry or
   generation-based stabilization.
2. `ENT_SharedMap` has no lifecycle protection beyond pointer-to-handle close.
3. `ENT_THREAD` and `UTL_Timer` still rely on service-local patterns rather than a
   runtime-owned resource registry.

The short-term public contract should remain conservative:

- `ENT_Close()` waits for already-entered operations, but callers must still serialize
  any new concurrent entries once close begins.
- `ENT_SharedMap` / `ENT_THREAD` / socket descriptor helpers do not have a safe stale
  raw pointer contract after close.
- `UTL_TPool` remains the reference pattern for how a resource registry can be made
  safe without flattening every public API into `ENT_HANDLE`.

## Audit Rules

The following rules were used to classify each handle/context:

1. If the API frees the handle and another public API can still dereference the raw
   pointer without a registry or generation fence, the stale raw pointer is marked as
   having no callable contract.
2. If validation and active-operation registration are not protected by the same lock,
   the handle is marked with a Validate -> BeginCall race.
3. If close/free can overlap with callback or worker execution, the handle is marked
   for callback/free race review.
4. If the close path can fail after the state has already been partially torn down, the
   handle is checked for unrecoverable close failure.
5. `UTL_TPOOL` is treated as the reference implementation and is not flagged for
   missing registry semantics.
6. "Already-entered drain" and "close vs new entry race" are classified separately.

## Risk Matrix

| Handle | Risk | Reason |
| --- | --- | --- |
| `ENT_HANDLE` | P0 | Validate -> BeginCall race; no registry/generation; close/free depends on caller-side serialization for new entries. |
| `ENT_LOG` / `ENT_LOG_CTX` | P1 | Service/context ownership is explicit, but concurrent entry vs close still depends on handle-state discipline rather than a runtime registry. |
| `DB_HANDLE` | P1 | Strong activeOps model and close drain exist, but the handle is still service-owned and stale raw pointers are not stabilized by a registry. |
| `UTL_TIMER_T` | P1 | Lifecycle ops, tag checks, callback workers, and RT workers are all present; the close/free/callback edge is still more complex than the tpool pattern. |
| `ENT_THREAD` | P1 | Join/close registry exists, but it is still a raw service handle with no runtime owner contract. |
| `UTL_TPOOL` | P2 | Reference implementation; strong registry + state + activeOps already exist. |
| `ENT_SharedMap` | P0 | Bare pointer handle with no state, registry, or activeOps; close/free vs Ptr/Size/Flush has no public concurrent-entry contract. |
| `UTL_LOCK` | P2 | Caller-owned primitive; close is by pointer, but it is not runtime-owned and does not require a registry. |
| `UTL_CV` | P2 | Same as `UTL_LOCK`; caller-owned primitive, no runtime owner. |
| `UTL_D_SOCKET` | P2 | Raw OS descriptor wrapper; no ent-level state machine or registry. |

## Per-Module Findings

### ENT_HANDLE

| Handle name | Create / init API | Use APIs | Close API | Close API pointer-to-handle | Close success sets NULL | tag / magic | state | activeOps / refcount | registry | owner runtime / ENT_CTX | close waits already-entered op | close vs new operation | stale pointer callable contract | current test coverage | risk | recommended action |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `ENT_HANDLE` | `ENT_Init(&handle, ...)` | `ENT_Run`, `ENT_Stop`, `ENT_SetRtAttributes` | `ENT_Close(&handle)` | yes | yes | yes | yes (`ACTIVE` / `CLOSING` / `CLOSED`) | yes (`activeCalls`) | no | yes (`ENT_CTX`) | yes | short-term only; new entries after close begins require caller-side serialization | no contract for stale raw copies after close | `test_ent_init.c`, downstream consumer, README quickref | P0 | Keep the current short-term public contract, and design a runtime owner / registry only as the next migration layer. |

Key finding:

- `iENT_HandleValidate()` and `iENT_HandleBeginCall()` are still separate, so a
  Validate -> BeginCall race remains.
- The current contract is intentionally conservative: close drains already-entered
  work, but callers must serialize any new `Run` / `Stop` / attribute entries once
  close begins.
- There is no global registry or generation fence yet.

### ENT_LOG / ENT_LOG_CTX

| Handle name | Create / init API | Use APIs | Close API | Close API pointer-to-handle | Close success sets NULL | tag / magic | state | activeOps / refcount | registry | owner runtime / ENT_CTX | close waits already-entered op | close vs new operation | stale pointer callable contract | current test coverage | risk | recommended action |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `ENT_LOG` | `ENT_LogInit`, `ENT_LogInitHandle(&log, ...)` | `ENT_LogSetOption`, `ENT_LogRaw`, `ENT_LogFatal/Error/Warn/Print/Debug` | `ENT_LogCloseHandle(log)`, `ENT_LogClose()` | no | no | no explicit magic; state-based validation | yes (`CREATED` / `ACTIVE` / `CLOSING` / `CLOSED`) | yes (`activeWriters`) | implicit service live-count only (`sLogNum`), not a runtime registry | service-owned; explicit context ownership exists for `ENT_LOG_CTX` | yes for writers / buffer thread drain | partial; writer path is protected, but context-level entry still depends on ownership checks and close sequencing | no contract after close for stale raw handles | `test_ent_log.c`, `test_ent_log_flush_deadline.c`, docs/log-return-codes.md | P1 | Keep the service/context split, but treat runtime-owned log handles as a future migration target rather than an immediate API break. |
| `ENT_LOG_CTX` | `ENT_LogCtxInit(&ctx)` | `ENT_LogCtxInitHandle`, `ENT_LogCtxSetOption`, `ENT_LogCtxRaw`, `ENT_LogCtxFatal/Error/Warn/Print/Debug` | `ENT_LogCtxClose(ctx)`, `ENT_LogCtxCloseHandle(ctx, log)` | no | no | no explicit magic; ownership validation plus handle state | yes (`CREATED` / `ACTIVE` / `CLOSING` / `CLOSED`) | yes (`activeWriters`) | no | ctx-owned, not runtime-owned | yes for owned-handle drain | partial; same-context ownership is enforced, but not a global registry fence | no contract after close for stale raw copies | `test_ent_log.c`, `test_ent_log_flush_deadline.c`, docs/log-return-codes.md | P1 | Keep explicit context ownership, then decide later whether the runtime owner should register ctx-owned resources. |

Key findings:

- `ENT_LOG` already has state and writer accounting, which is a good sign.
- `ENT_LOG_CTX` is explicit about same-context ownership, but it is not yet attached to
  a runtime-owned resource registry.
- `ENT_LogClose()` and `ENT_LogCtxClose()` are closer to a service-owned model than to
  a runtime-owned model.

### DB_HANDLE

| Handle name | Create / init API | Use APIs | Close API | Close API pointer-to-handle | Close success sets NULL | tag / magic | state | activeOps / refcount | registry | owner runtime / ENT_CTX | close waits already-entered op | close vs new operation | stale pointer callable contract | current test coverage | risk | recommended action |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `DB_HANDLE` | `ENT_DbInitHandle(&db, ...)` | `ENT_DbOpen`, `ENT_DbRead`, `ENT_DbWrite`, `ENT_DbReadParams`, `ENT_DbWriteParams`, `iENT_DbReInit` | `ENT_DbCloseHandle(&db)`, `ENT_DbClose()` | yes | yes | no explicit magic; state + validation checks | yes (`CREATED` / `ACTIVE` / `CLOSING` / `CLOSED`) | yes (`activeOps`) | no | no runtime owner today | yes | partial; active ops drain is enforced, and new ops are rejected while closing | no contract after close for stale raw copies | `test_ent_db.c`, `test_security.c`, sqlite/mysql/pgsql coverage, README lifecycle section | P1 | Keep the current close-drain model, and if DB resources move under runtime ownership later, use the DB handle as a child resource rather than flattening it. |

Key findings:

- `ENT_DbCloseHandle()` is already a strong close-and-drain implementation.
- `ENT_DbInitHandle()` auto-closes a previous live handle, so its failure path must keep
  the caller pointer consistent. Current tests already exercise the close/reinit edge.
- The handle is not tied to `ENT_CTX` yet, so runtime-owned shutdown is still a design
  question rather than an implementation fact.

### UTL_TIMER_T

| Handle name | Create / init API | Use APIs | Close API | Close API pointer-to-handle | Close success sets NULL | tag / magic | state | activeOps / refcount | registry | owner runtime / ENT_CTX | close waits already-entered op | close vs new operation | stale pointer callable contract | current test coverage | risk | recommended action |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `UTL_TIMER_T` | `UTL_TimerInit`, `UTL_TimerCreate`, `UTL_TimerCreateUs` | timer callback execution is implicit; public use is via the create/delete APIs | `UTL_TimerDelete(&timer)`, `UTL_TimerClose()` | yes for delete, no for service close | yes for delete | yes (`tag`) | partial; service closing flag + per-timer enable flag rather than a single explicit state enum | yes (`sTimerLifecycleOps` plus per-timer worker/callback counters) | yes (`sTimerCtx.dllHeader`) | no runtime owner today | yes for service shutdown and delete paths | partial; create/delete is gated by service lifecycle, but callback/RT worker/self-delete paths still need careful review | no contract after close for stale raw copies | `test_utl_timer.c`, perf timers, engineering hardening summary | P1 | Keep the current service registry model, but document the callback/free boundary sharply and treat runtime ownership as a future integration step. |

Key findings:

- `UTL_TimerDelete()` and `UTL_TimerClose()` already cooperate with a service-level
  registry and lifecycle op counter.
- The Linux RT path and the callback/self-delete path are the most delicate places.
- This module is a good candidate for runtime-owned registration, but it is not yet
  part of `ENT_CTX`.

### ENT_THREAD

| Handle name | Create / init API | Use APIs | Close API | Close API pointer-to-handle | Close success sets NULL | tag / magic | state | activeOps / refcount | registry | owner runtime / ENT_CTX | close waits already-entered op | close vs new operation | stale pointer callable contract | current test coverage | risk | recommended action |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `ENT_THREAD` | `ENT_ThreadInit(&th)` | `ENT_ThreadDetachCreate`, `ENT_ThreadCreate`, `ENT_ThreadWaitById` | `ENT_ThreadClose(th)` | no | no | yes (`ENT_TH_TAG`) | no explicit state enum; joinable thread records are tracked by tag | no explicit activeOps/refcount | yes (thread record DLL under the service handle) | no runtime owner today | yes for registered joinable threads | partial; the service handle joins and frees tracked thread records, but callers must still own the higher-level shutdown order | no contract after close for stale raw copies | `test_ent_thread.c`, `test_ent_thread_failures.c`, docs/threading-lifecycle.md | P1 | Keep it as an owner-thread lifecycle service for now; if runtime-owned child-thread tracking is needed later, add it as a child resource rather than changing the external handle type. |

Key findings:

- The thread service has a registry of joinable thread records and a clean join-on-close
  behavior.
- It still uses a raw service handle, so there is no runtime owner attachment today.
- The design is closer to "owner-thread lifecycle" than to a runtime-owned child resource.

### UTL_TPOOL

| Handle name | Create / init API | Use APIs | Close API | Close API pointer-to-handle | Close success sets NULL | tag / magic | state | activeOps / refcount | registry | owner runtime / ENT_CTX | close waits already-entered op | close vs new operation | stale pointer callable contract | current test coverage | risk | recommended action |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `UTL_TPOOL` | `UTL_TPoolInit(&pool, ...)` | `UTL_TPoolAddTask` | `UTL_TPoolClose(&pool)` | yes | yes | yes (`UTL_TPOOL_TAG`) | yes (`CREATED` / `ACTIVE` / `CLOSING` / `CLOSED`) | yes (`activeOps`) | yes (`registryNext` + service registry list) | no runtime owner today | yes | yes; `AddTask()` rejects while closing | no contract after close for stale raw copies | `test_utl_tpool.c`, `test_utl_tpool_integration.c`, docs/threading-lifecycle.md, engineering hardening summary | P2 | Keep this as the reference implementation for registry + state + activeOps. Reuse the pattern when designing runtime-owned resources. |

Key findings:

- `UTL_TPOOL` is the strongest lifecycle pattern in the repository.
- It already has the ingredients that other modules are missing: registry, state,
  activeOps, and an explicit close drain.
- It should be treated as the model to copy, not as the module to redesign first.

### ENT_SharedMap

| Handle name | Create / init API | Use APIs | Close API | Close API pointer-to-handle | Close success sets NULL | tag / magic | state | activeOps / refcount | registry | owner runtime / ENT_CTX | close waits already-entered op | close vs new operation | stale pointer callable contract | current test coverage | risk | recommended action |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `ENT_SharedMap` | `ENT_SharedMapOpen(&map, ...)` | `ENT_SharedMapPtr`, `ENT_SharedMapSize`, `ENT_SharedMapFlush` | `ENT_SharedMapClose(&map)` | yes | yes | no explicit magic | no explicit state | no activeOps/refcount | no | no runtime owner today | no | no; `Ptr` / `Size` / `Flush` are not synchronized against close | no contract after close for stale raw copies | `test_ent_shm.c`, `docs/ent-shm.md` | P0 | Document the handle as caller-synchronized today; if runtime ownership is needed later, add state + registry before claiming any concurrent close safety. |

Key findings:

- `ENT_SharedMap` is a bare pointer handle.
- It is fine as a file-mapped snapshot primitive, but it is not currently safe to
  describe it as close-drain capable or concurrent-entry safe.
- The current public contract should stay conservative: caller serializes `Ptr` /
  `Size` / `Flush` / `Close`.

### UTL_LOCK

| Handle name | Create / init API | Use APIs | Close API | Close API pointer-to-handle | Close success sets NULL | tag / magic | state | activeOps / refcount | registry | owner runtime / ENT_CTX | close waits already-entered op | close vs new operation | stale pointer callable contract | current test coverage | risk | recommended action |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `UTL_LOCK` | `UTL_LockInit`, `UTL_LockInitEx` | `UTL_LockEnter`, `UTL_LockEnterEx`, `UTL_LockLeave`, `UTL_LockLeaveEx` | `UTL_LockClose(&lock)` | yes | yes | no explicit magic/tag | no explicit state enum | no | no | no runtime owner today | no | no; caller must serialize lock close against use | no contract after close for stale raw copies | `test_utl_thread.c`, `test_utl_lock_cv.c` | P2 | Keep as a caller-owned primitive; document the caller-side lifecycle responsibility rather than trying to wrap it into `ENT_CTX`. |

### UTL_CV

| Handle name | Create / init API | Use APIs | Close API | Close API pointer-to-handle | Close success sets NULL | tag / magic | state | activeOps / refcount | registry | owner runtime / ENT_CTX | close waits already-entered op | close vs new operation | stale pointer callable contract | current test coverage | risk | recommended action |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `UTL_CV` | `UTL_CVInit`, `UTL_CVWait`, `UTL_CVWake`, `UTL_CVWakeAll` | wait/wake operations only | `UTL_CVClose(&cv)` | yes | yes | no explicit magic/tag | no explicit state enum | no | no | no runtime owner today | no | no; caller must serialize cv close against wait/wake use | no contract after close for stale raw copies | `test_utl_thread.c`, `test_utl_lock_cv.c` | P2 | Keep as a caller-owned primitive and document the shutdown ordering in the surrounding owner module. |

### UTL_D_SOCKET

| Handle name | Create / init API | Use APIs | Close API | Close API pointer-to-handle | Close success sets NULL | tag / magic | state | activeOps / refcount | registry | owner runtime / ENT_CTX | close waits already-entered op | close vs new operation | stale pointer callable contract | current test coverage | risk | recommended action |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `UTL_D_SOCKET` | `UTL_Socket` | `UTL_Bind`, `UTL_Connect`, `UTL_Listen`, `UTL_Accept`, `UTL_Recv`, `UTL_Send`, `UTL_SetSockOpt`, `UTL_Shutdown`, `UTL_GetSockOpt` | `UTL_CloseSocket` | no | no | no | no | no | no | no runtime owner today | no | no; this is a raw OS descriptor wrapper | no contract after close for stale raw copies | `test_utl_socket.c`, `perf_utl_socket.c` | P2 | Keep it as a thin descriptor wrapper; do not pretend it is a runtime-owned handle. |

## P0 Issues

1. `ENT_HANDLE` still has a Validate -> BeginCall race window. The public contract is
   currently caller-serialized after close begins, not code-level registry-safe.
2. `ENT_SharedMap` has no state machine, registry, or activeOps. It should not be
   described as concurrent-close safe.

## P1 Issues

1. `ENT_LOG` / `ENT_LOG_CTX` should eventually be expressible as runtime-owned
   resources, but the current model is still service/context owned.
2. `DB_HANDLE` has a strong drain model, but it is not yet attached to a runtime owner.
3. `UTL_TIMER_T` still has worker/callback/self-delete complexity that would benefit
   from explicit runtime ownership.
4. `ENT_THREAD` joins cleanly, but remains a raw service handle rather than a runtime
   child resource.

## P2 Issues

1. `UTL_TPOOL` is the reference lifecycle implementation; it is not a blocker, but it
   should be the model for future runtime-owned resources.
2. `UTL_LOCK`, `UTL_CV`, and socket descriptors are caller-owned primitives and should
   be documented as such rather than being forced into a registry model.

## Recommended Remediation Order

1. Formalize the runtime-owner design and registry shape.
2. Keep `ENT_HANDLE` contract wording conservative until a registry/generation design
   exists.
3. Harden the log and DB child-resource attachment story.
4. Normalize timer ownership and callback cleanup.
5. Decide whether `ENT_THREAD` should remain owner-thread only or become a runtime child.
6. Decide whether `ENT_SharedMap` stays caller-synchronized or becomes runtime-owned.
7. Reuse the `UTL_TPOOL` pattern as the implementation reference for any new registry.

## Non-Goals

- No public API rename or flattening of every handle to `ENT_HANDLE`.
- No registry implementation in this audit document.
- No DB, log, timer, thread, or shared-map behavioral changes here.
- No attempt to claim stale raw pointer safety after close unless a module already has
  a registry or generation fence.

## Open Questions

1. Should the runtime owner be the default `gEntCtx`, or should the ownership model
   move to explicit per-instance handles only?
2. Should `ENT_LOG_CTX` and `DB_HANDLE` become explicit runtime-owned children first,
   or should timers and thread pools be migrated first?
3. Should `ENT_SharedMap` stay caller-synchronized forever, or should it gain state and
   registry support in a later phase?
4. Should `ENT_THREAD` remain an owner-thread lifecycle service, or should it be
   attachable to `ENT_CTX` as a child resource?
5. Should `UTL_TPool` be copied as-is into the runtime owner registry, or should the
   registry get a simpler minimal interface first?

