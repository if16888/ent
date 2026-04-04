# UTL Timer RT Fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to execute each task with TDD and review checkpoints.

**Goal:** Address the four critical RT issues: ensure each RT tick is delivered (`cbPending` count), avoid self-join deadlock in RT delete, timestamp at scheduler wake-up, and define missed-deadline behavior.

**Architecture:** Modifiy `comm/utl_timer.c` RT path and `test/perf_utl_timer_rt.c`.  Maintain compatibility with existing ms API.

---

### Task 1: Turn `cbPending` into a queue counter

**Files:** `comm/utl_timer.c`

- [ ] **Step 1:** Update RT context to include `volatile int pending_ticks`.
- [ ] **Step 2:** Change RT worker to increment `pending_ticks` (not boolean) for each wake, even when callback worker busy.
- [ ] **Step 3:** Callback worker loops while `pending_ticks > 0`, decrementing per callback.
- [ ] **Step 4:** Bind callback worker wake-up signal accordingly.
- [ ] **Step 5:** Rebuild `perf_utl_timer_rt`, confirm it still hits Linux guard.
- [ ] **Step 6:** Commit with message `fix: deliver all rt ticks`.

### Task 2: Prevent self-join in RT delete

**Files:** `comm/utl_timer.c`

- [ ] **Step 1:** Add helper `static BOOL iUTL_TimerIsCaller(PTIMER_CTX_T ctx, pthread_t thread)` returning TRUE if current thread matches `cbWorker` or `rtWorker`.
- [ ] **Step 2:** In `iUTL_TimerDeleteRt`, check before `pthread_join`; if calling thread equals worker, skip join.
- [ ] **Step 3:** Ensure flag set before waking to avoid handling on same thread.
- [ ] **Step 4:** Rebuild perf target, verify no new warnings.
- [ ] **Step 5:** Commit `fix: avoid rt self join`.

### Task 3: Timestamp at scheduler wake-up and expose jitter

**Files:** `comm/utl_timer.c`, `test/perf_utl_timer_rt.c`

- [ ] **Step 1:** In RT worker, capture actual wake time into new array `last_wake_ns`.
- [ ] **Step 2:** Export `last_wake_ns` or pass to perf tool via callback argument.
- [ ] **Step 3:** Update perf tool to record `last_wake_ns` before invoking callback; use that for jitter stats.
- [ ] **Step 4:** Validate Linux guard still fires on mac.
- [ ] **Step 5:** Commit `feat: record rt wakes`.

### Task 4: Define missed-deadline handling

**Files:** `comm/utl_timer.c`

- [ ] **Step 1:** Introduce policy flag (e.g., `BOOL drop_missed_ticks`) in RT context.
- [ ] **Step 2:** In RT worker, if current time > deadline + period, decide to skip delta cycles or catch up; update `next_deadline_ns` accordingly.
- [ ] **Step 3:** Document behavior and ensure `period_ns` advances at least once per loop.
- [ ] **Step 4:** Rebuild and run `ctest`.
- [ ] **Step 5:** Commit `refactor: handle missed rt deadlines`.

### Task 5: Final verification

**Files:** same

- [ ] **Step 1:** cmake rebuild perf and timer targets.
- [ ] **Step 2:** Run full `ctest --output-on-failure`.
- [ ] **Step 3:** Run perf tool locally to ensure Linux guard still holds.
- [ ] **Step 4:** Commit all outstanding changes with summary.
