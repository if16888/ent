#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#include "ient_comm.h"
#include "ent_log.h"

/* Linked against the split log sources from the test CMake target. */

ENT_CTX gEntCtx;

typedef struct
{
    ENT_LOG log_handle;
    int iterations;
    int worker_id;
    int failures;
#ifdef WIN32
    HANDLE thread;
#else
    pthread_t thread;
#endif
} PERF_LOG_WORKER;

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

static int build_temp_log_dir(char* path, size_t path_len)
{
#ifdef WIN32
    char temp_root[MAX_PATH];
    DWORD len = GetTempPathA((DWORD)sizeof(temp_root), temp_root);

    if(len == 0 || len >= sizeof(temp_root))
    {
        return -1;
    }

    while(len > 0 && (temp_root[len - 1] == '\\' || temp_root[len - 1] == '/'))
    {
        temp_root[--len] = '\0';
    }

    if(snprintf(path,
                path_len,
                "%s%sperf_ent_log_%lu_%lu",
                temp_root,
                ENT_FILE_SEP,
                (unsigned long)GetCurrentProcessId(),
                (unsigned long)GetTickCount()) >= (int)path_len)
    {
        return -1;
    }
#else
    if(snprintf(path,
                path_len,
                "/tmp/perf_ent_log_%ld_%ld",
                (long)getpid(),
                (long)time(NULL)) >= (int)path_len)
    {
        return -1;
    }
#endif

    return 0;
}

#ifdef WIN32
static DWORD WINAPI perf_log_worker_main(LPVOID data)
#else
static void* perf_log_worker_main(void* data)
#endif
{
    PERF_LOG_WORKER* worker = (PERF_LOG_WORKER*)data;

    for(int i = 0; i < worker->iterations; ++i)
    {
        if(ENT_LogPrint(worker->log_handle, "worker=%d line=%d\n", worker->worker_id, i) != 0)
        {
            worker->failures++;
        }
    }

#ifdef WIN32
    return 0;
#else
    return NULL;
#endif
}

static int run_single_thread_benchmark(ENT_LOG log_handle, int iterations)
{
    double start_ms = now_ms();
    double elapsed_ms = 0.0;

    for(int i = 0; i < iterations; ++i)
    {
        if(ENT_LogPrint(log_handle, "single line=%d\n", i) != 0)
        {
            return EXIT_FAILURE;
        }
    }

    elapsed_ms = now_ms() - start_ms;
    printf("log_single lines=%d elapsed_ms=%.3f lines_per_sec=%.3f\n",
           iterations,
           elapsed_ms,
           iterations / (elapsed_ms / 1000.0));
    return EXIT_SUCCESS;
}

static int run_multi_thread_benchmark(ENT_LOG log_handle, int workers, int iterations_per_worker)
{
    PERF_LOG_WORKER* worker_ctx = NULL;
    double start_ms = 0.0;
    double elapsed_ms = 0.0;
    int total_lines = workers * iterations_per_worker;
    int failures = 0;

    worker_ctx = (PERF_LOG_WORKER*)calloc((size_t)workers, sizeof(PERF_LOG_WORKER));
    if(worker_ctx == NULL)
    {
        return EXIT_FAILURE;
    }

    start_ms = now_ms();
    for(int i = 0; i < workers; ++i)
    {
        worker_ctx[i].log_handle = log_handle;
        worker_ctx[i].iterations = iterations_per_worker;
        worker_ctx[i].worker_id = i;

#ifdef WIN32
        {
            worker_ctx[i].thread = CreateThread(NULL, 0, perf_log_worker_main, &worker_ctx[i], 0, NULL);
            if(worker_ctx[i].thread == NULL)
            {
                free(worker_ctx);
                return EXIT_FAILURE;
            }
        }
#else
        {
            if(pthread_create(&worker_ctx[i].thread, NULL, perf_log_worker_main, &worker_ctx[i]) != 0)
            {
                free(worker_ctx);
                return EXIT_FAILURE;
            }
        }
#endif
    }

    for(int i = 0; i < workers; ++i)
    {
#ifdef WIN32
        WaitForSingleObject(worker_ctx[i].thread, INFINITE);
        CloseHandle(worker_ctx[i].thread);
#else
        pthread_join(worker_ctx[i].thread, NULL);
#endif
        failures += worker_ctx[i].failures;
    }

    elapsed_ms = now_ms() - start_ms;
    printf("log_multi workers=%d lines=%d elapsed_ms=%.3f lines_per_sec=%.3f failures=%d\n",
           workers,
           total_lines,
           elapsed_ms,
           total_lines / (elapsed_ms / 1000.0),
           failures);

    free(worker_ctx);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

static int run_single_thread_benchmark_named(ENT_LOG log_handle, int iterations, const char* label)
{
    double start_ms = now_ms();
    double elapsed_ms = 0.0;

    for(int i = 0; i < iterations; ++i)
    {
        if(ENT_LogPrint(log_handle, "%s single line=%d\n", label, i) != 0)
        {
            return EXIT_FAILURE;
        }
    }

    elapsed_ms = now_ms() - start_ms;
    printf("%s_single lines=%d elapsed_ms=%.3f lines_per_sec=%.3f\n",
           label,
           iterations,
           elapsed_ms,
           iterations / (elapsed_ms / 1000.0));
    return EXIT_SUCCESS;
}

static int run_multi_thread_benchmark_named(ENT_LOG log_handle,
                                            int workers,
                                            int iterations_per_worker,
                                            const char* label)
{
    PERF_LOG_WORKER* worker_ctx = NULL;
    double start_ms = 0.0;
    double elapsed_ms = 0.0;
    int total_lines = workers * iterations_per_worker;
    int failures = 0;

    worker_ctx = (PERF_LOG_WORKER*)calloc((size_t)workers, sizeof(PERF_LOG_WORKER));
    if(worker_ctx == NULL)
    {
        return EXIT_FAILURE;
    }

    start_ms = now_ms();
    for(int i = 0; i < workers; ++i)
    {
        worker_ctx[i].log_handle = log_handle;
        worker_ctx[i].iterations = iterations_per_worker;
        worker_ctx[i].worker_id = i;

#ifdef WIN32
        worker_ctx[i].thread = CreateThread(NULL, 0, perf_log_worker_main, &worker_ctx[i], 0, NULL);
        if(worker_ctx[i].thread == NULL)
        {
            free(worker_ctx);
            return EXIT_FAILURE;
        }
#else
        if(pthread_create(&worker_ctx[i].thread, NULL, perf_log_worker_main, &worker_ctx[i]) != 0)
        {
            free(worker_ctx);
            return EXIT_FAILURE;
        }
#endif
    }

    for(int i = 0; i < workers; ++i)
    {
#ifdef WIN32
        WaitForSingleObject(worker_ctx[i].thread, INFINITE);
        CloseHandle(worker_ctx[i].thread);
#else
        pthread_join(worker_ctx[i].thread, NULL);
#endif
        failures += worker_ctx[i].failures;
    }

    elapsed_ms = now_ms() - start_ms;
    printf("%s_multi workers=%d lines=%d elapsed_ms=%.3f lines_per_sec=%.3f failures=%d\n",
           label,
           workers,
           total_lines,
           elapsed_ms,
           total_lines / (elapsed_ms / 1000.0),
           failures);

    free(worker_ctx);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

int main(void)
{
    enum { SINGLE_LINES = 5000, MULTI_WORKERS = 4, MULTI_LINES_PER_WORKER = 2000 };
    char log_dir[512];
    char module_name[64];
    ENT_LOG log_handle = NULL;
    ENT_LOG_LEV_E level = LOG_LEV_INFO_E;
    bool buffered = true;

    memset(log_dir, 0, sizeof(log_dir));
    memset(module_name, 0, sizeof(module_name));

    if(build_temp_log_dir(log_dir, sizeof(log_dir)) != 0)
    {
        return EXIT_FAILURE;
    }

#ifdef WIN32
    if(snprintf(module_name, sizeof(module_name), "PerfLog%lu", (unsigned long)GetCurrentProcessId()) >= (int)sizeof(module_name))
    {
        return EXIT_FAILURE;
    }
#else
    if(snprintf(module_name, sizeof(module_name), "PerfLog%ld", (long)getpid()) >= (int)sizeof(module_name))
    {
        return EXIT_FAILURE;
    }
#endif

    if(ENT_LogInit() != 0)
    {
        return EXIT_FAILURE;
    }

    if(ENT_LogInitHandle(&log_handle, module_name, log_dir) != 0)
    {
        ENT_LogClose();
        return EXIT_FAILURE;
    }

    if(ENT_LogSetOption(log_handle, ENT_LOG_LEVEL_E, &level) != 0)
    {
        ENT_LogCloseHandle(log_handle);
        ENT_LogClose();
        return EXIT_FAILURE;
    }

    if(run_single_thread_benchmark(log_handle, SINGLE_LINES) != EXIT_SUCCESS)
    {
        ENT_LogCloseHandle(log_handle);
        ENT_LogClose();
        return EXIT_FAILURE;
    }

    if(run_multi_thread_benchmark(log_handle, MULTI_WORKERS, MULTI_LINES_PER_WORKER) != EXIT_SUCCESS)
    {
        ENT_LogCloseHandle(log_handle);
        ENT_LogClose();
        return EXIT_FAILURE;
    }

    if(ENT_LogSetOption(log_handle, ENT_LOG_BUFFER_E, &buffered) != 0)
    {
        ENT_LogCloseHandle(log_handle);
        ENT_LogClose();
        return EXIT_FAILURE;
    }

    if(run_single_thread_benchmark_named(log_handle, SINGLE_LINES, "log_buffered") != EXIT_SUCCESS)
    {
        ENT_LogCloseHandle(log_handle);
        ENT_LogClose();
        return EXIT_FAILURE;
    }

    if(run_multi_thread_benchmark_named(log_handle, MULTI_WORKERS, MULTI_LINES_PER_WORKER, "log_buffered") != EXIT_SUCCESS)
    {
        ENT_LogCloseHandle(log_handle);
        ENT_LogClose();
        return EXIT_FAILURE;
    }

    if(ENT_LogCloseHandle(log_handle) != 0)
    {
        ENT_LogClose();
        return EXIT_FAILURE;
    }

    if(ENT_LogClose() != 0)
    {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
