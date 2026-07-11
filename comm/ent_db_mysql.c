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

#if ENT_ENABLE_MYSQL && ENT_MYSQL_FOUND
typedef struct MYSQL_PARAM_BIND_CTX
{
    MYSQL_BIND* binds;
    unsigned long* lengths;
    bool* isNull;
    int* int32Values;
    long long* int64Values;
    double* doubleValues;
} MYSQL_PARAM_BIND_CTX;

typedef struct MYSQL_RESULT_BIND_CTX
{
    MYSQL_BIND* binds;
    unsigned long* lengths;
    bool* isNull;
    char** buffers;
    size_t* bufferSizes;
} MYSQL_RESULT_BIND_CTX;

static void iENT_DbMySQLFreeParamBindCtx(MYSQL_PARAM_BIND_CTX* ctx)
{
    if(ctx == NULL)
    {
        return;
    }

    free(ctx->binds);
    free(ctx->lengths);
    free(ctx->isNull);
    free(ctx->int32Values);
    free(ctx->int64Values);
    free(ctx->doubleValues);
    memset(ctx, 0, sizeof(*ctx));
}

static void iENT_DbMySQLFreeResultBindCtx(MYSQL_RESULT_BIND_CTX* ctx, size_t fieldCount)
{
    size_t i;

    if(ctx == NULL)
    {
        return;
    }

    if(ctx->buffers != NULL)
    {
        for(i = 0; i < fieldCount; i++)
        {
            free(ctx->buffers[i]);
        }
    }

    free(ctx->binds);
    free(ctx->lengths);
    free(ctx->isNull);
    free(ctx->buffers);
    free(ctx->bufferSizes);
    memset(ctx, 0, sizeof(*ctx));
}

static MSG_ID_T iENT_DbMySQLBindParam(const ENT_DB_PARAM* param,
                                      MYSQL_BIND* bind,
                                      unsigned long* length,
                                      bool* isNull,
                                      int* int32Value,
                                      long long* int64Value,
                                      double* doubleValue)
{
    if(param == NULL || bind == NULL || length == NULL || isNull == NULL)
    {
        IENT_LOG_ERROR("mysql parameter binding arguments are invalid.\n");
        return ENT_DBS_BAD_ARGUMENT;
    }

    memset(bind, 0, sizeof(*bind));
    *length = 0;
    *isNull = 0;

    switch(param->type)
    {
        case ENT_DB_PARAM_TEXT_E:
            if(param->value.text == NULL)
            {
                IENT_LOG_ERROR("mysql text parameter is null.\n");
                return ENT_DBS_BAD_PARAMS;
            }
            bind->buffer_type = MYSQL_TYPE_STRING;
            bind->buffer = (void*)param->value.text;
            *length = (unsigned long)strlen(param->value.text);
            bind->buffer_length = *length;
            bind->length = length;
            break;

        case ENT_DB_PARAM_INT_E:
            if(int32Value == NULL)
            {
                IENT_LOG_ERROR("mysql int parameter storage is null.\n");
                return ENT_DBS_BAD_ARGUMENT;
            }
            *int32Value = param->value.i32;
            bind->buffer_type = MYSQL_TYPE_LONG;
            bind->buffer = (void*)int32Value;
            bind->is_unsigned = 0;
            break;

        case ENT_DB_PARAM_INT64_E:
            if(int64Value == NULL)
            {
                IENT_LOG_ERROR("mysql int64 parameter storage is null.\n");
                return ENT_DBS_BAD_ARGUMENT;
            }
            *int64Value = param->value.i64;
            bind->buffer_type = MYSQL_TYPE_LONGLONG;
            bind->buffer = (void*)int64Value;
            bind->is_unsigned = 0;
            break;

        case ENT_DB_PARAM_DOUBLE_E:
            if(doubleValue == NULL)
            {
                IENT_LOG_ERROR("mysql double parameter storage is null.\n");
                return ENT_DBS_BAD_ARGUMENT;
            }
            *doubleValue = param->value.d64;
            bind->buffer_type = MYSQL_TYPE_DOUBLE;
            bind->buffer = (void*)doubleValue;
            break;

        case ENT_DB_PARAM_NULL_E:
            *isNull = 1;
            bind->buffer_type = MYSQL_TYPE_NULL;
            bind->is_null = isNull;
            break;

        case ENT_DB_PARAM_BLOB_E:
            if(param->value.blob.size > (size_t)ULONG_MAX)
            {
                IENT_LOG_ERROR("mysql blob parameter is too large.\n");
                return ENT_DBS_BAD_PARAMS;
            }
            if(param->value.blob.size > 0 && param->value.blob.data == NULL)
            {
                IENT_LOG_ERROR("mysql blob parameter data is null.\n");
                return ENT_DBS_BAD_PARAMS;
            }
            bind->buffer_type = MYSQL_TYPE_BLOB;
            bind->buffer = (void*)param->value.blob.data;
            *length = (unsigned long)param->value.blob.size;
            bind->buffer_length = *length;
            bind->length = length;
            break;

        default:
            IENT_LOG_ERROR("mysql parameter type is invalid.[%d]\n", param->type);
            return ENT_DBS_BAD_PARAMS;
    }

    return ENT_SYS_NORMAL;
}

static MSG_ID_T iENT_DbMySQLBindParams(const ENT_DB_PARAM* params,
                                       size_t paramCount,
                                       MYSQL_STMT* stmt,
                                       MYSQL_PARAM_BIND_CTX* bindCtx)
{
    size_t i;
    MSG_ID_T sts;

    if(stmt == NULL || bindCtx == NULL)
    {
        IENT_LOG_ERROR("mysql parameter binding context is invalid.\n");
        return ENT_DBS_BAD_ARGUMENT;
    }

    memset(bindCtx, 0, sizeof(*bindCtx));

    if(paramCount == 0)
    {
        return ENT_SYS_NORMAL;
    }

    if(params == NULL)
    {
        IENT_LOG_ERROR("mysql parameter array is null.\n");
        return ENT_DBS_BAD_PARAMS;
    }

    if(paramCount > (size_t)ULONG_MAX)
    {
        IENT_LOG_ERROR("mysql parameter count is too large.\n");
        return ENT_DBS_BAD_PARAMS;
    }

    bindCtx->binds = (MYSQL_BIND*)calloc(paramCount, sizeof(MYSQL_BIND));
    bindCtx->lengths = (unsigned long*)calloc(paramCount, sizeof(unsigned long));
    bindCtx->isNull = (bool*)calloc(paramCount, sizeof(bool));
    bindCtx->int32Values = (int*)calloc(paramCount, sizeof(int));
    bindCtx->int64Values = (long long*)calloc(paramCount, sizeof(long long));
    bindCtx->doubleValues = (double*)calloc(paramCount, sizeof(double));
    if(bindCtx->binds == NULL || bindCtx->lengths == NULL || bindCtx->isNull == NULL ||
       bindCtx->int32Values == NULL || bindCtx->int64Values == NULL ||
       bindCtx->doubleValues == NULL)
    {
        IENT_LOG_ERROR("mysql parameter array allocation failed.\n");
        iENT_DbMySQLFreeParamBindCtx(bindCtx);
        return ENT_DBS_ALLOC_FAILED;
    }

    for(i = 0; i < paramCount; i++)
    {
        sts = iENT_DbMySQLBindParam(&params[i],
                                    &bindCtx->binds[i],
                                    &bindCtx->lengths[i],
                                    &bindCtx->isNull[i],
                                    &bindCtx->int32Values[i],
                                    &bindCtx->int64Values[i],
                                    &bindCtx->doubleValues[i]);
        if(sts < 0)
        {
            iENT_DbMySQLFreeParamBindCtx(bindCtx);
            return sts;
        }
    }

    if(mysql_stmt_bind_param(stmt, bindCtx->binds))
    {
        IENT_LOG_ERROR("mysql_stmt_bind_param failed:[%s]\n", mysql_stmt_error(stmt));
        iENT_DbMySQLFreeParamBindCtx(bindCtx);
        return ENT_DBS_BIND_FAILED;
    }

    return ENT_SYS_NORMAL;
}

static size_t iENT_DbMySQLResultBufferSize(const MYSQL_FIELD* field)
{
    size_t size;

    if(field == NULL)
    {
        return 1;
    }

    size = (size_t)field->max_length + 1;
    if(size == 1)
    {
        size = (size_t)field->length + 1;
    }
    if(size == 0)
    {
        size = 1;
    }

    return size;
}

static MSG_ID_T iENT_DbMySQLBindResults(MYSQL_STMT* stmt,
                                        MYSQL_RES* meta,
                                        MYSQL_RESULT_BIND_CTX* resultCtx)
{
    MYSQL_FIELD* fields;
    size_t i;
    unsigned int fieldCount;

    if(stmt == NULL || meta == NULL || resultCtx == NULL)
    {
        IENT_LOG_ERROR("mysql result binding arguments are invalid.\n");
        return ENT_DBS_BAD_ARGUMENT;
    }

    memset(resultCtx, 0, sizeof(*resultCtx));

    fieldCount = mysql_num_fields(meta);
    if(fieldCount == 0)
    {
        return ENT_SYS_NORMAL;
    }

    resultCtx->binds = (MYSQL_BIND*)calloc(fieldCount, sizeof(MYSQL_BIND));
    resultCtx->lengths = (unsigned long*)calloc(fieldCount, sizeof(unsigned long));
    resultCtx->isNull = (bool*)calloc(fieldCount, sizeof(bool));
    resultCtx->buffers = (char**)calloc(fieldCount, sizeof(char*));
    resultCtx->bufferSizes = (size_t*)calloc(fieldCount, sizeof(size_t));
    if(resultCtx->binds == NULL || resultCtx->lengths == NULL ||
       resultCtx->isNull == NULL || resultCtx->buffers == NULL ||
       resultCtx->bufferSizes == NULL)
    {
        IENT_LOG_ERROR("mysql result array allocation failed.\n");
        iENT_DbMySQLFreeResultBindCtx(resultCtx, fieldCount);
        return ENT_DBS_ALLOC_FAILED;
    }

    fields = mysql_fetch_fields(meta);
    for(i = 0; i < fieldCount; i++)
    {
        size_t bufferSize = iENT_DbMySQLResultBufferSize(&fields[i]);

        resultCtx->bufferSizes[i] = bufferSize;
        resultCtx->buffers[i] = (char*)calloc(bufferSize, sizeof(char));
        if(resultCtx->buffers[i] == NULL)
        {
            IENT_LOG_ERROR("mysql result buffer allocation failed.\n");
            iENT_DbMySQLFreeResultBindCtx(resultCtx, fieldCount);
            return ENT_DBS_ALLOC_FAILED;
        }

        memset(&resultCtx->binds[i], 0, sizeof(MYSQL_BIND));
        resultCtx->binds[i].buffer_type = MYSQL_TYPE_STRING;
        resultCtx->binds[i].buffer = resultCtx->buffers[i];
        resultCtx->binds[i].buffer_length = (unsigned long)bufferSize;
        resultCtx->binds[i].length = &resultCtx->lengths[i];
        resultCtx->binds[i].is_null = &resultCtx->isNull[i];
    }

    if(mysql_stmt_bind_result(stmt, resultCtx->binds))
    {
        IENT_LOG_ERROR("mysql_stmt_bind_result failed:[%s]\n", mysql_stmt_error(stmt));
        iENT_DbMySQLFreeResultBindCtx(resultCtx, fieldCount);
        return ENT_DBS_BIND_FAILED;
    }

    return ENT_SYS_NORMAL;
}

static MSG_ID_T iENT_DbMySQLCollectRows(MYSQL_STMT* stmt,
                                        MYSQL_RES* meta,
                                        SqlResultCB userCb,
                                        void* userData)
{
    MSG_ID_T sts = ENT_SYS_NORMAL;
    MYSQL_FIELD* fieldsMeta;
    MYSQL_RESULT_BIND_CTX resultCtx;
    char** fields = NULL;
    char** rows = NULL;
    unsigned int fieldCount;
    unsigned long long rowCount;
    unsigned long long rowIdx = 0;
    size_t cellCount = 0;
    int fetchRc;
    size_t i;

    memset(&resultCtx, 0, sizeof(resultCtx));

    fieldCount = mysql_num_fields(meta);
    rowCount = mysql_stmt_num_rows(stmt);

    if(fieldCount > 0)
    {
        fields = (char**)calloc(fieldCount, sizeof(char*));
        if(fields == NULL)
        {
            IENT_LOG_ERROR("mysql fields array allocation failed.\n");
            return ENT_DBS_ALLOC_FAILED;
        }

        fieldsMeta = mysql_fetch_fields(meta);
        for(i = 0; i < fieldCount; i++)
        {
            fields[i] = fieldsMeta[i].name;
        }
    }

    if(fieldCount > 0)
    {
        size_t allocationRows = (size_t)(rowCount > 0 ? rowCount : 1);
        if((unsigned long long)allocationRows != (rowCount > 0 ? rowCount : 1) ||
           !iENT_DbCheckedSizeMultiply(allocationRows, (size_t)fieldCount, &cellCount))
        {
            free(fields);
            return ENT_DBS_ALLOC_FAILED;
        }
        rows = (char**)calloc(cellCount, sizeof(char*));
        if(rows == NULL)
        {
            free(fields);
            IENT_LOG_ERROR("mysql rows array allocation failed.\n");
            return ENT_DBS_ALLOC_FAILED;
        }
    }

    sts = iENT_DbMySQLBindResults(stmt, meta, &resultCtx);
    if(sts < 0)
    {
        free(fields);
        free(rows);
        return sts;
    }

    while((fetchRc = mysql_stmt_fetch(stmt)) != MYSQL_NO_DATA)
    {
        if(fetchRc != 0 && fetchRc != MYSQL_DATA_TRUNCATED)
        {
            IENT_LOG_ERROR("mysql_stmt_fetch failed:[%d]->[%s]\n", fetchRc, mysql_stmt_error(stmt));
            sts = ENT_DBS_RESULT_FAILED;
            goto END_OF_ROUTINE;
        }

        if(rowCount > 0 && rows != NULL)
        {
            for(i = 0; i < fieldCount; i++)
            {
                size_t cellIdx = (size_t)rowIdx * fieldCount + i;

                if(resultCtx.isNull[i])
                {
                    rows[cellIdx] = NULL;
                }
                else
                {
                    rows[cellIdx] = ENT_StrDup(resultCtx.buffers[i]);
                    if(rows[cellIdx] == NULL)
                    {
                        IENT_LOG_ERROR("mysql row cell allocation failed.\n");
                        sts = ENT_DBS_ALLOC_FAILED;
                        goto END_OF_ROUTINE;
                    }
                }
            }
            rowIdx++;
        }
    }

    if(userCb != NULL)
    {
        userCb(fields, rows, (long long)rowCount, (int)fieldCount, userData);
    }
    else
    {
        defSqlResultCb(fields, rows, (long long)rowCount, (int)fieldCount, userData);
    }

END_OF_ROUTINE:
    if(rows != NULL)
    {
        for(i = 0; i < cellCount; i++)
        {
            free(rows[i]);
        }
    }
    free(rows);
    free(fields);
    iENT_DbMySQLFreeResultBindCtx(&resultCtx, fieldCount);
    return sts;
}

static MSG_ID_T iENT_DbMySQLExecPrepared(MYSQL* dbHandle,
                                         const char* query,
                                         const ENT_DB_PARAM* params,
                                         size_t paramCount,
                                         SqlResultCB userCb,
                                         void* userData,
                                         bool isWrite)
{
    MSG_ID_T sts;
    MYSQL_STMT* stmt = NULL;
    MYSQL_RES* meta = NULL;
    MYSQL_PARAM_BIND_CTX bindCtx;
    unsigned long expectedCount;

    memset(&bindCtx, 0, sizeof(bindCtx));

    stmt = mysql_stmt_init(dbHandle);
    if(stmt == NULL)
    {
        IENT_LOG_ERROR("mysql_stmt_init failed:[%s]\n", mysql_error(dbHandle));
        return ENT_DBS_ALLOC_FAILED;
    }

    if(mysql_stmt_prepare(stmt, query, (unsigned long)strlen(query)))
    {
        IENT_LOG_ERROR("mysql_stmt_prepare failed:[%s]\n", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return ENT_DBS_PREPARE_FAILED;
    }

    expectedCount = mysql_stmt_param_count(stmt);
    if(expectedCount != (unsigned long)paramCount)
    {
        IENT_LOG_ERROR("mysql parameter count mismatch: expected[%lu] got[%zu]\n",
                       expectedCount,
                       paramCount);
        mysql_stmt_close(stmt);
        return ENT_DBS_PARAM_COUNT;
    }

    sts = iENT_DbMySQLBindParams(params, paramCount, stmt, &bindCtx);
    if(sts < 0)
    {
        mysql_stmt_close(stmt);
        return sts;
    }

    if(mysql_stmt_execute(stmt))
    {
        IENT_LOG_ERROR("mysql_stmt_execute failed:[%s]\n", mysql_stmt_error(stmt));
        iENT_DbMySQLFreeParamBindCtx(&bindCtx);
        mysql_stmt_close(stmt);
        return ENT_DBS_EXEC_FAILED;
    }

    if(isWrite)
    {
        long long rows = (long long)mysql_stmt_affected_rows(stmt);

        if(userCb != NULL)
        {
            userCb(NULL, NULL, rows, 0, userData);
        }
        else
        {
            defSqlResultCb(NULL, NULL, rows, 0, userData);
        }

        iENT_DbMySQLFreeParamBindCtx(&bindCtx);
        mysql_stmt_close(stmt);
        return ENT_SYS_NORMAL;
    }

    meta = mysql_stmt_result_metadata(stmt);
    if(meta == NULL)
    {
        IENT_LOG_ERROR("mysql_stmt_result_metadata failed:[%s]\n", mysql_stmt_error(stmt));
        iENT_DbMySQLFreeParamBindCtx(&bindCtx);
        mysql_stmt_close(stmt);
        return ENT_DBS_RESULT_FAILED;
    }

    {
        bool updateMaxLength = true;
        if(mysql_stmt_attr_set(stmt, STMT_ATTR_UPDATE_MAX_LENGTH, &updateMaxLength))
        {
            IENT_LOG_WARN("mysql_stmt_attr_set failed:[%s]\n", mysql_stmt_error(stmt));
        }
    }

    if(mysql_stmt_store_result(stmt))
    {
        IENT_LOG_ERROR("mysql_stmt_store_result failed:[%s]\n", mysql_stmt_error(stmt));
        mysql_free_result(meta);
        iENT_DbMySQLFreeParamBindCtx(&bindCtx);
        mysql_stmt_close(stmt);
        return ENT_DBS_RESULT_FAILED;
    }

    sts = iENT_DbMySQLCollectRows(stmt, meta, userCb, userData);
    mysql_free_result(meta);
    iENT_DbMySQLFreeParamBindCtx(&bindCtx);
    mysql_stmt_close(stmt);
    return sts;
}
#endif

MSG_ID_T ENT_DbMySQLInit(DB_HANDLE dbHandle)
{
#if ENT_ENABLE_MYSQL && ENT_MYSQL_FOUND
    MYSQL* db;

    DB_CFG* dbCfg = (DB_CFG*)dbHandle;
    if(dbHandle == NULL)
    {
        IENT_LOG_ERROR("Arguments are invalid.\n");
        return ENT_DBS_BAD_ARGUMENT;
    }

    if(dbCfg->sTag!=ENTDB_S_TAG ||
      dbCfg->eTag!=ENTDB_E_TAG ||
      dbCfg->dbType != MYSQL_TYPE)
    {
        IENT_LOG_ERROR("Arguments are invalid.\n");
        return ENT_DBS_BAD_HANDLE;
    }

    db = mysql_init(NULL);
    if(db==NULL)
    {
        IENT_LOG_ERROR("mysql_init failed.\n");
        return ENT_DBS_OPEN_FAILED;
    }
    if (mysql_options(db, MYSQL_SET_CHARSET_NAME, "utf8"))
    {
        IENT_LOG_WARN("mysql_options failed,message:[%s]\n",mysql_error(db));
    }
    {
        bool reconnect = 1;
        if(mysql_options(db, MYSQL_OPT_RECONNECT, &reconnect))
        {
            IENT_LOG_WARN("mysql_options failed,message:[%s]\n",mysql_error(db));
        }
    }

    if (mysql_real_connect(db,
                   dbCfg->host,
                   dbCfg->userName,
                   dbCfg->passwd,
                   dbCfg->database, 0, NULL, CLIENT_REMEMBER_OPTIONS) == NULL)
    {
        IENT_LOG_ERROR("connect failed,message:[%s]\n",mysql_error(db));
        mysql_close(db);
        return ENT_DBS_OPEN_FAILED;
    }

    dbCfg->dbInstance.mysql = db;
    dbCfg->isOpen = true;
    return ENT_SYS_NORMAL;
#else
    (void)dbHandle;
    return iENT_DbBackendUnsupported(MYSQL_TYPE);
#endif
}

#if ENT_ENABLE_MYSQL && ENT_MYSQL_FOUND
MSG_ID_T ENT_DbMySQLRead(MYSQL* dbHandle,const char* query,SqlResultCB userCb,void* userData)
{
    MSG_ID_T sts = ENT_SYS_NORMAL;
    if(query==NULL || dbHandle==NULL)
    {
        IENT_LOG_ERROR("arguments is invalid\n");
        return ENT_DBS_BAD_ARGUMENT;
    }

    if (mysql_query(dbHandle, query))
    {
        IENT_LOG_ERROR("mysql_query [%s],message:[%s]\n",query,mysql_error(dbHandle));
        return ENT_DBS_QUERY_FAILED;
    }

    MYSQL_RES *result = mysql_store_result(dbHandle);

    if (result == NULL)
    {
        IENT_LOG_ERROR("mysql_store_result failed,message:[%s]\n",mysql_error(dbHandle));
        return ENT_DBS_RESULT_FAILED;
    }

    unsigned int num_fields_raw = mysql_num_fields(result);
    unsigned long long num_rows_raw = mysql_num_rows(result);
    int       num_fields;
    long long num_rows;
    size_t    allocation_rows;
    size_t    cell_count = 0;

    if(num_fields_raw > (unsigned int)INT_MAX || num_rows_raw > (unsigned long long)LLONG_MAX)
    {
        mysql_free_result(result);
        return ENT_DBS_RESULT_FAILED;
    }
    num_fields = (int)num_fields_raw;
    num_rows = (long long)num_rows_raw;
    allocation_rows = (size_t)(num_rows_raw > 0 ? num_rows_raw : 1);
    if((unsigned long long)allocation_rows != (num_rows_raw > 0 ? num_rows_raw : 1))
    {
        mysql_free_result(result);
        return ENT_DBS_ALLOC_FAILED;
    }

    char** fields = (char**)calloc((size_t)(num_fields > 0 ? num_fields : 1), sizeof(char*));
    if (fields == NULL)
    {
        mysql_free_result(result);
        return ENT_DBS_ALLOC_FAILED;
    }
    if(!iENT_DbCheckedSizeMultiply(allocation_rows,
                                   (size_t)(num_fields > 0 ? num_fields : 1),
                                   &cell_count))
    {
        free(fields);
        mysql_free_result(result);
        return ENT_DBS_ALLOC_FAILED;
    }
    char** rows = (char**)calloc(cell_count, sizeof(char*));
    if (rows == NULL)
    {
        free(fields);
        fields = NULL;
        mysql_free_result(result);
        return ENT_DBS_ALLOC_FAILED;
    }

    MYSQL_ROW    row;
    MYSQL_FIELD* field;
    long long    rowIdx = 0;

    {
        int colIdx = 0;
        while((field = mysql_fetch_field(result)))
        {
            fields[colIdx] = field->name;
            colIdx++;
        }
    }

    while ((row = mysql_fetch_row(result)))
    {
        int i;
        for(i = 0; i < num_fields; i++)
        {
            rows[(size_t)rowIdx * (size_t)num_fields + (size_t)i] = row[i];
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
    return ENT_SYS_NORMAL;
}

MSG_ID_T ENT_DbMySQLWrite(MYSQL* dbHandle,const char* query,SqlResultCB userCb,void* userData)
{
    MSG_ID_T  sts  = ENT_SYS_NORMAL;
    long long rows = 0;
    if(query==NULL || dbHandle==NULL)
    {
        IENT_LOG_ERROR("arguments is invalid\n");
        return ENT_DBS_BAD_ARGUMENT;
    }

    if (mysql_query(dbHandle, query))
    {
        IENT_LOG_ERROR("mysql_query failed [%s],message:[%s]\n",query,mysql_error(dbHandle));
        return ENT_DBS_QUERY_FAILED;
    }

    rows = mysql_affected_rows(dbHandle);
    if(rows==-1)
    {
        IENT_LOG_ERROR("rows failed [%s],message:[%s]\n",query,mysql_error(dbHandle));
        return ENT_DBS_RESULT_FAILED;
    }

    if(userCb!=NULL)
        userCb(NULL,NULL,rows,0,userData);
    else
        defSqlResultCb(NULL,NULL,rows,0,userData);

    return ENT_SYS_NORMAL;
}

MSG_ID_T ENT_DbMySQLReadParams(MYSQL* dbHandle,
                               const char* sql,
                               const ENT_DB_PARAM* params,
                               size_t paramCount,
                               SqlResultCB userCb,
                               void* userData)
{
    if(dbHandle == NULL || sql == NULL)
    {
        IENT_LOG_ERROR("Arguments are invalid.\n");
        return ENT_DBS_BAD_ARGUMENT;
    }

    return iENT_DbMySQLExecPrepared(dbHandle, sql, params, paramCount, userCb, userData, false);
}

MSG_ID_T ENT_DbMySQLWriteParams(MYSQL* dbHandle,
                                const char* sql,
                                const ENT_DB_PARAM* params,
                                size_t paramCount,
                                SqlResultCB userCb,
                                void* userData)
{
    if(dbHandle == NULL || sql == NULL)
    {
        IENT_LOG_ERROR("Arguments are invalid.\n");
        return ENT_DBS_BAD_ARGUMENT;
    }

    return iENT_DbMySQLExecPrepared(dbHandle, sql, params, paramCount, userCb, userData, true);
}
#endif

MSG_ID_T ENT_DbMySQLClose(DB_HANDLE dbHandle)
{
#if ENT_ENABLE_MYSQL && ENT_MYSQL_FOUND
    DB_CFG* dbCfg = (DB_CFG*)dbHandle;
    if(dbHandle == NULL)
    {
        IENT_LOG_ERROR("Arguments are invalid.\n");
        return ENT_DBS_BAD_ARGUMENT;
    }

    if(dbCfg->sTag!=ENTDB_S_TAG ||
      dbCfg->eTag!=ENTDB_E_TAG ||
      dbCfg->dbType != MYSQL_TYPE)
    {
        IENT_LOG_ERROR("Arguments are invalid.\n");
        return ENT_DBS_BAD_HANDLE;
    }

    if(dbCfg->dbInstance.mysql)
    {
        mysql_close(dbCfg->dbInstance.mysql);
        dbCfg->dbInstance.mysql = NULL;
    }
    dbCfg->isOpen = false;
    return ENT_SYS_NORMAL;
#else
    (void)dbHandle;
    return iENT_DbBackendUnsupported(MYSQL_TYPE);
#endif
}
