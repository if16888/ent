#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif

#include "ient_comm.h"
#include "ient_runtime.h"
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
    UTL_LOCK done_lock;
    UTL_CV done_cv;
} PERF_TPOOL_PROBE;

static double now_ms(void)
{
#ifdef _WIN32
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
    struct timespec ts;

    if(clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return -1.0;
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
#endif
}

static void perf_probe_mark_finished(PERF_TPOOL_PROBE* probe)
{
#ifdef _WIN32
    InterlockedIncrement((volatile LONG*)&probe->finished);
#else
    __sync_add_and_fetch(&probe->finished, 1);
#endif

    UTL_LockEnter(probe->done_lock);
    UTL_CVWakeAll(probe->done_cv);
    UTL_LockLeave(probe->done_lock);
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

int main(int argc, char** argv)
{
    enum { TASKS = 1000, WORKERS = 4, WAIT_LIMIT_MS = 10000 };
    UTL_TPOOL pool = NULL;
    PERF_TPOOL_PROBE probe;
    MSG_ID_T retVals[TASKS];
    double startMs = 0.0;
    double elapsedMs = 0.0;
    int submitted = 0;
    int submitFailed = 0;
    int closed = 0;
    int completed = 0;
    int selfTest = argc == 2 && strcmp(argv[1], "--test-submit-failure") == 0;

    if(argc > 2 || (argc == 2 && !selfTest)) return EXIT_FAILURE;
    memset(&probe, 0, sizeof(probe));
    memset(retVals, 0, sizeof(retVals));

    if(UTL_LockInit(&probe.done_lock, "perf_tpool_done") != ENT_SYS_NORMAL)
        return EXIT_FAILURE;
    if(UTL_CVInit(&probe.done_cv, "perf_tpool_done") != ENT_SYS_NORMAL)
    {
        UTL_LockClose(&probe.done_lock);
        return EXIT_FAILURE;
    }

    if(UTL_TPoolInit(&pool, WORKERS) != ENT_SYS_NORMAL)
    {
        UTL_CVClose(&probe.done_cv);
        UTL_LockClose(&probe.done_lock);
        return EXIT_FAILURE;
    }

    startMs = now_ms();
    if(startMs < 0.0)
    {
        UTL_TPoolClose(&pool);
        UTL_CVClose(&probe.done_cv);
        UTL_LockClose(&probe.done_lock);
        return EXIT_FAILURE;
    }

    for(int i = 0; i < TASKS; ++i)
    {
        /* Deliberately stop after the first accepted task to exercise
         * cleanup when the producer cannot submit its remaining work. */
        if(selfTest && i == 1)
        {
            submitFailed = 1;
            break;
        }
        if(UTL_TPoolAddTask(pool, perf_task_cb, perf_task_end_cb,
                            &probe, &retVals[i]) != ENT_SYS_NORMAL)
        {
            submitFailed = 1;
            break;
        }
        submitted++;
    }

    /* The callback owns references to probe and retVals. Always drain the
     * tasks actually accepted before destroying their storage. */
    UTL_LockEnter(probe.done_lock);
    while(probe.finished < submitted)
    {
        MSG_ID_T waitStatus;
        if(now_ms() - startMs >= WAIT_LIMIT_MS)
        {
            fprintf(stderr, "perf_utl_tpool: completion timed out (%d/%d)\n",
                    probe.finished, submitted);
            fflush(stderr);
            _Exit(EXIT_FAILURE); /* Do not free storage still owned by workers. */
        }
        waitStatus = UTL_CVWaitFor(probe.done_cv, probe.done_lock, 50, RW_WRITE_E);
        if(waitStatus != ENT_SYS_NORMAL && waitStatus != ENT_UTHD_WAIT_TIMEOUT)
        {
            fprintf(stderr, "perf_utl_tpool: completion wait failed (%d)\n", waitStatus);
            fflush(stderr);
            _Exit(EXIT_FAILURE);
        }
    }
    completed = probe.finished;
    UTL_LockLeave(probe.done_lock);

    elapsedMs = now_ms() - startMs;
    closed = UTL_TPoolClose(&pool) == ENT_SYS_NORMAL;
    UTL_CVClose(&probe.done_cv);
    UTL_LockClose(&probe.done_lock);

    if(selfTest)
    {
        /* The injected failure is expected, but all prior work must finish
         * and the worker pool must close successfully. */
        return submitFailed && submitted == 1 && completed == 1 && closed
            ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if(submitFailed || !closed || completed != TASKS || elapsedMs < 0.0)
        return EXIT_FAILURE;

    printf("tpool workers=%d tasks=%d elapsed_ms=%.3f tasks_per_sec=%.3f\n",
           WORKERS, TASKS, elapsedMs,
           elapsedMs > 0.0 ? TASKS / (elapsedMs / 1000.0) : 0.0);
    return EXIT_SUCCESS;
}
