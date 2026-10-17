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

/* Mark only SQL-code characters. This scan is shared by parameter counting
 * and rewriting so the two passes cannot disagree about quoted/comment text.
 * PostgreSQL permits nested block comments and tagged dollar strings. */
static int iENT_DbPgSQLIdentChar(char ch)
{
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
           (ch >= '0' && ch <= '9') || ch == '_';
}

static int iENT_DbPgSQLCodeMask(const char* sql, size_t len, unsigned char* code)
{
    size_t i = 0;
    while(i < len)
    {
        size_t j;
        char ch = sql[i];
        if(ch == '-' && i + 1 < len && sql[i + 1] == '-')
        {
            i += 2;
            while(i < len && sql[i] != '\n') i++;
            continue;
        }
        if(ch == '/' && i + 1 < len && sql[i + 1] == '*')
        {
            size_t depth = 1;
            i += 2;
            while(i < len && depth != 0)
            {
                if(i + 1 < len && sql[i] == '/' && sql[i + 1] == '*')
                {
                    if(depth == SIZE_MAX) return 0;
                    depth++;
                    i += 2;
                }
                else if(i + 1 < len && sql[i] == '*' && sql[i + 1] == '/')
                {
                    depth--;
                    i += 2;
                }
                else i++;
            }
            if(depth != 0) return 0;
            continue;
        }
        if(ch == '\'' || ch == '"')
        {
            char quote = ch;
            /* E'...' treats backslash-escaped quotes as literal. Standard
             * strings use only doubled quotes; identifiers use "". */
            int escaped = (quote == '\'' && i > 0 &&
                          (sql[i - 1] == 'E' || sql[i - 1] == 'e') &&
                          (i == 1 || !iENT_DbPgSQLIdentChar(sql[i - 2])));
            int closed = 0;
            i++;
            while(i < len)
            {
                if(escaped && sql[i] == '\\' && i + 1 < len)
                {
                    i += 2;
                }
                else if(sql[i] == quote)
                {
                    if(i + 1 < len && sql[i + 1] == quote) i += 2;
                    else { i++; closed = 1; break; }
                }
                else i++;
            }
            if(!closed) return 0;
            continue;
        }
        if(ch == '$' && (i == 0 || !iENT_DbPgSQLIdentChar(sql[i - 1])))
        {
            size_t delimEnd = i + 1;
            size_t delimLen;
            if(delimEnd < len && sql[delimEnd] != '$' &&
               !((sql[delimEnd] >= 'a' && sql[delimEnd] <= 'z') ||
                 (sql[delimEnd] >= 'A' && sql[delimEnd] <= 'Z') ||
                 sql[delimEnd] == '_'))
                delimEnd = len;
            else
            {
                while(delimEnd < len && iENT_DbPgSQLIdentChar(sql[delimEnd]))
                    delimEnd++;
            }
            if(delimEnd < len && sql[delimEnd] == '$')
            {
                int closed = 0;
                delimLen = delimEnd - i + 1;
                j = delimEnd + 1;
                while(j + delimLen <= len)
                {
                    if(memcmp(sql + j, sql + i, delimLen) == 0)
                    {
                        i = j + delimLen;
                        closed = 1;
                        break;
                    }
                    j++;
                }
                if(!closed) return 0;
                continue;
            }
        }
        code[i] = 1;
        i++;
    }
    return 1;
}

static MSG_ID_T iENT_DbPgSQLRewriteQuery(const char* query, size_t paramCount,
                                          char** rewrittenQuery)
{
    size_t len, i, questionCount = 0, dollarCount = 0, outLen, outIdx;
    size_t paramIndex = 1;
    unsigned char* code = NULL;
    MSG_ID_T result = ENT_SYS_NORMAL;

    if(rewrittenQuery == NULL) return ENT_DBS_BAD_ARGUMENT;
    *rewrittenQuery = NULL;
    if(query == NULL) return ENT_DBS_BAD_ARGUMENT;
    if(paramCount > ENT_DB_MAX_PARAMS) return ENT_DBS_BAD_PARAMS;
    len = strlen(query);
    if(len == SIZE_MAX) return ENT_DBS_BAD_PARAMS;
    code = (unsigned char*)calloc(len + 1, 1);
    if(code == NULL) return ENT_DBS_ALLOC_FAILED;
    if(!iENT_DbPgSQLCodeMask(query, len, code))
    {
        result = ENT_DBS_BAD_PARAMS; /* Unterminated SQL lexical construct. */
        goto END_OF_ROUTINE;
    }

    for(i = 0; i < len; i++)
    {
        if(!code[i]) continue;
        if(query[i] == '?')
        {
            if(++questionCount > ENT_DB_MAX_PARAMS)
            {
                result = ENT_DBS_BAD_PARAMS;
                goto END_OF_ROUTINE;
            }
        }
        if(query[i] == '$' && i + 1 < len &&
           query[i + 1] >= '0' && query[i + 1] <= '9')
        {
            size_t index = 0;
            size_t j = i + 1;
            while(j < len && query[j] >= '0' && query[j] <= '9')
            {
                size_t digit = (size_t)(query[j] - '0');
                if(index > (ENT_DB_MAX_PARAMS - digit) / 10)
                {
                    result = ENT_DBS_BAD_PARAMS;
                    goto END_OF_ROUTINE;
                }
                index = index * 10 + digit;
                j++;
            }
            if(index == 0)
            {
                result = ENT_DBS_BAD_PARAMS;
                goto END_OF_ROUTINE;
            }
            if(index > dollarCount) dollarCount = index;
            i = j - 1;
        }
    }
    if(questionCount > 0 && dollarCount > 0)
    {
        result = ENT_DBS_BAD_PARAMS;
        goto END_OF_ROUTINE;
    }
    if(questionCount != 0)
    {
        if(questionCount != paramCount)
        {
            result = ENT_DBS_PARAM_COUNT;
            goto END_OF_ROUTINE;
        }
        outLen = len + 1;
        for(i = 0; i < len; i++)
        {
            if(code[i] && query[i] == '?')
            {
                size_t digits = iENT_DbPgSQLCountDigits(paramIndex++);
                if(outLen > SIZE_MAX - digits)
                {
                    result = ENT_DBS_BAD_PARAMS;
                    goto END_OF_ROUTINE;
                }
                outLen += digits;
            }
        }
        *rewrittenQuery = (char*)malloc(outLen);
        if(*rewrittenQuery == NULL)
        {
            result = ENT_DBS_ALLOC_FAILED;
            goto END_OF_ROUTINE;
        }
        outIdx = 0;
        paramIndex = 1;
        for(i = 0; i < len; i++)
        {
            if(code[i] && query[i] == '?')
            {
                int n = snprintf(*rewrittenQuery + outIdx, outLen - outIdx,
                                 "$%zu", paramIndex++);
                if(n < 0 || (size_t)n >= outLen - outIdx)
                {
                    free(*rewrittenQuery);
                    *rewrittenQuery = NULL;
                    result = ENT_DBS_PREPARE_FAILED;
                    goto END_OF_ROUTINE;
                }
                outIdx += (size_t)n;
            }
            else (*rewrittenQuery)[outIdx++] = query[i];
        }
        (*rewrittenQuery)[outIdx] = '\0';
    }
    else
    {
        if((dollarCount > 0 && dollarCount != paramCount) ||
           (paramCount > 0 && dollarCount == 0))
        {
            result = ENT_DBS_PARAM_COUNT;
            goto END_OF_ROUTINE;
        }
        *rewrittenQuery = ENT_StrDup(query);
        if(*rewrittenQuery == NULL) result = ENT_DBS_ALLOC_FAILED;
    }

END_OF_ROUTINE:
    free(code);
    return result;
}

#ifdef ENT_DB_PGSQL_LEX_TEST_HOOKS
MSG_ID_T ENT_DbPgSQLRewriteForTest(const char* query, size_t count, char** out)
{
    return iENT_DbPgSQLRewriteQuery(query, count, out);
}
#endif

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
    size_t payloadBytes = 0;
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
                size_t cellBytes = 0;
                size_t newPayloadBytes = 0;
                if(!PQgetisnull(res, r, c) &&
                   ((size_t)PQgetlength(res, r, c) > ENT_DB_MAX_FIELD_BYTES ||
                    !iENT_DbCheckedSizeAdd((size_t)PQgetlength(res, r, c), 1, &cellBytes) ||
                    !iENT_DbCheckedSizeAdd(payloadBytes, cellBytes, &newPayloadBytes) ||
                    newPayloadBytes > ENT_DB_MAX_RESULT_BYTES))
                {
                    free(fields);
                    free(rowRes);
                    IENT_LOG_ERROR("PgSQL result exceeds the payload limit.\n");
                    return ENT_DBS_ALLOC_FAILED;
                }
                rowRes[(size_t)r * (size_t)cols + (size_t)c] = (char*)PQgetvalue(res, r, c);
                if(!PQgetisnull(res, r, c))
                {
                    payloadBytes = newPayloadBytes;
                }
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
