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
#ifdef WIN32
#include "windows.h"
#else
#include <pthread.h>
#include <errno.h>
#include <time.h>
#endif
#include <stdlib.h>
#include <string.h>
#include "ent_log.h"
#include "ent_msg.h"
#include "ent_thread.h"
#include "ent_utility.h"
#include "ient_comm.h"

typedef struct THREAD_DB
{
    DLL_D_HDR      dllLnk;
#ifdef WIN32
    HANDLE         thHandle;
    DWORD          thId;
#else
    void*          thHandle;
    pthread_t      thId;
    PTHREAD_START_ROUTINE thProc;
    void*          thData;
    void*          thRet;
    pthread_mutex_t doneMutex;
    pthread_cond_t  doneCv;
    bool            finished;
#endif
    unsigned int    tag;
}THREAD_DB;

#define ENT_TH_TAG (0xEB90CA8F)

typedef struct ENT_TH_CTX
{
    unsigned int    tag;
    UTL_LOCK        dllLock;
    DLL_D_HDR       dllHeader;
}ENT_TH_CTX;

#ifndef WIN32
static clockid_t iENT_ThreadCondClockId(void)
{
#if defined(__linux__)
    return CLOCK_MONOTONIC;
#else
    return CLOCK_REALTIME;
#endif
}

static int iENT_ThreadCondInit(pthread_cond_t* cond)
{
#if defined(__linux__)
    pthread_condattr_t attr;
    int s = pthread_condattr_init(&attr);
    if(s != 0)
    {
        return s;
    }
    s = pthread_condattr_setclock(&attr, iENT_ThreadCondClockId());
    if(s != 0)
    {
        pthread_condattr_destroy(&attr);
        return s;
    }
    s = pthread_cond_init(cond, &attr);
    pthread_condattr_destroy(&attr);
    return s;
#else
    return pthread_cond_init(cond, NULL);
#endif
}

static void* iENT_ThreadProc(void* data)
{
    THREAD_DB* thDb = (THREAD_DB*)data;
    if(thDb == NULL || thDb->thProc == NULL)
    {
        return NULL;
    }

    thDb->thRet = thDb->thProc(thDb->thData);
    pthread_mutex_lock(&thDb->doneMutex);
    thDb->finished = true;
    pthread_cond_broadcast(&thDb->doneCv);
    pthread_mutex_unlock(&thDb->doneMutex);
    return thDb->thRet;
}
#endif

ENT_PUBLIC MSG_ID_T ENT_ThreadInit(ENT_THREAD* pthHandle)
{   
    MSG_ID_T     sts=0;
    ENT_TH_CTX*  thCtx=NULL;
    if(pthHandle==NULL)
    {
        IENT_LOG_ERROR("thread init handle is null\n");
        return ENT_THRD_INVALID_ARGUMENT;
    }
    thCtx = (ENT_TH_CTX*)malloc(sizeof(ENT_TH_CTX));
    if(thCtx == NULL)
    {
        IENT_LOG_ERROR("malloc failed\n");
        return ENT_THRD_ALLOC_FAILED;
    }
    memset(thCtx,0,sizeof(ENT_TH_CTX));
    thCtx->tag = ENT_TH_TAG;
    sts = UTL_LockInit(&thCtx->dllLock,"ENT_ThreadInit");
    if(sts<0)
    {
        IENT_LOG_ERROR("UTL_LockInit failed,sts [%d].\n",sts);
        free(thCtx);
        return ENT_THRD_LOCK_FAILED;
    }
    UTL_DllInitHead(&thCtx->dllHeader);
    *pthHandle = thCtx;
    return ENT_SYS_NORMAL;
}
#ifdef WIN32
ENT_PUBLIC MSG_ID_T ENT_ThreadDetachCreate(ENT_THREAD handle,PTHREAD_START_ROUTINE thProc,void* thData)
{
    ENT_TH_CTX*  thCtx=NULL;
    HANDLE       tmpHandle;
    DWORD        thId;

    if(handle==NULL || thProc==NULL)
    {
        IENT_LOG_ERROR("thread handle is null\n");
        return ENT_THRD_INVALID_ARGUMENT;
    }
    thCtx=(ENT_TH_CTX*)handle;
    if(thCtx->tag != ENT_TH_TAG)
    {
        IENT_LOG_ERROR("break handle\n");
        return ENT_THRD_INVALID_HANDLE;
    }

    tmpHandle = CreateThread(NULL,0,thProc,thData,0,&thId);
     if(tmpHandle == NULL)
     {
        IENT_LOG_ERROR("CreateThread failed,error code %u\n",GetLastError());
         return ENT_THRD_CREATE_FAILED;
     }

     CloseHandle(tmpHandle);
     IENT_LOG_PRINT("CreateThread sucessful,thread id is %d\n",thId);
    return ENT_SYS_NORMAL;
}

ENT_PUBLIC MSG_ID_T ENT_ThreadCreate(ENT_THREAD_ID* tid,ENT_THREAD handle,PTHREAD_START_ROUTINE thProc,void* thData)
{
    MSG_ID_T     sts=0;
    ENT_TH_CTX*  thCtx=NULL;
    THREAD_DB*   tmp=NULL;
    HANDLE       tmpHandle;
    if(tid != NULL)
    {
        *tid = NULL;
    }
    if(handle==NULL || thProc==NULL)
    {
        IENT_LOG_ERROR("handle is null\n");
        return ENT_THRD_INVALID_ARGUMENT;
    }
    thCtx=(ENT_TH_CTX*)handle;
    if(thCtx->tag != ENT_TH_TAG)
    {
        IENT_LOG_ERROR("break handle\n");
        return ENT_THRD_INVALID_HANDLE;
    }
    tmp=(THREAD_DB*)malloc(sizeof(THREAD_DB));
    if(tmp==NULL)
    {
         IENT_LOG_ERROR("UTL_Malloc failed.\n");
         return ENT_THRD_ALLOC_FAILED;
    }
    memset(tmp,0,sizeof(THREAD_DB));
    
    tmpHandle = CreateThread(NULL,0,thProc,thData,0,&tmp->thId);
     if(tmpHandle == NULL)
     {
        IENT_LOG_ERROR("CreateThread failed,error code %u\n",GetLastError());
        free(tmp);
        return ENT_THRD_CREATE_FAILED;
     }
     tmp->thHandle = tmpHandle;
     tmp->tag = ENT_TH_TAG;
     UTL_LockEnter(thCtx->dllLock);
     sts = UTL_DllInsHead(&thCtx->dllHeader,(DLL_D_HDR*)tmp);
     UTL_LockLeave(thCtx->dllLock);
     if(sts < 0)
     {
        WaitForSingleObject(tmp->thHandle, INFINITE);
        CloseHandle(tmp->thHandle);
        tmp->thHandle = NULL;
        tmp->thId = 0;
        tmp->tag = 0x0;
        free(tmp);
        return ENT_THRD_CREATE_FAILED;
     }
     if(tid!=NULL)
     {
        *tid = tmp;
     }
    return ENT_SYS_NORMAL;
}

ENT_PUBLIC MSG_ID_T ENT_ThreadWaitById(ENT_THREAD_ID* tid,ENT_THREAD handle,int ms)
{
    MSG_ID_T     sts    = 0;
    MSG_ID_T     reSts  = 0;
    THREAD_DB*   thDb   = NULL;
    DLL_D_HDR*   tmpHdr = NULL;
    DWORD        ret;
    ENT_TH_CTX*  thCtx = NULL;
    DWORD        waitTm = 0;

    if(tid==NULL || handle==NULL)
    {
        IENT_LOG_ERROR("arguments is invalid\n");
        return ENT_THRD_INVALID_ARGUMENT;
    }

    thCtx=(ENT_TH_CTX*)handle;
    if(thCtx->tag != ENT_TH_TAG)
    {
        IENT_LOG_ERROR("break handle\n");
        return ENT_THRD_INVALID_HANDLE;
    }

    UTL_LockEnter(thCtx->dllLock);
    thDb = (THREAD_DB*)*tid;
    if(thDb==NULL || thDb->tag != ENT_TH_TAG)
    {
        UTL_LockLeave(thCtx->dllLock);
        IENT_LOG_ERROR("break tid\n");
        return ENT_THRD_NOT_FOUND;
    }
    sts = UTL_DllRemCurr(&thDb->dllLnk,&tmpHdr);
    if(sts < 0)
    {
        UTL_LockLeave(thCtx->dllLock);
        return ENT_THRD_NOT_FOUND;
    }
    *tid = NULL;
    UTL_LockLeave(thCtx->dllLock);
    
    if(thDb->thHandle)
    {
        waitTm = (ms<=0) ? INFINITE : (DWORD)ms;
        ret = WaitForSingleObject(thDb->thHandle,waitTm);
        switch(ret)
        {
            case WAIT_OBJECT_0: 
                reSts = ENT_SYS_NORMAL;
                break;
            case WAIT_TIMEOUT:
                UTL_LockEnter(thCtx->dllLock);
                UTL_DllInsHead(&thCtx->dllHeader,(DLL_D_HDR*)thDb);
                *tid = thDb;
                UTL_LockLeave(thCtx->dllLock);
                return ENT_THRD_WAIT_TIMEOUT;
            case WAIT_FAILED:
                IENT_LOG_ERROR("WaitForSingleObject ret [%d],err [%d]\n",ret,GetLastError());
                UTL_LockEnter(thCtx->dllLock);
                UTL_DllInsHead(&thCtx->dllHeader,(DLL_D_HDR*)thDb);
                *tid = thDb;
                UTL_LockLeave(thCtx->dllLock);
                return ENT_THRD_WAIT_FAILED;
            default:
                IENT_LOG_ERROR("WaitForSingleObject ret [%d],err [%d]\n",ret,GetLastError());
                UTL_LockEnter(thCtx->dllLock);
                UTL_DllInsHead(&thCtx->dllHeader,(DLL_D_HDR*)thDb);
                *tid = thDb;
                UTL_LockLeave(thCtx->dllLock);
                reSts = ENT_THRD_WAIT_FAILED;
                break;
        }
        CloseHandle(thDb->thHandle);
        thDb->thHandle = NULL;
        thDb->thId = 0;
        thDb->tag  = 0x0;
    }
    free(thDb);
    return reSts;
}

ENT_PUBLIC MSG_ID_T ENT_ThreadClose(ENT_THREAD handle)
{
    MSG_ID_T     sts=0;
    ENT_TH_CTX*  thCtx = NULL;
    THREAD_DB*   thDb  = NULL;
    DLL_D_HDR*   tmp;
    
    if(handle==NULL)
    {
        IENT_LOG_ERROR("handle is null\n");
        return ENT_THRD_INVALID_ARGUMENT;
    }
    thCtx=(ENT_TH_CTX*)handle;
    if(thCtx->tag != ENT_TH_TAG)
    {
        IENT_LOG_ERROR("break handle\n");
        return ENT_THRD_INVALID_HANDLE;
    }
    while(1)
    {
        UTL_LockEnter(thCtx->dllLock);
        sts = UTL_DllRemHead(&thCtx->dllHeader,&tmp);
        UTL_LockLeave(thCtx->dllLock);
        if(sts != 0)
        {
            break;
        }
        thDb = (THREAD_DB*)tmp;
        if(thDb->thHandle)
        {
            WaitForSingleObject(thDb->thHandle, INFINITE);
            CloseHandle(thDb->thHandle);
            thDb->thHandle = NULL;
            thDb->thId = 0;
            thDb->tag = 0x0;
        }
        free(thDb);
    }
    thCtx->tag = 0x0;
    UTL_LockClose(&thCtx->dllLock);
    free(handle); 
    return ENT_SYS_NORMAL;
}
#else
ENT_PUBLIC MSG_ID_T ENT_ThreadDetachCreate(ENT_THREAD handle,PTHREAD_START_ROUTINE thProc,void* thData)
{
    ENT_TH_CTX*    thCtx=(ENT_TH_CTX*)handle;
    pthread_t      thId;
    int            s;
    pthread_attr_t thAttr;
    
    if(handle==NULL || thProc==NULL)
    {
        IENT_LOG_ERROR("thread handle is null\n");
        return ENT_THRD_INVALID_ARGUMENT;
    }
    if(thCtx->tag != ENT_TH_TAG)
    {
        IENT_LOG_ERROR("break handle\n");
        return ENT_THRD_INVALID_HANDLE;
    }
    s = pthread_attr_init(&thAttr);
    if (s != 0)
    {
        IENT_LOG_ERROR("pthread_attr_init failed,error [%d]->[%s]\n",s,strerror(s));
        return ENT_THRD_ATTR_FAILED;
    }
    s = pthread_attr_setdetachstate(&thAttr, PTHREAD_CREATE_DETACHED);
    if (s != 0)
    {
        IENT_LOG_ERROR("pthread_attr_setdetachstate failed,error [%d]->[%s]\n",s,strerror(s));
        pthread_attr_destroy(&thAttr);
        return ENT_THRD_ATTR_FAILED;
    }
    s = pthread_create(&thId, &thAttr, thProc, thData); 
     if(s != 0)
     {
        IENT_LOG_ERROR("pthread_create failed,error [%d] ->[%s]\n",s,strerror(s));
        pthread_attr_destroy(&thAttr);
         return ENT_THRD_CREATE_FAILED;
     }
     IENT_LOG_PRINT("pthread_create sucessful,thread id is %ld\n",thId);
     s = pthread_attr_destroy(&thAttr);
     if(s != 0)
     {
        IENT_LOG_WARN("pthread_attr_destroy failed,error [%d]->[%s]\n",s,strerror(s));
     }
    return ENT_SYS_NORMAL;
}

ENT_PUBLIC MSG_ID_T ENT_ThreadCreate(ENT_THREAD_ID* tid,ENT_THREAD handle,PTHREAD_START_ROUTINE thProc,void* thData)
{
    MSG_ID_T       sts=0;
    ENT_TH_CTX*    thCtx=(ENT_TH_CTX*)handle;
    THREAD_DB*     tmp=NULL;
    int            s;
    
    if(tid != NULL)
    {
        *tid = NULL;
    }
    if(handle==NULL || thProc==NULL)
    {
        IENT_LOG_ERROR("handle is null\n");
        return ENT_THRD_INVALID_ARGUMENT;
    }
    if(thCtx->tag != ENT_TH_TAG)
    {
        IENT_LOG_ERROR("break handle\n");
        return ENT_THRD_INVALID_HANDLE;
    }
    tmp=(THREAD_DB*)malloc(sizeof(THREAD_DB));
    if(tmp==NULL)
    {
         IENT_LOG_ERROR("malloc failed.\n");
         return ENT_THRD_ALLOC_FAILED;
    }
    memset(tmp,0,sizeof(THREAD_DB));
    tmp->thProc = thProc;
    tmp->thData = thData;
    tmp->thRet = NULL;
    tmp->finished = false;
    s = pthread_mutex_init(&tmp->doneMutex,NULL);
    if(s != 0)
    {
        IENT_LOG_ERROR("pthread_mutex_init failed,error [%d]->[%s]\n",s,strerror(s));
        free(tmp);
        return ENT_THRD_CREATE_FAILED;
    }
    s = iENT_ThreadCondInit(&tmp->doneCv);
    if(s != 0)
    {
        IENT_LOG_ERROR("pthread_cond_init failed,error [%d]->[%s]\n",s,strerror(s));
        pthread_mutex_destroy(&tmp->doneMutex);
        free(tmp);
        return ENT_THRD_CREATE_FAILED;
    }
    s = pthread_create(&tmp->thId, NULL, iENT_ThreadProc, tmp); 
    if(s != 0)
    {
       IENT_LOG_ERROR("pthread_create failed,error [%d] ->[%s]\n",s,strerror(s));
       pthread_cond_destroy(&tmp->doneCv);
       pthread_mutex_destroy(&tmp->doneMutex);
       free(tmp);
       return ENT_THRD_CREATE_FAILED;
    }
    tmp->thHandle = NULL;
    tmp->tag = ENT_TH_TAG;
    UTL_LockEnter(thCtx->dllLock);
    sts = UTL_DllInsHead(&thCtx->dllHeader,(DLL_D_HDR*)tmp);
    UTL_LockLeave(thCtx->dllLock);
    if(sts < 0)
    {
        pthread_join(tmp->thId,NULL);
        pthread_cond_destroy(&tmp->doneCv);
        pthread_mutex_destroy(&tmp->doneMutex);
        tmp->thId = 0;
        tmp->tag = 0x0;
        free(tmp);
        return ENT_THRD_CREATE_FAILED;
    }
    if(tid!=NULL)
    {
       *tid = tmp;
    }
    return ENT_SYS_NORMAL;
}

ENT_PUBLIC MSG_ID_T ENT_ThreadWaitById(ENT_THREAD_ID* tid,ENT_THREAD handle,int ms)
{
    MSG_ID_T     sts = 0;
    THREAD_DB*   thDb  = NULL;
    DLL_D_HDR*   tmpHdr = NULL;
    ENT_TH_CTX*  thCtx=NULL;
    void*        retVal=NULL;
    int          s;
    struct timespec deadline;

    if(tid == NULL || handle==NULL)
    {
        IENT_LOG_ERROR("thread wait argument is null\n");
        return ENT_THRD_INVALID_ARGUMENT;
    }
    thCtx=(ENT_TH_CTX*)handle;
    if(thCtx->tag != ENT_TH_TAG)
    {
        IENT_LOG_ERROR("break handle\n");
        return ENT_THRD_INVALID_HANDLE;
    }
    UTL_LockEnter(thCtx->dllLock);
    thDb = (THREAD_DB*)*tid;
    if(thDb==NULL || thDb->tag != ENT_TH_TAG)
    {
        UTL_LockLeave(thCtx->dllLock);
        IENT_LOG_ERROR("break tid\n");
        return ENT_THRD_NOT_FOUND;
    }
    sts = UTL_DllRemCurr(&thDb->dllLnk,&tmpHdr);
    if(sts < 0)
    {
        UTL_LockLeave(thCtx->dllLock);
        return ENT_THRD_NOT_FOUND;
    }
    *tid = NULL;
    UTL_LockLeave(thCtx->dllLock);

    if(thDb->thId)
    {
        pthread_mutex_lock(&thDb->doneMutex);
        if(ms<=0)
        {
            while(!thDb->finished)
            {
                pthread_cond_wait(&thDb->doneCv,&thDb->doneMutex);
            }
            pthread_mutex_unlock(&thDb->doneMutex);
            s = pthread_join(thDb->thId,&retVal);
            if(s!=0)
            {
                IENT_LOG_WARN("join,error [%d]->[%s]\n",s,strerror(s));
            }
        }
        else
        {
            clock_gettime(iENT_ThreadCondClockId(),&deadline);
            deadline.tv_sec += ms/1000;
            deadline.tv_nsec += (long)(ms%1000) * 1000000L;
            if(deadline.tv_nsec >= 1000000000L)
            {
                deadline.tv_sec += deadline.tv_nsec / 1000000000L;
                deadline.tv_nsec = deadline.tv_nsec % 1000000000L;
            }

            while(!thDb->finished)
            {
                s = pthread_cond_timedwait(&thDb->doneCv,&thDb->doneMutex,&deadline);
                if(s == ETIMEDOUT && !thDb->finished)
                {
                    pthread_mutex_unlock(&thDb->doneMutex);
                    UTL_LockEnter(thCtx->dllLock);
                    UTL_DllInsHead(&thCtx->dllHeader,(DLL_D_HDR*)thDb);
                    *tid = thDb;
                    UTL_LockLeave(thCtx->dllLock);
                    return ENT_THRD_WAIT_TIMEOUT;
                }
                if(s != 0 && s != ETIMEDOUT)
                {
                    pthread_mutex_unlock(&thDb->doneMutex);
                    IENT_LOG_WARN("pthread_cond_timedwait failed [%d]->[%s]\n",s,strerror(s));
                    UTL_LockEnter(thCtx->dllLock);
                    UTL_DllInsHead(&thCtx->dllHeader,(DLL_D_HDR*)thDb);
                    *tid = thDb;
                    UTL_LockLeave(thCtx->dllLock);
                    return ENT_THRD_WAIT_FAILED;
                }
            }
            pthread_mutex_unlock(&thDb->doneMutex);
            s = pthread_join(thDb->thId,&retVal);
            if(s!=0)
            {
                IENT_LOG_WARN("join,error [%d]->[%s]\n",s,strerror(s));
            }
        }
        thDb->thHandle = NULL;
        thDb->thId = 0;
    }
    pthread_cond_destroy(&thDb->doneCv);
    pthread_mutex_destroy(&thDb->doneMutex);
    thDb->tag = 0x0;
    free(thDb);
    return ENT_SYS_NORMAL;
}

ENT_PUBLIC MSG_ID_T ENT_ThreadClose(ENT_THREAD handle)
{
    MSG_ID_T     sts=0;
    ENT_TH_CTX*  thCtx = NULL;
    THREAD_DB*   thDb  = NULL;
    DLL_D_HDR*   tmp;
    void*        retVal = NULL;
    
    if(handle==NULL)
    {
        IENT_LOG_ERROR("thread close handle is null\n");
        return ENT_THRD_INVALID_ARGUMENT;
    }
    thCtx=(ENT_TH_CTX*)handle;
    if(thCtx->tag != ENT_TH_TAG)
    {
        IENT_LOG_ERROR("break handle\n");
        return ENT_THRD_INVALID_HANDLE;
    }
    while(1)
    {
        UTL_LockEnter(thCtx->dllLock);
        sts = UTL_DllRemHead(&thCtx->dllHeader,&tmp);
        UTL_LockLeave(thCtx->dllLock);
        if(sts != 0)
        {
            break;
        }
        thDb = (THREAD_DB*)tmp;
        if(thDb->thId)
        {
            pthread_join(thDb->thId,&retVal);
            thDb->thHandle = NULL;
            thDb->thId = 0;
            thDb->tag = 0x0;
        }
        pthread_cond_destroy(&thDb->doneCv);
        pthread_mutex_destroy(&thDb->doneMutex);
        free(thDb);
    }
    thCtx->tag = 0x0;
    UTL_LockClose(&thCtx->dllLock);
    free(handle);  
    return ENT_SYS_NORMAL;
}
#endif
