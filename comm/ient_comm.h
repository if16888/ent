
#ifndef _I_ENT_COMM_H_
#define _I_ENT_COMM_H_
#include <stdlib.h>
#include <string.h>
#include "ent_log.h"
#include "ent_utility.h"

typedef struct ENT_CTX {
    bool           isInit;
    char*          entName;
    char*          workPath;
    char*          logName;
    char*          logPath;
    ENT_LOG_LEV_E  logLevel;       
    ENT_LOG        entLog; 
    UTL_LOCK       entLock;
    UTL_CV         entCV;   
} ENT_CTX;

extern ENT_CTX gEntCtx;

#ifdef WIN32

#define IENT_LOG_FATAL(format,...) \
 do { \
    ENT_LogFatal(gEntCtx.entLog,"Func [%s] Line [%d]," format,__FUNCTION__,__LINE__,__VA_ARGS__);\
 }\
 while(0)
 
#define IENT_LOG_ERROR(format,...) \
 do { \
    ENT_LogError(gEntCtx.entLog,"Func [%s] Line [%d]," format,__FUNCTION__,__LINE__,__VA_ARGS__);\
 }\
 while(0)
 
#define IENT_LOG_WARN(format,...) \
 do { \
    ENT_LogWarn(gEntCtx.entLog,"Func [%s] Line [%d]," format,__FUNCTION__,__LINE__,__VA_ARGS__);\
 }\
 while(0)
 
#define IENT_LOG_PRINT(format,...) \
 do { \
    ENT_LogPrint(gEntCtx.entLog,"Func [%s] Line [%d]," format,__FUNCTION__,__LINE__,__VA_ARGS__);\
 }\
 while(0)
 
#define IENT_LOG_DEBUG(format,...) \
 do { \
    ENT_LogDebug(gEntCtx.entLog,"Func [%s] Line [%d]," format,__FUNCTION__,__LINE__,__VA_ARGS__);\
 }\
 while(0)
#elif defined(__linux__)

#define IENT_LOG_FATAL(format,...) \
 do { \
    ENT_LogFatal(gEntCtx.entLog,"Func [%s] Line [%d]," format,__func__,__LINE__,##__VA_ARGS__);\
 }\
 while(0)
 
#define IENT_LOG_ERROR(format,...) \
 do { \
    ENT_LogError(gEntCtx.entLog,"Func [%s] Line [%d]," format,__func__,__LINE__,##__VA_ARGS__);\
 }\
 while(0)
 
#define IENT_LOG_WARN(format,...) \
 do { \
    ENT_LogWarn(gEntCtx.entLog,"Func [%s] Line [%d]," format,__func__,__LINE__,##__VA_ARGS__);\
 }\
 while(0)
 
#define IENT_LOG_PRINT(format,...) \
 do { \
    ENT_LogPrint(gEntCtx.entLog,"Func [%s] Line [%d]," format,__func__,__LINE__,##__VA_ARGS__);\
 }\
 while(0)
 
#define IENT_LOG_DEBUG(format,...) \
 do { \
    ENT_LogDebug(gEntCtx.entLog,"Func [%s] Line [%d]," format,__func__,__LINE__,##__VA_ARGS__);\
 }\
 while(0)
#endif

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
