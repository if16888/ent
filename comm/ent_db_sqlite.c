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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ient_db.h"
#include "ient_comm.h"

static MSG_ID_T iENT_DbBackendUnsupported(DB_TYPE dbType)
{
    IENT_LOG_ERROR("Database backend [%d] is not enabled in this build.\n",dbType);
    return -2;
}

MSG_ID_T ENT_DbSqliteInit(DB_HANDLE dbHandle)
{
#if !ENT_ENABLE_SQLITE
    (void)dbHandle;
    return iENT_DbBackendUnsupported(SQLITE_TYPE);
#else
    int rc;
    DB_CFG* dbCfg = (DB_CFG*)dbHandle;

    if(dbHandle == NULL)
    {
        IENT_LOG_ERROR("Arguments are invalid.\n");
        return -1;
    }
    if(dbCfg->database == NULL)
    {
        IENT_LOG_ERROR("database name is invalid.\n");
        return -2;
    }

    rc = sqlite3_open(dbCfg->database, &dbCfg->dbInstance.sqlite);
    if(rc)
    {
        IENT_LOG_ERROR("Can't open database:[%s]\n", sqlite3_errmsg(dbCfg->dbInstance.sqlite));
        sqlite3_close(dbCfg->dbInstance.sqlite);
        return -3;
    }
    dbCfg->isOpen = true;
    return 0;
#endif
}

MSG_ID_T ENT_DbSqliteClose(DB_HANDLE dbHandle)
{
#if !ENT_ENABLE_SQLITE
    (void)dbHandle;
    return iENT_DbBackendUnsupported(SQLITE_TYPE);
#else
    DB_CFG* dbCfg = (DB_CFG*)dbHandle;
    if(dbHandle == NULL ||
       dbCfg->dbType != SQLITE_TYPE)
    {
        IENT_LOG_ERROR("Arguments are invalid.\n");
        return -1;
    }

    if(dbCfg->dbInstance.sqlite)
    {
        sqlite3_close(dbCfg->dbInstance.sqlite);
        dbCfg->dbInstance.sqlite = NULL;
    }
    dbCfg->isOpen = false;

    if(dbCfg->host)
        free(dbCfg->host);
    if(dbCfg->database)
        free(dbCfg->database);
    if(dbCfg->userName)
        free(dbCfg->userName);
    if(dbCfg->passwd)
        free(dbCfg->passwd);

#ifdef WIN32
    DeleteCriticalSection(&dbCfg->cs);
#else
    pthread_mutex_destroy(&dbCfg->cs);
#endif

    sDbNum--;
    return 0;
#endif
}

#if ENT_ENABLE_SQLITE
static MSG_ID_T iENT_DbSqliteReadCb(void *data, int argc, char **argv, char **azColName)
{
    USER_SQLITE_READ* userCtx = (USER_SQLITE_READ*)data;
    if(data == NULL || userCtx->userCb == NULL || userCtx->userData == NULL)
    {
        IENT_LOG_ERROR("Arguments are invalid.\n");
        return -1;
    }
    userCtx->userCb(azColName, argv, 1, argc, userCtx->userData);
    return 0;
}

static MSG_ID_T iENT_DbSqliteWriteCb(void *data, int argc, char **argv, char **azColName)
{
    USER_SQLITE_WRITE* userCtx = (USER_SQLITE_WRITE*)data;
    if(data == NULL || userCtx->userCb == NULL)
    {
        IENT_LOG_ERROR("Arguments are invalid.\n");
        return -1;
    }

    userCtx->userCb(azColName, argv, 1, argc, userCtx->userData);
    return 0;
}

MSG_ID_T ENT_DbSqliteRead(sqlite3* dbHandle,const char* query,SqlResultCB userCb,void* userData)
{
    int rc;
    char* errMsg = NULL;
    USER_SQLITE_READ sqliteUser;

    if(dbHandle == NULL || query == NULL)
    {
        IENT_LOG_ERROR("Arguments are invalid.\n");
        return -1;
    }
    sqliteUser.userCb = userCb;
    sqliteUser.userData = userData;
    rc = sqlite3_exec(dbHandle, query, iENT_DbSqliteReadCb, &sqliteUser, &errMsg);
    if(rc != SQLITE_OK)
    {
        IENT_LOG_ERROR("Can't read table :[%d]->[%s]\n", rc, errMsg);
        sqlite3_free(errMsg);
        return -2;
    }

    return 0;
}

MSG_ID_T ENT_DbSqliteWrite(sqlite3* dbHandle,const char* query,SqlResultCB userCb,void* userData)
{
    MSG_ID_T sts = 0;
    int rc;
    char* errMsg = NULL;
    USER_SQLITE_WRITE sqliteUser;

    if(dbHandle == NULL || query == NULL)
    {
        IENT_LOG_ERROR("Arguments are invalid.\n");
        return -1;
    }

    sqliteUser.userCb = userCb;
    sqliteUser.userData = userData;

    rc = sqlite3_exec(dbHandle, query, iENT_DbSqliteWriteCb, &sqliteUser, &errMsg);
    if(rc != SQLITE_OK)
    {
        IENT_LOG_ERROR("Can't exec sql [%s] :[%d]->[%s]\n", query, rc, errMsg);
        sqlite3_free(errMsg);
        sts = -2;
    }
    return sts;
}
#endif
