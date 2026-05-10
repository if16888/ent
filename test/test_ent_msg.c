#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    MSG_ID_T appErr = 0;

    if(expect_true(ENT_SYS_NORMAL >= 0, "ENT_SYS_NORMAL should be non-negative") != 0)
    {
        return EXIT_FAILURE;
    }

    if(expect_true(ENT_INIT_INVALID_ARGUMENT < 0,
                   "ENT_INIT_INVALID_ARGUMENT should be negative") != 0)
    {
        return EXIT_FAILURE;
    }

    if(expect_true(ENT_MsgIsError(ENT_INIT_INVALID_ARGUMENT) == true,
                   "ENT_MsgIsError should identify error codes") != 0)
    {
        return EXIT_FAILURE;
    }

    if(expect_true(ENT_MsgGetModule(ENT_INIT_INVALID_ARGUMENT) == ENT_MSG_MODULE_ENT,
                   "ENT module id decode mismatch") != 0)
    {
        return EXIT_FAILURE;
    }

    if(expect_true(strcmp(ENT_MsgModuleName(ENT_INIT_INVALID_ARGUMENT), "ENT") == 0,
                   "ENT module name lookup mismatch") != 0)
    {
        return EXIT_FAILURE;
    }

    if(expect_true(strcmp(ENT_MsgSubmoduleName(ENT_INIT_INVALID_ARGUMENT), "INIT") == 0,
                   "ENT submodule name lookup mismatch") != 0)
    {
        return EXIT_FAILURE;
    }

    if(expect_true(ENT_MsgGetSubmodule(ENT_DBS_BAD_ARGUMENT) == 10u,
                   "DBS submodule id decode mismatch") != 0)
    {
        return EXIT_FAILURE;
    }

    if(expect_true(ENT_MsgGetModule(ENT_DBS_BAD_ARGUMENT) == ENT_MSG_MODULE_ENT,
                   "DBS module id decode mismatch") != 0)
    {
        return EXIT_FAILURE;
    }

    if(expect_true(strcmp(ENT_MsgSubmoduleName(ENT_DBS_BAD_ARGUMENT), "DBS") == 0,
                   "DBS submodule name lookup mismatch") != 0)
    {
        return EXIT_FAILURE;
    }

    if(expect_true(strcmp(ENT_MsgText(ENT_DBS_BAD_ARGUMENT), "invalid database argument") == 0,
                   "DBS message text lookup mismatch") != 0)
    {
        return EXIT_FAILURE;
    }

    if(expect_true(ENT_MsgGetSubmodule(ENT_SHM_BAD_ARGUMENT) == 12u,
                   "SHM submodule id decode mismatch") != 0)
    {
        return EXIT_FAILURE;
    }

    if(expect_true(strcmp(ENT_MsgSubmoduleName(ENT_SHM_BAD_ARGUMENT), "SHM") == 0,
                   "SHM submodule name lookup mismatch") != 0)
    {
        return EXIT_FAILURE;
    }

    if(expect_true(strcmp(ENT_MsgText(ENT_SHM_BAD_ARGUMENT), "invalid shared-map argument") == 0,
                   "SHM message text lookup mismatch") != 0)
    {
        return EXIT_FAILURE;
    }

    if(expect_true(ENT_MsgIsError(ENT_SHM_BAD_ARGUMENT) == true,
                   "SHM message should be an error code") != 0)
    {
        return EXIT_FAILURE;
    }

    if(expect_true(strcmp(ENT_MsgText(ENT_INIT_INVALID_ARGUMENT), "invalid init argument: name[%s] workPath[%s]") == 0,
                   "ENT message text lookup mismatch") != 0)
    {
        return EXIT_FAILURE;
    }

    {
        char msgBuf[128];
        int n = ENT_MsgFormat(msgBuf, sizeof(msgBuf), ENT_INIT_INVALID_ARGUMENT, "demo", "/tmp/demo");
        if(expect_true(n > 0, "ENT_MsgFormat should produce a formatted message") != 0)
        {
            return EXIT_FAILURE;
        }
        if(expect_true(strstr(msgBuf, "demo") != NULL, "formatted message should include provided name") != 0)
        {
            return EXIT_FAILURE;
        }
    }

    appErr = ENT_MsgBuild(true, ENT_MSG_EXTERNAL_MODULE_MIN, 3u, 5u);
    if(expect_true(appErr < 0, "external app error code should be negative") != 0)
    {
        return EXIT_FAILURE;
    }

    if(expect_true(ENT_MsgGetModule(appErr) == ENT_MSG_EXTERNAL_MODULE_MIN,
                   "external app module decode mismatch") != 0)
    {
        return EXIT_FAILURE;
    }

    if(expect_true(strcmp(ENT_MsgModuleName(appErr), "APP_MODULE") == 0,
                   "external app module name fallback mismatch") != 0)
    {
        return EXIT_FAILURE;
    }

    if(expect_true(strcmp(ENT_MsgSubmoduleName(appErr), "APP_SUBMODULE") == 0,
                   "external app submodule name fallback mismatch") != 0)
    {
        return EXIT_FAILURE;
    }

    {
        MSG_ID_T built = 0;
        if(expect_true(ENT_MsgTryBuild(&built, false, 1u, 2u, 3u) == ENT_SYS_NORMAL,
                       "ENT_MsgTryBuild should accept valid parts") != 0)
        {
            return EXIT_FAILURE;
        }
        if(expect_true(ENT_MsgIsError(built) == false, "ENT_MsgTryBuild should preserve severity") != 0)
        {
            return EXIT_FAILURE;
        }
    }

    {
        MSG_ID_T built = 0;
        if(expect_true(ENT_MsgTryBuild(&built, true, 0x1FFu, 2u, 3u) == ENT_SYS_INVALID_MSGCODE,
                       "ENT_MsgTryBuild should reject out-of-range module ids") != 0)
        {
            return EXIT_FAILURE;
        }
        if(expect_true(built == ENT_SYS_INVALID_MSGCODE,
                       "ENT_MsgTryBuild should return the invalid-code sentinel through outCode") != 0)
        {
            return EXIT_FAILURE;
        }
    }

    if(expect_true(ENT_MsgBuild(true, 0x1FFu, 2u, 3u) == ENT_SYS_INVALID_MSGCODE,
                   "ENT_MsgBuild should reject out-of-range parts instead of truncating") != 0)
    {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
