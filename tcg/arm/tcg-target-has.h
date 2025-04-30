/* SPDX-License-Identifier: MIT */
/*
 * Define target-specific opcode support
 * Copyright (c) 2008 Fabrice Bellard
 * Copyright (c) 2008 Andrzej Zaborowski
 */

#ifndef TCG_TARGET_HAS_H
#define TCG_TARGET_HAS_H

extern int arm_arch;

#define use_armv7_instructions  (__ARM_ARCH >= 7 || arm_arch >= 7)

#ifdef __ARM_ARCH_EXT_IDIV__
#define use_idiv_instructions  1
#else
extern bool use_idiv_instructions;
#endif
#ifdef __ARM_NEON__
#define use_neon_instructions  1
#else
extern bool use_neon_instructions;
#endif

/* optional instructions */
#define TCG_TARGET_HAS_qemu_ldst_i128   0
#define TCG_TARGET_HAS_tst              1

#define TCG_TARGET_HAS_v64              use_neon_instructions
#define TCG_TARGET_HAS_v128             use_neon_instructions
#define TCG_TARGET_HAS_v256             0

#define TCG_TARGET_HAS_andc_vec         1
#define TCG_TARGET_HAS_orc_vec          1
#define TCG_TARGET_HAS_nand_vec         0
#define TCG_TARGET_HAS_nor_vec          0
#define TCG_TARGET_HAS_eqv_vec          0
#define TCG_TARGET_HAS_not_vec          1
#define TCG_TARGET_HAS_neg_vec          1
#define TCG_TARGET_HAS_abs_vec          1
#define TCG_TARGET_HAS_roti_vec         0
#define TCG_TARGET_HAS_rots_vec         0
#define TCG_TARGET_HAS_rotv_vec         0
#define TCG_TARGET_HAS_shi_vec          1
#define TCG_TARGET_HAS_shs_vec          0
#define TCG_TARGET_HAS_shv_vec          0
#define TCG_TARGET_HAS_mul_vec          1
#define TCG_TARGET_HAS_sat_vec          1
#define TCG_TARGET_HAS_minmax_vec       1
#define TCG_TARGET_HAS_bitsel_vec       1
#define TCG_TARGET_HAS_cmpsel_vec       0
#define TCG_TARGET_HAS_tst_vec          1

static inline bool
tcg_target_extract_valid(TCGType type, unsigned ofs, unsigned len)
{
    if (use_armv7_instructions) {
        return true;  /* SBFX or UBFX */
    }
    switch (len) {
    case 8:   /* SXTB or UXTB */
    case 16:  /* SXTH or UXTH */
        return (ofs % 8) == 0;
    }
    return false;
}

#define TCG_TARGET_extract_valid   tcg_target_extract_valid
#define TCG_TARGET_sextract_valid  tcg_target_extract_valid
#define TCG_TARGET_deposit_valid(type, ofs, len)  use_armv7_instructions

#endif
