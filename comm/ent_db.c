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

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <errno.h>
#endif

#include "ient_db.h"
#include "ient_comm.h"
#include "ent_msg.h"

#ifdef _WIN32
static SRWLOCK sDbMutex = SRWLOCK_INIT;
#pragma warning(disable : 4996)
#else
static pthread_mutex_t  sDbMutex = PTHREAD_MUTEX_INITIALIZER;
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
        printf("%s ",fields[colIdx] ? fields[colIdx] : "(null)");
    }
    printf("\n");

    for(rowIdx = 0; rowIdx < rowNum; rowIdx++)
    {
        for(colIdx = 0; colIdx<columnNum; colIdx++)
        {
            printf("%s ",rowRes[rowIdx*columnNum+colIdx] ?
                   rowRes[rowIdx*columnNum+colIdx] : "(null)");
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

static const char* iENT_DbHandleStateName(DB_HANDLE_STATE_E state)
{
    switch(state)
    {
        case ENT_DB_HANDLE_CREATED_E:
            return "created";
        case ENT_DB_HANDLE_ACTIVE_E:
            return "active";
        case ENT_DB_HANDLE_CLOSING_E:
            return "closing";
        case ENT_DB_HANDLE_CLOSED_E:
            return "closed";
        default:
            return "unknown";
    }
}

static void iENT_DbGlobalLock(void)
{
#ifdef _WIN32
    AcquireSRWLockExclusive(&sDbMutex);
#else
    pthread_mutex_lock(&sDbMutex);
#endif
}

static void iENT_DbGlobalUnlock(void)
{
#ifdef _WIN32
    ReleaseSRWLockExclusive(&sDbMutex);
#else
    pthread_mutex_unlock(&sDbMutex);
#endif
}

static void iENT_DbHandleLock(DB_CFG* dbCfg)
{
#ifdef _WIN32
    EnterCriticalSection(&dbCfg->cs);
#else
    pthread_mutex_lock(&dbCfg->cs);
#endif
}

static void iENT_DbHandleUnlock(DB_CFG* dbCfg)
{
#ifdef _WIN32
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
#ifdef _WIN32
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
#ifdef _WIN32
    DeleteCriticalSection(&dbCfg->lifecycleCs);
#else
    pthread_cond_destroy(&dbCfg->lifecycleCv);
    pthread_mutex_destroy(&dbCfg->lifecycleCs);
#endif
}

static void iENT_DbLifecycleLock(DB_CFG* dbCfg)
{
#ifdef _WIN32
    EnterCriticalSection(&dbCfg->lifecycleCs);
#else
    pthread_mutex_lock(&dbCfg->lifecycleCs);
#endif
}

static void iENT_DbLifecycleUnlock(DB_CFG* dbCfg)
{
#ifdef _WIN32
    LeaveCriticalSection(&dbCfg->lifecycleCs);
#else
    pthread_mutex_unlock(&dbCfg->lifecycleCs);
#endif
}

static void iENT_DbLifecycleWait(DB_CFG* dbCfg)
{
#ifdef _WIN32
    SleepConditionVariableCS(&dbCfg->lifecycleCv, &dbCfg->lifecycleCs, INFINITE);
#else
    pthread_cond_wait(&dbCfg->lifecycleCv, &dbCfg->lifecycleCs);
#endif
}

static void iENT_DbLifecycleWakeAll(DB_CFG* dbCfg)
{
#ifdef _WIN32
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
#if ENT_ENABLE_SQLITE && ENT_SQLITE_FOUND
            return ENT_DbSqliteInit(dbCfg);
#else
            return iENT_DbBackendUnsupported(SQLITE_TYPE);
#endif

        case MYSQL_TYPE:
#if ENT_ENABLE_MYSQL && ENT_MYSQL_FOUND
            return ENT_DbMySQLInit(dbCfg);
#else
            return iENT_DbBackendUnsupported(MYSQL_TYPE);
#endif

        case PGSQL_TYPE:
#if ENT_ENABLE_PGSQL && ENT_PGSQL_FOUND
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

    iENT_DbGlobalLock();
    if(sDbMutexInit == false)
    {
        iENT_DbGlobalUnlock();
        IENT_LOG_ERROR("Uninitialized,please call ENT_DbInit.\n");
        return ENT_DBS_NOT_INITIALIZED;
    }
    sts = iENT_DbValidateHandle(dbHandle, &dbCfg, true);
    if(sts < 0)
    {
        iENT_DbGlobalUnlock();
        return sts;
    }

    if(dbCfg->handleState != ENT_DB_HANDLE_ACTIVE_E)
    {
        DB_HANDLE_STATE_E state = dbCfg->handleState;
        iENT_DbGlobalUnlock();
        return (state == ENT_DB_HANDLE_CLOSING_E) ? ENT_DBS_IN_USE : ENT_DBS_BAD_HANDLE;
    }

    iENT_DbLifecycleLock(dbCfg);
    if(dbCfg->handleState != ENT_DB_HANDLE_ACTIVE_E)
    {
        DB_HANDLE_STATE_E state = dbCfg->handleState;
        iENT_DbLifecycleUnlock(dbCfg);
        iENT_DbGlobalUnlock();
        IENT_LOG_WARN("Database handle is not available for operation, state[%s].\n",
                      iENT_DbHandleStateName(state));
        return (state == ENT_DB_HANDLE_CLOSING_E) ? ENT_DBS_IN_USE : ENT_DBS_BAD_HANDLE;
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
    if(dbCfg->handleState == ENT_DB_HANDLE_CLOSING_E && dbCfg->activeOps == 0)
    {
        iENT_DbLifecycleWakeAll(dbCfg);
    }
    iENT_DbLifecycleUnlock(dbCfg);
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

    iENT_DbGlobalLock();
    if(sDbMutexInit == false)
    {
        iENT_DbGlobalUnlock();
        IENT_LOG_ERROR("Uninitialized,please call ENT_DbInit.\n");
        return ENT_DBS_NOT_INITIALIZED;
    }
    sts = iENT_DbValidateHandle(dbHandle, &dbCfg, true);
    if(sts < 0)
    {
        iENT_DbGlobalUnlock();
        return sts;
    }

    if(dbCfg->handleState != ENT_DB_HANDLE_ACTIVE_E)
    {
        DB_HANDLE_STATE_E state = dbCfg->handleState;
        iENT_DbGlobalUnlock();
        return (state == ENT_DB_HANDLE_CLOSING_E) ? ENT_DBS_IN_USE : ENT_DBS_BAD_HANDLE;
    }

    if(dbType != dbCfg->dbType)
    {
        iENT_DbGlobalUnlock();
        IENT_LOG_ERROR("Database handle reinit dbType failed.\n");
        return ENT_DBS_BAD_ARGUMENT;
    }

    iENT_DbLifecycleLock(dbCfg);
    if(dbCfg->handleState != ENT_DB_HANDLE_ACTIVE_E || dbCfg->activeOps > 0)
    {
        DB_HANDLE_STATE_E state = dbCfg->handleState;
        iENT_DbLifecycleUnlock(dbCfg);
        iENT_DbGlobalUnlock();
        IENT_LOG_WARN("Database handle cannot reinit in state[%s] with activeOps[%ld].\n",
                      iENT_DbHandleStateName(state),
                      dbCfg->activeOps);
        return (state == ENT_DB_HANDLE_CLOSING_E || dbCfg->activeOps > 0) ? ENT_DBS_IN_USE : ENT_DBS_BAD_HANDLE;
    }

    if(host != NULL)
    {
        newHost = ENT_StrDup(host);
        if(newHost == NULL)
        {
            sts = ENT_DBS_ALLOC_FAILED;
            goto END_OF_ROUTINE;
        }
    }
    if(database != NULL)
    {
        newDatabase = ENT_StrDup(database);
        if(newDatabase == NULL)
        {
            sts = ENT_DBS_ALLOC_FAILED;
            goto END_OF_ROUTINE;
        }
    }
    if(user != NULL)
    {
        newUser = ENT_StrDup(user);
        if(newUser == NULL)
        {
            sts = ENT_DBS_ALLOC_FAILED;
            goto END_OF_ROUTINE;
        }
    }
    if(passwd != NULL)
    {
        newPasswd = ENT_StrDup(passwd);
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
#if ENT_ENABLE_SQLITE && ENT_SQLITE_FOUND
                sts = ENT_DbSqliteClose(dbCfg);
#else
                sts = iENT_DbBackendUnsupported(SQLITE_TYPE);
#endif
                break;
            case MYSQL_TYPE:
#if ENT_ENABLE_MYSQL && ENT_MYSQL_FOUND
                sts = ENT_DbMySQLClose(dbCfg);
#else
                sts = iENT_DbBackendUnsupported(MYSQL_TYPE);
#endif
                break;
            case PGSQL_TYPE:
#if ENT_ENABLE_PGSQL && ENT_PGSQL_FOUND
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
    iENT_DbGlobalLock();
    if(sDbMutexInit)
    {
        iENT_DbGlobalUnlock();
        return ENT_SYS_ALREADY_INITIALIZED;
    }
    IENT_LOG_PRINT("InitializeCriticalSection.\n");
    sDbMutexInit = true;
    iENT_DbGlobalUnlock();
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
    iENT_DbGlobalLock();
    if(sDbMutexInit == false)
    {
        iENT_DbGlobalUnlock();
        return ENT_SYS_CLOSE_UNINITIALIZED;
    }
    if(sDbNum > 0)
    {
        iENT_DbGlobalUnlock();
        IENT_LOG_WARN("Database service still has [%ld] live handle(s).\n", sDbNum);
        return ENT_DBS_IN_USE;
    }
    sDbMutexInit = false;
    iENT_DbGlobalUnlock();
    IENT_LOG_PRINT("DeleteCriticalSection.\n");
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
    if(pdbHandle==NULL)
    {
        IENT_LOG_ERROR("Database handle is null\n");
        return ENT_DBS_BAD_ARGUMENT;
    }

    dbCfg = (DB_CFG*)*pdbHandle;

    if(dbCfg!=NULL && dbCfg->sTag==ENTDB_S_TAG && dbCfg->eTag == ENTDB_E_TAG)
    {
        sts=ENT_DbCloseHandle(pdbHandle);
        if(sts < 0)
        {
            return sts;
        }
    }

    iENT_DbGlobalLock();
    if(sDbMutexInit == false)
    {
        iENT_DbGlobalUnlock();
        IENT_LOG_ERROR("Uninitialized,please call ENT_DbInit.\n");
        return ENT_DBS_NOT_INITIALIZED;
    }

    switch(dbType)
    {
        case SQLITE_TYPE:
#if !(ENT_ENABLE_SQLITE && ENT_SQLITE_FOUND)
            *pdbHandle = NULL;
            sts = iENT_DbBackendUnsupported(SQLITE_TYPE);
            goto END_OF_ROUTINE;
#endif
            break;

        case MYSQL_TYPE:
#if !(ENT_ENABLE_MYSQL && ENT_MYSQL_FOUND)
            *pdbHandle = NULL;
            sts = iENT_DbBackendUnsupported(MYSQL_TYPE);
            goto END_OF_ROUTINE;
#endif
            break;

        case PGSQL_TYPE:
#if !(ENT_ENABLE_PGSQL && ENT_PGSQL_FOUND)
            *pdbHandle = NULL;
            sts = iENT_DbBackendUnsupported(PGSQL_TYPE);
            goto END_OF_ROUTINE;
#endif
            break;

        default:
            *pdbHandle = NULL;
            IENT_LOG_WARN("Database Type is not supported,[%d]\n",dbType);
            sts = ENT_DBS_UNSUPPORTED;
            goto END_OF_ROUTINE;
    }

    dbCfg = (DB_CFG*)malloc(sizeof(DB_CFG));
    if(dbCfg==NULL)
    {
        IENT_LOG_ERROR("Database malloc db config failed\n");
        sts = ENT_DBS_ALLOC_FAILED;
        goto END_OF_ROUTINE;
    }
    memset(dbCfg,0,sizeof(DB_CFG));
    dbCfg->dbType = dbType;
    dbCfg->handleState = ENT_DB_HANDLE_CREATED_E;
    if(host)
    {
        dbCfg->host = ENT_StrDup(host);
        if(dbCfg->host == NULL)
        {
            sts = ENT_DBS_ALLOC_FAILED;
            goto END_OF_ROUTINE;
        }
    }
    if(database)
    {
        dbCfg->database = ENT_StrDup(database);
        if(dbCfg->database == NULL)
        {
            sts = ENT_DBS_ALLOC_FAILED;
            goto END_OF_ROUTINE;
        }
    }
    if(user)
    {
        dbCfg->userName = ENT_StrDup(user);
        if(dbCfg->userName == NULL)
        {
            sts = ENT_DBS_ALLOC_FAILED;
            goto END_OF_ROUTINE;
        }
    }
    if(passwd)
    {
        dbCfg->passwd = ENT_StrDup(passwd);
        if(dbCfg->passwd == NULL)
        {
            sts = ENT_DBS_ALLOC_FAILED;
            goto END_OF_ROUTINE;
        }
    }
    dbCfg->portNo = port;
#ifdef _WIN32
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
#ifndef _WIN32
        pthread_mutex_destroy(&dbCfg->cs);
#else
        DeleteCriticalSection(&dbCfg->cs);
#endif
        goto END_OF_ROUTINE;
    }
    dbCfg->isInit = true;
    dbCfg->isOpen = false;
    dbCfg->handleState = ENT_DB_HANDLE_ACTIVE_E;
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
ENT_PUBLIC MSG_ID_T ENT_DbCloseHandle(DB_HANDLE* dbHandle)
{
    MSG_ID_T sts=ENT_SYS_NORMAL;
    DB_CFG*  dbCfg=NULL;
    if(dbHandle == NULL)
    {
        IENT_LOG_ERROR("Database handle is null.\n");
        return ENT_DBS_BAD_ARGUMENT;
    }

    iENT_DbGlobalLock();
    if(sDbMutexInit == false)
    {
        iENT_DbGlobalUnlock();
        IENT_LOG_ERROR("Uninitialized,please call ENT_DbInit.\n");
        return ENT_DBS_NOT_INITIALIZED;
    }
    sts = iENT_DbValidateHandle(*dbHandle, &dbCfg, false);
    if(sts < 0)
    {
        iENT_DbGlobalUnlock();
        return sts;
    }

    if(dbCfg->handleState == ENT_DB_HANDLE_CLOSING_E)
    {
        iENT_DbGlobalUnlock();
        return ENT_DBS_IN_USE;
    }
    if(dbCfg->handleState != ENT_DB_HANDLE_ACTIVE_E)
    {
        iENT_DbGlobalUnlock();
        return ENT_DBS_BAD_HANDLE;
    }

    iENT_DbLifecycleLock(dbCfg);
    if(dbCfg->handleState == ENT_DB_HANDLE_CLOSING_E)
    {
        iENT_DbLifecycleUnlock(dbCfg);
        iENT_DbGlobalUnlock();
        return ENT_DBS_IN_USE;
    }
    if(dbCfg->handleState != ENT_DB_HANDLE_ACTIVE_E)
    {
        DB_HANDLE_STATE_E state = dbCfg->handleState;
        iENT_DbLifecycleUnlock(dbCfg);
        iENT_DbGlobalUnlock();
        IENT_LOG_WARN("Database handle cannot close in state[%s].\n", iENT_DbHandleStateName(state));
        return ENT_DBS_BAD_HANDLE;
    }
    dbCfg->handleState = ENT_DB_HANDLE_CLOSING_E;
    iENT_DbGlobalUnlock();
    while(dbCfg->activeOps > 0)
    {
        iENT_DbLifecycleWait(dbCfg);
    }
    dbCfg->isInit = false;
    iENT_DbLifecycleUnlock(dbCfg);

    switch(dbCfg->dbType)
    {
        case SQLITE_TYPE:
#if ENT_ENABLE_SQLITE && ENT_SQLITE_FOUND
            sts = ENT_DbSqliteClose(dbCfg);
#else
            sts = iENT_DbBackendUnsupported(SQLITE_TYPE);
#endif
            break;

        case MYSQL_TYPE:
#if ENT_ENABLE_MYSQL && ENT_MYSQL_FOUND
            sts = ENT_DbMySQLClose(dbCfg);
#else
            sts = iENT_DbBackendUnsupported(MYSQL_TYPE);
#endif
            break;
        case PGSQL_TYPE:
#if ENT_ENABLE_PGSQL && ENT_PGSQL_FOUND
            sts = ENT_DbPgSQLClose(dbCfg);
#else
            sts = iENT_DbBackendUnsupported(PGSQL_TYPE);
#endif
            break;
        default:
            sts = ENT_DBS_BAD_HANDLE;
            break;
    }

    if(sts < 0)
    {
        iENT_DbLifecycleLock(dbCfg);
        dbCfg->isInit = true;
        dbCfg->handleState = ENT_DB_HANDLE_ACTIVE_E;
        iENT_DbLifecycleUnlock(dbCfg);
        return sts;
    }

    iENT_DbGlobalLock();
    iENT_DbLifecycleLock(dbCfg);
    dbCfg->handleState = ENT_DB_HANDLE_CLOSED_E;
    iENT_DbLifecycleUnlock(dbCfg);
    iENT_DbFreeConfigStrings(dbCfg);
#ifdef _WIN32
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
    *dbHandle = NULL;
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
#if ENT_ENABLE_MYSQL && ENT_MYSQL_FOUND
            sts = ENT_DbMySQLRead(dbCfg->dbInstance.mysql,sql,sqlCb,userData);
#else
            sts = iENT_DbBackendUnsupported(MYSQL_TYPE);
#endif
            break;

        case SQLITE_TYPE:
#if ENT_ENABLE_SQLITE && ENT_SQLITE_FOUND
            sts = ENT_DbSqliteRead(dbCfg->dbInstance.sqlite,sql,sqlCb,userData);
#else
            sts = iENT_DbBackendUnsupported(SQLITE_TYPE);
#endif
            break;
        case PGSQL_TYPE:
#if ENT_ENABLE_PGSQL && ENT_PGSQL_FOUND
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
#if ENT_ENABLE_MYSQL && ENT_MYSQL_FOUND
            sts = ENT_DbMySQLWrite(dbCfg->dbInstance.mysql,sql,sqlCb,userData);
#else
            sts = iENT_DbBackendUnsupported(MYSQL_TYPE);
#endif
            break;

        case SQLITE_TYPE:
#if ENT_ENABLE_SQLITE && ENT_SQLITE_FOUND
            sts = ENT_DbSqliteWrite(dbCfg->dbInstance.sqlite,sql,sqlCb,userData);
#else
            sts = iENT_DbBackendUnsupported(SQLITE_TYPE);
#endif
            break;
        case PGSQL_TYPE:
#if ENT_ENABLE_PGSQL && ENT_PGSQL_FOUND
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
#if ENT_ENABLE_SQLITE && ENT_SQLITE_FOUND
            sts = ENT_DbSqliteReadParams(dbCfg->dbInstance.sqlite, sql, params, paramCount, sqlCb, userData);
#else
            sts = iENT_DbBackendUnsupported(SQLITE_TYPE);
#endif
            break;
        case MYSQL_TYPE:
#if ENT_ENABLE_MYSQL && ENT_MYSQL_FOUND
            sts = ENT_DbMySQLReadParams(dbCfg->dbInstance.mysql, sql, params, paramCount, sqlCb, userData);
#else
            sts = iENT_DbBackendUnsupported(MYSQL_TYPE);
#endif
            break;
        case PGSQL_TYPE:
#if ENT_ENABLE_PGSQL && ENT_PGSQL_FOUND
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
#if ENT_ENABLE_SQLITE && ENT_SQLITE_FOUND
            sts = ENT_DbSqliteWriteParams(dbCfg->dbInstance.sqlite, sql, params, paramCount, sqlCb, userData);
#else
            sts = iENT_DbBackendUnsupported(SQLITE_TYPE);
#endif
            break;
        case MYSQL_TYPE:
#if ENT_ENABLE_MYSQL && ENT_MYSQL_FOUND
            sts = ENT_DbMySQLWriteParams(dbCfg->dbInstance.mysql, sql, params, paramCount, sqlCb, userData);
#else
            sts = iENT_DbBackendUnsupported(MYSQL_TYPE);
#endif
            break;
        case PGSQL_TYPE:
#if ENT_ENABLE_PGSQL && ENT_PGSQL_FOUND
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
