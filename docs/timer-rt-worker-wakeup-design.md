# Linux microsecond RT worker: deadline wait + prompt stop notification (design only, NO-GO)

**Status:** design proposal for the T08 follow-up in [Issue #23](https://github.com/if16888/ent/issues/23). No production code changes in this document. The shipping `UTL_TimerCreateUs` worker is unchanged and the timerfd migration remains **NO-GO**.

## Problem

`iUTL_TimerRtWorker` (`comm/utl_timer.c`) waits for its next deadline with repeated
`clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME)` slices capped at
`UTL_TIMER_RT_STOP_POLL_NS = 1 ms`. The cap exists only so the worker notices
`stopWorker`, which is set by `iUTL_TimerRtRequestStop()` and currently wakes only
the callback condition variable (`cbCv`), not the RT worker.

Consequence: **one wakeup per millisecond per timer, independent of the timer
period**, plus one scheduling/callback wakeup per expiration. The cost is paid for
the whole lifetime of every microsecond timer.

### Measured baseline (in-tree tooling, after PR #57)

`perf_timer_scale` now reports process-wide `voluntary_ctxt_switches`
(`getrusage(RUSAGE_SELF).ru_nvcsw`) and CPU over the same window. Linux x64
container, 2 vCPU, 4 s per cell:

| timers | period | hits | CPU % | voluntary ctxt switches |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 1 ms | 4,000 | 1.15% | 8,004 |
| 1 | 20 ms | 199 | 0.41% | 3,996 |
| 1 | 1000 ms | 3 | 0.35% | 3,790 |
| 16 | 1 ms | 64,071 | 12.83% | 128,202 |
| 16 | 20 ms | 3,200 | 4.91% | 64,312 |
| 16 | 1000 ms | 64 | 4.37% | 60,922 |
| 64 | 1 ms | 257,357 | 60.65% | 514,994 |
| 64 | 20 ms | 12,834 | 23.41% | 262,080 |
| 64 | 1000 ms | 256 | 19.14% | 243,862 |

At 64 timers and a 1000 ms period, 256 expirations cost ~244k context switches
and 19.1% of one core. Wakeups stay near 2000/s per timer regardless of period.
For comparison, the legacy millisecond timer (`perf_timer_scale ms`) uses 0.33%
CPU for the same 64×1000 ms workload.

## Scope

- **In scope:** the Linux microsecond RT worker (`iUTL_TimerRtWorker`) wait
  mechanism only.
- **Out of scope:** timerfd/eventfd scheduling architecture, public API/ABI,
  callback executor design, millisecond timer thread, POSIX fallback thread,
  scheduling policy/priority changes.
- **No production switch** happens until the acceptance gates below are reviewed
  with before/after evidence.

## Current semantics that must be preserved

| Behavior | Current implementation | Preservation requirement |
|---|---|---|
| First expiration | `next_deadline_ns = now + period_ns` | First callback still occurs one period after creation |
| Periodic catch-up | Expired ticks advance `next_deadline_ns` by `(missed + 1) * period_ns`; callbacks coalesce via `pendingCallbacks` 0/1 | One callback per wakeup; missed ticks never replay |
| Coalescing | `pendingCallbacks` is a count; callback worker drains up to 64 per wake | No duplicate callbacks for one expiration |
| One-shot | Worker exits after the first expiration; callback worker delivers | One-shot still fires exactly once |
| Self-delete | `pthread_equal(pthread_self(), cbWorker)` in `iUTL_TimerDeleteRt`; detach + defer cleanup | Deleting from a callback still works |
| External delete | `iUTL_TimerRtRequestStop()` → join RT worker → join callback worker → free | Pending callback behavior unchanged |
| Delete/Close ordering | Deterministic tests in `test_utl_timer` / `test_utl_timer_rt_self_delete` | Same return codes and ordering |
| Stop latency | Bounded by the 1 ms poll slice | Must not get worse; prompt wake is the goal |
| Callback wake | RT worker wakes `cbCv` with `UTL_CVWake` when a callback is pending | Callback worker must never miss this wake |
| Error paths | `clock_gettime`/`clock_nanosleep` failure stops the worker | Same failure handling |
| Test hooks | `ENT_TIMER_TEST_HOOKS` counters, `iUTL_TimerTestRtContext*` | Same observable test behavior |
| Resources | 0 extra FDs, 2 threads per timer | No thread growth; FDs only if the option below needs them, and documented |

### Why the callback CV must not be reused

`UTL_CVWake(timerCtx->cbCv)` is the single-waiter signal that wakes the callback
worker. If the RT worker also waited on `cbCv`, a callback wake could be consumed
by the RT worker, delaying the callback until the next expiration — exactly the
"loses the callback-worker wake" hazard called out in Issue #23. Any new wait
primitive must be **independent** of `cbCv`.

## Proposed design

### Primary: dedicated CLOCK_MONOTONIC condition variable per RT timer

Add one internal member to the Linux RT context:

```
pthread_cond_t workerCv;   /* condattr clock = CLOCK_MONOTONIC */
```

- `workerCv` is **associated with the existing `cbLock`**, not a new mutex. The
  `stopWorker` predicate is already set and read under `cbLock`; reusing that
  mutex keeps the predicate single-lock and avoids introducing lock ordering
  between the stop path and the callback path.
- `workerCv` is independent of `cbCv`, so a deadline wake can never consume the
  callback-worker wake.
- `workerCv` is created with `pthread_condattr_setclock(CLOCK_MONOTONIC)`, so
  `pthread_cond_timedwait` takes an absolute `CLOCK_MONOTONIC` timespec with
  nanosecond resolution (the existing `UTL_CVWaitFor` wrapper is millisecond
  granularity and cannot serve microsecond timers).

Wait loop (replaces the 1 ms slice loop):

```
deadline = next_deadline_ns;
lock(cbLock);
while(!stopWorker) {
    now = clock_gettime(CLOCK_MONOTONIC);
    if(now >= deadline) break;
    ts = deadline;                      /* absolute monotonic */
    rc = pthread_cond_timedwait(&workerCv, &cbLock, &ts);
    if(rc == ETIMEDOUT) break;
    if(rc != 0) { fail = 1; break; }
    /* rc == 0: spurious wake, the absolute deadline above is re-checked */
}
unlock(cbLock);
```

- Normal wakeups: **one per expiration** (plus spurious wakes, which are
  re-checked against the absolute deadline and do not consume extra periods).
- Stop: `iUTL_TimerRtRequestStop()` already sets `stopWorker` under `cbLock`;
  it additionally broadcasts `workerCv` before waking `cbCv`. A signal that
  arrives before the wait starts is safe: the predicate is already true and the
  wait is skipped — no lost wakeup.
- `pthread_cond_timedwait` returns `ETIMEDOUT` at the deadline; the worker then
  performs the existing expiration/coalescing/advance logic unchanged.
- `clock_gettime` failure keeps the current `sleepFailed` behavior.

### Fallback: keep the 1 ms polling path

If `pthread_condattr_setclock`/initialization fails, the context keeps using the
current 1 ms polling loop. This keeps the change fail-safe and reversible on
platforms or libcs where the monotonic condattr is unavailable. The fallback is
selected once per timer at creation and is not user-visible.

### Initialization and cleanup

- `workerCv` is initialized in `iUTL_TimerCreateRt` together with `cbCv`,
  **before** either worker thread starts, so the `CREATE_FAILED` path can
  broadcast it safely even when only one thread was created.
- It must be destroyed in **both** RT cleanup paths: `iUTL_TimerDeleteRt`
  (external delete) and the callback-worker self-delete cleanup block. The RT
  worker is always joined before either path destroys the context.
- If monotonic-condattr init fails, the context records "no worker CV" and keeps
  the 1 ms polling loop; cleanup skips the destroy.

### Alternative considered: per-timer eventfd + ppoll

A per-timer `eventfd(EFD_CLOEXEC|EFD_NONBLOCK)` plus `ppoll` with a relative
nanosecond timeout also gives deadline wait plus prompt stop, with no mutex. It
costs **one FD per timer** (up to 128 extra FDs at the guardrail) and requires
recomputing the remaining timeout after each wake. It is the fallback candidate if
the condvar approach shows measurable wakeup or latency problems, but the condvar
option is preferred because it adds no FD and keeps the deadline absolute.

### Rejected options

| Option | Why rejected |
|---|---|
| Reuse `cbCv` for the deadline wait | Can consume the callback-worker wake (Issue #23 warning) |
| `UTL_CVWaitFor` (ms timeout) | Millisecond granularity loses µs deadline accuracy |
| Per-expiration `timerfd` | That is the architecture migration, still NO-GO |
| Busy-wait / shorter slices | Increases wakeups; opposite of the goal |

## Reversibility

- The change is internal to `comm/utl_timer.c`; **no public header, symbol,
  message code, or ABI change**.
- The polling loop stays in the source as the init-failure fallback, so the new
  path can be disabled by a small revert of one commit without touching callers.
- `git revert` of the implementation commit restores the current behavior; no
  data or configuration migration exists.

## Acceptance gates (pre-registered; review before implementation)

All measurements use the in-tree `perf_timer_scale` (wakeup counters + CPU) and
`scripts/timer_baseline.py` (20 trials per scenario), same host and affinity for
before/after, with raw CSV archived.

1. **Wakeups:** for periods ≥ 20 ms, volatile context switches per timer per
   second ≤ `4 / period + 4` (two worker wakeups per expiration plus margin; the
   counter includes both the RT and callback workers); at 1 ms, no worse than the
   ~2,000/s/timer baseline. The 64×1000 ms, 4 s cell must drop from ~244k to
   **≤ 2,000** (theoretical ≈ 640: 64 timers × 4 expirations × 2 workers plus
   stops).
2. **CPU:** 64×1000 ms ≤ **3%** of one core (baseline 19.1%); 64×20 ms ≤ **5%**
   (baseline 23.4%); 64×1 ms no worse than baseline (callback-dominated).
3. **Timing tails:** median across 20 trials of per-trial p99 inter-arrival error
   ≤ **1.10×** baseline in every scenario; worst run ≤ **1.25×** baseline.
   Deadline-lag (`ms` mode) remains the same definition.
4. **Stop/delete latency:** p99 of `delete_p99_us` no worse than baseline
   (bounded by the old 1 ms slice); target < 100 µs.
5. **Lifecycle:** existing `test_utl_timer`, `test_utl_timer_thread_safety`,
   `test_utl_timer_rt_self_delete` pass 50 consecutive runs on x64; timerfd
   fixtures unchanged.
6. **Sanitizers/platforms:** Linux ASan+UBSan, TSan and ARM64 self-hosted CI
   green; Windows unaffected (RT path is Linux-only).
7. **Resources:** threads stay at 2 per timer; FDs stay at zero added per timer
   (condvar option). No ABI/public API change (`Linux abi` job green).

Until 1–7 are reviewed with archived evidence, the current worker remains the
default and the timerfd migration stays **NO-GO**.

## Implementation plan (small, reviewable steps)

1. Add `workerCv` to the RT context with monotonic condattr init and cleanup; no
   behavior change yet (still polling).
2. Switch the wait loop to the absolute timedwait; keep polling as the
   init-failure fallback.
3. Add stop-latency and spurious-wake tests under `ENT_TIMER_TEST_HOOKS`.
4. Collect the T08 matrix and 20-trial baseline before/after on the same host.
5. Review the evidence, then decide whether to keep the default or revert.

## Residual risks

- `pthread_cond_timedwait` wakeup latency depends on the platform; measured tails
  may differ from `clock_nanosleep` and must be compared, not assumed.
- Coalescing still drops missed ticks by design; control-critical timer classes
  need a separate product decision (unchanged from today).
- The condvar reuses `cbLock` and does not hold it while waiting
  (`pthread_cond_timedwait` releases it), so added contention with the callback
  path is limited to the predicate check.
- The change does not serialize callbacks or alter scheduling policy.
- Short periods (near 1 µs) remain callback/CPU dominated; the change targets
  long-period wakeup waste, not sub-microsecond scheduling.
- No real-time guarantee is implied by either mechanism.

## References

- Issue #23 (acceptance gates, measurement history).
- `test/perf_timer_scale.c`, `scripts/timer_baseline.py` (wakeup/CPU tooling).
- `docs/timerfd-executor-design.md` (separate, still NO-GO architecture).
- `comm/utl_timer.c`: `iUTL_TimerRtWorker`, `iUTL_TimerRtRequestStop`,
  `iUTL_TimerCallbackWorker`, `iUTL_TimerDeleteRt`.
