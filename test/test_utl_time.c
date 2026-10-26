#include "ent_utility.h"
#include "ent_msg.h"
#include <stdio.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"failed line %d\n",__LINE__); return 1; } } while(0)
int main(void)
{
    ENT_U64 previous = 0, current = 0;
    int i;
    CHECK(UTL_MonotonicMs(NULL) == ENT_UTHD_INVALID_ARGUMENT);
    CHECK(UTL_MonotonicMs(&previous) == ENT_SYS_NORMAL);
    for(i = 0; i < 10000; ++i) {
        CHECK(UTL_MonotonicMs(&current) == ENT_SYS_NORMAL);
        CHECK(current >= previous);
        previous = current;
    }
    return 0;
}
