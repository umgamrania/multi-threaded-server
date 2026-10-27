#ifndef _HTTP_COMMON_H_
#define _HTTP_COMMON_H_

#include <stdio.h>

#define MIN(x, y) ((x) < (y) ? (x) : (y))

#ifdef DEBUG
    #define DEBUG_PRINTF(...) printf(__VA_ARGS__)
#else
    #define DEBUG_PRINTF(...) ((void)0)
#endif

#define UNLIKELY(x) __builtin_expect(!!(x), 0)

#endif