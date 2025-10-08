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

#endif /* TARGET_ARM_MMUIDX_INTERNAL_H */
