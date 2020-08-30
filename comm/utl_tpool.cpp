/*-----------------------------------------------------------------------------
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *-----------------------------------------------------------------------------
 */
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include "ient_comm.h"
#include "ent_thread.h"
#include "ent_utility.h"

typedef enum
{
    TASK_E_TYPE_USER = 0,
    TASK_E_TYPE_QUIT,
    TASK_E_TYPE_PAUSE
}TASK_E_TYPE;

typedef struct 
{
    ENT_THREAD    thHandle;
    UTL_LOCK      taskLock;
    UTL_CV        taskEmptyCV;
    DLL_D_HDR     taskActiveHeader;
    DLL_D_HDR     taskFinishHeader;
    int           taskNum;
    int           threadNum;
    int           waitNum;
    DLL_D_HDR     threadHeader;
}UTL_TPOOL_CTX;

typedef struct
{
    DLL_D_HDR          dllLnk;
    UTL_TP_TASK_F      taskCb;
    UTL_TP_TASK_END_F  taskEndCb;
    void*              taskData;
    MSG_ID_T*          pRetVal;
}UTL_TPOOL_TASK;

typedef struct
{
    DLL_D_HDR         dllLnk;
    TASK_E_TYPE       taskType;
    time_t            taskStartTime;
    ENT_THREAD_ID     thId;
    UTL_TPOOL_CTX*    pool;  
}UTL_TPOOL_THREAD;

static int sPoolIdx;

#define DEFAULT_NUM   8
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :  iUTL_TPoolTaskPro
 *
 * DESCRIPTION :  
 *
 * COMPLETION
 * STATUS      :  0
 *
 *-----------------------------------------------------------------------------
 */
static DWORD iUTL_TPoolTaskPro(void* data)
{
    MSG_ID_T            sts = 0;
    UTL_TPOOL_THREAD*   thCtx = NULL;
    UTL_TPOOL_CTX*      poolCtx = NULL;
    UTL_TPOOL_TASK*     taskCtx = NULL;
    DLL_D_HDR*          tmp = NULL;
    BOOL                isEmpty = FALSE;
    if(data == NULL)
    {
        IENT_LOG_ERROR("arguments invalid\n");
        return -1;
    }

    thCtx = (UTL_TPOOL_THREAD*)data;
    poolCtx = thCtx->pool;

    do 
    {
        UTL_LockEnter(poolCtx->taskLock);
        while(tmp==NULL)
        {
            UTL_DllIsEmpty(&isEmpty,&poolCtx->taskActiveHeader);
            if(isEmpty)
            {
                IENT_LOG_DEBUG("task is empty\n");
                poolCtx->waitNum++;
                sts = UTL_CVWait(poolCtx->taskEmptyCV,poolCtx->taskLock,0,RW_WRITE_E);
                poolCtx->waitNum--;
            }
            if(thCtx->taskType==TASK_E_TYPE_PAUSE)
            {
                continue;
            }
            sts = UTL_DllRemTail(&poolCtx->taskActiveHeader,&tmp);
            if(sts < 0)
            {
                tmp = NULL;
            }
            if(tmp==NULL&&thCtx->taskType==TASK_E_TYPE_QUIT)
            {
                IENT_LOG_DEBUG("thread end\n");
                poolCtx->threadNum--;
                UTL_LockLeave(poolCtx->taskLock);
                return 0;
            }   
        }
        UTL_LockLeave(poolCtx->taskLock);

        taskCtx = (UTL_TPOOL_TASK*)tmp;
        *(taskCtx->pRetVal) = taskCtx->taskCb(taskCtx->taskData);
        if(taskCtx->taskEndCb)
        {
            taskCtx->taskEndCb(taskCtx->taskData,taskCtx->pRetVal);
        }

        UTL_LockEnter(poolCtx->taskLock);
        UTL_DllInsHead(&poolCtx->taskFinishHeader,tmp);
        tmp = NULL;
        if(thCtx->taskType==TASK_E_TYPE_QUIT)
        {
            IENT_LOG_DEBUG("thread end\n");
            poolCtx->threadNum--;
            UTL_LockLeave(poolCtx->taskLock);
            break;
        }
        UTL_LockLeave(poolCtx->taskLock);
    }
    while(1);

    return 0;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :  iUTL_TPoolTaskProWin
 *
 * DESCRIPTION :  
 *
 * COMPLETION
 * STATUS      :  0
 *
 *-----------------------------------------------------------------------------
 */
#ifdef WIN32
static DWORD WINAPI  iUTL_TPoolTaskProWin(void* data)
{
    return iUTL_TPoolTaskPro(data);
}
#else
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :  iUTL_TPoolTaskProLinux
 *
 * DESCRIPTION :  
 *
 * COMPLETION
 * STATUS      :  0
 *
 *-----------------------------------------------------------------------------
 */
static void*  iUTL_TPoolTaskProLinux(void* data)
{
     iUTL_TPoolTaskPro(data);
     return NULL;
}
#endif
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :  UTL_TPoolInit
 *
 * DESCRIPTION :  
 *
 * COMPLETION
 * STATUS      :  0
 *
 *-----------------------------------------------------------------------------
 */
MSG_ID_T  UTL_TPoolInit(UTL_TPOOL*  pool,int num)
{
    MSG_ID_T     sts=0;
    UTL_TPOOL_CTX*  poolCtx=NULL;
    UTL_TPOOL_THREAD*  thCtx = NULL;
    if(pool==NULL)
    {
        IENT_LOG_ERROR("thread pool init handle is null\n");
        return -1;
    }
    poolCtx = (UTL_TPOOL_CTX*)malloc(sizeof(UTL_TPOOL_CTX));
    if(poolCtx == NULL)
    {
        IENT_LOG_ERROR("malloc failed\n");
        return -2;
    }
    memset(poolCtx,0,sizeof(UTL_TPOOL_CTX));

    sts = ENT_ThreadInit(&poolCtx->thHandle);
    if(sts < 0)
    {
        IENT_LOG_ERROR("ENT_ThreadInit failed,sts [%d]\n",sts);
        return -3;
    }

    sts = UTL_LockInit(&poolCtx->taskLock,"");
    if(sts<0)
    {
        ENT_ThreadClose(poolCtx->thHandle);
        IENT_LOG_ERROR("UTL_LockInit failed,sts [%d]\n",sts);
        return -4;
    }

    sts = UTL_CVInit(&poolCtx->taskEmptyCV,"");
    if(sts<0)
    {
        UTL_LockClose(poolCtx->taskLock);
        ENT_ThreadClose(poolCtx->thHandle);
        IENT_LOG_ERROR("UTL_LockInit failed,sts [%d]\n",sts);
        return -5;
    }

    sts = UTL_DllInitHead(&poolCtx->taskActiveHeader);

    sts = UTL_DllInitHead(&poolCtx->taskFinishHeader);

    sts = UTL_DllInitHead(&poolCtx->threadHeader);

    int tmpNum = num<=0?DEFAULT_NUM:num;
    for(int i=0; i< tmpNum; i++)
    {
        thCtx = (UTL_TPOOL_THREAD*)malloc(sizeof(UTL_TPOOL_THREAD));
        if(thCtx==NULL)
        {
            continue;
        }
        memset(thCtx,0,sizeof(UTL_TPOOL_THREAD));
        thCtx->pool = poolCtx;
#ifdef WIN32
        sts = ENT_ThreadCreate(&thCtx->thId,poolCtx->thHandle,iUTL_TPoolTaskProWin,thCtx);
#else
        sts = ENT_ThreadCreate(&thCtx->thId,poolCtx->thHandle,iUTL_TPoolTaskProLinux,thCtx);
#endif
        if(sts<0)
        {
            continue;
        }
        UTL_DllInsHead(&poolCtx->threadHeader,(DLL_D_HDR*)thCtx);
        poolCtx->threadNum++;
    }
    *pool = poolCtx;

    return 0;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :  UTL_TPoolClose
 *
 * DESCRIPTION :  
 *
 * COMPLETION
 * STATUS      :  0
 *
 *-----------------------------------------------------------------------------
 */
MSG_ID_T  UTL_TPoolClose(UTL_TPOOL  pool)
{
    MSG_ID_T          sts = 0;
    UTL_TPOOL_CTX*  poolCtx= NULL;
    UTL_TPOOL_THREAD*  thDb = NULL;
    UTL_TPOOL_TASK*    taskDb = NULL;
    DLL_D_HDR*        tmp = NULL;
    DLL_D_HDR*        curr = NULL;
    if(pool==NULL)
    {
        IENT_LOG_ERROR("thread pool init handle is null\n");
        return -1;
    }
    poolCtx=(UTL_TPOOL_CTX*)pool;

    curr = &poolCtx->threadHeader;
    while((sts = UTL_DllNextLe(curr,&tmp))==0 &&tmp!=&poolCtx->threadHeader)
    {
        thDb = (UTL_TPOOL_THREAD*)tmp;
        if(thDb->thId)
        {
            thDb->taskType = TASK_E_TYPE_QUIT;
            time(&thDb->taskStartTime);
        }
        curr = tmp;
    }

    UTL_CVWakeAll(poolCtx->taskEmptyCV);
    while((sts = UTL_DllRemHead(&poolCtx->threadHeader,&tmp))==0)
    {
        thDb = (UTL_TPOOL_THREAD*)tmp;
        if(thDb->thId)
        {
            sts = ENT_ThreadWaitById(&thDb->thId,poolCtx->thHandle,0);
            IENT_LOG_WARN("pool remove thread sts [%d]\n",sts);
        }

        if(thDb)
        {
            free(thDb);
        }
    }

    UTL_LockEnter(poolCtx->taskLock);
    while((sts = UTL_DllRemHead(&poolCtx->taskActiveHeader,&tmp))==0)
    {
        taskDb = (UTL_TPOOL_TASK*)tmp;

        if(taskDb)
        {
            free(taskDb);
        }
    }
    while((sts = UTL_DllRemHead(&poolCtx->taskFinishHeader,&tmp))==0)
    {
        taskDb = (UTL_TPOOL_TASK*)tmp;

        if(taskDb)
        {
            free(taskDb);
        }
    }
    UTL_LockLeave(poolCtx->taskLock);

    UTL_CVClose(poolCtx->taskEmptyCV);
    UTL_LockClose(poolCtx->taskLock);
    ENT_ThreadClose(poolCtx->thHandle);
    
    free(poolCtx);
    return 0;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :  UTL_TPoolAddTask
 *
 * DESCRIPTION :  
 *
 * COMPLETION
 * STATUS      :  0
 *
 *-----------------------------------------------------------------------------
 */
MSG_ID_T  UTL_TPoolAddTask(UTL_TPOOL pool,UTL_TP_TASK_F taskCb,UTL_TP_TASK_END_F taskEndCb,void* taskData,MSG_ID_T* retVal)
{
    MSG_ID_T          sts = 0;
    UTL_TPOOL_CTX*  poolCtx= NULL;
    UTL_TPOOL_TASK*    taskDb = NULL;
    DLL_D_HDR*        tmp = NULL;
    if(pool==NULL || taskCb==NULL || retVal == NULL)
    {
        IENT_LOG_ERROR("invalid arguments\n");
        return -1;
    }

    poolCtx=(UTL_TPOOL_CTX*)pool;

    UTL_LockEnter(poolCtx->taskLock);

    sts = UTL_DllRemHead(&poolCtx->taskFinishHeader,&tmp);
    if(sts<0)
    {
        taskDb = (UTL_TPOOL_TASK*)malloc(sizeof(UTL_TPOOL_TASK));
        if(taskDb == NULL)
        {
            IENT_LOG_ERROR("malloc size [%d] failed\n",sizeof(UTL_TPOOL_TASK));
            UTL_LockLeave(poolCtx->taskLock);
            return -1;
        }
    }
    else
        taskDb = (UTL_TPOOL_TASK*)tmp;

    memset(taskDb,0,sizeof(UTL_TPOOL_TASK));

    taskDb->taskCb    = taskCb;
    taskDb->taskEndCb = taskEndCb;
    taskDb->taskData  = taskData;
    taskDb->pRetVal   = retVal;
    UTL_DllInsHead(&poolCtx->taskActiveHeader,(DLL_D_HDR*)taskDb);
    UTL_LockLeave(poolCtx->taskLock);
    UTL_CVWake(poolCtx->taskEmptyCV);
    
    return 0;
}
