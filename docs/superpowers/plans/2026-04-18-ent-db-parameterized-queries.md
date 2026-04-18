# ENT DB Parameterized Queries Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a parameterized database query API that blocks SQL injection on the supported backends while keeping the existing raw SQL entry points as compatibility wrappers.

**Architecture:** Introduce a backend-neutral parameter array in the public DB API, then route execution through backend-specific prepared-statement helpers. Keep `ENT_DbRead` and `ENT_DbWrite` as raw-SQL compatibility paths so existing callers do not break, but move security-sensitive code and tests to the new parameterized entry points. Add explicit error codes for parameter validation and binding failures so callers can distinguish invalid input from backend execution failures.

**Tech Stack:** C, SQLite C API, MySQL C API, PostgreSQL libpq, existing ENT message-code system.

---

### Task 1: Define the public parameterized DB API and message codes

**Files:**
- Modify: `inc/ent_db.h`
- Modify: `comm/ient_db.h`
- Modify: `msg/ent.msg`
- Modify: `AGENTS.md`
- Test: `test/test_security.c`

- [ ] **Step 1: Add the failing API usage to the security test**

```c
ENT_DB_PARAM params[1];
params[0].type = ENT_DB_PARAM_TEXT_E;
params[0].value.text = "Alice";
ret = ENT_DbReadParams(db,
                       "SELECT name FROM public_info WHERE name = ?",
                       params,
                       1,
                       inject_capture_cb,
                       &cap);
ASSERT_EQ(0, ret, "ENT_DbReadParams should succeed with one bound text parameter");
```

- [ ] **Step 2: Run the targeted test to confirm the new API does not compile yet**

Run: `cmake --build build --target test_security`
Expected: compile fails with missing `ENT_DB_PARAM` / `ENT_DbReadParams` declarations.

- [ ] **Step 3: Define the public types and function declarations**

```c
typedef enum ENT_DB_PARAM_TYPE_TAG
{
    ENT_DB_PARAM_TEXT_E = 1,
    ENT_DB_PARAM_INT_E,
    ENT_DB_PARAM_INT64_E,
    ENT_DB_PARAM_DOUBLE_E,
    ENT_DB_PARAM_NULL_E,
    ENT_DB_PARAM_BLOB_E,
} ENT_DB_PARAM_TYPE;

typedef struct ENT_DB_PARAM_TAG
{
    ENT_DB_PARAM_TYPE type;
    union
    {
        const char* text;
        int         i32;
        long long   i64;
        double      d64;
        struct
        {
            const void* data;
            size_t      size;
        } blob;
    } value;
} ENT_DB_PARAM;

ENT_PUBLIC MSG_ID_T ENT_DbReadParams(DB_HANDLE dbHandle,
                                     const char* sql,
                                     const ENT_DB_PARAM* params,
                                     size_t paramCount,
                                     SqlResultCB sqlCb,
                                     void* userData);

ENT_PUBLIC MSG_ID_T ENT_DbWriteParams(DB_HANDLE dbHandle,
                                      const char* sql,
                                      const ENT_DB_PARAM* params,
                                      size_t paramCount,
                                      SqlResultCB sqlCb,
                                      void* userData);
```

- [ ] **Step 4: Add internal helper declarations and new DB error codes**

```c
MSG_ID_T ENT_DbSqliteReadParams(sqlite3* dbHandle,
                                const char* sql,
                                const ENT_DB_PARAM* params,
                                size_t paramCount,
                                SqlResultCB userCb,
                                void* userData);
MSG_ID_T ENT_DbSqliteWriteParams(sqlite3* dbHandle,
                                 const char* sql,
                                 const ENT_DB_PARAM* params,
                                 size_t paramCount,
                                 SqlResultCB userCb,
                                 void* userData);
```

Add message codes for:
- bad parameter array / bad parameter count
- statement prepare failure
- bind failure
- execute failure

- [ ] **Step 5: Update the message-code rules for the new DB submodule**

Add the new DB parameterization message submodule and codes to `AGENTS.md` in the same change set so the repo rules stay synchronized with `msg/ent.msg`.

- [ ] **Step 6: Rebuild the security test to verify the headers and codes are wired**

Run: `cmake --build build --target test_security`
Expected: compile still fails in the implementation files, but the public DB API headers now resolve.

- [ ] **Step 7: Commit the API and message-code scaffolding**

```bash
git add inc/ent_db.h comm/ient_db.h msg/ent.msg AGENTS.md test/test_security.c
git commit -m "feat: add db parameterized query api"
```

### Task 2: Implement SQLite parameterized execution

**Files:**
- Modify: `comm/ent_db_sqlite.c`
- Modify: `comm/ent_db.c`
- Modify: `comm/ient_db.h`
- Test: `test/test_security.c`

- [ ] **Step 1: Write a failing SQLite regression that proves injection is blocked**

```c
const char* injection = "Alice' OR 1=1; DROP TABLE secrets; --";
ENT_DB_PARAM params[1];
params[0].type = ENT_DB_PARAM_TEXT_E;
params[0].value.text = injection;
ret = ENT_DbReadParams(db,
                       "SELECT data FROM secrets WHERE data = ?",
                       params,
                       1,
                       inject_capture_cb,
                       &cap);
ASSERT_EQ(0, ret, "parameterized SQLite query should succeed");
ret = ENT_DbRead(db, "SELECT * FROM secrets;", NULL, NULL);
ASSERT_EQ(0, ret, "raw table should still exist after bound query");
```

- [ ] **Step 2: Run the test to confirm the SQLite path still uses raw `sqlite3_exec`**

Run: `./build/bin/test_security`
Expected: the new test fails or reports injection findings because the parameterized path is not implemented yet.

- [ ] **Step 3: Add SQLite statement preparation and binding helpers**

```c
static MSG_ID_T iENT_DbSqliteBindParams(sqlite3_stmt* stmt,
                                        const ENT_DB_PARAM* params,
                                        size_t paramCount)
{
    for(size_t i = 0; i < paramCount; ++i)
    {
        switch(params[i].type)
        {
            case ENT_DB_PARAM_TEXT_E:
                sqlite3_bind_text(stmt, (int)i + 1, params[i].value.text, -1, SQLITE_TRANSIENT);
                break;
            case ENT_DB_PARAM_INT_E:
                sqlite3_bind_int(stmt, (int)i + 1, params[i].value.i32);
                break;
            case ENT_DB_PARAM_INT64_E:
                sqlite3_bind_int64(stmt, (int)i + 1, (sqlite3_int64)params[i].value.i64);
                break;
            case ENT_DB_PARAM_DOUBLE_E:
                sqlite3_bind_double(stmt, (int)i + 1, params[i].value.d64);
                break;
            case ENT_DB_PARAM_NULL_E:
                sqlite3_bind_null(stmt, (int)i + 1);
                break;
            default:
                return ENT_DBS_BAD_ARGUMENT;
        }
    }
    return ENT_SYS_NORMAL;
}
```

Use `sqlite3_prepare_v2`, `sqlite3_bind_*`, `sqlite3_step`, `sqlite3_column_*`, and `sqlite3_finalize` instead of `sqlite3_exec` for the new parameterized path.

- [ ] **Step 4: Route `ENT_DbReadParams` and `ENT_DbWriteParams` through the SQLite helpers**

Keep the raw `ENT_DbRead` and `ENT_DbWrite` wrappers unchanged so existing behavior stays available.

- [ ] **Step 5: Update the security test to assert the parameterized path blocks injection**

Change the injection test to use `ENT_DbReadParams`/`ENT_DbWriteParams` for the sensitive queries, then assert the `secrets` table still exists and the UNION payload does not leak.

- [ ] **Step 6: Rebuild and run the security test**

Run: `cmake --build build --target test_security && ./build/bin/test_security`
Expected: the SQLite injection cases no longer report findings.

- [ ] **Step 7: Commit the SQLite implementation**

```bash
git add comm/ent_db_sqlite.c comm/ent_db.c comm/ient_db.h test/test_security.c
git commit -m "fix: add sqlite parameterized queries"
```

### Task 3: Implement PostgreSQL parameterized execution

**Files:**
- Modify: `comm/ent_db_pgsql.c`
- Modify: `comm/ent_db.c`
- Modify: `comm/ient_db.h`
- Test: `test/test_security.c`

- [ ] **Step 1: Write the PostgreSQL parameterized query path as a failing regression**

```c
ENT_DB_PARAM params[1];
params[0].type = ENT_DB_PARAM_TEXT_E;
params[0].value.text = "s3cr3t_p@ss";
ret = ENT_DbReadParams(db,
                       "SELECT password FROM private_creds WHERE password = $1",
                       params,
                       1,
                       inject_capture_cb,
                       &cap);
ASSERT_EQ(0, ret, "parameterized PostgreSQL query should succeed");
```

- [ ] **Step 2: Run the backend-specific build to confirm the libpq path is still raw**

Run: `cmake --build build --target test_security`
Expected: the PostgreSQL parameterized calls fail to link or fail at runtime until the new API is implemented.

- [ ] **Step 3: Add a libpq parameter helper**

```c
static const char* iENT_DbPgSQLParamText(const ENT_DB_PARAM* param)
{
    return param->type == ENT_DB_PARAM_TEXT_E ? param->value.text : NULL;
}
```

Use `PQexecParams` with `paramValues`, `paramLengths`, and `paramFormats` so the backend sends values separately from the SQL text.

- [ ] **Step 4: Keep raw `ENT_DbRead`/`ENT_DbWrite` behavior unchanged**

The compatibility wrappers should still call the existing raw execution helpers for callers that have not migrated.

- [ ] **Step 5: Rebuild and verify PostgreSQL-specific code**

Run: `cmake --build build --target test_security`
Expected: the parameterized path compiles cleanly when PostgreSQL is enabled.

- [ ] **Step 6: Commit the PostgreSQL implementation**

```bash
git add comm/ent_db_pgsql.c comm/ent_db.c comm/ient_db.h test/test_security.c
git commit -m "fix: add pgsql parameterized queries"
```

### Task 4: Implement MySQL parameterized execution

**Files:**
- Modify: `comm/ent_db_mysql.c`
- Modify: `comm/ent_db.c`
- Modify: `comm/ient_db.h`
- Test: `test/test_security.c`

- [ ] **Step 1: Add a failing MySQL parameterized query regression**

```c
ENT_DB_PARAM params[1];
params[0].type = ENT_DB_PARAM_TEXT_E;
params[0].value.text = "Alice";
ret = ENT_DbReadParams(db,
                       "SELECT name FROM public_info WHERE name = ?",
                       params,
                       1,
                       inject_capture_cb,
                       &cap);
ASSERT_EQ(0, ret, "parameterized MySQL query should succeed");
```

- [ ] **Step 2: Run the MySQL build to confirm prepared statements are not wired yet**

Run: `cmake --build build --target test_security`
Expected: the MySQL parameterized path is still missing or incomplete before the implementation lands.

- [ ] **Step 3: Implement `MYSQL_STMT` preparation, binding, and fetch helpers**

```c
MYSQL_STMT* stmt = mysql_stmt_init(dbHandle);
mysql_stmt_prepare(stmt, sql, (unsigned long)strlen(sql));
mysql_stmt_bind_param(stmt, bind);
mysql_stmt_execute(stmt);
```

Use typed binds for text, integers, 64-bit integers, doubles, nulls, and blobs where available.

- [ ] **Step 4: Route the new parameterized public API through the MySQL helpers**

Keep raw `ENT_DbRead` and `ENT_DbWrite` as compatibility wrappers that still accept plain SQL strings.

- [ ] **Step 5: Rebuild and verify the MySQL path**

Run: `cmake --build build --target test_security`
Expected: the parameterized MySQL path compiles cleanly and the security test remains green when MySQL is enabled.

- [ ] **Step 6: Commit the MySQL implementation**

```bash
git add comm/ent_db_mysql.c comm/ent_db.c comm/ient_db.h test/test_security.c
git commit -m "fix: add mysql parameterized queries"
```

### Task 5: Retire the injection findings from the security suite and validate compatibility

**Files:**
- Modify: `test/test_security.c`
- Modify: `test/test_ent_db.c`
- Modify: `test/CMakeLists.txt`
- Modify: `comm/CMakeLists.txt`

- [ ] **Step 1: Update the security suite to exercise the new API instead of raw SQL injection**

```c
ENT_DB_PARAM params[1];
params[0].type = ENT_DB_PARAM_INT_E;
params[0].value.i32 = 1;
ret = ENT_DbReadParams(db,
                       "SELECT * FROM secrets WHERE id = ?",
                       params,
                       1,
                       NULL,
                       NULL);
ASSERT_EQ(0, ret, "parameterized DB read should succeed");
```

Replace the DROP TABLE and UNION SELECT findings with assertions that the bound queries do not mutate schema and do not leak data.

- [ ] **Step 2: Add a compatibility smoke test for raw SQL callers**

```c
ret = ENT_DbRead(db, "SELECT 1;", NULL, NULL);
ASSERT_EQ(0, ret, "raw SQL compatibility path should still work");
```

- [ ] **Step 3: Rebuild the DB and security targets**

Run: `cmake --build build --target test_ent_db test_security`
Expected: both targets build cleanly with the new API in place.

- [ ] **Step 4: Run the full DB-focused smoke tests**

Run: `./build/bin/test_ent_db && ./build/bin/test_security`
Expected: the injection findings are gone, and the raw SQL compatibility smoke test still passes.

- [ ] **Step 5: Commit the final compatibility and test updates**

```bash
git add test/test_security.c test/test_ent_db.c test/CMakeLists.txt comm/CMakeLists.txt
git commit -m "test: migrate db security checks to parameterized queries"
```
