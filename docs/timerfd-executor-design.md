# Experimental timerfd callback executor: contract-first design (NO-GO)

**Status:** design proposal and executable **isolated test model**, not a production architecture decision. Tracked by [Issue #23](https://github.com/if16888/ent/issues/23). No changes to the shipping C API/ABI or existing POSIX/microsecond timer backend.

## Finding behind the proposal

The existing Linux perf_timerfd_scale prototype uses one epoll dispatcher and timestamps each timer's firing. It does **not** execute user callbacks. The existing --selftest-hol test demonstrates that if a slow application callback ran on this same dispatcher, it would block dispatch of unrelated timers. Consequently CPU/thread and deadline measurements from the callback-free benchmark cannot establish the performance of an actual callback-capable replacement.

**Proposed separation:** an epoll/timerfd/eventfd scheduling thread must never execute user code. It offers timer expirations to a **bounded worker executor** through a nonblocking submit operation. Callback invocation occurs only in worker threads. Every queue or cancellation decision must be observable in metrics.

## Candidate contract (experimental only)

| Dimension | Proposed behavior in isolated model | Production qualification still needed |
|---|---|---|
| Pool / capacity | Two workers, **one** pending queue slot, four fixed timer IDs for deterministic tests | Select real worker count and bounded memory by measured field workload |
| Submit | Under one mutex: OK, COALESCED, FULL, CANCELLED, CLOSING; never wait for workers | Decide if missed dispatches are acceptable for each timer class |
| Per-timer concurrency | **At most one** active or queued invocation per timer; additional ticks coalesce (are dropped, not replayed) | Compare to fixed-delay/microsecond timer contracts |
| Queue order | FIFO for accepted entries; no priority or deadline scheduling | Design priority, fairness, starvation limits for field control |
| Full queue | Immediate explicit FULL result and overflow counter; no unbounded queue growth | Define alarms and overload response; dropping critical ticks may be unacceptable |
| Cancel from owner | Mark cancelled, remove pending, wait for running callback to finish; future submissions rejected | Generation-safe refcounted handles and repeated Delete semantics |
| Close from owner | Reject submissions, discard pending, wait for active callbacks, join workers | Concurrent Close/Delete lifecycle and shutdown timeout |
| Reentrant Close/Cancel | Called from **any** callback worker: EDEADLK, no shutdown or wait | Existing production self-delete supports deferred cleanup; preserve semantics |
| Cleanup | Join all workers; repeat tests 20x; compare /proc/self/status thread counts | Real timerfd/eventfd/epoll FD lifecycle, sanitizer/fault injection |
| Hung callbacks | Model callbacks return when their test gate opens | Policy for permanently blocked user callbacks |

The executable model is at test/test_timerfd_executor_model.c, registered as Linux CTest test_timerfd_executor_model. **It is not connected to timerfd**, has no public handles, does not manage arbitrary callback context lifetime, and is not a reusable production library. It is an isolated proof of proposed semantics. Bounded queues do not imply a bounded Close latency if a callback hangs.

## Deterministic checks

1. Hold timer 0's callback; timer 1's callback must finish on another worker **before** releasing timer 0. Another timer 0 submit must be COALESCED.
2. Hold two callbacks occupying both workers. Queue timer 2; its duplicate coalesces. Timer 3 returns FULL. Cancel timer 2 and successfully queue timer 3.
3. Close while both workers remain gated; future submits return CLOSING, pending timer 3 is discarded, and Close waits for running callbacks.
4. Cancel a running timer externally; Cancel must wait until it finishes and future submissions are CANCELLED.
5. Attempt Close and Cancel from a callback; both return EDEADLK without self-join.
6. Repeat four test functions **20 times per CTest invocation**, verify post-join threads return to baseline, and run on both normal CI and sanitizer lanes.

Passing the model test demonstrates candidate-contract consistency **only**. It does not establish real-time jitter, capacity, existing API parity or real FD resource safety.

## Experimental real-expiration integration fixture (still NO-GO)

Linux-only `test/test_timerfd_dispatch_executor.c` connects actual **one-shot
`timerfd` readiness** to an `epoll` dispatcher and **two dedicated callback
workers** through a **one-slot bounded queue**. A separate `eventfd` wakes
shutdown. The dispatcher itself never runs application callbacks.

The deterministic fixture exercises, on the real expiration path:

- A gated slow callback on timer 0 while timer 1 finishes on the other
  worker without releasing timer 0 first.
- Both workers occupied; one pending callback accepted, the next expiration
  rejected as `FULL` without blocking the dispatcher.
- Re-arming a busy timer yields `COALESCED`, without concurrent callbacks.
- Cancelling a pending timer removes it from the queue and subsequent
  expirations yield `CANCELLED`.
- **20 construction/shutdown cycles** with `/proc/self/fd` and thread-count
  checks after all workers are joined. A three-second condition-variable
  timeout reports failed progress instead of relying on unbounded sleeps.

It is included in Linux CTest as `test_timerfd_dispatch_executor`; it is
test-only, does not build into `ent`, does not alter production timer source,
and does not change its API or ABI. This proves **bounded callback isolation
on actual kernel timer events in the narrow fixture only**, not correctness
under arbitrary concurrency or hard real-time deadline compliance.

**Still missing** before production use: generation-safe public handles,
callback self-delete and reentrancy semantics, concurrent Delete/Close/Destroy
ownership and fault injection, persistent overload/backpressure policy for
control-critical timers, fixed-delay re-arm parity and the distinct microsecond
worker contract, callback-inclusive comparable P99 deadline-lag benchmarks
under full effective four-CPU load, and explicit resource/rollback budgets.
The fixture intentionally does not replace the separate performance prototype.
Production timerfd migration stays **NO-GO**.

## Unresolved production architecture gates

- Classify timers by deadline-critical versus best-effort behavior: whether coalescing or queue-full dropping is acceptable must be explicitly proven, especially for protection/control.
- Define cross-timer order: worker pool FIFO only controls acceptance, **not callback completion order**.
- Current millisecond fixed-delay periods rearm *after callback completion*. A coalescing model is not automatically equivalent; Linux microsecond timers have a distinct scheduler.
- Set an execution-time budget per callback and safe behavior when *all* workers are stuck. Adding workers alone cannot eliminate total saturation.
- Define ownership for Cancel concurrent with Close/Destroy and reuse of timer IDs; the model assumes externally controlled destroy and no races with its memory deallocation.
- Establish queue depth, overflow reporting, worst-run deadline P99 budget, FD/thread/memory resource limits and rollback criteria.

**Next gate:** review the contract against the existing public API, then combine isolated timerfd scheduling and bounded callback execution in a separate opt-in experimental target. Add timerfd FD lifecycle and concurrent ownership tests, and compare **callback-inclusive** latency against legacy. Require controlled four-CPU evidence and a reversible migration design before reconsidering Issue #23. **Production replacement remains NO-GO.**
