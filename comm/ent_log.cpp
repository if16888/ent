/*-----------------------------------------------------------------------------
 *   Copyright 2019 Fei Li<if16888@foxmail.com>
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
#include <stdlib.h>
#include <stdarg.h>
#include <malloc.h>
#include <string.h>
#ifdef WIN32
#include <sys/timeb.h>
#include <Windows.h>
#include <tchar.h>
#endif

#ifdef __linux__
#include <pthread.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/limits.h>
#include <unistd.h>
#include <errno.h>
#endif

#include <time.h>
#include "ent_log.h"
#include "ient_comm.h"



#define DEF_MAX_NUM_LOG  15

#ifdef WIN32
static CRITICAL_SECTION sLogMutex;
#pragma warning(disable : 4996)
#elif defined(__linux__)
static pthread_mutex_t  sLogMutex;
#define _MAX_PATH PATH_MAX
#define _snprintf snprintf
#endif

static int              sLogNum = 0;
volatile static bool    sLogMutexInit = false;


#define ENTLOG_TAG (0x6AFEFE6A)
typedef struct
{
    unsigned int     tag;//check tag
    bool             isInit;//
    bool             isDebug;//
    bool             isBuffer;
    ENT_LOG_LEV_E    logLevel;//log level
    FILE*            logFp;
#ifdef WIN32
    CRITICAL_SECTION cs;
#else
    pthread_mutex_t  cs;
#endif
    char*            moduleName;
    char*            logPath;
    int              maxNum;
    time_t           nextCreate;
} ENT_LOG_CTX;

static const char* sLogLevelStr[]={
                "FATAL",
                "ERROR",
                "WARN",
                "INFO",
                "DEBUG"};

static ENT_LOG_CTX sDefLog={
                ENTLOG_TAG,
                false,
                false,
                false,
                LOG_LEV_WARN_E,//default log LEVEL
                NULL,
                {0},
                NULL,
                NULL,
                DEF_MAX_NUM_LOG,
                0
            };
            
static MSG_ID_T iENT_LogPathCheck(const char* path);
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :iENT_LogRollCheck
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
#undef  FUNC_NAME
#define FUNC_NAME "iENT_LogRollCheck"
static MSG_ID_T  iENT_LogRollCheck(ENT_LOG logHandle,time_t nowTime)
{
    ENT_LOG_CTX* log = (ENT_LOG_CTX*)logHandle;
    const static char* tmFormat="%Y%m%d";
    char dayStr[9];//4+2+2
    struct tm nowTm;
    
    if(log == NULL)
    {
        log = &sDefLog;
    }
    
    if(log->nextCreate>nowTime)
        return 1;
    
    if(log->logFp == NULL ||
       log->nextCreate+86400<nowTime
        )
    {
    #ifdef WIN32
        localtime_s(&nowTm,&nowTime);
    #else
        localtime_r(&nowTime,&nowTm);
    #endif
        strftime(dayStr, 9,tmFormat, &nowTm);
        nowTm.tm_hour = 0;
        nowTm.tm_min  = 0;
        nowTm.tm_sec  = 0;
        log->nextCreate = mktime(&nowTm)+86400;//next day
    }
    else
    {
    #ifdef WIN32
        localtime_s(&nowTm,&log->nextCreate);
    #else
        localtime_r(&log->nextCreate,&nowTm);
    #endif
        strftime(dayStr, 9,tmFormat, &nowTm);
        log->nextCreate += 86400;//next day    
    }
    
    if(log->logFp!=NULL)
    {
        fclose(log->logFp);
        log->logFp = NULL;
    }
    char fileName[_MAX_PATH];
    memset(fileName,0,_MAX_PATH);
    if(log->logPath==NULL)
    {
        _snprintf(fileName,_MAX_PATH-1,"%s_%s.log",log->moduleName,dayStr);
    }
    else
    {
        _snprintf(fileName,_MAX_PATH-1,"%s%s%s_%s.log",log->logPath,ENT_FILE_SEP,log->moduleName,dayStr);
    }
    
    fileName[_MAX_PATH-1]='\0';
    
    log->logFp = fopen(fileName,"a");
    if(log->logFp==NULL)
    {
        fprintf(stderr,"Func [%s] Line [%d],The file %s  was not opened\n",FUNC_NAME,__LINE__,fileName);
    }
    
    if(log->maxNum>0)
    {
        time_t deleteTime;
        deleteTime = log->nextCreate-(log->maxNum+1)*86400;
    #ifdef WIN32
        localtime_s(&nowTm,&deleteTime);
    #else
        localtime_r(&deleteTime,&nowTm);
    #endif
        strftime(dayStr, 9,tmFormat, &nowTm);
        memset(fileName,0,_MAX_PATH);
        if(log->logPath==NULL)
            _snprintf(fileName,_MAX_PATH-1,"%s_%s.log",log->moduleName,dayStr);
        else
            _snprintf(fileName,_MAX_PATH-1,"%s%s%s_%s.log",log->logPath,ENT_FILE_SEP,log->moduleName,dayStr);
        
        fileName[_MAX_PATH-1]='\0';

    #ifdef WIN32
        DeleteFileA(fileName);
    #else
        unlink(fileName);
    #endif

    }
    return 0;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_LogInit
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
#undef  FUNC_NAME
#define FUNC_NAME "ENT_LogInit"
MSG_ID_T  ENT_LogInit()
{
    if(sLogMutexInit)
    {
        return 1;
    }
#ifdef WIN32
    InitializeCriticalSection(&sLogMutex);
#else
    pthread_mutex_init(&sLogMutex,NULL);
#endif
    fprintf(stderr,"Func [%s] Line [%d],sucessful.\n",FUNC_NAME,__LINE__);
    sLogMutexInit = true;
    return 0;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_LogClose
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
#undef  FUNC_NAME
#define FUNC_NAME "ENT_LogClose"
MSG_ID_T  ENT_LogClose()
{
#ifdef WIN32
    DeleteCriticalSection(&sLogMutex);
#else
    pthread_mutex_destroy(&sLogMutex);
#endif
    
    fprintf(stderr,"Func [%s] Line [%d],sucessful.\n",FUNC_NAME,__LINE__);
    sLogMutexInit = false;
    return 0;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_LogInitHandle
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
#undef  FUNC_NAME
#define FUNC_NAME "ENT_LogInitHandle"
MSG_ID_T  ENT_LogInitHandle(ENT_LOG* pLogHandle,const char* moduleName,const char* logPath)
{
    MSG_ID_T sts = 0;
    int      len;
    ENT_LOG_CTX* log;
    
    if(sLogMutexInit==false)
    {
        fprintf(stderr,"Func [%s] Line [%d],Uninitialized,please call ENT_LogInit.\n",FUNC_NAME,__LINE__);
        return -1;
    }
    
#ifdef WIN32
    EnterCriticalSection(&sLogMutex);
#else
    pthread_mutex_lock(&sLogMutex);
#endif
    
    //_ftime(&nowTmb);
    
    if(pLogHandle==NULL)
    {
        log = &sDefLog;
        if(log->isInit)
        {
            sts = 1;
            fprintf(stderr,"Func [%s] Line [%d],The module [%s] log path [%s] was already opened.\n",FUNC_NAME,__LINE__,moduleName,logPath);
            goto END_OF_ROUTINE;
         }      
    }
    else
    {
        log = (ENT_LOG_CTX*)malloc(sizeof(ENT_LOG_CTX));
        if(log == NULL)
        {
            sts = -1;
            fprintf(stderr,"Func [%s] Line [%d],The module [%s] log path [%s] malloc failed.\n",FUNC_NAME,__LINE__,moduleName,logPath);
            goto END_OF_ROUTINE;
        }   
        memset(log,0,sizeof(ENT_LOG_CTX));
    }
    if(moduleName)
        log->moduleName = strdup(moduleName);
    else
        log->moduleName = strdup("default");  
        
    if(logPath)
    {
        log->logPath = strdup(logPath);
        len = strlen(log->logPath);
        if(log->logPath[len-1]=='\\' ||
        log->logPath[len-1]=='/')
        {
            log->logPath[len-1]='\0';
        }
        sts=iENT_LogPathCheck(log->logPath);
    }     
    
    //iENT_LogRollCheck(log,nowTmb.time);
#ifdef WIN32
    InitializeCriticalSection(&log->cs);
#else
    pthread_mutex_init(&log->cs,NULL);
#endif
    log->isInit   = true;
    log->isDebug  = false;
    log->isBuffer = false;
    log->logLevel = LOG_LEV_WARN_E;
    log->maxNum   = DEF_MAX_NUM_LOG;
    log->tag      = ENTLOG_TAG;
    if(pLogHandle)
    {
        *pLogHandle = log;
    }
    sLogNum++;
    
END_OF_ROUTINE:
#ifdef WIN32   
    LeaveCriticalSection(&sLogMutex);
#else
    pthread_mutex_unlock(&sLogMutex);
#endif
    
    return sts;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_LogSetOption
 *
 * DESCRIPTION :   
 *                 
 *                   
 *
 * COMPLETION
 * STATUS      :  0
 *                Success; Service has completed successfully.           
 *                -1
 *                Failed: Arguments is invalid.
 *
 *            
 *
 *-----------------------------------------------------------------------------
 */
#undef  FUNC_NAME
#define FUNC_NAME "ENT_LogSetOption"
MSG_ID_T  ENT_LogSetOption(ENT_LOG logHandle,ENT_LOG_OPTIONS_E option,const void* arg)
{
    ENT_LOG_CTX* log = (ENT_LOG_CTX*)logHandle;
    int          len;
    MSG_ID_T     sts=0;
    
    if(sLogMutexInit==false)
    {
        fprintf(stderr,"Func [%s] Line [%d],Uninitialized,please call ENT_LogInit.\n",FUNC_NAME,__LINE__);
        return -1;
    }
    
    if(arg == NULL)
    {
        fprintf(stderr,"Func [%s] Line [%d],arguments is invalid.\n",FUNC_NAME,__LINE__);
        return -1;
    }
    
    if(log == NULL)
    {
        if(sDefLog.isInit)
            log = &sDefLog;
        else
        {
            fprintf(stderr,"Func [%s] Line [%d],arguments is invalid.\n",FUNC_NAME,__LINE__);
            return -2;
        }
    }
    else if(log->tag!= ENTLOG_TAG || log->isInit == false)
    {
        fprintf(stderr,"Func [%s] Line [%d],arguments is invalid.\n",FUNC_NAME,__LINE__);
        return -2;
    }

#ifdef WIN32
    EnterCriticalSection(&log->cs);
#else
    pthread_mutex_lock(&log->cs);
#endif
    
    switch(option)
    {
        case ENT_LOG_DEBUG_E:
            log->isDebug = *(bool*)arg;
            break;
        
        case ENT_LOG_PATH_E:
            if(log->logPath)
            {
                free(log->logPath);
                log->logPath = NULL;
            }
            log->logPath = strdup((const char*)arg);
			if (log->logPath == NULL)
			{
				sts = -3;
				fprintf(stderr,"Func [%s] Line [%d],strdup failed.\n",FUNC_NAME,__LINE__);
				goto END_OF_ROUTINE;
			}
            len = strlen(log->logPath);
            if(log->logPath[len-1]=='\\' ||
            log->logPath[len-1]=='/')
            {
                log->logPath[len-1]='\0';
            }
            sts=iENT_LogPathCheck(log->logPath);
            if(sts<0)
            {
                if(log->logPath)
                {
                    free(log->logPath);
                    log->logPath=NULL;
                 }
            }
            break;
            
        case ENT_LOG_MAX_E:
            log->maxNum = *(int*)arg;
            break;
        
        case ENT_LOG_BUFFER_E:
            log->isBuffer = *(bool*)arg;
            break;
            
        case ENT_LOG_LEVEL_E:
            log->logLevel = *(ENT_LOG_LEV_E*)arg;
            break;
            
        default:
            break;
    }
END_OF_ROUTINE:
#ifdef WIN32   
    LeaveCriticalSection(&log->cs);
#else
    pthread_mutex_unlock(&log->cs);
#endif
    return sts;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_LogCloseHandle
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
#undef  FUNC_NAME
#define FUNC_NAME "ENT_LogCloseHandle"
MSG_ID_T ENT_LogCloseHandle(ENT_LOG logHandle)
{
    MSG_ID_T  sts = 0;
    
    if(sLogMutexInit==false)
    {
        fprintf(stderr,"Func [%s] Line [%d],Uninitialized,please call ENT_LogInit.\n",FUNC_NAME,__LINE__);
        return -1;
    }
    
#ifdef WIN32
    EnterCriticalSection(&sLogMutex);
#else
    pthread_mutex_lock(&sLogMutex);
#endif
            
    ENT_LOG_CTX* log = (ENT_LOG_CTX*)logHandle;
    if(log == NULL)
    {
        if(sDefLog.isInit)
            log = &sDefLog;
        else
        {
            sts = -2;
            fprintf(stderr,"Func [%s] Line [%d],arguments is invalid.\n",FUNC_NAME,__LINE__);
            goto END_OF_ROUTINE;
        }
    }
    else if(log->tag!=ENTLOG_TAG || log->isInit == false)
    {
        sts = -2;
        fprintf(stderr,"Func [%s] Line [%d],arguments is invalid.\n",FUNC_NAME,__LINE__);
        goto END_OF_ROUTINE;
    }
    
    if(log->logFp)
        fclose(log->logFp);
    
    log->isInit = false;
    log->logFp  = NULL;
#ifdef WIN32
    DeleteCriticalSection(&log->cs);
#else
    pthread_mutex_destroy(&log->cs);
#endif
    
    if(log!=&sDefLog)
    {
        if(log->moduleName)
        {
            free(log->moduleName);
            log->moduleName = NULL;
        }
        if(log->logPath)
        {
            free(log->logPath);
            log->logPath = NULL;
        }
        free(log);
    }
    sLogNum--;
    
END_OF_ROUTINE:
#ifdef WIN32   
    LeaveCriticalSection(&sLogMutex);
#else
    pthread_mutex_unlock(&sLogMutex);
#endif
     
    return 0;  
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :iENT_LogGetCtx
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
#undef  FUNC_NAME
#define FUNC_NAME "iENT_LogGetCtx"
static MSG_ID_T iENT_LogGetCtx(ENT_LOG_CTX** logCtx,ENT_LOG logHandle)
{
    ENT_LOG_CTX* log = (ENT_LOG_CTX*)logHandle;
    *logCtx = NULL;
    
    if(sLogMutexInit==false)
    {
        fprintf(stderr,"Func [%s] Line [%d],Uninitialized,please call ENT_LogInit.\n",FUNC_NAME,__LINE__);
        return -1;
    }
    
    if(log == NULL)
    {
        if(sDefLog.isInit)
            log = &sDefLog;
        else
        {
            fprintf(stderr,"Func [%s] Line [%d],arguments is invalid.\n",FUNC_NAME,__LINE__);
            return -2;
        }
    }
    else if(log->tag!=ENTLOG_TAG || log->isInit==false)
    {
        fprintf(stderr,"Func [%s] Line [%d],arguments is invalid.\n",FUNC_NAME,__LINE__);
        return -2;
    }
    *logCtx = log;
    
    return 0; 
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :iENT_LogVRaw
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
#undef  FUNC_NAME
#define FUNC_NAME "iENT_LogVRaw"
static MSG_ID_T iENT_LogVRaw(ENT_LOG_CTX* log,const char* format,va_list va_args)
{   
#ifdef WIN32
    EnterCriticalSection(&log->cs);
#else
    pthread_mutex_lock(&log->cs);
#endif

    time_t  nowTime = time(NULL);
    
    iENT_LogRollCheck(log,nowTime);
    
    FILE* fp = log->logFp==NULL?stderr:log->logFp;
    
    vfprintf(fp,format,va_args);
    fflush(fp);
#ifdef WIN32   
    LeaveCriticalSection(&log->cs);
#else
    pthread_mutex_unlock(&log->cs);
#endif    
    
    return 0;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_LogRaw
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
#undef  FUNC_NAME
#define FUNC_NAME "ENT_LogRaw"
MSG_ID_T ENT_LogRaw(ENT_LOG logHandle,const char* format,...)
{
    MSG_ID_T  sts=0;
    ENT_LOG_CTX*  logCtx = NULL;
    
    sts = iENT_LogGetCtx(&logCtx,logHandle);
    if(sts<0)
    {
        return sts;
    }
    va_list va_args;
    va_start(va_args,format);
    sts = iENT_LogVRaw(logCtx,format,va_args);
    va_end(va_args);
    return sts;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_LogVPrint
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
#undef  FUNC_NAME
#define FUNC_NAME "ENT_LogVPrint"
static MSG_ID_T iENT_LogVPrint(ENT_LOG_CTX* log,ENT_LOG_LEV_E logLevel,const char* format,va_list va_args)
{   
    const static char* tmFormat="[Time %Y%m%d %H:%M:%S";
    const static int TM_STR_LEN=23;
    const static int TM_MILLITM_LEN=29;
    char tmpbuf[64];
#ifdef WIN32
    struct _timeb nowTmb;
#else
    struct timeval nowTmv;
#endif
    struct tm nowTm;
    
#ifdef WIN32
    EnterCriticalSection(&log->cs);
#else
    pthread_mutex_lock(&log->cs);
#endif
    
#ifdef WIN32
    _ftime(&nowTmb);
    iENT_LogRollCheck(log,nowTmb.time);
    localtime_s(&nowTm,&nowTmb.time);
    memset(tmpbuf,0,64);
    strftime( tmpbuf, 64,tmFormat, &nowTm);
    _snprintf_s(&tmpbuf[TM_STR_LEN],64-TM_STR_LEN-1,6,".%03d] ",nowTmb.millitm);
    _snprintf_s(&tmpbuf[TM_MILLITM_LEN],64-TM_MILLITM_LEN-1,32,"[%5s] [tid %5ld] ",sLogLevelStr[logLevel],GetCurrentThreadId());
#elif defined(__linux__)
    gettimeofday(&nowTmv,NULL);
    iENT_LogRollCheck(log,nowTmv.tv_sec);
    localtime_r(&nowTmv.tv_sec,&nowTm);
    memset(tmpbuf,0,64);
    strftime( tmpbuf, 64,tmFormat, &nowTm);
    snprintf(&tmpbuf[TM_STR_LEN],64-TM_STR_LEN-1,".%03d] ",nowTmv.tv_usec/1000);
    snprintf(&tmpbuf[TM_MILLITM_LEN],64-TM_MILLITM_LEN-1,"[%5s] [tid %5ld] ",sLogLevelStr[logLevel],(unsigned long int)pthread_self());
#endif
    
    if(log->isDebug)
    {
        printf(tmpbuf);
    }
    FILE* fp = log->logFp==NULL?stderr:log->logFp;
    
    fprintf(fp,tmpbuf);
    if(log->isDebug)
    {
        vprintf(format,va_args);
    }
    vfprintf(fp,format,va_args);
    fflush(fp);
#ifdef WIN32   
    LeaveCriticalSection(&log->cs);
#else
    pthread_mutex_unlock(&log->cs);
#endif    
    
    return 0;   
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_LogFatal
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
#undef  FUNC_NAME
#define FUNC_NAME "ENT_LogFatal"
MSG_ID_T ENT_LogFatal(ENT_LOG logHandle,const char* format,...)
{
    MSG_ID_T  sts=0;
    ENT_LOG_CTX*  logCtx = NULL;
    
    sts = iENT_LogGetCtx(&logCtx,logHandle);
    if(sts<0)
    {
        return sts;
    }
    if(LOG_LEV_FATAL_E>logCtx->logLevel)
    {
        return 1;
    }
    
    va_list va_args;
    va_start(va_args,format);
    sts = iENT_LogVPrint(logCtx,LOG_LEV_FATAL_E,format,va_args);
    va_end(va_args);
    return sts;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_LogError
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
#undef  FUNC_NAME
#define FUNC_NAME "ENT_LogError"
MSG_ID_T ENT_LogError(ENT_LOG logHandle,const char* format,...)
{
    MSG_ID_T  sts=0;
    ENT_LOG_CTX*  logCtx = NULL;
    
    sts = iENT_LogGetCtx(&logCtx,logHandle);
    if(sts<0)
    {
        return sts;
    }
    if(LOG_LEV_ERROR_E>logCtx->logLevel)//日志级别优先级高
    {
        return 1;
    }
    
    va_list va_args;
    va_start(va_args,format);
    sts = iENT_LogVPrint(logCtx,LOG_LEV_ERROR_E,format,va_args);
    va_end(va_args);
    return sts;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_LogWarn
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
#undef  FUNC_NAME
#define FUNC_NAME "ENT_LogWarn"
MSG_ID_T ENT_LogWarn(ENT_LOG logHandle,const char* format,...)
{
    MSG_ID_T  sts=0;
    ENT_LOG_CTX*  logCtx = NULL;
    
    sts = iENT_LogGetCtx(&logCtx,logHandle);
    if(sts<0)
    {
        return sts;
    }
    if(LOG_LEV_WARN_E>logCtx->logLevel)
    {
        return 1;
    }
    
    va_list va_args;
    va_start(va_args,format);
    sts = iENT_LogVPrint(logCtx,LOG_LEV_WARN_E,format,va_args);
    va_end(va_args);
    return sts;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_LogPrint
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
#undef  FUNC_NAME
#define FUNC_NAME "ENT_LogPrint"
MSG_ID_T ENT_LogPrint(ENT_LOG logHandle,const char* format,...)
{
    MSG_ID_T  sts=0;
    ENT_LOG_CTX*  logCtx = NULL;
    
    sts = iENT_LogGetCtx(&logCtx,logHandle);
    if(sts<0)
    {
        return sts;
    }
    if(LOG_LEV_INFO_E>logCtx->logLevel)
    {
        return 1;
    }
    
    va_list va_args;
    va_start(va_args,format);
    sts = iENT_LogVPrint(logCtx,LOG_LEV_INFO_E,format,va_args);
    va_end(va_args);
    return sts;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_LogDebug
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
#undef  FUNC_NAME
#define FUNC_NAME "ENT_LogDebug"
MSG_ID_T ENT_LogDebug(ENT_LOG logHandle,const char* format,...)
{
    MSG_ID_T  sts=0;
    ENT_LOG_CTX*  logCtx = NULL;
    
    sts = iENT_LogGetCtx(&logCtx,logHandle);
    if(sts<0)
    {
        return sts;
    }
    if(LOG_LEV_DEBUG_E>logCtx->logLevel)
    {
        return 1;
    }
    
    va_list va_args;
    va_start(va_args,format);
    sts = iENT_LogVPrint(logCtx,LOG_LEV_DEBUG_E,format,va_args);
    va_end(va_args);
    return sts;
}
#ifdef WIN32
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :UTL_MultiToWide
 *
 * DESCRIPTION :   
 *                 
 *                   
 *
 * COMPLETION
 * STATUS      :  Not NULL
 *                Success; Service has completed successfully.           
 *
 *                            
 *
 *-----------------------------------------------------------------------------
 */
#undef  FUNC_NAME
#define FUNC_NAME "UTL_MultiToWide"
static WCHAR* UTL_MultiToWide(_TCHAR** pwStr,const char* localStr)
{       
    if(localStr == NULL)
    {
        IENT_LOG_ERROR("arguments is invalid.\n");
        return NULL;
    }
        
    wchar_t *unicodeStr = NULL;
    int nRetLen = 0;
    nRetLen = MultiByteToWideChar(CP_ACP, 0, localStr, -1, NULL, 0);
    unicodeStr = (WCHAR *)malloc(nRetLen * sizeof(WCHAR));
    nRetLen = MultiByteToWideChar(CP_ACP, 0, localStr, -1, unicodeStr, nRetLen);
    if(pwStr)
        *pwStr = unicodeStr;
        
    return unicodeStr;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :iENT_LogPathCheck
 *
 * DESCRIPTION :   for windows
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
#undef  FUNC_NAME
#define FUNC_NAME "iENT_LogPathCheck"
static MSG_ID_T iENT_LogPathCheck(const char* path)
{
    HANDLE hDir = INVALID_HANDLE_VALUE;        

    if(path==NULL)
    {
        fprintf(stderr,"Func [%s] Line [%d],arguments is invalid.\n",FUNC_NAME,__LINE__);
        return -1;
    }
        
    WCHAR* wPath=NULL;
    UTL_MultiToWide(&wPath,path);
    int len = wcslen(wPath);
    if ((wPath[len - 1] == L'\\') || (wPath[len - 1] == L'/'))
    {
        wPath[len - 1] = L'\0';
    }
    
    hDir = CreateFile(wPath,
        GENERIC_READ,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        NULL
        );

    if (INVALID_HANDLE_VALUE == hDir)
    {
        if(!CreateDirectory(wPath, NULL))
        {
            fprintf(stderr,"Func [%s] Line [%d],Can not create log directory, invalid path name[%s]\n",FUNC_NAME,__LINE__,path);
            if(wPath) free(wPath);
            return -1;
        }
    }
    else
    {
        if(wPath) free(wPath);
        CloseHandle(hDir);
    }
    return 0;
}
#else
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :iENT_LogPathCheck
 *
 * DESCRIPTION :   for Linux
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
#undef  FUNC_NAME
#define FUNC_NAME "iENT_LogPathCheck"
static MSG_ID_T iENT_LogPathCheck(const char* path)
{   
    int ret = 0;     
    if(path==NULL)
    {
        fprintf(stderr,"Func [%s] Line [%d],arguments is invalid.\n",FUNC_NAME,__LINE__);
        return -1;
    }
        
    struct stat st = {0};

    if (stat(path, &st) == -1) 
    {
        ret=mkdir(path, 0777);
        if(ret==-1)
        {
             fprintf(stderr,"Func [%s] Line [%d],Can not create log directory [%s],msg->[%s]\n",FUNC_NAME,__LINE__,path,strerror(errno));
        }
    }
    return 0;
}
#endif
