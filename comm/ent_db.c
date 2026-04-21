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
#include <memory.h>
#include <time.h>

#ifdef WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <errno.h>
#endif

#include "ient_db.h"
#include "ient_comm.h"
#include "ent_msg.h"

#ifdef WIN32
static CRITICAL_SECTION sDbMutex;
#pragma warning(disable : 4996)
#else
static pthread_mutex_t  sDbMutex;
#endif

long                    sDbNum = 0;
volatile static bool    sDbMutexInit = false;
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :defSqlResultCb
 *
 * DESCRIPTION :
 *
 * COMPLETION
 * STATUS      :  void
 *
 *-----------------------------------------------------------------------------
 */
static void defSqlResultCb(char** fields,char** rowRes,long long rowNum,int columnNum,void* data)
{
    printf("Affect rows [%lld]\n",rowNum);
    if(fields ==NULL || rowRes == NULL)
    {
        return;
    }
    int rowIdx = 0;
    int colIdx = 0;

    for(colIdx = 0; colIdx<columnNum; colIdx++)
    {
        printf("%s ",fields[colIdx]);
    }
    printf("\n");

    for(rowIdx = 0; rowIdx < rowNum; rowIdx++)
    {
        for(colIdx = 0; colIdx<columnNum; colIdx++)
        {
            printf("%s ",rowRes[rowIdx*columnNum+colIdx]);
        }
        printf("\n");
    }
    (void)data;
}

static MSG_ID_T iENT_DbBackendUnsupported(DB_TYPE dbType)
{
    IENT_LOG_ERROR("Database backend [%d] is not enabled in this build.\n",dbType);
    return ENT_DBS_UNSUPPORTED;
}

static void iENT_DbGlobalLock(void)
{
#ifdef WIN32
    EnterCriticalSection(&sDbMutex);
#else
    pthread_mutex_lock(&sDbMutex);
#endif
}

static void iENT_DbGlobalUnlock(void)
{
#ifdef WIN32
    LeaveCriticalSection(&sDbMutex);
#else
    pthread_mutex_unlock(&sDbMutex);
#endif
}

static void iENT_DbHandleLock(DB_CFG* dbCfg)
{
#ifdef WIN32
    EnterCriticalSection(&dbCfg->cs);
#else
    pthread_mutex_lock(&dbCfg->cs);
#endif
}

static void iENT_DbHandleUnlock(DB_CFG* dbCfg)
{
#ifdef WIN32
    LeaveCriticalSection(&dbCfg->cs);
#else
    pthread_mutex_unlock(&dbCfg->cs);
#endif
}

static MSG_ID_T iENT_DbLifecycleInit(DB_CFG* dbCfg)
{
    if(dbCfg == NULL)
    {
        return ENT_DBS_BAD_ARGUMENT;
    }
#ifdef WIN32
    InitializeCriticalSection(&dbCfg->lifecycleCs);
    InitializeConditionVariable(&dbCfg->lifecycleCv);
    return ENT_SYS_NORMAL;
#else
    if(pthread_mutex_init(&dbCfg->lifecycleCs, NULL) != 0)
    {
        IENT_LOG_ERROR("pthread_mutex_init failed for DB lifecycle lock.\n");
        return ENT_UTHD_INIT_FAILED;
    }
    if(pthread_cond_init(&dbCfg->lifecycleCv, NULL) != 0)
    {
        IENT_LOG_ERROR("pthread_cond_init failed for DB lifecycle cv.\n");
        pthread_mutex_destroy(&dbCfg->lifecycleCs);
        return ENT_UTHD_INIT_FAILED;
    }
    return ENT_SYS_NORMAL;
#endif
}

static void iENT_DbLifecycleDestroy(DB_CFG* dbCfg)
{
    if(dbCfg == NULL)
    {
        return;
    }
#ifdef WIN32
    DeleteCriticalSection(&dbCfg->lifecycleCs);
#else
    pthread_cond_destroy(&dbCfg->lifecycleCv);
    pthread_mutex_destroy(&dbCfg->lifecycleCs);
#endif
}

static void iENT_DbLifecycleLock(DB_CFG* dbCfg)
{
#ifdef WIN32
    EnterCriticalSection(&dbCfg->lifecycleCs);
#else
    pthread_mutex_lock(&dbCfg->lifecycleCs);
#endif
}

static void iENT_DbLifecycleUnlock(DB_CFG* dbCfg)
{
#ifdef WIN32
    LeaveCriticalSection(&dbCfg->lifecycleCs);
#else
    pthread_mutex_unlock(&dbCfg->lifecycleCs);
#endif
}

static void iENT_DbLifecycleWait(DB_CFG* dbCfg)
{
#ifdef WIN32
    SleepConditionVariableCS(&dbCfg->lifecycleCv, &dbCfg->lifecycleCs, INFINITE);
#else
    pthread_cond_wait(&dbCfg->lifecycleCv, &dbCfg->lifecycleCs);
#endif
}

static void iENT_DbLifecycleWakeAll(DB_CFG* dbCfg)
{
#ifdef WIN32
    WakeAllConditionVariable(&dbCfg->lifecycleCv);
#else
    pthread_cond_broadcast(&dbCfg->lifecycleCv);
#endif
}

static void iENT_DbFreeConfigStrings(DB_CFG* dbCfg)
{
    if(dbCfg == NULL)
    {
        return;
    }
    if(dbCfg->host != NULL)
    {
        free(dbCfg->host);
        dbCfg->host = NULL;
    }
    if(dbCfg->database != NULL)
    {
        free(dbCfg->database);
        dbCfg->database = NULL;
    }
    if(dbCfg->userName != NULL)
    {
        free(dbCfg->userName);
        dbCfg->userName = NULL;
    }
    if(dbCfg->passwd != NULL)
    {
        free(dbCfg->passwd);
        dbCfg->passwd = NULL;
    }
}

static MSG_ID_T iENT_DbValidateHandle(DB_HANDLE dbHandle,
                                      DB_CFG** dbCfgOut,
                                      bool requireInit)
{
    DB_CFG* dbCfg = (DB_CFG*)dbHandle;

    if(dbCfgOut != NULL)
    {
        *dbCfgOut = NULL;
    }

    if(dbHandle == NULL)
    {
        IENT_LOG_ERROR("Database handle is null.\n");
        return ENT_DBS_BAD_ARGUMENT;
    }

    if(dbCfg->sTag != ENTDB_S_TAG || dbCfg->eTag != ENTDB_E_TAG)
    {
        IENT_LOG_ERROR("Database handle is invalid.\n");
        return ENT_DBS_BAD_HANDLE;
    }

    if(requireInit && dbCfg->isInit == false)
    {
        IENT_LOG_ERROR("Database handle is not initialized.\n");
        return ENT_DBS_NOT_INITIALIZED;
    }

    if(dbCfgOut != NULL)
    {
        *dbCfgOut = dbCfg;
    }

    return ENT_SYS_NORMAL;
}

static MSG_ID_T iENT_DbOpenLocked(DB_CFG* dbCfg)
{
    if(dbCfg == NULL)
    {
        return ENT_DBS_BAD_HANDLE;
    }

    if(dbCfg->isOpen == true)
    {
        return ENT_SYS_NORMAL;
    }

    switch(dbCfg->dbType)
    {
        case SQLITE_TYPE:
#if ENT_ENABLE_SQLITE
            return ENT_DbSqliteInit(dbCfg);
#else
            return iENT_DbBackendUnsupported(SQLITE_TYPE);
#endif

        case MYSQL_TYPE:
#if ENT_ENABLE_MYSQL
            return ENT_DbMySQLInit(dbCfg);
#else
            return iENT_DbBackendUnsupported(MYSQL_TYPE);
#endif

        case PGSQL_TYPE:
#if ENT_ENABLE_PGSQL
            return ENT_DbPgSQLInit(dbCfg);
#else
            return iENT_DbBackendUnsupported(PGSQL_TYPE);
#endif

        default:
            return ENT_DBS_BAD_HANDLE;
    }
}

static MSG_ID_T iENT_DbEnterHandleOp(DB_HANDLE dbHandle, DB_CFG** dbCfgOut)
{
    MSG_ID_T sts;
    DB_CFG* dbCfg = NULL;

    if(dbCfgOut != NULL)
    {
        *dbCfgOut = NULL;
    }

    if(sDbMutexInit == false)
    {
        IENT_LOG_ERROR("Uninitialized,please call ENT_DbInit.\n");
        return ENT_DBS_NOT_INITIALIZED;
    }

    iENT_DbGlobalLock();
    sts = iENT_DbValidateHandle(dbHandle, &dbCfg, true);
    if(sts < 0)
    {
        iENT_DbGlobalUnlock();
        return sts;
    }

    iENT_DbLifecycleLock(dbCfg);
    if(dbCfg->closing)
    {
        iENT_DbLifecycleUnlock(dbCfg);
        iENT_DbGlobalUnlock();
        IENT_LOG_WARN("Database handle is closing.\n");
        return ENT_DBS_IN_USE;
    }
    dbCfg->activeOps++;
    iENT_DbLifecycleUnlock(dbCfg);
    iENT_DbGlobalUnlock();

    if(dbCfgOut != NULL)
    {
        *dbCfgOut = dbCfg;
    }
    return ENT_SYS_NORMAL;
}

static void iENT_DbLeaveHandleOp(DB_CFG* dbCfg)
{
    if(dbCfg == NULL)
    {
        return;
    }

    iENT_DbLifecycleLock(dbCfg);
    if(dbCfg->activeOps > 0)
    {
        dbCfg->activeOps--;
    }
    if(dbCfg->closing && dbCfg->activeOps == 0)
    {
        iENT_DbLifecycleWakeAll(dbCfg);
    }
    iENT_DbLifecycleUnlock(dbCfg);
}

static MSG_ID_T iENT_DbReplaceString(char** target, const char* value)
{
    char* newValue = NULL;

    if(target == NULL)
    {
        return ENT_DBS_BAD_ARGUMENT;
    }

    if(value == NULL)
    {
        return ENT_SYS_NORMAL;
    }

    newValue = strdup(value);
    if(newValue == NULL)
    {
        IENT_LOG_ERROR("Database string duplication failed.\n");
        return ENT_DBS_ALLOC_FAILED;
    }

    if(*target != NULL)
    {
        free(*target);
    }
    *target = newValue;
    return ENT_SYS_NORMAL;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :iENT_DbReInit
 *
 * DESCRIPTION :
 *
 *-----------------------------------------------------------------------------
 */
MSG_ID_T  iENT_DbReInit(DB_HANDLE dbHandle,
                     DB_TYPE dbType,
                     const char* host,
                     const char* database,
                     const char* user,
                     const char* passwd,
                     int  port)
{
    MSG_ID_T sts = ENT_SYS_NORMAL;
    DB_CFG* dbCfg = NULL;

    char* newHost = NULL;
    char* newDatabase = NULL;
    char* newUser = NULL;
    char* newPasswd = NULL;

    if(sDbMutexInit == false)
    {
        IENT_LOG_ERROR("Uninitialized,please call ENT_DbInit.\n");
        return ENT_DBS_NOT_INITIALIZED;
    }

    iENT_DbGlobalLock();
    sts = iENT_DbValidateHandle(dbHandle, &dbCfg, true);
    if(sts < 0)
    {
        iENT_DbGlobalUnlock();
        return sts;
    }

    if(dbType != dbCfg->dbType)
    {
        iENT_DbGlobalUnlock();
        IENT_LOG_ERROR("Database handle reinit dbType failed.\n");
        return ENT_DBS_BAD_ARGUMENT;
    }

    iENT_DbLifecycleLock(dbCfg);
    if(dbCfg->closing || dbCfg->activeOps > 0)
    {
        iENT_DbLifecycleUnlock(dbCfg);
        iENT_DbGlobalUnlock();
        IENT_LOG_WARN("Database handle is in use during reinit.\n");
        return ENT_DBS_IN_USE;
    }

    if(host != NULL)
    {
        newHost = strdup(host);
        if(newHost == NULL)
        {
            sts = ENT_DBS_ALLOC_FAILED;
            goto END_OF_ROUTINE;
        }
    }
    if(database != NULL)
    {
        newDatabase = strdup(database);
        if(newDatabase == NULL)
        {
            sts = ENT_DBS_ALLOC_FAILED;
            goto END_OF_ROUTINE;
        }
    }
    if(user != NULL)
    {
        newUser = strdup(user);
        if(newUser == NULL)
        {
            sts = ENT_DBS_ALLOC_FAILED;
            goto END_OF_ROUTINE;
        }
    }
    if(passwd != NULL)
    {
        newPasswd = strdup(passwd);
        if(newPasswd == NULL)
        {
            sts = ENT_DBS_ALLOC_FAILED;
            goto END_OF_ROUTINE;
        }
    }

    if(dbCfg->isOpen)
    {
        switch(dbCfg->dbType)
        {
            case SQLITE_TYPE:
#if ENT_ENABLE_SQLITE
                sts = ENT_DbSqliteClose(dbCfg);
#else
                sts = iENT_DbBackendUnsupported(SQLITE_TYPE);
#endif
                break;
            case MYSQL_TYPE:
#if ENT_ENABLE_MYSQL
                sts = ENT_DbMySQLClose(dbCfg);
#else
                sts = iENT_DbBackendUnsupported(MYSQL_TYPE);
#endif
                break;
            case PGSQL_TYPE:
#if ENT_ENABLE_PGSQL
                sts = ENT_DbPgSQLClose(dbCfg);
#else
                sts = iENT_DbBackendUnsupported(PGSQL_TYPE);
#endif
                break;
            default:
                sts = ENT_DBS_BAD_HANDLE;
                break;
        }
        if(sts < 0 && sts != ENT_DBS_UNSUPPORTED)
        {
            goto END_OF_ROUTINE;
        }
    }

    if(host != NULL)
    {
        free(dbCfg->host);
        dbCfg->host = newHost;
        newHost = NULL;
    }
    if(database != NULL)
    {
        free(dbCfg->database);
        dbCfg->database = newDatabase;
        newDatabase = NULL;
    }
    if(user != NULL)
    {
        free(dbCfg->userName);
        dbCfg->userName = newUser;
        newUser = NULL;
    }
    if(passwd != NULL)
    {
        free(dbCfg->passwd);
        dbCfg->passwd = newPasswd;
        newPasswd = NULL;
    }
    dbCfg->portNo = port;
    sts = ENT_SYS_NORMAL;

END_OF_ROUTINE:
    if(newHost != NULL)
    {
        free(newHost);
    }
    if(newDatabase != NULL)
    {
        free(newDatabase);
    }
    if(newUser != NULL)
    {
        free(newUser);
    }
    if(newPasswd != NULL)
    {
        free(newPasswd);
    }
    iENT_DbLifecycleUnlock(dbCfg);
    iENT_DbGlobalUnlock();
    return sts;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_DbInit
 *
 * DESCRIPTION :
 *
 *-----------------------------------------------------------------------------
 */
ENT_PUBLIC MSG_ID_T  ENT_DbInit()
{
    if(sDbMutexInit)
    {
        return ENT_SYS_ALREADY_INITIALIZED;
    }
#ifdef WIN32
    InitializeCriticalSection(&sDbMutex);
#else
    pthread_mutex_init(&sDbMutex,NULL);
#endif
    IENT_LOG_PRINT("InitializeCriticalSection.\n");
    sDbMutexInit = true;
    return ENT_SYS_NORMAL;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_DbClose
 *
 * DESCRIPTION :
 *
 *-----------------------------------------------------------------------------
 */
ENT_PUBLIC MSG_ID_T  ENT_DbClose()
{
    if(sDbMutexInit == false)
    {
        return ENT_SYS_CLOSE_UNINITIALIZED;
    }

    iENT_DbGlobalLock();
    if(sDbNum > 0)
    {
        iENT_DbGlobalUnlock();
        IENT_LOG_WARN("Database service still has [%ld] live handle(s).\n", sDbNum);
        return ENT_DBS_IN_USE;
    }
#ifdef WIN32
    DeleteCriticalSection(&sDbMutex);
#else
    pthread_mutex_destroy(&sDbMutex);
#endif
    IENT_LOG_PRINT("DeleteCriticalSection.\n");
    sDbMutexInit = false;
    return ENT_SYS_NORMAL;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_DbInitHandle
 *
 * DESCRIPTION :
 *
 *-----------------------------------------------------------------------------
 */
ENT_PUBLIC MSG_ID_T  ENT_DbInitHandle(DB_HANDLE* pdbHandle,
                     DB_TYPE dbType,
                     const char* host,
                     const char* database,
                     const char* user,
                     const char* passwd,
                     int  port)
{
    MSG_ID_T sts=ENT_SYS_NORMAL;
    DB_CFG*  dbCfg=NULL;
    if(sDbMutexInit==false)
    {
        IENT_LOG_ERROR("Uninitialized,please call ENT_DbInit.\n");
        return ENT_DBS_NOT_INITIALIZED;
    }

    if(pdbHandle==NULL)
    {
        IENT_LOG_ERROR("Database handle is null\n");
        return ENT_DBS_BAD_ARGUMENT;
    }

    dbCfg = (DB_CFG*)*pdbHandle;

    if(dbCfg!=NULL && dbCfg->sTag==ENTDB_S_TAG && dbCfg->eTag == ENTDB_E_TAG)
    {
        sts=ENT_DbCloseHandle(dbCfg);
        if(sts < 0)
        {
            *pdbHandle = NULL;
            return sts;
        }
    }

    iENT_DbGlobalLock();

    switch(dbType)
    {
        case SQLITE_TYPE:
        case MYSQL_TYPE:
        case PGSQL_TYPE:
            dbCfg = (DB_CFG*)malloc(sizeof(DB_CFG));
            if(dbCfg==NULL)
            {
                IENT_LOG_ERROR("Database malloc db config failed\n");
                sts = ENT_DBS_ALLOC_FAILED;
                goto END_OF_ROUTINE;
            }
            memset(dbCfg,0,sizeof(DB_CFG));
            dbCfg->dbType = dbType;
            if(host)
            {
                dbCfg->host = strdup(host);
                if(dbCfg->host == NULL)
                {
                    sts = ENT_DBS_ALLOC_FAILED;
                    goto END_OF_ROUTINE;
                }
            }
            if(database)
            {
                dbCfg->database = strdup(database);
                if(dbCfg->database == NULL)
                {
                    sts = ENT_DBS_ALLOC_FAILED;
                    goto END_OF_ROUTINE;
                }
            }
            if(user)
            {
                dbCfg->userName = strdup(user);
                if(dbCfg->userName == NULL)
                {
                    sts = ENT_DBS_ALLOC_FAILED;
                    goto END_OF_ROUTINE;
                }
            }
            if(passwd)
            {
                dbCfg->passwd = strdup(passwd);
                if(dbCfg->passwd == NULL)
                {
                    sts = ENT_DBS_ALLOC_FAILED;
                    goto END_OF_ROUTINE;
                }
            }
            dbCfg->portNo = port;
            break;

        default:
            *pdbHandle = NULL;
            IENT_LOG_WARN("Database Type is not supported,[%d]\n",dbType);
            sts = ENT_DBS_UNSUPPORTED;
            goto END_OF_ROUTINE;
    }
#ifdef WIN32
    InitializeCriticalSection(&dbCfg->cs);
#else
    if(pthread_mutex_init(&dbCfg->cs,NULL) != 0)
    {
        sts = ENT_UTHD_INIT_FAILED;
        goto END_OF_ROUTINE;
    }
#endif
    sts = iENT_DbLifecycleInit(dbCfg);
    if(sts < 0)
    {
#ifndef WIN32
        pthread_mutex_destroy(&dbCfg->cs);
#else
        DeleteCriticalSection(&dbCfg->cs);
#endif
        goto END_OF_ROUTINE;
    }
    dbCfg->isInit = true;
    dbCfg->isOpen = false;
    dbCfg->closing = false;
    dbCfg->activeOps = 0;
    dbCfg->sTag = ENTDB_S_TAG;
    dbCfg->eTag = ENTDB_E_TAG;
    *pdbHandle = dbCfg;
    sDbNum++;

END_OF_ROUTINE:
    if(sts < 0 && dbCfg != NULL)
    {
        iENT_DbFreeConfigStrings(dbCfg);
        free(dbCfg);
        if(pdbHandle != NULL)
        {
            *pdbHandle = NULL;
        }
    }
    iENT_DbGlobalUnlock();
    return sts;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_DbOpen
 *
 * DESCRIPTION :
 *
 *-----------------------------------------------------------------------------
 */
ENT_PUBLIC MSG_ID_T ENT_DbOpen(DB_HANDLE dbHandle)
{
    MSG_ID_T sts=ENT_SYS_NORMAL;
    DB_CFG*  dbCfg=NULL;

    sts = iENT_DbEnterHandleOp(dbHandle, &dbCfg);
    if(sts < 0)
    {
        return sts;
    }

    iENT_DbHandleLock(dbCfg);
    sts = iENT_DbOpenLocked(dbCfg);
    iENT_DbHandleUnlock(dbCfg);

    iENT_DbLeaveHandleOp(dbCfg);
    return sts;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_DbCloseHandle
 *
 * DESCRIPTION :
 *
 *-----------------------------------------------------------------------------
 */
ENT_PUBLIC MSG_ID_T ENT_DbCloseHandle(DB_HANDLE dbHandle)
{
    MSG_ID_T sts=ENT_SYS_NORMAL;
    DB_CFG*  dbCfg=NULL;
    if(sDbMutexInit==false)
    {
        IENT_LOG_ERROR("Uninitialized,please call ENT_DbInit.\n");
        return ENT_DBS_NOT_INITIALIZED;
    }

    iENT_DbGlobalLock();
    sts = iENT_DbValidateHandle(dbHandle, &dbCfg, false);
    if(sts < 0)
    {
        iENT_DbGlobalUnlock();
        return sts;
    }

    iENT_DbLifecycleLock(dbCfg);
    if(dbCfg->closing)
    {
        iENT_DbLifecycleUnlock(dbCfg);
        iENT_DbGlobalUnlock();
        return ENT_DBS_IN_USE;
    }
    dbCfg->closing = true;
    while(dbCfg->activeOps > 0)
    {
        iENT_DbLifecycleWait(dbCfg);
    }
    dbCfg->isInit = false;
    iENT_DbLifecycleUnlock(dbCfg);

    switch(dbCfg->dbType)
    {
        case SQLITE_TYPE:
#if ENT_ENABLE_SQLITE
            sts = ENT_DbSqliteClose(dbCfg);
#else
            sts = iENT_DbBackendUnsupported(SQLITE_TYPE);
#endif
            break;

        case MYSQL_TYPE:
#if ENT_ENABLE_MYSQL
            sts = ENT_DbMySQLClose(dbCfg);
#else
            sts = iENT_DbBackendUnsupported(MYSQL_TYPE);
#endif
            break;
        case PGSQL_TYPE:
#if ENT_ENABLE_PGSQL
            sts = ENT_DbPgSQLClose(dbCfg);
#else
            sts = iENT_DbBackendUnsupported(PGSQL_TYPE);
#endif
            break;
        default:
            sts = ENT_DBS_BAD_HANDLE;
            break;
    }

    iENT_DbFreeConfigStrings(dbCfg);
#ifdef WIN32
    DeleteCriticalSection(&dbCfg->cs);
#else
    pthread_mutex_destroy(&dbCfg->cs);
#endif
    if(sDbNum > 0)
    {
        sDbNum--;
    }
    dbCfg->sTag = 0;
    dbCfg->eTag = 0;

    iENT_DbGlobalUnlock();

    iENT_DbLifecycleDestroy(dbCfg);
    free(dbCfg);
    return sts;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_DbRead
 *
 * DESCRIPTION :
 *
 *-----------------------------------------------------------------------------
 */
ENT_PUBLIC MSG_ID_T ENT_DbRead(DB_HANDLE dbHandle,const char* sql,SqlResultCB sqlCb,void* userData)
{
    MSG_ID_T sts=ENT_SYS_NORMAL;
    DB_CFG*  dbCfg=NULL;

    if(sql==NULL)
    {
        IENT_LOG_ERROR("Arguments are invalid.\n");
        return ENT_DBS_BAD_ARGUMENT;
    }

    sts = iENT_DbEnterHandleOp(dbHandle, &dbCfg);
    if(sts < 0)
    {
        return sts;
    }

    iENT_DbHandleLock(dbCfg);
    if(dbCfg->isOpen==false)
    {
        sts = iENT_DbOpenLocked(dbCfg);
        if(sts<0)
        {
            iENT_DbHandleUnlock(dbCfg);
            iENT_DbLeaveHandleOp(dbCfg);
            IENT_LOG_ERROR("ENT_DbOpen failed.\n");
            return sts;
        }
    }

    switch(dbCfg->dbType)
    {
        case MYSQL_TYPE:
#if ENT_ENABLE_MYSQL
            sts = ENT_DbMySQLRead(dbCfg->dbInstance.mysql,sql,sqlCb,userData);
#else
            sts = iENT_DbBackendUnsupported(MYSQL_TYPE);
#endif
            break;

        case SQLITE_TYPE:
#if ENT_ENABLE_SQLITE
            sts = ENT_DbSqliteRead(dbCfg->dbInstance.sqlite,sql,sqlCb,userData);
#else
            sts = iENT_DbBackendUnsupported(SQLITE_TYPE);
#endif
            break;
        case PGSQL_TYPE:
#if ENT_ENABLE_PGSQL
            sts = ENT_DbPgSQLRead(dbCfg->dbInstance.pgsql,sql,sqlCb,userData);
#else
            sts = iENT_DbBackendUnsupported(PGSQL_TYPE);
#endif
            break;
        default:
            sts = ENT_DBS_BAD_HANDLE;
            break;
    }
    iENT_DbHandleUnlock(dbCfg);
    iENT_DbLeaveHandleOp(dbCfg);

    return sts;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_DbWrite
 *
 * DESCRIPTION :
 *
 *-----------------------------------------------------------------------------
 */
ENT_PUBLIC MSG_ID_T ENT_DbWrite(DB_HANDLE dbHandle,const char* sql,SqlResultCB sqlCb,void* userData)
{
    MSG_ID_T sts=ENT_SYS_NORMAL;
    DB_CFG*  dbCfg=NULL;

    if(sql==NULL)
    {
        IENT_LOG_ERROR("Arguments are invalid.\n");
        return ENT_DBS_BAD_ARGUMENT;
    }

    sts = iENT_DbEnterHandleOp(dbHandle, &dbCfg);
    if(sts < 0)
    {
        return sts;
    }

    iENT_DbHandleLock(dbCfg);
    if(dbCfg->isOpen==false)
    {
        sts = iENT_DbOpenLocked(dbCfg);
        if(sts<0)
        {
            iENT_DbHandleUnlock(dbCfg);
            iENT_DbLeaveHandleOp(dbCfg);
            IENT_LOG_ERROR("ENT_DbOpen failed.\n");
            return sts;
        }
    }

    switch(dbCfg->dbType)
    {
        case MYSQL_TYPE:
#if ENT_ENABLE_MYSQL
            sts = ENT_DbMySQLWrite(dbCfg->dbInstance.mysql,sql,sqlCb,userData);
#else
            sts = iENT_DbBackendUnsupported(MYSQL_TYPE);
#endif
            break;

        case SQLITE_TYPE:
#if ENT_ENABLE_SQLITE
            sts = ENT_DbSqliteWrite(dbCfg->dbInstance.sqlite,sql,sqlCb,userData);
#else
            sts = iENT_DbBackendUnsupported(SQLITE_TYPE);
#endif
            break;
        case PGSQL_TYPE:
#if ENT_ENABLE_PGSQL
            sts = ENT_DbPgSQLWrite(dbCfg->dbInstance.pgsql,sql,sqlCb,userData);
#else
            sts = iENT_DbBackendUnsupported(PGSQL_TYPE);
#endif
            break;
        default:
            sts = ENT_DBS_BAD_HANDLE;
            break;
    }
    iENT_DbHandleUnlock(dbCfg);
    iENT_DbLeaveHandleOp(dbCfg);

    return sts;
}

ENT_PUBLIC MSG_ID_T ENT_DbReadParams(DB_HANDLE dbHandle,
                                     const char* sql,
                                     const ENT_DB_PARAM* params,
                                     size_t paramCount,
                                     SqlResultCB sqlCb,
                                     void* userData)
{
    MSG_ID_T sts=ENT_SYS_NORMAL;
    DB_CFG*  dbCfg=NULL;

    if(sql==NULL)
    {
        IENT_LOG_ERROR("Arguments are invalid.\n");
        return ENT_DBS_BAD_ARGUMENT;
    }

    sts = iENT_DbEnterHandleOp(dbHandle, &dbCfg);
    if(sts < 0)
    {
        return sts;
    }

    iENT_DbHandleLock(dbCfg);
    if(dbCfg->isOpen==false)
    {
        sts = iENT_DbOpenLocked(dbCfg);
        if(sts<0)
        {
            iENT_DbHandleUnlock(dbCfg);
            iENT_DbLeaveHandleOp(dbCfg);
            IENT_LOG_ERROR("ENT_DbOpen failed.\n");
            return sts;
        }
    }

    switch(dbCfg->dbType)
    {
        case SQLITE_TYPE:
#if ENT_ENABLE_SQLITE
            sts = ENT_DbSqliteReadParams(dbCfg->dbInstance.sqlite, sql, params, paramCount, sqlCb, userData);
#else
            sts = iENT_DbBackendUnsupported(SQLITE_TYPE);
#endif
            break;
        case MYSQL_TYPE:
#if ENT_ENABLE_MYSQL
            sts = ENT_DbMySQLReadParams(dbCfg->dbInstance.mysql, sql, params, paramCount, sqlCb, userData);
#else
            sts = iENT_DbBackendUnsupported(MYSQL_TYPE);
#endif
            break;
        case PGSQL_TYPE:
#if ENT_ENABLE_PGSQL
            sts = ENT_DbPgSQLReadParams(dbCfg->dbInstance.pgsql, sql, params, paramCount, sqlCb, userData);
#else
            sts = iENT_DbBackendUnsupported(PGSQL_TYPE);
#endif
            break;
        default:
            sts = ENT_DBS_BAD_HANDLE;
            break;
    }

    iENT_DbHandleUnlock(dbCfg);
    iENT_DbLeaveHandleOp(dbCfg);
    return sts;
}

ENT_PUBLIC MSG_ID_T ENT_DbWriteParams(DB_HANDLE dbHandle,
                                      const char* sql,
                                      const ENT_DB_PARAM* params,
                                      size_t paramCount,
                                      SqlResultCB sqlCb,
                                      void* userData)
{
    MSG_ID_T sts=ENT_SYS_NORMAL;
    DB_CFG*  dbCfg=NULL;

    if(sql==NULL)
    {
        IENT_LOG_ERROR("Arguments are invalid.\n");
        return ENT_DBS_BAD_ARGUMENT;
    }

    sts = iENT_DbEnterHandleOp(dbHandle, &dbCfg);
    if(sts < 0)
    {
        return sts;
    }

    iENT_DbHandleLock(dbCfg);
    if(dbCfg->isOpen==false)
    {
        sts = iENT_DbOpenLocked(dbCfg);
        if(sts<0)
        {
            iENT_DbHandleUnlock(dbCfg);
            iENT_DbLeaveHandleOp(dbCfg);
            IENT_LOG_ERROR("ENT_DbOpen failed.\n");
            return sts;
        }
    }

    switch(dbCfg->dbType)
    {
        case SQLITE_TYPE:
#if ENT_ENABLE_SQLITE
            sts = ENT_DbSqliteWriteParams(dbCfg->dbInstance.sqlite, sql, params, paramCount, sqlCb, userData);
#else
            sts = iENT_DbBackendUnsupported(SQLITE_TYPE);
#endif
            break;
        case MYSQL_TYPE:
#if ENT_ENABLE_MYSQL
            sts = ENT_DbMySQLWriteParams(dbCfg->dbInstance.mysql, sql, params, paramCount, sqlCb, userData);
#else
            sts = iENT_DbBackendUnsupported(MYSQL_TYPE);
#endif
            break;
        case PGSQL_TYPE:
#if ENT_ENABLE_PGSQL
            sts = ENT_DbPgSQLWriteParams(dbCfg->dbInstance.pgsql, sql, params, paramCount, sqlCb, userData);
#else
            sts = iENT_DbBackendUnsupported(PGSQL_TYPE);
#endif
            break;
        default:
            sts = ENT_DBS_BAD_HANDLE;
            break;
    }

    iENT_DbHandleUnlock(dbCfg);
    iENT_DbLeaveHandleOp(dbCfg);
    return sts;
}
