# ENT Log Explicit Context Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Keep the current `ENT_Log*` and `IENT_LOG_*` convenience API, but move the implementation onto an explicit log context so the default handle becomes one instance rather than the only instance.

**Architecture:** Introduce an internal log context owned by the log core, keep the existing `NULL` default-handle path as a compatibility wrapper, and split the current monolithic `ent_log.c` into focused source files for core lifecycle, writer/buffer behavior, option dispatch, and compatibility glue. The first pass must preserve behavior for existing callers while making private log instances truly isolated.

**Tech Stack:** C99, CMake, ctest, pthreads/Windows synchronization primitives, Obsidian markdown for mirrored planning notes

---

### Task 1: Introduce An Explicit Log Context ABI Without Breaking Default-Handle Callers

**Files:**
- Create: `comm/ient_log.h`
- Modify: `inc/ent_log.h`
- Modify: `comm/ent_log.c`
- Modify: `test/test_ent_log.c`

- [ ] **Step 1: Add failing tests for explicit-context creation and isolation**

  In `test/test_ent_log.c`, add a focused regression that references the new context APIs before they exist. The test should create a private context, create a private log handle inside that context, write one message, close the private handle, and verify the default `NULL`-handle path still works afterwards.

  ```c
  static int test_explicit_context_isolated_from_default(void)
  {
      ENT_LOG_CTX ctx = NULL;
      ENT_LOG privateLog = NULL;
      ENT_LOG_LEV_E level = LOG_LEV_INFO_E;

      if(expect_true(ENT_LogCtxInit(&ctx) == 0,
                     "ENT_LogCtxInit should create an explicit context") != 0)
      {
          return 1;
      }

      if(expect_true(ENT_LogCtxInitHandle(ctx, &privateLog, "CtxModule", ".") == 0,
                     "ENT_LogCtxInitHandle should create a private log handle") != 0)
      {
          ENT_LogCtxClose(ctx);
          return 1;
      }

      if(expect_true(ENT_LogCtxSetOption(ctx, privateLog, ENT_LOG_LEVEL_E, &level) == 0,
                     "ENT_LogCtxSetOption should accept per-context options") != 0)
      {
          ENT_LogCtxCloseHandle(ctx, privateLog);
          ENT_LogCtxClose(ctx);
          return 1;
      }

      if(expect_true(ENT_LogCtxCloseHandle(ctx, privateLog) == 0,
                     "ENT_LogCtxCloseHandle should close the private handle") != 0)
      {
          ENT_LogCtxClose(ctx);
          return 1;
      }

      return expect_true(ENT_LogCtxClose(ctx) == 0,
                         "ENT_LogCtxClose should destroy the explicit context");
  }
  ```

- [ ] **Step 2: Run the focused log test and verify it fails at compile/link time**

  Run: `cmake --build build --target test_ent_log`

  Expected: The build fails because the new `ENT_LogCtx*` APIs and `ENT_LOG_CTX` type are not defined yet.

- [ ] **Step 3: Add the explicit context type and API declarations**

  In `inc/ent_log.h`, keep the existing `ENT_LOG` handle and add an opaque context handle:

  ```c
  typedef struct ENT_LOG_CTX_TAG* ENT_LOG_CTX;

  ENT_PUBLIC MSG_ID_T  ENT_LogCtxInit(ENT_LOG_CTX* pCtx);
  ENT_PUBLIC MSG_ID_T  ENT_LogCtxClose(ENT_LOG_CTX ctx);
  ENT_PUBLIC MSG_ID_T  ENT_LogCtxInitHandle(ENT_LOG_CTX ctx, ENT_LOG* pLogHandle, const char* moduleName, const char* logPath);
  ENT_PUBLIC MSG_ID_T  ENT_LogCtxSetOption(ENT_LOG_CTX ctx, ENT_LOG logHandle, ENT_LOG_OPTIONS_E option, const void* arg);
  ENT_PUBLIC MSG_ID_T  ENT_LogCtxCloseHandle(ENT_LOG_CTX ctx, ENT_LOG logHandle);
  ENT_PUBLIC MSG_ID_T  ENT_LogCtxRaw(ENT_LOG_CTX ctx, ENT_LOG logHandle, const char* format, ...);
  ENT_PUBLIC MSG_ID_T  ENT_LogCtxFatal(ENT_LOG_CTX ctx, ENT_LOG logHandle, const char* format, ...);
  ENT_PUBLIC MSG_ID_T  ENT_LogCtxError(ENT_LOG_CTX ctx, ENT_LOG logHandle, const char* format, ...);
  ENT_PUBLIC MSG_ID_T  ENT_LogCtxWarn(ENT_LOG_CTX ctx, ENT_LOG logHandle, const char* format, ...);
  ENT_PUBLIC MSG_ID_T  ENT_LogCtxPrint(ENT_LOG_CTX ctx, ENT_LOG logHandle, const char* format, ...);
  ENT_PUBLIC MSG_ID_T  ENT_LogCtxDebug(ENT_LOG_CTX ctx, ENT_LOG logHandle, const char* format, ...);
  ```

  Create `comm/ient_log.h` as the private header for the real context layout and helper accessors. It should own the internal context structure, the default-context accessor, and the private helper declarations that the split source files will share.

- [ ] **Step 4: Teach the existing `ENT_Log*` entry points to delegate to the default context**

  In `comm/ent_log.c`, keep the old `ENT_LogInit/Close/InitHandle/SetOption/CloseHandle/Raw/Error/Warn/Print/Debug` functions, but route them through the new explicit-context helpers so the default instance becomes just one context rather than a special code path.

- [ ] **Step 5: Rebuild the log test target and confirm the new ABI is wired**

  Run: `cmake --build build --target test_ent_log`

  Expected: `test_ent_log` compiles and links once the new context declarations and wrappers exist.

- [ ] **Step 6: Commit**

  ```bash
  git add comm/ient_log.h inc/ent_log.h comm/ent_log.c test/test_ent_log.c
  git commit -m "refactor: add explicit log context api"
  ```

### Task 2: Split `ent_log.c` Into Core, Writer, Options, And Compatibility Files

**Files:**
- Create: `comm/ent_log_core.c`
- Create: `comm/ent_log_writer.c`
- Create: `comm/ent_log_options.c`
- Create: `comm/ent_log_compat.c`
- Modify: `comm/CMakeLists.txt`
- Modify: `test/CMakeLists.txt`
- Modify: `test/perf_ent_log.c`
- Modify: `test/test_security.c`
- Modify: `test/test_ent_log.c`
- Modify: `comm/ent_log.c`

- [ ] **Step 1: Add build coverage for the new source layout**

  Update `test/CMakeLists.txt` so the log-focused targets compile the split source files instead of `../comm/ent_log.c`.

  ```cmake
  add_executable(
    test_ent_log
    test_ent_log.c
    ../comm/ent_log_core.c
    ../comm/ent_log_writer.c
    ../comm/ent_log_options.c
    ../comm/ent_log_compat.c
    ../comm/utl_thread.c
    ../comm/utl_dll.c
  )
  ```

  Apply the same replacement to `test_security` and `perf_ent_log`.

- [ ] **Step 2: Run the current log-related targets and verify the monolith is still required**

  Run: `cmake --build build --target test_ent_log test_security perf_ent_log`

  Expected: The build fails or produces duplicate-definition errors until the functions are actually moved out of `comm/ent_log.c`.

- [ ] **Step 3: Move the source by responsibility**

  In `comm/ent_log_core.c`, keep the context lifecycle, default-instance routing, handle validation, and the functions that choose between default and private handles.

  In `comm/ent_log_writer.c`, move the message formatting and file/buffer write helpers.

  In `comm/ent_log_options.c`, move `ENT_LogSetOption` and the option-to-state switch logic.

  In `comm/ent_log_compat.c`, keep only the old `ENT_Log*` entry points and the macro-facing compatibility glue.

  Reduce `comm/ent_log.c` to either a tiny compatibility shell or remove it from the build entirely once the split files own every exported symbol.

- [ ] **Step 4: Update the library build to compile the split files**

  In `comm/CMakeLists.txt`, replace the monolithic `ent_log.c` entry with the new split sources in the object library:

  ```cmake
  add_library(
    ${LIB_NAME_OBJS}
    OBJECT
    ent_init.c ent_db.c ent_db_sqlite.c ent_db_mysql.c ent_db_pgsql.c
    ent_log_core.c ent_log_writer.c ent_log_options.c ent_log_compat.c
    ent_thread.c ent_msg.c ent_script.c
    utl_socket.c utl_thread.c utl_timer.c utl_dll.c utl_tpool.c
    ${ENT_MSG_GENERATED_SOURCE}
  )
  ```

- [ ] **Step 5: Rebuild the log and security targets**

  Run:
  - `cmake --build build --target test_ent_log test_security perf_ent_log`
  - `ctest --output-on-failure -R "test_ent_log|test_security"`

  Expected: the split layout links cleanly and the log/security tests still pass.

- [ ] **Step 6: Commit**

  ```bash
  git add comm/ent_log_core.c comm/ent_log_writer.c comm/ent_log_options.c comm/ent_log_compat.c comm/CMakeLists.txt test/CMakeLists.txt test/perf_ent_log.c test/test_security.c test/test_ent_log.c comm/ent_log.c
  git commit -m "refactor: split log implementation"
  ```

### Task 3: Remove Direct `gEntCtx.entLog` Coupling And Validate Compatibility

**Files:**
- Modify: `comm/ient_comm.h`
- Modify: `comm/ent_log_core.c`
- Modify: `comm/ent_log_compat.c`
- Modify: `test/test_ent_log.c`
- Modify: `test/perf_ent_log.c`
- Modify: `test/test_security.c`

- [ ] **Step 1: Add tests that prove the default path and private contexts stay isolated**

  In `test/test_ent_log.c`, extend the existing private-handle coverage so it exercises both the default `NULL`-handle path and a private explicit context in the same process. The test should verify that closing the private context does not change the behavior of `ENT_LogPrint(NULL, ...)` or `ENT_LOG_PRINT(...)`.

  ```c
  static int test_default_and_private_log_contexts(void)
  {
      ENT_LOG_CTX ctx = NULL;
      ENT_LOG privateLog = NULL;

      if(expect_true(ENT_LogCtxInit(&ctx) == 0,
                     "ENT_LogCtxInit should create a private context") != 0)
      {
          return 1;
      }

      if(expect_true(ENT_LogCtxInitHandle(ctx, &privateLog, "PrivateModule", ".") == 0,
                     "ENT_LogCtxInitHandle should create a private handle") != 0)
      {
          ENT_LogCtxClose(ctx);
          return 1;
      }

      if(expect_true(ENT_LogPrint(NULL, "default path still works\n") == 0,
                     "default log path should still work") != 0)
      {
          ENT_LogCtxCloseHandle(ctx, privateLog);
          ENT_LogCtxClose(ctx);
          return 1;
      }

      ENT_LogCtxCloseHandle(ctx, privateLog);
      ENT_LogCtxClose(ctx);
      return expect_true(ENT_LogPrint(NULL, "default path survives private teardown\n") == 0,
                         "default log path should survive private context teardown");
  }
  ```

- [ ] **Step 2: Run the focused targets and confirm any remaining `gEntCtx.entLog` use is visible**

  Run: `cmake --build build --target test_ent_log test_security perf_ent_log`

  Expected: if any code path still reaches into `gEntCtx.entLog` directly, the split modules will be harder to compile or the new tests will fail.

- [ ] **Step 3: Replace direct macro coupling with a default-context accessor**

  In `comm/ient_comm.h`, change the `IENT_LOG_*` macros so they no longer dereference `gEntCtx.entLog` directly. Instead, make them route through a small accessor implemented in `comm/ent_log_core.c`, so the macros keep their old call shape but the dependency becomes explicit inside the log module.

  Keep the existing macro names and formatting behavior unchanged for callers.

- [ ] **Step 4: Remove any remaining direct default-handle reach-ins**

  Search for `gEntCtx.entLog` across `comm/` and `test/`, and either remove the reference or replace it with the new accessor. The only remaining ownership of the default instance should live in the log core layer.

- [ ] **Step 5: Re-run the focused verification set**

  Run:
  - `cmake --build build --target test_ent_log test_security perf_ent_log`
  - `ctest --output-on-failure -R "test_ent_log|test_security"`

  Expected: default and explicit log contexts both work, and the compatibility macros keep the old call sites working.

- [ ] **Step 6: Commit**

  ```bash
  git add comm/ient_comm.h comm/ent_log_core.c comm/ent_log_compat.c test/test_ent_log.c test/perf_ent_log.c test/test_security.c
  git commit -m "refactor: route log macros through explicit context"
  ```

### Task 4: Remove Obsolete Monolith And Finalize The Migration

**Files:**
- Delete: `comm/ent_log.c` if it is no longer needed after the split
- Modify: `comm/CMakeLists.txt`
- Modify: `test/CMakeLists.txt`
- Modify: `docs/superpowers/specs/2026-04-18-ent-log-explicit-context-design.md` only if the implemented API names differ from the design

- [ ] **Step 1: Confirm the old file is no longer needed**

  After the split and compatibility work is complete, inspect `comm/ent_log.c`. If it only duplicates code that now lives in the split files, delete it from the build and remove the file.

- [ ] **Step 2: Run the full log-focused verification set**

  Run:
  - `cmake --build build --target test_ent_log test_security perf_ent_log`
  - `ctest --output-on-failure -R "test_ent_log|test_security"`

  Expected: all log-facing tests pass with the new explicit-context implementation and the compatibility layer intact.

- [ ] **Step 3: Commit**

  ```bash
  git add comm/CMakeLists.txt test/CMakeLists.txt comm/ent_log.c docs/superpowers/specs/2026-04-18-ent-log-explicit-context-design.md
  git commit -m "refactor: finalize explicit log context migration"
  ```

