#include "ent_init.h"
#include <stdio.h>

#ifdef WIN32
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

int main(void)
{
    ENT_HANDLE handle = NULL;

    if(ENT_Init(&handle, "tsan", ".", LOG_LEV_WARN_E, ENT_MODE_NORMAL_E) != ENT_SYS_NORMAL)
    {
        return 1;
    }

#ifdef WIN32
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

    return ENT_Close(&handle) == ENT_SYS_NORMAL && handle == NULL ? 0 : 1;
}
