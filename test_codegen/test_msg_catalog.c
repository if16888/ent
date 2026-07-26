#include <string.h>

#include "zx_msg_gen.h"

int main(void)
{
    if(g_zx_msg_items_count != 3u)
    {
        return 1;
    }
    if(ZX_NET_OK != (MSG_ID_T)0x08008000u)
    {
        return 2;
    }
    if(ZX_NET_BAD != (MSG_ID_T)0x88008001u)
    {
        return 3;
    }
    if(ZX_YZ_RPC_TIMEOUT != (MSG_ID_T)0x88810001u)
    {
        return 4;
    }
    if(strcmp(g_zx_msg_items[0].symbol, "ZX_NET_OK") != 0)
    {
        return 5;
    }
    if(strcmp(g_zx_msg_items[2].message, "generated rpc timeout") != 0)
    {
        return 6;
    }
    return 0;
}
