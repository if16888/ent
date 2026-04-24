# UTL Timer Linux RT Optimization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 `utl_timer` 增加 Linux RT 专用实现路径，使其更适合 `1 ms` 周期、目标抖动控制在 `100 us` 以内的用户态实时场景。

**Architecture:** 保留现有 `UTL_TimerCreate(..., int ms, ...)` 路径作为兼容实现，新增 Linux RT 专用 `UTL_TimerCreateUs(...)` 路径。RT 路径内部统一使用 `ns`，基于 `CLOCK_MONOTONIC` 和专用高优先级 worker 线程，通过 `clock_nanosleep(..., TIMER_ABSTIME)` 做绝对时间调度，并将调度与用户回调执行解耦。验证继续依赖 `perf_utl_timer_rt`。

**Tech Stack:** C, CMake, pthread, clock_nanosleep, CLOCK_MONOTONIC, Linux scheduling APIs, existing ent timer APIs

---

### Task 1: Extend Public Timer API For Linux RT

**Files:**
- Modify: `inc/ent_utility.h`
- Modify: `comm/utl_timer.c`
- Test: `test/perf_utl_timer_rt.c`

- [ ] **Step 1: Write the failing compile-time API usage**

Add the new declaration to `inc/ent_utility.h`:

```c
ENT_PUBLIC MSG_ID_T UTL_TimerCreateUs(UTL_TIMER_T* pTimer, unsigned int type, int period_us, UTL_TIMER_EV_F evCb, void* data);
```

Then update `test/perf_utl_timer_rt.c` to call `UTL_TimerCreateUs(...)` instead of `UTL_TimerCreate(...)`:

```c
if(UTL_TimerCreateUs(&timer, UTL_TIMER_E_PERIOD, target_period_us, perf_rt_timer_cb, &probe) != 0)
{
    fprintf(stderr, "UTL_TimerCreateUs failed\n");
    goto CLEANUP;
}
```

- [ ] **Step 2: Build to verify it fails**

Run:

```bash
cmake -S /Users/lifei/test/ent -B /Users/lifei/test/ent/build
cmake --build /Users/lifei/test/ent/build --target perf_utl_timer_rt
```

Expected: build fails because `UTL_TimerCreateUs` is declared but not implemented.

- [ ] **Step 3: Add minimal stub implementation**

In `comm/utl_timer.c`, add a temporary stub near other public timer APIs:

```c
MSG_ID_T UTL_TimerCreateUs(UTL_TIMER_T* pTimer, unsigned int type, int period_us, UTL_TIMER_EV_F evCb, void* data)
{
    (void)pTimer;
    (void)type;
    (void)period_us;
    (void)evCb;
    (void)data;
    return -1;
}
```

- [ ] **Step 4: Build again to verify the target links but runtime stays red**

Run:

```bash
cmake --build /Users/lifei/test/ent/build --target perf_utl_timer_rt
/Users/lifei/test/ent/build/bin/perf_utl_timer_rt 1000 5000 0 80
```

Expected: build succeeds; on Linux this would fail at runtime with `UTL_TimerCreateUs failed`. On Darwin it will still stop at the Linux-only guard.

- [ ] **Step 5: Commit**

```bash
git add inc/ent_utility.h comm/utl_timer.c test/perf_utl_timer_rt.c
git commit -m "feat: add timer us api surface"
```

### Task 2: Introduce Linux RT Timer Context And Worker Lifecycle

**Files:**
- Modify: `comm/utl_timer.c`
- Modify: `inc/ent_utility.h`

- [ ] **Step 1: Write the failing internal shape for RT context**

Add Linux RT-only fields to `TIMER_CTX_T`:

```c
#elif ENT_TIMER_IMPL_LINUX
    timer_t           timerId;
    long long         period_ns;
    long long         next_deadline_ns;
    pthread_t         rtWorker;
    pthread_t         cbWorker;
    UTL_LOCK          cbLock;
    UTL_CV            cbCv;
    BOOL              cbPending;
    BOOL              stopWorker;
    BOOL              stopCallbackWorker;
    BOOL              rtMode;
```

Do not implement behavior yet.

- [ ] **Step 2: Build to verify it still compiles**

Run: `cmake --build /Users/lifei/test/ent/build --target perf_utl_timer_rt`

Expected: build succeeds because fields are additive only.

- [ ] **Step 3: Add worker helper declarations with temporary failure path**

Add Linux-only internal helpers:

```c
static long long iUTL_TimerMonotonicNs(void);
static void* iUTL_TimerRtWorker(void* data);
static void* iUTL_TimerCallbackWorker(void* data);
static MSG_ID_T iUTL_TimerCreateRt(UTL_TIMER_T* pTimer, unsigned int type, int period_us, UTL_TIMER_EV_F evCb, void* data);
static MSG_ID_T iUTL_TimerDeleteRt(PTIMER_CTX_T timerCtx);
```

Implement `iUTL_TimerCreateRt(...)` as temporary `return -9;`.

- [ ] **Step 4: Wire the public RT API to the RT helper**

Update:

```c
MSG_ID_T UTL_TimerCreateUs(UTL_TIMER_T* pTimer, unsigned int type, int period_us, UTL_TIMER_EV_F evCb, void* data)
{
#if ENT_TIMER_IMPL_LINUX
    return iUTL_TimerCreateRt(pTimer, type, period_us, evCb, data);
#else
    (void)pTimer;
    (void)type;
    (void)period_us;
    (void)evCb;
    (void)data;
    return -1;
#endif
}
```

- [ ] **Step 5: Build and verify Linux path remains intentionally incomplete**

Run: `cmake --build /Users/lifei/test/ent/build --target perf_utl_timer_rt`

Expected: build succeeds; Linux runtime would still fail with RT helper returning `-9`.

- [ ] **Step 6: Commit**

```bash
git add comm/utl_timer.c inc/ent_utility.h
git commit -m "refactor: add linux rt timer context"
```

### Task 3: Implement CLOCK_MONOTONIC Absolute-Time RT Worker

**Files:**
- Modify: `comm/utl_timer.c`

- [ ] **Step 1: Write the failing RT worker path**

In `iUTL_TimerCreateRt(...)`, allocate a timer context, initialize:

- `tag`
- `timerType`
- `timer_ev_cb`
- `data`
- `period_ns = (long long)period_us * 1000LL`
- `rtMode = TRUE`

Then return `-10` before thread creation.

- [ ] **Step 2: Build to verify compile success**

Run: `cmake --build /Users/lifei/test/ent/build --target perf_utl_timer_rt`

Expected: build succeeds.

- [ ] **Step 3: Implement monotonic helper and absolute sleep worker**

Add:

```c
static long long iUTL_TimerMonotonicNs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}
```

Implement the RT worker:

```c
static void* iUTL_TimerRtWorker(void* data)
{
    PTIMER_CTX_T timerCtx = (PTIMER_CTX_T)data;

    timerCtx->next_deadline_ns = iUTL_TimerMonotonicNs() + timerCtx->period_ns;

    while(!timerCtx->stopWorker)
    {
        struct timespec ts;
        long long deadline = timerCtx->next_deadline_ns;

        ts.tv_sec = deadline / 1000000000LL;
        ts.tv_nsec = deadline % 1000000000LL;
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);

        if(timerCtx->stopWorker)
        {
            break;
        }

        UTL_LockEnter(timerCtx->cbLock);
        timerCtx->cbPending = TRUE;
        UTL_LockLeave(timerCtx->cbLock);
        UTL_CVWake(timerCtx->cbCv);

        if(timerCtx->timerType & UTL_TIMER_E_ONESHOT)
        {
            break;
        }

        timerCtx->next_deadline_ns += timerCtx->period_ns;
    }

    return NULL;
}
```

- [ ] **Step 4: Start the RT worker from `iUTL_TimerCreateRt`**

Add:

- `UTL_LockInit(&timerCtx->cbLock, "timer_rt_cb")`
- `UTL_CVInit(&timerCtx->cbCv, "timer_rt_cb")`
- `pthread_create(&timerCtx->rtWorker, NULL, iUTL_TimerRtWorker, timerCtx)`

Insert the timer into `sTimerCtx.dllHeader` only after worker startup succeeds.

- [ ] **Step 5: Build again**

Run: `cmake --build /Users/lifei/test/ent/build --target perf_utl_timer_rt`

Expected: build succeeds.

- [ ] **Step 6: Commit**

```bash
git add comm/utl_timer.c
git commit -m "feat: add monotonic absolute timer worker"
```

### Task 4: Decouple Callback Execution From RT Scheduling

**Files:**
- Modify: `comm/utl_timer.c`

- [ ] **Step 1: Write the failing callback worker hookup**

Keep the RT worker from directly calling the user callback. Add a callback worker stub that returns immediately:

```c
static void* iUTL_TimerCallbackWorker(void* data)
{
    (void)data;
    return NULL;
}
```

This preserves compile success but makes Linux runtime behavior incomplete.

- [ ] **Step 2: Implement the callback worker loop**

Use the lock/CV pair to wait for pending callbacks:

```c
static void* iUTL_TimerCallbackWorker(void* data)
{
    PTIMER_CTX_T timerCtx = (PTIMER_CTX_T)data;

    while(!timerCtx->stopCallbackWorker)
    {
        UTL_LockEnter(timerCtx->cbLock);
        while(!timerCtx->cbPending && !timerCtx->stopCallbackWorker)
        {
            UTL_CVWait(timerCtx->cbCv, timerCtx->cbLock, 0, RW_WRITE_E);
        }
        if(timerCtx->stopCallbackWorker)
        {
            UTL_LockLeave(timerCtx->cbLock);
            break;
        }
        timerCtx->cbPending = FALSE;
        UTL_LockLeave(timerCtx->cbLock);

        if(timerCtx->timer_ev_cb)
        {
            timerCtx->timer_ev_cb(timerCtx->data);
        }

        if(timerCtx->timerType & UTL_TIMER_E_ONESHOT)
        {
            break;
        }
    }

    return NULL;
}
```

- [ ] **Step 3: Start callback worker during RT create**

Add:

```c
if(pthread_create(&timerCtx->cbWorker, NULL, iUTL_TimerCallbackWorker, timerCtx) != 0)
{
    /* rollback lock/cv/rtWorker */
}
```

- [ ] **Step 4: Build to verify it compiles**

Run: `cmake --build /Users/lifei/test/ent/build --target perf_utl_timer_rt`

Expected: build succeeds.

- [ ] **Step 5: Commit**

```bash
git add comm/utl_timer.c
git commit -m "feat: decouple rt timer callback execution"
```

### Task 5: Implement RT Delete/Close Semantics

**Files:**
- Modify: `comm/utl_timer.c`

- [ ] **Step 1: Write the failing RT delete path**

Hook `UTL_TimerDelete(...)` so that Linux RT contexts enter `iUTL_TimerDeleteRt(timerCtx)`, but initially return `-11`.

- [ ] **Step 2: Build to verify compile success**

Run: `cmake --build /Users/lifei/test/ent/build --target perf_utl_timer_rt`

Expected: build succeeds.

- [ ] **Step 3: Implement RT delete helper**

`iUTL_TimerDeleteRt(...)` should:

- set `stopWorker = TRUE`
- set `stopCallbackWorker = TRUE`
- wake the callback worker CV
- `pthread_join(rtWorker, NULL)`
- `pthread_join(cbWorker, NULL)`
- remove the timer from `sTimerCtx.dllHeader`
- close `cbCv` and `cbLock`
- zero/free context

Use this structure:

```c
static MSG_ID_T iUTL_TimerDeleteRt(PTIMER_CTX_T timerCtx)
{
    DLL_D_HDR* tmpDll = NULL;

    timerCtx->stopWorker = TRUE;
    timerCtx->stopCallbackWorker = TRUE;
    UTL_CVWakeAll(timerCtx->cbCv);

    pthread_join(timerCtx->rtWorker, NULL);
    pthread_join(timerCtx->cbWorker, NULL);

    UTL_LockEnter(sTimerCtx.dllLock);
    UTL_DllRemCurr((DLL_D_HDR*)timerCtx, &tmpDll);
    UTL_LockLeave(sTimerCtx.dllLock);

    UTL_CVClose(&timerCtx->cbCv);
    UTL_LockClose(&timerCtx->cbLock);
    memset(timerCtx, 0, sizeof(*timerCtx));
    free(timerCtx);
    return 0;
}
```

- [ ] **Step 4: Update `UTL_TimerClose()` to work with RT contexts**

Keep existing traversal logic, but ensure RT-created timers are deleted through the RT helper path without leaking worker threads.

- [ ] **Step 5: Build to verify it compiles**

Run: `cmake --build /Users/lifei/test/ent/build --target perf_utl_timer_rt`

Expected: build succeeds.

- [ ] **Step 6: Commit**

```bash
git add comm/utl_timer.c
git commit -m "fix: add rt timer cleanup path"
```

### Task 6: Point Linux RT Evaluator At New Path

**Files:**
- Modify: `test/perf_utl_timer_rt.c`

- [ ] **Step 1: Keep the evaluator on the new API**

Ensure `perf_utl_timer_rt.c` uses:

```c
if(UTL_TimerCreateUs(&timer, UTL_TIMER_E_PERIOD, target_period_us, perf_rt_timer_cb, &probe) != 0)
{
    fprintf(stderr, "UTL_TimerCreateUs failed\n");
    goto CLEANUP;
}
```

- [ ] **Step 2: Tighten runtime reporting**

Add one more runtime field to the `result` or `runtime` output indicating the evaluator is using the RT path:

```c
printf("runtime affinity_set=%d sched_set=%d actual_policy=%s actual_priority=%d timer_path=rt_us\n",
       affinity_set,
       sched_set,
       policy_name(actual_policy),
       actual_priority);
```

- [ ] **Step 3: Build to verify it compiles**

Run: `cmake --build /Users/lifei/test/ent/build --target perf_utl_timer_rt`

Expected: build succeeds.

- [ ] **Step 4: On Darwin, verify non-Linux failure path still works**

Run: `/Users/lifei/test/ent/build/bin/perf_utl_timer_rt 1000 5000 0 80`

Expected: still exits non-zero with the clear Linux-only message.

- [ ] **Step 5: Commit**

```bash
git add test/perf_utl_timer_rt.c
git commit -m "perf: point rt evaluator at us timer path"
```

### Task 7: Final Verification

**Files:**
- Verify: `inc/ent_utility.h`
- Verify: `comm/utl_timer.c`
- Verify: `test/perf_utl_timer_rt.c`
- Verify: `test/CMakeLists.txt`

- [ ] **Step 1: Reconfigure and rebuild**

Run:

```bash
cmake -S /Users/lifei/test/ent -B /Users/lifei/test/ent/build
cmake --build /Users/lifei/test/ent/build --target perf_utl_timer_rt
```

Expected: build succeeds.

- [ ] **Step 2: Verify default tests are not broken**

Run: `ctest --output-on-failure`

Expected: existing registered tests still pass.

- [ ] **Step 3: Verify Darwin/Linux guard locally**

Run: `/Users/lifei/test/ent/build/bin/perf_utl_timer_rt 1000 5000 0 80`

Expected on this machine: non-zero exit with `perf_utl_timer_rt requires Linux`.

- [ ] **Step 4: Record the Linux RT real-run command**

Use this command in the final summary:

```bash
/Users/lifei/test/ent/build/bin/perf_utl_timer_rt 1000 100000 0 80
```

Expected on Linux RT: approximately 100 seconds of `1 ms` jitter data, with `p99`, `p999`, and `over_100us_ratio`.

- [ ] **Step 5: Commit the verification-ready state**

```bash
git add inc/ent_utility.h comm/utl_timer.c test/perf_utl_timer_rt.c test/CMakeLists.txt
git commit -m "feat: add linux rt timer path"
```
