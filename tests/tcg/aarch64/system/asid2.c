/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *
 * ASID2 Feature presence and enabled TCR2_EL1 bits test
 *
 * Copyright (c) 2025 Linaro Ltd
 *
 */

#include <stdint.h>
#include <minilib.h>

#define ID_AA64MMFR3_EL1 "S3_0_C0_C7_3"
#define ID_AA64MMFR4_EL1 "S3_0_C0_C7_4"
#define TCR2_EL1 "S3_0_C2_C0_3"

int main()
{
    /*
     * Test for presence of ASID2 and three feature bits enabled by it:
     * https://developer.arm.com/documentation/109697/2025_09/Feature-descriptions/The-Armv9-5-architecture-extension
     * Bits added are FNG1, FNG0, and A2. These should be RES0 if A2 is
     * not enabled and read as the written value if A2 is enabled.
     */

    uint64_t out;
    uint64_t idreg3;
    uint64_t idreg4;
    int tcr2_present;
    int asid2_present;

    /* Mask is FNG1, FNG0, and A2 */
    const uint64_t feature_mask = (1ULL << 18 | 1ULL << 17 | 1ULL << 16);
    const uint64_t in = feature_mask;

    asm("mrs %[idreg3], " ID_AA64MMFR3_EL1 "\n\t"
        : [idreg3] "=r" (idreg3));

    tcr2_present = ((idreg3 & 0xF) != 0);

    if (!tcr2_present) {
        ml_printf("TCR2 is not present, cannot perform test");
        return 0;
    }

    asm("mrs %[idreg4], " ID_AA64MMFR4_EL1 "\n\t"
        : [idreg4] "=r" (idreg4));

    asid2_present = ((idreg4 & 0xF00) != 0);

    asm("msr " TCR2_EL1 ", %[x0]\n\t"
        "mrs %[x1], " TCR2_EL1 "\n\t"
        : [x1] "=r" (out)
        : [x0] "r" (in));

    if (asid2_present) {
        if ((out & feature_mask) == in) {
            ml_printf("OK\n");
            return 0;
        } else {
            ml_printf("FAIL: ASID2 present, but read value %lx != "
                      "written value %lx\n",
                      out & feature_mask, in);
            return 1;
        }
    } else {
        if (out == 0) {
            ml_printf("TCR2_EL1 reads as RES0 as expected\n");
            return 0;
        } else {
            ml_printf("FAIL: ASID2, missing but read value %lx != 0\n",
                      out & feature_mask, in);
            return 1;
        }
    }
}
