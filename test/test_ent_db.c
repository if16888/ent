#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

#include "ient_comm.h"
#include "ient_runtime.h"
#include "ient_db.h"
#include "ent_db.h"
#include "ent_msg.h"

MSG_ID_T iENT_DbReInit(DB_HANDLE dbHandle,
                       DB_TYPE dbType,
                       const char* host,
                       const char* database,
                       const char* user,
                       const char* passwd,
                       int port);

ENT_CTX gEntCtx;

typedef struct DB_READ_CAPTURE {
    int called;
    long long row_num;
    int column_num;
    char name[32];
} DB_READ_CAPTURE;

typedef struct TEST_EVENT {
#ifdef WIN32
    CRITICAL_SECTION lock;
    CONDITION_VARIABLE cv;
#else
    pthread_mutex_t lock;
    pthread_cond_t cv;
#endif
    int signaled;
} TEST_EVENT;

typedef struct DB_CLOSE_WAIT_PROBE {
    TEST_EVENT callback_entered;
    TEST_EVENT callback_release;
    TEST_EVENT close_started;
    volatile int callback_calls;
    volatile int close_finished;
} DB_CLOSE_WAIT_PROBE;

typedef struct DB_READ_THREAD_CTX {
    DB_HANDLE db_handle;
    DB_CLOSE_WAIT_PROBE* probe;
    MSG_ID_T status;
} DB_READ_THREAD_CTX;

typedef struct DB_CLOSE_THREAD_CTX {
    DB_HANDLE db_handle;
    DB_CLOSE_WAIT_PROBE* probe;
    MSG_ID_T status;
} DB_CLOSE_THREAD_CTX;

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

static void test_sleep_ms(int ms)
{
#ifdef WIN32
    Sleep((DWORD)ms);
#else
    usleep((useconds_t)ms * 1000U);
#endif
}

static int test_event_init(TEST_EVENT* ev)
{
    if(ev == NULL)
    {
        return 1;
    }
#ifdef WIN32
    InitializeCriticalSection(&ev->lock);
    InitializeConditionVariable(&ev->cv);
#else
    if(pthread_mutex_init(&ev->lock, NULL) != 0)
    {
        return 1;
    }
    if(pthread_cond_init(&ev->cv, NULL) != 0)
    {
        pthread_mutex_destroy(&ev->lock);
        return 1;
    }
#endif
    ev->signaled = 0;
    return 0;
}

static void test_event_destroy(TEST_EVENT* ev)
{
    if(ev == NULL)
    {
        return;
    }
#ifdef WIN32
    DeleteCriticalSection(&ev->lock);
#else
    pthread_cond_destroy(&ev->cv);
    pthread_mutex_destroy(&ev->lock);
#endif
    ev->signaled = 0;
}

static void test_event_signal(TEST_EVENT* ev)
{
    if(ev == NULL)
    {
        return;
    }
#ifdef WIN32
    EnterCriticalSection(&ev->lock);
    ev->signaled = 1;
    WakeAllConditionVariable(&ev->cv);
    LeaveCriticalSection(&ev->lock);
#else
    pthread_mutex_lock(&ev->lock);
    ev->signaled = 1;
    pthread_cond_broadcast(&ev->cv);
    pthread_mutex_unlock(&ev->lock);
#endif
}

static void test_event_wait(TEST_EVENT* ev)
{
    if(ev == NULL)
    {
        return;
    }
#ifdef WIN32
    EnterCriticalSection(&ev->lock);
    while(!ev->signaled)
    {
        SleepConditionVariableCS(&ev->cv, &ev->lock, INFINITE);
    }
    LeaveCriticalSection(&ev->lock);
#else
    pthread_mutex_lock(&ev->lock);
    while(!ev->signaled)
    {
        pthread_cond_wait(&ev->cv, &ev->lock);
    }
    pthread_mutex_unlock(&ev->lock);
#endif
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

static void blocking_read_callback(char** fields, char** row_res, long long row_num, int column_num, void* user_data)
{
    DB_CLOSE_WAIT_PROBE* probe = (DB_CLOSE_WAIT_PROBE*)user_data;
    (void)fields;
    (void)row_res;
    (void)row_num;
    (void)column_num;
    if(probe == NULL)
    {
        return;
    }
    probe->callback_calls++;
    test_event_signal(&probe->callback_entered);
    test_event_wait(&probe->callback_release);
}

#ifdef WIN32
static DWORD WINAPI db_read_thread_proc(LPVOID data)
#else
static void* db_read_thread_proc(void* data)
#endif
{
    DB_READ_THREAD_CTX* ctx = (DB_READ_THREAD_CTX*)data;
    ctx->status = ENT_DbRead(ctx->db_handle,
                             "SELECT name FROM test_user WHERE id = 1;",
                             blocking_read_callback,
                             ctx->probe);
#ifdef WIN32
    return 0;
#else
    return NULL;
#endif
}

#ifdef WIN32
static DWORD WINAPI db_close_thread_proc(LPVOID data)
#else
static void* db_close_thread_proc(void* data)
#endif
{
    DB_CLOSE_THREAD_CTX* ctx = (DB_CLOSE_THREAD_CTX*)data;
    test_event_signal(&ctx->probe->close_started);
    ctx->status = ENT_DbCloseHandle(&ctx->db_handle);
    ctx->probe->close_finished = 1;
#ifdef WIN32
    return 0;
#else
    return NULL;
#endif
}

static int reset_db_service(void)
{
    ENT_DbClose();
    return ENT_DbInit();
}

static int read_env_or_null(const char* name, char* out, size_t out_len)
{
    char* value = ENT_GetEnvDup(name);

    if(out == NULL || out_len == 0)
    {
        free(value);
        return 0;
    }

    if(value == NULL)
    {
        out[0] = '\0';
        return 0;
    }

    if(strlen(value) + 1 > out_len)
    {
        free(value);
        out[0] = '\0';
        return 0;
    }

    memcpy(out, value, strlen(value) + 1);
    free(value);
    return (out[0] != '\0');
}

static int read_env_or_default_int(const char* name, int fallback)
{
    char* value = ENT_GetEnvDup(name);
    int result = fallback;

    if(value == NULL)
    {
        return fallback;
    }

    result = atoi(value);
    free(value);
    return result;
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
        ENT_DbCloseHandle(&db_handle);
        ENT_DbClose();
        cleanup_temp_db_path(db_path);
        return 1;
    }

    if(expect_true(ENT_DbCloseHandle(&db_handle) == 0,
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

static int test_sqlite_close_busy_preserves_handle_for_retry(void)
{
#if !ENT_ENABLE_SQLITE || !ENT_SQLITE_FOUND
    return 0;
#else
    char db_path[512];
    DB_HANDLE db_handle = NULL;
    DB_CFG* db_cfg = NULL;
    sqlite3_stmt* stmt = NULL;
    MSG_ID_T sts;
    int rc = 1;

    memset(db_path, 0, sizeof(db_path));
    if(prepare_temp_db_path(db_path, sizeof(db_path)) != 0)
    {
        return 1;
    }
    if(reset_db_service() != ENT_SYS_NORMAL ||
       ENT_DbInitHandle(&db_handle, SQLITE_TYPE, NULL, db_path, NULL, NULL, 0) != ENT_SYS_NORMAL ||
       ENT_DbOpen(db_handle) != ENT_SYS_NORMAL)
    {
        goto END_OF_ROUTINE;
    }

    db_cfg = (DB_CFG*)db_handle;
    if(sqlite3_prepare_v2(db_cfg->dbInstance.sqlite, "SELECT 1;", -1, &stmt, NULL) != SQLITE_OK)
    {
        goto END_OF_ROUTINE;
    }

    sts = ENT_DbCloseHandle(&db_handle);
    if(expect_true(sts == ENT_DBS_IN_USE,
                   "ENT_DbCloseHandle should report busy when sqlite has an unfinalized statement") != 0 ||
       expect_true(db_handle != NULL,
                   "ENT_DbCloseHandle should preserve the handle after a retryable close failure") != 0 ||
       expect_true(db_cfg->handleState == ENT_DB_HANDLE_ACTIVE_E && db_cfg->isInit,
                   "ENT_DbCloseHandle should restore active state after a retryable close failure") != 0)
    {
        goto END_OF_ROUTINE;
    }

    sqlite3_finalize(stmt);
    stmt = NULL;
    if(expect_true(ENT_DbCloseHandle(&db_handle) == ENT_SYS_NORMAL,
                   "ENT_DbCloseHandle should succeed after the sqlite statement is finalized") != 0 ||
       expect_true(db_handle == NULL,
                   "ENT_DbCloseHandle should clear the handle after retry succeeds") != 0)
    {
        goto END_OF_ROUTINE;
    }
    rc = 0;

END_OF_ROUTINE:
    if(stmt != NULL)
    {
        sqlite3_finalize(stmt);
    }
    if(db_handle != NULL)
    {
        ENT_DbCloseHandle(&db_handle);
    }
    ENT_DbClose();
    cleanup_temp_db_path(db_path);
    return rc;
#endif
}

static int test_db_close_handle_waits_for_active_read(void)
{
    char db_path[512];
    DB_HANDLE db_handle = NULL;
    DB_CLOSE_WAIT_PROBE probe;
    DB_READ_THREAD_CTX read_ctx;
    DB_CLOSE_THREAD_CTX close_ctx;
    DB_READ_CAPTURE capture;
    ENT_DB_PARAM read_param;
    ENT_DB_PARAM write_param;
    MSG_ID_T init_sts = 0;
    int rc = 0;

    memset(db_path, 0, sizeof(db_path));
    memset(&probe, 0, sizeof(probe));
    memset(&read_ctx, 0, sizeof(read_ctx));
    memset(&close_ctx, 0, sizeof(close_ctx));
    memset(&capture, 0, sizeof(capture));
    memset(&read_param, 0, sizeof(read_param));
    memset(&write_param, 0, sizeof(write_param));
    read_param.type = ENT_DB_PARAM_INT_E;
    read_param.value.i32 = 1;
    write_param.type = ENT_DB_PARAM_TEXT_E;
    write_param.value.text = "frank";

    if(prepare_temp_db_path(db_path, sizeof(db_path)) != 0)
    {
        return 1;
    }

    if(test_event_init(&probe.callback_entered) != 0 ||
       test_event_init(&probe.callback_release) != 0 ||
       test_event_init(&probe.close_started) != 0)
    {
        test_event_destroy(&probe.callback_entered);
        test_event_destroy(&probe.callback_release);
        test_event_destroy(&probe.close_started);
        cleanup_temp_db_path(db_path);
        return 1;
    }

    init_sts = reset_db_service();
    if(expect_true(init_sts == ENT_SYS_NORMAL || init_sts == ENT_SYS_ALREADY_INITIALIZED,
                   "ENT_DbInit should initialize the DB service before concurrent close testing") != 0)
    {
        rc = 1;
        goto CLEANUP;
    }

    if(expect_true(ENT_DbInitHandle(&db_handle, SQLITE_TYPE, NULL, db_path, NULL, NULL, 0) == 0,
                   "ENT_DbInitHandle should create a SQLite handle for concurrent close testing") != 0)
    {
        rc = 1;
        goto CLEANUP;
    }

    if(expect_true(ENT_DbWrite(db_handle, "CREATE TABLE test_user(id INTEGER PRIMARY KEY, name TEXT NOT NULL);", NULL, NULL) == 0,
                   "ENT_DbWrite should create a SQLite table for concurrent close testing") != 0)
    {
        rc = 1;
        goto HANDLE_CLEANUP;
    }

    if(expect_true(ENT_DbWrite(db_handle, "INSERT INTO test_user(name) VALUES('eve');", NULL, NULL) == 0,
                   "ENT_DbWrite should insert a SQLite row for concurrent close testing") != 0)
    {
        rc = 1;
        goto HANDLE_CLEANUP;
    }

    read_ctx.db_handle = db_handle;
    read_ctx.probe = &probe;
    read_ctx.status = ENT_DBS_RESULT_FAILED;

    close_ctx.db_handle = db_handle;
    close_ctx.probe = &probe;
    close_ctx.status = ENT_DBS_RESULT_FAILED;

#ifdef WIN32
    {
        HANDLE read_thread = CreateThread(NULL, 0, db_read_thread_proc, &read_ctx, 0, NULL);
        HANDLE close_thread = NULL;
        if(expect_true(read_thread != NULL,
                       "read helper thread should start for concurrent close testing") != 0)
        {
            rc = 1;
            goto HANDLE_CLEANUP;
        }

        test_event_wait(&probe.callback_entered);

        close_thread = CreateThread(NULL, 0, db_close_thread_proc, &close_ctx, 0, NULL);
        if(expect_true(close_thread != NULL,
                       "close helper thread should start for concurrent close testing") != 0)
        {
            test_event_signal(&probe.callback_release);
            WaitForSingleObject(read_thread, INFINITE);
            CloseHandle(read_thread);
            rc = 1;
            goto HANDLE_CLEANUP;
        }

        test_event_wait(&probe.close_started);
        test_sleep_ms(50);
        if(expect_true(probe.close_finished == 0,
                       "ENT_DbCloseHandle should wait while an active read callback is still running") != 0)
        {
            test_event_signal(&probe.callback_release);
            WaitForSingleObject(read_thread, INFINITE);
            WaitForSingleObject(close_thread, INFINITE);
            CloseHandle(read_thread);
            CloseHandle(close_thread);
            rc = 1;
            goto CLEANUP;
        }
        if(expect_true(ENT_DbOpen(db_handle) == ENT_DBS_IN_USE,
                       "ENT_DbOpen should reject a handle that is already closing") != 0)
        {
            test_event_signal(&probe.callback_release);
            WaitForSingleObject(read_thread, INFINITE);
            WaitForSingleObject(close_thread, INFINITE);
            CloseHandle(read_thread);
            CloseHandle(close_thread);
            rc = 1;
            goto CLEANUP;
        }
        if(expect_true(ENT_DbRead(db_handle,
                                  "SELECT name FROM test_user WHERE id = 1;",
                                  capture_single_name_row,
                                  &capture) == ENT_DBS_IN_USE,
                       "ENT_DbRead should reject a handle that is already closing") != 0)
        {
            test_event_signal(&probe.callback_release);
            WaitForSingleObject(read_thread, INFINITE);
            WaitForSingleObject(close_thread, INFINITE);
            CloseHandle(read_thread);
            CloseHandle(close_thread);
            rc = 1;
            goto CLEANUP;
        }
        if(expect_true(ENT_DbReadParams(db_handle,
                                        "SELECT name FROM test_user WHERE id = ?;",
                                        &read_param,
                                        1,
                                        capture_single_name_row,
                                        &capture) == ENT_DBS_IN_USE,
                       "ENT_DbReadParams should reject a handle that is already closing") != 0)
        {
            test_event_signal(&probe.callback_release);
            WaitForSingleObject(read_thread, INFINITE);
            WaitForSingleObject(close_thread, INFINITE);
            CloseHandle(read_thread);
            CloseHandle(close_thread);
            rc = 1;
            goto CLEANUP;
        }
        if(expect_true(ENT_DbWrite(db_handle,
                                   "INSERT INTO test_user(name) VALUES('frank');",
                                   NULL,
                                   NULL) == ENT_DBS_IN_USE,
                       "ENT_DbWrite should reject a handle that is already closing") != 0)
        {
            test_event_signal(&probe.callback_release);
            WaitForSingleObject(read_thread, INFINITE);
            WaitForSingleObject(close_thread, INFINITE);
            CloseHandle(read_thread);
            CloseHandle(close_thread);
            rc = 1;
            goto CLEANUP;
        }
        if(expect_true(ENT_DbWriteParams(db_handle,
                                         "INSERT INTO test_user(name) VALUES(?);",
                                         &write_param,
                                         1,
                                         NULL,
                                         NULL) == ENT_DBS_IN_USE,
                       "ENT_DbWriteParams should reject a handle that is already closing") != 0)
        {
            test_event_signal(&probe.callback_release);
            WaitForSingleObject(read_thread, INFINITE);
            WaitForSingleObject(close_thread, INFINITE);
            CloseHandle(read_thread);
            CloseHandle(close_thread);
            rc = 1;
            goto CLEANUP;
        }
        if(expect_true(iENT_DbReInit(db_handle, SQLITE_TYPE, NULL, db_path, NULL, NULL, 0) == ENT_DBS_IN_USE,
                       "iENT_DbReInit should reject a handle that is already closing") != 0)
        {
            test_event_signal(&probe.callback_release);
            WaitForSingleObject(read_thread, INFINITE);
            WaitForSingleObject(close_thread, INFINITE);
            CloseHandle(read_thread);
            CloseHandle(close_thread);
            rc = 1;
            goto CLEANUP;
        }
        if(expect_true(ENT_DbClose() == ENT_DBS_IN_USE,
                       "ENT_DbClose should reject the DB service while a live handle is closing") != 0)
        {
            test_event_signal(&probe.callback_release);
            WaitForSingleObject(read_thread, INFINITE);
            WaitForSingleObject(close_thread, INFINITE);
            CloseHandle(read_thread);
            CloseHandle(close_thread);
            rc = 1;
            goto CLEANUP;
        }
        if(expect_true(ENT_DbCloseHandle(&db_handle) == ENT_DBS_IN_USE,
                       "A second ENT_DbCloseHandle should report the handle as already closing") != 0)
        {
            test_event_signal(&probe.callback_release);
            WaitForSingleObject(read_thread, INFINITE);
            WaitForSingleObject(close_thread, INFINITE);
            CloseHandle(read_thread);
            CloseHandle(close_thread);
            rc = 1;
            goto CLEANUP;
        }

        test_event_signal(&probe.callback_release);
        WaitForSingleObject(read_thread, INFINITE);
        WaitForSingleObject(close_thread, INFINITE);
        CloseHandle(read_thread);
        CloseHandle(close_thread);
    }
#else
    {
        pthread_t read_thread;
        pthread_t close_thread;
        if(expect_true(pthread_create(&read_thread, NULL, db_read_thread_proc, &read_ctx) == 0,
                       "read helper thread should start for concurrent close testing") != 0)
        {
            rc = 1;
            goto HANDLE_CLEANUP;
        }

        test_event_wait(&probe.callback_entered);

        if(expect_true(pthread_create(&close_thread, NULL, db_close_thread_proc, &close_ctx) == 0,
                       "close helper thread should start for concurrent close testing") != 0)
        {
            test_event_signal(&probe.callback_release);
            pthread_join(read_thread, NULL);
            rc = 1;
            goto HANDLE_CLEANUP;
        }

        test_event_wait(&probe.close_started);
        test_sleep_ms(50);
        if(expect_true(probe.close_finished == 0,
                       "ENT_DbCloseHandle should wait while an active read callback is still running") != 0)
        {
            test_event_signal(&probe.callback_release);
            pthread_join(read_thread, NULL);
            pthread_join(close_thread, NULL);
            rc = 1;
            goto CLEANUP;
        }
        if(expect_true(ENT_DbOpen(db_handle) == ENT_DBS_IN_USE,
                       "ENT_DbOpen should reject a handle that is already closing") != 0)
        {
            test_event_signal(&probe.callback_release);
            pthread_join(read_thread, NULL);
            pthread_join(close_thread, NULL);
            rc = 1;
            goto CLEANUP;
        }
        if(expect_true(ENT_DbRead(db_handle,
                                  "SELECT name FROM test_user WHERE id = 1;",
                                  capture_single_name_row,
                                  &capture) == ENT_DBS_IN_USE,
                       "ENT_DbRead should reject a handle that is already closing") != 0)
        {
            test_event_signal(&probe.callback_release);
            pthread_join(read_thread, NULL);
            pthread_join(close_thread, NULL);
            rc = 1;
            goto CLEANUP;
        }
        if(expect_true(ENT_DbReadParams(db_handle,
                                        "SELECT name FROM test_user WHERE id = ?;",
                                        &read_param,
                                        1,
                                        capture_single_name_row,
                                        &capture) == ENT_DBS_IN_USE,
                       "ENT_DbReadParams should reject a handle that is already closing") != 0)
        {
            test_event_signal(&probe.callback_release);
            pthread_join(read_thread, NULL);
            pthread_join(close_thread, NULL);
            rc = 1;
            goto CLEANUP;
        }
        if(expect_true(ENT_DbWrite(db_handle,
                                   "INSERT INTO test_user(name) VALUES('frank');",
                                   NULL,
                                   NULL) == ENT_DBS_IN_USE,
                       "ENT_DbWrite should reject a handle that is already closing") != 0)
        {
            test_event_signal(&probe.callback_release);
            pthread_join(read_thread, NULL);
            pthread_join(close_thread, NULL);
            rc = 1;
            goto CLEANUP;
        }
        if(expect_true(ENT_DbWriteParams(db_handle,
                                         "INSERT INTO test_user(name) VALUES(?);",
                                         &write_param,
                                         1,
                                         NULL,
                                         NULL) == ENT_DBS_IN_USE,
                       "ENT_DbWriteParams should reject a handle that is already closing") != 0)
        {
            test_event_signal(&probe.callback_release);
            pthread_join(read_thread, NULL);
            pthread_join(close_thread, NULL);
            rc = 1;
            goto CLEANUP;
        }
        if(expect_true(iENT_DbReInit(db_handle, SQLITE_TYPE, NULL, db_path, NULL, NULL, 0) == ENT_DBS_IN_USE,
                       "iENT_DbReInit should reject a handle that is already closing") != 0)
        {
            test_event_signal(&probe.callback_release);
            pthread_join(read_thread, NULL);
            pthread_join(close_thread, NULL);
            rc = 1;
            goto CLEANUP;
        }
        if(expect_true(ENT_DbClose() == ENT_DBS_IN_USE,
                       "ENT_DbClose should reject the DB service while a live handle is closing") != 0)
        {
            test_event_signal(&probe.callback_release);
            pthread_join(read_thread, NULL);
            pthread_join(close_thread, NULL);
            rc = 1;
            goto CLEANUP;
        }
        if(expect_true(ENT_DbCloseHandle(&db_handle) == ENT_DBS_IN_USE,
                       "A second ENT_DbCloseHandle should report the handle as already closing") != 0)
        {
            test_event_signal(&probe.callback_release);
            pthread_join(read_thread, NULL);
            pthread_join(close_thread, NULL);
            rc = 1;
            goto CLEANUP;
        }

        test_event_signal(&probe.callback_release);
        pthread_join(read_thread, NULL);
        pthread_join(close_thread, NULL);
    }
#endif

    if(expect_true(read_ctx.status == ENT_SYS_NORMAL,
                   "ENT_DbRead should complete successfully after the blocked callback is released") != 0)
    {
        rc = 1;
        goto CLEANUP;
    }

    if(expect_true(close_ctx.status == ENT_SYS_NORMAL,
                   "ENT_DbCloseHandle should complete successfully after the active read finishes") != 0)
    {
        rc = 1;
        goto CLEANUP;
    }

    if(expect_true(probe.callback_calls == 1,
                   "blocking read callback should run exactly once during concurrent close testing") != 0)
    {
        rc = 1;
        goto CLEANUP;
    }

    cleanup_temp_db_path(db_path);
    if(expect_true(ENT_DbClose() == 0,
                   "ENT_DbClose should succeed after concurrent close testing") != 0)
    {
        rc = 1;
        goto CLEANUP_NO_DB;
    }

    test_event_destroy(&probe.callback_entered);
    test_event_destroy(&probe.callback_release);
    test_event_destroy(&probe.close_started);
    return 0;

HANDLE_CLEANUP:
    if(db_handle != NULL)
    {
        ENT_DbCloseHandle(&db_handle);
    }
CLEANUP:
    test_event_signal(&probe.callback_release);
    cleanup_temp_db_path(db_path);
    ENT_DbClose();
CLEANUP_NO_DB:
    test_event_destroy(&probe.callback_entered);
    test_event_destroy(&probe.callback_release);
    test_event_destroy(&probe.close_started);
    return rc;
}

static int test_db_reinit_rejects_active_read(void)
{
    char db_path[512];
    DB_HANDLE db_handle = NULL;
    DB_CLOSE_WAIT_PROBE probe;
    DB_READ_THREAD_CTX read_ctx;
    MSG_ID_T init_sts = 0;
    int rc = 0;

    memset(db_path, 0, sizeof(db_path));
    memset(&probe, 0, sizeof(probe));
    memset(&read_ctx, 0, sizeof(read_ctx));

    if(prepare_temp_db_path(db_path, sizeof(db_path)) != 0)
    {
        return 1;
    }

    if(test_event_init(&probe.callback_entered) != 0 ||
       test_event_init(&probe.callback_release) != 0 ||
       test_event_init(&probe.close_started) != 0)
    {
        test_event_destroy(&probe.callback_entered);
        test_event_destroy(&probe.callback_release);
        test_event_destroy(&probe.close_started);
        cleanup_temp_db_path(db_path);
        return 1;
    }

    init_sts = reset_db_service();
    if(expect_true(init_sts == ENT_SYS_NORMAL || init_sts == ENT_SYS_ALREADY_INITIALIZED,
                   "ENT_DbInit should initialize the DB service before concurrent reinit testing") != 0)
    {
        rc = 1;
        goto CLEANUP;
    }

    if(expect_true(ENT_DbInitHandle(&db_handle, SQLITE_TYPE, NULL, db_path, NULL, NULL, 0) == 0,
                   "ENT_DbInitHandle should create a SQLite handle for concurrent reinit testing") != 0)
    {
        rc = 1;
        goto CLEANUP;
    }

    if(expect_true(ENT_DbWrite(db_handle, "CREATE TABLE test_user(id INTEGER PRIMARY KEY, name TEXT NOT NULL);", NULL, NULL) == 0,
                   "ENT_DbWrite should create a SQLite table for concurrent reinit testing") != 0)
    {
        rc = 1;
        goto HANDLE_CLEANUP;
    }

    if(expect_true(ENT_DbWrite(db_handle, "INSERT INTO test_user(name) VALUES('mallory');", NULL, NULL) == 0,
                   "ENT_DbWrite should insert a SQLite row for concurrent reinit testing") != 0)
    {
        rc = 1;
        goto HANDLE_CLEANUP;
    }

    read_ctx.db_handle = db_handle;
    read_ctx.probe = &probe;
    read_ctx.status = ENT_DBS_RESULT_FAILED;

#ifdef WIN32
    {
        HANDLE read_thread = CreateThread(NULL, 0, db_read_thread_proc, &read_ctx, 0, NULL);
        if(expect_true(read_thread != NULL,
                       "read helper thread should start for concurrent reinit testing") != 0)
        {
            rc = 1;
            goto HANDLE_CLEANUP;
        }

        test_event_wait(&probe.callback_entered);

        if(expect_true(iENT_DbReInit(db_handle, SQLITE_TYPE, NULL, db_path, NULL, NULL, 0) == ENT_DBS_IN_USE,
                       "iENT_DbReInit should reject a handle that is busy with an active read") != 0)
        {
            test_event_signal(&probe.callback_release);
            WaitForSingleObject(read_thread, INFINITE);
            CloseHandle(read_thread);
            rc = 1;
            goto HANDLE_CLEANUP;
        }

        test_event_signal(&probe.callback_release);
        WaitForSingleObject(read_thread, INFINITE);
        CloseHandle(read_thread);
    }
#else
    {
        pthread_t read_thread;
        if(expect_true(pthread_create(&read_thread, NULL, db_read_thread_proc, &read_ctx) == 0,
                       "read helper thread should start for concurrent reinit testing") != 0)
        {
            rc = 1;
            goto HANDLE_CLEANUP;
        }

        test_event_wait(&probe.callback_entered);

        if(expect_true(iENT_DbReInit(db_handle, SQLITE_TYPE, NULL, db_path, NULL, NULL, 0) == ENT_DBS_IN_USE,
                       "iENT_DbReInit should reject a handle that is busy with an active read") != 0)
        {
            test_event_signal(&probe.callback_release);
            pthread_join(read_thread, NULL);
            rc = 1;
            goto HANDLE_CLEANUP;
        }

        test_event_signal(&probe.callback_release);
        pthread_join(read_thread, NULL);
    }
#endif

    if(expect_true(read_ctx.status == ENT_SYS_NORMAL,
                   "ENT_DbRead should complete successfully after reinit rejection testing") != 0)
    {
        rc = 1;
        goto HANDLE_CLEANUP;
    }

    if(expect_true(probe.callback_calls == 1,
                   "blocking read callback should run exactly once during concurrent reinit testing") != 0)
    {
        rc = 1;
        goto HANDLE_CLEANUP;
    }

HANDLE_CLEANUP:
    if(db_handle != NULL)
    {
        if(expect_true(ENT_DbCloseHandle(&db_handle) == 0,
                       "ENT_DbCloseHandle should close the SQLite handle after concurrent reinit testing") != 0)
        {
            rc = 1;
        }
    }
CLEANUP:
    test_event_signal(&probe.callback_release);
    cleanup_temp_db_path(db_path);
    if(expect_true(ENT_DbClose() == 0,
                   "ENT_DbClose should close the DB service after concurrent reinit testing") != 0)
    {
        rc = 1;
    }
    test_event_destroy(&probe.callback_entered);
    test_event_destroy(&probe.callback_release);
    test_event_destroy(&probe.close_started);
    return rc;
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
        ENT_DbCloseHandle(&db_handle);
        ENT_DbClose();
        return 1;
    }

    if(expect_true(ENT_DbCloseHandle(&db_handle) == 0,
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
        ENT_DbCloseHandle(&db_handle);
        ENT_DbClose();
        cleanup_temp_db_path(db_path);
        return 1;
    }

    {
        MSG_ID_T reopen_sts = ENT_DbOpen(db_handle);
        if(expect_true(reopen_sts == 0,
                   "ENT_DbOpen should treat an already-open SQLite handle as success") != 0)
        {
            ENT_DbCloseHandle(&db_handle);
            ENT_DbClose();
            cleanup_temp_db_path(db_path);
            return 1;
        }
    }

    if(expect_true(ENT_DbCloseHandle(&db_handle) == 0,
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
        ENT_DbCloseHandle(&db_handle);
        ENT_DbClose();
        cleanup_temp_db_path(db_path);
        return 1;
    }

    if(expect_true(ENT_DbWrite(db_handle, "INSERT INTO test_user(name) VALUES('alice');", NULL, NULL) == 0,
                   "ENT_DbWrite should insert a SQLite row") != 0)
    {
        ENT_DbCloseHandle(&db_handle);
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
        ENT_DbCloseHandle(&db_handle);
        ENT_DbClose();
        cleanup_temp_db_path(db_path);
        return 1;
    }

    if(expect_true(capture.called == 1, "ENT_DbRead should invoke the SQLite callback once") != 0)
    {
        ENT_DbCloseHandle(&db_handle);
        ENT_DbClose();
        cleanup_temp_db_path(db_path);
        return 1;
    }

    if(expect_true(capture.column_num == 1, "ENT_DbRead should return one selected column") != 0)
    {
        ENT_DbCloseHandle(&db_handle);
        ENT_DbClose();
        cleanup_temp_db_path(db_path);
        return 1;
    }

    if(expect_true(strcmp(capture.name, "alice") == 0, "ENT_DbRead should return the inserted row contents") != 0)
    {
        ENT_DbCloseHandle(&db_handle);
        ENT_DbClose();
        cleanup_temp_db_path(db_path);
        return 1;
    }

    if(expect_true(ENT_DbCloseHandle(&db_handle) == 0,
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
        ENT_DbCloseHandle(&db_handle);
        ENT_DbClose();
        cleanup_temp_db_path(db_path);
        return 1;
    }

    if(expect_true(ENT_DbCloseHandle(&db_handle) == 0,
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
        ENT_DbCloseHandle(&db_handle);
        ENT_DbClose();
        cleanup_temp_db_path(db_path);
        return 1;
    }

    if(expect_true(ENT_DbWrite(db_handle, "INSERT INTO test_user(name) VALUES('bob');", NULL, NULL) == 0,
                   "ENT_DbWrite should insert a SQLite row for callback validation") != 0)
    {
        ENT_DbCloseHandle(&db_handle);
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
        ENT_DbCloseHandle(&db_handle);
        ENT_DbClose();
        cleanup_temp_db_path(db_path);
        return 1;
    }

    if(expect_true(capture.called == 0,
                   "ENT_DbRead should not populate caller state when SQLite aborts the callback") != 0)
    {
        ENT_DbCloseHandle(&db_handle);
        ENT_DbClose();
        cleanup_temp_db_path(db_path);
        return 1;
    }

    if(expect_true(ENT_DbCloseHandle(&db_handle) == 0,
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

    sts = ENT_DbCloseHandle(&db_handle);
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
    char host[256];
    char database[256];
    char user[256];
    char passwd[256];
    int port;

    host[0] = '\0';
    database[0] = '\0';
    user[0] = '\0';
    passwd[0] = '\0';
    read_env_or_null("ENT_PGSQL_HOST", host, sizeof(host));
    read_env_or_null("ENT_PGSQL_DB", database, sizeof(database));
    read_env_or_null("ENT_PGSQL_USER", user, sizeof(user));
    read_env_or_null("ENT_PGSQL_PASSWORD", passwd, sizeof(passwd));
    port = read_env_or_default_int("ENT_PGSQL_PORT", 5432);

    if(host[0] == '\0' || database[0] == '\0' || user[0] == '\0')
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
        ENT_DbCloseHandle(&db_handle);
        ENT_DbClose();
        return 1;
    }

    if(expect_true(ENT_DbWrite(db_handle,
                               "CREATE TEMP TABLE ent_pgsql_test_user(id SERIAL PRIMARY KEY, name TEXT NOT NULL);",
                               NULL,
                               NULL) == 0,
                   "ENT_DbWrite should create a PostgreSQL temp table") != 0)
    {
        ENT_DbCloseHandle(&db_handle);
        ENT_DbClose();
        return 1;
    }

    if(expect_true(ENT_DbWrite(db_handle,
                               "INSERT INTO ent_pgsql_test_user(name) VALUES('carol');",
                               NULL,
                               NULL) == 0,
                   "ENT_DbWrite should insert a PostgreSQL row") != 0)
    {
        ENT_DbCloseHandle(&db_handle);
        ENT_DbClose();
        return 1;
    }

    if(expect_true(ENT_DbRead(db_handle,
                              "SELECT name FROM ent_pgsql_test_user WHERE id = 1;",
                              capture_single_name_row,
                              &capture) == 0,
                   "ENT_DbRead should fetch the inserted PostgreSQL row") != 0)
    {
        ENT_DbCloseHandle(&db_handle);
        ENT_DbClose();
        return 1;
    }

    if(expect_true(capture.called == 1,
                   "ENT_DbRead should invoke the PostgreSQL callback once") != 0)
    {
        ENT_DbCloseHandle(&db_handle);
        ENT_DbClose();
        return 1;
    }

    if(expect_true(capture.column_num == 1,
                   "ENT_DbRead should return a single PostgreSQL column") != 0)
    {
        ENT_DbCloseHandle(&db_handle);
        ENT_DbClose();
        return 1;
    }

    if(expect_true(strcmp(capture.name, "carol") == 0,
                   "ENT_DbRead should return the inserted PostgreSQL row contents") != 0)
    {
        ENT_DbCloseHandle(&db_handle);
        ENT_DbClose();
        return 1;
    }

    if(expect_true(ENT_DbCloseHandle(&db_handle) == 0,
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

    if(test_sqlite_close_busy_preserves_handle_for_retry() != 0)
    {
        return 1;
    }

    if(test_db_close_handle_waits_for_active_read() != 0)
    {
        return 1;
    }

    if(test_db_reinit_rejects_active_read() != 0)
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
