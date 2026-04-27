/*-----------------------------------------------------------------------------
 *   Copyright 2019 Fei Li
 * 
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 *
 *-----------------------------------------------------------------------------
 */
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#ifdef WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif
#include "ient_comm.h"
#include "ent_msg.h"
#include "ent_thread.h"
#include "ent_utility.h"

typedef enum
{
    TASK_E_TYPE_USER = 0,
    TASK_E_TYPE_QUIT,
    TASK_E_TYPE_PAUSE
}TASK_E_TYPE;

typedef enum
{
    UTL_TPOOL_STATE_CREATED_E = 0,
    UTL_TPOOL_STATE_ACTIVE_E,
    UTL_TPOOL_STATE_CLOSING_E,
    UTL_TPOOL_STATE_CLOSED_E
} UTL_TPOOL_STATE_E;

typedef struct UTL_TPOOL_CTX_TAG
{
    unsigned int              tag;
    BOOL                      acceptingTasks;
    BOOL                      closing;
    UTL_TPOOL_STATE_E         state;
    int                       activeOps;
    struct UTL_TPOOL_CTX_TAG* registryNext;
    ENT_THREAD                thHandle;
    UTL_LOCK                  taskLock;
    UTL_LOCK                  recycleLock;
    UTL_CV                    taskEmptyCV;
    DLL_D_HDR                 taskActiveHeader;
    DLL_D_HDR                 taskFinishHeader;
    int                       taskNum;
    int                       threadNum;
    int                       waitNum;
    DLL_D_HDR                 threadHeader;
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
#define UTL_TPOOL_TAG (0xEB90F00D)

#ifdef WIN32
static INIT_ONCE sTPoolRegistryOnce = INIT_ONCE_STATIC_INIT;
static CRITICAL_SECTION sTPoolRegistryLock;
static BOOL CALLBACK iUTL_TPoolRegistryInitOnce(PINIT_ONCE once, PVOID param, PVOID* context)
{
    (void)once;
    (void)param;
    (void)context;
    InitializeCriticalSection(&sTPoolRegistryLock);
    return TRUE;
}
static void iUTL_TPoolRegistryLock(void)
{
    InitOnceExecuteOnce(&sTPoolRegistryOnce, iUTL_TPoolRegistryInitOnce, NULL, NULL);
    EnterCriticalSection(&sTPoolRegistryLock);
}
static void iUTL_TPoolRegistryUnlock(void)
{
    LeaveCriticalSection(&sTPoolRegistryLock);
}
#else
static pthread_mutex_t sTPoolRegistryLock = PTHREAD_MUTEX_INITIALIZER;
static void iUTL_TPoolRegistryLock(void)
{
    pthread_mutex_lock(&sTPoolRegistryLock);
}
static void iUTL_TPoolRegistryUnlock(void)
{
    pthread_mutex_unlock(&sTPoolRegistryLock);
}
#endif

static UTL_TPOOL_CTX* sTPoolRegistryHead = NULL;

#ifdef ENT_TPOOL_TEST_HOOKS
static BOOL sTPoolTestFailThreadListInsert = FALSE;

ENT_PUBLIC void UTL_TPoolTestSetThreadListInsertFailure(BOOL enable)
{
    sTPoolTestFailThreadListInsert = enable;
}
#endif

static void iUTL_TPoolRegistryAdd(UTL_TPOOL_CTX* poolCtx)
{
    iUTL_TPoolRegistryLock();
    poolCtx->registryNext = sTPoolRegistryHead;
    sTPoolRegistryHead = poolCtx;
    iUTL_TPoolRegistryUnlock();
}

static int iUTL_TPoolRegistryContainsLocked(UTL_TPOOL_CTX* poolCtx)
{
    UTL_TPOOL_CTX* cur = sTPoolRegistryHead;
    while(cur != NULL)
    {
        if(cur == poolCtx)
        {
            return 1;
        }
        cur = cur->registryNext;
    }
    return 0;
}

static void iUTL_TPoolRegistryRemoveLocked(UTL_TPOOL_CTX* poolCtx)
{
    UTL_TPOOL_CTX** cur = &sTPoolRegistryHead;
    while(*cur != NULL)
    {
        if(*cur == poolCtx)
        {
            *cur = poolCtx->registryNext;
            poolCtx->registryNext = NULL;
            return;
        }
        cur = &((*cur)->registryNext);
    }
}

static MSG_ID_T iUTL_TPoolAcquireOp(UTL_TPOOL pool, UTL_TPOOL_CTX** outPool)
{
    UTL_TPOOL_CTX* poolCtx = NULL;

    if(outPool == NULL)
    {
        return ENT_TPL_BAD_ARGUMENT;
    }
    *outPool = NULL;
    if(pool == NULL)
    {
        return ENT_TPL_BAD_ARGUMENT;
    }

    poolCtx = (UTL_TPOOL_CTX*)pool;
    iUTL_TPoolRegistryLock();
    if(!iUTL_TPoolRegistryContainsLocked(poolCtx) ||
       poolCtx->tag != UTL_TPOOL_TAG ||
       poolCtx->state != UTL_TPOOL_STATE_ACTIVE_E)
    {
        iUTL_TPoolRegistryUnlock();
        return ENT_TPL_NOT_INITIALIZED;
    }
    poolCtx->activeOps++;
    *outPool = poolCtx;
    iUTL_TPoolRegistryUnlock();
    return ENT_SYS_NORMAL;
}

static void iUTL_TPoolReleaseOp(UTL_TPOOL_CTX* poolCtx)
{
    if(poolCtx == NULL)
    {
        return;
    }
    iUTL_TPoolRegistryLock();
    if(iUTL_TPoolRegistryContainsLocked(poolCtx) && poolCtx->activeOps > 0)
    {
        poolCtx->activeOps--;
    }
    iUTL_TPoolRegistryUnlock();
}

static MSG_ID_T iUTL_TPoolBeginClose(UTL_TPOOL pool, UTL_TPOOL_CTX** outPool)
{
    UTL_TPOOL_CTX* poolCtx = NULL;

    if(outPool == NULL)
    {
        return ENT_TPL_BAD_ARGUMENT;
    }
    *outPool = NULL;
    if(pool == NULL)
    {
        return ENT_TPL_BAD_ARGUMENT;
    }

    poolCtx = (UTL_TPOOL_CTX*)pool;
    iUTL_TPoolRegistryLock();
    if(!iUTL_TPoolRegistryContainsLocked(poolCtx) || poolCtx->tag != UTL_TPOOL_TAG)
    {
        iUTL_TPoolRegistryUnlock();
        return ENT_TPL_BAD_ARGUMENT;
    }
    if(poolCtx->state == UTL_TPOOL_STATE_CLOSING_E || poolCtx->state == UTL_TPOOL_STATE_CLOSED_E)
    {
        iUTL_TPoolRegistryUnlock();
        return ENT_SYS_NORMAL;
    }
    poolCtx->state = UTL_TPOOL_STATE_CLOSING_E;
    *outPool = poolCtx;
    iUTL_TPoolRegistryUnlock();
    return ENT_SYS_NORMAL;
}

static void iUTL_TPoolWaitActiveOps(UTL_TPOOL_CTX* poolCtx)
{
    int activeOps = 0;

    if(poolCtx == NULL)
    {
        return;
    }

    do
    {
        iUTL_TPoolRegistryLock();
        activeOps = (iUTL_TPoolRegistryContainsLocked(poolCtx)) ? poolCtx->activeOps : 0;
        iUTL_TPoolRegistryUnlock();
        if(activeOps > 0)
        {
            UTL_Sleep(1);
        }
    } while(activeOps > 0);
}

static void iUTL_TPoolAbortStartedWorker(UTL_TPOOL_CTX* poolCtx, UTL_TPOOL_THREAD* thCtx)
{
    if(poolCtx == NULL || thCtx == NULL)
    {
        return;
    }

    UTL_LockEnter(poolCtx->taskLock);
    thCtx->taskType = TASK_E_TYPE_QUIT;
    UTL_CVWakeAll(poolCtx->taskEmptyCV);
    UTL_LockLeave(poolCtx->taskLock);

    ENT_ThreadWaitById(&thCtx->thId, poolCtx->thHandle, 0);
}

static DWORD iUTL_TPoolTaskPro(void* data)
{
    MSG_ID_T            sts = 0;
    UTL_TPOOL_THREAD*   thCtx = NULL;
    UTL_TPOOL_CTX*      poolCtx = NULL;
    UTL_TPOOL_TASK*     taskCtx = NULL;
    DLL_D_HDR*          tmp = NULL;
    if(data == NULL)
    {
        IENT_LOG_ERROR("arguments invalid\n");
        return ENT_TPL_BAD_ARGUMENT;
    }

    thCtx = (UTL_TPOOL_THREAD*)data;
    poolCtx = thCtx->pool;

    do
    {
        UTL_LockEnter(poolCtx->taskLock);
        while(tmp==NULL)
        {
            if(poolCtx->taskActiveHeader.fw_ptr == &poolCtx->taskActiveHeader)
            {
                if(thCtx->taskType==TASK_E_TYPE_QUIT || poolCtx->closing)
                {
                    poolCtx->threadNum--;
                    UTL_LockLeave(poolCtx->taskLock);
                    return 0;
                }
                poolCtx->waitNum++;
                sts = UTL_CVWait(poolCtx->taskEmptyCV,poolCtx->taskLock,0,RW_WRITE_E);
                poolCtx->waitNum--;
                if(sts < 0 && sts != ENT_UTHD_WAIT_TIMEOUT)
                {
                    poolCtx->threadNum--;
                    UTL_LockLeave(poolCtx->taskLock);
                    return 0;
                }
                continue;
            }
            if(thCtx->taskType==TASK_E_TYPE_PAUSE)
            {
                UTL_LockLeave(poolCtx->taskLock);
                UTL_Sleep(1);
                UTL_LockEnter(poolCtx->taskLock);
                continue;
            }
            sts = UTL_DllRemTail(&poolCtx->taskActiveHeader,&tmp);
            if(sts < 0)
            {
                tmp = NULL;
            }
            else
            {
                poolCtx->taskNum--;
            }
            if(tmp==NULL&&(thCtx->taskType==TASK_E_TYPE_QUIT || poolCtx->closing))
            {
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

        UTL_LockEnter(poolCtx->recycleLock);
        UTL_DllInsHead(&poolCtx->taskFinishHeader,tmp);
        UTL_LockLeave(poolCtx->recycleLock);
        tmp = NULL;

        UTL_LockEnter(poolCtx->taskLock);
        if(thCtx->taskType==TASK_E_TYPE_QUIT || poolCtx->closing)
        {
            poolCtx->threadNum--;
            UTL_LockLeave(poolCtx->taskLock);
            break;
        }
        UTL_LockLeave(poolCtx->taskLock);
    }
    while(1);

    return 0;
}

#ifdef WIN32
static DWORD WINAPI  iUTL_TPoolTaskProWin(void* data)
{
    return iUTL_TPoolTaskPro(data);
}
#else
static void*  iUTL_TPoolTaskProLinux(void* data)
{
     iUTL_TPoolTaskPro(data);
     return NULL;
}
#endif

ENT_PUBLIC MSG_ID_T  UTL_TPoolInit(UTL_TPOOL*  pool,int num)
{
    MSG_ID_T          sts=0;
    UTL_TPOOL_CTX*    poolCtx=NULL;
    UTL_TPOOL_THREAD* thCtx = NULL;
    if(pool==NULL)
    {
        IENT_LOG_ERROR("thread pool init handle is null\n");
        return ENT_TPL_BAD_ARGUMENT;
    }
    *pool = NULL;

    poolCtx = (UTL_TPOOL_CTX*)malloc(sizeof(UTL_TPOOL_CTX));
    if(poolCtx == NULL)
    {
        IENT_LOG_ERROR("malloc failed\n");
        return ENT_TPL_ALLOC_FAILED;
    }
    memset(poolCtx,0,sizeof(UTL_TPOOL_CTX));
    poolCtx->tag = UTL_TPOOL_TAG;
    poolCtx->acceptingTasks = TRUE;
    poolCtx->closing = FALSE;
    poolCtx->state = UTL_TPOOL_STATE_CREATED_E;
    poolCtx->activeOps = 0;
    poolCtx->registryNext = NULL;

    sts = ENT_ThreadInit(&poolCtx->thHandle);
    if(sts < 0)
    {
        IENT_LOG_ERROR("ENT_ThreadInit failed,sts [%d]\n",sts);
        free(poolCtx);
        return ENT_TPL_THREAD_INITFAIL;
    }

    sts = UTL_LockInit(&poolCtx->taskLock,"");
    if(sts<0)
    {
        ENT_ThreadClose(poolCtx->thHandle);
        free(poolCtx);
        IENT_LOG_ERROR("UTL_LockInit failed,sts [%d]\n",sts);
        return ENT_TPL_LOCK_INITFAIL;
    }

    sts = UTL_LockInitEx(&poolCtx->recycleLock,"",LOCK_SPIN_E);
    if(sts<0)
    {
        UTL_LockClose(&poolCtx->taskLock);
        ENT_ThreadClose(poolCtx->thHandle);
        free(poolCtx);
        IENT_LOG_ERROR("UTL_LockInitEx failed,sts [%d]\n",sts);
        return ENT_TPL_RECYCLE_LOCKFAIL;
    }

    sts = UTL_CVInit(&poolCtx->taskEmptyCV,"");
    if(sts<0)
    {
        UTL_LockClose(&poolCtx->recycleLock);
        UTL_LockClose(&poolCtx->taskLock);
        ENT_ThreadClose(poolCtx->thHandle);
        free(poolCtx);
        IENT_LOG_ERROR("UTL_CVInit failed,sts [%d]\n",sts);
        return ENT_TPL_CV_INITFAIL;
    }

    UTL_DllInitHead(&poolCtx->taskActiveHeader);
    UTL_DllInitHead(&poolCtx->taskFinishHeader);
    UTL_DllInitHead(&poolCtx->threadHeader);

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
            free(thCtx);
            continue;
        }
        sts = UTL_DllInsHead(&poolCtx->threadHeader,(DLL_D_HDR*)thCtx);
#ifdef ENT_TPOOL_TEST_HOOKS
        if(sts >= 0 && sTPoolTestFailThreadListInsert)
        {
            sts = ENT_TPL_WORKER_CREATEFAIL;
        }
#endif
        if(sts < 0)
        {
            iUTL_TPoolAbortStartedWorker(poolCtx, thCtx);
            free(thCtx);
            continue;
        }
        poolCtx->threadNum++;
    }

    if(poolCtx->threadNum <= 0)
    {
        UTL_CVClose(&poolCtx->taskEmptyCV);
        UTL_LockClose(&poolCtx->recycleLock);
        UTL_LockClose(&poolCtx->taskLock);
        ENT_ThreadClose(poolCtx->thHandle);
        free(poolCtx);
        return ENT_TPL_WORKER_CREATEFAIL;
    }

    poolCtx->state = UTL_TPOOL_STATE_ACTIVE_E;
    iUTL_TPoolRegistryAdd(poolCtx);
    *pool = poolCtx;

    return ENT_SYS_NORMAL;
}

ENT_PUBLIC MSG_ID_T  UTL_TPoolClose(UTL_TPOOL* pool)
{
    MSG_ID_T            sts = 0;
    UTL_TPOOL_CTX*      poolCtx= NULL;
    UTL_TPOOL_THREAD*   thDb = NULL;
    UTL_TPOOL_TASK*     taskDb = NULL;
    DLL_D_HDR*          tmp = NULL;
    DLL_D_HDR*          curr = NULL;
    if(pool == NULL)
    {
        IENT_LOG_ERROR("invalid thread pool handle\n");
        return ENT_TPL_BAD_ARGUMENT;
    }

    sts = iUTL_TPoolBeginClose(*pool, &poolCtx);
    if(sts != ENT_SYS_NORMAL)
    {
        IENT_LOG_ERROR("invalid thread pool handle\n");
        return sts;
    }
    if(poolCtx == NULL)
    {
        *pool = NULL;
        return ENT_SYS_NORMAL;
    }

    iUTL_TPoolWaitActiveOps(poolCtx);

    UTL_LockEnter(poolCtx->taskLock);
    poolCtx->closing = TRUE;
    poolCtx->acceptingTasks = FALSE;
    curr = &poolCtx->threadHeader;
    while((sts = UTL_DllNextLe(curr,&tmp))==0 && tmp!=&poolCtx->threadHeader)
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
    UTL_LockLeave(poolCtx->taskLock);

    while((sts = UTL_DllRemHead(&poolCtx->threadHeader,&tmp))==0)
    {
        thDb = (UTL_TPOOL_THREAD*)tmp;
        if(thDb->thId)
        {
            sts = ENT_ThreadWaitById(&thDb->thId,poolCtx->thHandle,0);
            IENT_LOG_WARN("pool remove thread sts [%d]\n",sts);
        }

        free(thDb);
    }

    UTL_LockEnter(poolCtx->taskLock);
    while((sts = UTL_DllRemHead(&poolCtx->taskActiveHeader,&tmp))==0)
    {
        taskDb = (UTL_TPOOL_TASK*)tmp;
        free(taskDb);
    }
    UTL_LockLeave(poolCtx->taskLock);

    UTL_LockEnter(poolCtx->recycleLock);
    while((sts = UTL_DllRemHead(&poolCtx->taskFinishHeader,&tmp))==0)
    {
        taskDb = (UTL_TPOOL_TASK*)tmp;
        free(taskDb);
    }
    UTL_LockLeave(poolCtx->recycleLock);

    iUTL_TPoolRegistryLock();
    iUTL_TPoolRegistryRemoveLocked(poolCtx);
    poolCtx->state = UTL_TPOOL_STATE_CLOSED_E;
    poolCtx->tag = 0;
    iUTL_TPoolRegistryUnlock();

    UTL_CVClose(&poolCtx->taskEmptyCV);
    UTL_LockClose(&poolCtx->recycleLock);
    UTL_LockClose(&poolCtx->taskLock);
    ENT_ThreadClose(poolCtx->thHandle);

    free(poolCtx);
    *pool = NULL;
    return ENT_SYS_NORMAL;
}

ENT_PUBLIC MSG_ID_T  UTL_TPoolAddTask(UTL_TPOOL pool,UTL_TP_TASK_F taskCb,UTL_TP_TASK_END_F taskEndCb,void* taskData,MSG_ID_T* retVal)
{
    MSG_ID_T          sts = 0;
    UTL_TPOOL_CTX*    poolCtx= NULL;
    UTL_TPOOL_TASK*   taskDb = NULL;
    DLL_D_HDR*        tmp = NULL;
    BOOL              shouldWake = FALSE;
    if(taskCb==NULL || retVal == NULL)
    {
        IENT_LOG_ERROR("invalid arguments\n");
        return ENT_TPL_BAD_ARGUMENT;
    }

    sts = iUTL_TPoolAcquireOp(pool, &poolCtx);
    if(sts != ENT_SYS_NORMAL)
    {
        return sts;
    }

    UTL_LockEnter(poolCtx->recycleLock);
    sts = UTL_DllRemHead(&poolCtx->taskFinishHeader,&tmp);
    UTL_LockLeave(poolCtx->recycleLock);
    if(sts<0)
    {
        taskDb = (UTL_TPOOL_TASK*)malloc(sizeof(UTL_TPOOL_TASK));
        if(taskDb == NULL)
        {
            IENT_LOG_ERROR("malloc size [%d] failed\n",(int)sizeof(UTL_TPOOL_TASK));
            iUTL_TPoolReleaseOp(poolCtx);
            return ENT_TPL_TASK_ALLOCFAIL;
        }
    }
    else
    {
        taskDb = (UTL_TPOOL_TASK*)tmp;
    }

    memset(taskDb,0,sizeof(UTL_TPOOL_TASK));
    taskDb->taskCb    = taskCb;
    taskDb->taskEndCb = taskEndCb;
    taskDb->taskData  = taskData;
    taskDb->pRetVal   = retVal;

    UTL_LockEnter(poolCtx->taskLock);
    if(poolCtx->tag != UTL_TPOOL_TAG || poolCtx->closing || !poolCtx->acceptingTasks)
    {
        UTL_LockLeave(poolCtx->taskLock);
        free(taskDb);
        iUTL_TPoolReleaseOp(poolCtx);
        return ENT_TPL_NOT_INITIALIZED;
    }
    poolCtx->taskNum++;
    sts = UTL_DllInsHead(&poolCtx->taskActiveHeader,(DLL_D_HDR*)taskDb);
    if(sts < 0)
    {
        poolCtx->taskNum--;
        UTL_LockLeave(poolCtx->taskLock);
        free(taskDb);
        iUTL_TPoolReleaseOp(poolCtx);
        return ENT_TPL_TASK_ALLOCFAIL;
    }
    shouldWake = (poolCtx->waitNum > 0) ? TRUE : FALSE;
    UTL_LockLeave(poolCtx->taskLock);
    if(shouldWake)
    {
        UTL_CVWake(poolCtx->taskEmptyCV);
    }

    iUTL_TPoolReleaseOp(poolCtx);
    return ENT_SYS_NORMAL;
}
