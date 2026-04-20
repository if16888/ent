#include <stdio.h>

#include "ent_init.h"
#include "ent_msg.h"

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
    if(sts != ENT_SYS_NORMAL)
    {
        fprintf(stderr, "ENT_Helpers failed: %d\n", (int)sts);
        return 1;
    }

    print_backend_matrix();
    puts("downstream consumer OK");
    return 0;
}
