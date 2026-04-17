# Runtime Refactor Roadmap Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reduce structural risk in `ent` by hardening the DB lifecycle, splitting oversized DB internals, isolating runtime/log coupling, and making the script engine safe under concurrent use.

**Architecture:** Keep the public API stable in the first pass. Refactor in four small commits: first remove correctness bugs, then split `ent_db.c` into backend-focused units, then narrow global runtime/log dependencies, and finally make the script subsystem safe for multi-threaded callers. Each task must leave the tree buildable and `ctest`-verifiable.

**Tech Stack:** C99, CMake, ctest, SQLite/MySQL/PostgreSQL backends, pthreads/Windows threading, Obsidian markdown for mirrored planning notes

---

### Task 1: Harden Database Handle Validation And Open Lifecycle

**Files:**
- Modify: `comm/ent_db.c`
- Modify: `test/test_ent_db.c`
- Modify: `test/test_security.c`

- [ ] **Step 1: Add failing tests for NULL-handle and concurrent-open safety**

  In `test/test_ent_db.c`, add a test that calls `ENT_DbOpen(NULL)`, `ENT_DbRead(NULL, "select 1", ...)`, and `ENT_DbWrite(NULL, "select 1", ...)` and asserts they return an error instead of crashing.

  ```c
  static int test_null_handle_rejection(void)
  {
      if(expect_true(ENT_DbOpen(NULL) < 0,
                     "ENT_DbOpen should reject NULL handles") != 0)
      {
          return -1;
      }

      if(expect_true(ENT_DbRead(NULL, "select 1", NULL, NULL) < 0,
                     "ENT_DbRead should reject NULL handles") != 0)
      {
          return -1;
      }

      return expect_true(ENT_DbWrite(NULL, "select 1", NULL, NULL) < 0,
                         "ENT_DbWrite should reject NULL handles");
  }
  ```

  In `test/test_security.c`, add a regression that exercises the same API family with `NULL` input to ensure the security suite covers this path.

- [ ] **Step 2: Run the focused tests and verify the current code fails**

  Run: `ctest --output-on-failure -R "test_ent_db|test_security"`

  Expected: One or more tests fail or crash due to `DB_CFG* dbCfg=(DB_CFG*)dbHandle;` being dereferenced before `NULL` validation.

- [ ] **Step 3: Introduce one internal validation helper and reuse it**

  In `comm/ent_db.c`, add an internal helper near the top of the file:

  ```c
  static MSG_ID_T iENT_DbValidateHandle(DB_HANDLE dbHandle,
                                        DB_CFG** outCfg,
                                        bool requireOpen,
                                        bool requireInit)
  {
      DB_CFG* dbCfg = (DB_CFG*)dbHandle;

      if(outCfg != NULL)
      {
          *outCfg = NULL;
      }

      if(dbHandle == NULL)
      {
          IENT_LOG_ERROR("Database handle is null.\n");
          return -1;
      }

      if(dbCfg->sTag != ENTDB_S_TAG || dbCfg->eTag != ENTDB_E_TAG)
      {
          IENT_LOG_ERROR("Database handle is invalid.\n");
          return -1;
      }

      if(requireInit && dbCfg->isInit == false)
      {
          IENT_LOG_ERROR("Database handle is not initialized.\n");
          return -1;
      }

      if(requireOpen && dbCfg->isOpen == false)
      {
          IENT_LOG_ERROR("Database handle is not open.\n");
          return -1;
      }

      if(outCfg != NULL)
      {
          *outCfg = dbCfg;
      }
      return 0;
  }
  ```

  Then use it in `ENT_DbOpen`, `ENT_DbCloseHandle`, `ENT_DbRead`, and `ENT_DbWrite` before touching `dbCfg`.

- [ ] **Step 4: Close the open race with a lock-protected double check**

  In `ENT_DbOpen`, move the `isOpen` decision under `dbCfg->cs` and re-check it after the lock is acquired.

  ```c
  sts = iENT_DbValidateHandle(dbHandle, &dbCfg, false, true);
  if(sts < 0)
  {
      return sts;
  }

  pthread_mutex_lock(&dbCfg->cs);
  if(dbCfg->isOpen)
  {
      pthread_mutex_unlock(&dbCfg->cs);
      return 0;
  }

  switch(dbCfg->dbType)
  {
      /* existing backend dispatch */
  }
  pthread_mutex_unlock(&dbCfg->cs);
  ```

- [ ] **Step 5: Re-run tests and confirm the hardening pass is green**

  Run: `ctest --output-on-failure -R "test_ent_db|test_security"`

  Expected: Both tests pass without crashes.

- [ ] **Step 6: Commit**

  ```bash
  git add comm/ent_db.c test/test_ent_db.c test/test_security.c
  git commit -m "fix: harden db handle validation and open lifecycle"
  ```

### Task 2: Split Database Core From Backend Implementations

**Files:**
- Create: `comm/ient_db.h`
- Create: `comm/ent_db_core.c`
- Create: `comm/ent_db_sqlite.c`
- Create: `comm/ent_db_mysql.c`
- Create: `comm/ent_db_pgsql.c`
- Modify: `comm/CMakeLists.txt`
- Modify: `comm/ent_db.c`

- [ ] **Step 1: Create the internal DB header with shared struct and backend prototypes**

  Move `DB_CFG`, backend feature macros, and backend function declarations into `comm/ient_db.h`.

  ```c
  typedef struct DB_CFG
  {
      unsigned int sTag;
      DB_TYPE dbType;
      char* userName;
      char* passwd;
      char* host;
      char* database;
      int portNo;
      bool isInit;
  #ifdef WIN32
      CRITICAL_SECTION cs;
  #else
      pthread_mutex_t cs;
  #endif
      bool isOpen;
      union
      {
  #if ENT_ENABLE_SQLITE
          sqlite3* sqlite;
  #endif
  #if ENT_ENABLE_MYSQL
          MYSQL* mysql;
  #endif
  #if ENT_ENABLE_PGSQL
          PGconn* pgsql;
  #endif
          void* raw;
      } dbInstance;
      unsigned int eTag;
  } DB_CFG;
  ```

- [ ] **Step 2: Move lifecycle routing code into `ent_db_core.c`**

  Put `ENT_DbInit`, `ENT_DbClose`, `ENT_DbInitHandle`, `ENT_DbOpen`, `ENT_DbCloseHandle`, `ENT_DbRead`, `ENT_DbWrite`, and validation helpers into `comm/ent_db_core.c`.

  Leave a tiny compatibility shell in `comm/ent_db.c` only if needed by build layout; otherwise convert `comm/ent_db.c` into a stub include boundary or remove its implementation body from compilation.

- [ ] **Step 3: Move each backend implementation into a focused file**

  Backend split:
  - `comm/ent_db_sqlite.c`: `ENT_DbSqliteInit`, `ENT_DbSqliteClose`, `ENT_DbSqliteRead`, `ENT_DbSqliteWrite`
  - `comm/ent_db_mysql.c`: MySQL counterparts
  - `comm/ent_db_pgsql.c`: PostgreSQL counterparts

  Keep helper functions backend-local with `static` linkage.

- [ ] **Step 4: Update `comm/CMakeLists.txt` to compile the new sources**

  Add the new files to the library target and remove duplicate compilation of old monolithic definitions.

  ```cmake
  target_sources(ent_objs PRIVATE
      ent_db_core.c
      ent_db_sqlite.c
      ent_db_mysql.c
      ent_db_pgsql.c
  )
  ```

- [ ] **Step 5: Build and run DB-focused tests**

  Run:
  - `cmake --build build`
  - `ctest --output-on-failure -R "test_ent_db|test_security"`

  Expected: Build succeeds and DB/security suites still pass after the file split.

- [ ] **Step 6: Commit**

  ```bash
  git add comm/ient_db.h comm/ent_db_core.c comm/ent_db_sqlite.c comm/ent_db_mysql.c comm/ent_db_pgsql.c comm/CMakeLists.txt comm/ent_db.c
  git commit -m "refactor: split db core from backend implementations"
  ```

### Task 3: Isolate Runtime And Logging Dependencies

**Files:**
- Modify: `comm/ient_comm.h`
- Modify: `comm/ent_init.c`
- Modify: `comm/ent_log.c`
- Modify: `test/test_ent_init.c`
- Modify: `test/test_ent_log.c`

- [ ] **Step 1: Add a regression for default-log cleanup symmetry**

  In `test/test_ent_log.c`, add a test that repeatedly runs `ENT_LogInit() -> ENT_LogInitHandle(NULL, ...) -> ENT_LogCloseHandle(NULL) -> ENT_LogClose()` and asserts the sequence succeeds twice in a row.

  ```c
  static int test_default_handle_reopen_cycle(void)
  {
      int i;
      for(i = 0; i < 2; ++i)
      {
          if(ENT_LogInit() != 0) return -1;
          if(ENT_LogInitHandle(NULL, "CycleModule", ".") < 0) return -1;
          if(ENT_LogCloseHandle(NULL) != 0) return -1;
          if(ENT_LogClose() != 0) return -1;
      }
      return 0;
  }
  ```

- [ ] **Step 2: Run init/log tests before refactoring**

  Run: `ctest --output-on-failure -R "test_ent_init|test_ent_log"`

  Expected: Existing behavior is captured before touching runtime/log coupling.

- [ ] **Step 3: Narrow `ient_comm.h` to internal state definitions and logging adapters**

  Remove mixed concerns from `comm/ient_comm.h`:
  - keep `ENT_CTX`
  - keep `extern ENT_CTX gEntCtx`
  - move `UTL_Malloc` to a more appropriate utility source if it is still needed
  - replace direct `gEntCtx.entLog` macro coupling with a helper that safely resolves a log handle

  Example internal helper:

  ```c
  static inline ENT_LOG iENT_RuntimeLogHandle(void)
  {
      return gEntCtx.entLog;
  }
  ```

- [ ] **Step 4: Make default-log cleanup symmetric in `ent_log.c`**

  In `ENT_LogCloseHandle`, free `moduleName` and `logPath` for `sDefLog` too, then clear the fields.

  ```c
  if(log->moduleName != NULL)
  {
      free(log->moduleName);
      log->moduleName = NULL;
  }
  if(log->logPath != NULL)
  {
      free(log->logPath);
      log->logPath = NULL;
  }
  if(log != &sDefLog)
  {
      free(log);
  }
  ```

- [ ] **Step 5: Keep `ent_init.c` as orchestration only**

  In `comm/ent_init.c`, consolidate cleanup into one internal teardown helper so `ENT_Init` failure paths stop duplicating resource-release order.

  ```c
  static void iENT_CTXCleanupAfterInitFailure(ENT_CTX* ctx,
                                              bool closeEntLog,
                                              bool closeDefaultLog,
                                              bool closeLock,
                                              bool closeCv)
  {
      if(closeCv && ctx->entCV != NULL) { UTL_CVClose(ctx->entCV); ctx->entCV = NULL; }
      if(closeLock && ctx->entLock != NULL) { UTL_LockClose(ctx->entLock); ctx->entLock = NULL; }
      iENT_CTXCloseLog(ctx, closeEntLog, closeDefaultLog);
      iENT_CTXFree(ctx);
      iENT_CTXResetRuntime(ctx);
  }
  ```

- [ ] **Step 6: Re-run the focused suites**

  Run:
  - `ctest --output-on-failure -R "test_ent_init|test_ent_log"`
  - `ctest --output-on-failure`

  Expected: init/log tests and full suite pass.

- [ ] **Step 7: Commit**

  ```bash
  git add comm/ient_comm.h comm/ent_init.c comm/ent_log.c test/test_ent_init.c test/test_ent_log.c
  git commit -m "refactor: isolate runtime and logging dependencies"
  ```

### Task 4: Make The Script Engine Safe For Concurrent Use

**Files:**
- Modify: `comm/ent_script.c`
- Modify: `test/test_ent_script.c`

- [ ] **Step 1: Add a concurrent script-call regression**

  In `test/test_ent_script.c`, add a small worker that repeatedly calls `ENT_ScriptCall` from two threads after a single `ENT_ScriptInit`.

  ```c
  static void* test_script_worker(void* data)
  {
      ENT_SCRIPT_RET_T out;
      (void)data;
      memset(&out, 0, sizeof(out));
      return (void*)(long)ENT_ScriptCall("health", NULL, &out);
  }
  ```

  The assertion target is simple: no crash, no corrupted state, and a deterministic status code.

- [ ] **Step 2: Run the script tests to establish baseline**

  Run: `ctest --output-on-failure -R test_ent_script`

  Expected: Current single-thread tests pass; the new concurrent test may fail or expose flaky behavior.

- [ ] **Step 3: Add internal locking around the global script context**

  Extend `ENT_SCRIPT_CTX_T` with a lock and guard all stateful public entry points.

  ```c
  typedef struct
  {
      bool isInit;
      char scriptRoot[ENT_SCRIPT_PATH_MAX];
  #ifdef WIN32
      CRITICAL_SECTION lock;
  #else
      pthread_mutex_t lock;
  #endif
  #if ENT_ENABLE_LUA
      lua_State* state;
  #endif
  } ENT_SCRIPT_CTX_T;
  ```

  Then lock/unlock inside `ENT_ScriptReload`, `ENT_ScriptCall`, and `ENT_ScriptClose`.

- [ ] **Step 4: Keep API compatibility, but document the next step**

  Do not add a new public handle API in this pass. Add a short internal comment noting that the next compatible evolution is an instance-based script engine if multi-instance use becomes a requirement.

- [ ] **Step 5: Re-run script tests**

  Run: `ctest --output-on-failure -R test_ent_script`

  Expected: script tests pass consistently with the concurrency regression included.

- [ ] **Step 6: Commit**

  ```bash
  git add comm/ent_script.c test/test_ent_script.c
  git commit -m "refactor: make script engine concurrency-safe"
  ```

### Task 5: Final Verification And Documentation Sync

**Files:**
- Modify: `README.md`
- Modify: `AGENTS.md`

- [ ] **Step 1: Update documentation only if behavior changed**

  If any public-facing runtime expectation changed during Tasks 1-4, add a short note to `README.md`. If no user-visible behavior changed, leave `README.md` untouched.

  Update `AGENTS.md` only if message-code or contributor workflow rules changed. Do not edit it for internal-only refactors.

- [ ] **Step 2: Run full verification**

  Run:
  - `cmake --build build`
  - `ctest --output-on-failure`

  Expected: full build and full test suite succeed.

- [ ] **Step 3: Commit the final sync if docs changed**

  ```bash
  git add README.md AGENTS.md
  git commit -m "docs: sync runtime refactor notes"
  ```

  If no docs changed, skip this commit.
