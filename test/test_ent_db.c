#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ient_comm.h"
#include "ent_db.h"

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

static int test_db_init_handle_rejects_uninitialized_service(void)
{
    DB_HANDLE db_handle = NULL;

    ENT_DbClose();

    return expect_true(ENT_DbInitHandle(&db_handle, SQLITE_TYPE, NULL, "ignored.db", NULL, NULL, 0) == -1,
                       "ENT_DbInitHandle should reject calls before ENT_DbInit");
}

static int test_sqlite_open_rejects_missing_database_path(void)
{
    DB_HANDLE db_handle = NULL;
    MSG_ID_T init_sts = 0;

    init_sts = reset_db_service();
    if(expect_true(init_sts == 0 || init_sts == 1,
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

    if(expect_true(ENT_DbOpen(db_handle) == -2,
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

static int test_sqlite_write_and_read_roundtrip(void)
{
    char db_template[] = "/tmp/ent_db_test_XXXXXX.sqlite";
    DB_HANDLE db_handle = NULL;
    DB_READ_CAPTURE capture;
    int fd = -1;
    MSG_ID_T init_sts = 0;

    memset(&capture, 0, sizeof(capture));

    fd = mkstemps(db_template, 7);
    if(fd < 0)
    {
        perror("mkstemps");
        return 1;
    }
    close(fd);
    unlink(db_template);

    init_sts = reset_db_service();
    if(expect_true(init_sts == 0 || init_sts == 1,
                   "ENT_DbInit should initialize the DB service") != 0)
    {
        return 1;
    }

    if(expect_true(ENT_DbInitHandle(&db_handle, SQLITE_TYPE, NULL, db_template, NULL, NULL, 0) == 0,
                   "ENT_DbInitHandle should create a SQLite handle") != 0)
    {
        ENT_DbClose();
        unlink(db_template);
        return 1;
    }

    if(expect_true(ENT_DbWrite(db_handle, "CREATE TABLE test_user(id INTEGER PRIMARY KEY, name TEXT NOT NULL);", NULL, NULL) == 0,
                   "ENT_DbWrite should create a SQLite table") != 0)
    {
        ENT_DbCloseHandle(db_handle);
        ENT_DbClose();
        unlink(db_template);
        return 1;
    }

    if(expect_true(ENT_DbWrite(db_handle, "INSERT INTO test_user(name) VALUES('alice');", NULL, NULL) == 0,
                   "ENT_DbWrite should insert a SQLite row") != 0)
    {
        ENT_DbCloseHandle(db_handle);
        ENT_DbClose();
        unlink(db_template);
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
        unlink(db_template);
        return 1;
    }

    if(expect_true(capture.called == 1, "ENT_DbRead should invoke the SQLite callback once") != 0)
    {
        ENT_DbCloseHandle(db_handle);
        ENT_DbClose();
        unlink(db_template);
        return 1;
    }

    if(expect_true(capture.column_num == 1, "ENT_DbRead should return one selected column") != 0)
    {
        ENT_DbCloseHandle(db_handle);
        ENT_DbClose();
        unlink(db_template);
        return 1;
    }

    if(expect_true(strcmp(capture.name, "alice") == 0, "ENT_DbRead should return the inserted row contents") != 0)
    {
        ENT_DbCloseHandle(db_handle);
        ENT_DbClose();
        unlink(db_template);
        return 1;
    }

    if(expect_true(ENT_DbCloseHandle(db_handle) == 0,
                   "ENT_DbCloseHandle should close an open SQLite handle") != 0)
    {
        ENT_DbClose();
        unlink(db_template);
        return 1;
    }

    unlink(db_template);
    return expect_true(ENT_DbClose() == 0,
                       "ENT_DbClose should close the DB service after the SQLite roundtrip");
}

static int test_sqlite_write_rejects_invalid_sql(void)
{
    char db_template[] = "/tmp/ent_db_test_XXXXXX.sqlite";
    DB_HANDLE db_handle = NULL;
    int fd = -1;
    MSG_ID_T init_sts = 0;

    fd = mkstemps(db_template, 7);
    if(fd < 0)
    {
        perror("mkstemps");
        return 1;
    }
    close(fd);
    unlink(db_template);

    init_sts = reset_db_service();
    if(expect_true(init_sts == 0 || init_sts == 1,
                   "ENT_DbInit should initialize the DB service") != 0)
    {
        return 1;
    }

    if(expect_true(ENT_DbInitHandle(&db_handle, SQLITE_TYPE, NULL, db_template, NULL, NULL, 0) == 0,
                   "ENT_DbInitHandle should create a SQLite handle for invalid SQL testing") != 0)
    {
        ENT_DbClose();
        unlink(db_template);
        return 1;
    }

    if(expect_true(ENT_DbWrite(db_handle, "THIS IS NOT SQL", NULL, NULL) == -2,
                   "ENT_DbWrite should return -2 when SQLite rejects invalid SQL") != 0)
    {
        ENT_DbCloseHandle(db_handle);
        ENT_DbClose();
        unlink(db_template);
        return 1;
    }

    if(expect_true(ENT_DbCloseHandle(db_handle) == 0,
                   "ENT_DbCloseHandle should close the SQLite handle after invalid SQL") != 0)
    {
        ENT_DbClose();
        unlink(db_template);
        return 1;
    }

    unlink(db_template);
    return expect_true(ENT_DbClose() == 0,
                       "ENT_DbClose should close the DB service after the invalid SQL test");
}

static int test_sqlite_read_rejects_callback_without_user_data(void)
{
    char db_template[] = "/tmp/ent_db_test_XXXXXX.sqlite";
    DB_HANDLE db_handle = NULL;
    DB_READ_CAPTURE capture;
    int fd = -1;
    MSG_ID_T init_sts = 0;

    memset(&capture, 0, sizeof(capture));

    fd = mkstemps(db_template, 7);
    if(fd < 0)
    {
        perror("mkstemps");
        return 1;
    }
    close(fd);
    unlink(db_template);

    init_sts = reset_db_service();
    if(expect_true(init_sts == 0 || init_sts == 1,
                   "ENT_DbInit should initialize the DB service") != 0)
    {
        return 1;
    }

    if(expect_true(ENT_DbInitHandle(&db_handle, SQLITE_TYPE, NULL, db_template, NULL, NULL, 0) == 0,
                   "ENT_DbInitHandle should create a SQLite handle for callback validation") != 0)
    {
        ENT_DbClose();
        unlink(db_template);
        return 1;
    }

    if(expect_true(ENT_DbWrite(db_handle, "CREATE TABLE test_user(id INTEGER PRIMARY KEY, name TEXT NOT NULL);", NULL, NULL) == 0,
                   "ENT_DbWrite should create a SQLite table for callback validation") != 0)
    {
        ENT_DbCloseHandle(db_handle);
        ENT_DbClose();
        unlink(db_template);
        return 1;
    }

    if(expect_true(ENT_DbWrite(db_handle, "INSERT INTO test_user(name) VALUES('bob');", NULL, NULL) == 0,
                   "ENT_DbWrite should insert a SQLite row for callback validation") != 0)
    {
        ENT_DbCloseHandle(db_handle);
        ENT_DbClose();
        unlink(db_template);
        return 1;
    }

    if(expect_true(ENT_DbRead(db_handle,
                              "SELECT name FROM test_user WHERE id = 1;",
                              capture_single_name_row,
                              NULL) == -2,
                   "ENT_DbRead should fail when a callback is provided without user data") != 0)
    {
        ENT_DbCloseHandle(db_handle);
        ENT_DbClose();
        unlink(db_template);
        return 1;
    }

    if(expect_true(capture.called == 0,
                   "ENT_DbRead should not populate caller state when SQLite aborts the callback") != 0)
    {
        ENT_DbCloseHandle(db_handle);
        ENT_DbClose();
        unlink(db_template);
        return 1;
    }

    if(expect_true(ENT_DbCloseHandle(db_handle) == 0,
                   "ENT_DbCloseHandle should close the SQLite handle after callback validation") != 0)
    {
        ENT_DbClose();
        unlink(db_template);
        return 1;
    }

    unlink(db_template);
    return expect_true(ENT_DbClose() == 0,
                       "ENT_DbClose should close the DB service after the callback validation test");
}

int main(void)
{
    if(test_db_init_handle_rejects_uninitialized_service() != 0)
    {
        return 1;
    }

    if(test_sqlite_open_rejects_missing_database_path() != 0)
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

    return 0;
}
