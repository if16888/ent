#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Private injection seam: exercise realloc failure without exhausting RAM. */
static int s_fail_realloc = 0;
static void* test_realloc(void* pointer, size_t bytes)
{
    if(s_fail_realloc) return NULL;
    return realloc(pointer, bytes);
}
#define IENT_UTL_REALLOC test_realloc
#include "ient_comm.h"

int main(void)
{
    size_t cap = 0;
    size_t old_cap;
    uint32_t* data = NULL;
    uint32_t* next;
    size_t i;

    data = (uint32_t*)UTL_Malloc(NULL, &cap, (int)sizeof(*data), 3);
    if(data == NULL || cap != 8) goto FAILED;
    for(i = 0; i < cap; ++i) if(data[i] != 0) goto FAILED;
    data[0] = 0x12345678u;
    next = (uint32_t*)UTL_Malloc(data, &cap, (int)sizeof(*data), 10);
    if(next == NULL) goto FAILED;
    data = next;
    if(cap != 24 || data[0] != 0x12345678u) goto FAILED;
    for(i = 8; i < cap; ++i) if(data[i] != 0) goto FAILED;

    old_cap = cap;
    s_fail_realloc = 1;
    next = (uint32_t*)UTL_Malloc(data, &cap, (int)sizeof(*data), old_cap + 1);
    s_fail_realloc = 0;
    if(next != NULL || cap != old_cap || data[0] != 0x12345678u) goto FAILED;

    next = (uint32_t*)UTL_Malloc(data, &cap, 1, SIZE_MAX);
    if(next != NULL || cap != old_cap || data[0] != 0x12345678u) goto FAILED;
    next = (uint32_t*)UTL_Malloc(data, &cap, 16, SIZE_MAX / 4);
    if(next != NULL || cap != old_cap || data[0] != 0x12345678u) goto FAILED;

    free(data);
    puts("UTL_Malloc capacity, zero growth, overflow and injected failure: PASS");
    return EXIT_SUCCESS;
FAILED:
    fprintf(stderr, "UTL_Malloc allocation contract failed, capacity=%zu\n", cap);
    free(data);
    return EXIT_FAILURE;
}
