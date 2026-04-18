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
#include <limits.h>

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
typedef struct SQLITE_RESULT_TAG
{
    char** fields;
    char** rows;
    int columnCount;
    size_t rowCount;
    size_t rowCapacity;
} SQLITE_RESULT;

static void iENT_DbSqliteDefaultCb(char** fields,char** rowRes,long long rowNum,int columnNum,void* data)
{
    printf("Affect rows [%lld]\n", rowNum);
    if(fields == NULL || rowRes == NULL)
    {
        return;
    }

    int rowIdx = 0;
    int colIdx = 0;

    for(colIdx = 0; colIdx < columnNum; colIdx++)
    {
        printf("%s ", fields[colIdx]);
    }
    printf("\n");

    for(rowIdx = 0; rowIdx < rowNum; rowIdx++)
    {
        for(colIdx = 0; colIdx < columnNum; colIdx++)
        {
            printf("%s ", rowRes[rowIdx * columnNum + colIdx]);
        }
        printf("\n");
    }
    (void)data;
}

static void iENT_DbSqliteFreeResult(SQLITE_RESULT* result)
{
    if(result == NULL)
    {
        return;
    }

    if(result->rows != NULL)
    {
        size_t i;
        for(i = 0; i < result->rowCount * (size_t)result->columnCount; i++)
        {
            if(result->rows[i] != NULL)
            {
                free(result->rows[i]);
            }
        }
        free(result->rows);
    }

    if(result->fields != NULL)
    {
        free(result->fields);
    }
    memset(result, 0, sizeof(*result));
}

static MSG_ID_T iENT_DbSqliteAppendCell(SQLITE_RESULT* result,
                                        sqlite3_stmt* stmt,
                                        int rowIdx,
                                        int colIdx)
{
    const unsigned char* textValue = sqlite3_column_text(stmt, colIdx);
    int byteCount = sqlite3_column_bytes(stmt, colIdx);
    char* copy = NULL;

    if(sqlite3_column_type(stmt, colIdx) == SQLITE_NULL)
    {
        result->rows[(size_t)rowIdx * (size_t)result->columnCount + (size_t)colIdx] = NULL;
        return ENT_SYS_NORMAL;
    }

    if(byteCount < 0)
    {
        byteCount = 0;
    }

    copy = (char*)malloc((size_t)byteCount + 1);
    if(copy == NULL)
    {
        IENT_LOG_ERROR("sqlite cell allocation failed.\n");
        return ENT_DBS_ALLOC_FAILED;
    }

    if(byteCount > 0 && textValue != NULL)
    {
        memcpy(copy, textValue, (size_t)byteCount);
    }
    copy[byteCount] = '\0';
    result->rows[(size_t)rowIdx * (size_t)result->columnCount + (size_t)colIdx] = copy;
    return ENT_SYS_NORMAL;
}

static MSG_ID_T iENT_DbSqliteCollectRows(sqlite3* dbHandle,
                                         sqlite3_stmt* stmt,
                                         SqlResultCB userCb,
                                         void* userData)
{
    MSG_ID_T sts = ENT_SYS_NORMAL;
    SQLITE_RESULT result;
    int rc;

    memset(&result, 0, sizeof(result));
    result.columnCount = sqlite3_column_count(stmt);
    if(result.columnCount < 0)
    {
        IENT_LOG_ERROR("sqlite column count failed.\n");
        return ENT_DBS_RESULT_FAILED;
    }

    while((rc = sqlite3_step(stmt)) == SQLITE_ROW)
    {
        int colIdx;
        char** newRows;
        size_t newCapacity;

        if(result.rowCount == result.rowCapacity)
        {
            newCapacity = (result.rowCapacity == 0) ? 8 : result.rowCapacity * 2;
            newRows = (char**)realloc(result.rows, newCapacity * (size_t)result.columnCount * sizeof(char*));
            if(newRows == NULL)
            {
                IENT_LOG_ERROR("sqlite result allocation failed.\n");
                sts = ENT_DBS_ALLOC_FAILED;
                goto END_OF_ROUTINE;
            }
            result.rows = newRows;
            memset(result.rows + result.rowCapacity * (size_t)result.columnCount,
                   0,
                   (newCapacity - result.rowCapacity) * (size_t)result.columnCount * sizeof(char*));
            result.rowCapacity = newCapacity;
        }

        if(result.fields == NULL)
        {
            result.fields = (char**)malloc((size_t)result.columnCount * sizeof(char*));
            if(result.fields == NULL)
            {
                IENT_LOG_ERROR("sqlite field allocation failed.\n");
                sts = ENT_DBS_ALLOC_FAILED;
                goto END_OF_ROUTINE;
            }
            memset(result.fields, 0, (size_t)result.columnCount * sizeof(char*));
            for(colIdx = 0; colIdx < result.columnCount; colIdx++)
            {
                result.fields[colIdx] = (char*)sqlite3_column_name(stmt, colIdx);
            }
        }

        for(colIdx = 0; colIdx < result.columnCount; colIdx++)
        {
            sts = iENT_DbSqliteAppendCell(&result, stmt, (int)result.rowCount, colIdx);
            if(sts < 0)
            {
                goto END_OF_ROUTINE;
            }
        }
        result.rowCount++;
    }

    if(rc != SQLITE_DONE)
    {
        IENT_LOG_ERROR("sqlite step failed:[%d]->[%s]\n", rc, sqlite3_errmsg(dbHandle));
        sts = ENT_DBS_EXEC_FAILED;
        goto END_OF_ROUTINE;
    }

    if(result.rowCount > 0)
    {
        if(userCb != NULL)
        {
            userCb(result.fields, result.rows, (long long)result.rowCount, result.columnCount, userData);
        }
        else
        {
            iENT_DbSqliteDefaultCb(result.fields, result.rows, (long long)result.rowCount, result.columnCount, userData);
        }
    }

END_OF_ROUTINE:
    iENT_DbSqliteFreeResult(&result);
    return sts;
}

static MSG_ID_T iENT_DbSqliteBindParam(sqlite3_stmt* stmt, int index, const ENT_DB_PARAM* param)
{
    int rc;

    if(param == NULL)
    {
        IENT_LOG_ERROR("sqlite parameter is null.\n");
        return ENT_DBS_BAD_PARAMS;
    }

    switch(param->type)
    {
        case ENT_DB_PARAM_TEXT_E:
            rc = sqlite3_bind_text(stmt, index, param->value.text, -1, SQLITE_TRANSIENT);
            break;
        case ENT_DB_PARAM_INT_E:
            rc = sqlite3_bind_int(stmt, index, param->value.i32);
            break;
        case ENT_DB_PARAM_INT64_E:
            rc = sqlite3_bind_int64(stmt, index, (sqlite3_int64)param->value.i64);
            break;
        case ENT_DB_PARAM_DOUBLE_E:
            rc = sqlite3_bind_double(stmt, index, param->value.d64);
            break;
        case ENT_DB_PARAM_NULL_E:
            rc = sqlite3_bind_null(stmt, index);
            break;
        case ENT_DB_PARAM_BLOB_E:
            if(param->value.blob.size > (size_t)INT_MAX)
            {
                IENT_LOG_ERROR("sqlite blob parameter is too large.\n");
                return ENT_DBS_BAD_PARAMS;
            }
            rc = sqlite3_bind_blob(stmt,
                                   index,
                                   param->value.blob.data,
                                   (int)param->value.blob.size,
                                   SQLITE_TRANSIENT);
            break;
        default:
            IENT_LOG_ERROR("sqlite parameter type is invalid.[%d]\n", param->type);
            return ENT_DBS_BAD_PARAMS;
    }

    if(rc != SQLITE_OK)
    {
        IENT_LOG_ERROR("sqlite bind failed:[%d]\n", rc);
        return ENT_DBS_BIND_FAILED;
    }

    return ENT_SYS_NORMAL;
}

static MSG_ID_T iENT_DbSqliteBindParams(sqlite3_stmt* stmt,
                                        const ENT_DB_PARAM* params,
                                        size_t paramCount)
{
    int expectedCount;
    size_t i;
    MSG_ID_T sts;

    expectedCount = sqlite3_bind_parameter_count(stmt);
    if(expectedCount < 0)
    {
        IENT_LOG_ERROR("sqlite parameter count failed.\n");
        return ENT_DBS_RESULT_FAILED;
    }

    if((size_t)expectedCount != paramCount)
    {
        IENT_LOG_ERROR("sqlite parameter count mismatch: expected[%d] got[%zu]\n",
                       expectedCount,
                       paramCount);
        return ENT_DBS_PARAM_COUNT;
    }

    if(paramCount > 0 && params == NULL)
    {
        IENT_LOG_ERROR("sqlite parameter array is null.\n");
        return ENT_DBS_BAD_PARAMS;
    }

    for(i = 0; i < paramCount; i++)
    {
        sts = iENT_DbSqliteBindParam(stmt, (int)i + 1, &params[i]);
        if(sts < 0)
        {
            return sts;
        }
    }

    return ENT_SYS_NORMAL;
}

static MSG_ID_T iENT_DbSqliteExecWrite(sqlite3* dbHandle,
                                       sqlite3_stmt* stmt,
                                       SqlResultCB userCb,
                                       void* userData)
{
    int rc;
    long long rows;

    rc = sqlite3_step(stmt);
    if(rc != SQLITE_DONE && rc != SQLITE_ROW)
    {
        IENT_LOG_ERROR("sqlite write step failed:[%d]->[%s]\n", rc, sqlite3_errmsg(dbHandle));
        return ENT_DBS_EXEC_FAILED;
    }

    rows = (long long)sqlite3_changes(dbHandle);
    if(userCb != NULL)
    {
        userCb(NULL, NULL, rows, 0, userData);
    }
    else
    {
        iENT_DbSqliteDefaultCb(NULL, NULL, rows, 0, userData);
    }

    return ENT_SYS_NORMAL;
}

static MSG_ID_T iENT_DbSqliteExecPrepared(sqlite3* dbHandle,
                                           const char* sql,
                                           const ENT_DB_PARAM* params,
                                           size_t paramCount,
                                           SqlResultCB userCb,
                                           void* userData,
                                           bool isWrite)
{
    MSG_ID_T sts = ENT_SYS_NORMAL;
    sqlite3_stmt* stmt = NULL;
    int rc;

    rc = sqlite3_prepare_v2(dbHandle, sql, -1, &stmt, NULL);
    if(rc != SQLITE_OK)
    {
        IENT_LOG_ERROR("sqlite prepare failed:[%d]->[%s]\n", rc, sqlite3_errmsg(dbHandle));
        return ENT_DBS_PREPARE_FAILED;
    }

    sts = iENT_DbSqliteBindParams(stmt, params, paramCount);
    if(sts < 0)
    {
        goto END_OF_ROUTINE;
    }

    if(isWrite || sqlite3_column_count(stmt) == 0)
    {
        sts = iENT_DbSqliteExecWrite(dbHandle, stmt, userCb, userData);
        goto END_OF_ROUTINE;
    }

    sts = iENT_DbSqliteCollectRows(dbHandle, stmt, userCb, userData);

END_OF_ROUTINE:
    sqlite3_finalize(stmt);
    return sts;
}

MSG_ID_T ENT_DbSqliteReadParams(sqlite3* dbHandle,
                                const char* sql,
                                const ENT_DB_PARAM* params,
                                size_t paramCount,
                                SqlResultCB userCb,
                                void* userData)
{
    if(dbHandle == NULL || sql == NULL || (userCb != NULL && userData == NULL))
    {
        IENT_LOG_ERROR("Arguments are invalid.\n");
        return ENT_DBS_BAD_ARGUMENT;
    }

    return iENT_DbSqliteExecPrepared(dbHandle, sql, params, paramCount, userCb, userData, false);
}

MSG_ID_T ENT_DbSqliteWriteParams(sqlite3* dbHandle,
                                 const char* sql,
                                 const ENT_DB_PARAM* params,
                                 size_t paramCount,
                                 SqlResultCB userCb,
                                 void* userData)
{
    if(dbHandle == NULL || sql == NULL || (userCb != NULL && userData == NULL))
    {
        IENT_LOG_ERROR("Arguments are invalid.\n");
        return ENT_DBS_BAD_ARGUMENT;
    }

    return iENT_DbSqliteExecPrepared(dbHandle, sql, params, paramCount, userCb, userData, true);
}

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
