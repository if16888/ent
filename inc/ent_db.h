#ifndef _ENT_DB_H_
#define _ENT_DB_H_

#include "ent_comm.h"

typedef enum DB_TYPE
{
  SQLITE_TYPE = 1,
  MYSQL_TYPE  = 2,
  MSSQL_TYPE  = 3,
  ORACLE_TYPE = 4
} DB_TYPE;

typedef struct DB_READ_HEADER
{
    BOOL isFetchMore;//是否有更多记录
}DB_READ_HEADER;

typedef void(* SqlResultCB)(char** fields,char** rowRes,long long rowNum,int columnNum,void* userData);

#ifdef __cplusplus
extern "C" {
#endif

ENT_PUBLIC MSG_ID_T  ENT_DbInit();

ENT_PUBLIC MSG_ID_T  ENT_DbClose();

ENT_PUBLIC MSG_ID_T  ENT_DbInitHandle(DB_HANDLE* pdbHandle,
                     DB_TYPE dbType,
                     const char* host,
                     const char* database,
                     const char* user,
                     const char* passwd,
                     int  port);

ENT_PUBLIC MSG_ID_T  ENT_DbOpen(DB_HANDLE dbHandle);

ENT_PUBLIC MSG_ID_T  ENT_DbCloseHandle(DB_HANDLE dbHandle);

ENT_PUBLIC MSG_ID_T  ENT_DbRead(DB_HANDLE dbHandle,const char* sql,SqlResultCB sqlCb,void* userData);

ENT_PUBLIC MSG_ID_T ENT_DbWrite(DB_HANDLE dbHandle,const char* sql,SqlResultCB sqlCb,void* userData);

#ifdef __cplusplus
}
#endif

#endif