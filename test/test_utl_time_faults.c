#define _POSIX_C_SOURCE 200809L
#include <time.h>
#include <stdint.h>
#include <stdio.h>
#include "ent_utility.h"
#include "ent_msg.h"
static struct timespec sample;
static int failure;
int iUTL_TestClockGettime(clockid_t clock, struct timespec* output)
{
    if(clock != CLOCK_MONOTONIC) return -1;
    if(failure) return -1;
    *output = sample;
    return 0;
}
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"failed line %d\n",__LINE__); return 1; } } while(0)
int main(void)
{
    ENT_U64 value = 123;
    failure = 1;
    CHECK(UTL_MonotonicMs(&value) == ENT_UTHD_CLOCK_FAILED && value == 123);
    failure = 0;
    sample.tv_sec = -1;
    CHECK(UTL_MonotonicMs(&value) == ENT_UTHD_CLOCK_FAILED && value == 123);
    sample.tv_sec = 0; sample.tv_nsec = -1;
    CHECK(UTL_MonotonicMs(&value) == ENT_UTHD_CLOCK_FAILED && value == 123);
    sample.tv_nsec = 1000000000L;
    CHECK(UTL_MonotonicMs(&value) == ENT_UTHD_CLOCK_FAILED && value == 123);
    sample.tv_sec = 7; sample.tv_nsec = 1999999;
    CHECK(UTL_MonotonicMs(&value) == ENT_SYS_NORMAL && value == 7001);
    sample.tv_sec = 0; sample.tv_nsec = 0;
    CHECK(UTL_MonotonicMs(&value) == ENT_SYS_NORMAL && value == 0);
    if(sizeof(time_t) >= 8) {
        sample.tv_sec = (time_t)(UINT64_MAX / 1000U);
        sample.tv_nsec = (long)(UINT64_MAX % 1000U) * 1000000L;
        CHECK(UTL_MonotonicMs(&value) == ENT_SYS_NORMAL && value == UINT64_MAX);
        sample.tv_nsec += 1000000L;
        CHECK(UTL_MonotonicMs(&value) == ENT_UTHD_CLOCK_FAILED && value == UINT64_MAX);
        sample.tv_sec++; sample.tv_nsec = 0;
        CHECK(UTL_MonotonicMs(&value) == ENT_UTHD_CLOCK_FAILED && value == UINT64_MAX);
    }
    return 0;
}
