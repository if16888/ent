# Runtime Resource Ownership

This document describes the target architecture for moving `ent` from a collection of
fragmented service-level lifecycles to a runtime-owned resource model.

It is intentionally a design document only. It does **not** introduce a new public API
or a global registry implementation. The purpose is to define the destination so the
next implementation steps can be staged safely.

## Problem Statement

`ent` currently has multiple independent lifetime models:

- `ENT_HANDLE` owns the runtime instance boundary.
- `ENT_LOG` / `ENT_LOG_CTX` own log service and context lifecycles.
- `DB_HANDLE` owns database connection lifecycles.
- `UTL_TIMER_T` owns timer lifecycles.
- `ENT_THREAD` owns registered thread lifecycles.
- `UTL_TPOOL` owns a pool registry with activeOps.
- `ENT_SharedMap` currently remains a caller-synchronized file-backed mapping handle.
- `UTL_LOCK`, `UTL_CV`, and socket descriptors remain caller-owned primitives.

The result is a fragmented shutdown story:

- some modules have a registry, some do not;
- some modules have active-op draining, some do not;
- some modules require pointer-to-handle close, some do not;
- some modules are safe to treat as service-owned, others are still raw pointers.

The long-term goal is not to flatten every handle into `ENT_HANDLE`. The goal is to
make `ENT_HANDLE` / `ENT_CTX` the ownership root and let subresources register
themselves there.

## Current Fragmented Lifecycle Model

The current model is a mix of the following patterns:

1. **Service-level state with explicit child ownership**
   - `ENT_LOG` / `ENT_LOG_CTX`
   - `DB_HANDLE`
2. **Registry + state + activeOps**
   - `UTL_TPOOL`
   - parts of `UTL_TIMER_T`
3. **Raw pointer handle with pointer-to-handle close**
   - `ENT_HANDLE`
     - `ENT_SharedMap` as a later candidate if caller-synchronized semantics are ever replaced
4. **Raw service handle with join/close semantics**
   - `ENT_THREAD`
5. **Raw caller-owned primitives**
   - `UTL_LOCK`
   - `UTL_CV`
   - socket descriptors

The strongest pattern in the repository is `UTL_TPOOL`. It is the clearest proof that
a registry + state + activeOps model can work without flattening the API surface.

## Target Runtime-Owner Model

The target model is:

```text
ENT_HANDLE
  -> ENT_CTX
      -> module state
      -> resource registry
          -> log handles
          -> db handles
          -> timers
          -> thread pools
          -> shared maps
          -> thread handles
```

In this model:

- `ENT_HANDLE` stays the top-level runtime instance.
- `ENT_CTX` becomes the owner of runtime-scoped subresources.
- Each subresource keeps its strong public type (`ENT_LOG`, `DB_HANDLE`,
  `UTL_TIMER_T`, `UTL_TPOOL`, `ENT_SharedMap`, `ENT_THREAD`) instead of being
  flattened into `ENT_HANDLE`.
- Runtime-owned resources register on creation and deregister on close.
- Runtime close becomes the final drain-and-destroy entrypoint.

## Why `ENT_HANDLE` Should Be the Owner, Not a Universal Handle

The goal is to make `ENT_HANDLE` an ownership root, not a universal replacement type.

Reasons:

- Strongly typed subresources are easier to reason about than a single monolithic
  handle type.
- Different modules need different cleanup rules, and a universal type would hide that
  complexity instead of removing it.
- Existing code already has service-specific semantics that are worth preserving.
- A universal handle would make the ABI less readable and more fragile.

The right abstraction is:

- keep the specialized handles;
- attach them to the runtime owner when they are runtime-scoped;
- keep caller-owned primitives caller-owned.

## Resource Registry Design

The registry is a conceptual design here, not an implementation requirement for this
task.

### Registry entry shape

Each resource entry should be able to answer:

- what kind of resource it is;
- what public handle it owns;
- which close function destroys it;
- whether it has a dependency on another resource;
- whether it is already closing;
- whether it has active users;
- whether it should be drained before or after other resource types.

### Minimal registry responsibilities

- Register the resource after successful creation.
- Prevent new runtime-owned resources from being created once the runtime is closing.
- Drain resources in dependency order.
- Remove the resource from the registry before freeing the underlying memory.
- Keep the registry internal to the runtime owner.

### Why `UTL_TPOOL` is the reference

`UTL_TPOOL` already has:

- a registry list;
- a state machine;
- activeOps accounting;
- close-waits-drain behavior;
- a clear "reject new work while closing" rule.

That makes it the best reference for the runtime registry design.

## Module State Design

The runtime owner should use a small set of states:

- `ACTIVE`
- `CLOSING`
- `CLOSED`

Subresources may keep module-specific states, but the runtime owner only needs enough
state to decide:

- whether new resources may be created;
- whether new operations may enter;
- whether a close is in progress;
- whether the owner has already been fully destroyed.

The state contract should stay simple:

1. `ACTIVE` accepts new work.
2. `CLOSING` rejects new work and drains existing work.
3. `CLOSED` means no further use is allowed.

## Close Ordering

The recommended runtime shutdown order is:

1. Switch the runtime to `CLOSING`.
2. Reject new runtime-scoped resource creation.
3. `ENT_Stop(ent)` to request top-level stop.
4. Stop timers.
5. Stop thread pools and join worker threads.
6. Close database handles.
7. Close shared maps.
8. Flush and close log handles.
9. Close module services and caller-owned cleanup helpers.
10. Free `ENT_CTX` and finally the `ENT_HANDLE` storage.

The exact destroy order may vary by dependency graph, but the key rule is unchanged:
resources with workers or callbacks must be drained before their backing storage is
freed.

## API Migration Strategy

The migration strategy should be staged and backwards-compatible.

### Short-term

Keep the existing APIs:

- `ENT_DbInitHandle(&db, ...)`
- `ENT_LogInitHandle(&log, ...)`
- `UTL_TimerCreate(&timer, ...)`
  - `UTL_TPoolInit(&pool, ...)`
  - `ENT_SharedMapOpen(&map, ...)` remains caller-synchronized on the legacy path

These remain valid and continue to work on the legacy/default runtime path.

### Medium-term

Add runtime-aware variants as opt-in APIs:

- `ENT_DbInitHandleEx(ENT_HANDLE ent, DB_HANDLE* db, ...)`
- `ENT_LogInitHandleEx(ENT_HANDLE ent, ENT_LOG* log, ...)`
- `ENT_TimerCreateEx(ENT_HANDLE ent, UTL_TIMER_T* timer, ...)`
- `ENT_TPoolInitEx(ENT_HANDLE ent, UTL_TPOOL* pool, ...)`
- `ENT_SharedMapOpenEx(ENT_HANDLE ent, ENT_SharedMap** map, ...)` only if shared-map ownership is later migrated

The `Ex` suffix is only a naming suggestion. The important property is that the
runtime owner is explicit.

### Long-term

Legacy APIs can keep working through a compatibility path, but they should be
documented as legacy-compatible rather than owner-native.

## Compatibility Plan

The compatibility plan should preserve behavior while the runtime owner model is
introduced:

1. Existing public APIs remain available.
2. Old APIs continue to bind to the default runtime or the current active context.
3. Runtime-aware APIs are introduced behind the same semantics, but with explicit
   ownership attachment.
4. Callers that never migrate continue to work, but they remain on the compatibility
   path.
5. New code should prefer runtime-aware APIs once they exist.

This keeps the migration incremental and avoids a single breaking cutover.

## Testing Strategy

The migration should be covered by tests at several layers:

- resource registration and deregistration tests;
- close-order tests;
- already-entered drain tests;
- new-entry rejection tests after close begins;
- stale-handle validation tests;
- callback/free race tests;
- cross-platform Windows / Linux tests for the runtime owner path.

The strongest acceptance signal should be the same as it is today:

- `git diff --check`
- the appropriate module CTest targets
- Windows x64 / Win32 and Linux CI runs

## Rollout Phases

### Phase 1: Audit and contract freeze

- Keep the current public APIs.
- Document the ownership gaps.
- Classify each handle into runtime-owned, service-owned, or caller-owned.

### Phase 2: Runtime owner scaffolding

- Add internal resource registration to `ENT_CTX`.
- Keep the registry private.
- Add tests for registration, draining, and close ordering.

### Phase 3: Runtime-aware opt-in APIs

- Introduce `*Ex` APIs that take `ENT_HANDLE`.
- Route runtime-scoped resources through the owner registry.

### Phase 4: Legacy compatibility and gradual migration

- Keep legacy APIs available.
- Encourage new code to use runtime-aware variants.
- Defer any breaking removal until the compatibility window is explicitly closed.

## Testing and Verification Notes

This design document intentionally does not require a business-code build.
The next implementation step should be a bounded follow-up task with its own
impact-scan and targeted tests.
