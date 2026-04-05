#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

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
    double stamps[256];
    int count;
} PERF_TIMER_PROBE;

static double now_ms(void)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
}

static void* perf_timer_cb(void* data)
{
    PERF_TIMER_PROBE* probe = (PERF_TIMER_PROBE*)data;

    if(probe->count < (int)(sizeof(probe->stamps) / sizeof(probe->stamps[0])))
    {
        probe->stamps[probe->count++] = now_ms();
    }

    return NULL;
}

int main(void)
{
    UTL_TIMER_T timer = NULL;
    PERF_TIMER_PROBE probe;
    double minDelta = 0.0;
    double maxDelta = 0.0;
    double totalDelta = 0.0;

    memset(&probe, 0, sizeof(probe));

    if(UTL_TimerInit() != 0) return EXIT_FAILURE;
    if(UTL_TimerCreate(&timer, UTL_TIMER_E_PERIOD, 20, perf_timer_cb, &probe) != 0) return EXIT_FAILURE;
    UTL_Sleep(500);
    if(UTL_TimerDelete(&timer) != 0) return EXIT_FAILURE;
    if(UTL_TimerClose() != 0) return EXIT_FAILURE;
    if(probe.count < 2) return EXIT_FAILURE;

    minDelta = probe.stamps[1] - probe.stamps[0];
    maxDelta = minDelta;

    for(int i = 1; i < probe.count; ++i)
    {
        double delta = probe.stamps[i] - probe.stamps[i - 1];

        if(delta < minDelta)
        {
            minDelta = delta;
        }
        if(delta > maxDelta)
        {
            maxDelta = delta;
        }
        totalDelta += delta;
    }

    printf("timer hits=%d avg_ms=%.3f min_ms=%.3f max_ms=%.3f\n",
           probe.count,
           totalDelta / (probe.count - 1),
           minDelta,
           maxDelta);

    return EXIT_SUCCESS;
}
