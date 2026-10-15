/* Exercise backend result handling with deterministic client fetch outcomes. */
#include <stdio.h>
#include <string.h>
#include "ient_db.h"

static MYSQL_BIND* sBindings;
static MYSQL_FIELD sField;
static int sMode;
static int sFetched;
static unsigned int test_field_count(MYSQL_RES* meta)
{
    (void)meta;
    return 1;
}
static my_ulonglong test_row_count(MYSQL_STMT* stmt)
{
    (void)stmt;
    return sMode == 3 ? 2 : 1;
}
static MYSQL_FIELD* test_fields(MYSQL_RES* meta)
{
    (void)meta;
    return &sField;
}
static int test_bind(MYSQL_STMT* stmt, MYSQL_BIND* bindings)
{
    (void)stmt;
    sBindings = bindings;
    return 0;
}
static int test_fetch(MYSQL_STMT* stmt)
{
    int row = sFetched++;
    (void)stmt;
    if(row >= (sMode == 3 ? 2 : 1))
    {
        return MYSQL_NO_DATA;
    }
    memcpy(sBindings[0].buffer, "abc", 3);
    *sBindings[0].length = sMode == 2 ? sBindings[0].buffer_length : 3;
    return (sMode == 1 || (sMode == 3 && row == 1)) ? MYSQL_DATA_TRUNCATED : 0;
}
static const char* test_error(MYSQL_STMT* stmt)
{
    (void)stmt;
    return "injected truncation";
}
#define mysql_num_fields test_field_count
#define mysql_stmt_num_rows test_row_count
#define mysql_fetch_fields test_fields
#define mysql_stmt_bind_result test_bind
#define mysql_stmt_fetch test_fetch
#define mysql_stmt_error test_error
#include "../comm/ent_db_mysql.c"

static void result(char** fields, char** rows, long long count, int columns, void* data)
{
    int* calls = (int*)data;
    if(count == 1 && columns == 1 && fields != NULL && rows != NULL &&
       rows[0] != NULL && strcmp(rows[0], "abc") == 0)
    {
        (*calls)++;
    }
    else
    {
        *calls = -100;
    }
}

int main(void)
{
    int mode;
    memset(&sField, 0, sizeof(sField));
    sField.name = "value";
    sField.max_length = 3;
    for(mode = 0; mode < 4; mode++)
    {
        int calls = 0;
        MSG_ID_T status;
        sMode = mode;
        sFetched = 0;
        status = iENT_DbMySQLCollectRows((MYSQL_STMT*)1, (MYSQL_RES*)1, result, &calls);
        if((mode == 0 && (status != ENT_SYS_NORMAL || calls != 1)) ||
           (mode != 0 && (status != ENT_DBS_RESULT_FAILED || calls != 0)))
        {
            fprintf(stderr, "mysql fetch mode %d: status=%d callback count=%d\n", mode, (int)status, calls);
            return 1;
        }
    }
    return 0;
}
