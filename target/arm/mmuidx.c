/*
 * QEMU Arm software mmu index definitions
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "mmuidx-internal.h"


#define EL(X)  ((X << R_MMUIDXINFO_EL_SHIFT) | R_MMUIDXINFO_ELVALID_MASK)
#define REL(X) ((X << R_MMUIDXINFO_REL_SHIFT) | R_MMUIDXINFO_RELVALID_MASK)

const uint32_t arm_mmuidx_table[ARM_MMU_IDX_M + 8] = {
    /*
     * A-profile.
     */
    [ARMMMUIdx_E10_0]           = EL(0) | REL(1),
    [ARMMMUIdx_E10_1]           = EL(1) | REL(1),
    [ARMMMUIdx_E10_1_PAN]       = EL(1) | REL(1),

    [ARMMMUIdx_E20_0]           = EL(0) | REL(2),
    [ARMMMUIdx_E20_2]           = EL(2) | REL(2),
    [ARMMMUIdx_E20_2_PAN]       = EL(2) | REL(2),

    [ARMMMUIdx_E2]              = EL(2) | REL(2),

    [ARMMMUIdx_E3]              = EL(3) | REL(3),
    [ARMMMUIdx_E30_0]           = EL(0) | REL(3),
    [ARMMMUIdx_E30_3_PAN]       = EL(3) | REL(3),

    [ARMMMUIdx_Stage2_S]        = REL(2),
    [ARMMMUIdx_Stage2]          = REL(2),

    [ARMMMUIdx_Stage1_E0]       = REL(1),
    [ARMMMUIdx_Stage1_E1]       = REL(1),
    [ARMMMUIdx_Stage1_E1_PAN]   = REL(1),

    /*
     * M-profile.
     */
    [ARMMMUIdx_MUser]           = EL(0) | REL(1),
    [ARMMMUIdx_MPriv]           = EL(1) | REL(1),
    [ARMMMUIdx_MUserNegPri]     = EL(0) | REL(1),
    [ARMMMUIdx_MPrivNegPri]     = EL(1) | REL(1),
    [ARMMMUIdx_MSUser]          = EL(0) | REL(1),
    [ARMMMUIdx_MSPriv]          = EL(1) | REL(1),
    [ARMMMUIdx_MSUserNegPri]    = EL(0) | REL(1),
    [ARMMMUIdx_MSPrivNegPri]    = EL(1) | REL(1),
};
