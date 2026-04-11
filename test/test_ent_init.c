#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "ient_comm.h"
#include "ent_init.h"
#include "ent_msg.h"

static UTL_CV s_last_cv = NULL;
static UTL_LOCK s_last_lock = NULL;
static int s_wait_calls = 0;
static int s_log_close_handle_calls = 0;
static int s_log_close_calls = 0;
static int s_lock_close_calls = 0;
static int s_cv_close_calls = 0;
static int s_fail_log_init = 0;
static int s_fail_log_init_handle_call = 0;
static int s_log_init_handle_calls = 0;
static int s_fail_log_set_option_call = 0;
static int s_log_set_option_calls = 0;
static int s_fail_lock_init = 0;
static int s_log_error_calls = 0;
static int s_event_counter = 0;
static int s_log_error_order = 0;
static int s_log_close_order = 0;
static int s_mlockall_result = 0;
static int s_mlockall_errno = 0;
static int s_mlockall_calls = 0;
static int s_munlockall_result = 0;
static int s_munlockall_errno = 0;
static int s_munlockall_calls = 0;
static ENT_LOG s_ent_log_at_lock_init = NULL;
static const char* s_last_log_init_handle_module = NULL;
static const char* s_last_log_init_handle_path = NULL;
static const char* s_first_log_init_handle_module = NULL;
static const char* s_first_log_init_handle_path = NULL;
static const char* s_second_log_init_handle_module = NULL;
static const char* s_second_log_init_handle_path = NULL;
static ENT_LOG s_last_closed_log_handle = NULL;
static ENT_LOG s_last_log_error_handle = NULL;
static char s_expected_log_path[512];

static const char* expected_log_path_for(const char* work_path)
{
    size_t len = strlen(work_path);

#ifdef WIN32
    if(work_path[len - 1] == '\\' || work_path[len - 1] == '/')
#else
    if(work_path[len - 1] == '/')
#endif
    {
        snprintf(s_expected_log_path, sizeof(s_expected_log_path), "%slog", work_path);
    }
    else
    {
        snprintf(s_expected_log_path, sizeof(s_expected_log_path), "%s%slog", work_path, ENT_FILE_SEP);
    }

    return s_expected_log_path;
}

static void reset_wait_capture(void)
{
    s_last_cv = NULL;
    s_last_lock = NULL;
    s_wait_calls = 0;
}

static void reset_close_counters(void)
{
    s_log_close_handle_calls = 0;
    s_log_close_calls = 0;
    s_lock_close_calls = 0;
    s_cv_close_calls = 0;
}

static void reset_log_failures(void)
{
    s_fail_log_init = 0;
    s_fail_log_init_handle_call = 0;
    s_log_init_handle_calls = 0;
    s_fail_log_set_option_call = 0;
    s_log_set_option_calls = 0;
    s_fail_lock_init = 0;
    s_log_error_calls = 0;
    s_event_counter = 0;
    s_log_error_order = 0;
    s_log_close_order = 0;
    s_ent_log_at_lock_init = NULL;
    s_last_log_init_handle_module = NULL;
    s_last_log_init_handle_path = NULL;
    s_first_log_init_handle_module = NULL;
    s_first_log_init_handle_path = NULL;
    s_second_log_init_handle_module = NULL;
    s_second_log_init_handle_path = NULL;
    s_last_closed_log_handle = NULL;
    s_last_log_error_handle = NULL;
    s_mlockall_result = 0;
    s_mlockall_errno = 0;
    s_mlockall_calls = 0;
    s_munlockall_result = 0;
    s_munlockall_errno = 0;
    s_munlockall_calls = 0;
}

static int expect_true(int condition, const char* message)
{
    if(!condition)
    {
        fprintf(stderr, "%s\n", message);
        return 1;
    }

    return 0;
}

#ifndef WIN32
int mlockall(int flags)
{
    (void)flags;
    s_mlockall_calls++;
    if(s_mlockall_result != 0)
    {
        errno = s_mlockall_errno;
    }
    return s_mlockall_result;
}

int munlockall(void)
{
    s_munlockall_calls++;
    if(s_munlockall_result != 0)
    {
        errno = s_munlockall_errno;
    }
    return s_munlockall_result;
}
#endif

MSG_ID_T ENT_LogInit(void)
{
    if(s_fail_log_init != 0)
    {
        return s_fail_log_init;
    }
    return 0;
}

MSG_ID_T ENT_LogClose(void)
{
    s_log_close_calls++;
    s_log_close_order = ++s_event_counter;
    return 0;
}

MSG_ID_T ENT_LogInitHandle(ENT_LOG* pLogHandle, const char* moduleName, const char* logPath)
{
    s_log_init_handle_calls++;
    s_last_log_init_handle_module = moduleName;
    s_last_log_init_handle_path = logPath;
    if(s_log_init_handle_calls == 1)
    {
        s_first_log_init_handle_module = moduleName;
        s_first_log_init_handle_path = logPath;
    }
    else if(s_log_init_handle_calls == 2)
    {
        s_second_log_init_handle_module = moduleName;
        s_second_log_init_handle_path = logPath;
    }
    if(s_fail_log_init_handle_call == s_log_init_handle_calls)
    {
        return -100 - s_log_init_handle_calls;
    }
    if(pLogHandle != NULL)
    {
        *pLogHandle = (ENT_LOG)0x10;
    }
    return 0;
}

MSG_ID_T ENT_LogSetOption(ENT_LOG logHandle, ENT_LOG_OPTIONS_E option, const void* arg)
{
    (void)logHandle;
    (void)option;
    (void)arg;
    s_log_set_option_calls++;
    if(s_fail_log_set_option_call == s_log_set_option_calls)
    {
        return -200 - s_log_set_option_calls;
    }
    return 0;
}

MSG_ID_T ENT_LogCloseHandle(ENT_LOG logHandle)
{
    s_last_closed_log_handle = logHandle;
    s_log_close_handle_calls++;
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
    s_last_log_error_handle = logHandle;
    s_log_error_calls++;
    s_log_error_order = ++s_event_counter;
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

MSG_ID_T UTL_LockInit(UTL_LOCK* lock, const char* name)
{
    (void)name;
    s_ent_log_at_lock_init = gEntCtx.entLog;
    if(s_fail_lock_init != 0)
    {
        return s_fail_lock_init;
    }
    if(lock != NULL)
    {
        *lock = (UTL_LOCK)0x20;
    }
    return 0;
}

MSG_ID_T UTL_LockInitEx(UTL_LOCK* lock, const char* name, UTL_LOCK_TYPE_T type)
{
    (void)type;
    return UTL_LockInit(lock, name);
}

MSG_ID_T UTL_LockEnter(UTL_LOCK lock)
{
    (void)lock;
    return 0;
}

MSG_ID_T UTL_LockEnterEx(UTL_LOCK lock, UTL_LOCK_RW_TYPE_T rwType)
{
    (void)lock;
    (void)rwType;
    return 0;
}

MSG_ID_T UTL_LockLeave(UTL_LOCK lock)
{
    (void)lock;
    return 0;
}

MSG_ID_T UTL_LockLeaveEx(UTL_LOCK lock, UTL_LOCK_RW_TYPE_T rwType)
{
    (void)lock;
    (void)rwType;
    return 0;
}

MSG_ID_T UTL_LockClose(UTL_LOCK lock)
{
    (void)lock;
    s_lock_close_calls++;
    return 0;
}

MSG_ID_T UTL_CVInit(UTL_CV* cv, const char* name)
{
    (void)name;
    if(cv != NULL)
    {
        *cv = (UTL_CV)0x30;
    }
    return 0;
}

MSG_ID_T UTL_CVClose(UTL_CV cv)
{
    (void)cv;
    s_cv_close_calls++;
    return 0;
}

MSG_ID_T UTL_CVWait(UTL_CV cv, UTL_LOCK lock, int ms, UTL_LOCK_RW_TYPE_T rwType)
{
    (void)ms;
    (void)rwType;
    s_last_cv = cv;
    s_last_lock = lock;
    s_wait_calls++;
    return 0;
}

MSG_ID_T UTL_CVWake(UTL_CV cv)
{
    (void)cv;
    return 0;
}

MSG_ID_T UTL_CVWakeAll(UTL_CV cv)
{
    (void)cv;
    return 0;
}

static int test_ent_run_rejects_uninitialized_context(void)
{
    memset(&gEntCtx, 0, sizeof(gEntCtx));
    reset_wait_capture();

    if(expect_true(ENT_Run() == ENT_SYS_RUN_UNINITIALIZED, "ENT_Run should reject an uninitialized context") != 0)
    {
        return 1;
    }

    return expect_true(s_wait_calls == 0, "ENT_Run should not wait when the context is uninitialized");
}

static int test_ent_run_waits_on_cv_with_lock(void)
{
    memset(&gEntCtx, 0, sizeof(gEntCtx));
    gEntCtx.isInit = true;
    gEntCtx.entCV = (UTL_CV)0x1234;
    gEntCtx.entLock = (UTL_LOCK)0x5678;
    reset_wait_capture();

    if(expect_true(ENT_Run() == ENT_SYS_NORMAL, "ENT_Run should succeed after initialization") != 0)
    {
        return 1;
    }

    if(expect_true(s_wait_calls == 1, "ENT_Run should call UTL_CVWait exactly once") != 0)
    {
        return 1;
    }

    if(expect_true(s_last_cv == gEntCtx.entCV, "ENT_Run should pass entCV as the first UTL_CVWait argument") != 0)
    {
        return 1;
    }

    return expect_true(s_last_lock == gEntCtx.entLock, "ENT_Run should pass entLock as the second UTL_CVWait argument");
}

static int test_ent_close_clears_runtime_handles(void)
{
    memset(&gEntCtx, 0, sizeof(gEntCtx));
    gEntCtx.isInit = true;
    gEntCtx.entLog = (ENT_LOG)0x11;
    gEntCtx.entLock = (UTL_LOCK)0x22;
    gEntCtx.entCV = (UTL_CV)0x33;
    reset_close_counters();

    if(expect_true(ENT_Close() == ENT_SYS_NORMAL, "ENT_Close should succeed for an initialized context") != 0)
    {
        return 1;
    }

    if(expect_true(gEntCtx.isInit == false, "ENT_Close should mark the context as uninitialized") != 0)
    {
        return 1;
    }

    if(expect_true(gEntCtx.entLog == NULL, "ENT_Close should clear entLog after closing it") != 0)
    {
        return 1;
    }

    if(expect_true(gEntCtx.entLock == NULL, "ENT_Close should clear entLock after closing it") != 0)
    {
        return 1;
    }

    if(expect_true(gEntCtx.entCV == NULL, "ENT_Close should clear entCV after closing it") != 0)
    {
        return 1;
    }

    if(expect_true(s_cv_close_calls == 1, "ENT_Close should close the condition variable exactly once") != 0)
    {
        return 1;
    }

    if(expect_true(s_lock_close_calls == 1, "ENT_Close should close the lock exactly once") != 0)
    {
        return 1;
    }

    if(expect_true(s_log_close_handle_calls == 2, "ENT_Close should close both log handles") != 0)
    {
        return 1;
    }

    return expect_true(s_log_close_calls == 1, "ENT_Close should close logging exactly once");
}

static int test_ent_init_closes_logging_when_default_log_level_setup_fails(void)
{
    memset(&gEntCtx, 0, sizeof(gEntCtx));
    reset_close_counters();
    reset_log_failures();
    s_fail_log_set_option_call = 1;

    if(expect_true(ENT_Init("demo", "/tmp/demo", LOG_LEV_WARN_E, ENT_MODE_NORMAL_E) == ENT_INIT_DEFAULT_LEVELFAIL,
                   "ENT_Init should report default log level setup failure") != 0)
    {
        return 1;
    }

    if(expect_true(gEntCtx.isInit == false, "ENT_Init should leave the context uninitialized on failure") != 0)
    {
        return 1;
    }

    if(expect_true(s_log_close_handle_calls == 1,
                   "ENT_Init should close the default log handle when default log setup fails") != 0)
    {
        return 1;
    }

    return expect_true(s_log_close_calls == 1,
                       "ENT_Init should close logging when default log setup fails");
}

static int test_ent_init_logs_before_tearing_down_logging_when_lock_init_fails(void)
{
    memset(&gEntCtx, 0, sizeof(gEntCtx));
    reset_close_counters();
    reset_log_failures();
    s_fail_lock_init = -1;

    if(expect_true(ENT_Init("demo", "/tmp/demo", LOG_LEV_WARN_E, ENT_MODE_NORMAL_E) == ENT_INIT_LOCK_INITFAIL,
                   "ENT_Init should report lock initialization failure") != 0)
    {
        return 1;
    }

    if(expect_true(s_log_error_calls == 1,
                   "ENT_Init should emit one internal error log before tearing logging down") != 0)
    {
        return 1;
    }

    if(expect_true(s_last_log_error_handle != NULL,
                   "ENT_Init should log with the entity handle while it is still valid") != 0)
    {
        return 1;
    }

    if(expect_true(s_ent_log_at_lock_init != NULL,
                   "ENT_Init should have an entity log handle before lock initialization runs") != 0)
    {
        return 1;
    }

    if(expect_true(s_log_error_order != 0 && s_log_close_order != 0 && s_log_error_order < s_log_close_order,
                   "ENT_Init should log the lock failure before closing the logging subsystem") != 0)
    {
        return 1;
    }

    if(expect_true(gEntCtx.isInit == false, "ENT_Init should leave the context uninitialized after lock failure") != 0)
    {
        return 1;
    }

    return expect_true(s_log_close_calls == 1,
                       "ENT_Init should still close logging after logging the lock failure");
}

static int test_ent_init_builds_paths_without_trailing_separator(void)
{
    const char* expected_log_path = expected_log_path_for("/tmp/demo");

    memset(&gEntCtx, 0, sizeof(gEntCtx));
    reset_close_counters();
    reset_log_failures();

    if(expect_true(ENT_Init("demo", "/tmp/demo", LOG_LEV_WARN_E, ENT_MODE_NORMAL_E) == ENT_SYS_NORMAL,
                   "ENT_Init should succeed for a normal work path") != 0)
    {
        return 1;
    }

    if(expect_true(strcmp(gEntCtx.workPath, "/tmp/demo") == 0,
                   "ENT_Init should retain the original workPath") != 0)
    {
        return 1;
    }

    if(expect_true(strcmp(gEntCtx.entName, "demo") == 0,
                   "ENT_Init should retain the original entName") != 0)
    {
        return 1;
    }

    if(expect_true(strcmp(gEntCtx.logPath, expected_log_path) == 0,
                   "ENT_Init should append /log when workPath has no trailing separator") != 0)
    {
        return 1;
    }

    if(expect_true(strcmp(gEntCtx.logName, "ent_demo") == 0,
                   "ENT_Init should prefix the log name with ent_") != 0)
    {
        return 1;
    }

    if(expect_true(s_log_init_handle_calls == 2,
                   "ENT_Init should initialize the default and entity log handles") != 0)
    {
        return 1;
    }

    if(expect_true(strcmp(s_first_log_init_handle_module, "demo") == 0,
                   "ENT_Init should initialize the default logger with the entity name") != 0)
    {
        return 1;
    }

    if(expect_true(strcmp(s_first_log_init_handle_path, expected_log_path) == 0,
                   "ENT_Init should initialize the default logger with the computed log path") != 0)
    {
        return 1;
    }

    if(expect_true(strcmp(s_second_log_init_handle_module, "ent_demo") == 0,
                   "ENT_Init should initialize the entity logger with the prefixed log name") != 0)
    {
        return 1;
    }

    if(expect_true(strcmp(s_second_log_init_handle_path, expected_log_path) == 0,
                   "ENT_Init should initialize the entity logger with the computed log path") != 0)
    {
        return 1;
    }

    if(expect_true(s_log_set_option_calls == 2,
                   "ENT_Init should set log level for the default and entity loggers") != 0)
    {
        return 1;
    }

    if(expect_true(ENT_Close() == ENT_SYS_NORMAL, "ENT_Close should succeed after a successful ENT_Init") != 0)
    {
        return 1;
    }

    return expect_true(s_log_close_handle_calls == 2,
                       "ENT_Close should close both log handles after a successful ENT_Init");
}

static int test_ent_init_builds_paths_with_trailing_separator(void)
{
    const char* expected_log_path = expected_log_path_for("/tmp/demo/");

    memset(&gEntCtx, 0, sizeof(gEntCtx));
    reset_close_counters();
    reset_log_failures();

    if(expect_true(ENT_Init("demo", "/tmp/demo/", LOG_LEV_WARN_E, ENT_MODE_NORMAL_E) == ENT_SYS_NORMAL,
                   "ENT_Init should succeed for a work path that already ends with a separator") != 0)
    {
        return 1;
    }

    if(expect_true(strcmp(gEntCtx.logPath, expected_log_path) == 0,
                   "ENT_Init should not duplicate the separator before log") != 0)
    {
        return 1;
    }

    if(expect_true(strcmp(s_first_log_init_handle_path, expected_log_path) == 0,
                   "The default logger should receive the normalized log path") != 0)
    {
        return 1;
    }

    if(expect_true(strcmp(s_second_log_init_handle_path, expected_log_path) == 0,
                   "The entity logger should receive the normalized log path") != 0)
    {
        return 1;
    }

    return expect_true(ENT_Close() == ENT_SYS_NORMAL, "ENT_Close should succeed after trailing-separator initialization");
}

static int test_ent_init_rejects_empty_name_or_work_path(void)
{
    MSG_ID_T sts = 0;

    memset(&gEntCtx, 0, sizeof(gEntCtx));
    reset_close_counters();
    reset_log_failures();

    sts = ENT_Init("", "/tmp/demo", LOG_LEV_WARN_E, ENT_MODE_NORMAL_E);
    if(expect_true(sts == ENT_INIT_INVALID_ARGUMENT,
                   "ENT_Init should reject an empty entity name") != 0)
    {
        return 1;
    }

    sts = ENT_Init("demo", "", LOG_LEV_WARN_E, ENT_MODE_NORMAL_E);
    if(expect_true(sts == ENT_INIT_INVALID_ARGUMENT,
                   "ENT_Init should reject an empty workPath") != 0)
    {
        return 1;
    }

    if(expect_true(gEntCtx.isInit == false, "ENT_Init should remain uninitialized after rejecting empty input") != 0)
    {
        return 1;
    }

    return expect_true(s_log_init_handle_calls == 0,
                       "ENT_Init should fail before touching logging for empty input");
}

static int test_ent_init_realtime_mode_can_degrade_to_normal(void)
{
    memset(&gEntCtx, 0, sizeof(gEntCtx));
    reset_close_counters();
    reset_log_failures();

#ifndef WIN32
    s_mlockall_result = -1;
    s_mlockall_errno = EPERM;
#endif

    if(expect_true(ENT_Init("demo", "/tmp/demo", LOG_LEV_WARN_E, ENT_MODE_REALTIME_E) == ENT_SYS_NORMAL,
                   "ENT_Init should still succeed when realtime mode degrades to normal mode") != 0)
    {
        return 1;
    }

    if(expect_true(gEntCtx.rtRequested == true, "ENT_Init should persist that realtime mode was requested") != 0)
    {
        ENT_Close();
        return 1;
    }

    if(expect_true(gEntCtx.rtEnabled == false, "ENT_Init should report degraded normal mode when realtime is not applied") != 0)
    {
        ENT_Close();
        return 1;
    }

#ifndef WIN32
    if(expect_true(gEntCtx.rtLastError == EPERM, "ENT_Init should preserve the mlockall errno when realtime degrades") != 0)
    {
        ENT_Close();
        return 1;
    }

    if(expect_true(s_mlockall_calls == 1, "ENT_Init should attempt mlockall once for realtime mode") != 0)
    {
        ENT_Close();
        return 1;
    }
#endif

    int closeStatus = ENT_Close();

    return expect_true(closeStatus == ENT_SYS_NORMAL, "ENT_Close should succeed after realtime degrade initialization");
}

static int test_ent_set_rt_attributes_rejects_uninitialized_context(void)
{
    memset(&gEntCtx, 0, sizeof(gEntCtx));
    return expect_true(ENT_SetRtAttributes(-1, ENT_RT_POLICY_OTHER_E, 0) == ENT_RT_NOT_INITIALIZED,
                       "ENT_SetRtAttributes should reject an uninitialized context");
}

static int test_ent_set_rt_attributes_allows_noop_after_init(void)
{
    memset(&gEntCtx, 0, sizeof(gEntCtx));
    reset_close_counters();
    reset_log_failures();

    if(expect_true(ENT_Init("demo", "/tmp/demo", LOG_LEV_WARN_E, ENT_MODE_REALTIME_E) == ENT_SYS_NORMAL,
                   "ENT_Init should succeed before applying RT attributes") != 0)
    {
        return 1;
    }

    if(expect_true(ENT_SetRtAttributes(-1, ENT_RT_POLICY_OTHER_E, 0) == ENT_SYS_NORMAL,
                   "ENT_SetRtAttributes should allow noop configuration after init") != 0)
    {
        ENT_Close();
        return 1;
    }

    return expect_true(ENT_Close() == ENT_SYS_NORMAL, "ENT_Close should succeed after ENT_SetRtAttributes noop");
}

static int test_ent_set_rt_attributes_rejects_normal_mode(void)
{
    memset(&gEntCtx, 0, sizeof(gEntCtx));
    reset_close_counters();
    reset_log_failures();

    if(expect_true(ENT_Init("demo", "/tmp/demo", LOG_LEV_WARN_E, ENT_MODE_NORMAL_E) == ENT_SYS_NORMAL,
                   "ENT_Init should succeed in normal mode before checking rt attribute rejection") != 0)
    {
        return 1;
    }

    if(expect_true(ENT_SetRtAttributes(0, ENT_RT_POLICY_FIFO_E, 1) == ENT_RT_NOTRT,
                   "ENT_SetRtAttributes should reject realtime attributes when init mode is normal") != 0)
    {
        ENT_Close();
        return 1;
    }

    if(expect_true(gEntCtx.rtCpu == -1 && gEntCtx.rtPolicy == ENT_RT_POLICY_OTHER_E && gEntCtx.rtPriority == 0,
                   "ENT_SetRtAttributes should leave rt settings unchanged when mode is normal") != 0)
    {
        ENT_Close();
        return 1;
    }

    return expect_true(ENT_Close() == ENT_SYS_NORMAL, "ENT_Close should succeed after normal-mode rt rejection");
}

int main(void)
{
    int failures = 0;

    failures += test_ent_run_rejects_uninitialized_context();
    failures += test_ent_run_waits_on_cv_with_lock();
    failures += test_ent_close_clears_runtime_handles();
    failures += test_ent_init_closes_logging_when_default_log_level_setup_fails();
    failures += test_ent_init_logs_before_tearing_down_logging_when_lock_init_fails();
    failures += test_ent_init_builds_paths_without_trailing_separator();
    failures += test_ent_init_builds_paths_with_trailing_separator();
    failures += test_ent_init_rejects_empty_name_or_work_path();
    failures += test_ent_init_realtime_mode_can_degrade_to_normal();
    failures += test_ent_set_rt_attributes_rejects_uninitialized_context();
    failures += test_ent_set_rt_attributes_allows_noop_after_init();
    failures += test_ent_set_rt_attributes_rejects_normal_mode();

    if(failures != 0)
    {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
