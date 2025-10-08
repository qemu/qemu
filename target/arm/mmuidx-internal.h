/*
 * QEMU Arm software mmu index internal definitions
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef TARGET_ARM_MMUIDX_INTERNAL_H
#define TARGET_ARM_MMUIDX_INTERNAL_H

#include "mmuidx.h"
#include "tcg/debug-assert.h"
#include "hw/registerfields.h"


FIELD(MMUIDXINFO, EL, 0, 2)
FIELD(MMUIDXINFO, ELVALID, 2, 1)
FIELD(MMUIDXINFO, REL, 3, 2)
FIELD(MMUIDXINFO, RELVALID, 5, 1)
FIELD(MMUIDXINFO, 2RANGES, 6, 1)
FIELD(MMUIDXINFO, PAN, 7, 1)
FIELD(MMUIDXINFO, USER, 8, 1)
FIELD(MMUIDXINFO, STAGE1, 9, 1)
FIELD(MMUIDXINFO, STAGE2, 10, 1)
FIELD(MMUIDXINFO, GCS, 11, 1)
FIELD(MMUIDXINFO, TG, 12, 5)

extern const uint32_t arm_mmuidx_table[ARM_MMU_IDX_M + 8];

#define arm_mmuidx_is_valid(x)  ((unsigned)(x) < ARRAY_SIZE(arm_mmuidx_table))

/* Return the exception level associated with this mmu index. */
static inline int arm_mmu_idx_to_el(ARMMMUIdx idx)
{
    tcg_debug_assert(arm_mmuidx_is_valid(idx));
    tcg_debug_assert(FIELD_EX32(arm_mmuidx_table[idx], MMUIDXINFO, ELVALID));
    return FIELD_EX32(arm_mmuidx_table[idx], MMUIDXINFO, EL);
}

/*
 * Return the exception level for the address translation regime
 * associated with this mmu index.
 */
static inline uint32_t regime_el(ARMMMUIdx idx)
{
    tcg_debug_assert(arm_mmuidx_is_valid(idx));
    tcg_debug_assert(FIELD_EX32(arm_mmuidx_table[idx], MMUIDXINFO, RELVALID));
    return FIELD_EX32(arm_mmuidx_table[idx], MMUIDXINFO, REL);
}

/*
 * Return true if this address translation regime has two ranges.
 * Note that this will not return the correct answer for AArch32
 * Secure PL1&0 (i.e. mmu indexes E3, E30_0, E30_3_PAN), but it is
 * never called from a context where EL3 can be AArch32. (The
 * correct return value for ARMMMUIdx_E3 would be different for
 * that case, so we can't just make the function return the
 * correct value anyway; we would need an extra "bool e3_is_aarch32"
 * argument which all the current callsites would pass as 'false'.)
 */
static inline bool regime_has_2_ranges(ARMMMUIdx idx)
{
    tcg_debug_assert(arm_mmuidx_is_valid(idx));
    return FIELD_EX32(arm_mmuidx_table[idx], MMUIDXINFO, 2RANGES);
}

/* Return true if Privileged Access Never is enabled for this mmu index. */
static inline bool regime_is_pan(ARMMMUIdx idx)
{
    tcg_debug_assert(arm_mmuidx_is_valid(idx));
    return FIELD_EX32(arm_mmuidx_table[idx], MMUIDXINFO, PAN);
}

/*
 * Return true if the exception level associated with this mmu index is 0.
 * Differs from arm_mmu_idx_to_el(idx) == 0 in that this allows querying
 * Stage1 and Stage2 mmu indexes.
 */
static inline bool regime_is_user(ARMMMUIdx idx)
{
    tcg_debug_assert(arm_mmuidx_is_valid(idx));
    return FIELD_EX32(arm_mmuidx_table[idx], MMUIDXINFO, USER);
}

/* Return true if this mmu index is stage 1 of a 2-stage translation. */
static inline bool arm_mmu_idx_is_stage1_of_2(ARMMMUIdx idx)
{
    tcg_debug_assert(arm_mmuidx_is_valid(idx));
    return FIELD_EX32(arm_mmuidx_table[idx], MMUIDXINFO, STAGE1);
}

/* Return true if this mmu index is stage 2 of a 2-stage translation. */
static inline bool regime_is_stage2(ARMMMUIdx idx)
{
    tcg_debug_assert(arm_mmuidx_is_valid(idx));
    return FIELD_EX32(arm_mmuidx_table[idx], MMUIDXINFO, STAGE2);
}

/* Return true if this mmu index implies AccessType_GCS. */
static inline bool regime_is_gcs(ARMMMUIdx idx)
{
    tcg_debug_assert(arm_mmuidx_is_valid(idx));
    return FIELD_EX32(arm_mmuidx_table[idx], MMUIDXINFO, GCS);
}

/* Return the GCS MMUIdx for a given regime. */
static inline ARMMMUIdx regime_to_gcs(ARMMMUIdx idx)
{
    tcg_debug_assert(arm_mmuidx_is_valid(idx));
    uint32_t core = FIELD_EX32(arm_mmuidx_table[idx], MMUIDXINFO, TG);
    tcg_debug_assert(core != 0); /* core 0 is E10_0, not a GCS index */
    return core | ARM_MMU_IDX_A;
}

#endif /* TARGET_ARM_MMUIDX_INTERNAL_H */
