/* SPDX-License-Identifier: MIT */
/*
 * Define target-specific opcode support
 * Copyright (c) 2008 Fabrice Bellard
 */

#ifndef TCG_TARGET_HAS_H
#define TCG_TARGET_HAS_H

#include "host/cpuinfo.h"

#define have_isa_2_06  (cpuinfo & CPUINFO_V2_06)
#define have_isa_2_07  (cpuinfo & CPUINFO_V2_07)
#define have_isa_3_00  (cpuinfo & CPUINFO_V3_0)
#define have_isa_3_10  (cpuinfo & CPUINFO_V3_1)
#define have_altivec   (cpuinfo & CPUINFO_ALTIVEC)
#define have_vsx       (cpuinfo & CPUINFO_VSX)

/* optional instructions */
#define TCG_TARGET_HAS_extr_i64_i32     0

#define TCG_TARGET_HAS_qemu_ldst_i128   have_isa_2_07

#define TCG_TARGET_HAS_tst              1

/*
 * While technically Altivec could support V64, it has no 64-bit store
 * instruction and substituting two 32-bit stores makes the generated
 * code quite large.
 */
#define TCG_TARGET_HAS_v64              have_vsx
#define TCG_TARGET_HAS_v128             have_altivec
#define TCG_TARGET_HAS_v256             0

#define TCG_TARGET_HAS_andc_vec         1
#define TCG_TARGET_HAS_orc_vec          have_isa_2_07
#define TCG_TARGET_HAS_nand_vec         have_isa_2_07
#define TCG_TARGET_HAS_nor_vec          1
#define TCG_TARGET_HAS_eqv_vec          have_isa_2_07
#define TCG_TARGET_HAS_not_vec          1
#define TCG_TARGET_HAS_neg_vec          have_isa_3_00
#define TCG_TARGET_HAS_abs_vec          0
#define TCG_TARGET_HAS_roti_vec         0
#define TCG_TARGET_HAS_rots_vec         0
#define TCG_TARGET_HAS_rotv_vec         1
#define TCG_TARGET_HAS_shi_vec          0
#define TCG_TARGET_HAS_shs_vec          0
#define TCG_TARGET_HAS_shv_vec          1
#define TCG_TARGET_HAS_mul_vec          1
#define TCG_TARGET_HAS_sat_vec          1
#define TCG_TARGET_HAS_minmax_vec       1
#define TCG_TARGET_HAS_bitsel_vec       have_vsx
#define TCG_TARGET_HAS_cmpsel_vec       1
#define TCG_TARGET_HAS_tst_vec          0

#define TCG_TARGET_extract_valid(type, ofs, len)   1
#define TCG_TARGET_deposit_valid(type, ofs, len)   1

static inline bool
tcg_target_sextract_valid(TCGType type, unsigned ofs, unsigned len)
{
    if (type == TCG_TYPE_I64 && ofs + len == 32) {
        return true;
    }
    return ofs == 0 && (len == 8 || len == 16);
}
#define TCG_TARGET_sextract_valid  tcg_target_sextract_valid

#endif
