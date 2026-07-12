#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <errno.h>
#endif

#include "ient_comm.h"
#include "ient_runtime.h"
#include "ent_msg.h"
#include "ent_thread.h"
#include "ent_utility.h"

ENT_CTX gEntCtx;

static int s_dll_ins_fail = 0;
static int s_join_calls = 0;
static int s_wait_calls = 0;
static int s_close_calls = 0;
static int s_lock_close_calls = 0;
static int s_cv_init_fail = 0;
static int s_callback_calls = 0;
static int s_wrapper_calls = 0;
#ifdef WIN32
static LPTHREAD_START_ROUTINE s_saved_start_routine = NULL;
static LPVOID s_saved_param = NULL;
#endif

static int expect_true(int condition, const char* message)
{
    if(!condition)
    {
        fprintf(stderr, "%s\n", message);
        return 1;
    }
    return 0;
}

MSG_ID_T ENT_LogInit(void){return 0;}
MSG_ID_T ENT_LogClose(void){return 0;}
MSG_ID_T ENT_LogInitHandle(ENT_LOG* pLogHandle, const char* moduleName, const char* logPath){(void)pLogHandle;(void)moduleName;(void)logPath;return 0;}
MSG_ID_T ENT_LogSetOption(ENT_LOG logHandle, ENT_LOG_OPTIONS_E option, const void* arg){(void)logHandle;(void)option;(void)arg;return 0;}
MSG_ID_T ENT_LogCloseHandle(ENT_LOG logHandle){(void)logHandle;return 0;}
MSG_ID_T ENT_LogRaw(ENT_LOG logHandle, const char* format, ...){(void)logHandle;(void)format;return 0;}
MSG_ID_T ENT_LogFatal(ENT_LOG logHandle, const char* format, ...){(void)logHandle;(void)format;return 0;}
MSG_ID_T ENT_LogError(ENT_LOG logHandle, const char* format, ...){(void)logHandle;(void)format;return 0;}
MSG_ID_T ENT_LogWarn(ENT_LOG logHandle, const char* format, ...){(void)logHandle;(void)format;return 0;}
MSG_ID_T ENT_LogPrint(ENT_LOG logHandle, const char* format, ...){(void)logHandle;(void)format;return 0;}
MSG_ID_T ENT_LogDebug(ENT_LOG logHandle, const char* format, ...){(void)logHandle;(void)format;return 0;}

#define UTL_LockInit mock_UTL_LockInit
#define UTL_LockClose mock_UTL_LockClose
#define UTL_LockEnter mock_UTL_LockEnter
#define UTL_LockLeave mock_UTL_LockLeave
#define UTL_CVInit mock_UTL_CVInit
#define UTL_CVClose mock_UTL_CVClose
#define UTL_CVWait mock_UTL_CVWait
#define UTL_CVWakeAll mock_UTL_CVWakeAll
#define UTL_DllInitHead mock_UTL_DllInitHead
#define UTL_DllInsHead mock_UTL_DllInsHead
#define UTL_DllRemHead mock_UTL_DllRemHead
#define UTL_DllRemCurr mock_UTL_DllRemCurr
#ifdef WIN32
#define CreateThread mock_CreateThread
#define WakeConditionVariable mock_WakeConditionVariable
#define WaitForSingleObject mock_WaitForSingleObject
#define CloseHandle mock_CloseHandle
#else
#define pthread_mutex_init mock_pthread_mutex_init
#define pthread_mutex_destroy mock_pthread_mutex_destroy
#define pthread_cond_init mock_pthread_cond_init
#define pthread_cond_destroy mock_pthread_cond_destroy
#define pthread_create mock_pthread_create
#define pthread_join mock_pthread_join
#endif

static MSG_ID_T mock_UTL_LockInit(UTL_LOCK* lock,const char* name)
{
    static int dummy;
    (void)name;
    if(lock == NULL)
    {
        return ENT_UTHD_INVALID_ARGUMENT;
    }
    *lock = &dummy;
    return ENT_SYS_NORMAL;
}

static MSG_ID_T mock_UTL_LockClose(UTL_LOCK* lock)
{
    (void)lock;
    s_lock_close_calls++;
    return ENT_SYS_NORMAL;
}

static MSG_ID_T mock_UTL_LockEnter(UTL_LOCK lock)
{
    (void)lock;
    return ENT_SYS_NORMAL;
}

static MSG_ID_T mock_UTL_LockLeave(UTL_LOCK lock)
{
    (void)lock;
    return ENT_SYS_NORMAL;
}

static MSG_ID_T mock_UTL_CVInit(UTL_CV* cv,const char* name)
{
    static int dummy;
    (void)name;
    if(s_cv_init_fail)
    {
        return ENT_UTHD_INIT_FAILED;
    }
    *cv = &dummy;
    return ENT_SYS_NORMAL;
}

static MSG_ID_T mock_UTL_CVClose(UTL_CV* cv)
{
    (void)cv;
    return ENT_SYS_NORMAL;
}

static MSG_ID_T mock_UTL_CVWait(UTL_CV cv,UTL_LOCK lock,int ms,UTL_LOCK_RW_TYPE_T rwType)
{
    (void)cv;
    (void)lock;
    (void)ms;
    (void)rwType;
    return ENT_SYS_NORMAL;
}

static MSG_ID_T mock_UTL_CVWakeAll(UTL_CV cv)
{
    (void)cv;
    return ENT_SYS_NORMAL;
}

static MSG_ID_T mock_UTL_DllInitHead(DLL_D_HDR* head)
{
    if(head == NULL)
    {
        return -1;
    }
    head->fw_ptr = head;
    head->bw_ptr = head;
    return ENT_SYS_NORMAL;
}

static MSG_ID_T mock_UTL_DllInsHead(DLL_D_HDR* head, DLL_D_HDR* node)
{
    if(head == NULL || node == NULL)
    {
        return -1;
    }
    if(s_dll_ins_fail)
    {
        return -1;
    }
    node->fw_ptr = head->fw_ptr;
    node->bw_ptr = head;
    head->fw_ptr->bw_ptr = node;
    head->fw_ptr = node;
    return ENT_SYS_NORMAL;
}

static MSG_ID_T mock_UTL_DllRemHead(DLL_D_HDR* head, DLL_D_HDR** out)
{
    DLL_D_HDR* node = NULL;
    if(head == NULL || out == NULL)
    {
        return -1;
    }
    if(head->fw_ptr == head)
    {
        *out = NULL;
        return -1;
    }
    node = head->fw_ptr;
    head->fw_ptr = node->fw_ptr;
    node->fw_ptr->bw_ptr = head;
    node->fw_ptr = NULL;
    node->bw_ptr = NULL;
    *out = node;
    return ENT_SYS_NORMAL;
}

static MSG_ID_T mock_UTL_DllRemCurr(DLL_D_HDR* curr, DLL_D_HDR** out)
{
    if(curr == NULL || out == NULL || curr->fw_ptr == NULL || curr->bw_ptr == NULL)
    {
        return -1;
    }
    curr->bw_ptr->fw_ptr = curr->fw_ptr;
    curr->fw_ptr->bw_ptr = curr->bw_ptr;
    curr->fw_ptr = NULL;
    curr->bw_ptr = NULL;
    *out = curr;
    return ENT_SYS_NORMAL;
}

#ifndef WIN32
static int mock_pthread_mutex_init(pthread_mutex_t* mutex, const pthread_mutexattr_t* attr)
{
    (void)mutex;
    (void)attr;
    return 0;
}

static int mock_pthread_mutex_destroy(pthread_mutex_t* mutex)
{
    (void)mutex;
    return 0;
}

static int mock_pthread_cond_init(pthread_cond_t* cond, const pthread_condattr_t* attr)
{
    (void)cond;
    (void)attr;
    return 0;
}

static int mock_pthread_cond_destroy(pthread_cond_t* cond)
{
    (void)cond;
    return 0;
}

static int mock_pthread_create(pthread_t* thread, const pthread_attr_t* attr, void* (*start_routine)(void*), void* arg)
{
    (void)attr;
    (void)start_routine;
    (void)arg;
    *thread = (pthread_t)0x1234;
    return 0;
}

static int mock_pthread_join(pthread_t thread, void** retval)
{
    (void)thread;
    if(retval != NULL)
    {
        *retval = NULL;
    }
    s_join_calls++;
    return 0;
}
#else
static HANDLE mock_CreateThread(LPSECURITY_ATTRIBUTES attrs, SIZE_T stackSize, LPTHREAD_START_ROUTINE startRoutine, LPVOID param, DWORD flags, LPDWORD threadId)
{
    (void)attrs;
    (void)stackSize;
    (void)flags;
    s_saved_start_routine = startRoutine;
    s_saved_param = param;
    if(threadId != NULL)
    {
        *threadId = 77;
    }
    return (HANDLE)0x1234;
}

static BOOL mock_WakeConditionVariable(PCONDITION_VARIABLE cv)
{
    (void)cv;
    s_wrapper_calls++;
    if(s_saved_start_routine != NULL)
    {
        s_saved_start_routine(s_saved_param);
    }
    return TRUE;
}

static DWORD mock_WaitForSingleObject(HANDLE handle, DWORD milliseconds)
{
    (void)handle;
    (void)milliseconds;
    s_wait_calls++;
    return WAIT_OBJECT_0;
}

static BOOL mock_CloseHandle(HANDLE handle)
{
    (void)handle;
    s_close_calls++;
    return TRUE;
}
#endif

#ifdef WIN32
static DWORD WINAPI dummy_thread(void* data)
#else
static void* dummy_thread(void* data)
#endif
{
    s_callback_calls++;
#ifdef WIN32
    return (DWORD)(ULONG_PTR)data;
#else
    return data;
#endif
}

#include "../comm/ent_thread.c"

static int test_thread_init_releases_lock_when_cv_init_fails(void)
{
    ENT_THREAD handle = NULL;

    s_lock_close_calls = 0;
    s_cv_init_fail = 1;
    if(expect_true(ENT_ThreadInit(&handle) == ENT_THRD_LOCK_FAILED,
                   "ENT_ThreadInit should report lifecycle CV initialization failure") != 0 ||
       expect_true(handle == NULL,
                   "ENT_ThreadInit should not publish a handle after lifecycle CV initialization failure") != 0 ||
       expect_true(s_lock_close_calls == 1,
                   "ENT_ThreadInit should release the lifecycle lock after CV initialization failure") != 0)
    {
        s_cv_init_fail = 0;
        return 1;
    }
    s_cv_init_fail = 0;
    return 0;
}

static int test_thread_create_reclaims_created_thread_if_registration_fails(void)
{
    ENT_THREAD handle = NULL;
    ENT_THREAD_ID tid = (ENT_THREAD_ID)0x1;
    int value = 7;

    s_dll_ins_fail = 0;
    s_join_calls = 0;
    s_wait_calls = 0;
    s_close_calls = 0;
    s_callback_calls = 0;
    s_wrapper_calls = 0;
#ifdef WIN32
    s_saved_start_routine = NULL;
    s_saved_param = NULL;
#endif

    if(expect_true(ENT_ThreadInit(&handle) == ENT_SYS_NORMAL,
                   "ENT_ThreadInit should create a thread context for registration failure checks") != 0)
    {
        return 1;
    }

    s_dll_ins_fail = 1;
    if(expect_true(ENT_ThreadCreate(&tid, handle, dummy_thread, &value) == ENT_THRD_CREATE_FAILED,
                   "ENT_ThreadCreate should fail when internal thread registration fails") != 0)
    {
        ENT_ThreadClose(handle);
        return 1;
    }

    if(expect_true(tid == NULL,
                   "ENT_ThreadCreate should clear tid when registration fails after thread creation") != 0)
    {
        ENT_ThreadClose(handle);
        return 1;
    }

    if(expect_true(s_callback_calls == 0,
                   "registration failure must not invoke the user callback") != 0)
    {
        ENT_ThreadClose(handle);
        return 1;
    }

#ifdef WIN32
    if(expect_true(s_wrapper_calls == 1,
                   "registration failure should execute the internal wrapper") != 0)
    {
        ENT_ThreadClose(handle);
        return 1;
    }
#endif

#ifdef WIN32
    if(expect_true(s_wait_calls == 1,
                   "Windows create failure path should wait for the created thread before discarding it") != 0)
    {
        ENT_ThreadClose(handle);
        return 1;
    }

    if(expect_true(s_close_calls == 1,
                   "Windows create failure path should close the created thread handle") != 0)
    {
        ENT_ThreadClose(handle);
        return 1;
    }
#else
    if(expect_true(s_join_calls == 1,
                   "POSIX create failure path should join the created thread before discarding it") != 0)
    {
        ENT_ThreadClose(handle);
        return 1;
    }
#endif

    s_dll_ins_fail = 0;
    return expect_true(ENT_ThreadClose(handle) == ENT_SYS_NORMAL,
                       "ENT_ThreadClose should still release the thread context after create failure cleanup");
}

int main(void)
{
    int failures = 0;
    failures += test_thread_init_releases_lock_when_cv_init_fails();
    failures += test_thread_create_reclaims_created_thread_if_registration_fails();
    if(failures != 0)
    {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
