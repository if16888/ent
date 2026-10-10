#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>

#include "ient_comm.h"
#include "ient_runtime.h"
#include "ient_db.h"
#include "ent_msg.h"

ENT_CTX gEntCtx;

MSG_ID_T ENT_LogError(ENT_LOG logHandle, const char* format, ...)
{
    (void)logHandle;
    (void)format;
    return ENT_SYS_NORMAL;
}

int main(void)
{
    sqlite3* dbHandle = NULL;
    MSG_ID_T status = ENT_SYS_NORMAL;
    int rc = sqlite3_open(":memory:", &dbHandle);

    if(rc != SQLITE_OK)
    {
        fprintf(stderr, "sqlite3_open failed: %s\n", dbHandle ? sqlite3_errmsg(dbHandle) : "no handle");
        sqlite3_close(dbHandle);
        return EXIT_FAILURE;
    }

    status = ENT_DbSqliteReadParams(dbHandle,
                                   "SELECT 'first', 'second'",
                                   NULL,
                                   0,
                                   NULL,
                                   NULL);
    sqlite3_close(dbHandle);
    if(status != ENT_DBS_ALLOC_FAILED)
    {
        fprintf(stderr, "expected partial-row payload rejection, got status %d\n", (int)status);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
