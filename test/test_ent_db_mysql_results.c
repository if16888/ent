/* Exercise backend result handling with deterministic client fetch outcomes. */
#include <stdio.h>
#include <string.h>
#include "ient_db.h"

static MYSQL_BIND* sBindings;
static MYSQL_FIELD sField;
static int sMode;
static int sFetched;
static my_ulonglong sReportedRows;
static int sActualRows;
static unsigned int test_field_count(MYSQL_RES* meta)
{
    (void)meta;
    return 1;
}
static my_ulonglong test_row_count(MYSQL_STMT* stmt)
{
    (void)stmt;
    return sReportedRows;
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
    if(row >= sActualRows)
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
    long long i;
    if(count != (long long)sReportedRows || columns != 1 || fields == NULL || rows == NULL)
    {
        *calls = -100;
        return;
    }
    for(i = 0; i < count; i++)
    {
        if(rows[i] == NULL || strcmp(rows[i], "abc") != 0)
        {
            *calls = -100;
            return;
        }
    }
    (*calls)++;
}

int main(void)
{
    static const struct
    {
        const char* name;
        my_ulonglong reported;
        int actual;
        int mode;
        MSG_ID_T expected;
    } cases[] = {
        {"one row", 1, 1, 0, ENT_SYS_NORMAL},
        {"multiple rows", 2, 2, 0, ENT_SYS_NORMAL},
        {"empty result", 0, 0, 0, ENT_SYS_NORMAL},
        {"excess rows", 1, 2, 0, ENT_DBS_RESULT_FAILED},
        {"missing row", 2, 1, 0, ENT_DBS_RESULT_FAILED},
        {"unexpected nonempty result", 0, 1, 0, ENT_DBS_RESULT_FAILED},
        {"unexpected empty result", 2, 0, 0, ENT_DBS_RESULT_FAILED},
        {"truncated first row", 1, 1, 1, ENT_DBS_RESULT_FAILED},
        {"invalid field length", 1, 1, 2, ENT_DBS_RESULT_FAILED},
        {"truncated after allocation", 2, 2, 3, ENT_DBS_RESULT_FAILED},
        {"valid result after errors", 1, 1, 0, ENT_SYS_NORMAL},
    };
    size_t i;
    memset(&sField, 0, sizeof(sField));
    sField.name = "value";
    sField.max_length = 3;
    for(i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        int calls = 0;
        MSG_ID_T status;
        sMode = cases[i].mode;
        sReportedRows = cases[i].reported;
        sActualRows = cases[i].actual;
        sFetched = 0;
        status = iENT_DbMySQLCollectRows((MYSQL_STMT*)1, (MYSQL_RES*)1, result, &calls);
        if(status != cases[i].expected || calls != (status == ENT_SYS_NORMAL ? 1 : 0))
        {
            fprintf(stderr, "mysql %s: status=%d callback count=%d\n", cases[i].name, (int)status, calls);
            return 1;
        }
    }
    return 0;
}
