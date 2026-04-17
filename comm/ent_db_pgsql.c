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

MSG_ID_T ENT_DbPgSQLInit(DB_HANDLE dbHandle)
{
#if ENT_ENABLE_PGSQL
    DB_CFG* dbCfg = (DB_CFG*)dbHandle;
    PGconn* pgConn;
    char conninfo[1024];

    if(dbHandle == NULL)
    {
        IENT_LOG_ERROR("Arguments are invalid.\n");
        return ENT_DBS_BAD_ARGUMENT;
    }

    if(dbCfg->sTag != ENTDB_S_TAG ||
       dbCfg->eTag != ENTDB_E_TAG ||
       dbCfg->dbType != PGSQL_TYPE)
    {
        IENT_LOG_ERROR("Arguments are invalid.\n");
        return ENT_DBS_BAD_HANDLE;
    }

    snprintf(conninfo, sizeof(conninfo), "host=%s port=%d dbname=%s user=%s password=%s",
             dbCfg->host ? dbCfg->host : "",
             dbCfg->portNo ? dbCfg->portNo : 5432,
             dbCfg->database ? dbCfg->database : "",
             dbCfg->userName ? dbCfg->userName : "",
             dbCfg->passwd ? dbCfg->passwd : "");

    pgConn = PQconnectdb(conninfo);
    if(pgConn == NULL)
    {
        IENT_LOG_ERROR("PgSQL Connection failed: connection handle is null.\n");
        return ENT_DBS_OPEN_FAILED;
    }

    if(PQstatus(pgConn) != CONNECTION_OK)
    {
        IENT_LOG_ERROR("PgSQL Connection failed: %s\n", PQerrorMessage(pgConn));
        PQfinish(pgConn);
        dbCfg->dbInstance.pgsql = NULL;
        return ENT_DBS_OPEN_FAILED;
    }

    dbCfg->dbInstance.pgsql = pgConn;
    dbCfg->isOpen = true;
    return ENT_SYS_NORMAL;
#else
    (void)dbHandle;
    return iENT_DbBackendUnsupported(PGSQL_TYPE);
#endif
}

MSG_ID_T ENT_DbPgSQLClose(DB_HANDLE dbHandle)
{
#if ENT_ENABLE_PGSQL
    DB_CFG* dbCfg = (DB_CFG*)dbHandle;
    if(dbHandle == NULL)
    {
        IENT_LOG_ERROR("Arguments are invalid.\n");
        return ENT_DBS_BAD_ARGUMENT;
    }

    if(dbCfg->sTag != ENTDB_S_TAG ||
       dbCfg->eTag != ENTDB_E_TAG ||
       dbCfg->dbType != PGSQL_TYPE)
    {
        IENT_LOG_ERROR("Arguments are invalid.\n");
        return ENT_DBS_BAD_HANDLE;
    }

    if(dbCfg->dbInstance.pgsql)
    {
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
    return ENT_SYS_NORMAL;
#else
    (void)dbHandle;
    return iENT_DbBackendUnsupported(PGSQL_TYPE);
#endif
}

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
        return ENT_DBS_BAD_ARGUMENT;
    }

    res = PQexec(pgConn, query);
    if(res == NULL)
    {
        IENT_LOG_ERROR("PgSQL Read failed: execution returned no result.\n");
        return ENT_DBS_QUERY_FAILED;
    }

    if(PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        IENT_LOG_ERROR("PgSQL Read failed: %s\n", PQerrorMessage(pgConn));
        PQclear(res);
        return ENT_DBS_RESULT_FAILED;
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
        return ENT_DBS_ALLOC_FAILED;
    }

    memset(fields, 0, fieldsCount * sizeof(char*));
    memset(rowRes, 0, rowCount * sizeof(char*));

    for(int i = 0; i < cols; i++)
    {
        fields[i] = (char*)PQfname(res, i);
    }

    for(int r = 0; r < rows; r++)
    {
        for(int c = 0; c < cols; c++)
        {
            rowRes[r * cols + c] = (char*)PQgetvalue(res, r, c);
        }
    }

    if(userCb)
    {
        userCb(fields, rowRes, rows, cols, userData);
    }
    else
    {
        defSqlResultCb(fields, rowRes, rows, cols, userData);
    }

    free(fields);
    free(rowRes);
    PQclear(res);
    return ENT_SYS_NORMAL;
#else
    (void)dbHandle;
    (void)query;
    (void)userCb;
    (void)userData;
    return iENT_DbBackendUnsupported(PGSQL_TYPE);
#endif
}

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
        return ENT_DBS_BAD_ARGUMENT;
    }

    res = PQexec(pgConn, query);
    if(res == NULL)
    {
        IENT_LOG_ERROR("PgSQL Write failed: execution returned no result.\n");
        return ENT_DBS_QUERY_FAILED;
    }

    status = PQresultStatus(res);
    if(status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK)
    {
        IENT_LOG_ERROR("PgSQL Write failed: %s\n", PQerrorMessage(pgConn));
        PQclear(res);
        return ENT_DBS_RESULT_FAILED;
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

    if(userCb)
    {
        userCb(NULL, NULL, affectedRows, 0, userData);
    }
    else
    {
        defSqlResultCb(NULL, NULL, affectedRows, 0, userData);
    }

    PQclear(res);
    return ENT_SYS_NORMAL;
#else
    (void)dbHandle;
    (void)query;
    (void)userCb;
    (void)userData;
    return iENT_DbBackendUnsupported(PGSQL_TYPE);
#endif
}
