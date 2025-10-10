/*
 * QEMU Arm software mmu index definitions
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "mmuidx-internal.h"


#define EL(X)  ((X << R_MMUIDXINFO_EL_SHIFT) | R_MMUIDXINFO_ELVALID_MASK | \
                ((X == 0) << R_MMUIDXINFO_USER_SHIFT))
#define REL(X) ((X << R_MMUIDXINFO_REL_SHIFT) | R_MMUIDXINFO_RELVALID_MASK)
#define R2     R_MMUIDXINFO_2RANGES_MASK
#define PAN    R_MMUIDXINFO_PAN_MASK
#define USER   R_MMUIDXINFO_USER_MASK
#define S1     R_MMUIDXINFO_STAGE1_MASK
#define S2     R_MMUIDXINFO_STAGE2_MASK
#define GCS    R_MMUIDXINFO_GCS_MASK
#define TG(X)  \
    ((ARMMMUIdx_##X##_GCS & ARM_MMU_IDX_COREIDX_MASK) << R_MMUIDXINFO_TG_SHIFT)

const uint32_t arm_mmuidx_table[ARM_MMU_IDX_M + 8] = {
    /*
     * A-profile.
     */
    [ARMMMUIdx_E10_0]           = EL(0) | REL(1) | R2 | TG(E10_0),
    [ARMMMUIdx_E10_0_GCS]       = EL(0) | REL(1) | R2 | GCS,
    [ARMMMUIdx_E10_1]           = EL(1) | REL(1) | R2 | TG(E10_1),
    [ARMMMUIdx_E10_1_PAN]       = EL(1) | REL(1) | R2 | TG(E10_1) | PAN,
    [ARMMMUIdx_E10_1_GCS]       = EL(1) | REL(1) | R2 | GCS,

    [ARMMMUIdx_E20_0]           = EL(0) | REL(2) | R2 | TG(E20_0),
    [ARMMMUIdx_E20_0_GCS]       = EL(0) | REL(2) | R2 | GCS,
    [ARMMMUIdx_E20_2]           = EL(2) | REL(2) | R2 | TG(E20_2),
    [ARMMMUIdx_E20_2_PAN]       = EL(2) | REL(2) | R2 | TG(E20_2) | PAN,
    [ARMMMUIdx_E20_2_GCS]       = EL(2) | REL(2) | R2 | GCS,

    [ARMMMUIdx_E2]              = EL(2) | REL(2) | TG(E2),
    [ARMMMUIdx_E2_GCS]          = EL(2) | REL(2) | GCS,

    [ARMMMUIdx_E3]              = EL(3) | REL(3) | TG(E3),
    [ARMMMUIdx_E3_GCS]          = EL(3) | REL(3) | GCS,
    [ARMMMUIdx_E30_0]           = EL(0) | REL(3),
    [ARMMMUIdx_E30_3_PAN]       = EL(3) | REL(3) | PAN,

    [ARMMMUIdx_Stage2_S]        = REL(2) | S2,
    [ARMMMUIdx_Stage2]          = REL(2) | S2,

    [ARMMMUIdx_Stage1_E0]       = REL(1) | R2 | S1 | USER | TG(Stage1_E0),
    [ARMMMUIdx_Stage1_E0_GCS]   = REL(1) | R2 | S1 | USER | GCS,
    [ARMMMUIdx_Stage1_E1]       = REL(1) | R2 | S1 | TG(Stage1_E1),
    [ARMMMUIdx_Stage1_E1_PAN]   = REL(1) | R2 | S1 | TG(Stage1_E1) | PAN,
    [ARMMMUIdx_Stage1_E1_GCS]   = REL(1) | R2 | S1 | GCS,

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
