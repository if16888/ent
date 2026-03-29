#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ient_comm.h"
#include "ent_init.h"

static UTL_CV s_last_cv = NULL;
static UTL_LOCK s_last_lock = NULL;
static int s_wait_calls = 0;

static void reset_wait_capture(void)
{
    s_last_cv = NULL;
    s_last_lock = NULL;
    s_wait_calls = 0;
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
    (void)moduleName;
    (void)logPath;
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

MSG_ID_T UTL_LockInit(UTL_LOCK* lock, const char* name)
{
    (void)name;
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

    if(expect_true(ENT_Run() == -1, "ENT_Run should reject an uninitialized context") != 0)
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

    if(expect_true(ENT_Run() == 0, "ENT_Run should succeed after initialization") != 0)
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

int main(void)
{
    int failures = 0;

    failures += test_ent_run_rejects_uninitialized_context();
    failures += test_ent_run_waits_on_cv_with_lock();

    if(failures != 0)
    {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
