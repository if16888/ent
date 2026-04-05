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
    pthread_attr_t thAttr;
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

/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_ThreadInit
 *
 * DESCRIPTION :   
 *                 
 *                   
 *
 * COMPLETION
 * STATUS      :  0
 *                Success; Service has completed successfully.           
 *
 *                            
 *
 *-----------------------------------------------------------------------------
 */
ENT_PUBLIC MSG_ID_T ENT_ThreadInit(ENT_THREAD* pthHandle)
{   
    MSG_ID_T     sts=0;
    ENT_TH_CTX*  thCtx=NULL;
    if(pthHandle==NULL)
    {
        IENT_LOG_ERROR("thread init handle is null\n");
        return -1;
    }
    thCtx = (ENT_TH_CTX*)malloc(sizeof(ENT_TH_CTX));
    if(thCtx == NULL)
    {
        IENT_LOG_ERROR("malloc failed\n");
        return -2;
    }
    memset(thCtx,0,sizeof(ENT_TH_CTX));
    thCtx->tag = ENT_TH_TAG;
    sts = UTL_LockInit(&thCtx->dllLock,"ENT_ThreadInit");
    if(sts<0)
    {
        IENT_LOG_ERROR("UTL_LockInit failed,sts [%d].\n",sts);
        free(thCtx);
        return -3;
    }
    UTL_DllInitHead(&thCtx->dllHeader);
    *pthHandle = thCtx;
    return 0;
}
#ifdef WIN32
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_ThreadDetachCreate
 *
 * DESCRIPTION :   
 *                 
 *                   
 *
 * COMPLETION
 * STATUS      :  0
 *                Success; Service has completed successfully.           
 *
 *                            
 *
 *-----------------------------------------------------------------------------
 */
ENT_PUBLIC MSG_ID_T ENT_ThreadDetachCreate(ENT_THREAD handle,PTHREAD_START_ROUTINE thProc,void* thData)
{
    MSG_ID_T     sts=0;
    ENT_TH_CTX*  thCtx=NULL;
    HANDLE       tmpHandle;
    DWORD        thId;

    if(handle==NULL || thProc==NULL)
    {
        IENT_LOG_ERROR("thread handle is null\n");
        return -1;
    }
    thCtx=(ENT_TH_CTX*)handle;
    if(thCtx->tag != ENT_TH_TAG)
    {
        IENT_LOG_ERROR("break handle\n");
        return -2;
    }

    tmpHandle = CreateThread(
            NULL,                   // default security attributes
            0,                      // use default stack size  
            thProc,                 // thread function name
            thData,                 // argument to thread function 
            0,                      // use default creation flags 
            &thId);   // returns the thread identifier 
     
     if(tmpHandle == NULL)
     {
        IENT_LOG_ERROR("CreateThread failed,error code %u\n",GetLastError());
         return -3;
     }

     CloseHandle(tmpHandle);
     IENT_LOG_PRINT("CreateThread sucessful,thread id is %d\n",thId);
    
    return 0;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_ThreadCreate
 *
 * DESCRIPTION :   
 *                 
 *                   
 *
 * COMPLETION
 * STATUS      :  0
 *                Success; Service has completed successfully.           
 *
 *                            
 *
 *-----------------------------------------------------------------------------
 */
ENT_PUBLIC MSG_ID_T ENT_ThreadCreate(ENT_THREAD_ID* tid,ENT_THREAD handle,PTHREAD_START_ROUTINE thProc,void* thData)
{
    MSG_ID_T     sts=0;
    ENT_TH_CTX*  thCtx=NULL;
    THREAD_DB*   tmp=NULL;
    HANDLE       tmpHandle;
    if(handle==NULL || thProc==NULL)
    {
        IENT_LOG_ERROR("handle is null\n");
        return -1;
    }
    thCtx=(ENT_TH_CTX*)handle;
    if(thCtx->tag != ENT_TH_TAG)
    {
        IENT_LOG_ERROR("break handle\n");
        return -2;
    }
    tmp=(THREAD_DB*)malloc(sizeof(THREAD_DB));
    if(tmp==NULL)
    {
         IENT_LOG_ERROR("UTL_Malloc failed.\n");
         if(tid != NULL)
         {
             *tid = NULL;
         }
         return -2;
    }
    
    tmpHandle = CreateThread(
            NULL,                   // default security attributes
            0,                      // use default stack size  
            thProc,                 // thread function name
            thData,                 // argument to thread function 
            0,                      // use default creation flags 
            &tmp->thId);   // returns the thread identifier 
     
     if(tmpHandle == NULL)
     {
        IENT_LOG_ERROR("CreateThread failed,error code %u\n",GetLastError());
        if(tid != NULL)
        {
            *tid = NULL;
        }
        free(tmp);
        return -3;
     }
     //ENT_LOG_PRINT("CreateThread sucessful,thread id is %d\n",tmp->thId);
     tmp->thHandle = tmpHandle;
     tmp->tag = ENT_TH_TAG;
     UTL_LockEnter(thCtx->dllLock);
     sts = UTL_DllInsHead(&thCtx->dllHeader,(DLL_D_HDR*)tmp);
     UTL_LockLeave(thCtx->dllLock);
     if(tid!=NULL)
     {
        *tid = tmp;
     }

    return 0;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_ThreadWaitById
 *
 * DESCRIPTION :   
 *                 
 *                   
 *
 * COMPLETION
 * STATUS      :  0
 *                Success; Service has completed successfully.           
 *
 *                            
 *
 *-----------------------------------------------------------------------------
 */
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
        return -1;
    }

    thCtx=(ENT_TH_CTX*)handle;
    if(thCtx->tag != ENT_TH_TAG)
    {
        IENT_LOG_ERROR("break handle\n");
        return -2;
    }

    UTL_LockEnter(thCtx->dllLock);
    thDb = (THREAD_DB*)*tid;
    if(thDb==NULL || thDb->tag != ENT_TH_TAG)
    {
        IENT_LOG_ERROR("break tid\n");
        UTL_LockLeave(thCtx->dllLock);
        return -3;
    }
    UTL_LockLeave(thCtx->dllLock);
    
    if(thDb->thHandle)
    {
        if(ms<=0)
        {
            waitTm = INFINITE;
        }
        else
        {
            waitTm = ms;
        }
        ret = WaitForSingleObject(thDb->thHandle,waitTm);
        switch(ret)
        {
            case WAIT_OBJECT_0: 
                reSts = 0;
                break;

            case WAIT_TIMEOUT:
                return 1;
                break;
            
            case WAIT_FAILED:
                IENT_LOG_ERROR("WaitForSingleObject ret [%d],err [%d]\n",ret,GetLastError());
                return -4;
                break;

            default:
                IENT_LOG_ERROR("WaitForSingleObject ret [%d],err [%d]\n",ret,GetLastError());
                reSts = -5;
                break;
        }

        CloseHandle(thDb->thHandle);
        thDb->thHandle = NULL;
        thDb->thId = 0;
        thDb->tag  = 0x0;
    }
    UTL_LockEnter(thCtx->dllLock);
    sts = UTL_DllRemCurr(&thDb->dllLnk,&tmpHdr);
    free(thDb);
    *tid = NULL;
    UTL_LockLeave(thCtx->dllLock);
    return reSts;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_ThreadClose
 *
 * DESCRIPTION :   
 *                 
 *                   
 *
 * COMPLETION
 * STATUS      :  0
 *                Success; Service has completed successfully.           
 *
 *                            
 *
 *-----------------------------------------------------------------------------
 */
ENT_PUBLIC MSG_ID_T ENT_ThreadClose(ENT_THREAD handle)
{
    MSG_ID_T     sts=0;
    ENT_TH_CTX*  thCtx = NULL;
    THREAD_DB*   thDb  = NULL;
    DLL_D_HDR*   tmp;
    
    if(handle==NULL)
    {
        IENT_LOG_ERROR("handle is null\n");
        return -1;
    }
    
    thCtx=(ENT_TH_CTX*)handle;
    if(thCtx->tag != ENT_TH_TAG)
    {
        IENT_LOG_ERROR("break handle\n");
        return -2;
    }

    UTL_LockEnter(thCtx->dllLock);
    while((sts = UTL_DllRemHead(&thCtx->dllHeader,&tmp))==0)
    {
        thDb = (THREAD_DB*)tmp;
        if(thDb->thHandle)
        {
            TerminateThread(thDb->thHandle,1);
            CloseHandle(thDb->thHandle);
            thDb->thHandle = NULL;
            thDb->thId = 0;
            thDb->tag = 0x0;
        }
        if(thDb)
        {
            free(thDb);
        }
    }
    thCtx->tag = 0x0;
    UTL_LockLeave(thCtx->dllLock);
    UTL_LockClose(thCtx->dllLock);
    
    free(handle); 
    return 0;
}
#else
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_ThreadDetachCreate
 *
 * DESCRIPTION :   
 *                 
 *                   
 *
 * COMPLETION
 * STATUS      :  0
 *                Success; Service has completed successfully.           
 *
 *                            
 *
 *-----------------------------------------------------------------------------
 */
ENT_PUBLIC MSG_ID_T ENT_ThreadDetachCreate(ENT_THREAD handle,PTHREAD_START_ROUTINE thProc,void* thData)
{
    MSG_ID_T       sts=0;
    ENT_TH_CTX*    thCtx=(ENT_TH_CTX*)handle;
    pthread_t      thId;
    int            s;
    pthread_attr_t thAttr;
    
    if(handle==NULL || thProc==NULL)
    {
        IENT_LOG_ERROR("thread handle is null\n");
        return -1;
    }

    if(thCtx->tag != ENT_TH_TAG)
    {
        IENT_LOG_ERROR("break handle\n");
        return -2;
    }
    s = pthread_attr_init(&thAttr);
    if (s != 0)
    {
        IENT_LOG_ERROR("pthread_attr_init failed,error [%d]->[%s]\n",s,strerror(s));
        return -3;
    }

    s = pthread_attr_setdetachstate(&thAttr, PTHREAD_CREATE_DETACHED);
    if (s != 0)
    {
        IENT_LOG_ERROR("pthread_attr_setdetachstate failed,error [%d]->[%s]\n",s,strerror(s));
        return -4;
    }
    
    s = pthread_create(&thId, &thAttr, thProc, thData); 
     if(s != 0)
     {
        IENT_LOG_ERROR("pthread_create failed,error [%d] ->[%s]\n",s,strerror(s));
         return -5;
     }
     IENT_LOG_PRINT("pthread_create sucessful,thread id is %ld\n",thId);
     
     pthread_attr_destroy(&thAttr);
     if(s != 0)
     {
        IENT_LOG_ERROR("pthread_attr_destroy failed,error [%d]->[%s]\n",s,strerror(s));
         return -6;
     }
    
    return 0;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_ThreadCreate
 *
 * DESCRIPTION :   
 *                 
 *                   
 *
 * COMPLETION
 * STATUS      :  0
 *                Success; Service has completed successfully.           
 *
 *                            
 *
 *-----------------------------------------------------------------------------
 */
ENT_PUBLIC MSG_ID_T ENT_ThreadCreate(ENT_THREAD_ID* tid,ENT_THREAD handle,PTHREAD_START_ROUTINE thProc,void* thData)
{
    MSG_ID_T       sts=0;
    ENT_TH_CTX*    thCtx=(ENT_TH_CTX*)handle;
    THREAD_DB*     tmp=NULL;
    int            s;
    pthread_attr_t thAttr;
    
    if(handle==NULL || thProc==NULL)
    {
        IENT_LOG_ERROR("handle is null\n");
        return -1;
    }

    if(thCtx->tag != ENT_TH_TAG)
    {
        IENT_LOG_ERROR("break handle\n");
        return -2;
    }

    tmp=(THREAD_DB*)malloc(sizeof(THREAD_DB));
    if(tmp==NULL)
    {
         IENT_LOG_ERROR("malloc failed.\n");
         sts = -2;
         goto END_OF_ROUTINE;
    }
    memset(tmp,0,sizeof(THREAD_DB));
    
    s = pthread_attr_init(&thAttr);
    if(s != 0)
    {
        IENT_LOG_ERROR("pthread_attr_init failed,error [%d]->[%s]\n",s,strerror(s));
        free(tmp);
        sts = -3;
        goto END_OF_ROUTINE;
    }

    //s = pthread_attr_setdetachstate(&thAttr, PTHREAD_CREATE_DETACHED);
    //if (s != 0)
    //{
    //    IENT_LOG_ERROR("pthread_attr_setdetachstate failed,error [%d]->[%s]\n",s,strerror(s));
    //    return -4;
    //}
    
    tmp->thProc = thProc;
    tmp->thData = thData;
    tmp->thRet = NULL;
    tmp->finished = false;
    s = pthread_mutex_init(&tmp->doneMutex,NULL);
    if(s != 0)
    {
        IENT_LOG_ERROR("pthread_mutex_init failed,error [%d]->[%s]\n",s,strerror(s));
        free(tmp);
        sts = -4;
        goto END_OF_ROUTINE;
    }
    s = pthread_cond_init(&tmp->doneCv,NULL);
    if(s != 0)
    {
        IENT_LOG_ERROR("pthread_cond_init failed,error [%d]->[%s]\n",s,strerror(s));
        pthread_mutex_destroy(&tmp->doneMutex);
        free(tmp);
        sts = -4;
        goto END_OF_ROUTINE;
    }

    s = pthread_create(&tmp->thId, &thAttr, iENT_ThreadProc, tmp); 
    if(s != 0)
    {
       IENT_LOG_ERROR("pthread_create failed,error [%d] ->[%s]\n",s,strerror(s));
       pthread_cond_destroy(&tmp->doneCv);
       pthread_mutex_destroy(&tmp->doneMutex);
       free(tmp);
       pthread_attr_destroy(&thAttr);
       sts = -5;
       goto END_OF_ROUTINE;
    }
    //ENT_LOG_PRINT("pthread_create sucessful,thread id is %ld\n",tmp->thId);
    tmp->thHandle = NULL;
    tmp->tag = ENT_TH_TAG;
    
    pthread_attr_destroy(&thAttr);
    if(s != 0)
    {
       IENT_LOG_WARN("pthread_attr_destroy failed,error [%d]->[%s]\n",s,strerror(s));
    }

    UTL_LockEnter(thCtx->dllLock);
    sts = UTL_DllInsHead(&thCtx->dllHeader,(DLL_D_HDR*)tmp);
    UTL_LockLeave(thCtx->dllLock);

    if(tid!=NULL)
    {
       *tid = tmp;
    }
    return 0;

END_OF_ROUTINE:
    if(tid != NULL)
    {
        *tid = NULL;
    }
    return sts;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_ThreadWaitById
 *
 * DESCRIPTION :   
 *                 
 *                   
 *
 * COMPLETION
 * STATUS      :  0
 *                Success; Service has completed successfully.           
 *
 *                            
 *
 *-----------------------------------------------------------------------------
 */
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
        IENT_LOG_ERROR("Database handle is null\n");
        return -1;
    }

    thDb = (THREAD_DB*)*tid;
    if(thDb==NULL || thDb->tag != ENT_TH_TAG)
    {
        IENT_LOG_ERROR("break tid\n");
        return -2;
    }
    thCtx=(ENT_TH_CTX*)handle;
    if(thCtx->tag != ENT_TH_TAG)
    {
        IENT_LOG_ERROR("break handle\n");
        return -3;
    }

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
            clock_gettime(CLOCK_REALTIME,&deadline);
            deadline.tv_sec += ms/1000;
            deadline.tv_nsec += (long)(ms%1000) * 1000000L;
            if(deadline.tv_nsec >= 1000000000L)
            {
                deadline.tv_sec += 1;
                deadline.tv_nsec -= 1000000000L;
            }

            while(!thDb->finished)
            {
                s = pthread_cond_timedwait(&thDb->doneCv,&thDb->doneMutex,&deadline);
                if(s == ETIMEDOUT && !thDb->finished)
                {
                    pthread_mutex_unlock(&thDb->doneMutex);
                    return 1;
                }
                if(s != 0 && s != ETIMEDOUT)
                {
                    pthread_mutex_unlock(&thDb->doneMutex);
                    IENT_LOG_WARN("pthread_cond_timedwait failed [%d]->[%s]\n",s,strerror(s));
                    return -4;
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
    UTL_LockEnter(thCtx->dllLock);
    if(thDb->tag != ENT_TH_TAG)
    {
        UTL_LockLeave(thCtx->dllLock);
        return -4;
    }
    sts = UTL_DllRemCurr(&thDb->dllLnk,&tmpHdr);
    UTL_LockLeave(thCtx->dllLock);
    pthread_cond_destroy(&thDb->doneCv);
    pthread_mutex_destroy(&thDb->doneMutex);
    free(thDb);
    *tid = NULL;
    return 0;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_ThreadClose
 *
 * DESCRIPTION :   
 *                 
 *                   
 *
 * COMPLETION
 * STATUS      :  0
 *                Success; Service has completed successfully.           
 *
 *                            
 *
 *-----------------------------------------------------------------------------
 */
ENT_PUBLIC MSG_ID_T ENT_ThreadClose(ENT_THREAD handle)
{
    MSG_ID_T     sts=0;
    ENT_TH_CTX*  thCtx = NULL;
    THREAD_DB*   thDb  = NULL;
    DLL_D_HDR*   tmp;
    void*        retVal = NULL;
    
    if(handle==NULL)
    {
        IENT_LOG_ERROR("Database handle is null\n");
        return -1;
    }
    
    thCtx=(ENT_TH_CTX*)handle;
    if(thCtx->tag != ENT_TH_TAG)
    {
        IENT_LOG_ERROR("break handle\n");
        return -2;
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
        if(thDb)
        {
            pthread_cond_destroy(&thDb->doneCv);
            pthread_mutex_destroy(&thDb->doneMutex);
            free(thDb);
        }
    }
    thCtx->tag = 0x0;
    UTL_LockClose(thCtx->dllLock);
    
    free(handle);  
    return 0;
}
#endif
