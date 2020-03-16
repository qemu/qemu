/*
 * SVE ioctls tests
 *
 * Test the SVE width setting ioctls work and provide a base for
 * testing the gdbstub.
 *
 * Copyright (c) 2019 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <sys/prctl.h>
#include <asm/hwcap.h>
#include <stdio.h>
#include <sys/auxv.h>
#include <stdint.h>
#include <stdlib.h>

#ifndef HWCAP_CPUID
#define HWCAP_CPUID (1 << 11)
#endif

#define SVE_MAX_QUADS  (2048 / 128)
#define BYTES_PER_QUAD (128 / 8)

#define get_cpu_reg(id) ({                                      \
            unsigned long __val;                                \
            asm("mrs %0, "#id : "=r" (__val));                  \
            __val;                                              \
        })

static int do_sve_ioctl_test(void)
{
    int i, res, init_vq;

    res = prctl(PR_SVE_GET_VL, 0, 0, 0, 0);
    if (res < 0) {
        printf("FAILED to PR_SVE_GET_VL (%d)", res);
        return -1;
    }
    init_vq = res & PR_SVE_VL_LEN_MASK;

    for (i = init_vq; i > 15; i /= 2) {
        printf("Checking PR_SVE_SET_VL=%d\n", i);
        res = prctl(PR_SVE_SET_VL, i, 0, 0, 0, 0);
        if (res < 0) {
            printf("FAILED to PR_SVE_SET_VL (%d)", res);
            return -1;
        }
        asm("index z0.b, #0, #1\n"
            ".global __sve_ld_done\n"
            "__sve_ld_done:\n"
            "mov z0.b, #0\n"
            : /* no outputs kept */
            : /* no inputs */
            : "memory", "z0");
    }
    printf("PASS\n");
    return 0;
}

int main(int argc, char **argv)
{
    /* we also need to probe for the ioctl support */
    if (getauxval(AT_HWCAP) & HWCAP_SVE) {
        return do_sve_ioctl_test();
    } else {
        printf("SKIP: no HWCAP_SVE on this system\n");
        return 0;
    }
}
