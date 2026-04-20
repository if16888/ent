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
#include "ent_msg.h"
#include "ent_thread.h"
#include "ent_utility.h"

ENT_CTX gEntCtx;

static int s_use_real_threads = 0;

typedef struct
{
#ifdef WIN32
    HANDLE thread;
#else
    pthread_t thread;
#endif
    int joined;
} TEST_THREAD_ID;

#ifdef WIN32
typedef struct
{
    PTHREAD_START_ROUTINE thProc;
    void* thData;
} TEST_THREAD_START_DATA;

static DWORD WINAPI test_thread_start(LPVOID data)
{
    TEST_THREAD_START_DATA* startData = (TEST_THREAD_START_DATA*)data;
    PTHREAD_START_ROUTINE thProc = startData->thProc;
    void* thData = startData->thData;
    free(startData);
    (void)thProc(thData);
    return 0;
}
#endif

typedef struct
{
    volatile int task_started;
    volatile int task_hits;
} TPOOL_LIFECYCLE_PROBE;

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

MSG_ID_T ENT_ThreadInit(ENT_THREAD* pthHandle)
{
    if(pthHandle == NULL)
    {
        return -1;
    }
    *pthHandle = malloc(1);
    if(*pthHandle == NULL)
    {
        return -2;
    }
    return 0;
}

MSG_ID_T ENT_ThreadDetachCreate(ENT_THREAD handle, PTHREAD_START_ROUTINE thProc, void* thData)
{
    (void)handle;
    (void)thProc;
    (void)thData;
    return 0;
}

MSG_ID_T ENT_ThreadCreate(ENT_THREAD_ID* tid, ENT_THREAD handle, PTHREAD_START_ROUTINE thProc, void* thData)
{
    TEST_THREAD_ID* threadId = NULL;
    (void)handle;
    if(tid != NULL)
    {
        *tid = NULL;
    }
    if(tid == NULL)
    {
        return -1;
    }
    if(!s_use_real_threads)
    {
        *tid = malloc(1);
        return (*tid == NULL) ? -6 : 0;
    }
    threadId = (TEST_THREAD_ID*)calloc(1, sizeof(TEST_THREAD_ID));
    if(threadId == NULL)
    {
        return -6;
    }
#ifdef WIN32
    {
        TEST_THREAD_START_DATA* startData = (TEST_THREAD_START_DATA*)calloc(1, sizeof(TEST_THREAD_START_DATA));
        if(startData == NULL)
        {
            free(threadId);
            return -6;
        }
        startData->thProc = thProc;
        startData->thData = thData;
        threadId->thread = CreateThread(NULL, 0, test_thread_start, startData, 0, NULL);
        if(threadId->thread == NULL)
        {
            free(startData);
            free(threadId);
            return -7;
        }
    }
#else
    if(pthread_create(&threadId->thread, NULL, thProc, thData) != 0)
    {
        free(threadId);
        return -7;
    }
#endif
    *tid = (ENT_THREAD_ID)threadId;
    return 0;
}

MSG_ID_T ENT_ThreadWaitById(ENT_THREAD_ID* tid, ENT_THREAD handle, int ms)
{
    TEST_THREAD_ID* threadId = NULL;
    (void)handle;
    (void)ms;
    if(tid != NULL && *tid != NULL)
    {
        if(!s_use_real_threads)
        {
            free(*tid);
        }
        else
        {
            threadId = (TEST_THREAD_ID*)(*tid);
            if(!threadId->joined)
            {
#ifdef WIN32
                WaitForSingleObject(threadId->thread, INFINITE);
                CloseHandle(threadId->thread);
#else
                pthread_join(threadId->thread, NULL);
#endif
                threadId->joined = 1;
            }
            free(threadId);
        }
        *tid = NULL;
    }
    return 0;
}

MSG_ID_T ENT_ThreadClose(ENT_THREAD handle)
{
    free(handle);
    return 0;
}

MSG_ID_T UTL_Sleep(int ms)
{
    if(ms > 0)
    {
#ifdef WIN32
        Sleep((DWORD)ms);
#else
        usleep((useconds_t)ms * 1000U);
#endif
    }
    return 0;
}

static MSG_ID_T slow_task_cb(void* data)
{
    TPOOL_LIFECYCLE_PROBE* probe = (TPOOL_LIFECYCLE_PROBE*)data;
    probe->task_started = 1;
    UTL_Sleep(80);
    probe->task_hits++;
    return 9;
}

#ifdef WIN32
static DWORD WINAPI close_pool_thread(LPVOID data)
#else
static void* close_pool_thread(void* data)
#endif
{
    UTL_TPOOL pool = (UTL_TPOOL)data;
    (void)UTL_TPoolClose(pool);
#ifdef WIN32
    return 0;
#else
    return NULL;
#endif
}

static int test_tpool_add_task_is_rejected_once_close_starts(void)
{
    UTL_TPOOL pool = NULL;
    TPOOL_LIFECYCLE_PROBE probe;
    MSG_ID_T retVal = -1;
    MSG_ID_T addSts = ENT_SYS_NORMAL;
    int sawReject = 0;
    int i = 0;

    memset(&probe, 0, sizeof(probe));
    s_use_real_threads = 1;

    if(expect_true(UTL_TPoolInit(&pool, 1) == ENT_SYS_NORMAL,
                   "UTL_TPoolInit should create a worker for lifecycle race test") != 0)
    {
        s_use_real_threads = 0;
        return 1;
    }

    if(expect_true(UTL_TPoolAddTask(pool, slow_task_cb, NULL, &probe, &retVal) == ENT_SYS_NORMAL,
                   "UTL_TPoolAddTask should queue the initial slow task") != 0)
    {
        UTL_TPoolClose(pool);
        s_use_real_threads = 0;
        return 1;
    }

    for(i = 0; i < 50 && probe.task_started == 0; ++i)
    {
        UTL_Sleep(5);
    }

    if(expect_true(probe.task_started == 1,
                   "slow task should start before close/add race is exercised") != 0)
    {
        UTL_TPoolClose(pool);
        s_use_real_threads = 0;
        return 1;
    }

#ifdef WIN32
    {
        HANDLE th = CreateThread(NULL, 0, close_pool_thread, pool, 0, NULL);
        if(expect_true(th != NULL, "close helper thread should start on Windows") != 0)
        {
            UTL_TPoolClose(pool);
            s_use_real_threads = 0;
            return 1;
        }
        for(i = 0; i < 200; ++i)
        {
            addSts = UTL_TPoolAddTask(pool, slow_task_cb, NULL, &probe, &retVal);
            if(addSts == ENT_TPL_NOT_INITIALIZED)
            {
                sawReject = 1;
                break;
            }
            UTL_Sleep(1);
        }
        WaitForSingleObject(th, INFINITE);
        CloseHandle(th);
    }
#else
    {
        pthread_t th;
        if(expect_true(pthread_create(&th, NULL, close_pool_thread, pool) == 0,
                       "close helper thread should start on POSIX") != 0)
        {
            UTL_TPoolClose(pool);
            s_use_real_threads = 0;
            return 1;
        }
        for(i = 0; i < 200; ++i)
        {
            addSts = UTL_TPoolAddTask(pool, slow_task_cb, NULL, &probe, &retVal);
            if(addSts == ENT_TPL_NOT_INITIALIZED)
            {
                sawReject = 1;
                break;
            }
            UTL_Sleep(1);
        }
        pthread_join(th, NULL);
    }
#endif

    s_use_real_threads = 0;
    return expect_true(sawReject == 1,
                       "UTL_TPoolAddTask should be rejected once close has started");
}

int main(void)
{
    int failures = 0;
    failures += test_tpool_add_task_is_rejected_once_close_starts();
    if(failures != 0)
    {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
