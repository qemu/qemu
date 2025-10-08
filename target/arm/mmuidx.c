/*
 * QEMU Arm software mmu index definitions
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "mmuidx-internal.h"


#define EL(X)  ((X << R_MMUIDXINFO_EL_SHIFT) | R_MMUIDXINFO_ELVALID_MASK)

const uint32_t arm_mmuidx_table[ARM_MMU_IDX_M + 8] = {
    /*
     * A-profile.
     */
    [ARMMMUIdx_E10_0]           = EL(0),
    [ARMMMUIdx_E10_1]           = EL(1),
    [ARMMMUIdx_E10_1_PAN]       = EL(1),

    [ARMMMUIdx_E20_0]           = EL(0),
    [ARMMMUIdx_E20_2]           = EL(2),
    [ARMMMUIdx_E20_2_PAN]       = EL(2),

    [ARMMMUIdx_E2]              = EL(2),

    [ARMMMUIdx_E3]              = EL(3),
    [ARMMMUIdx_E30_0]           = EL(0),
    [ARMMMUIdx_E30_3_PAN]       = EL(3),

    /*
     * M-profile.
     */
    [ARMMMUIdx_MUser]           = EL(0),
    [ARMMMUIdx_MPriv]           = EL(1),
    [ARMMMUIdx_MUserNegPri]     = EL(0),
    [ARMMMUIdx_MPrivNegPri]     = EL(1),
    [ARMMMUIdx_MSUser]          = EL(0),
    [ARMMMUIdx_MSPriv]          = EL(1),
    [ARMMMUIdx_MSUserNegPri]    = EL(0),
    [ARMMMUIdx_MSPrivNegPri]    = EL(1),
};
