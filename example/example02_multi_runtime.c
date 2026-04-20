#include <stdio.h>

#include "ent_init.h"
#include "ent_log.h"
#include "ent_msg.h"

static void close_runtime_if_needed(ENT_RUNTIME* runtime)
{
    if(runtime != NULL && *runtime != NULL)
    {
        ENT_RuntimeClose(*runtime);
        *runtime = NULL;
    }
}

int main(void)
{
    ENT_RUNTIME runtime_a = NULL;
    ENT_RUNTIME runtime_b = NULL;
    MSG_ID_T sts = ENT_SYS_NORMAL;

    sts = ENT_RuntimeInit(&runtime_a,
                          "node_a",
                          "./work_a",
                          LOG_LEV_WARN_E,
                          ENT_MODE_NORMAL_E);
    if(sts != ENT_SYS_NORMAL)
    {
        fprintf(stderr, "ENT_RuntimeInit(runtime_a) failed: %d\n", (int)sts);
        return 1;
    }

    sts = ENT_RuntimeInit(&runtime_b,
                          "node_b",
                          "./work_b",
                          LOG_LEV_WARN_E,
                          ENT_MODE_NORMAL_E);
    if(sts != ENT_SYS_NORMAL)
    {
        fprintf(stderr, "ENT_RuntimeInit(runtime_b) failed: %d\n", (int)sts);
        close_runtime_if_needed(&runtime_a);
        return 1;
    }

    /*
     * In a real application, each runtime would typically be driven from its
     * own worker thread via ENT_RuntimeRun(runtime_x).
     *
     * This sample intentionally avoids calling ENT_RuntimeRun() directly,
     * because the default runtime loop blocks waiting on its condition
     * variable and would turn the sample into a hanging demo.
     */

    sts = ENT_RuntimeSetRtAttributes(runtime_a, -1, ENT_RT_POLICY_OTHER_E, 0);
    if(sts != ENT_SYS_NORMAL && sts != ENT_RT_NOTRT)
    {
        fprintf(stderr, "ENT_RuntimeSetRtAttributes(runtime_a) failed: %d\n", (int)sts);
        close_runtime_if_needed(&runtime_b);
        close_runtime_if_needed(&runtime_a);
        return 1;
    }

    puts("two isolated runtime instances initialized successfully");

    close_runtime_if_needed(&runtime_b);
    close_runtime_if_needed(&runtime_a);
    return 0;
}
