#include <stdio.h>

#include "ent_db.h"
#include "ent_msg.h"

static int expect_status(MSG_ID_T actual, MSG_ID_T expected, const char* message)
{
    if(actual != expected)
    {
        fprintf(stderr,
                "%s: expected status[%d], actual[%d]\n",
                message,
                (int)expected,
                (int)actual);
        return 1;
    }
    return 0;
}

static int expect_disabled_backend(DB_TYPE dbType, const char* name)
{
    DB_HANDLE handle = NULL;
    MSG_ID_T sts = ENT_DbInitHandle(&handle,
                                    dbType,
                                    NULL,
                                    NULL,
                                    NULL,
                                    NULL,
                                    0);
    int failures = 0;

    failures += expect_status(sts,
                              ENT_DBS_UNSUPPORTED,
                              "disabled backend must be rejected during handle init");
    if(handle != NULL)
    {
        fprintf(stderr, "%s disabled backend returned a live handle\n", name);
        failures++;
    }
    return failures;
}

int main(void)
{
    int failures = 0;
    MSG_ID_T sts = ENT_DbInit();

    failures += expect_status(sts,
                              ENT_SYS_NORMAL,
                              "database service must initialize without backends");
    if(sts != ENT_SYS_NORMAL)
    {
        return failures;
    }

#if !(ENT_ENABLE_SQLITE && ENT_SQLITE_FOUND)
    failures += expect_disabled_backend(SQLITE_TYPE, "SQLite");
#endif
#if !(ENT_ENABLE_MYSQL && ENT_MYSQL_FOUND)
    failures += expect_disabled_backend(MYSQL_TYPE, "MySQL");
#endif
#if !(ENT_ENABLE_PGSQL && ENT_PGSQL_FOUND)
    failures += expect_disabled_backend(PGSQL_TYPE, "PostgreSQL");
#endif

    sts = ENT_DbClose();
    failures += expect_status(sts,
                              ENT_SYS_NORMAL,
                              "disabled backend rejection must not leave live handles");

    return failures;
}
