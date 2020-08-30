#ifndef  _ENT_LOG_H_
#define  _ENT_LOG_H_

#include "ent_comm.h"
#include "ent_types.h"

typedef void*  ENT_LOG;

typedef enum
{
    ENT_LOG_DEBUG_E = 0x01,
    ENT_LOG_PATH_E,
    ENT_LOG_MAX_E,
    ENT_LOG_BUFFER_E,
    ENT_LOG_LEVEL_E,
} ENT_LOG_OPTIONS_E;

typedef enum
{
    LOG_LEV_FATAL_E = 0x0,
    LOG_LEV_ERROR_E,
    LOG_LEV_WARN_E,
    LOG_LEV_INFO_E,
    LOG_LEV_DEBUG_E,
}ENT_LOG_LEV_E;

#ifdef __cplusplus
extern "C" {
#endif

ENT_PUBLIC MSG_ID_T  ENT_LogInit();

ENT_PUBLIC MSG_ID_T  ENT_LogClose();

ENT_PUBLIC MSG_ID_T  ENT_LogInitHandle(ENT_LOG* pLogHandle,const char* moduleName,const char* logPath);

ENT_PUBLIC MSG_ID_T  ENT_LogSetOption(ENT_LOG logHandle,ENT_LOG_OPTIONS_E option,const void* arg);

ENT_PUBLIC MSG_ID_T  ENT_LogCloseHandle(ENT_LOG logHandle);

ENT_PUBLIC MSG_ID_T  ENT_LogRaw(ENT_LOG logHandle,const char* format,...);

ENT_PUBLIC MSG_ID_T  ENT_LogFatal(ENT_LOG logHandle,const char* format,...);

ENT_PUBLIC MSG_ID_T  ENT_LogError(ENT_LOG logHandle,const char* format,...);

ENT_PUBLIC MSG_ID_T  ENT_LogWarn(ENT_LOG logHandle,const char* format,...);

ENT_PUBLIC MSG_ID_T  ENT_LogPrint(ENT_LOG logHandle,const char* format,...);

ENT_PUBLIC MSG_ID_T  ENT_LogDebug(ENT_LOG logHandle,const char* format,...);

#ifdef __cplusplus
}
#endif

#ifdef WIN32

#define ENT_LOG_FATAL(format,...) \
 do { \
    ENT_LogFatal(NULL,"Func [%s] Line [%d]," format,__FUNCTION__,__LINE__,__VA_ARGS__);\
 }\
 while(0)
 
#define ENT_LOG_ERROR(format,...) \
 do { \
    ENT_LogError(NULL,"Func [%s] Line [%d]," format,__FUNCTION__,__LINE__,__VA_ARGS__);\
 }\
 while(0)
 
#define ENT_LOG_WARN(format,...) \
 do { \
    ENT_LogWarn(NULL,"Func [%s] Line [%d]," format,__FUNCTION__,__LINE__,__VA_ARGS__);\
 }\
 while(0)
 
#define ENT_LOG_PRINT(format,...) \
 do { \
    ENT_LogPrint(NULL,"Func [%s] Line [%d]," format,__FUNCTION__,__LINE__,__VA_ARGS__);\
 }\
 while(0)
 
#define ENT_LOG_DEBUG(format,...) \
 do { \
    ENT_LogDebug(NULL,"Func [%s] Line [%d]," format,__FUNCTION__,__LINE__,__VA_ARGS__);\
 }\
 while(0)
#elif defined(__linux__)

#define ENT_LOG_FATAL(format,...) \
 do { \
    ENT_LogFatal(NULL,"Func [%s] Line [%d]," format,__func__,__LINE__,##__VA_ARGS__);\
 }\
 while(0)
 
#define ENT_LOG_ERROR(format,...) \
 do { \
    ENT_LogError(NULL,"Func [%s] Line [%d]," format,__func__,__LINE__,##__VA_ARGS__);\
 }\
 while(0)
 
#define ENT_LOG_WARN(format,...) \
 do { \
    ENT_LogWarn(NULL,"Func [%s] Line [%d]," format,__func__,__LINE__,##__VA_ARGS__);\
 }\
 while(0)
 
#define ENT_LOG_PRINT(format,...) \
 do { \
    ENT_LogPrint(NULL,"Func [%s] Line [%d]," format,__func__,__LINE__,##__VA_ARGS__);\
 }\
 while(0)
 
#define ENT_LOG_DEBUG(format,...) \
 do { \
    ENT_LogDebug(NULL,"Func [%s] Line [%d]," format,__func__,__LINE__,##__VA_ARGS__);\
 }\
 while(0)
#endif
#endif