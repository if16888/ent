#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "ient_comm.h"
#include "ient_runtime.h"
#include "ent_db.h"
#include "ent_msg.h"

ENT_CTX gEntCtx;

typedef struct DB_READ_CAPTURE {
    int called;
    long long row_num;
    int column_num;
    char name[32];
} DB_READ_CAPTURE;

static int expect_true(int condition, const char* message)
{
    if(!condition)
    {
        fprintf(stderr, "%s\n", message);
        return 1;
    }

    return 0;
}

MSG_ID_T ENT_LogInit(void)
{
    return 0;
}

MSG_ID_T ENT_LogClose(void)
{
    return 0;
}

MSG_ID_T ENT_LogInitHandle(ENT_LOG* pLogHandle, const char* moduleName, const char* logPath)
{
    (void)pLogHandle;
    (void)moduleName;
    (void)logPath;
    return 0;
}

MSG_ID_T ENT_LogSetOption(ENT_LOG logHandle, ENT_LOG_OPTIONS_E option, const void* arg)
{
    (void)logHandle;
    (void)option;
    (void)arg;
    return 0;
}

MSG_ID_T ENT_LogCloseHandle(ENT_LOG logHandle)
{
    (void)logHandle;
    return 0;
}

MSG_ID_T ENT_LogRaw(ENT_LOG logHandle, const char* format, ...)
{
    (void)logHandle;
    (void)format;
    return 0;
}

MSG_ID_T ENT_LogFatal(ENT_LOG logHandle, const char* format, ...)
{
    (void)logHandle;
    (void)format;
    return 0;
}

MSG_ID_T ENT_LogError(ENT_LOG logHandle, const char* format, ...)
{
    (void)logHandle;
    (void)format;
    return 0;
}

MSG_ID_T ENT_LogWarn(ENT_LOG logHandle, const char* format, ...)
{
    (void)logHandle;
    (void)format;
    return 0;
}

MSG_ID_T ENT_LogPrint(ENT_LOG logHandle, const char* format, ...)
{
    (void)logHandle;
    (void)format;
    return 0;
}

MSG_ID_T ENT_LogDebug(ENT_LOG logHandle, const char* format, ...)
{
    (void)logHandle;
    (void)format;
    return 0;
}

static int prepare_temp_db_path(char* db_path, size_t db_path_len)
{
#ifdef WIN32
    char temp_dir[MAX_PATH];
    char temp_file[MAX_PATH];

    if(GetTempPathA((DWORD)sizeof(temp_dir), temp_dir) == 0)
    {
        fprintf(stderr, "GetTempPathA failed\n");
        return 1;
    }

    if(GetTempFileNameA(temp_dir, "edb", 0, temp_file) == 0)
    {
        fprintf(stderr, "GetTempFileNameA failed\n");
        return 1;
    }

    DeleteFileA(temp_file);

    if(snprintf(db_path, db_path_len, "%s.sqlite", temp_file) >= (int)db_path_len)
    {
        fprintf(stderr, "temporary SQLite path is too long\n");
        return 1;
    }

    DeleteFileA(db_path);
    return 0;
#else
    char db_template[] = "/tmp/ent_db_test_XXXXXX";
    int fd = mkstemp(db_template);

    if(fd < 0)
    {
        perror("mkstemp");
        return 1;
    }

    close(fd);
    unlink(db_template);

    if(snprintf(db_path, db_path_len, "%s.sqlite", db_template) >= (int)db_path_len)
    {
        fprintf(stderr, "temporary SQLite path is too long\n");
        return 1;
    }

    unlink(db_path);
    return 0;
#endif
}

static void cleanup_temp_db_path(const char* db_path)
{
#ifdef WIN32
    DeleteFileA(db_path);
#else
    unlink(db_path);
#endif
}

static void capture_single_name_row(char** fields, char** row_res, long long row_num, int column_num, void* user_data)
{
    DB_READ_CAPTURE* capture = (DB_READ_CAPTURE*)user_data;
    int column_idx = 0;

    if(capture == NULL)
    {
        return;
    }

    capture->called++;
    capture->row_num = row_num;
    capture->column_num = column_num;

    for(column_idx = 0; column_idx < column_num; ++column_idx)
    {
        if(fields[column_idx] != NULL && strcmp(fields[column_idx], "name") == 0 && row_res[column_idx] != NULL)
        {
            snprintf(capture->name, sizeof(capture->name), "%s", row_res[column_idx]);
        }
    }
}

static int reset_db_service(void)
{
    ENT_DbClose();
    return ENT_DbInit();
}

static const char* read_env_or_null(const char* name)
{
    const char* value = getenv(name);
    if(value == NULL || value[0] == '\0')
    {
        return NULL;
    }

    return value;
}

static int read_env_or_default_int(const char* name, int fallback)
{
    const char* value = getenv(name);
    if(value == NULL || value[0] == '\0')
    {
        return fallback;
    }

    return atoi(value);
}

static int test_db_init_handle_rejects_uninitialized_service(void)
{
    DB_HANDLE db_handle = NULL;

    ENT_DbClose();

    return expect_true(ENT_DbInitHandle(&db_handle, SQLITE_TYPE, NULL, "ignored.db", NULL, NULL, 0) == ENT_DBS_NOT_INITIALIZED,
                       "ENT_DbInitHandle should reject calls before ENT_DbInit");
}

static int test_db_api_rejects_null_handles(void)
{
    MSG_ID_T init_sts = 0;

    init_sts = reset_db_service();
    if(expect_true(init_sts == ENT_SYS_NORMAL || init_sts == ENT_SYS_ALREADY_INITIALIZED,
                   "ENT_DbInit should initialize the DB service before NULL-handle validation") != 0)
    {
        return 1;
    }

    if(expect_true(ENT_DbOpen(NULL) == ENT_DBS_BAD_ARGUMENT,
                   "ENT_DbOpen should reject NULL handles") != 0)
    {
        ENT_DbClose();
        return 1;
    }

    if(expect_true(ENT_DbRead(NULL, "SELECT 1;", NULL, NULL) == ENT_DBS_BAD_ARGUMENT,
                   "ENT_DbRead should reject NULL handles") != 0)
    {
        ENT_DbClose();
        return 1;
    }

    if(expect_true(ENT_DbWrite(NULL, "SELECT 1;", NULL, NULL) == ENT_DBS_BAD_ARGUMENT,
                   "ENT_DbWrite should reject NULL handles") != 0)
    {
        ENT_DbClose();
        return 1;
    }

    return expect_true(ENT_DbClose() == 0,
                       "ENT_DbClose should close the DB service after NULL-handle validation");
}

static int test_db_close_rejects_live_handles(void)
{
    char db_path[512];
    DB_HANDLE db_handle = NULL;
    MSG_ID_T init_sts = 0;

    memset(db_path, 0, sizeof(db_path));
    if(prepare_temp_db_path(db_path, sizeof(db_path)) != 0)
    {
        return 1;
    }

    init_sts = reset_db_service();
    if(expect_true(init_sts == ENT_SYS_NORMAL || init_sts == ENT_SYS_ALREADY_INITIALIZED,
                   "ENT_DbInit should initialize the DB service before live-handle close validation") != 0)
    {
        cleanup_temp_db_path(db_path);
        return 1;
    }

    if(expect_true(ENT_DbInitHandle(&db_handle, SQLITE_TYPE, NULL, db_path, NULL, NULL, 0) == 0,
                   "ENT_DbInitHandle should create a SQLite handle for live-handle close validation") != 0)
    {
        ENT_DbClose();
        cleanup_temp_db_path(db_path);
        return 1;
    }

    if(expect_true(ENT_DbClose() == ENT_DBS_IN_USE,
                   "ENT_DbClose should refuse to tear down the DB service while handles are still live") != 0)
    {
        ENT_DbCloseHandle(db_handle);
        ENT_DbClose();
        cleanup_temp_db_path(db_path);
        return 1;
    }

    if(expect_true(ENT_DbCloseHandle(db_handle) == 0,
                   "ENT_DbCloseHandle should close the live SQLite handle after service-close rejection") != 0)
    {
        ENT_DbClose();
        cleanup_temp_db_path(db_path);
        return 1;
    }

    cleanup_temp_db_path(db_path);
    return expect_true(ENT_DbClose() == 0,
                       "ENT_DbClose should succeed after all live handles are closed");
}

static int test_sqlite_open_rejects_missing_database_path(void)
{
    DB_HANDLE db_handle = NULL;
    MSG_ID_T init_sts = 0;

    init_sts = reset_db_service();
    if(expect_true(init_sts == ENT_SYS_NORMAL || init_sts == ENT_SYS_ALREADY_INITIALIZED,
                   "ENT_DbInit should initialize the DB service") != 0)
    {
        return 1;
    }

    if(expect_true(ENT_DbInitHandle(&db_handle, SQLITE_TYPE, NULL, NULL, NULL, NULL, 0) == 0,
                   "ENT_DbInitHandle should create a SQLite handle without opening it yet") != 0)
    {
        ENT_DbClose();
        return 1;
    }

    if(expect_true(ENT_DbOpen(db_handle) == ENT_DBS_BAD_ARGUMENT,
                   "ENT_DbOpen should reject a SQLite handle without a database path") != 0)
    {
        ENT_DbCloseHandle(db_handle);
        ENT_DbClose();
        return 1;
    }

    if(expect_true(ENT_DbCloseHandle(db_handle) == 0,
                   "ENT_DbCloseHandle should clean up a closed SQLite handle") != 0)
    {
        ENT_DbClose();
        return 1;
    }

    return expect_true(ENT_DbClose() == 0,
                       "ENT_DbClose should close the DB service after the missing-path test");
}

static int test_sqlite_open_is_idempotent(void)
{
    char db_path[512];
    DB_HANDLE db_handle = NULL;
    MSG_ID_T init_sts = 0;

    memset(db_path, 0, sizeof(db_path));

    if(prepare_temp_db_path(db_path, sizeof(db_path)) != 0)
    {
        return 1;
    }

    init_sts = reset_db_service();
    if(expect_true(init_sts == ENT_SYS_NORMAL || init_sts == ENT_SYS_ALREADY_INITIALIZED,
                   "ENT_DbInit should initialize the DB service before idempotent open testing") != 0)
    {
        cleanup_temp_db_path(db_path);
        return 1;
    }

    if(expect_true(ENT_DbInitHandle(&db_handle, SQLITE_TYPE, NULL, db_path, NULL, NULL, 0) == 0,
                   "ENT_DbInitHandle should create a SQLite handle for idempotent open testing") != 0)
    {
        ENT_DbClose();
        cleanup_temp_db_path(db_path);
        return 1;
    }

    if(expect_true(ENT_DbOpen(db_handle) == 0,
                   "ENT_DbOpen should open a valid SQLite handle") != 0)
    {
        ENT_DbCloseHandle(db_handle);
        ENT_DbClose();
        cleanup_temp_db_path(db_path);
        return 1;
    }

    {
        MSG_ID_T reopen_sts = ENT_DbOpen(db_handle);
        if(expect_true(reopen_sts == 0,
                   "ENT_DbOpen should treat an already-open SQLite handle as success") != 0)
        {
            ENT_DbCloseHandle(db_handle);
            ENT_DbClose();
            cleanup_temp_db_path(db_path);
            return 1;
        }
    }

    if(expect_true(ENT_DbCloseHandle(db_handle) == 0,
                   "ENT_DbCloseHandle should close the SQLite handle after idempotent open testing") != 0)
    {
        ENT_DbClose();
        cleanup_temp_db_path(db_path);
        return 1;
    }

    cleanup_temp_db_path(db_path);
    return expect_true(ENT_DbClose() == 0,
                       "ENT_DbClose should close the DB service after idempotent open testing");
}

static int test_sqlite_write_and_read_roundtrip(void)
{
    char db_path[512];
    DB_HANDLE db_handle = NULL;
    DB_READ_CAPTURE capture;
    MSG_ID_T init_sts = 0;

    memset(&capture, 0, sizeof(capture));
    memset(db_path, 0, sizeof(db_path));

    if(prepare_temp_db_path(db_path, sizeof(db_path)) != 0)
    {
        return 1;
    }

    init_sts = reset_db_service();
    if(expect_true(init_sts == ENT_SYS_NORMAL || init_sts == ENT_SYS_ALREADY_INITIALIZED,
                   "ENT_DbInit should initialize the DB service") != 0)
    {
        return 1;
    }

    if(expect_true(ENT_DbInitHandle(&db_handle, SQLITE_TYPE, NULL, db_path, NULL, NULL, 0) == 0,
                   "ENT_DbInitHandle should create a SQLite handle") != 0)
    {
        ENT_DbClose();
        cleanup_temp_db_path(db_path);
        return 1;
    }

    if(expect_true(ENT_DbWrite(db_handle, "CREATE TABLE test_user(id INTEGER PRIMARY KEY, name TEXT NOT NULL);", NULL, NULL) == 0,
                   "ENT_DbWrite should create a SQLite table") != 0)
    {
        ENT_DbCloseHandle(db_handle);
        ENT_DbClose();
        cleanup_temp_db_path(db_path);
        return 1;
    }

    if(expect_true(ENT_DbWrite(db_handle, "INSERT INTO test_user(name) VALUES('alice');", NULL, NULL) == 0,
                   "ENT_DbWrite should insert a SQLite row") != 0)
    {
        ENT_DbCloseHandle(db_handle);
        ENT_DbClose();
        cleanup_temp_db_path(db_path);
        return 1;
    }

    if(expect_true(ENT_DbRead(db_handle,
                              "SELECT name FROM test_user WHERE id = 1;",
                              capture_single_name_row,
                              &capture) == 0,
                   "ENT_DbRead should fetch the inserted SQLite row") != 0)
    {
        ENT_DbCloseHandle(db_handle);
        ENT_DbClose();
        cleanup_temp_db_path(db_path);
        return 1;
    }

    if(expect_true(capture.called == 1, "ENT_DbRead should invoke the SQLite callback once") != 0)
    {
        ENT_DbCloseHandle(db_handle);
        ENT_DbClose();
        cleanup_temp_db_path(db_path);
        return 1;
    }

    if(expect_true(capture.column_num == 1, "ENT_DbRead should return one selected column") != 0)
    {
        ENT_DbCloseHandle(db_handle);
        ENT_DbClose();
        cleanup_temp_db_path(db_path);
        return 1;
    }

    if(expect_true(strcmp(capture.name, "alice") == 0, "ENT_DbRead should return the inserted row contents") != 0)
    {
        ENT_DbCloseHandle(db_handle);
        ENT_DbClose();
        cleanup_temp_db_path(db_path);
        return 1;
    }

    if(expect_true(ENT_DbCloseHandle(db_handle) == 0,
                   "ENT_DbCloseHandle should close an open SQLite handle") != 0)
    {
        ENT_DbClose();
        cleanup_temp_db_path(db_path);
        return 1;
    }

    cleanup_temp_db_path(db_path);
    return expect_true(ENT_DbClose() == 0,
                       "ENT_DbClose should close the DB service after the SQLite roundtrip");
}

static int test_sqlite_write_rejects_invalid_sql(void)
{
    char db_path[512];
    DB_HANDLE db_handle = NULL;
    MSG_ID_T init_sts = 0;

    memset(db_path, 0, sizeof(db_path));

    if(prepare_temp_db_path(db_path, sizeof(db_path)) != 0)
    {
        return 1;
    }

    init_sts = reset_db_service();
    if(expect_true(init_sts == ENT_SYS_NORMAL || init_sts == ENT_SYS_ALREADY_INITIALIZED,
                   "ENT_DbInit should initialize the DB service") != 0)
    {
        return 1;
    }

    if(expect_true(ENT_DbInitHandle(&db_handle, SQLITE_TYPE, NULL, db_path, NULL, NULL, 0) == 0,
                   "ENT_DbInitHandle should create a SQLite handle for invalid SQL testing") != 0)
    {
        ENT_DbClose();
        cleanup_temp_db_path(db_path);
        return 1;
    }

    if(expect_true(ENT_DbWrite(db_handle, "THIS IS NOT SQL", NULL, NULL) == ENT_DBS_QUERY_FAILED,
                   "ENT_DbWrite should return a query failure when SQLite rejects invalid SQL") != 0)
    {
        ENT_DbCloseHandle(db_handle);
        ENT_DbClose();
        cleanup_temp_db_path(db_path);
        return 1;
    }

    if(expect_true(ENT_DbCloseHandle(db_handle) == 0,
                   "ENT_DbCloseHandle should close the SQLite handle after invalid SQL") != 0)
    {
        ENT_DbClose();
        cleanup_temp_db_path(db_path);
        return 1;
    }

    cleanup_temp_db_path(db_path);
    return expect_true(ENT_DbClose() == 0,
                       "ENT_DbClose should close the DB service after the invalid SQL test");
}

static int test_sqlite_read_rejects_callback_without_user_data(void)
{
    char db_path[512];
    DB_HANDLE db_handle = NULL;
    DB_READ_CAPTURE capture;
    MSG_ID_T init_sts = 0;

    memset(&capture, 0, sizeof(capture));
    memset(db_path, 0, sizeof(db_path));

    if(prepare_temp_db_path(db_path, sizeof(db_path)) != 0)
    {
        return 1;
    }

    init_sts = reset_db_service();
    if(expect_true(init_sts == ENT_SYS_NORMAL || init_sts == ENT_SYS_ALREADY_INITIALIZED,
                   "ENT_DbInit should initialize the DB service") != 0)
    {
        return 1;
    }

    if(expect_true(ENT_DbInitHandle(&db_handle, SQLITE_TYPE, NULL, db_path, NULL, NULL, 0) == 0,
                   "ENT_DbInitHandle should create a SQLite handle for callback validation") != 0)
    {
        ENT_DbClose();
        cleanup_temp_db_path(db_path);
        return 1;
    }

    if(expect_true(ENT_DbWrite(db_handle, "CREATE TABLE test_user(id INTEGER PRIMARY KEY, name TEXT NOT NULL);", NULL, NULL) == 0,
                   "ENT_DbWrite should create a SQLite table for callback validation") != 0)
    {
        ENT_DbCloseHandle(db_handle);
        ENT_DbClose();
        cleanup_temp_db_path(db_path);
        return 1;
    }

    if(expect_true(ENT_DbWrite(db_handle, "INSERT INTO test_user(name) VALUES('bob');", NULL, NULL) == 0,
                   "ENT_DbWrite should insert a SQLite row for callback validation") != 0)
    {
        ENT_DbCloseHandle(db_handle);
        ENT_DbClose();
        cleanup_temp_db_path(db_path);
        return 1;
    }

    if(expect_true(ENT_DbRead(db_handle,
                              "SELECT name FROM test_user WHERE id = 1;",
                              capture_single_name_row,
                              NULL) == ENT_DBS_BAD_ARGUMENT,
                   "ENT_DbRead should fail when a callback is provided without user data") != 0)
    {
        ENT_DbCloseHandle(db_handle);
        ENT_DbClose();
        cleanup_temp_db_path(db_path);
        return 1;
    }

    if(expect_true(capture.called == 0,
                   "ENT_DbRead should not populate caller state when SQLite aborts the callback") != 0)
    {
        ENT_DbCloseHandle(db_handle);
        ENT_DbClose();
        cleanup_temp_db_path(db_path);
        return 1;
    }

    if(expect_true(ENT_DbCloseHandle(db_handle) == 0,
                   "ENT_DbCloseHandle should close the SQLite handle after callback validation") != 0)
    {
        ENT_DbClose();
        cleanup_temp_db_path(db_path);
        return 1;
    }

    cleanup_temp_db_path(db_path);
    return expect_true(ENT_DbClose() == 0,
                       "ENT_DbClose should close the DB service after the callback validation test");
}

static int test_pgsql_handle_lifecycle(void)
{
    DB_HANDLE db_handle = NULL;
    MSG_ID_T sts = 0;

    sts = reset_db_service();
    if(expect_true(sts == 0 || sts == 1,
                   "ENT_DbInit should initialize the DB service") != 0)
    {
        return 1;
    }

    sts = ENT_DbInitHandle(&db_handle, PGSQL_TYPE, "127.0.0.1", "test", "root", "123456", 5432);
    if(expect_true(sts == 0 && db_handle != NULL,
                   "ENT_DbInitHandle should create a PgSQL handle structure") != 0)
    {
        ENT_DbClose();
        return 1;
    }

    sts = ENT_DbCloseHandle(db_handle);
#if ENT_ENABLE_PGSQL
    if(expect_true(sts == ENT_SYS_NORMAL,
                   "ENT_DbCloseHandle should clean up the PgSQL handle") != 0)
#else
    if(expect_true(sts == ENT_DBS_UNSUPPORTED,
                   "ENT_DbCloseHandle should report PgSQL as unsupported when it is disabled") != 0)
#endif
    {
        ENT_DbClose();
        return 1;
    }

    return expect_true(ENT_DbClose() == 0,
                       "ENT_DbClose should close the DB service after the PgSQL lifecycle test");
}

#if ENT_ENABLE_PGSQL
static int test_pgsql_roundtrip_if_configured(void)
{
    DB_HANDLE db_handle = NULL;
    DB_READ_CAPTURE capture;
    MSG_ID_T sts = 0;
    const char* host;
    const char* database;
    const char* user;
    const char* passwd;
    int port;

    host = read_env_or_null("ENT_PGSQL_HOST");
    database = read_env_or_null("ENT_PGSQL_DB");
    user = read_env_or_null("ENT_PGSQL_USER");
    passwd = getenv("ENT_PGSQL_PASSWORD");
    if(passwd == NULL)
    {
        passwd = "";
    }
    port = read_env_or_default_int("ENT_PGSQL_PORT", 5432);

    if(host == NULL || database == NULL || user == NULL)
    {
        printf("PgSQL integration test skipped: set ENT_PGSQL_HOST, ENT_PGSQL_DB, and ENT_PGSQL_USER to run it.\n");
        return 0;
    }

    memset(&capture, 0, sizeof(capture));

    sts = reset_db_service();
    if(expect_true(sts == 0 || sts == 1,
                   "ENT_DbInit should initialize the DB service") != 0)
    {
        return 1;
    }

    sts = ENT_DbInitHandle(&db_handle, PGSQL_TYPE, host, database, user, passwd, port);
    if(expect_true(sts == 0 && db_handle != NULL,
                   "ENT_DbInitHandle should create a PgSQL handle for the integration test") != 0)
    {
        ENT_DbClose();
        return 1;
    }

    sts = ENT_DbOpen(db_handle);
    if(expect_true(sts == 0,
                   "ENT_DbOpen should connect to PostgreSQL when the integration environment is configured") != 0)
    {
        ENT_DbCloseHandle(db_handle);
        ENT_DbClose();
        return 1;
    }

    if(expect_true(ENT_DbWrite(db_handle,
                               "CREATE TEMP TABLE ent_pgsql_test_user(id SERIAL PRIMARY KEY, name TEXT NOT NULL);",
                               NULL,
                               NULL) == 0,
                   "ENT_DbWrite should create a PostgreSQL temp table") != 0)
    {
        ENT_DbCloseHandle(db_handle);
        ENT_DbClose();
        return 1;
    }

    if(expect_true(ENT_DbWrite(db_handle,
                               "INSERT INTO ent_pgsql_test_user(name) VALUES('carol');",
                               NULL,
                               NULL) == 0,
                   "ENT_DbWrite should insert a PostgreSQL row") != 0)
    {
        ENT_DbCloseHandle(db_handle);
        ENT_DbClose();
        return 1;
    }

    if(expect_true(ENT_DbRead(db_handle,
                              "SELECT name FROM ent_pgsql_test_user WHERE id = 1;",
                              capture_single_name_row,
                              &capture) == 0,
                   "ENT_DbRead should fetch the inserted PostgreSQL row") != 0)
    {
        ENT_DbCloseHandle(db_handle);
        ENT_DbClose();
        return 1;
    }

    if(expect_true(capture.called == 1,
                   "ENT_DbRead should invoke the PostgreSQL callback once") != 0)
    {
        ENT_DbCloseHandle(db_handle);
        ENT_DbClose();
        return 1;
    }

    if(expect_true(capture.column_num == 1,
                   "ENT_DbRead should return a single PostgreSQL column") != 0)
    {
        ENT_DbCloseHandle(db_handle);
        ENT_DbClose();
        return 1;
    }

    if(expect_true(strcmp(capture.name, "carol") == 0,
                   "ENT_DbRead should return the inserted PostgreSQL row contents") != 0)
    {
        ENT_DbCloseHandle(db_handle);
        ENT_DbClose();
        return 1;
    }

    if(expect_true(ENT_DbCloseHandle(db_handle) == 0,
                   "ENT_DbCloseHandle should close the PostgreSQL handle after the roundtrip") != 0)
    {
        ENT_DbClose();
        return 1;
    }

    return expect_true(ENT_DbClose() == 0,
                       "ENT_DbClose should close the DB service after the PostgreSQL roundtrip");
}
#endif

int main(void)
{
    if(test_db_init_handle_rejects_uninitialized_service() != 0)
    {
        return 1;
    }

    if(test_db_api_rejects_null_handles() != 0)
    {
        return 1;
    }

    if(test_db_close_rejects_live_handles() != 0)
    {
        return 1;
    }

    if(test_sqlite_open_rejects_missing_database_path() != 0)
    {
        return 1;
    }

    if(test_sqlite_open_is_idempotent() != 0)
    {
        return 1;
    }

    if(test_sqlite_write_and_read_roundtrip() != 0)
    {
        return 1;
    }

    if(test_sqlite_write_rejects_invalid_sql() != 0)
    {
        return 1;
    }

    if(test_sqlite_read_rejects_callback_without_user_data() != 0)
    {
        return 1;
    }

    if(test_pgsql_handle_lifecycle() != 0)
    {
        return 1;
    }

#if ENT_ENABLE_PGSQL
    if(test_pgsql_roundtrip_if_configured() != 0)
    {
        return 1;
    }
#endif

    return 0;
}
