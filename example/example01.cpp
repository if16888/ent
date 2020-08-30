#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#ifdef __linux__
#include <unistd.h>
#elif defined(WIN32)
#include <tchar.h>
//#include <locale.h>
#endif
#include "ent_init.h"
#include "ent_log.h"
#include "ent_thread.h"
#include "ent_db.h"
#include "ent_utility.h"

#ifdef WIN32
DWORD WINAPI thread_func(void* data)
{
    char* cfg = (char*)data;
    for(int i=0;i<20;i++)
    {
        UTL_Sleep(3000);
        ENT_LOG_WARN("[%s]->[%d]\n",cfg,i);
    }
    
    return 0;
}
#else
void* thread_func(void* data)
{
    char* cfg = (char*)data;
    for(int i=0;i<20;i++)
    {
        UTL_Sleep(3000);
        printf("[%s] %ld,test %d\n",cfg,pthread_self(),i);
        ENT_LOG_WARN("[%s]->[%d]\n",cfg,i);
    }
    return NULL;
}
#endif

void* timer_func(void* data)
{
    char* cfg = (char*)data;
    ENT_LOG_WARN("data [%s]\n",cfg);
    return NULL;
}

void timer_exmaple()
{
    MSG_ID_T  sts;
    sts = UTL_TimerInit();

    UTL_TIMER_T tmr1 = NULL;
    UTL_TIMER_T tmr2 = NULL;
    UTL_TIMER_T tmr3 = NULL;
    UTL_TIMER_T tmr4 = NULL;
    sts = UTL_TimerCreate(&tmr1,UTL_TIMER_E_PERIOD,10000,timer_func,(void*)"timer1");
    if(sts<0)
    {
        ENT_LOG_ERROR("UTL_TimerCreate failed,return sts [%d].\n",sts);
    }
    sts = UTL_TimerCreate(&tmr2,UTL_TIMER_E_ONESHOT,5000,timer_func,(void*)"timer2");
    if(sts<0)
    {
        ENT_LOG_ERROR("UTL_TimerCreate failed,return sts [%d].\n",sts);
    }

    sts = UTL_TimerCreate(&tmr3,UTL_TIMER_E_PERIOD,7000,timer_func,(void*)"timer3");
    if(sts<0)
    {
        ENT_LOG_ERROR("UTL_TimerCreate failed,return sts [%d].\n",sts);
    }

    sts = UTL_TimerCreate(&tmr4,UTL_TIMER_E_PERIOD|UTL_TIMER_E_SIGNAL,2000,timer_func,(void*)"timer4");
    if(sts<0)
    {
        ENT_LOG_ERROR("UTL_TimerCreate failed,return sts [%d].\n",sts);
    }
}

ENT_THREAD  thHandle;
void thread_example()
{
    MSG_ID_T sts;

    sts = ENT_ThreadInit(&thHandle);
    if(sts<0)
    {
        ENT_LOG_FATAL("ENT_ThreadInit failed,sts [%d]\n",sts);    
    }
    sts = ENT_ThreadCreate(NULL,thHandle,thread_func,(void*)"no tid");
    if(sts<0)
    {
        ENT_LOG_ERROR("ENT_ThreadCreate failed,return sts [%d].\n",sts);
    }

    ENT_THREAD_ID tid;
    sts = ENT_ThreadCreate(&tid,thHandle,thread_func,(void*)"   tid");
    if(sts<0)
    {
        ENT_LOG_ERROR("ENT_ThreadCreate failed,return sts [%d].\n",sts);
    }

    while(1)
    {
        sts = ENT_ThreadWaitById(&tid,thHandle,500);
        if(sts == 0)
        {
            break;
        }
    }
}


MSG_ID_T  tp_task(void* data)
{
    int*  tmp = (int*)data;

    if(data==NULL)
    {
        return -1;
    }
    int i=0;
    while(i<5)
    {
        ENT_LOG_WARN("task [%03d] idx [%d]\n",tmp[0],i);
        UTL_Sleep(1000);
        i++;
    }
    return *tmp;
}

MSG_ID_T  tp_task_end(void* data,MSG_ID_T* retVal)
{
    int*  tmp = (int*)data;

    ENT_LOG_WARN("task [%03d] [%p] retVal [%03d]\n",*tmp,retVal,*retVal);
    if(retVal)
    {
        free(retVal);
    }
    return 1;
}

static UTL_TPOOL    tpHandle;
MSG_ID_T tpool_example_start()
{
    MSG_ID_T     sts = 0;
    
    sts = UTL_TPoolInit(&tpHandle,8);
    if(sts < 0)
    {
        ENT_LOG_ERROR("UTL_ThPoolInit failed,sts [%d]\n",sts);
        return -1;
    }

    MSG_ID_T* tmp    = NULL;
    int*      datas  = (int*)malloc(100*sizeof(int));
    for(int i=0;i<100;i++)
    {
        tmp  = (MSG_ID_T*)malloc(sizeof(MSG_ID_T));
        *tmp = 0xff;
        datas[i] = i+1;
        UTL_TPoolAddTask(tpHandle,tp_task,tp_task_end,&datas[i],tmp);
    } 
    return 0;   
}

MSG_ID_T tpool_example_stop()
{
    MSG_ID_T  sts = 0;
    sts = UTL_TPoolClose(tpHandle);
    if(sts < 0)
    {
        ENT_LOG_ERROR("UTL_TPoolClose failed,sts [%d]\n",sts);
    }
    return sts;
}

#ifdef WIN32
int _tmain(int argc,_TCHAR* argv[])
#else
int main(int argc,char* argv[])
#endif
{
    MSG_ID_T  sts=0;
    
    sts = ENT_Init("example",".",LOG_LEV_DEBUG_E);
    
    timer_exmaple();
    thread_example();

    tpool_example_start();
    UTL_Sleep(60*1000);
    tpool_example_stop();

    sts = ENT_ThreadClose(thHandle);
    if(sts < 0)
    {
        ENT_LOG_WARN("ENT_ThreadClose sts [%d]\n",sts);
    }
    sts = UTL_TimerClose();
    if(sts < 0)
    {
        ENT_LOG_WARN("UTL_TimerClose sts [%d]\n",sts);
    }
    sts = ENT_DbClose();
    if(sts < 0)
    {
        ENT_LOG_WARN("ENT_DbClose sts [%d]\n",sts);
    }
    sts = ENT_LogClose();
    if(sts < 0)
    {
        ENT_LOG_WARN("ENT_LogClose sts [%d]\n",sts);
    }
    return 0;
}
