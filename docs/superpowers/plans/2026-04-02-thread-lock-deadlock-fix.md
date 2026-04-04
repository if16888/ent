# Thread Lock Deadlock Fix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove the confirmed lock-held wait/join deadlock risks in `ent_thread` and the lock-held spin risk in `utl_tpool` without changing the public API.

**Architecture:** Keep `dllLock` and `taskLock` responsible only for protecting list state and short-lived shared metadata. Move all blocking waits and joins outside those locks, and add focused regression tests that verify the new lock scope using stubs and call-order instrumentation.

**Tech Stack:** C99, pthreads, CMake, ctest

---

### Task 1: Add Failing Regression Tests For `ent_thread`

**Files:**
- Modify: `test/test_ent_thread.c`
- Test: `test/test_ent_thread.c`

- [ ] **Step 1: Write the failing tests**

```c
static int s_lock_enter_calls = 0;
static int s_lock_leave_calls = 0;
static int s_join_calls = 0;
static int s_cancel_calls = 0;
static int s_join_seen_inside_lock = 0;
static int s_cancel_seen_inside_lock = 0;
static int s_lock_depth = 0;

MSG_ID_T UTL_LockEnter(UTL_LOCK lock)
{
    (void)lock;
    s_lock_enter_calls++;
    s_lock_depth++;
    return 0;
}

MSG_ID_T UTL_LockLeave(UTL_LOCK lock)
{
    (void)lock;
    s_lock_leave_calls++;
    s_lock_depth--;
    return 0;
}

int pthread_join(pthread_t thread, void** retval)
{
    (void)thread;
    if(retval != NULL)
    {
        *retval = NULL;
    }
    s_join_calls++;
    if(s_lock_depth > 0)
    {
        s_join_seen_inside_lock = 1;
    }
    return 0;
}

int pthread_cancel(pthread_t thread)
{
    (void)thread;
    s_cancel_calls++;
    if(s_lock_depth > 0)
    {
        s_cancel_seen_inside_lock = 1;
    }
    return 0;
}

static int test_thread_wait_joins_outside_dll_lock(void)
{
    ENT_THREAD handle = NULL;
    ENT_THREAD_ID tid = NULL;
    int value = 7;

    reset_thread_lock_probes();

    if(expect_true(ENT_ThreadInit(&handle) == 0, "ENT_ThreadInit should create a thread context") != 0)
    {
        return 1;
    }

    if(expect_true(ENT_ThreadCreate(&tid, handle, quick_thread, &value) == 0,
                   "ENT_ThreadCreate should create a joinable thread") != 0)
    {
        ENT_ThreadClose(handle);
        return 1;
    }

    if(expect_true(ENT_ThreadWaitById(&tid, handle, 0) == 0,
                   "ENT_ThreadWaitById should still join successfully") != 0)
    {
        ENT_ThreadClose(handle);
        return 1;
    }

    if(expect_true(s_join_seen_inside_lock == 0,
                   "ENT_ThreadWaitById should not call pthread_join while dllLock is held") != 0)
    {
        ENT_ThreadClose(handle);
        return 1;
    }

    return expect_true(ENT_ThreadClose(handle) == 0, "ENT_ThreadClose should release the thread context");
}

static int test_thread_close_cancels_and_joins_outside_dll_lock(void)
{
    ENT_THREAD handle = NULL;
    ENT_THREAD_ID tid = NULL;
    int value = 9;

    reset_thread_lock_probes();

    if(expect_true(ENT_ThreadInit(&handle) == 0, "ENT_ThreadInit should create a thread context") != 0)
    {
        return 1;
    }

    if(expect_true(ENT_ThreadCreate(&tid, handle, sleepy_thread, &value) == 0,
                   "ENT_ThreadCreate should create a joinable thread for close testing") != 0)
    {
        ENT_ThreadClose(handle);
        return 1;
    }

    if(expect_true(ENT_ThreadClose(handle) == 0, "ENT_ThreadClose should close a populated thread context") != 0)
    {
        return 1;
    }

    if(expect_true(s_cancel_seen_inside_lock == 0,
                   "ENT_ThreadClose should not call pthread_cancel while dllLock is held") != 0)
    {
        return 1;
    }

    return expect_true(s_join_seen_inside_lock == 0,
                       "ENT_ThreadClose should not call pthread_join while dllLock is held");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_ent_thread && ./build/bin/test_ent_thread`
Expected: FAIL because the current `ent_thread.c` implementation calls `pthread_join` and `pthread_cancel` while `dllLock` is still held.

- [ ] **Step 3: Write minimal implementation**

```c
    UTL_LockEnter(thCtx->dllLock);
    thDb = (THREAD_DB*)*tid;
    if(thDb==NULL || thDb->tag != ENT_TH_TAG)
    {
        IENT_LOG_ERROR("break tid\n");
        UTL_LockLeave(thCtx->dllLock);
        return -2;
    }
    UTL_LockLeave(thCtx->dllLock);

    if(thDb->thId)
    {
        s = pthread_join(thDb->thId,&retVal);
    }

    UTL_LockEnter(thCtx->dllLock);
    sts = UTL_DllRemCurr(&thDb->dllLnk,&tmpHdr);
    UTL_LockLeave(thCtx->dllLock);
```

```c
    while(1)
    {
        UTL_LockEnter(thCtx->dllLock);
        sts = UTL_DllRemHead(&thCtx->dllHeader,&tmp);
        UTL_LockLeave(thCtx->dllLock);
        if(sts != 0)
        {
            break;
        }

        thDb = (THREAD_DB*)tmp;
        if(thDb->thId)
        {
            pthread_cancel(thDb->thId);
            pthread_join(thDb->thId,&retVal);
        }
        free(thDb);
    }
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target test_ent_thread && ./build/bin/test_ent_thread`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add test/test_ent_thread.c comm/ent_thread.c
git commit -m "fix thread lock wait scope"
```

### Task 2: Add Failing Regression Test For `utl_tpool`

**Files:**
- Modify: `test/test_utl_tpool.c`
- Test: `test/test_utl_tpool.c`

- [ ] **Step 1: Write the failing test**

```c
static int s_lock_depth = 0;
static int s_tpool_lock_reentered = 0;

MSG_ID_T UTL_LockEnter(UTL_LOCK lock)
{
    (void)lock;
    if(s_lock_depth > 0)
    {
        s_tpool_lock_reentered = 1;
    }
    s_lock_depth++;
    return 0;
}

MSG_ID_T UTL_LockLeave(UTL_LOCK lock)
{
    (void)lock;
    s_lock_depth--;
    return 0;
}

static int test_tpool_close_does_not_depend_on_worker_holding_task_lock(void)
{
    UTL_TPOOL pool = NULL;

    reset_tpool_lock_probes();

    if(expect_true(UTL_TPoolInit(&pool, 1) == 0,
                   "UTL_TPoolInit should create a pool for close testing") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_TPoolClose(pool) == 0,
                   "UTL_TPoolClose should close a pool cleanly") != 0)
    {
        return 1;
    }

    return expect_true(s_tpool_lock_reentered == 0,
                       "UTL_TPoolClose should not encounter a worker path that spins while holding taskLock");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_utl_tpool && ./build/bin/test_utl_tpool`
Expected: FAIL after tightening the probe so the worker `PAUSE`/wait loop exposes the lock-held spin behavior.

- [ ] **Step 3: Write minimal implementation**

```c
            if(thCtx->taskType==TASK_E_TYPE_PAUSE)
            {
                UTL_LockLeave(poolCtx->taskLock);
                UTL_Sleep(1);
                tmp = NULL;
                continue;
            }
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target test_utl_tpool && ./build/bin/test_utl_tpool`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add test/test_utl_tpool.c comm/utl_tpool.c
git commit -m "fix thread pool lock pause path"
```

### Task 3: Full Verification

**Files:**
- Modify: `comm/ent_thread.c`
- Modify: `comm/utl_tpool.c`
- Test: `test/test_ent_thread.c`
- Test: `test/test_utl_tpool.c`

- [ ] **Step 1: Run focused ctest targets**

Run: `ctest --output-on-failure -R "test_ent_thread|test_utl_tpool" --test-dir build`
Expected: PASS

- [ ] **Step 2: Run full suite**

Run: `ctest --output-on-failure --test-dir build`
Expected: PASS all registered tests

- [ ] **Step 3: Confirm final diff scope**

Run: `git status --short`
Expected: Only the intended files for this change are newly touched in addition to the pre-existing dirty worktree.
