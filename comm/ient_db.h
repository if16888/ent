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
#ifndef _I_ENT_DB_H_
#define _I_ENT_DB_H_

#include <stdlib.h>
#include <stddef.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

#include "ent_db.h"
#include "ent_types.h"

#ifndef ENT_ENABLE_SQLITE
#define ENT_ENABLE_SQLITE 1
#endif

#ifndef ENT_ENABLE_MYSQL
#define ENT_ENABLE_MYSQL 1
#endif

#ifndef ENT_ENABLE_PGSQL
#define ENT_ENABLE_PGSQL 0
#endif

#if ENT_ENABLE_SQLITE && ENT_SQLITE_FOUND
#include "sqlite3.h"
#endif

#if ENT_ENABLE_MYSQL && ENT_MYSQL_FOUND
#include "mysql.h"
#endif

#if ENT_ENABLE_PGSQL && ENT_PGSQL_FOUND
#include <libpq-fe.h>
#endif

#define ENTDB_S_TAG (0xEADBEB90)
#define ENTDB_E_TAG (0xEB90EADB)

/* Result callbacks receive a fully materialized pointer matrix. Bound it before allocation. */
#ifndef ENT_DB_MAX_RESULT_CELLS
#define ENT_DB_MAX_RESULT_CELLS (1024u * 1024u)
#endif

/* Bound the copied payload held by the callback materialization layer. */
#ifndef ENT_DB_MAX_RESULT_BYTES
#define ENT_DB_MAX_RESULT_BYTES (64u * 1024u * 1024u)
#endif

typedef enum DB_HANDLE_STATE_TAG
{
    ENT_DB_HANDLE_CREATED_E = 0,
    ENT_DB_HANDLE_ACTIVE_E  = 1,
    ENT_DB_HANDLE_CLOSING_E = 2,
    ENT_DB_HANDLE_CLOSED_E  = 3
} DB_HANDLE_STATE_E;

typedef struct DB_CFG
{
    unsigned int sTag;
    DB_TYPE dbType;
    char* userName;
    char* passwd;
    char* host;
    char* database;
    int portNo;
    bool isInit;
#ifdef _WIN32
    CRITICAL_SECTION cs;
    CRITICAL_SECTION lifecycleCs;
    CONDITION_VARIABLE lifecycleCv;
#else
    pthread_mutex_t cs;
    pthread_mutex_t lifecycleCs;
    pthread_cond_t lifecycleCv;
#endif
    bool isOpen;
    DB_HANDLE_STATE_E handleState;
    long activeOps;
    union
    {
#if ENT_ENABLE_SQLITE && ENT_SQLITE_FOUND
        sqlite3* sqlite;
#endif
#if ENT_ENABLE_MYSQL && ENT_MYSQL_FOUND
        MYSQL* mysql;
#endif
#if ENT_ENABLE_PGSQL && ENT_PGSQL_FOUND
        PGconn* pgsql;
#endif
        void* raw;
    } dbInstance;
    unsigned int eTag;
} DB_CFG;

typedef struct USER_SQLITE_DATA
{
    SqlResultCB userCb;
    void* userData;
} USER_SQLITE_READ, USER_SQLITE_WRITE;

static int iENT_DbCheckedSizeMultiply(size_t left, size_t right, size_t* result)
{
    if(result == NULL || (left != 0 && right > ((size_t)-1) / left))
    {
        return 0;
    }
    *result = left * right;
    return 1;
}

static int iENT_DbCheckedSizeAdd(size_t left, size_t right, size_t* result)
{
    if(result == NULL || right > ((size_t)-1) - left)
    {
        return 0;
    }
    *result = left + right;
    return 1;
}

MSG_ID_T ENT_DbSqliteInit(DB_HANDLE dbHandle);
MSG_ID_T ENT_DbSqliteClose(DB_HANDLE dbHandle);
#if ENT_ENABLE_SQLITE && ENT_SQLITE_FOUND
MSG_ID_T ENT_DbSqliteRead(sqlite3* dbHandle,
                          const char* query,
                          SqlResultCB userCb,
                          void* userData);
MSG_ID_T ENT_DbSqliteWrite(sqlite3* dbHandle,
                           const char* query,
                           SqlResultCB userCb,
                           void* userData);
MSG_ID_T ENT_DbSqliteReadParams(sqlite3* dbHandle,
                                const char* sql,
                                const ENT_DB_PARAM* params,
                                size_t paramCount,
                                SqlResultCB userCb,
                                void* userData);
MSG_ID_T ENT_DbSqliteWriteParams(sqlite3* dbHandle,
                                 const char* sql,
                                 const ENT_DB_PARAM* params,
                                 size_t paramCount,
                                 SqlResultCB userCb,
                                 void* userData);
#endif

#if ENT_ENABLE_MYSQL && ENT_MYSQL_FOUND
MSG_ID_T ENT_DbMySQLInit(DB_HANDLE dbHandle);
MSG_ID_T ENT_DbMySQLClose(DB_HANDLE dbHandle);
MSG_ID_T ENT_DbMySQLRead(MYSQL* dbHandle,
                         const char* query,
                         SqlResultCB userCb,
                         void* userData);
MSG_ID_T ENT_DbMySQLWrite(MYSQL* dbHandle,
                          const char* query,
                          SqlResultCB userCb,
                          void* userData);
MSG_ID_T ENT_DbMySQLReadParams(MYSQL* dbHandle,
                               const char* sql,
                               const ENT_DB_PARAM* params,
                               size_t paramCount,
                               SqlResultCB userCb,
                               void* userData);
MSG_ID_T ENT_DbMySQLWriteParams(MYSQL* dbHandle,
                                const char* sql,
                                const ENT_DB_PARAM* params,
                                size_t paramCount,
                                SqlResultCB userCb,
                                void* userData);
#endif

#if ENT_ENABLE_PGSQL && ENT_PGSQL_FOUND
MSG_ID_T ENT_DbPgSQLInit(DB_HANDLE dbHandle);
MSG_ID_T ENT_DbPgSQLClose(DB_HANDLE dbHandle);
MSG_ID_T ENT_DbPgSQLRead(void* dbHandle,
                         const char* query,
                         SqlResultCB userCb,
                         void* userData);
MSG_ID_T ENT_DbPgSQLWrite(void* dbHandle,
                          const char* query,
                          SqlResultCB userCb,
                          void* userData);
MSG_ID_T ENT_DbPgSQLReadParams(void* dbHandle,
                               const char* sql,
                               const ENT_DB_PARAM* params,
                               size_t paramCount,
                               SqlResultCB userCb,
                               void* userData);
MSG_ID_T ENT_DbPgSQLWriteParams(void* dbHandle,
                                const char* sql,
                                const ENT_DB_PARAM* params,
                                size_t paramCount,
                                SqlResultCB userCb,
                                void* userData);
#endif

extern long sDbNum;

#endif
