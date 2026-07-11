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

#if ENT_ENABLE_PGSQL && ENT_PGSQL_FOUND
static void iENT_DbPgSQLFreeBoundParams(char** paramValues,
                                        char** paramStorage,
                                        int* paramLengths,
                                        int* paramFormats,
                                        size_t paramCount)
{
    size_t i;

    if(paramStorage != NULL)
    {
        for(i = 0; i < paramCount; i++)
        {
            if(paramStorage[i] != NULL)
            {
                free(paramStorage[i]);
            }
        }
    }

    free(paramValues);
    free(paramStorage);
    free(paramLengths);
    free(paramFormats);
}

static size_t iENT_DbPgSQLCountDigits(size_t value)
{
    size_t digits = 1;

    while(value >= 10)
    {
        value /= 10;
        digits++;
    }

    return digits;
}

static MSG_ID_T iENT_DbPgSQLRewriteQuery(const char* query, size_t paramCount, char** rewrittenQuery)
{
    size_t i;
    size_t questionCount = 0;
    size_t dollarCount = 0;
    bool inSingleQuote = false;
    bool inDoubleQuote = false;

    if(rewrittenQuery == NULL)
    {
        IENT_LOG_ERROR("PgSQL query output argument is invalid.\n");
        return ENT_DBS_BAD_ARGUMENT;
    }

    *rewrittenQuery = NULL;

    if(query == NULL)
    {
        IENT_LOG_ERROR("PgSQL query is null.\n");
        return ENT_DBS_BAD_ARGUMENT;
    }

    for(i = 0; query[i] != '\0'; i++)
    {
        char ch = query[i];

        if(inSingleQuote)
        {
            if(ch == '\'')
            {
                if(query[i + 1] == '\'')
                {
                    i++;
                }
                else
                {
                    inSingleQuote = false;
                }
            }
            continue;
        }

        if(inDoubleQuote)
        {
            if(ch == '"')
            {
                if(query[i + 1] == '"')
                {
                    i++;
                }
                else
                {
                    inDoubleQuote = false;
                }
            }
            continue;
        }

        if(ch == '\'')
        {
            inSingleQuote = true;
            continue;
        }

        if(ch == '"')
        {
            inDoubleQuote = true;
            continue;
        }

        if(ch == '?')
        {
            questionCount++;
            continue;
        }

        if(ch == '$' && query[i + 1] >= '0' && query[i + 1] <= '9')
        {
            size_t index = 0;
            size_t j = i + 1;

            while(query[j] >= '0' && query[j] <= '9')
            {
                index = index * 10 + (size_t)(query[j] - '0');
                j++;
            }

            if(index > dollarCount)
            {
                dollarCount = index;
            }
            i = j - 1;
        }
    }

    if(questionCount > 0 && dollarCount > 0)
    {
        IENT_LOG_ERROR("PgSQL query mixes '?' and '$N' parameters.\n");
        return ENT_DBS_BAD_PARAMS;
    }

    if(questionCount > 0)
    {
        size_t outLen = 1;
        size_t paramIndex = 1;

        if(questionCount != paramCount)
        {
            IENT_LOG_ERROR("PgSQL parameter count mismatch: expected[%zu] got[%zu]\n",
                           questionCount,
                           paramCount);
            return ENT_DBS_PARAM_COUNT;
        }

        for(i = 0; query[i] != '\0'; i++)
        {
            char ch = query[i];

            if(ch == '\'')
            {
                outLen++;
                i++;
                while(query[i] != '\0')
                {
                    outLen++;
                    if(query[i] == '\'')
                    {
                        if(query[i + 1] == '\'')
                        {
                            outLen++;
                            i++;
                        }
                        else
                        {
                            break;
                        }
                    }
                    i++;
                }
                continue;
            }

            if(ch == '"')
            {
                outLen++;
                i++;
                while(query[i] != '\0')
                {
                    outLen++;
                    if(query[i] == '"')
                    {
                        if(query[i + 1] == '"')
                        {
                            outLen++;
                            i++;
                        }
                        else
                        {
                            break;
                        }
                    }
                    i++;
                }
                continue;
            }

            if(ch == '?')
            {
                outLen += 1 + iENT_DbPgSQLCountDigits(paramIndex);
                paramIndex++;
                continue;
            }

            outLen++;
        }

        *rewrittenQuery = (char*)malloc(outLen);
        if(*rewrittenQuery == NULL)
        {
            IENT_LOG_ERROR("PgSQL query rewrite allocation failed.\n");
            return ENT_DBS_ALLOC_FAILED;
        }

        {
            size_t outIdx = 0;
            paramIndex = 1;

            for(i = 0; query[i] != '\0'; i++)
            {
                char ch = query[i];

                if(ch == '\'')
                {
                    (*rewrittenQuery)[outIdx++] = ch;
                    i++;
                    while(query[i] != '\0')
                    {
                        (*rewrittenQuery)[outIdx++] = query[i];
                        if(query[i] == '\'')
                        {
                            if(query[i + 1] == '\'')
                            {
                                (*rewrittenQuery)[outIdx++] = query[i + 1];
                                i++;
                            }
                            else
                            {
                                break;
                            }
                        }
                        i++;
                    }
                    continue;
                }

                if(ch == '"')
                {
                    (*rewrittenQuery)[outIdx++] = ch;
                    i++;
                    while(query[i] != '\0')
                    {
                        (*rewrittenQuery)[outIdx++] = query[i];
                        if(query[i] == '"')
                        {
                            if(query[i + 1] == '"')
                            {
                                (*rewrittenQuery)[outIdx++] = query[i + 1];
                                i++;
                            }
                            else
                            {
                                break;
                            }
                        }
                        i++;
                    }
                    continue;
                }

                if(ch == '?')
                {
                    int written = snprintf(*rewrittenQuery + outIdx,
                                           outLen - outIdx,
                                           "$%zu",
                                           paramIndex++);
                    if(written < 0 || (size_t)written >= outLen - outIdx)
                    {
                        free(*rewrittenQuery);
                        *rewrittenQuery = NULL;
                        IENT_LOG_ERROR("PgSQL query rewrite failed.\n");
                        return ENT_DBS_PREPARE_FAILED;
                    }
                    outIdx += (size_t)written;
                    continue;
                }

                (*rewrittenQuery)[outIdx++] = ch;
            }

            (*rewrittenQuery)[outIdx] = '\0';
        }

        return ENT_SYS_NORMAL;
    }

    if(dollarCount > 0 && dollarCount != paramCount)
    {
        IENT_LOG_ERROR("PgSQL parameter count mismatch: expected[%zu] got[%zu]\n",
                       dollarCount,
                       paramCount);
        return ENT_DBS_PARAM_COUNT;
    }

    if(paramCount > 0 && dollarCount == 0)
    {
        IENT_LOG_ERROR("PgSQL query does not contain parameters.\n");
        return ENT_DBS_PARAM_COUNT;
    }

    *rewrittenQuery = ENT_StrDup(query);
    if(*rewrittenQuery == NULL)
    {
        IENT_LOG_ERROR("PgSQL query allocation failed.\n");
        return ENT_DBS_ALLOC_FAILED;
    }

    return ENT_SYS_NORMAL;
}

static MSG_ID_T iENT_DbPgSQLBindParam(const ENT_DB_PARAM* param,
                                      char** paramValue,
                                      char** paramStorage,
                                      int* paramLength,
                                      int* paramFormat)
{
    char* buffer;
    int written;

    if(param == NULL || paramValue == NULL || paramStorage == NULL ||
       paramLength == NULL || paramFormat == NULL)
    {
        IENT_LOG_ERROR("PgSQL parameter binding arguments are invalid.\n");
        return ENT_DBS_BAD_ARGUMENT;
    }

    *paramValue = NULL;
    *paramStorage = NULL;
    *paramLength = 0;
    *paramFormat = 0;

    switch(param->type)
    {
        case ENT_DB_PARAM_TEXT_E:
            if(param->value.text == NULL)
            {
                IENT_LOG_ERROR("PgSQL text parameter is null.\n");
                return ENT_DBS_BAD_PARAMS;
            }
            *paramValue = (char*)param->value.text;
            return ENT_SYS_NORMAL;

        case ENT_DB_PARAM_INT_E:
            buffer = (char*)malloc(32);
            if(buffer == NULL)
            {
                IENT_LOG_ERROR("PgSQL integer parameter allocation failed.\n");
                return ENT_DBS_ALLOC_FAILED;
            }
            written = snprintf(buffer, 32, "%d", param->value.i32);
            if(written < 0 || written >= 32)
            {
                free(buffer);
                IENT_LOG_ERROR("PgSQL integer parameter formatting failed.\n");
                return ENT_DBS_RESULT_FAILED;
            }
            *paramValue = buffer;
            *paramStorage = buffer;
            return ENT_SYS_NORMAL;

        case ENT_DB_PARAM_INT64_E:
            buffer = (char*)malloc(32);
            if(buffer == NULL)
            {
                IENT_LOG_ERROR("PgSQL int64 parameter allocation failed.\n");
                return ENT_DBS_ALLOC_FAILED;
            }
            written = snprintf(buffer, 32, "%lld", param->value.i64);
            if(written < 0 || written >= 32)
            {
                free(buffer);
                IENT_LOG_ERROR("PgSQL int64 parameter formatting failed.\n");
                return ENT_DBS_RESULT_FAILED;
            }
            *paramValue = buffer;
            *paramStorage = buffer;
            return ENT_SYS_NORMAL;

        case ENT_DB_PARAM_DOUBLE_E:
            buffer = (char*)malloc(64);
            if(buffer == NULL)
            {
                IENT_LOG_ERROR("PgSQL double parameter allocation failed.\n");
                return ENT_DBS_ALLOC_FAILED;
            }
            written = snprintf(buffer, 64, "%.17g", param->value.d64);
            if(written < 0 || written >= 64)
            {
                free(buffer);
                IENT_LOG_ERROR("PgSQL double parameter formatting failed.\n");
                return ENT_DBS_RESULT_FAILED;
            }
            *paramValue = buffer;
            *paramStorage = buffer;
            return ENT_SYS_NORMAL;

        case ENT_DB_PARAM_NULL_E:
            *paramValue = NULL;
            return ENT_SYS_NORMAL;

        case ENT_DB_PARAM_BLOB_E:
            if(param->value.blob.size > (size_t)INT_MAX)
            {
                IENT_LOG_ERROR("PgSQL blob parameter is too large.\n");
                return ENT_DBS_BAD_PARAMS;
            }
            if(param->value.blob.size > 0 && param->value.blob.data == NULL)
            {
                IENT_LOG_ERROR("PgSQL blob parameter data is null.\n");
                return ENT_DBS_BAD_PARAMS;
            }
            *paramValue = (char*)param->value.blob.data;
            *paramLength = (int)param->value.blob.size;
            *paramFormat = 1;
            return ENT_SYS_NORMAL;

        default:
            IENT_LOG_ERROR("PgSQL parameter type is invalid.[%d]\n", param->type);
            return ENT_DBS_BAD_PARAMS;
    }
}

static MSG_ID_T iENT_DbPgSQLBindParams(const ENT_DB_PARAM* params,
                                       size_t paramCount,
                                       char*** paramValuesOut,
                                       char*** paramStorageOut,
                                       int** paramLengthsOut,
                                       int** paramFormatsOut)
{
    size_t i;
    MSG_ID_T sts;
    char** paramValues;
    char** paramStorage;
    int* paramLengths;
    int* paramFormats;

    if(paramValuesOut == NULL || paramStorageOut == NULL ||
       paramLengthsOut == NULL || paramFormatsOut == NULL)
    {
        IENT_LOG_ERROR("PgSQL parameter output arguments are invalid.\n");
        return ENT_DBS_BAD_ARGUMENT;
    }

    *paramValuesOut = NULL;
    *paramStorageOut = NULL;
    *paramLengthsOut = NULL;
    *paramFormatsOut = NULL;

    if(paramCount == 0)
    {
        return ENT_SYS_NORMAL;
    }

    if(params == NULL)
    {
        IENT_LOG_ERROR("PgSQL parameter array is null.\n");
        return ENT_DBS_BAD_PARAMS;
    }

    if(paramCount > (size_t)INT_MAX)
    {
        IENT_LOG_ERROR("PgSQL parameter count is too large.\n");
        return ENT_DBS_BAD_PARAMS;
    }

    paramValues = (char**)calloc(paramCount, sizeof(char*));
    paramStorage = (char**)calloc(paramCount, sizeof(char*));
    paramLengths = (int*)calloc(paramCount, sizeof(int));
    paramFormats = (int*)calloc(paramCount, sizeof(int));
    if(paramValues == NULL || paramStorage == NULL ||
       paramLengths == NULL || paramFormats == NULL)
    {
        IENT_LOG_ERROR("PgSQL parameter array allocation failed.\n");
        iENT_DbPgSQLFreeBoundParams(paramValues, paramStorage, paramLengths, paramFormats, paramCount);
        return ENT_DBS_ALLOC_FAILED;
    }

    for(i = 0; i < paramCount; i++)
    {
        sts = iENT_DbPgSQLBindParam(&params[i],
                                    &paramValues[i],
                                    &paramStorage[i],
                                    &paramLengths[i],
                                    &paramFormats[i]);
        if(sts < 0)
        {
            iENT_DbPgSQLFreeBoundParams(paramValues, paramStorage, paramLengths, paramFormats, paramCount);
            return sts;
        }
    }

    *paramValuesOut = paramValues;
    *paramStorageOut = paramStorage;
    *paramLengthsOut = paramLengths;
    *paramFormatsOut = paramFormats;
    return ENT_SYS_NORMAL;
}

static MSG_ID_T iENT_DbPgSQLCollectRows(PGconn* pgConn, PGresult* res, SqlResultCB userCb, void* userData)
{
    int rows;
    int cols;
    size_t fieldsCount;
    size_t rowCount;
    char** fields;
    char** rowRes;

    rows = PQntuples(res);
    cols = PQnfields(res);
    fieldsCount = (size_t)(cols > 0 ? cols : 1);
    if(rows > 0 && cols > 0)
    {
        if(!iENT_DbCheckedSizeMultiply((size_t)rows, (size_t)cols, &rowCount))
        {
            return ENT_DBS_ALLOC_FAILED;
        }
        if(rowCount > ENT_DB_MAX_RESULT_CELLS)
        {
            IENT_LOG_ERROR("PgSQL result exceeds the materialization limit.\n");
            return ENT_DBS_ALLOC_FAILED;
        }
    }
    else
    {
        rowCount = 1;
    }
    fields = (char**)calloc(fieldsCount, sizeof(char*));
    rowRes = (char**)calloc(rowCount, sizeof(char*));

    if(fields == NULL || rowRes == NULL)
    {
        IENT_LOG_ERROR("PgSQL read failed: out of memory.\n");
        if(fields) free(fields);
        if(rowRes) free(rowRes);
        return ENT_DBS_ALLOC_FAILED;
    }

    {
        int i;
        for(i = 0; i < cols; i++)
        {
            fields[i] = (char*)PQfname(res, i);
        }
    }

    {
        int r;
        for(r = 0; r < rows; r++)
        {
            int c;
            for(c = 0; c < cols; c++)
            {
                rowRes[(size_t)r * (size_t)cols + (size_t)c] = (char*)PQgetvalue(res, r, c);
            }
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
    (void)pgConn;
    return ENT_SYS_NORMAL;
}

static MSG_ID_T iENT_DbPgSQLExecPrepared(PGconn* pgConn,
                                         const char* query,
                                         const ENT_DB_PARAM* params,
                                         size_t paramCount,
                                         SqlResultCB userCb,
                                         void* userData,
                                         bool isWrite)
{
    MSG_ID_T sts;
    PGresult* res;
    ExecStatusType status;
    char* rewrittenQuery = NULL;
    char** paramValues = NULL;
    char** paramStorage = NULL;
    int* paramLengths = NULL;
    int* paramFormats = NULL;
    long long affectedRows = 0;

    sts = iENT_DbPgSQLRewriteQuery(query, paramCount, &rewrittenQuery);
    if(sts < 0)
    {
        return sts;
    }

    sts = iENT_DbPgSQLBindParams(params,
                                 paramCount,
                                 &paramValues,
                                 &paramStorage,
                                 &paramLengths,
                                 &paramFormats);
    if(sts < 0)
    {
        free(rewrittenQuery);
        return sts;
    }

    res = PQexecParams(pgConn,
                       rewrittenQuery,
                       (int)paramCount,
                       NULL,
                       (const char* const*)paramValues,
                       paramLengths,
                       paramFormats,
                       0);
    iENT_DbPgSQLFreeBoundParams(paramValues, paramStorage, paramLengths, paramFormats, paramCount);
    free(rewrittenQuery);
    if(res == NULL)
    {
        IENT_LOG_ERROR("PgSQL prepared execution failed: execution returned no result.\n");
        return ENT_DBS_EXEC_FAILED;
    }

    status = PQresultStatus(res);
    if(isWrite)
    {
        if(status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK)
        {
            IENT_LOG_ERROR("PgSQL prepared write failed: %s\n", PQerrorMessage(pgConn));
            PQclear(res);
            return ENT_DBS_RESULT_FAILED;
        }

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
    }

    if(status != PGRES_TUPLES_OK)
    {
        IENT_LOG_ERROR("PgSQL prepared read failed: %s\n", PQerrorMessage(pgConn));
        PQclear(res);
        return ENT_DBS_RESULT_FAILED;
    }

    sts = iENT_DbPgSQLCollectRows(pgConn, res, userCb, userData);
    PQclear(res);
    return sts;
}

MSG_ID_T ENT_DbPgSQLReadParams(void* dbHandle,
                               const char* sql,
                               const ENT_DB_PARAM* params,
                               size_t paramCount,
                               SqlResultCB userCb,
                               void* userData)
{
    PGconn* pgConn = (PGconn*)dbHandle;

    if(dbHandle == NULL || sql == NULL)
    {
        IENT_LOG_ERROR("Arguments are invalid.\n");
        return ENT_DBS_BAD_ARGUMENT;
    }

    return iENT_DbPgSQLExecPrepared(pgConn, sql, params, paramCount, userCb, userData, false);
}

MSG_ID_T ENT_DbPgSQLWriteParams(void* dbHandle,
                                const char* sql,
                                const ENT_DB_PARAM* params,
                                size_t paramCount,
                                SqlResultCB userCb,
                                void* userData)
{
    PGconn* pgConn = (PGconn*)dbHandle;

    if(dbHandle == NULL || sql == NULL)
    {
        IENT_LOG_ERROR("Arguments are invalid.\n");
        return ENT_DBS_BAD_ARGUMENT;
    }

    return iENT_DbPgSQLExecPrepared(pgConn, sql, params, paramCount, userCb, userData, true);
}
#endif

MSG_ID_T ENT_DbPgSQLInit(DB_HANDLE dbHandle)
{
#if ENT_ENABLE_PGSQL && ENT_PGSQL_FOUND
    DB_CFG* dbCfg = (DB_CFG*)dbHandle;
    PGconn* pgConn;
    char port[16];
    const char* keywords[] = {"host", "port", "dbname", "user", "password", NULL};
    const char* values[6];
    int written;

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

    written = snprintf(port, sizeof(port), "%d", dbCfg->portNo ? dbCfg->portNo : 5432);
    if(written < 0 || (size_t)written >= sizeof(port))
    {
        IENT_LOG_ERROR("PgSQL port formatting failed.\n");
        return ENT_DBS_BAD_PARAMS;
    }

    values[0] = dbCfg->host ? dbCfg->host : "";
    values[1] = port;
    values[2] = dbCfg->database ? dbCfg->database : "";
    values[3] = dbCfg->userName ? dbCfg->userName : "";
    values[4] = dbCfg->passwd ? dbCfg->passwd : "";
    values[5] = NULL;

    pgConn = PQconnectdbParams(keywords, values, 0);
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
#if ENT_ENABLE_PGSQL && ENT_PGSQL_FOUND
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
    return ENT_SYS_NORMAL;
#else
    (void)dbHandle;
    return iENT_DbBackendUnsupported(PGSQL_TYPE);
#endif
}

MSG_ID_T ENT_DbPgSQLRead(void* dbHandle, const char* query, SqlResultCB userCb, void* userData)
{
#if ENT_ENABLE_PGSQL && ENT_PGSQL_FOUND
    PGconn* pgConn = (PGconn*)dbHandle;
    PGresult* res;

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

    if(iENT_DbPgSQLCollectRows(pgConn, res, userCb, userData) < 0)
    {
        PQclear(res);
        return ENT_DBS_ALLOC_FAILED;
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

MSG_ID_T ENT_DbPgSQLWrite(void* dbHandle, const char* query, SqlResultCB userCb, void* userData)
{
#if ENT_ENABLE_PGSQL && ENT_PGSQL_FOUND
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
