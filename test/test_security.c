/*
 * test_security.c — 手工安全审计工具
 *
 * 覆盖：缓冲区越界、SQL 注入、输入校验
 *
 * 说明：
 * 1. 这个文件用于记录当前安全行为，不应伪装成“全部通过”的回归测试。
 * 2. ASSERT_* 失败表示审计工具自身或基础前置条件失败。
 * 3. FINDING 表示发现了需要后续修复的安全风险。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "ient_comm.h"
#include "ient_runtime.h"
#include "ent_init.h"
#include "ent_db.h"
#include "ent_msg.h"

/* Security checks compile against the split log sources through test CMake wiring. */

/* ── 测试宏 ────────────────────────────────────────────── */
static int s_assert_failures = 0;
static int s_findings = 0;
static int s_checks = 0;

#define TEST_BEGIN(name) \
    do { \
        s_checks++; \
        const char* _test_name = (name); \
        fprintf(stdout, "  [RUN ] %s\n", _test_name);

#define TEST_END() \
        fprintf(stdout, "  [DONE] %s\n", _test_name); \
    } while(0)

#define ASSERT_TRUE(cond, msg) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "  [FAIL] %s — %s (line %d)\n", _test_name, msg, __LINE__); \
            s_assert_failures++; \
            return; \
        } \
    } while(0)

#define ASSERT_EQ(expected, actual, msg) \
    do { \
        long _e = (long)(expected), _a = (long)(actual); \
        if (_e != _a) { \
            fprintf(stderr, "  [FAIL] %s — %s (expected %ld, got %ld, line %d)\n", \
                    _test_name, msg, _e, _a, __LINE__); \
            s_assert_failures++; \
            return; \
        } \
    } while(0)

#define ASSERT_NE(not_expected, actual, msg) \
    do { \
        long _ne = (long)(not_expected), _a = (long)(actual); \
        if (_ne == _a) { \
            fprintf(stderr, "  [FAIL] %s — %s (got %ld, line %d)\n", \
                    _test_name, msg, _a, __LINE__); \
            s_assert_failures++; \
            return; \
        } \
    } while(0)

#define RECORD_FINDING(name, msg) \
    do { \
        s_findings++; \
        fprintf(stdout, "    -> [FINDING] %s: %s\n", (name), (msg)); \
    } while(0)

typedef struct {
    int    called;
    int    column_num;
    long long row_num;
    char   leaked_data[256];
} INJECT_CAPTURE;

static void inject_capture_cb(char** fields, char** rowRes,
                               long long rowNum, int columnNum, void* userData)
{
    INJECT_CAPTURE* cap = (INJECT_CAPTURE*)userData;
    if (cap == NULL) return;
    (void)fields;
    cap->called = 1;
    cap->column_num = columnNum;
    cap->row_num = rowNum;
    if (rowRes && rowNum > 0 && columnNum > 0 && rowRes[0]) {
        size_t copy_len = strlen(rowRes[0]);
        if(copy_len >= sizeof(cap->leaked_data))
        {
            copy_len = sizeof(cap->leaked_data) - 1;
        }
        memcpy(cap->leaked_data, rowRes[0], copy_len);
        cap->leaked_data[copy_len] = '\0';
    }
}

/* ── 辅助：安全清理 ENT_Init 遗留状态 ──────────────────── */
static void safe_reset_ent(void)
{
    memset(&gEntCtx, 0, sizeof(gEntCtx));
}

static int get_temp_root(char* path, size_t path_len)
{
#ifdef _WIN32
    DWORD len = GetTempPathA((DWORD)path_len, path);
    if(len == 0 || len >= path_len)
    {
        fprintf(stderr, "GetTempPathA failed\n");
        return 1;
    }
    while(len > 0 && (path[len - 1] == '\\' || path[len - 1] == '/'))
    {
        path[--len] = '\0';
    }
#else
    if(snprintf(path, path_len, "/tmp") >= (int)path_len)
    {
        fprintf(stderr, "temporary root path is too long\n");
        return 1;
    }
#endif
    return 0;
}

static int build_temp_path(char* path, size_t path_len, const char* leaf_name)
{
    char temp_root[512];

    memset(temp_root, 0, sizeof(temp_root));
    if(get_temp_root(temp_root, sizeof(temp_root)) != 0)
    {
        return 1;
    }

    if(snprintf(path, path_len, "%s%s%s", temp_root, ENT_FILE_SEP, leaf_name) >= (int)path_len)
    {
        fprintf(stderr, "temporary path is too long\n");
        return 1;
    }

    return 0;
}

static int prepare_temp_db_path(char* db_path, size_t db_path_len, const char* prefix)
{
#ifdef _WIN32
    char temp_dir[MAX_PATH];
    char temp_file[MAX_PATH];

    if(GetTempPathA((DWORD)sizeof(temp_dir), temp_dir) == 0)
    {
        fprintf(stderr, "GetTempPathA failed\n");
        return 1;
    }

    if(GetTempFileNameA(temp_dir, prefix, 0, temp_file) == 0)
    {
        fprintf(stderr, "GetTempFileNameA failed\n");
        return 1;
    }

    DeleteFileA(temp_file);

    if(snprintf(db_path, db_path_len, "%s.db", temp_file) >= (int)db_path_len)
    {
        fprintf(stderr, "temporary database path is too long\n");
        return 1;
    }

    DeleteFileA(db_path);
#else
    char db_template[512];
    int fd = -1;

    if(snprintf(db_template, sizeof(db_template), "/tmp/%s_XXXXXX", prefix) >= (int)sizeof(db_template))
    {
        fprintf(stderr, "temporary database template is too long\n");
        return 1;
    }

    fd = mkstemp(db_template);
    if(fd < 0)
    {
        perror("mkstemp");
        return 1;
    }

    close(fd);
    unlink(db_template);

    if(snprintf(db_path, db_path_len, "%s.db", db_template) >= (int)db_path_len)
    {
        fprintf(stderr, "temporary database path is too long\n");
        return 1;
    }

    unlink(db_path);
#endif
    return 0;
}

static void cleanup_temp_db_path(const char* db_path)
{
#ifdef _WIN32
    DeleteFileA(db_path);
#else
    unlink(db_path);
#endif
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

static int test_security_pgsql_is_configured(char* host,
                                             size_t host_len,
                                             char* database,
                                             size_t database_len,
                                             char* user,
                                             size_t user_len,
                                             char* passwd,
                                             size_t passwd_len,
                                             int* port)
{
    if(port == NULL)
    {
        return 0;
    }
    *port = 5432;

    read_env_or_null("ENT_PGSQL_HOST", host, host_len);
    read_env_or_null("ENT_PGSQL_DB", database, database_len);
    read_env_or_null("ENT_PGSQL_USER", user, user_len);
    read_env_or_null("ENT_PGSQL_PASSWORD", passwd, passwd_len);

    {
        char portBuf[32];
        if(read_env_or_null("ENT_PGSQL_PORT", portBuf, sizeof(portBuf)))
        {
            *port = atoi(portBuf);
        }
    }

    return (host[0] != '\0' && database[0] != '\0' && user[0] != '\0');
}

static int test_security_mysql_is_configured(char* host,
                                             size_t host_len,
                                             char* database,
                                             size_t database_len,
                                             char* user,
                                             size_t user_len,
                                             char* passwd,
                                             size_t passwd_len,
                                             int* port)
{
    if(port == NULL)
    {
        return 0;
    }
    *port = 3306;

    read_env_or_null("ENT_MYSQL_HOST", host, host_len);
    read_env_or_null("ENT_MYSQL_DB", database, database_len);
    read_env_or_null("ENT_MYSQL_USER", user, user_len);
    read_env_or_null("ENT_MYSQL_PASSWORD", passwd, passwd_len);

    {
        char portBuf[32];
        if(read_env_or_null("ENT_MYSQL_PORT", portBuf, sizeof(portBuf)))
        {
            *port = atoi(portBuf);
        }
    }

    return (host[0] != '\0' && database[0] != '\0' && user[0] != '\0');
}

/* ══════════════════════════════════════════════════════════
 * TEST 1: ent_init — 超长 name 不崩溃
 * 风险：sprintf(tmpStr, "ent_%s", name) 无边界检查
 * ══════════════════════════════════════════════════════════ */
static void test_init_long_name_no_overflow(void)
{
    TEST_BEGIN("test_init_long_name_no_overflow");
    char work_path[512];

    safe_reset_ent();
    memset(work_path, 0, sizeof(work_path));
    ASSERT_EQ(0, build_temp_path(work_path, sizeof(work_path), "sec_test"),
              "temporary work path should build");

    /* 构造一个 4096 字节长的 name */
    char long_name[4097];
    memset(long_name, 'A', 4096);
    long_name[4096] = '\0';

    /*
     * 这个调用可能触发堆缓冲区溢出（sprintf 无长度限制）。
     * 我们期望它至少不会 SIGSEGV 崩溃，并且应当返回非零错误码
     * 表示拒绝过长输入。当前代码没有这个检查，所以如果它返回 0
     * 说明风险确实存在，需要记录为 FINDING。
     */
    ENT_HANDLE handle = NULL;
    MSG_ID_T ret = ENT_Init(&handle, long_name, work_path, LOG_LEV_WARN_E, ENT_MODE_NORMAL_E);

    /* 无论成功失败，能走到这里就说明没崩溃 */
    fprintf(stdout, "    → ENT_Init(long_name) returned %d (no crash)\n", ret);

    if (ret == ENT_SYS_NORMAL) {
        RECORD_FINDING(_test_name, "ENT_Init accepted an overlong module name");
    }

    /* 清理 */
    if (ret == ENT_SYS_NORMAL) {
        ENT_Close(&handle);
    }
    safe_reset_ent();

    TEST_END();
}

/* ══════════════════════════════════════════════════════════
 * TEST 2: ent_init — 超长 workPath 不崩溃
 * 风险：sprintf(tmpStr, "%s/log", workPath) 无边界检查
 * ══════════════════════════════════════════════════════════ */
static void test_init_long_workpath_no_overflow(void)
{
    TEST_BEGIN("test_init_long_workpath_no_overflow");

    safe_reset_ent();

    /* 构造一个 4096 字节长的路径 */
    char long_path[4097];
    memset(long_path, 'B', 4096);
#ifdef _WIN32
    long_path[0] = 'C';
    long_path[1] = ':';
    long_path[2] = '\\';
#else
    long_path[0] = '/';  /* 使其看起来像一个绝对路径 */
#endif
    long_path[4096] = '\0';

    ENT_HANDLE handle = NULL;
    MSG_ID_T ret = ENT_Init(&handle, "sectest", long_path, LOG_LEV_WARN_E, ENT_MODE_NORMAL_E);

    fprintf(stdout, "    → ENT_Init(long_path) returned %d (no crash)\n", ret);

    if (ret == ENT_SYS_NORMAL) {
        RECORD_FINDING(_test_name, "ENT_Init accepted an overlong workPath");
    }

    if (ret == ENT_SYS_NORMAL) {
        ENT_Close(&handle);
    }
    safe_reset_ent();

    TEST_END();
}

/* ══════════════════════════════════════════════════════════
 * TEST 3: ent_log — 空字符串 logPath 不崩溃
 * 风险：strlen("") == 0 → logPath[len-1] 即 logPath[-1]
 * ══════════════════════════════════════════════════════════ */
static void test_log_empty_path_no_crash(void)
{
    TEST_BEGIN("test_log_empty_path_no_crash");

    MSG_ID_T ret;

    ret = ENT_LogInit();
    ASSERT_EQ(0, ret, "ENT_LogInit should succeed");

    ENT_LOG logHandle = NULL;
    /* 传入空字符串作为 logPath —— 这会触发 logPath[-1] 访问 */
    ret = ENT_LogInitHandle(&logHandle, "SecModule", "");

    fprintf(stdout, "    → ENT_LogInitHandle(\"\") returned %d\n", ret);
    ASSERT_TRUE(ret != 0, "ENT_LogInitHandle should reject an empty logPath");

    /* 无论结果如何，能到这里就是好的 */
    if (logHandle != NULL) {
        ENT_LogCloseHandle(logHandle);
    }
    ENT_LogClose();

    TEST_END();
}

/* ══════════════════════════════════════════════════════════
 * TEST 4: ent_log — maxNum 负数校验
 * 风险：负数 maxNum 导致 deleteTime 计算异常
 * ══════════════════════════════════════════════════════════ */
static void test_log_option_negative_maxnum(void)
{
    TEST_BEGIN("test_log_option_negative_maxnum");
    char log_path[512];

    MSG_ID_T ret;
    memset(log_path, 0, sizeof(log_path));
    ASSERT_EQ(0, build_temp_path(log_path, sizeof(log_path), "sec_maxnum"),
              "temporary log path should build");

    ret = ENT_LogInit();
    ASSERT_EQ(0, ret, "ENT_LogInit should succeed");

    ENT_LOG logHandle = NULL;
    ret = ENT_LogInitHandle(&logHandle, "MaxNumTest", log_path);
    ASSERT_EQ(0, ret, "ENT_LogInitHandle should succeed");
    ASSERT_TRUE(logHandle != NULL, "logHandle should not be NULL");

    /* 设置 maxNum 为负数 —— 应该被拒绝 */
    int neg_max = -1;
    ret = ENT_LogSetOption(logHandle, ENT_LOG_MAX_E, &neg_max);

    fprintf(stdout, "    → ENT_LogSetOption(maxNum=-1) returned %d\n", ret);
    ASSERT_TRUE(ret != 0, "ENT_LogSetOption should reject negative maxNum");

    ENT_LogCloseHandle(logHandle);
    ENT_LogClose();

    TEST_END();
}

/* ══════════════════════════════════════════════════════════
 * TEST 5: ent_log — 非法日志等级不崩溃
 * 风险：sLogLevelStr[] 只有 5 个元素，传 999 会越界
 * ══════════════════════════════════════════════════════════ */
static void test_log_invalid_level_no_crash(void)
{
    TEST_BEGIN("test_log_invalid_level_no_crash");
    char log_path[512];

    MSG_ID_T ret;
    memset(log_path, 0, sizeof(log_path));
    ASSERT_EQ(0, build_temp_path(log_path, sizeof(log_path), "sec_level"),
              "temporary log path should build");

    ret = ENT_LogInit();
    ASSERT_EQ(0, ret, "ENT_LogInit should succeed");

    ENT_LOG logHandle = NULL;
    ret = ENT_LogInitHandle(&logHandle, "LevelTest", log_path);
    ASSERT_EQ(0, ret, "ENT_LogInitHandle should succeed");
    ASSERT_TRUE(logHandle != NULL, "logHandle should not be NULL");

    /* 设置一个非法的日志等级，应该被拒绝 */
    ENT_LOG_LEV_E invalid_level = (ENT_LOG_LEV_E)999;
    ret = ENT_LogSetOption(logHandle, ENT_LOG_LEVEL_E, &invalid_level);
    fprintf(stdout, "    → ENT_LogSetOption(level=999) returned %d\n", ret);
    ASSERT_TRUE(ret != 0, "ENT_LogSetOption should reject invalid log levels");

    ENT_LogCloseHandle(logHandle);
    ENT_LogClose();

    TEST_END();
}

/* ══════════════════════════════════════════════════════════
 * TEST 6: ent_db — 参数化 API 形状编译检查
 * 风险：新的 public API / 参数类型未导出，安全测试无法引用
 * ══════════════════════════════════════════════════════════ */
static void test_db_parameterized_api_shape(void)
{
    TEST_BEGIN("test_db_parameterized_api_shape");

    ENT_DB_PARAM params[1];
    memset(params, 0, sizeof(params));
    params[0].type = ENT_DB_PARAM_TEXT_E;
    params[0].value.text = "Alice";

    ASSERT_EQ((long)sizeof(ENT_DB_PARAM), (long)sizeof(params[0]),
              "ENT_DB_PARAM should be a concrete public type");
    ASSERT_TRUE(sizeof(&ENT_DbReadParams) > 0, "ENT_DbReadParams should be declared");
    ASSERT_TRUE(sizeof(&ENT_DbWriteParams) > 0, "ENT_DbWriteParams should be declared");

    TEST_END();
}

/* ══════════════════════════════════════════════════════════
 * TEST 7: ent_db — 参数化写入避免 DROP TABLE 注入
 * ══════════════════════════════════════════════════════════ */
static void test_db_sql_injection_drop_table(void)
{
    TEST_BEGIN("test_db_sql_injection_drop_table");

    char db_path[512];
    memset(db_path, 0, sizeof(db_path));
    ASSERT_EQ(0, prepare_temp_db_path(db_path, sizeof(db_path), "esi"),
              "temporary database path should build");

    /* 初始化 DB 服务 */
    MSG_ID_T ret;
    ret = ENT_DbInit();
    ASSERT_TRUE(ret == 0 || ret == 1, "ENT_DbInit should succeed");

    DB_HANDLE db = NULL;
    ret = ENT_DbInitHandle(&db, SQLITE_TYPE, NULL, db_path, NULL, NULL, 0);
    ASSERT_EQ(0, ret, "ENT_DbInitHandle should succeed");

    /* 建表 */
    ret = ENT_DbWrite(db, "CREATE TABLE secrets(id INTEGER PRIMARY KEY, data TEXT);", NULL, NULL);
    ASSERT_EQ(0, ret, "CREATE TABLE should succeed");

    const char* injection =
        "confidential'); DROP TABLE secrets; --";
    ENT_DB_PARAM params[1];
    memset(params, 0, sizeof(params));
    params[0].type = ENT_DB_PARAM_TEXT_E;
    params[0].value.text = injection;

    ret = ENT_DbWriteParams(db,
                            "INSERT INTO secrets(data) VALUES(?);",
                            params,
                            1,
                            NULL,
                            NULL);
    ASSERT_EQ(0, ret, "parameterized INSERT should succeed");

    /*
     * malicious text is bound as data, not as SQL.
     */
    INJECT_CAPTURE cap;
    memset(&cap, 0, sizeof(cap));

    ret = ENT_DbReadParams(db,
                           "SELECT data FROM secrets WHERE data = ?;",
                           params,
                           1,
                           inject_capture_cb,
                           &cap);
    ASSERT_EQ(0, ret, "parameterized SELECT should succeed");
    ASSERT_EQ(1, cap.called, "parameterized SELECT should return one row");
    ASSERT_EQ(1, cap.column_num, "parameterized SELECT should return one column");
    ASSERT_EQ(1, cap.row_num, "parameterized SELECT should return one row");
    ASSERT_TRUE(strcmp(cap.leaked_data, injection) == 0,
                "parameterized SELECT should preserve bound text");

    cap.called = 0;
    cap.column_num = 0;
    cap.row_num = 0;
    cap.leaked_data[0] = '\0';

    ret = ENT_DbRead(db, "SELECT * FROM secrets;", inject_capture_cb, &cap);
    ASSERT_EQ(0, ret, "table should remain queryable after bound injection text");
    ASSERT_EQ(1, cap.called, "table should still contain one row");

    /* 清理 */
    ENT_DbCloseHandle(&db);
    ENT_DbClose();
    cleanup_temp_db_path(db_path);

    TEST_END();
}

/* ══════════════════════════════════════════════════════════
 * TEST 8: ent_db — 参数化查询避免 UNION SELECT 信息泄露
 * ══════════════════════════════════════════════════════════ */
static void test_db_sql_injection_union_select(void)
{
    TEST_BEGIN("test_db_sql_injection_union_select");

    char db_path[512];
    memset(db_path, 0, sizeof(db_path));
    ASSERT_EQ(0, prepare_temp_db_path(db_path, sizeof(db_path), "esu"),
              "temporary database path should build");

    MSG_ID_T ret;
    ret = ENT_DbInit();
    ASSERT_TRUE(ret == 0 || ret == 1, "ENT_DbInit should succeed");

    DB_HANDLE db = NULL;
    ret = ENT_DbInitHandle(&db, SQLITE_TYPE, NULL, db_path, NULL, NULL, 0);
    ASSERT_EQ(0, ret, "ENT_DbInitHandle should succeed");

    /* 建两张表：公开表和私密表 */
    ret = ENT_DbWrite(db, "CREATE TABLE public_info(id INTEGER PRIMARY KEY, name TEXT);", NULL, NULL);
    ASSERT_EQ(0, ret, "CREATE public_info should succeed");

    ret = ENT_DbWrite(db, "CREATE TABLE private_creds(id INTEGER PRIMARY KEY, password TEXT);", NULL, NULL);
    ASSERT_EQ(0, ret, "CREATE private_creds should succeed");

    ret = ENT_DbWrite(db, "INSERT INTO public_info(name) VALUES('Alice');", NULL, NULL);
    ASSERT_EQ(0, ret, "INSERT public row should succeed");

    ret = ENT_DbWrite(db, "INSERT INTO private_creds(password) VALUES('s3cr3t_p@ss');", NULL, NULL);
    ASSERT_EQ(0, ret, "INSERT private row should succeed");

    INJECT_CAPTURE cap;
    memset(&cap, 0, sizeof(cap));

    ENT_DB_PARAM params[1];
    memset(params, 0, sizeof(params));
    params[0].type = ENT_DB_PARAM_INT_E;
    params[0].value.i32 = 999;

    ret = ENT_DbReadParams(db,
                           "SELECT name FROM public_info WHERE id = ?;",
                           params,
                           1,
                           inject_capture_cb,
                           &cap);
    ASSERT_EQ(0, ret, "parameterized UNION probe should succeed");
    ASSERT_EQ(0, cap.called, "parameterized UNION probe should not return rows");

    cap.called = 0;
    cap.column_num = 0;
    cap.row_num = 0;
    cap.leaked_data[0] = '\0';

    params[0].value.i32 = 1;
    ret = ENT_DbReadParams(db,
                           "SELECT name FROM public_info WHERE id = ?;",
                           params,
                           1,
                           inject_capture_cb,
                           &cap);
    ASSERT_EQ(0, ret, "benign parameterized query should succeed");
    ASSERT_EQ(1, cap.called, "benign parameterized query should return one row");
    ASSERT_TRUE(strcmp(cap.leaked_data, "Alice") == 0,
                "benign parameterized query should return the requested row");

    ENT_DbCloseHandle(&db);
    ENT_DbClose();
    cleanup_temp_db_path(db_path);

    TEST_END();
}

/* ══════════════════════════════════════════════════════════
 * TEST 9: ent_db — PostgreSQL 参数化查询应使用绑定参数
 * 风险：PgSQL 分支仍然只走 raw SQL，参数化 API 直接返回 UNSUPPORTED
 * ══════════════════════════════════════════════════════════ */
static void test_db_pgsql_parameterized_queries_if_configured(void)
{
    const char* _test_name = "test_db_pgsql_parameterized_queries_if_configured";

    s_checks++;
    fprintf(stdout, "  [RUN ] %s\n", _test_name);

#if ENT_ENABLE_PGSQL
    char host[256];
    char database[256];
    char user[256];
    char passwd[256];
    int port;
    DB_HANDLE db = NULL;
    MSG_ID_T ret;
    INJECT_CAPTURE cap;
    ENT_DB_PARAM params[1];

    if(test_security_pgsql_is_configured(host, sizeof(host),
                                         database, sizeof(database),
                                         user, sizeof(user),
                                         passwd, sizeof(passwd),
                                         &port) == 0)
    {
        fprintf(stdout, "    -> PostgreSQL security test skipped: set ENT_PGSQL_HOST, ENT_PGSQL_DB, and ENT_PGSQL_USER to run it.\n");
        fprintf(stdout, "  [DONE] %s\n", _test_name);
        return;
    }

    memset(&cap, 0, sizeof(cap));
    memset(params, 0, sizeof(params));

    ret = ENT_DbInit();
    ASSERT_TRUE(ret == 0 || ret == 1, "ENT_DbInit should succeed");

    ret = ENT_DbInitHandle(&db, PGSQL_TYPE, host, database, user, passwd, port);
    ASSERT_EQ(0, ret, "ENT_DbInitHandle should create a PostgreSQL handle");

    ret = ENT_DbOpen(db);
    ASSERT_EQ(0, ret, "ENT_DbOpen should connect to PostgreSQL");

    ret = ENT_DbWrite(db,
                      "CREATE TEMP TABLE ent_security_pgsql(id SERIAL PRIMARY KEY, data TEXT NOT NULL);",
                      NULL,
                      NULL);
    ASSERT_EQ(0, ret, "CREATE TEMP TABLE should succeed");

    params[0].type = ENT_DB_PARAM_TEXT_E;
    params[0].value.text = "pg'); DROP TABLE ent_security_pgsql; --";

    ret = ENT_DbWriteParams(db,
                            "INSERT INTO ent_security_pgsql(data) VALUES(?);",
                            params,
                            1,
                            NULL,
                            NULL);
    ASSERT_EQ(0, ret, "PostgreSQL parameterized INSERT should succeed");

    ret = ENT_DbReadParams(db,
                           "SELECT data FROM ent_security_pgsql WHERE data = ?;",
                           params,
                           1,
                           inject_capture_cb,
                           &cap);
    ASSERT_EQ(0, ret, "PostgreSQL parameterized SELECT should succeed");
    ASSERT_EQ(1, cap.called, "PostgreSQL parameterized SELECT should return one row");
    ASSERT_EQ(1, cap.column_num, "PostgreSQL parameterized SELECT should return one column");
    ASSERT_EQ(1, cap.row_num, "PostgreSQL parameterized SELECT should return one row");
    ASSERT_TRUE(strcmp(cap.leaked_data, params[0].value.text) == 0,
                "PostgreSQL parameterized SELECT should preserve bound text");

    ENT_DbCloseHandle(&db);
    ENT_DbClose();
#else
    fprintf(stdout, "    -> PostgreSQL security test skipped: ENT_ENABLE_PGSQL is disabled in this build.\n");
#endif

    fprintf(stdout, "  [DONE] %s\n", _test_name);
}

/* ══════════════════════════════════════════════════════════
 * TEST 10: ent_db — MySQL 参数化查询应使用绑定参数
 * 风险：MySQL 分支仍然只走 raw SQL，参数化 API 直接返回 UNSUPPORTED
 * ══════════════════════════════════════════════════════════ */
static void test_db_mysql_parameterized_queries_if_configured(void)
{
    const char* _test_name = "test_db_mysql_parameterized_queries_if_configured";

    s_checks++;
    fprintf(stdout, "  [RUN ] %s\n", _test_name);

#if ENT_ENABLE_MYSQL
    char host[256];
    char database[256];
    char user[256];
    char passwd[256];
    int port;
    DB_HANDLE db = NULL;
    MSG_ID_T ret;
    INJECT_CAPTURE cap;
    ENT_DB_PARAM params[1];

    if(test_security_mysql_is_configured(host, sizeof(host),
                                         database, sizeof(database),
                                         user, sizeof(user),
                                         passwd, sizeof(passwd),
                                         &port) == 0)
    {
        fprintf(stdout, "    -> MySQL security test skipped: set ENT_MYSQL_HOST, ENT_MYSQL_DB, and ENT_MYSQL_USER to run it.\n");
        fprintf(stdout, "  [DONE] %s\n", _test_name);
        return;
    }

    memset(&cap, 0, sizeof(cap));
    memset(params, 0, sizeof(params));

    ret = ENT_DbInit();
    ASSERT_TRUE(ret == 0 || ret == 1, "ENT_DbInit should succeed");

    ret = ENT_DbInitHandle(&db, MYSQL_TYPE, host, database, user, passwd, port);
    ASSERT_EQ(0, ret, "ENT_DbInitHandle should create a MySQL handle");

    ret = ENT_DbOpen(db);
    ASSERT_EQ(0, ret, "ENT_DbOpen should connect to MySQL");

    ret = ENT_DbWrite(db,
                      "CREATE TEMPORARY TABLE ent_security_mysql(id INT AUTO_INCREMENT PRIMARY KEY, data TEXT NOT NULL);",
                      NULL,
                      NULL);
    ASSERT_EQ(0, ret, "CREATE TEMPORARY TABLE should succeed");

    params[0].type = ENT_DB_PARAM_TEXT_E;
    params[0].value.text = "mysql'); DROP TABLE ent_security_mysql; --";

    ret = ENT_DbWriteParams(db,
                            "INSERT INTO ent_security_mysql(data) VALUES(?);",
                            params,
                            1,
                            NULL,
                            NULL);
    ASSERT_EQ(0, ret, "MySQL parameterized INSERT should succeed");

    ret = ENT_DbReadParams(db,
                           "SELECT data FROM ent_security_mysql WHERE data = ?;",
                           params,
                           1,
                           inject_capture_cb,
                           &cap);
    ASSERT_EQ(0, ret, "MySQL parameterized SELECT should succeed");
    ASSERT_EQ(1, cap.called, "MySQL parameterized SELECT should return one row");
    ASSERT_EQ(1, cap.column_num, "MySQL parameterized SELECT should return one column");
    ASSERT_EQ(1, cap.row_num, "MySQL parameterized SELECT should return one row");
    ASSERT_TRUE(strcmp(cap.leaked_data, params[0].value.text) == 0,
                "MySQL parameterized SELECT should preserve bound text");

    ENT_DbCloseHandle(&db);
    ENT_DbClose();
#else
    fprintf(stdout, "    -> MySQL security test skipped: ENT_ENABLE_MYSQL is disabled in this build.\n");
#endif

    fprintf(stdout, "  [DONE] %s\n", _test_name);
}

/* ══════════════════════════════════════════════════════════
 * TEST 9: ent_db — NULL sql 参数被正确拒绝
 * ══════════════════════════════════════════════════════════ */
static void test_db_null_sql_rejected(void)
{
    TEST_BEGIN("test_db_null_sql_rejected");

    char db_path[512];
    memset(db_path, 0, sizeof(db_path));
    ASSERT_EQ(0, prepare_temp_db_path(db_path, sizeof(db_path), "esn"),
              "temporary database path should build");

    MSG_ID_T ret;
    ret = ENT_DbInit();
    ASSERT_TRUE(ret == 0 || ret == 1, "ENT_DbInit should succeed");

    DB_HANDLE db = NULL;
    ret = ENT_DbInitHandle(&db, SQLITE_TYPE, NULL, db_path, NULL, NULL, 0);
    ASSERT_EQ(0, ret, "ENT_DbInitHandle should succeed");

    /* 传 NULL sql —— 应当被拒绝，返回 -1 */
    ret = ENT_DbRead(db, NULL, NULL, NULL);
    ASSERT_EQ(ENT_DBS_BAD_ARGUMENT, ret, "ENT_DbRead(NULL sql) should return BAD_ARGUMENT");

    ret = ENT_DbWrite(db, NULL, NULL, NULL);
    ASSERT_EQ(ENT_DBS_BAD_ARGUMENT, ret, "ENT_DbWrite(NULL sql) should return BAD_ARGUMENT");

    ENT_DbCloseHandle(&db);
    ENT_DbClose();
    cleanup_temp_db_path(db_path);

    TEST_END();
}

/* ══════════════════════════════════════════════════════════
 * TEST 10: ent_db — NULL handle 被正确拒绝
 * 风险：dbHandle==NULL 时先解引用 dbCfg 会触发未定义行为
 * ══════════════════════════════════════════════════════════ */
static void test_db_null_handle_rejected(void)
{
    TEST_BEGIN("test_db_null_handle_rejected");

    MSG_ID_T ret;
    ret = ENT_DbInit();
    ASSERT_TRUE(ret == 0 || ret == 1, "ENT_DbInit should succeed");

    ret = ENT_DbOpen(NULL);
    ASSERT_EQ(ENT_DBS_BAD_ARGUMENT, ret, "ENT_DbOpen(NULL handle) should return BAD_ARGUMENT");

    ret = ENT_DbRead(NULL, "SELECT 1;", NULL, NULL);
    ASSERT_EQ(ENT_DBS_BAD_ARGUMENT, ret, "ENT_DbRead(NULL handle) should return BAD_ARGUMENT");

    ret = ENT_DbWrite(NULL, "SELECT 1;", NULL, NULL);
    ASSERT_EQ(ENT_DBS_BAD_ARGUMENT, ret, "ENT_DbWrite(NULL handle) should return BAD_ARGUMENT");

    ENT_DbClose();

    TEST_END();
}

/* ══════════════════════════════════════════════════════════
 * main
 * ══════════════════════════════════════════════════════════ */
int main(void)
{
    fprintf(stdout, "=== Security Test Suite ===\n\n");

    /* Buffer overflow tests */
    fprintf(stdout, "[Buffer Overflow]\n");
    test_init_long_name_no_overflow();
    test_init_long_workpath_no_overflow();

    /* Input validation tests */
    fprintf(stdout, "\n[Input Validation]\n");
    test_log_empty_path_no_crash();
    test_log_option_negative_maxnum();
    test_log_invalid_level_no_crash();

    /* SQL injection tests */
    fprintf(stdout, "\n[SQL Injection]\n");
    test_db_parameterized_api_shape();
    test_db_sql_injection_drop_table();
    test_db_sql_injection_union_select();
    test_db_mysql_parameterized_queries_if_configured();
    test_db_pgsql_parameterized_queries_if_configured();
    test_db_null_sql_rejected();
    test_db_null_handle_rejected();

    /* 汇总 */
    fprintf(stdout, "\n=== Audit Summary ===\n");
    fprintf(stdout, "checks=%d assert_failures=%d findings=%d\n",
            s_checks, s_assert_failures, s_findings);

    if (s_assert_failures > 0) {
        fprintf(stderr, "FAILED: %d audit assertion(s) failed\n", s_assert_failures);
        return EXIT_FAILURE;
    }

    if (s_findings > 0) {
        fprintf(stdout, "AUDIT FOUND %d issue(s) that need follow-up fixes\n", s_findings);
    }

    return EXIT_SUCCESS;
}
