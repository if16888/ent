# ENT_Init Log Failure Order Fix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ensure `ENT_Init()` logs initialization failures before tearing down the logging subsystem so internal error output remains available.

**Architecture:** Keep the fix local to `comm/ent_init.c`. Add a focused regression test in `test/test_ent_init.c` that exercises an `ENT_Init()` failure after log initialization succeeds, then reorder cleanup and logging in the failing branch so the test passes without changing global logging semantics.

**Tech Stack:** C99, CMake, ctest

---

### Task 1: Add Regression Test For Post-Init Failure Logging

**Files:**
- Modify: `test/test_ent_init.c`
- Test: `test/test_ent_init.c`

- [ ] **Step 1: Write the failing test**

```c
static int s_fail_lock_init = 0;
static int s_log_error_calls = 0;
static ENT_LOG s_last_log_error_handle = NULL;

MSG_ID_T ENT_LogError(ENT_LOG logHandle, const char* format, ...)
{
    (void)format;
    s_log_error_calls++;
    s_last_log_error_handle = logHandle;
    return 0;
}

MSG_ID_T UTL_LockInit(UTL_LOCK* lock, const char* name)
{
    (void)name;
    if(s_fail_lock_init != 0)
    {
        return s_fail_lock_init;
    }
    if(lock != NULL)
    {
        *lock = (UTL_LOCK)0x20;
    }
    return 0;
}

static int test_ent_init_logs_before_tearing_down_logging_when_lock_init_fails(void)
{
    memset(&gEntCtx, 0, sizeof(gEntCtx));
    reset_close_counters();
    reset_log_failures();
    s_fail_lock_init = -9;
    s_log_error_calls = 0;
    s_last_log_error_handle = NULL;

    if(expect_true(ENT_Init("demo", "/tmp/demo", LOG_LEV_WARN_E) == -9,
                   "ENT_Init should surface lock-init failures") != 0)
    {
        return 1;
    }

    if(expect_true(s_log_error_calls == 1,
                   "ENT_Init should emit one internal error log before cleanup") != 0)
    {
        return 1;
    }

    if(expect_true(s_last_log_error_handle != NULL,
                   "ENT_Init should log through the entity log handle while it is still valid") != 0)
    {
        return 1;
    }

    return expect_true(s_log_close_calls == 1,
                       "ENT_Init should still close the logging subsystem after logging the failure");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_ent_init && ./build/bin/test_ent_init`
Expected: FAIL because `s_log_error_calls` stays `0` after `ENT_Init()` clears logging before `IENT_LOG_ERROR(...)`.

- [ ] **Step 3: Write minimal implementation**

```c
    sts = UTL_LockInit(&gEntCtx.entLock,"ent");
    if(sts<0)
    {
        IENT_LOG_ERROR("UTL_LockInit failed,sts [%d]\n",sts);
        iENT_CTXFree(&gEntCtx);
        iENT_CTXCloseLog(&gEntCtx,true,true);
        iENT_CTXResetRuntime(&gEntCtx);
        return -9;
    }
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target test_ent_init && ./build/bin/test_ent_init`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add test/test_ent_init.c comm/ent_init.c docs/superpowers/plans/2026-04-02-ent-init-log-order-fix.md
git commit -m "fix ent_init failure log order"
```

### Task 2: Run Focused Regression Suite

**Files:**
- Modify: `comm/ent_init.c`
- Test: `test/test_ent_init.c`

- [ ] **Step 1: Run the focused ctest target**

Run: `ctest --output-on-failure -R test_ent_init --test-dir build`
Expected: PASS

- [ ] **Step 2: Run the full suite for regression coverage**

Run: `ctest --output-on-failure --test-dir build`
Expected: PASS all registered tests

- [ ] **Step 3: Review dirty worktree impact**

Run: `git status --short`
Expected: Only the intended files for this fix are additionally modified.
