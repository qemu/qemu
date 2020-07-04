/*
 * Helper Functions
 *
 * Copyright (c) 2019 IBM Corp.
 *
 * Author(s): Jason J. Herne <jjherne@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#ifndef S390_CCW_HELPER_H
#define S390_CCW_HELPER_H

#include "s390-ccw.h"
#include "s390-time.h"

/* Avoids compiler warnings when casting a pointer to a u32 */
static inline uint32_t ptr2u32(void *ptr)
{
    IPL_assert((uint64_t)ptr <= 0xffffffffull, "ptr2u32: ptr too large");
    return (uint32_t)(uint64_t)ptr;
}

/* Avoids compiler warnings when casting a u32 to a pointer */
static inline void *u32toptr(uint32_t n)
{
    return (void *)(uint64_t)n;
}

static inline void yield(void)
{
    asm volatile ("diag 0,0,0x44"
                  : :
                  : "memory", "cc");
}

static inline void sleep(unsigned int seconds)
{
    ulong target = get_time_seconds() + seconds;

    while (get_time_seconds() < target) {
        yield();
    }
}

#endif
