/* Read-only integration checks against disposable MySQL/PostgreSQL fixtures. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ent_db.h"
#include "ent_msg.h"

typedef struct RESULT_CHECK
{
    int calls;
    size_t expectedBytes;
    int badResult;
} RESULT_CHECK;

static void result(char** fields, char** rows, long long count, int columns, void* data)
{
    RESULT_CHECK* check = (RESULT_CHECK*)data;
    (void)fields;
    check->calls++;
    if(count != 1 || columns != 1 || rows == NULL || rows[0] == NULL ||
       strlen(rows[0]) != check->expectedBytes)
    {
        check->badResult = 1;
    }
}

static int backend(DB_TYPE type, const char* prefix)
{
    char name[64];
    char query[128];
    const char* host;
    const char* database;
    const char* user;
    const char* password;
    const char* portText;
    DB_HANDLE handle = NULL;
    RESULT_CHECK check = {0, ENT_DB_MAX_FIELD_BYTES, 0};
    int port;
    int failed = 0;
    MSG_ID_T status;
#define ENV(suffix) (snprintf(name, sizeof(name), "%s_%s", prefix, suffix), getenv(name))
    host = ENV("HOST"); database = ENV("DB"); user = ENV("USER");
    password = ENV("PASSWORD"); portText = ENV("PORT");
#undef ENV
    if(host == NULL || database == NULL || user == NULL)
    {
        fprintf(stderr, "%s fixture is not configured\n", prefix);
        return 77;
    }
    port = portText ? atoi(portText) : (type == MYSQL_TYPE ? 3306 : 5432);
    status = ENT_DbInitHandle(&handle, type, host, database, user, password, port);
    if(status < 0)
    {
        fprintf(stderr, "%s init failed: %d\n", prefix, (int)status);
        return 1;
    }
    snprintf(query, sizeof(query), "SELECT repeat('x', %u)", (unsigned int)ENT_DB_MAX_FIELD_BYTES);
    status = ENT_DbReadParams(handle, query, NULL, 0, result, &check);
    if(status < 0 || check.calls != 1 || check.badResult)
    {
        fprintf(stderr, "%s field boundary failed: %d\n", prefix, (int)status);
        failed = 1;
    }
    snprintf(query, sizeof(query), "SELECT repeat('x', %u)", (unsigned int)ENT_DB_MAX_FIELD_BYTES + 1);
    status = ENT_DbReadParams(handle, query, NULL, 0, result, &check);
    if(status != ENT_DBS_ALLOC_FAILED || check.calls != 1)
    {
        fprintf(stderr, "%s over-limit rejection failed: %d\n", prefix, (int)status);
        failed = 1;
    }
    check.expectedBytes = 1;
    status = ENT_DbReadParams(handle, "SELECT 1", NULL, 0, result, &check);
    if(status < 0 || check.calls != 2 || check.badResult)
    {
        fprintf(stderr, "%s handle recovery failed: %d\n", prefix, (int)status);
        failed = 1;
    }
    if(ENT_DbCloseHandle(&handle) < 0 || handle != NULL)
    {
        failed = 1;
    }
    return failed;
}

int main(void)
{
    int mysql;
    int pgsql;
    if(getenv("ENT_MYSQL_HOST") == NULL || getenv("ENT_PGSQL_HOST") == NULL)
    {
        puts("DB resource integration skipped: configure both disposable fixtures");
        return getenv("ENT_DB_REQUIRE_FIXTURES") ? EXIT_FAILURE : 77;
    }
    if(ENT_DbInit() < 0)
    {
        return EXIT_FAILURE;
    }
    mysql = backend(MYSQL_TYPE, "ENT_MYSQL");
    pgsql = backend(PGSQL_TYPE, "ENT_PGSQL");
    if(ENT_DbClose() < 0)
    {
        return EXIT_FAILURE;
    }
    return mysql != 0 || pgsql != 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
