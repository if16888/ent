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
#ifndef _ENT_DB_H_
#define _ENT_DB_H_

#include <stddef.h>

#include "ent_comm.h"

/* Library build defaults. Backend-specific limits may be lower. */
#ifndef ENT_DB_MAX_PARAMS
#define ENT_DB_MAX_PARAMS 4096u
#endif
#ifndef ENT_DB_MAX_FIELD_BYTES
#define ENT_DB_MAX_FIELD_BYTES (16u * 1024u * 1024u)
#endif
#ifndef ENT_DB_MAX_INPUT_BYTES
#define ENT_DB_MAX_INPUT_BYTES (64u * 1024u * 1024u)
#endif

typedef enum DB_TYPE
{
  SQLITE_TYPE = 1,
  MYSQL_TYPE  = 2,
  MSSQL_TYPE  = 3,
  ORACLE_TYPE = 4,
  PGSQL_TYPE  = 5
} DB_TYPE;

typedef struct DB_READ_HEADER
{
    ENT_BOOL isFetchMore;// whether more rows are available
}DB_READ_HEADER;

typedef void(* SqlResultCB)(char** fields,char** rowRes,long long rowNum,int columnNum,void* userData);

typedef enum ENT_DB_PARAM_TYPE_TAG
{
    ENT_DB_PARAM_TEXT_E   = 1,
    ENT_DB_PARAM_INT_E    = 2,
    ENT_DB_PARAM_INT64_E  = 3,
    ENT_DB_PARAM_DOUBLE_E = 4,
    ENT_DB_PARAM_NULL_E   = 5,
    ENT_DB_PARAM_BLOB_E   = 6
} ENT_DB_PARAM_TYPE;

typedef struct ENT_DB_PARAM_TAG
{
    ENT_DB_PARAM_TYPE type;
    union
    {
        const char* text;
        int         i32;
        long long   i64;
        double      d64;
        struct
        {
            const void* data;
            size_t      size;
        } blob;
    } value;
} ENT_DB_PARAM;

#ifdef __cplusplus
extern "C" {
#endif

ENT_PUBLIC MSG_ID_T  ENT_DbInit(void);

ENT_PUBLIC MSG_ID_T  ENT_DbClose(void);

/* Output-only: the slot is set to NULL before initialization, so it may be an
 * uninitialized local variable. Close any existing handle before reusing its
 * variable, since this function does not inspect or close prior contents. */
ENT_PUBLIC MSG_ID_T  ENT_DbInitHandle(DB_HANDLE* pdbHandle,
                      DB_TYPE dbType,
                      const char* host,
                      const char* database,
                      const char* user,
                      const char* passwd,
                      int  port);

ENT_PUBLIC MSG_ID_T  ENT_DbOpen(DB_HANDLE dbHandle);

ENT_PUBLIC MSG_ID_T  ENT_DbCloseHandle(DB_HANDLE* dbHandle);

ENT_PUBLIC MSG_ID_T  ENT_DbRead(DB_HANDLE dbHandle,const char* sql,SqlResultCB sqlCb,void* userData);

ENT_PUBLIC MSG_ID_T ENT_DbWrite(DB_HANDLE dbHandle,const char* sql,SqlResultCB sqlCb,void* userData);

/* Arrays/text/blob storage must remain valid until the call returns. Input
 * budget/count violations return ENT_DBS_BAD_PARAMS before opening/executing.
 * Text/blob fields are limited to ENT_DB_MAX_FIELD_BYTES. Materialized results
 * are limited to 1,048,576 cells / 64 MiB field payload including terminators;
 * excess results return ENT_DBS_ALLOC_FAILED without invoking the callback.
 * Callbacks borrow storage only for their duration; do not retain pointers or
 * reenter the same handle. Owner synchronization must fence new calls from
 * close; close waits for already admitted operations and nulls the handle. */
ENT_PUBLIC MSG_ID_T ENT_DbReadParams(DB_HANDLE dbHandle,
                                     const char* sql,
                                     const ENT_DB_PARAM* params,
                                     size_t paramCount,
                                     SqlResultCB sqlCb,
                                     void* userData);

ENT_PUBLIC MSG_ID_T ENT_DbWriteParams(DB_HANDLE dbHandle,
                                      const char* sql,
                                      const ENT_DB_PARAM* params,
                                      size_t paramCount,
                                      SqlResultCB sqlCb,
                                      void* userData);

#ifdef __cplusplus
}
#endif

#endif
