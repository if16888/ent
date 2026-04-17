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

MSG_ID_T ENT_DbMySQLInit(DB_HANDLE dbHandle)
{
#if ENT_ENABLE_MYSQL
    MYSQL* db;

    DB_CFG* dbCfg = (DB_CFG*)dbHandle;
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
        return -2;
    }
    if (mysql_options(db, MYSQL_SET_CHARSET_NAME, "utf8"))
    {
        IENT_LOG_WARN("mysql_options failed,message:[%s]\n",mysql_error(db));
    }
    bool reconnect = 1;
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

#if ENT_ENABLE_MYSQL
MSG_ID_T ENT_DbMySQLRead(MYSQL* dbHandle,const char* query,SqlResultCB userCb,void* userData)
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
        IENT_LOG_ERROR("malloc size [%d] failed\n",num_fields*(int)sizeof(char*));
        return -2;
    }
    memset(fields,0,num_fields*sizeof(char*));
    char** rows = (char**)malloc(num_fields*(size_t)num_rows*sizeof(char*));
    if (rows == NULL)
    {
        free(fields);
        fields = NULL;
        IENT_LOG_ERROR("malloc size [%d] failed\n",num_fields*(int)num_rows*(int)sizeof(char*));
        return -3;
    }
    memset(rows,0,num_fields*(size_t)num_rows*sizeof(char*));

    MYSQL_ROW    row;
    MYSQL_FIELD* field;
    long long    rowIdx = 0;

    int colIdx = 0;
    while((field = mysql_fetch_field(result)))
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

MSG_ID_T ENT_DbMySQLWrite(MYSQL* dbHandle,const char* query,SqlResultCB userCb,void* userData)
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

MSG_ID_T ENT_DbMySQLClose(DB_HANDLE dbHandle)
{
#if ENT_ENABLE_MYSQL
    DB_CFG* dbCfg = (DB_CFG*)dbHandle;
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
        dbCfg->dbInstance.mysql = NULL;
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
