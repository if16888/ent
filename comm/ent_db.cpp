/*-----------------------------------------------------------------------------
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *-----------------------------------------------------------------------------
 */
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>

#ifdef WIN32
#include <windows.h>
#endif

#ifdef __linux__
#include <pthread.h>
#endif

#include "sqlite3.h"
#include "mysql.h"
#include "ent_types.h"
#include "ent_db.h"
#include "ient_comm.h"

#define ENTDB_S_TAG (0xEADBEB90)
#define ENTDB_E_TAG (0xEB90EADB)

typedef  struct DB_CFG {
  unsigned int     sTag;
  DB_TYPE          dbType;
  char*            userName;
  char*            passwd;
  char*            host;
  char*            database;
  int              portNo;
  bool             isInit;
#ifdef WIN32
  CRITICAL_SECTION cs;
#else
  pthread_mutex_t  cs;
#endif
  bool             isOpen;
  union 
    {
      sqlite3* sqlite;
      MYSQL*   mysql;
    }      dbInstance;
  unsigned int eTag;
} DB_CFG;

typedef struct USER_SQLITE_DATA
{
    SqlResultCB  userCb;
    void*        userData;
}USER_SQLITE_READ,USER_SQLITE_WRITE;

MSG_ID_T  ENT_DbSqliteInit(DB_HANDLE dbHandle);
MSG_ID_T  ENT_DbSqliteClose(DB_HANDLE dbHandle);
MSG_ID_T  ENT_DbSqliteRead(sqlite3* dbHandle,const char* query,SqlResultCB userCb,void* userData);
MSG_ID_T  ENT_DbSqliteWrite(sqlite3* dbHandle,const char* query,SqlResultCB userCb,void* userData);

MSG_ID_T  ENT_DbMySQLInit(DB_HANDLE dbHandle);
MSG_ID_T  ENT_DbMySQLClose(DB_HANDLE dbHandle);
MSG_ID_T  ENT_DbMySQLRead(MYSQL* dbHandle,const char* query,SqlResultCB userCb,void* userData);
MSG_ID_T  ENT_DbMySQLWrite(MYSQL* dbHandle,const char* query,SqlResultCB userCb,void* userData);

#ifdef WIN32
static CRITICAL_SECTION sDbMutex;
#pragma warning(disable : 4996)
#else
static pthread_mutex_t  sDbMutex;
#endif

static long             sDbNum = 0;
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
MSG_ID_T  ENT_DbInit()
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
MSG_ID_T  ENT_DbClose()
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
MSG_ID_T  ENT_DbInitHandle(DB_HANDLE* pdbHandle,
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
MSG_ID_T ENT_DbOpen(DB_HANDLE dbHandle)
{
    MSG_ID_T sts=0;
    DB_CFG*  dbCfg=(DB_CFG*)dbHandle;
    if(sDbMutexInit==false)
    {
        IENT_LOG_ERROR("Uninitialized,please call ENT_DbInit.\n");
        return -1;
    }
    
    if(dbHandle==NULL || 
    dbCfg->sTag!=ENTDB_S_TAG||
    dbCfg->eTag!=ENTDB_E_TAG||
    dbCfg->isInit == false||
    dbCfg->isOpen == true)
    {
        IENT_LOG_ERROR("Arguments are invalid.\n");
        return -1;
    }
    
#ifdef WIN32
    EnterCriticalSection(&dbCfg->cs);   
#else
    pthread_mutex_lock(&dbCfg->cs);
#endif 
    switch(dbCfg->dbType)
    {
        case SQLITE_TYPE:
            sts = ENT_DbSqliteInit(dbCfg);
            break;
            
        case MYSQL_TYPE:
            sts = ENT_DbMySQLInit(dbCfg);
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
MSG_ID_T ENT_DbCloseHandle(DB_HANDLE dbHandle)
{
    MSG_ID_T sts=0;   
    DB_CFG*  dbCfg=(DB_CFG*)dbHandle;
    if(sDbMutexInit==false)
    {
        IENT_LOG_ERROR("Uninitialized,please call ENT_DbInit.\n");
        return -1;
    }
    
    if(dbHandle==NULL||
       dbCfg->sTag!=ENTDB_S_TAG||
       dbCfg->eTag!=ENTDB_E_TAG)
    {
        IENT_LOG_ERROR("Arguments are invalid.\n");
        return -1;
    }

#ifdef WIN32
    EnterCriticalSection(&sDbMutex);   
#else
    pthread_mutex_lock(&sDbMutex);
#endif    

    switch(dbCfg->dbType)
    {
        case SQLITE_TYPE:
            sts = ENT_DbSqliteClose(dbCfg);
            break;
            
        case MYSQL_TYPE:
            sts = ENT_DbMySQLClose(dbCfg);
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
MSG_ID_T ENT_DbRead(DB_HANDLE dbHandle,const char* sql,SqlResultCB sqlCb,void* userData)
{
    MSG_ID_T sts=0;
    DB_CFG*  dbCfg=(DB_CFG*)dbHandle;
    if(dbHandle==NULL || 
        dbCfg->sTag!=ENTDB_S_TAG||
        dbCfg->eTag!=ENTDB_E_TAG ||
        dbCfg->isInit == false ||
        sql==NULL)
    {
        IENT_LOG_ERROR("Arguments are invalid.\n");
        return -1;
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
            sts = ENT_DbMySQLRead(dbCfg->dbInstance.mysql,sql,sqlCb,userData);
            break;
            
        case SQLITE_TYPE:
            sts = ENT_DbSqliteRead(dbCfg->dbInstance.sqlite,sql,sqlCb,userData);
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
MSG_ID_T ENT_DbWrite(DB_HANDLE dbHandle,const char* sql,SqlResultCB sqlCb,void* userData)
{
    MSG_ID_T sts=0;
    DB_CFG*  dbCfg=(DB_CFG*)dbHandle;
    if(dbHandle==NULL|| 
        dbCfg->sTag!=ENTDB_S_TAG||
        dbCfg->eTag!=ENTDB_E_TAG ||
        dbCfg->isInit == false ||
        sql==NULL)
    {
        IENT_LOG_ERROR("Arguments are invalid.\n");
        return -1;
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
            sts = ENT_DbMySQLWrite(dbCfg->dbInstance.mysql,sql,sqlCb,userData);
            break;
            
        case SQLITE_TYPE:
            sts = ENT_DbSqliteWrite(dbCfg->dbInstance.sqlite,sql,sqlCb,userData);
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
 * NAME        :ENT_DbSqliteInit
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
MSG_ID_T  ENT_DbSqliteInit(DB_HANDLE dbHandle)
{
    int               rc;
    DB_CFG*           dbCfg=(DB_CFG*)dbHandle;

    if(dbHandle==NULL)
    {
        IENT_LOG_ERROR("Arguments are invalid.\n");
        return -1;
    }
    if(dbCfg->database==NULL)
    {
        IENT_LOG_ERROR("database name is invalid.\n");
        return -2;
    }
    rc = sqlite3_open(dbCfg->database,&dbCfg->dbInstance.sqlite);
    if(rc)
    {
        IENT_LOG_ERROR("Can't open database:[%s]\n",sqlite3_errmsg(dbCfg->dbInstance.sqlite));
        sqlite3_close(dbCfg->dbInstance.sqlite);
        return  -3;
    }
    dbCfg->isOpen = true;
    return 0;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_DbSqliteClose
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
MSG_ID_T  ENT_DbSqliteClose(DB_HANDLE dbHandle)
{
    DB_CFG*  dbCfg=(DB_CFG*)dbHandle;
    if(dbHandle == NULL || 
      dbCfg->dbType != SQLITE_TYPE)
    {
        IENT_LOG_ERROR("Arguments are invalid.\n");
        return -1;
    }
    
    if(dbCfg->dbInstance.sqlite)
    {
        sqlite3_close(dbCfg->dbInstance.sqlite);
        dbCfg->dbInstance.sqlite=NULL;
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
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :iENT_DbSqliteReadCb
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
static MSG_ID_T iENT_DbSqliteReadCb(void *data, int argc, char **argv, char **azColName)
{
    USER_SQLITE_READ*  userCtx = (USER_SQLITE_READ*)data;
    if(data == NULL || userCtx->userCb == NULL || userCtx->userData == NULL)
    {
        IENT_LOG_ERROR("Arguments are invalid.\n");
        return -1;
    }
    userCtx->userCb(azColName,argv,1,argc,userCtx->userData);
    return 0;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :iENT_DbSqliteWriteCb
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
static MSG_ID_T iENT_DbSqliteWriteCb(void *data, int argc, char **argv, char **azColName)
{
    USER_SQLITE_WRITE*  userCtx = (USER_SQLITE_WRITE*)data;
    if(data == NULL || userCtx->userCb == NULL)
    {
        IENT_LOG_ERROR("Arguments are invalid.\n");
        return -1;
    }
    
    userCtx->userCb(azColName,argv,1,argc,userCtx->userData);
    return 0;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_DbSqliteRead
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
MSG_ID_T  ENT_DbSqliteRead(sqlite3* dbHandle,const char* query,SqlResultCB userCb,void* userData)
{
    int      rc;
    char*    errMsg=NULL;
    USER_SQLITE_READ  sqliteUser;
    if(dbHandle == NULL || query==NULL)
    {
        IENT_LOG_ERROR("Arguments are invalid.\n");
        return -1;
    }
    sqliteUser.userCb   = userCb;
    sqliteUser.userData = userData;
    rc = sqlite3_exec(dbHandle,query,iENT_DbSqliteReadCb,&sqliteUser,&errMsg);
    if( rc != SQLITE_OK )
    {
        IENT_LOG_ERROR("Can't read table :[%d]->[%s]\n",rc,errMsg);
        sqlite3_free(errMsg);
        return -2;
    }  

    return 0;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_DbSqliteWrite
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
MSG_ID_T  ENT_DbSqliteWrite(sqlite3* dbHandle,const char* query,SqlResultCB userCb,void* userData)
{
    MSG_ID_T           sts = 0;
    int                rc;
    char*              errMsg=NULL;
    USER_SQLITE_WRITE  sqliteUser;
    if(dbHandle == NULL || query==NULL)
    {
        IENT_LOG_ERROR("Arguments are invalid.\n");
        return -1;
    }
    
    sqliteUser.userCb   = userCb;
    sqliteUser.userData = userData;
    
    rc = sqlite3_exec(dbHandle,query,iENT_DbSqliteWriteCb,&sqliteUser,&errMsg);
    if( rc != SQLITE_OK )
    {
        IENT_LOG_ERROR("Can't exec sql [%s] :[%d]->[%s]\n",query,rc,errMsg);
        sqlite3_free(errMsg);
        sts = -2;
    }  
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
}

