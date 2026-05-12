#include <stdio.h>

#include "ent_init.h"
#include "ent_log.h"
#include "ent_msg.h"

static void close_handle_if_needed(ENT_HANDLE* handle)
{
    if(handle != NULL && *handle != NULL)
    {
        ENT_Close(handle);
    }
}

int main(void)
{
    ENT_HANDLE handle_a = NULL;
    ENT_HANDLE handle_b = NULL;
    MSG_ID_T sts = ENT_SYS_NORMAL;

    sts = ENT_Init(&handle_a,
                   "node_a",
                   "./work_a",
                   LOG_LEV_WARN_E,
                   ENT_MODE_NORMAL_E);
    if(sts != ENT_SYS_NORMAL)
    {
        fprintf(stderr, "ENT_Init(handle_a) failed: %d\n", (int)sts);
        return 1;
    }

    sts = ENT_Init(&handle_b,
                   "node_b",
                   "./work_b",
                   LOG_LEV_WARN_E,
                   ENT_MODE_NORMAL_E);
    if(sts != ENT_SYS_NORMAL)
    {
        fprintf(stderr, "ENT_Init(handle_b) failed: %d\n", (int)sts);
        close_handle_if_needed(&handle_a);
        return 1;
    }

    /*
     * In a real application, each handle would typically be driven from its
     * own worker thread via ENT_Run(handle_x).
     *
     * This sample intentionally avoids calling ENT_Run() directly,
     * because the default run loop blocks waiting on its condition
     * variable and would turn the sample into a hanging demo.
     * If you do start the workers, pair ENT_Run() with ENT_Stop()
     * before ENT_Close().
     */

    sts = ENT_SetRtAttributes(handle_a, -1, ENT_RT_POLICY_OTHER_E, 0);
    if(sts != ENT_SYS_NORMAL && sts != ENT_RT_NOTRT)
    {
        fprintf(stderr, "ENT_SetRtAttributes(handle_a) failed: %d\n", (int)sts);
        close_handle_if_needed(&handle_b);
        close_handle_if_needed(&handle_a);
        return 1;
    }

    puts("two isolated handle instances initialized successfully");

    close_handle_if_needed(&handle_b);
    close_handle_if_needed(&handle_a);
    return 0;
}
