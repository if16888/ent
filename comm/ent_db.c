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
 *                   
 *
 * COMPLETION
 * STATUS      :  void
 *                           
 *
 *                            
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
}

static MSG_ID_T iENT_DbBackendUnsupported(DB_TYPE dbType)
{
    IENT_LOG_ERROR("Database backend [%d] is not enabled in this build.\n",dbType);
    return ENT_DBS_UNSUPPORTED;
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
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :iENT_DbReInit
 *
 * DESCRIPTION :   
 *                 
 *                   
 *
 * COMPLETION
 * STATUS      :  void
 *                           
 *
 *                            
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
    MSG_ID_T sts=ENT_SYS_NORMAL;
    DB_CFG* dbCfg = (DB_CFG*)dbHandle;
    if(dbCfg==NULL || dbCfg->sTag!=ENTDB_S_TAG || dbCfg->eTag != ENTDB_E_TAG)
    {
        IENT_LOG_ERROR("Database handle is invalid.\n");
        return ENT_DBS_BAD_HANDLE;
    }

    if(dbType != dbCfg->dbType)
    {
        IENT_LOG_ERROR("Database handle reinit dbType failed.\n");
        return ENT_DBS_BAD_ARGUMENT;
    }
    
    if(host)
    {
        if(dbCfg->host)
        {
            free(dbCfg->host);
            dbCfg->host = NULL;
        }
        dbCfg->host = strdup(host);
    }
    if(database)
    {
        if(dbCfg->database)
        {
            free(dbCfg->database);
            dbCfg->database = NULL;
        }
        dbCfg->database = strdup(database);
    }
    if(user)
    {
        if(dbCfg->userName)
        {
            free(dbCfg->userName);
            dbCfg->userName = NULL;
        }
        dbCfg->userName = strdup(user);
     }
    if(passwd)
    {
        if(dbCfg->passwd)
        {
            free(dbCfg->passwd);
            dbCfg->passwd = NULL;
        }
        dbCfg->passwd = strdup(passwd);
    }
    dbCfg->portNo = port;
    if(dbCfg->isOpen)
    {
        ENT_DbCloseHandle(dbCfg);
    }
    return sts;   
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_DbInit
 *
 * DESCRIPTION :   
 *                 
 *                   
 *
 * COMPLETION
 * STATUS      :  0
 *                Success; Service has completed successfully.           
 *
 *                            
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
 *                   
 *
 * COMPLETION
 * STATUS      :  0
 *                Success; Service has completed successfully.           
 *
 *                            
 *
 *-----------------------------------------------------------------------------
 */
ENT_PUBLIC MSG_ID_T  ENT_DbClose()
{
    if(sDbMutexInit == false)
    {
        return ENT_SYS_CLOSE_UNINITIALIZED;
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
 *                   
 *
 * COMPLETION
 * STATUS      :  0
 *                Success; Service has completed successfully.           
 *
 *                            
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
    }
    
#ifdef WIN32
    EnterCriticalSection(&sDbMutex);
#else
    pthread_mutex_lock(&sDbMutex);
#endif

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
                dbCfg->host = strdup(host);
            if(database)
                dbCfg->database = strdup(database);
            if(user)
                dbCfg->userName = strdup(user);
            if(passwd)
                dbCfg->passwd = strdup(passwd);
            dbCfg->portNo = port;
            break;
            
        default:
            *pdbHandle = NULL;
            IENT_LOG_WARN("Database Type is not supported,[%d]\n",dbType);
            sts = ENT_DBS_UNSUPPORTED;
            goto END_OF_ROUTINE;
            break; 
    }
#ifdef WIN32
    InitializeCriticalSection(&dbCfg->cs);
#else
    pthread_mutex_init(&dbCfg->cs,NULL);
#endif
    dbCfg->isInit = true;
    dbCfg->sTag = ENTDB_S_TAG;
    dbCfg->eTag = ENTDB_E_TAG;
    *pdbHandle = dbCfg; 
    sDbNum++;  

END_OF_ROUTINE: 
#ifdef WIN32   
    LeaveCriticalSection(&sDbMutex);
#else
    pthread_mutex_unlock(&sDbMutex);
#endif
    return sts;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_DbOpen
 *
 * DESCRIPTION :   
 *                 
 *                   
 *
 * COMPLETION
 * STATUS      :  0
 *                Success; Service has completed successfully.           
 *
 *                            
 *
 *-----------------------------------------------------------------------------
 */
ENT_PUBLIC MSG_ID_T ENT_DbOpen(DB_HANDLE dbHandle)
{
    MSG_ID_T sts=ENT_SYS_NORMAL;
    DB_CFG*  dbCfg=NULL;
    if(sDbMutexInit==false)
    {
        IENT_LOG_ERROR("Uninitialized,please call ENT_DbInit.\n");
        return ENT_DBS_NOT_INITIALIZED;
    }

    sts = iENT_DbValidateHandle(dbHandle, &dbCfg, true);
    if(sts < 0)
    {
        return sts;
    }

#ifdef WIN32
    EnterCriticalSection(&dbCfg->cs);   
#else
    pthread_mutex_lock(&dbCfg->cs);
#endif

    if(dbCfg->isOpen == true)
    {
        sts = ENT_SYS_NORMAL;
        goto END_OF_ROUTINE;
    }

    switch(dbCfg->dbType)
    {
        case SQLITE_TYPE:
#if ENT_ENABLE_SQLITE
            sts = ENT_DbSqliteInit(dbCfg);
#else
            sts = iENT_DbBackendUnsupported(SQLITE_TYPE);
#endif
            break;
            
        case MYSQL_TYPE:
#if ENT_ENABLE_MYSQL
            sts = ENT_DbMySQLInit(dbCfg);
#else
            sts = iENT_DbBackendUnsupported(MYSQL_TYPE);
#endif
            break;
        case PGSQL_TYPE:
#if ENT_ENABLE_PGSQL
            sts = ENT_DbPgSQLInit(dbCfg);
#else
            sts = iENT_DbBackendUnsupported(PGSQL_TYPE);
#endif
            break;
        default:
            sts = ENT_DBS_BAD_HANDLE;
            break;
    }

END_OF_ROUTINE:
#ifdef WIN32
    LeaveCriticalSection(&dbCfg->cs);
#else
    pthread_mutex_unlock(&dbCfg->cs);
#endif

    return sts;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_DbCloseHandle
 *
 * DESCRIPTION :   
 *                 
 *                   
 *
 * COMPLETION
 * STATUS      :  0
 *                Success; Service has completed successfully.           
 *
 *                            
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

    sts = iENT_DbValidateHandle(dbHandle, &dbCfg, false);
    if(sts < 0)
    {
        return sts;
    }

#ifdef WIN32
    EnterCriticalSection(&sDbMutex);   
#else
    pthread_mutex_lock(&sDbMutex);
#endif    

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
    memset(dbCfg,0,sizeof(DB_CFG));
    if(dbCfg)
    {
        free(dbCfg);
    }
    
#ifdef WIN32
    LeaveCriticalSection(&sDbMutex); 
#else
    pthread_mutex_unlock(&sDbMutex);
#endif
          
    return sts;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_DbRead
 *
 * DESCRIPTION :   
 *                 
 *                   
 *
 * COMPLETION
 * STATUS      :  0
 *                Success; Service has completed successfully.           
 *
 *                            
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

    sts = iENT_DbValidateHandle(dbHandle, &dbCfg, true);
    if(sts < 0)
    {
        return sts;
    }

    if(dbCfg->isOpen==false)
    {
        sts = ENT_DbOpen(dbCfg);
        if(sts<0)
        {
            IENT_LOG_ERROR("ENT_DbOpen failed.\n");
            return sts;
        }
    } 
    
#ifdef WIN32
    EnterCriticalSection(&dbCfg->cs);   
#else
    pthread_mutex_lock(&dbCfg->cs);
#endif

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
#ifdef WIN32
    LeaveCriticalSection(&dbCfg->cs);
#else
    pthread_mutex_unlock(&dbCfg->cs);
#endif
 
    return sts;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_DbWrite
 *
 * DESCRIPTION :   
 *                 
 *                   
 *
 * COMPLETION
 * STATUS      :  0
 *                Success; Service has completed successfully.           
 *
 *                            
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

    sts = iENT_DbValidateHandle(dbHandle, &dbCfg, true);
    if(sts < 0)
    {
        return sts;
    }

    if(dbCfg->isOpen==false)
    {
        sts = ENT_DbOpen(dbCfg);
        if(sts<0)
        {
            IENT_LOG_ERROR("ENT_DbOpen failed.\n");
            return sts;
        }
    } 
    
#ifdef WIN32
    EnterCriticalSection(&dbCfg->cs);   
#else
    pthread_mutex_lock(&dbCfg->cs);
#endif
     
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
#ifdef WIN32
    LeaveCriticalSection(&dbCfg->cs);
#else
    pthread_mutex_unlock(&dbCfg->cs);
#endif

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

    sts = iENT_DbValidateHandle(dbHandle, &dbCfg, true);
    if(sts < 0)
    {
        return sts;
    }

    if(dbCfg->isOpen==false)
    {
        sts = ENT_DbOpen(dbCfg);
        if(sts<0)
        {
            IENT_LOG_ERROR("ENT_DbOpen failed.\n");
            return sts;
        }
    }

#ifdef WIN32
    EnterCriticalSection(&dbCfg->cs);
#else
    pthread_mutex_lock(&dbCfg->cs);
#endif

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
            sts = iENT_DbBackendUnsupported(MYSQL_TYPE);
            break;
        case PGSQL_TYPE:
            sts = iENT_DbBackendUnsupported(PGSQL_TYPE);
            break;
        default:
            sts = ENT_DBS_BAD_HANDLE;
            break;
    }

#ifdef WIN32
    LeaveCriticalSection(&dbCfg->cs);
#else
    pthread_mutex_unlock(&dbCfg->cs);
#endif

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

    sts = iENT_DbValidateHandle(dbHandle, &dbCfg, true);
    if(sts < 0)
    {
        return sts;
    }

    if(dbCfg->isOpen==false)
    {
        sts = ENT_DbOpen(dbCfg);
        if(sts<0)
        {
            IENT_LOG_ERROR("ENT_DbOpen failed.\n");
            return sts;
        }
    }

#ifdef WIN32
    EnterCriticalSection(&dbCfg->cs);
#else
    pthread_mutex_lock(&dbCfg->cs);
#endif

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
            sts = iENT_DbBackendUnsupported(MYSQL_TYPE);
            break;
        case PGSQL_TYPE:
            sts = iENT_DbBackendUnsupported(PGSQL_TYPE);
            break;
        default:
            sts = ENT_DBS_BAD_HANDLE;
            break;
    }

#ifdef WIN32
    LeaveCriticalSection(&dbCfg->cs);
#else
    pthread_mutex_unlock(&dbCfg->cs);
#endif

    return sts;
}
