#include <stdio.h>
#include <stdlib.h>

#include "ent_db.h"
#include "ent_msg.h"

static int expect_true(int condition, const char* message)
{
    if(!condition)
    {
        fprintf(stderr, "%s\n", message);
        return 1;
    }
    return 0;
}

int main(void)
{
    DB_HANDLE db_handle = NULL;
    MSG_ID_T sts = ENT_SYS_NORMAL;

    ENT_DbClose();
    sts = ENT_DbInit();
    if(expect_true(sts == ENT_SYS_NORMAL || sts == ENT_SYS_ALREADY_INITIALIZED,
                   "ENT_DbInit should initialize the DB service") != 0)
    {
        return EXIT_FAILURE;
    }

    sts = ENT_DbInitHandle(&db_handle,
                           SQLITE_TYPE,
                           NULL,
                           ":memory:",
                           NULL,
                           NULL,
                           0);
    if(expect_true(sts == ENT_SYS_NORMAL && db_handle != NULL,
                   "ENT_DbInitHandle should create the initial SQLite handle") != 0)
    {
        ENT_DbClose();
        return EXIT_FAILURE;
    }

    sts = ENT_DbInitHandle(&db_handle,
                           (DB_TYPE)999,
                           "unused",
                           "unused",
                           "unused",
                           "unused",
                           0);
    if(expect_true(sts == ENT_DBS_UNSUPPORTED,
                   "reinitializing an existing handle to an unsupported backend should fail safely") != 0)
    {
        if(db_handle != NULL)
        {
            ENT_DbCloseHandle(&db_handle);
        }
        ENT_DbClose();
        return EXIT_FAILURE;
    }

    if(expect_true(db_handle == NULL,
                   "unsupported reinit should leave no live DB handle") != 0)
    {
        ENT_DbClose();
        return EXIT_FAILURE;
    }

    return expect_true(ENT_DbClose() == ENT_SYS_NORMAL,
                       "unsupported reinit should release the old handle exactly once") == 0
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}
