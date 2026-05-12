#include <stdio.h>

#include "ent_init.h"
#include "ent_msg.h"
#include "ent_log.h"

static int print_backend_matrix(void)
{
    printf("ENT_ENABLE_SQLITE=%d\n", (int)ENT_ENABLE_SQLITE);
    printf("ENT_ENABLE_MYSQL=%d\n", (int)ENT_ENABLE_MYSQL);
    printf("ENT_ENABLE_PGSQL=%d\n", (int)ENT_ENABLE_PGSQL);
    return 0;
}

int main(void)
{
    MSG_ID_T sts = ENT_Helpers();
    ENT_HANDLE handle = NULL;
    if(sts != ENT_SYS_NORMAL)
    {
        fprintf(stderr, "ENT_Helpers failed: %d\n", (int)sts);
        return 1;
    }

    sts = ENT_Init(&handle, "downstream_consumer", ".", LOG_LEV_WARN_E, ENT_MODE_NORMAL_E);
    if(sts != ENT_SYS_NORMAL)
    {
        fprintf(stderr, "ENT_Init failed: %d\n", (int)sts);
        return 1;
    }

    sts = ENT_Close(&handle);
    if(sts != ENT_SYS_NORMAL || handle != NULL)
    {
        fprintf(stderr, "ENT_Close failed: %d\n", (int)sts);
        return 1;
    }

    print_backend_matrix();
    puts("downstream consumer OK");
    return 0;
}
