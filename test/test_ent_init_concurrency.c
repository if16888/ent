#include "ent_init.h"
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
static DWORD WINAPI run_worker(void* data)
{
    return (DWORD)ENT_Run((ENT_HANDLE)data);
}
#else
#include <pthread.h>
static void* run_worker(void* data)
{
    ENT_Run((ENT_HANDLE)data);
    return NULL;
}
#endif

#ifdef __linux__
#include <sched.h>
#include <errno.h>
#include "ient_runtime.h"

typedef struct RT_TEST_GATE
{
    pthread_mutex_t lock;
    pthread_cond_t cv;
    int ready;
    int start;
    ENT_HANDLE handle;
} RT_TEST_GATE;

typedef struct RT_SETTER_PROBE
{
    RT_TEST_GATE* gate;
    int cpu;
    int failed;
} RT_SETTER_PROBE;

static void* rt_setter(void* data)
{
    RT_SETTER_PROBE* probe = (RT_SETTER_PROBE*)data;
    RT_TEST_GATE* gate = probe->gate;
    ENT_HANDLE handle;
    int i;

    pthread_mutex_lock(&gate->lock);
    gate->ready++;
    pthread_cond_broadcast(&gate->cv);
    while(!gate->start)
    {
        pthread_cond_wait(&gate->cv, &gate->lock);
    }
    handle = gate->handle;
    pthread_mutex_unlock(&gate->lock);
    if(handle == NULL)
    {
        return NULL;
    }
    for(i = 0; i < 100; i++)
    {
        struct sched_param params;
        int policy;
        if(ENT_SetRtAttributes(handle, probe->cpu, ENT_RT_POLICY_OTHER_E, 0) != ENT_SYS_NORMAL ||
           pthread_getschedparam(pthread_self(), &policy, &params) != 0 ||
           policy != SCHED_OTHER || params.sched_priority != 0)
        {
            probe->failed = 1;
            break;
        }
    }
    return NULL;
}

/* Failed CPU validation must not alter calling-thread native affinity.
 * RT attributes refer to pthread_self(), not an arbitrary runtime worker. */
static int test_invalid_cpu_preserves_native_affinity(void)
{
    ENT_HANDLE handle = NULL;
    cpu_set_t before, after;
    MSG_ID_T status;
    int failed = 0;
    if(pthread_getaffinity_np(pthread_self(), sizeof(before), &before) != 0)
        return 1;
    if(ENT_Init(&handle, "rt_invalid_cpu", ".", LOG_LEV_ERROR_E,
                ENT_MODE_REALTIME_E) != ENT_SYS_NORMAL)
        return 1;

    status = ENT_SetRtAttributes(handle, CPU_SETSIZE, ENT_RT_POLICY_OTHER_E, 0);
    if(status != ENT_RT_BAD_CPU)
        failed = 1;
    if(pthread_getaffinity_np(pthread_self(), sizeof(after), &after) != 0 ||
       !CPU_EQUAL(&before, &after))
        failed = 1;
    if(((ENT_HANDLE_CTX_T*)handle)->ctx.rtLastError != EINVAL)
        failed = 1;
    if(ENT_Close(&handle) != ENT_SYS_NORMAL || handle != NULL)
        failed = 1;
    if(failed)
        fprintf(stderr, "invalid CPU must leave native affinity unchanged and save EINVAL\\n");
    return failed;
}

static int test_concurrent_rt_setters(void)
{
    RT_TEST_GATE gate;
    int round;
    int failed = 0;

    if(pthread_mutex_init(&gate.lock, NULL) != 0)
    {
        return 1;
    }
    if(pthread_cond_init(&gate.cv, NULL) != 0)
    {
        pthread_mutex_destroy(&gate.lock);
        return 1;
    }
    for(round = 0; round < 50; round++)
    {
        pthread_t threads[2];
        RT_SETTER_PROBE probes[2] = {{&gate, -1, 0}, {&gate, -2, 0}};
        ENT_HANDLE handle = NULL;
        MSG_ID_T initStatus = ENT_SYS_NORMAL;
        int created = 0;
        int i;

        gate.ready = 0;
        gate.start = 0;
        gate.handle = NULL;
        for(i = 0; i < 2; i++)
        {
            if(pthread_create(&threads[i], NULL, rt_setter, &probes[i]) != 0)
            {
                failed = 1;
                break;
            }
            created++;
        }
        if(created == 2)
        {
            pthread_mutex_lock(&gate.lock);
            while(gate.ready != 2)
            {
                pthread_cond_wait(&gate.cv, &gate.lock);
            }
            pthread_mutex_unlock(&gate.lock);
            /* Start workers first: a successful MCL_FUTURE must not prevent
             * thread creation. OTHER/-1 needs no realtime privileges. */
            initStatus = ENT_Init(&handle, "rt_setters", ".", LOG_LEV_ERROR_E, ENT_MODE_REALTIME_E);
            if(initStatus != ENT_SYS_NORMAL)
            {
                failed = 1;
            }
        }
        pthread_mutex_lock(&gate.lock);
        gate.handle = handle;
        gate.start = 1;
        pthread_cond_broadcast(&gate.cv);
        pthread_mutex_unlock(&gate.lock);
        for(i = 0; i < created; i++)
        {
            pthread_join(threads[i], NULL);
            failed |= probes[i].failed;
        }
        if(handle != NULL)
        {
            ENT_CTX* ctx = &((ENT_HANDLE_CTX_T*)handle)->ctx;
            if((ctx->rtCpu != -1 && ctx->rtCpu != -2) ||
               ctx->rtPolicy != ENT_RT_POLICY_OTHER_E || ctx->rtPriority != 0 || ctx->rtLastError != 0)
            {
                failed = 1;
            }
            if(ENT_Close(&handle) != ENT_SYS_NORMAL || handle != NULL)
            {
                failed = 1;
            }
        }
        if(failed)
        {
            fprintf(stderr, "concurrent RT setters failed at round %d (init=%d)\n", round, (int)initStatus);
            break;
        }
    }
    pthread_cond_destroy(&gate.cv);
    pthread_mutex_destroy(&gate.lock);
    return failed;
}
#endif

int main(void)
{
    ENT_HANDLE handle = NULL;

    if(ENT_Init(&handle, "tsan", ".", LOG_LEV_WARN_E, ENT_MODE_NORMAL_E) != ENT_SYS_NORMAL)
    {
        return 1;
    }

#ifdef _WIN32
    {
        HANDLE thread = CreateThread(NULL, 0, run_worker, handle, 0, NULL);
        if(thread == NULL)
        {
            ENT_Close(&handle);
            return 1;
        }
        Sleep(20);
        ENT_Stop(handle);
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
    }
#else
    {
        pthread_t thread;
        if(pthread_create(&thread, NULL, run_worker, handle) != 0)
        {
            ENT_Close(&handle);
            return 1;
        }
        ENT_Stop(handle);
        pthread_join(thread, NULL);
    }
#endif

    if(ENT_Close(&handle) != ENT_SYS_NORMAL || handle != NULL)
    {
        return 1;
    }
#ifdef __linux__
    return test_invalid_cpu_preserves_native_affinity() | test_concurrent_rt_setters();
#else
    return 0;
#endif
}
