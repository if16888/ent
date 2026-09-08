#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ent_script.h"

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
#ifdef _WIN32
    const char* script_root = ".";
    const char* script_path = "ent_script_return_compat.lua";
#else
    const char* script_root = "/tmp";
    const char* script_path = "/tmp/ent_script_return_compat.lua";
#endif
    const char* script_name = "ent_script_return_compat.lua";
    ENT_SCRIPT_RET_T out;
    FILE* fp;
    int rc = EXIT_FAILURE;

    ENT_ScriptClose();
    remove(script_path);

    fp = fopen(script_path, "wb");
    if(fp == NULL)
    {
        fprintf(stderr, "failed to create compatibility script\n");
        return EXIT_FAILURE;
    }
    fprintf(fp, "function table_string_code(args) return {code=\"-7\", message=\"denied\"} end\n");
    fprintf(fp, "function scalar_number_code(args) return -7 end\n");
    fclose(fp);

    if(expect_true(ENT_ScriptInit(script_root) == ENT_SYS_NORMAL,
                   "ENT_ScriptInit should initialize for return compatibility test") != 0)
    {
        goto CLEANUP_FILE;
    }
    if(expect_true(ENT_ScriptReload(script_name) == ENT_SYS_NORMAL,
                   "ENT_ScriptReload should load return compatibility script") != 0)
    {
        goto CLEANUP_ENGINE;
    }

    memset(&out, 0, sizeof(out));
    if(expect_true(ENT_ScriptCall("table_string_code", NULL, &out) == ENT_SYS_NORMAL,
                   "table string code call should succeed") != 0 ||
       expect_true(out.code == -7,
                   "numeric string table code should preserve legacy numeric conversion") != 0 ||
       expect_true(strcmp(out.message, "denied") == 0,
                   "table message should remain intact when code is a numeric string") != 0)
    {
        goto CLEANUP_ENGINE;
    }

    memset(&out, 0, sizeof(out));
    if(expect_true(ENT_ScriptCall("scalar_number_code", NULL, &out) == ENT_SYS_NORMAL,
                   "scalar numeric return call should succeed") != 0 ||
       expect_true(out.code == -7,
                   "scalar numeric return should remain an error code") != 0 ||
       expect_true(out.message[0] == '\0',
                   "scalar numeric return should not be converted to a message string") != 0)
    {
        goto CLEANUP_ENGINE;
    }

    rc = EXIT_SUCCESS;

CLEANUP_ENGINE:
    ENT_ScriptClose();
CLEANUP_FILE:
    remove(script_path);
    return rc;
}
