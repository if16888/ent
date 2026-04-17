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
#include "ent_msg.h"

static MSG_ID_T iENT_DbBackendUnsupported(DB_TYPE dbType)
{
    IENT_LOG_ERROR("Database backend [%d] is not enabled in this build.\n",dbType);
    return ENT_DBS_UNSUPPORTED;
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
        return ENT_DBS_BAD_ARGUMENT;
    }
    if(dbCfg->dbType != SQLITE_TYPE)
    {
        IENT_LOG_ERROR("Arguments are invalid.\n");
        return ENT_DBS_BAD_HANDLE;
    }
    if(dbCfg->database == NULL)
    {
        IENT_LOG_ERROR("database name is invalid.\n");
        return ENT_DBS_BAD_ARGUMENT;
    }

    rc = sqlite3_open(dbCfg->database, &dbCfg->dbInstance.sqlite);
    if(rc)
    {
        IENT_LOG_ERROR("Can't open database:[%s]\n", sqlite3_errmsg(dbCfg->dbInstance.sqlite));
        sqlite3_close(dbCfg->dbInstance.sqlite);
        return ENT_DBS_OPEN_FAILED;
    }
    dbCfg->isOpen = true;
    return ENT_SYS_NORMAL;
#endif
}

MSG_ID_T ENT_DbSqliteClose(DB_HANDLE dbHandle)
{
#if !ENT_ENABLE_SQLITE
    (void)dbHandle;
    return iENT_DbBackendUnsupported(SQLITE_TYPE);
#else
    DB_CFG* dbCfg = (DB_CFG*)dbHandle;
    if(dbHandle == NULL)
    {
        IENT_LOG_ERROR("Arguments are invalid.\n");
        return ENT_DBS_BAD_ARGUMENT;
    }

    if(dbCfg->dbType != SQLITE_TYPE)
    {
        IENT_LOG_ERROR("Arguments are invalid.\n");
        return ENT_DBS_BAD_HANDLE;
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
    return ENT_SYS_NORMAL;
#endif
}

#if ENT_ENABLE_SQLITE
static MSG_ID_T iENT_DbSqliteReadCb(void *data, int argc, char **argv, char **azColName)
{
    USER_SQLITE_READ* userCtx = (USER_SQLITE_READ*)data;
    if(data == NULL || userCtx->userCb == NULL || userCtx->userData == NULL)
    {
        IENT_LOG_ERROR("Arguments are invalid.\n");
        return ENT_DBS_BAD_ARGUMENT;
    }
    userCtx->userCb(azColName, argv, 1, argc, userCtx->userData);
    return ENT_SYS_NORMAL;
}

static MSG_ID_T iENT_DbSqliteWriteCb(void *data, int argc, char **argv, char **azColName)
{
    USER_SQLITE_WRITE* userCtx = (USER_SQLITE_WRITE*)data;
    if(data == NULL || userCtx->userCb == NULL)
    {
        IENT_LOG_ERROR("Arguments are invalid.\n");
        return ENT_DBS_BAD_ARGUMENT;
    }

    userCtx->userCb(azColName, argv, 1, argc, userCtx->userData);
    return ENT_SYS_NORMAL;
}

MSG_ID_T ENT_DbSqliteRead(sqlite3* dbHandle,const char* query,SqlResultCB userCb,void* userData)
{
    int rc;
    char* errMsg = NULL;
    USER_SQLITE_READ sqliteUser;

    if(dbHandle == NULL || query == NULL || (userCb != NULL && userData == NULL))
    {
        IENT_LOG_ERROR("Arguments are invalid.\n");
        return ENT_DBS_BAD_ARGUMENT;
    }
    sqliteUser.userCb = userCb;
    sqliteUser.userData = userData;
    rc = sqlite3_exec(dbHandle, query, iENT_DbSqliteReadCb, &sqliteUser, &errMsg);
    if(rc != SQLITE_OK)
    {
        IENT_LOG_ERROR("Can't read table :[%d]->[%s]\n", rc, errMsg);
        sqlite3_free(errMsg);
        return ENT_DBS_QUERY_FAILED;
    }

    return ENT_SYS_NORMAL;
}

MSG_ID_T ENT_DbSqliteWrite(sqlite3* dbHandle,const char* query,SqlResultCB userCb,void* userData)
{
    MSG_ID_T sts = ENT_SYS_NORMAL;
    int rc;
    char* errMsg = NULL;
    USER_SQLITE_WRITE sqliteUser;

    if(dbHandle == NULL || query == NULL || (userCb != NULL && userData == NULL))
    {
        IENT_LOG_ERROR("Arguments are invalid.\n");
        return ENT_DBS_BAD_ARGUMENT;
    }

    sqliteUser.userCb = userCb;
    sqliteUser.userData = userData;

    rc = sqlite3_exec(dbHandle, query, iENT_DbSqliteWriteCb, &sqliteUser, &errMsg);
    if(rc != SQLITE_OK)
    {
        IENT_LOG_ERROR("Can't exec sql [%s] :[%d]->[%s]\n", query, rc, errMsg);
        sqlite3_free(errMsg);
        sts = ENT_DBS_QUERY_FAILED;
    }
    return sts;
}
#endif
