# PostgreSQL Database Support Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Integrate PostgreSQL officially as a supported database backend using `libpq` in `ent_db.c`.

**Architecture:** Use the existing macro inclusion branching (`#if ENT_ENABLE_PGSQL`) within `ent_db.c` to wrap `<libpq-fe.h>`, structure definitions, and specific DB implementations (`ENT_DbPgSQLInit`, `ENT_DbPgSQLClose`, `ENT_DbPgSQLRead`, `ENT_DbPgSQLWrite`). Then bind these inside the public routing functions.

**Tech Stack:** C99, CMake, PostgreSQL `libpq`

---

### Task 1: CMake PostgreSQL Integration

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `test/CMakeLists.txt`

- [ ] **Step 1: Add PostgreSQL finding and linking to root CMakeLists.txt**
  Add dependency checks at the root `CMakeLists.txt` before defining modules.

  ```cmake
  # Find it in root CMakeLists.txt near other library configs
  find_package(PostgreSQL)
  if(PostgreSQL_FOUND)
      add_compile_definitions(ENT_ENABLE_PGSQL=1)
      include_directories(${PostgreSQL_INCLUDE_DIRS})
  else()
      add_compile_definitions(ENT_ENABLE_PGSQL=0)
  endif()
  ```

- [ ] **Step 2: Add PostgreSQL linking to test targets**
  In `test/CMakeLists.txt`, within the `if(ENT_ENABLE_SQLITE AND ...)` block or where `test_ent_db` is configured, add the link library for pg:

  ```cmake
  if(PostgreSQL_FOUND)
      target_link_libraries(test_ent_db PRIVATE ${PostgreSQL_LIBRARIES})
      target_link_libraries(test_security PRIVATE ${PostgreSQL_LIBRARIES})
  endif()
  ```

- [ ] **Step 3: Run CMake to verify it finds (or gracefully skips) PG**
  Run: `cd build && cmake ..`
  Expected: Successful cmake configuration whether PG is found or not.

- [ ] **Step 4: Commit**
  ```bash
  git add CMakeLists.txt test/CMakeLists.txt
  git commit -m "build: add postgresql cmake integration"
  ```

### Task 2: Core Header and Struct Configurations

**Files:**
- Modify: `inc/ent_db.h`
- Modify: `comm/ent_db.c`
- Modify: `test/test_ent_db.c`

- [ ] **Step 1: Write a failing test in `test_ent_db.c`**
  ```c
  // in test_ent_db.c add standard test void test_pgsql_init() { ... }
  void test_pgsql_init()
  {
      DB_HANDLE dbHandle = NULL;
      // PGSQL_TYPE should be 5
      MSG_ID_T sts = ENT_DbInitHandle(&dbHandle, 5, "127.0.0.1", "test", "root", "123456", 5432);
      if(sts == 0 && dbHandle != NULL) {
          printf("PgSQL init struct success\n");
          ENT_DbCloseHandle(dbHandle);
      }
  }
  ```
  Add a call to `test_pgsql_init();` in the `main()` function of `test_ent_db.c`.

- [ ] **Step 2: Run the test to verify compilation fails**
  Run: `cd build && make test_ent_db`
  Expected: It should compile since `5` is passed directly, but the run fails or complains about unsupported DB because `PGSQL_TYPE = 5` and its switch case aren't implemented in `ent_db.c`.

- [ ] **Step 3: Write minimal implementation in `ent_db.h` and `ent_db.c`**
  ```c
  // inc/ent_db.h
  // Inside DB_TYPE enum append:
    PGSQL_TYPE  = 5

  // comm/ent_db.c
  #ifndef ENT_ENABLE_PGSQL
  #define ENT_ENABLE_PGSQL 0
  #endif

  #if ENT_ENABLE_PGSQL
  #include <libpq-fe.h>
  #endif

  // Inside DB_CFG union dbInstance:
  #if ENT_ENABLE_PGSQL
        PGconn*   pgsql;
  #endif

  // Add the basic stub logic to ENT_DbInitHandle and ENT_DbCloseHandle:
  // Inside ENT_DbInitHandle switch(dbType):
  case PGSQL_TYPE:
      dbCfg = (DB_CFG*)malloc(sizeof(DB_CFG));
      memset(dbCfg,0,sizeof(DB_CFG));
      dbCfg->dbType = dbType;
      if(host) dbCfg->host = strdup(host);
      if(database) dbCfg->database = strdup(database);
      if(user) dbCfg->userName = strdup(user);
      if(passwd) dbCfg->passwd = strdup(passwd);
      dbCfg->portNo = port;
      break;

  // Inside ENT_DbCloseHandle switch:
  case PGSQL_TYPE:
  #if ENT_ENABLE_PGSQL
      // Do nothing stub for now
  #else
      sts = iENT_DbBackendUnsupported(PGSQL_TYPE);
  #endif
      break;
  ```

- [ ] **Step 4: Run test to verify it passes structural checks**
  Run: `cd build && make test_ent_db && ./bin/test_ent_db`
  Expected: "PgSQL init struct success" (provided `test_pgsql_init` is called).

- [ ] **Step 5: Commit**
  ```bash
  git add inc/ent_db.h comm/ent_db.c test/test_ent_db.c
  git commit -m "feat: add pgsql type and config structs"
  ```

### Task 3: Postgres Connection (Open/Close) Implementation

**Files:**
- Modify: `comm/ent_db.c`

- [ ] **Step 1: Write test stub in `test_ent_db.c`**
  Update `test_pgsql_init` to call `ENT_DbOpen(dbHandle)`. We expect it to possibly fail if PostgreSQL server isn't running locally, but it shouldn't crash.
  ```c
  sts = ENT_DbOpen(dbHandle);
  if (sts != 0) {
      printf("PgSQL open gracefully failed (as expected without local db)\n");
  }
  ```

- [ ] **Step 2: Run test to fail compilation/execution**
  Run: `cd build && make test_ent_db`
  Expected: Missing open switch logic.

- [ ] **Step 3: Implement `ENT_DbPgSQLInit` and `ENT_DbPgSQLClose`**
  ```c
  // comm/ent_db.c
  #if ENT_ENABLE_PGSQL
  MSG_ID_T ENT_DbPgSQLInit(DB_HANDLE dbHandle)
  {
      DB_CFG* dbCfg = (DB_CFG*)dbHandle;
      char conninfo[1024];
      snprintf(conninfo, sizeof(conninfo), "host=%s port=%d dbname=%s user=%s password=%s",
               dbCfg->host ? dbCfg->host : "",
               dbCfg->portNo ? dbCfg->portNo : 5432,
               dbCfg->database ? dbCfg->database : "",
               dbCfg->userName ? dbCfg->userName : "",
               dbCfg->passwd ? dbCfg->passwd : "");

      dbCfg->dbInstance.pgsql = PQconnectdb(conninfo);
      if (PQstatus(dbCfg->dbInstance.pgsql) != CONNECTION_OK) {
          IENT_LOG_ERROR("PgSQL Connection failed: %s\n", PQerrorMessage(dbCfg->dbInstance.pgsql));
          PQfinish(dbCfg->dbInstance.pgsql);
          dbCfg->dbInstance.pgsql = NULL;
          return -3;
      }
      dbCfg->isOpen = true;
      return 0;
  }

  MSG_ID_T ENT_DbPgSQLClose(DB_HANDLE dbHandle)
  {
      DB_CFG* dbCfg = (DB_CFG*)dbHandle;
      if (dbCfg->dbInstance.pgsql) {
          PQfinish(dbCfg->dbInstance.pgsql);
          dbCfg->dbInstance.pgsql = NULL;
      }
      dbCfg->isOpen = false;
      
      if(dbCfg->host) free(dbCfg->host);
      if(dbCfg->database) free(dbCfg->database); 
      if(dbCfg->userName) free(dbCfg->userName);
      if(dbCfg->passwd) free(dbCfg->passwd);

  #ifdef WIN32
      DeleteCriticalSection(&dbCfg->cs);
  #else
      pthread_mutex_destroy(&dbCfg->cs);
  #endif    
      sDbNum--;
      return 0;
  }
  #endif
  ```
  Add `case PGSQL_TYPE:` to `ENT_DbOpen` and `ENT_DbCloseHandle` calling these functions respectively. Wait, for `ENT_DbCloseHandle` we need to replace the step 2 stub with actual calls to `ENT_DbPgSQLClose`. In `ENT_DbOpen`:
  ```c
  case PGSQL_TYPE:
  #if ENT_ENABLE_PGSQL
      sts = ENT_DbPgSQLInit(dbCfg);
  #else
      sts = iENT_DbBackendUnsupported(PGSQL_TYPE);
  #endif
      break;
  ```

- [ ] **Step 4: Run test to verify**
  Run: `cd build && make test_ent_db && ./bin/test_ent_db`
  Expected: Graceful error if PG not available locally, but no segmentation faults.

- [ ] **Step 5: Commit**
  ```bash
  git add comm/ent_db.c test/test_ent_db.c
  git commit -m "feat: implement pgsql init and close"
  ```

### Task 4: Postgres Read and Write Implementation

**Files:**
- Modify: `comm/ent_db.c`

- [ ] **Step 1: Write `ENT_DbPgSQLRead` and `ENT_DbPgSQLWrite` implementation**
  ```c
  // comm/ent_db.c
  #if ENT_ENABLE_PGSQL
  MSG_ID_T ENT_DbPgSQLRead(PGconn* dbHandle, const char* query, SqlResultCB userCb, void* userData)
  {
      PGresult *res = PQexec(dbHandle, query);
      if (PQresultStatus(res) != PGRES_TUPLES_OK) {
          IENT_LOG_ERROR("PgSQL Read failed: %s\n", PQerrorMessage(dbHandle));
          PQclear(res);
          return -4;
      }

      int rows = PQntuples(res);
      int cols = PQnfields(res);
      
      char** fields = (char**)malloc(cols * sizeof(char*));
      char** rowRes = (char**)malloc(rows * cols * sizeof(char*));

      for (int i = 0; i < cols; i++) {
          fields[i] = PQfname(res, i);
      }

      for (int r = 0; r < rows; r++) {
          for (int c = 0; c < cols; c++) {
              rowRes[r * cols + c] = PQgetvalue(res, r, c);
          }
      }

      if (userCb) {
          userCb(fields, rowRes, rows, cols, userData);
      }

      free(fields);
      free(rowRes);
      PQclear(res);
      return 0;
  }

  MSG_ID_T ENT_DbPgSQLWrite(PGconn* dbHandle, const char* query, SqlResultCB userCb, void* userData)
  {
      PGresult *res = PQexec(dbHandle, query);
      ExecStatusType status = PQresultStatus(res);
      if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
          IENT_LOG_ERROR("PgSQL Write failed: %s\n", PQerrorMessage(dbHandle));
          PQclear(res);
          return -4;
      }

      // Normally a write returns modified rows via PQcmdTuples
      long long affectedRows = atoll(PQcmdTuples(res));
      
      if (userCb) {
          // Send back simple affected rows response. Fields/RowRes mapping can be NULL.
          userCb(NULL, NULL, affectedRows, 0, userData);
      }

      PQclear(res);
      return 0;
  }
  #endif
  ```

- [ ] **Step 2: Update switch routing for Read/Write**
  In `ENT_DbRead` and `ENT_DbWrite`, add `case PGSQL_TYPE:` calling these handlers just like the Open/Close routing logic.

- [ ] **Step 3: Run CMake and Make to verify full build**
  Run: `cd build && cmake .. && make test_ent_db`
  Expected: Successful compilation without warnings.

- [ ] **Step 4: Commit**
  ```bash
  git add comm/ent_db.c
  git commit -m "feat: implement pgsql read and write handlers"
  ```
