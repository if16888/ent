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
#ifndef _I_ENT_COMM_H_
#define _I_ENT_COMM_H_
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <sys/timeb.h>
#endif
#include "ient_runtime.h"
#include "ent_log.h"
#include "ent_utility.h"

#ifdef _WIN32

#define IENT_LOG_FATAL(format,...) \
 do { \
    ENT_LogFatal(iENT_LogDefaultHandle(),"Func [%s] Line [%d]," format,__FUNCTION__,__LINE__,##__VA_ARGS__);\
 }\
 while(0)
 
#define IENT_LOG_ERROR(format,...) \
 do { \
    ENT_LogError(iENT_LogDefaultHandle(),"Func [%s] Line [%d]," format,__FUNCTION__,__LINE__,##__VA_ARGS__);\
 }\
 while(0)
 
#define IENT_LOG_WARN(format,...) \
 do { \
    ENT_LogWarn(iENT_LogDefaultHandle(),"Func [%s] Line [%d]," format,__FUNCTION__,__LINE__,##__VA_ARGS__);\
 }\
 while(0)
 
#define IENT_LOG_PRINT(format,...) \
 do { \
    ENT_LogPrint(iENT_LogDefaultHandle(),"Func [%s] Line [%d]," format,__FUNCTION__,__LINE__,##__VA_ARGS__);\
 }\
 while(0)
 
#define IENT_LOG_DEBUG(format,...) \
 do { \
    ENT_LogDebug(iENT_LogDefaultHandle(),"Func [%s] Line [%d]," format,__FUNCTION__,__LINE__,##__VA_ARGS__);\
 }\
 while(0)
#else

#define IENT_LOG_FATAL(format,...) \
 do { \
    ENT_LogFatal(iENT_LogDefaultHandle(),"Func [%s] Line [%d]," format,__func__,__LINE__,##__VA_ARGS__);\
 }\
 while(0)
 
#define IENT_LOG_ERROR(format,...) \
 do { \
    ENT_LogError(iENT_LogDefaultHandle(),"Func [%s] Line [%d]," format,__func__,__LINE__,##__VA_ARGS__);\
 }\
 while(0)
 
#define IENT_LOG_WARN(format,...) \
 do { \
    ENT_LogWarn(iENT_LogDefaultHandle(),"Func [%s] Line [%d]," format,__func__,__LINE__,##__VA_ARGS__);\
 }\
 while(0)
 
#define IENT_LOG_PRINT(format,...) \
 do { \
    ENT_LogPrint(iENT_LogDefaultHandle(),"Func [%s] Line [%d]," format,__func__,__LINE__,##__VA_ARGS__);\
 }\
 while(0)
 
#define IENT_LOG_DEBUG(format,...) \
 do { \
    ENT_LogDebug(iENT_LogDefaultHandle(),"Func [%s] Line [%d]," format,__func__,__LINE__,##__VA_ARGS__);\
 }\
 while(0)
#endif

static inline char* ENT_StrDup(const char* text)
{
    if(text == NULL)
    {
        return NULL;
    }
#ifdef _WIN32
    return _strdup(text);
#else
    return strdup(text);
#endif
}

static inline FILE* ENT_FOpen(const char* fileName, const char* mode)
{
    if(fileName == NULL || mode == NULL)
    {
        return NULL;
    }
#ifdef _WIN32
    FILE* fp = NULL;
    if(fopen_s(&fp, fileName, mode) != 0)
    {
        return NULL;
    }
    return fp;
#else
    return fopen(fileName, mode);
#endif
}

#ifdef _WIN32
static inline int ENT_FTime64(struct _timeb* timeBuf)
{
    return _ftime64_s(timeBuf);
}
#endif

static inline char* ENT_GetEnvDup(const char* name)
{
    if(name == NULL || name[0] == '\0')
    {
        return NULL;
    }

#ifdef _WIN32
    char* value = NULL;
    size_t valueLen = 0;

    if(_dupenv_s(&value, &valueLen, name) != 0 || value == NULL || value[0] == '\0')
    {
        free(value);
        return NULL;
    }
    return value;
#else
    const char* value = getenv(name);
    return (value == NULL || value[0] == '\0') ? NULL : ENT_StrDup(value);
#endif
}

static inline void* UTL_Malloc(void* oldMem,size_t* num,int size,size_t reqNum)
{
    char* newMem;
    if(num == NULL || size<=0)
    {
        IENT_LOG_ERROR("arguments is invalid.\n");
        return NULL;
    }
    size_t newNum = reqNum;
    size_t oldNum = *num;
    if(*num<reqNum)
    {
        newNum = (reqNum*2+7)&0xFFFFFFF8;
    }
    else
    {
        return oldMem;
    }
    
    newMem = (char*)realloc(oldMem,newNum*size);
    if(newMem!=NULL)
    {
        memset(newMem+oldNum*size,0,(newNum-oldNum)*size);
    }
    *num = newNum;
    return newMem;    
}

#endif
