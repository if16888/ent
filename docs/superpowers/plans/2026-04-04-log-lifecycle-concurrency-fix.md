# Log Lifecycle Concurrency Fix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 消除 `ent_log` 在并发写日志和关闭句柄时的悬空指针与销毁竞争，保留现有公开 API。

**Architecture:** 在 `ENT_LOG_CTX` 内增加“关闭中”状态、活跃 writer 计数和关闭等待原语，让写路径先获取一次安全引用，再进入 `log->cs`，关闭路径先拒绝新 writer，再等待活跃 writer 退出后销毁资源。实现保持同步写日志模型，不在本计划内引入异步日志线程。

**Tech Stack:** C, pthread / Win32 synchronization, existing ent_log APIs, existing unit test framework

---

### Task 1: Add a Failing Concurrency Regression Test

**Files:**
- Modify: `test/test_ent_log.c`
- Test: `test/test_ent_log.c`

- [ ] **Step 1: Write the failing test scaffolding**

Add a Linux/Apple-friendly threaded regression test to `test/test_ent_log.c` that creates one writer thread looping on `ENT_LogPrint`, while the main thread closes the same handle:

```c
typedef struct
{
    ENT_LOG logHandle;
    volatile int stop;
    volatile int writes;
} TEST_LOG_RACE_CTX;

static void* noisy_log_writer(void* data)
{
    TEST_LOG_RACE_CTX* ctx = (TEST_LOG_RACE_CTX*)data;

    while(!ctx->stop)
    {
        ENT_LogPrint(ctx->logHandle, "race write %d\n", ctx->writes);
        ctx->writes++;
        UTL_Sleep(1);
    }

    return NULL;
}
```

- [ ] **Step 2: Add the failing regression case**

Append this test to `test/test_ent_log.c`:

```c
static int test_log_close_handle_waits_for_active_writers(void)
{
    ENT_LOG logHandle = NULL;
    ENT_THREAD threadHandle = NULL;
    ENT_THREAD_ID writerTid = NULL;
    TEST_LOG_RACE_CTX ctx;

    memset(&ctx, 0, sizeof(ctx));

    if(expect_true(ENT_LogInit() == 0, "ENT_LogInit should initialize before close/write race test") != 0)
    {
        return 1;
    }
    if(expect_true(ENT_LogInitHandle(&logHandle, "RaceModule", ".") == 0,
                   "ENT_LogInitHandle should create a private handle for race test") != 0)
    {
        ENT_LogClose();
        return 1;
    }
    if(expect_true(ENT_ThreadInit(&threadHandle) == 0, "ENT_ThreadInit should create a thread context for race test") != 0)
    {
        ENT_LogCloseHandle(logHandle);
        ENT_LogClose();
        return 1;
    }

    ctx.logHandle = logHandle;
    if(expect_true(ENT_ThreadCreate(&writerTid, threadHandle, noisy_log_writer, &ctx) == 0,
                   "ENT_ThreadCreate should start a writer thread") != 0)
    {
        ENT_ThreadClose(threadHandle);
        ENT_LogCloseHandle(logHandle);
        ENT_LogClose();
        return 1;
    }

    UTL_Sleep(20);
    ctx.stop = 1;

    if(expect_true(ENT_LogCloseHandle(logHandle) == 0,
                   "ENT_LogCloseHandle should succeed even while a writer is winding down") != 0)
    {
        ENT_ThreadClose(threadHandle);
        ENT_LogClose();
        return 1;
    }

    if(expect_true(ENT_ThreadWaitById(&writerTid, threadHandle, 0) == 0,
                   "ENT_ThreadWaitById should join the writer thread after close") != 0)
    {
        ENT_ThreadClose(threadHandle);
        ENT_LogClose();
        return 1;
    }

    if(expect_true(ENT_ThreadClose(threadHandle) == 0, "ENT_ThreadClose should release the race-test thread context") != 0)
    {
        ENT_LogClose();
        return 1;
    }

    return expect_true(ENT_LogClose() == 0, "ENT_LogClose should succeed after close/write race test");
}
```

- [ ] **Step 3: Register the new test in `main`**

Add:

```c
failures += test_log_close_handle_waits_for_active_writers();
```

- [ ] **Step 4: Run the focused test to verify red**

Run:

```bash
cmake --build /Users/lifei/test/ent/build --target test_ent_log
/Users/lifei/test/ent/build/bin/test_ent_log
```

Expected: the test fails or crashes before the fix because `ENT_LogCloseHandle` can destroy/free a handle that a writer thread is still using.

- [ ] **Step 5: Commit**

```bash
git -C /Users/lifei/test/ent add test/test_ent_log.c
git -C /Users/lifei/test/ent commit -m "test: add log close writer race coverage"
```

### Task 2: Add Log Context Lifecycle State

**Files:**
- Modify: `comm/ent_log.c`

- [ ] **Step 1: Extend `ENT_LOG_CTX` with lifecycle fields**

Add these fields to `ENT_LOG_CTX` in `comm/ent_log.c`:

```c
    bool             closing;
    int              activeWriters;
#ifdef WIN32
    CONDITION_VARIABLE closeCv;
#else
    pthread_cond_t   closeCv;
#endif
```

- [ ] **Step 2: Initialize the new fields when opening a handle**

Inside `ENT_LogInitHandle(...)`, after zeroing the context and before `isInit = true`, initialize:

```c
    log->closing = false;
    log->activeWriters = 0;
#ifdef WIN32
    InitializeConditionVariable(&log->closeCv);
#else
    pthread_cond_init(&log->closeCv, NULL);
#endif
```

- [ ] **Step 3: Build to verify additive changes compile**

Run:

```bash
cmake --build /Users/lifei/test/ent/build --target test_ent_log
```

Expected: build succeeds, runtime behavior is unchanged.

- [ ] **Step 4: Commit**

```bash
git -C /Users/lifei/test/ent add comm/ent_log.c
git -C /Users/lifei/test/ent commit -m "refactor: add log lifecycle state"
```

### Task 3: Guard Writer Entry and Exit

**Files:**
- Modify: `comm/ent_log.c`

- [ ] **Step 1: Add internal writer acquire/release helpers**

Add these helpers near `iENT_LogGetCtx(...)`:

```c
static MSG_ID_T iENT_LogAcquireWriter(ENT_LOG_CTX** logCtx, ENT_LOG logHandle);
static void iENT_LogReleaseWriter(ENT_LOG_CTX* logCtx);
```

Implement them so they:

```c
static MSG_ID_T iENT_LogAcquireWriter(ENT_LOG_CTX** logCtx, ENT_LOG logHandle)
{
    MSG_ID_T sts = iENT_LogGetCtx(logCtx, logHandle);
    ENT_LOG_CTX* log = NULL;

    if(sts < 0)
    {
        return sts;
    }

    log = *logCtx;
#ifdef WIN32
    EnterCriticalSection(&sLogMutex);
#else
    pthread_mutex_lock(&sLogMutex);
#endif
    if(log->closing || !log->isInit)
    {
#ifdef WIN32
        LeaveCriticalSection(&sLogMutex);
#else
        pthread_mutex_unlock(&sLogMutex);
#endif
        *logCtx = NULL;
        return -3;
    }
    log->activeWriters++;
#ifdef WIN32
    LeaveCriticalSection(&sLogMutex);
#else
    pthread_mutex_unlock(&sLogMutex);
#endif
    return 0;
}

static void iENT_LogReleaseWriter(ENT_LOG_CTX* log)
{
#ifdef WIN32
    EnterCriticalSection(&sLogMutex);
#else
    pthread_mutex_lock(&sLogMutex);
#endif
    log->activeWriters--;
    if(log->closing && log->activeWriters == 0)
    {
#ifdef WIN32
        WakeAllConditionVariable(&log->closeCv);
#else
        pthread_cond_broadcast(&log->closeCv);
#endif
    }
#ifdef WIN32
    LeaveCriticalSection(&sLogMutex);
#else
    pthread_mutex_unlock(&sLogMutex);
#endif
}
```

- [ ] **Step 2: Switch write entry points to the new acquire path**

Update:
- `ENT_LogRaw(...)`
- `ENT_LogFatal(...)`
- `ENT_LogError(...)`
- `ENT_LogWarn(...)`
- `ENT_LogPrint(...)`
- `ENT_LogDebug(...)`

Replace `iENT_LogGetCtx(...)` with `iENT_LogAcquireWriter(...)`, and after each `iENT_LogVRaw` / `iENT_LogVPrint` call, add `iENT_LogReleaseWriter(logCtx);`.

- [ ] **Step 3: Run the focused test to verify it still fails in close path, not write path**

Run:

```bash
/Users/lifei/test/ent/build/bin/test_ent_log
```

Expected: the race may still fail because close can still free the handle too early, but writers should no longer enter after `closing` becomes true.

- [ ] **Step 4: Commit**

```bash
git -C /Users/lifei/test/ent add comm/ent_log.c
git -C /Users/lifei/test/ent commit -m "refactor: guard log writer entry"
```

### Task 4: Make Close Wait for Active Writers

**Files:**
- Modify: `comm/ent_log.c`

- [ ] **Step 1: Change `ENT_LogCloseHandle` to mark closing before teardown**

In `ENT_LogCloseHandle(...)`, after validating `log`, add:

```c
    log->closing = true;
    while(log->activeWriters > 0)
    {
#ifdef WIN32
        SleepConditionVariableCS(&log->closeCv, &sLogMutex, INFINITE);
#else
        pthread_cond_wait(&log->closeCv, &sLogMutex);
#endif
    }
```

This must happen while `sLogMutex` is still held, before `fclose`, `pthread_mutex_destroy`, and `free(log)`.

- [ ] **Step 2: Keep resource destruction after the wait**

Retain the existing teardown order, but only after active writers reach zero:

```c
    if(log->logFp)
    {
        fclose(log->logFp);
    }
    log->isInit = false;
    log->logFp = NULL;
```

- [ ] **Step 3: Destroy `closeCv` for non-default handles during close**

Before `free(log)` on non-default handles, add:

```c
#ifndef WIN32
    pthread_cond_destroy(&log->closeCv);
#endif
```

Do not destroy the Win32 condition variable because it requires no explicit destruction.

- [ ] **Step 4: Re-run the focused race test to verify green**

Run:

```bash
/Users/lifei/test/ent/build/bin/test_ent_log
```

Expected: the new close/write race test passes consistently.

- [ ] **Step 5: Commit**

```bash
git -C /Users/lifei/test/ent add comm/ent_log.c
git -C /Users/lifei/test/ent commit -m "fix: wait for active log writers on close"
```

### Task 5: Final Verification

**Files:**
- Verify: `comm/ent_log.c`
- Verify: `test/test_ent_log.c`

- [ ] **Step 1: Rebuild the relevant targets**

Run:

```bash
cmake -S /Users/lifei/test/ent -B /Users/lifei/test/ent/build
cmake --build /Users/lifei/test/ent/build --target test_ent_log
```

Expected: build succeeds.

- [ ] **Step 2: Run the focused log tests**

Run:

```bash
/Users/lifei/test/ent/build/bin/test_ent_log
```

Expected: all log tests pass, including the new concurrency regression.

- [ ] **Step 3: Run the full registered test suite**

Run:

```bash
ctest --output-on-failure
```

Expected: existing registered tests still pass.

- [ ] **Step 4: Commit the verification-ready state**

```bash
git -C /Users/lifei/test/ent add comm/ent_log.c test/test_ent_log.c
git -C /Users/lifei/test/ent commit -m "fix: harden log handle lifecycle"
```
