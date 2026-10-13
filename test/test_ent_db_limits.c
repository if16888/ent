#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ent_db.h"
#include "ent_msg.h"

static int calls;
static void result(char** fields, char** rows, long long count, int columns, void* data)
{
    (void)fields; (void)rows; (void)count; (void)columns; (void)data;
    calls++;
}

#define CHECK(expr) do { if(!(expr)) { fprintf(stderr, "failed: %s\n", #expr); failed++; } } while(0)

int main(void)
{
    DB_HANDLE db = NULL;
    ENT_DB_PARAM params[5];
    char* text = (char*)malloc((size_t)ENT_DB_MAX_FIELD_BYTES + 2);
    int failed = 0;
    size_t i;
    if(text == NULL)
    {
        return EXIT_FAILURE;
    }
    memset(text, 'x', ENT_DB_MAX_FIELD_BYTES + 1);
    text[ENT_DB_MAX_FIELD_BYTES + 1] = '\0';
    memset(params, 0, sizeof(params));
    params[0].type = ENT_DB_PARAM_TEXT_E;
    params[0].value.text = text;
    CHECK(ENT_DbReadParams(NULL, "select ?", params, ENT_DB_MAX_PARAMS + 1, result, &calls) == ENT_DBS_BAD_PARAMS);
    CHECK(ENT_DbReadParams(NULL, "select ?", params, 1, result, &calls) == ENT_DBS_BAD_PARAMS);
    CHECK(ENT_DbReadParams(NULL, "select ?", NULL, 1, result, &calls) == ENT_DBS_BAD_PARAMS);
    params[0].type = (ENT_DB_PARAM_TYPE)999;
    CHECK(ENT_DbReadParams(NULL, "select ?", params, 1, result, &calls) == ENT_DBS_BAD_PARAMS);
    params[0].type = ENT_DB_PARAM_BLOB_E;
    params[0].value.blob.data = text;
    params[0].value.blob.size = (size_t)-1;
    CHECK(ENT_DbWriteParams(NULL, "select ?", params, 1, result, &calls) == ENT_DBS_BAD_PARAMS);
    CHECK(calls == 0);

#if ENT_ENABLE_SQLITE && ENT_SQLITE_FOUND
    CHECK(ENT_DbInit() >= 0);
    CHECK(ENT_DbInitHandle(&db, SQLITE_TYPE, NULL, ":memory:", NULL, NULL, 0) >= 0);
    for(i = 0; i < 5; i++)
    {
        params[i].type = ENT_DB_PARAM_TEXT_E;
        params[i].value.text = text;
    }
    text[ENT_DB_MAX_FIELD_BYTES] = '\0';
    CHECK(ENT_DbReadParams(db, "select length(?)", params, 1, result, &calls) >= 0);
    CHECK(calls == 1);
    CHECK(ENT_DbReadParams(db, "select 1", params, 5, result, &calls) == ENT_DBS_BAD_PARAMS);
    CHECK(calls == 1);
    CHECK(ENT_DbReadParams(db, "select length(?), length(?), length(?), length(?)", params, 4, result, &calls) >= 0);
    CHECK(calls == 2);
    CHECK(ENT_DbReadParams(db, "select printf('%.*c', 16777217, 'a')", NULL, 0, result, &calls) == ENT_DBS_ALLOC_FAILED);
    CHECK(calls == 2);
    CHECK(ENT_DbRead(db, "select 1", result, &calls) >= 0);
    CHECK(calls == 3);
    CHECK(ENT_DbCloseHandle(&db) >= 0 && db == NULL);
    CHECK(ENT_DbClose() >= 0);
#else
    (void)db; (void)i;
#endif
    free(text);
    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
