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
    return -2;
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
        return -1;
    }

    if(dbCfg->sTag != ENTDB_S_TAG || dbCfg->eTag != ENTDB_E_TAG)
    {
        IENT_LOG_ERROR("Database handle is invalid.\n");
        return -1;
    }

    if(requireInit && dbCfg->isInit == false)
    {
        IENT_LOG_ERROR("Database handle is not initialized.\n");
        return -1;
    }

    if(dbCfgOut != NULL)
    {
        *dbCfgOut = dbCfg;
    }

    return 0;
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
    MSG_ID_T sts=0;
    DB_CFG* dbCfg = (DB_CFG*)dbHandle;
    if(dbCfg==NULL || dbCfg->sTag!=ENTDB_S_TAG || dbCfg->eTag != ENTDB_E_TAG)
    {
        IENT_LOG_ERROR("Database handle is invalid.\n");
        return -1;
    }

    if(dbType != dbCfg->dbType)
    {
        IENT_LOG_ERROR("Database handle reinit dbType failed.\n");
        return -2;
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
        return 1;
    }
#ifdef WIN32
    InitializeCriticalSection(&sDbMutex);
#else
    pthread_mutex_init(&sDbMutex,NULL);
#endif
    IENT_LOG_PRINT("InitializeCriticalSection.\n");
    sDbMutexInit = true;
    return 0;
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
#ifdef WIN32
    DeleteCriticalSection(&sDbMutex);
#else
    pthread_mutex_destroy(&sDbMutex);
#endif
    IENT_LOG_PRINT("DeleteCriticalSection.\n");
    sDbMutexInit = false;
    return 0;
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
    MSG_ID_T sts=0;
    DB_CFG*  dbCfg=NULL;
    if(sDbMutexInit==false)
    {
        IENT_LOG_ERROR("Uninitialized,please call ENT_DbInit.\n");
        return -1;
    }
    
    if(pdbHandle==NULL)
    {
        IENT_LOG_ERROR("Database handle is null\n");
        return -1;
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
                sts = -1;
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
            sts = -1;
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
    MSG_ID_T sts=0;
    DB_CFG*  dbCfg=NULL;
    if(sDbMutexInit==false)
    {
        IENT_LOG_ERROR("Uninitialized,please call ENT_DbInit.\n");
        return -1;
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
        sts = 0;
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
            sts = -1;
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
    MSG_ID_T sts=0;   
    DB_CFG*  dbCfg=NULL;
    if(sDbMutexInit==false)
    {
        IENT_LOG_ERROR("Uninitialized,please call ENT_DbInit.\n");
        return -1;
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
            sts = -1;
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
    MSG_ID_T sts=0;
    DB_CFG*  dbCfg=NULL;

    if(sql==NULL)
    {
        IENT_LOG_ERROR("Arguments are invalid.\n");
        return -1;
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
            return -2;
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
            sts = -1;
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
    MSG_ID_T sts=0;
    DB_CFG*  dbCfg=NULL;

    if(sql==NULL)
    {
        IENT_LOG_ERROR("Arguments are invalid.\n");
        return -1;
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
            return -2;
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
            sts = -1;
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
 * NAME        :ENT_DbMySQLInit
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
MSG_ID_T  ENT_DbMySQLInit(DB_HANDLE dbHandle)
{
#if ENT_ENABLE_MYSQL
    MYSQL*     db;

    DB_CFG*  dbCfg=(DB_CFG*)dbHandle;
    if(dbHandle == NULL ||
      dbCfg->sTag!=ENTDB_S_TAG ||
      dbCfg->eTag!=ENTDB_E_TAG ||
      dbCfg->dbType != MYSQL_TYPE)
    {
        IENT_LOG_ERROR("Arguments are invalid.\n");
        return -1;
    }
    
    db = mysql_init(NULL);
    if(db==NULL)
    {
        IENT_LOG_ERROR("mysql_init failed.\n");
        return  -2;
    }
    if (mysql_options(db, MYSQL_SET_CHARSET_NAME, "utf8"))
    {
        IENT_LOG_WARN("mysql_options failed,message:[%s]\n",mysql_error(db));
    }
    bool reconnect = 1;//enable reconnect
    if(mysql_options(db, MYSQL_OPT_RECONNECT, &reconnect))
    {
        IENT_LOG_WARN("mysql_options failed,message:[%s]\n",mysql_error(db));
    }
    
    if (mysql_real_connect(db, 
                   dbCfg->host, 
                   dbCfg->userName, 
                   dbCfg->passwd, 
                   dbCfg->database, 0, NULL, CLIENT_REMEMBER_OPTIONS) == NULL) 
    {
        IENT_LOG_ERROR("connect failed,message:[%s]\n",mysql_error(db));
        mysql_close(db);
        return -3;
    } 
    
    dbCfg->dbInstance.mysql = db; 
    dbCfg->isOpen = true;
    return 0;
#else
    (void)dbHandle;
    return iENT_DbBackendUnsupported(MYSQL_TYPE);
#endif
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_DbMySQLRead
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
#if ENT_ENABLE_MYSQL
MSG_ID_T  ENT_DbMySQLRead(MYSQL* dbHandle,const char* query,SqlResultCB userCb,void* userData)
{
    MSG_ID_T sts = 0;
    if(query==NULL || dbHandle==NULL)
    {
        IENT_LOG_ERROR("arguments is invalid\n");
        return -1;
    }
    
    if (mysql_query(dbHandle, query)) 
    {
        IENT_LOG_ERROR("mysql_query [%s],message:[%s]\n",query,mysql_error(dbHandle));
        return -1;
    }
     
     MYSQL_RES *result = mysql_store_result(dbHandle);
     
     if (result == NULL) 
     {
         IENT_LOG_ERROR("mysql_store_result failed,message:[%s]\n",mysql_error(dbHandle));
         return -1;
     }

     int       num_fields = mysql_num_fields(result);
     long long num_rows   = mysql_num_rows(result);
     
     char** fields = (char**)malloc(num_fields*sizeof(char*)); 
	 if (fields == NULL)
	 {
		 IENT_LOG_ERROR("malloc size [%d] failed\n",num_fields*sizeof(char*));
		 return -2;
	 }
     memset(fields,0,num_fields);
     char** rows   = (char**)malloc(num_fields*(size_t)num_rows*sizeof(char*));
	 if (rows == NULL)
	 {
		 free(fields);
		 fields = NULL;
		 IENT_LOG_ERROR("malloc size [%d] failed\n",num_fields*(size_t)num_rows*sizeof(char*));
		 return -3;
	 }
     memset(rows,0,num_fields*(size_t)num_rows);

     MYSQL_ROW    row;
     MYSQL_FIELD* field;
     long long    rowIdx = 0;
     
     int colIdx = 0;           
     while(field = mysql_fetch_field(result)) 
     {
         fields[colIdx] = field->name;
         colIdx++;
     }    
     
     while ((row = mysql_fetch_row(result))) 
     { 
         for(int i = 0; i < num_fields; i++) 
         {              
             rows[rowIdx*num_fields+i] = row[i];
         } 
         rowIdx++;
     }
     
     if(userCb!=NULL)
        userCb(fields,rows,num_rows,num_fields,userData);
     else
        defSqlResultCb(fields,rows,num_rows,num_fields,userData);
        
	 if (fields)
	 {
		 free(fields);
		 fields = NULL;
	 }
     
     if(rows)
     {
        free(rows);
        rows = NULL;
     }
     
     mysql_free_result(result);
     return 0;
}
#endif
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_DbMySQLWrite
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
#if ENT_ENABLE_MYSQL
MSG_ID_T  ENT_DbMySQLWrite(MYSQL* dbHandle,const char* query,SqlResultCB userCb,void* userData)
{
    MSG_ID_T  sts  = 0;
    long long rows = 0;
    if(query==NULL || dbHandle==NULL)
    {
        IENT_LOG_ERROR("arguments is invalid\n");
        return -1;
    }
    
    if (mysql_query(dbHandle, query)) 
    {
        IENT_LOG_ERROR("mysql_query failed [%s],message:[%s]\n",query,mysql_error(dbHandle));
        return -1;
    }
    
    rows = mysql_affected_rows(dbHandle);
    if(rows==-1)
    {
        IENT_LOG_ERROR("rows failed [%s],message:[%s]\n",query,mysql_error(dbHandle));
        return -2;
    }
    
    if(userCb!=NULL)
        userCb(NULL,NULL,rows,0,userData);
     else
        defSqlResultCb(NULL,NULL,rows,0,userData);
    
    return 0;
}
#endif
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_DbMySQLClose
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
MSG_ID_T  ENT_DbMySQLClose(DB_HANDLE dbHandle)
{
#if ENT_ENABLE_MYSQL
    DB_CFG*  dbCfg=(DB_CFG*)dbHandle;
    if(dbHandle == NULL || 
      dbCfg->sTag!=ENTDB_S_TAG ||
      dbCfg->eTag!=ENTDB_E_TAG ||
      dbCfg->dbType != MYSQL_TYPE)
    {
        IENT_LOG_ERROR("Arguments are invalid.\n");
        return -1;
    }
    
    if(dbCfg->dbInstance.mysql)
    {
        mysql_close(dbCfg->dbInstance.mysql);
        dbCfg->dbInstance.mysql=NULL;
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
#else
    (void)dbHandle;
    return iENT_DbBackendUnsupported(MYSQL_TYPE);
#endif
}


/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_DbPgSQLInit
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
MSG_ID_T ENT_DbPgSQLInit(DB_HANDLE dbHandle)
{
#if ENT_ENABLE_PGSQL
    DB_CFG* dbCfg = (DB_CFG*)dbHandle;
    PGconn* pgConn;
    char conninfo[1024];

    if(dbHandle == NULL)
    {
        IENT_LOG_ERROR("Arguments are invalid.\n");
        return -1;
    }

    if(dbCfg->sTag != ENTDB_S_TAG ||
       dbCfg->eTag != ENTDB_E_TAG ||
       dbCfg->dbType != PGSQL_TYPE)
    {
        IENT_LOG_ERROR("Arguments are invalid.\n");
        return -1;
    }

    snprintf(conninfo, sizeof(conninfo), "host=%s port=%d dbname=%s user=%s password=%s",
             dbCfg->host ? dbCfg->host : "",
             dbCfg->portNo ? dbCfg->portNo : 5432,
             dbCfg->database ? dbCfg->database : "",
             dbCfg->userName ? dbCfg->userName : "",
             dbCfg->passwd ? dbCfg->passwd : "");

    pgConn = PQconnectdb(conninfo);
    if (pgConn == NULL) {
        IENT_LOG_ERROR("PgSQL Connection failed: connection handle is null.\n");
        return -3;
    }

    if (PQstatus(pgConn) != CONNECTION_OK) {
        IENT_LOG_ERROR("PgSQL Connection failed: %s\n", PQerrorMessage(pgConn));
        PQfinish(pgConn);
        dbCfg->dbInstance.pgsql = NULL;
        return -3;
    }

    dbCfg->dbInstance.pgsql = pgConn;
    dbCfg->isOpen = true;
    return 0;
#else
    (void)dbHandle;
    return iENT_DbBackendUnsupported(PGSQL_TYPE);
#endif
}

/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_DbPgSQLClose
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
MSG_ID_T ENT_DbPgSQLClose(DB_HANDLE dbHandle)
{
#if ENT_ENABLE_PGSQL
    DB_CFG* dbCfg = (DB_CFG*)dbHandle;
    if(dbHandle == NULL)
    {
        IENT_LOG_ERROR("Arguments are invalid.\n");
        return -1;
    }

    if(dbCfg->sTag != ENTDB_S_TAG ||
       dbCfg->eTag != ENTDB_E_TAG ||
       dbCfg->dbType != PGSQL_TYPE)
    {
        IENT_LOG_ERROR("Arguments are invalid.\n");
        return -1;
    }

    if (dbCfg->dbInstance.pgsql) {
        PQfinish(dbCfg->dbInstance.pgsql);
        dbCfg->dbInstance.pgsql = NULL;
    }
    dbCfg->isOpen = false;
    
    if(dbCfg->host) free(dbCfg->host);
    if(dbCfg->database) free(dbCfg->database); 
    if(dbCfg->userName) free(dbCfg->userName);
    if(dbCfg->passwd) free(dbCfg->passwd);

#ifdef WIN32
    DeleteCriticalSection(&dbCfg->cs);
#else
    pthread_mutex_destroy(&dbCfg->cs);
#endif    
    sDbNum--;
    return 0;
#else
    (void)dbHandle;
    return iENT_DbBackendUnsupported(PGSQL_TYPE);
#endif
}

/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_DbPgSQLRead
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
MSG_ID_T ENT_DbPgSQLRead(void* dbHandle, const char* query, SqlResultCB userCb, void* userData)
{
#if ENT_ENABLE_PGSQL
    PGconn* pgConn = (PGconn*)dbHandle;
    PGresult* res;
    int rows;
    int cols;
    size_t fieldsCount;
    size_t rowCount;
    char** fields;
    char** rowRes;

    if(dbHandle == NULL || query == NULL)
    {
        IENT_LOG_ERROR("Arguments are invalid.\n");
        return -1;
    }

    res = PQexec(pgConn, query);
    if (res == NULL) {
        IENT_LOG_ERROR("PgSQL Read failed: execution returned no result.\n");
        return -4;
    }

    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        IENT_LOG_ERROR("PgSQL Read failed: %s\n", PQerrorMessage(pgConn));
        PQclear(res);
        return -4;
    }

    rows = PQntuples(res);
    cols = PQnfields(res);
    fieldsCount = (size_t)(cols > 0 ? cols : 1);
    rowCount = (size_t)((rows > 0 && cols > 0) ? rows * cols : 1);
    fields = (char**)malloc(fieldsCount * sizeof(char*));
    rowRes = (char**)malloc(rowCount * sizeof(char*));

    if(fields == NULL || rowRes == NULL)
    {
        IENT_LOG_ERROR("PgSQL Read failed: out of memory.\n");
        if(fields) free(fields);
        if(rowRes) free(rowRes);
        PQclear(res);
        return -3;
    }

    memset(fields, 0, fieldsCount * sizeof(char*));
    memset(rowRes, 0, rowCount * sizeof(char*));

    for (int i = 0; i < cols; i++) {
        fields[i] = (char*)PQfname(res, i);
    }

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            rowRes[r * cols + c] = (char*)PQgetvalue(res, r, c);
        }
    }

    if (userCb) {
        userCb(fields, rowRes, rows, cols, userData);
    } else {
        defSqlResultCb(fields, rowRes, rows, cols, userData);
    }

    free(fields);
    free(rowRes);
    PQclear(res);
    return 0;
#else
    (void)dbHandle; (void)query; (void)userCb; (void)userData;
    return iENT_DbBackendUnsupported(PGSQL_TYPE);
#endif
}

/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_DbPgSQLWrite
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
MSG_ID_T ENT_DbPgSQLWrite(void* dbHandle, const char* query, SqlResultCB userCb, void* userData)
{
#if ENT_ENABLE_PGSQL
    PGconn* pgConn = (PGconn*)dbHandle;
    PGresult* res;
    ExecStatusType status;
    long long affectedRows;

    if(dbHandle == NULL || query == NULL)
    {
        IENT_LOG_ERROR("Arguments are invalid.\n");
        return -1;
    }

    res = PQexec(pgConn, query);
    if(res == NULL)
    {
        IENT_LOG_ERROR("PgSQL Write failed: execution returned no result.\n");
        return -4;
    }

    status = PQresultStatus(res);
    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
        IENT_LOG_ERROR("PgSQL Write failed: %s\n", PQerrorMessage(pgConn));
        PQclear(res);
        return -4;
    }

    affectedRows = 0;
    {
        const char* cmdTuples = PQcmdTuples(res);
        if(cmdTuples != NULL && cmdTuples[0] != '\0')
        {
            affectedRows = atoll(cmdTuples);
        }
        else if(status == PGRES_TUPLES_OK)
        {
            affectedRows = PQntuples(res);
        }
    }
    
    if (userCb) {
        userCb(NULL, NULL, affectedRows, 0, userData);
    } else {
        defSqlResultCb(NULL, NULL, affectedRows, 0, userData);
    }

    PQclear(res);
    return 0;
#else
    (void)dbHandle; (void)query; (void)userCb; (void)userData;
    return iENT_DbBackendUnsupported(PGSQL_TYPE);
#endif
}
