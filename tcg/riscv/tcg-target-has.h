/* SPDX-License-Identifier: MIT */
/*
 * Define target-specific opcode support
 * Copyright (c) 2018 SiFive, Inc
 */

#ifndef TCG_TARGET_HAS_H
#define TCG_TARGET_HAS_H

#include "host/cpuinfo.h"

/* optional instructions */
#define TCG_TARGET_HAS_extr_i64_i32     1
#define TCG_TARGET_HAS_qemu_ldst_i128   0
#define TCG_TARGET_HAS_tst              0

/* vector instructions */
#define TCG_TARGET_HAS_v64              (cpuinfo & CPUINFO_ZVE64X)
#define TCG_TARGET_HAS_v128             (cpuinfo & CPUINFO_ZVE64X)
#define TCG_TARGET_HAS_v256             (cpuinfo & CPUINFO_ZVE64X)
#define TCG_TARGET_HAS_andc_vec         0
#define TCG_TARGET_HAS_orc_vec          0
#define TCG_TARGET_HAS_nand_vec         0
#define TCG_TARGET_HAS_nor_vec          0
#define TCG_TARGET_HAS_eqv_vec          0
#define TCG_TARGET_HAS_not_vec          1
#define TCG_TARGET_HAS_neg_vec          1
#define TCG_TARGET_HAS_abs_vec          0
#define TCG_TARGET_HAS_roti_vec         1
#define TCG_TARGET_HAS_rots_vec         1
#define TCG_TARGET_HAS_rotv_vec         1
#define TCG_TARGET_HAS_shi_vec          1
#define TCG_TARGET_HAS_shs_vec          1
#define TCG_TARGET_HAS_shv_vec          1
#define TCG_TARGET_HAS_mul_vec          1
#define TCG_TARGET_HAS_sat_vec          1
#define TCG_TARGET_HAS_minmax_vec       1
#define TCG_TARGET_HAS_bitsel_vec       0
#define TCG_TARGET_HAS_cmpsel_vec       1

#define TCG_TARGET_HAS_tst_vec          0

static inline bool
tcg_target_extract_valid(TCGType type, unsigned ofs, unsigned len)
{
    if (type == TCG_TYPE_I64 && ofs + len == 32) {
        /* ofs > 0 uses SRLIW; ofs == 0 uses add.uw. */
        return ofs || (cpuinfo & CPUINFO_ZBA);
    }
    switch (len) {
    case 1:
        return (cpuinfo & CPUINFO_ZBS) && ofs != 0;
    case 16:
        return (cpuinfo & CPUINFO_ZBB) && ofs == 0;
    }
    return false;
}
#define TCG_TARGET_extract_valid  tcg_target_extract_valid

static inline bool
tcg_target_sextract_valid(TCGType type, unsigned ofs, unsigned len)
{
    if (type == TCG_TYPE_I64 && ofs + len == 32) {
        return true;
    }
    return (cpuinfo & CPUINFO_ZBB) && ofs == 0 && (len == 8 || len == 16);
}
#define TCG_TARGET_sextract_valid  tcg_target_sextract_valid

#define TCG_TARGET_deposit_valid(type, ofs, len)  0

#endif
