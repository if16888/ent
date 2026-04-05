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
#include <signal.h>
#include <setjmp.h>
#include <unistd.h>
#include <limits.h>

#include "ient_comm.h"
#include "ent_init.h"
#include "ent_db.h"

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

/* ── 辅助：安全清理 ENT_Init 遗留状态 ──────────────────── */
static void safe_reset_ent(void)
{
    ENT_Close();
    memset(&gEntCtx, 0, sizeof(gEntCtx));
}

/* ══════════════════════════════════════════════════════════
 * TEST 1: ent_init — 超长 name 不崩溃
 * 风险：sprintf(tmpStr, "ent_%s", name) 无边界检查
 * ══════════════════════════════════════════════════════════ */
static void test_init_long_name_no_overflow(void)
{
    TEST_BEGIN("test_init_long_name_no_overflow");

    safe_reset_ent();

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
    MSG_ID_T ret = ENT_Init(long_name, "/tmp/sec_test", LOG_LEV_WARN_E);

    /* 无论成功失败，能走到这里就说明没崩溃 */
    fprintf(stdout, "    → ENT_Init(long_name) returned %d (no crash)\n", ret);

    if (ret == 0) {
        RECORD_FINDING(_test_name, "ENT_Init accepted an overlong module name");
    }

    /* 清理 */
    if (ret == 0) {
        ENT_Close();
    }
    memset(&gEntCtx, 0, sizeof(gEntCtx));

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
    long_path[0] = '/';  /* 使其看起来像一个绝对路径 */
    long_path[4096] = '\0';

    MSG_ID_T ret = ENT_Init("sectest", long_path, LOG_LEV_WARN_E);

    fprintf(stdout, "    → ENT_Init(long_path) returned %d (no crash)\n", ret);

    if (ret == 0) {
        RECORD_FINDING(_test_name, "ENT_Init accepted an overlong workPath");
    }

    if (ret == 0) {
        ENT_Close();
    }
    memset(&gEntCtx, 0, sizeof(gEntCtx));

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

    fprintf(stdout, "    → ENT_LogInitHandle(\"\") returned %d (no crash)\n", ret);

    if (ret == 0) {
        RECORD_FINDING(_test_name, "ENT_LogInitHandle accepted an empty logPath");
    }

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

    MSG_ID_T ret;

    ret = ENT_LogInit();
    ASSERT_EQ(0, ret, "ENT_LogInit should succeed");

    ENT_LOG logHandle = NULL;
    ret = ENT_LogInitHandle(&logHandle, "MaxNumTest", "/tmp/sec_maxnum");
    ASSERT_EQ(0, ret, "ENT_LogInitHandle should succeed");
    ASSERT_TRUE(logHandle != NULL, "logHandle should not be NULL");

    /* 设置 maxNum 为负数 —— 当前代码不做校验 */
    int neg_max = -1;
    ret = ENT_LogSetOption(logHandle, ENT_LOG_MAX_E, &neg_max);

    fprintf(stdout, "    → ENT_LogSetOption(maxNum=-1) returned %d\n", ret);

    /*
     * 如果返回 0 表示被接受了（风险存在），
     * 理想情况应返回错误码拒绝负数。
     */
    if (ret == 0) {
        RECORD_FINDING(_test_name, "ENT_LogSetOption accepted negative maxNum");
    }

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

    MSG_ID_T ret;

    ret = ENT_LogInit();
    ASSERT_EQ(0, ret, "ENT_LogInit should succeed");

    ENT_LOG logHandle = NULL;
    ret = ENT_LogInitHandle(&logHandle, "LevelTest", "/tmp/sec_level");
    ASSERT_EQ(0, ret, "ENT_LogInitHandle should succeed");
    ASSERT_TRUE(logHandle != NULL, "logHandle should not be NULL");

    /* 设置一个超大的日志等级，使所有打印都通过等级检查 */
    ENT_LOG_LEV_E invalid_level = (ENT_LOG_LEV_E)999;
    ret = ENT_LogSetOption(logHandle, ENT_LOG_LEVEL_E, &invalid_level);
    ASSERT_EQ(0, ret, "ENT_LogSetOption should accept (no validation currently)");

    /*
     * 调用 ENT_LogPrint — 内部会用 sLogLevelStr[logLevel] 索引数组，
     * 但 logLevel 实参由函数内部硬编码为 LOG_LEV_INFO_E (3)，
     * 所以这个调用本身不会越界。
     *
     * 真正的风险是 iENT_LogVPrint 接收到的 logLevel 参数，
     * 在宏 ENT_LOG_FATAL 等中是固定值，不受 logLevel 字段影响。
     * 但将 logLevel 字段设置为 999 会使 level 检查条件全部通过，
     * 这本身就是一个安全风险（所有日志都会被写入，无法抑制）。
     */
    ret = ENT_LogPrint(logHandle, "test message from invalid level %d\n", 999);
    fprintf(stdout, "    → ENT_LogPrint with logLevel=999 returned %d (no crash)\n", ret);

    RECORD_FINDING(_test_name, "invalid log level is accepted without validation");

    ENT_LogCloseHandle(logHandle);
    ENT_LogClose();

    TEST_END();
}

/* ══════════════════════════════════════════════════════════
 * TEST 6: ent_db — SQL 注入 DROP TABLE
 * 风险：sqlite3_exec 直接执行原始 SQL，无参数化
 * ══════════════════════════════════════════════════════════ */
static void test_db_sql_injection_drop_table(void)
{
    TEST_BEGIN("test_db_sql_injection_drop_table");

    char db_template[] = "/tmp/ent_sec_inject_XXXXXX.db";
    int fd = mkstemps(db_template, 3);
    ASSERT_TRUE(fd >= 0, "mkstemps should create temp file");
    close(fd);
    unlink(db_template);

    /* 初始化 DB 服务 */
    MSG_ID_T ret;
    ret = ENT_DbInit();
    ASSERT_TRUE(ret == 0 || ret == 1, "ENT_DbInit should succeed");

    DB_HANDLE db = NULL;
    ret = ENT_DbInitHandle(&db, SQLITE_TYPE, NULL, db_template, NULL, NULL, 0);
    ASSERT_EQ(0, ret, "ENT_DbInitHandle should succeed");

    /* 建表 */
    ret = ENT_DbWrite(db, "CREATE TABLE secrets(id INTEGER PRIMARY KEY, data TEXT);", NULL, NULL);
    ASSERT_EQ(0, ret, "CREATE TABLE should succeed");

    ret = ENT_DbWrite(db, "INSERT INTO secrets(data) VALUES('confidential');", NULL, NULL);
    ASSERT_EQ(0, ret, "INSERT should succeed");

    /*
     * SQL 注入攻击：通过分号注入 DROP TABLE 语句
     * 如果 sqlite3_exec 能执行多语句（它可以），则表会被删除
     */
    const char* injection = "SELECT * FROM secrets; DROP TABLE secrets; --";
    ret = ENT_DbRead(db, injection, NULL, NULL);

    fprintf(stdout, "    → ENT_DbRead(injection) returned %d\n", ret);

    /* 尝试再次查询 secrets 表 —— 如果注入成功，表已被删除 */
    ret = ENT_DbRead(db, "SELECT * FROM secrets;", NULL, NULL);

    if (ret < 0) {
        RECORD_FINDING(_test_name, "DROP TABLE SQL injection succeeded");
        fprintf(stdout, "    → 后续查询失败 (ret=%d)，表已被删除\n", ret);
    } else {
        fprintf(stdout, "    → DROP TABLE injection was blocked (ret=%d)\n", ret);
    }

    /* 清理 */
    ENT_DbCloseHandle(db);
    ENT_DbClose();
    unlink(db_template);

    TEST_END();
}

/* ══════════════════════════════════════════════════════════
 * TEST 7: ent_db — SQL 注入 UNION SELECT 信息泄露
 * 风险：攻击者可通过 UNION 查询窃取其他表数据
 * ══════════════════════════════════════════════════════════ */
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
    cap->called = 1;
    cap->column_num = columnNum;
    cap->row_num = rowNum;
    if (rowRes && rowNum > 0 && columnNum > 0 && rowRes[0]) {
        strncpy(cap->leaked_data, rowRes[0], sizeof(cap->leaked_data) - 1);
        cap->leaked_data[sizeof(cap->leaked_data) - 1] = '\0';
    }
}

static void test_db_sql_injection_union_select(void)
{
    TEST_BEGIN("test_db_sql_injection_union_select");

    char db_template[] = "/tmp/ent_sec_union_XXXXXX.db";
    int fd = mkstemps(db_template, 3);
    ASSERT_TRUE(fd >= 0, "mkstemps should create temp file");
    close(fd);
    unlink(db_template);

    MSG_ID_T ret;
    ret = ENT_DbInit();
    ASSERT_TRUE(ret == 0 || ret == 1, "ENT_DbInit should succeed");

    DB_HANDLE db = NULL;
    ret = ENT_DbInitHandle(&db, SQLITE_TYPE, NULL, db_template, NULL, NULL, 0);
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

    /*
     * UNION SELECT 注入：通过公开表查询窃取私密表数据
     */
    const char* union_inject =
        "SELECT name FROM public_info WHERE id=1 "
        "UNION SELECT password FROM private_creds";

    INJECT_CAPTURE cap;
    memset(&cap, 0, sizeof(cap));

    ret = ENT_DbRead(db, union_inject, inject_capture_cb, &cap);

    fprintf(stdout, "    → ENT_DbRead(union_inject) returned %d\n", ret);

    if (cap.called && strlen(cap.leaked_data) > 0) {
        RECORD_FINDING(_test_name, "UNION SELECT injection leaked data");
        fprintf(stdout, "    → leaked data: '%s'\n", cap.leaked_data);
        fprintf(stdout, "    → rows=%lld, columns=%d\n", cap.row_num, cap.column_num);
    } else {
        fprintf(stdout, "    → UNION injection was blocked or no data leaked\n");
    }

    ENT_DbCloseHandle(db);
    ENT_DbClose();
    unlink(db_template);

    TEST_END();
}

/* ══════════════════════════════════════════════════════════
 * TEST 8: ent_db — NULL sql 参数被正确拒绝
 * ══════════════════════════════════════════════════════════ */
static void test_db_null_sql_rejected(void)
{
    TEST_BEGIN("test_db_null_sql_rejected");

    char db_template[] = "/tmp/ent_sec_null_XXXXXX.db";
    int fd = mkstemps(db_template, 3);
    ASSERT_TRUE(fd >= 0, "mkstemps should create temp file");
    close(fd);
    unlink(db_template);

    MSG_ID_T ret;
    ret = ENT_DbInit();
    ASSERT_TRUE(ret == 0 || ret == 1, "ENT_DbInit should succeed");

    DB_HANDLE db = NULL;
    ret = ENT_DbInitHandle(&db, SQLITE_TYPE, NULL, db_template, NULL, NULL, 0);
    ASSERT_EQ(0, ret, "ENT_DbInitHandle should succeed");

    /* 传 NULL sql —— 应当被拒绝，返回 -1 */
    ret = ENT_DbRead(db, NULL, NULL, NULL);
    ASSERT_EQ(-1, ret, "ENT_DbRead(NULL sql) should return -1");

    ret = ENT_DbWrite(db, NULL, NULL, NULL);
    ASSERT_EQ(-1, ret, "ENT_DbWrite(NULL sql) should return -1");

    ENT_DbCloseHandle(db);
    ENT_DbClose();
    unlink(db_template);

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
    test_db_sql_injection_drop_table();
    test_db_sql_injection_union_select();
    test_db_null_sql_rejected();

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
