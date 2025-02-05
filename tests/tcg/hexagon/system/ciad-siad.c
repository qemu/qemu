/*
 *  Copyright(c) 2023-2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdint.h>
#include <stdio.h>


static inline void siad(uint32_t val)
{
    asm volatile ("siad(%0);"
                  : : "r"(val));
    return;
}
static inline void ciad(uint32_t val)
{
    asm volatile ("ciad(%0);"
                  : : "r"(val));
    return;
}

static inline uint32_t getipendad()
{
    uint32_t reg;
    asm volatile ("%0=s20;"
                  : "=r"(reg));
    return reg;
}
int
main(int argc, char *argv[])
{
    siad(4);
    int ipend = getipendad();
    if (ipend != (0x4 << 16)) {
        goto fail;
    }
    ciad(4);
    ipend = getipendad();
    if (ipend) {
        goto fail;
    }

    printf("PASS\n");
    return 0;
fail:
    printf("FAIL\n");
    return 1;
}
