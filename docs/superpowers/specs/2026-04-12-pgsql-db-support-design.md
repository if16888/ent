# PostgreSQL Database Support Design

## 1. Overview
The `ent` framework currently abstracts database operations by providing a unified interface (`ENT_DbInitHandle`, `ENT_DbOpen`, `ENT_DbRead`, `ENT_DbWrite`, etc.) masking internal SQLite and MySQL implementations. This specification defines the design to add PostgreSQL (PgSQL) support into the same module using the official `libpq` C client.

## 2. Architecture & Backend Approach 

We will adhere to the "Direct Integration via Macro Branching" model. PostgreSQL support will be built directly into `ent_db.c`, parallel to SQLite and MySQL branches, maximizing backwards-compatibility and ensuring identical synchronous callback behavior (`SqlResultCB`).

### 2.1 Interface Extensions (`ent_db.h`)
- Add `PGSQL_TYPE = 5` to the `DB_TYPE` enumeration.
- No other changes to the public header are required. All queries remain string-based executed synchronously.

### 2.2 Internal Structure Changes (`ent_db.c`)
- Add the required preprocessing conditions: 
  ```c
  #ifndef ENT_ENABLE_PGSQL
  #define ENT_ENABLE_PGSQL 0
  #endif

  #if ENT_ENABLE_PGSQL
  #include <libpq-fe.h>
  #endif
  ```
- Expand the `dbInstance` union in `DB_CFG`:
  ```c
  union {
      // existing sqlite and mysql definitions...
  #if ENT_ENABLE_PGSQL
      PGconn*  pgsql;
  #endif
      void*    raw;
  } dbInstance;
  ```

### 2.3 PostgreSQL Internal Handlers
Implementation of core PostgreSQL procedures inside `ent_db.c`:

1. **`ENT_DbPgSQLInit`**:
   Constructs the connection string dynamically from `DB_CFG->host`, `portNo`, `database`, `userName`, and `passwd`. Uses `PQconnectdb()` to obtain a `PGconn*`. Verifies success via `PQstatus(conn) == CONNECTION_OK`.

2. **`ENT_DbPgSQLClose`**:
   Closes the underlying handler via `PQfinish()`, safely setting the instance pointer to `NULL`, freeing connection credentials if necessary.

3. **`ENT_DbPgSQLRead` / `ENT_DbPgSQLWrite`**:
   - Transmits queries synchronously using `PQexec()`.
   - Iterates through the result using `PQntuples()` and `PQnfields()`.
   - Dynamically constructs `char** fields` (column metadata via `PQfname`) and `char** rowRes` (field strings via `PQgetvalue`).
   - Invokes `SqlResultCB` with the assembled data and rows/columns counts.
   - Cleans up generated memory and uses `PQclear()` on the `PGresult`.

### 2.4 Control Flow Dispatch Update
In functions `ENT_DbInitHandle`, `ENT_DbOpen`, `ENT_DbCloseHandle`, `ENT_DbRead`, `ENT_DbWrite`, the `switch(dbType)` blocks will be extended with:
  ```c
  case PGSQL_TYPE:
  #if ENT_ENABLE_PGSQL
      sts = ENT_DbPgSQL<Action>(...);
  #else
      sts = iENT_DbBackendUnsupported(PGSQL_TYPE);
  #endif
      break;
  ```

## 3. Build System (`CMakeLists.txt`)
- In `CMakeLists.txt` (or via global flags), if `ENT_ENABLE_PGSQL` is desired, we will load PostgreSQL resources:
  ```cmake
  find_package(PostgreSQL REQUIRED)
  ```
- Link libraries using `PostgreSQL::PostgreSQL` (or fallback vars like `${PostgreSQL_LIBRARIES}`) to targets `test_ent_db` and `ent_objs` when `PGSQL` flag is active.

## 4. Error Handling and Constraints
- **Security**: The current interface takes raw `sql` strings. Parameterized queries (`PQexecParams`) are out-of-scope for the legacy-compatible Read/Write function mapping, but consumers must be aware of SQL-injection limits.
- **Failovers**: Network/Authentication failures inside `PQconnectdb` or `PQexec` must be gracefully caught and outputted using `IENT_LOG_ERROR`, avoiding any hard aborts. Memory allocated during data conversion for callbacks must be freed using a guaranteed `goto END` or cleanup routine.
