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

    return EXIT_SUCCESS;
}
