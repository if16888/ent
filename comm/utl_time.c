/* Copyright 2026 Fei Li. Licensed under the Apache License, Version 2.0. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "ent_utility.h"
#include "ent_msg.h"
#include <stdint.h>
#ifndef _WIN32
#include <time.h>
#endif

ENT_PUBLIC MSG_ID_T UTL_MonotonicMs(ENT_U64* value)
{
    if(value == NULL)
        return ENT_UTHD_INVALID_ARGUMENT;
#ifdef _WIN32
    *value = (ENT_U64)GetTickCount64();
#else
    struct timespec now;
    ENT_U64 seconds, fraction;
    if(clock_gettime(CLOCK_MONOTONIC, &now) != 0 || now.tv_sec < 0 ||
       now.tv_nsec < 0 || now.tv_nsec >= 1000000000L)
        return ENT_UTHD_CLOCK_FAILED;
    seconds = (ENT_U64)now.tv_sec;
    fraction = (ENT_U64)now.tv_nsec / 1000000U;
    if(seconds > (UINT64_MAX - fraction) / 1000U)
        return ENT_UTHD_CLOCK_FAILED;
    *value = seconds * 1000U + fraction;
#endif
    return ENT_SYS_NORMAL;
}
