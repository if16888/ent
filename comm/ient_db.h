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

#ifdef WIN32
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

#if ENT_ENABLE_SQLITE
#include "sqlite3.h"
#endif

#if ENT_ENABLE_MYSQL
#include "mysql.h"
#endif

#if ENT_ENABLE_PGSQL
#include <libpq-fe.h>
#endif

#define ENTDB_S_TAG (0xEADBEB90)
#define ENTDB_E_TAG (0xEB90EADB)

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
#ifdef WIN32
    CRITICAL_SECTION cs;
#else
    pthread_mutex_t cs;
#endif
    bool isOpen;
    union
    {
#if ENT_ENABLE_SQLITE
        sqlite3* sqlite;
#endif
#if ENT_ENABLE_MYSQL
        MYSQL* mysql;
#endif
#if ENT_ENABLE_PGSQL
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

MSG_ID_T ENT_DbSqliteInit(DB_HANDLE dbHandle);
MSG_ID_T ENT_DbSqliteClose(DB_HANDLE dbHandle);
#if ENT_ENABLE_SQLITE
MSG_ID_T ENT_DbSqliteRead(sqlite3* dbHandle,
                          const char* query,
                          SqlResultCB userCb,
                          void* userData);
MSG_ID_T ENT_DbSqliteWrite(sqlite3* dbHandle,
                           const char* query,
                           SqlResultCB userCb,
                           void* userData);
#endif

#if ENT_ENABLE_MYSQL
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
#endif

#if ENT_ENABLE_PGSQL
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
#endif

#endif
