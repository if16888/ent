#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <sys/time.h>
#endif

#include "ient_comm.h"
#include "ent_utility.h"

ENT_CTX gEntCtx;

MSG_ID_T ENT_LogInit(void) { return 0; }
MSG_ID_T ENT_LogClose(void) { return 0; }
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
MSG_ID_T ENT_LogCloseHandle(ENT_LOG logHandle) { (void)logHandle; return 0; }
MSG_ID_T ENT_LogRaw(ENT_LOG logHandle, const char* format, ...) { (void)logHandle; (void)format; return 0; }
MSG_ID_T ENT_LogFatal(ENT_LOG logHandle, const char* format, ...) { (void)logHandle; (void)format; return 0; }
MSG_ID_T ENT_LogError(ENT_LOG logHandle, const char* format, ...) { (void)logHandle; (void)format; return 0; }
MSG_ID_T ENT_LogWarn(ENT_LOG logHandle, const char* format, ...) { (void)logHandle; (void)format; return 0; }
MSG_ID_T ENT_LogPrint(ENT_LOG logHandle, const char* format, ...) { (void)logHandle; (void)format; return 0; }
MSG_ID_T ENT_LogDebug(ENT_LOG logHandle, const char* format, ...) { (void)logHandle; (void)format; return 0; }

typedef struct
{
    volatile int finished;
    int total_tasks;
#ifdef WIN32
    HANDLE done_event;
#else
    pthread_mutex_t done_mutex;
    pthread_cond_t  done_cv;
#endif
} PERF_TPOOL_PROBE;

static double now_ms(void)
{
#ifdef WIN32
    static LARGE_INTEGER frequency;
    static int frequency_initialized = 0;
    LARGE_INTEGER counter;

    if(!frequency_initialized)
    {
        QueryPerformanceFrequency(&frequency);
        frequency_initialized = 1;
    }

    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart * 1000.0 / (double)frequency.QuadPart;
#else
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
#endif
}

static void perf_probe_mark_finished(PERF_TPOOL_PROBE* probe)
{
    int finished = 0;

#ifdef WIN32
    finished = (int)InterlockedIncrement((volatile LONG*)&probe->finished);
    if(finished >= probe->total_tasks)
    {
        SetEvent(probe->done_event);
    }
#else
    pthread_mutex_lock(&probe->done_mutex);
    finished = __sync_add_and_fetch(&probe->finished, 1);
    if(finished >= probe->total_tasks)
    {
        pthread_cond_signal(&probe->done_cv);
    }
    pthread_mutex_unlock(&probe->done_mutex);
#endif
}

static MSG_ID_T perf_task_cb(void* data)
{
    (void)data;
    return 0;
}

static MSG_ID_T perf_task_end_cb(void* data, MSG_ID_T* retVal)
{
    PERF_TPOOL_PROBE* probe = (PERF_TPOOL_PROBE*)data;

    (void)retVal;
    perf_probe_mark_finished(probe);
    return 0;
}

int main(void)
{
    enum { TASKS = 1000, WORKERS = 4 };
    UTL_TPOOL pool = NULL;
    PERF_TPOOL_PROBE probe;
    MSG_ID_T retVals[TASKS];
    double startMs = 0.0;
    double elapsedMs = 0.0;

    memset(&probe, 0, sizeof(probe));
    memset(retVals, 0, sizeof(retVals));
    probe.total_tasks = TASKS;

#ifdef WIN32
    probe.done_event = CreateEvent(NULL, TRUE, FALSE, NULL);
    if(probe.done_event == NULL) return EXIT_FAILURE;
#else
    if(pthread_mutex_init(&probe.done_mutex, NULL) != 0) return EXIT_FAILURE;
    if(pthread_cond_init(&probe.done_cv, NULL) != 0)
    {
        pthread_mutex_destroy(&probe.done_mutex);
        return EXIT_FAILURE;
    }
#endif

    if(UTL_TPoolInit(&pool, WORKERS) != 0) return EXIT_FAILURE;

    startMs = now_ms();
    for(int i = 0; i < TASKS; ++i)
    {
        if(UTL_TPoolAddTask(pool, perf_task_cb, perf_task_end_cb, &probe, &retVals[i]) != 0)
        {
            return EXIT_FAILURE;
        }
    }

#ifdef WIN32
    WaitForSingleObject(probe.done_event, INFINITE);
#else
    pthread_mutex_lock(&probe.done_mutex);
    while(probe.finished < TASKS)
    {
        pthread_cond_wait(&probe.done_cv, &probe.done_mutex);
    }
    pthread_mutex_unlock(&probe.done_mutex);
#endif

    elapsedMs = now_ms() - startMs;

    if(UTL_TPoolClose(pool) != 0) return EXIT_FAILURE;

#ifdef WIN32
    CloseHandle(probe.done_event);
#else
    pthread_cond_destroy(&probe.done_cv);
    pthread_mutex_destroy(&probe.done_mutex);
#endif

    printf("tpool workers=%d tasks=%d elapsed_ms=%.3f tasks_per_sec=%.3f\n",
           WORKERS,
           TASKS,
           elapsedMs,
           TASKS / (elapsedMs / 1000.0));

    return EXIT_SUCCESS;
}
