/* SPDX-License-Identifier: MIT */
/*
 * Define target-specific opcode support
 * Copyright (c) 2008 Fabrice Bellard
 */

#ifndef TCG_TARGET_HAS_H
#define TCG_TARGET_HAS_H

#include "host/cpuinfo.h"

#define have_bmi1         (cpuinfo & CPUINFO_BMI1)
#define have_popcnt       (cpuinfo & CPUINFO_POPCNT)
#define have_avx1         (cpuinfo & CPUINFO_AVX1)
#define have_avx2         (cpuinfo & CPUINFO_AVX2)
#define have_movbe        (cpuinfo & CPUINFO_MOVBE)

/*
 * There are interesting instructions in AVX512, so long as we have AVX512VL,
 * which indicates support for EVEX on sizes smaller than 512 bits.
 */
#define have_avx512vl     ((cpuinfo & CPUINFO_AVX512VL) && \
                           (cpuinfo & CPUINFO_AVX512F))
#define have_avx512bw     ((cpuinfo & CPUINFO_AVX512BW) && have_avx512vl)
#define have_avx512dq     ((cpuinfo & CPUINFO_AVX512DQ) && have_avx512vl)
#define have_avx512vbmi2  ((cpuinfo & CPUINFO_AVX512VBMI2) && have_avx512vl)

/* optional instructions */
#define TCG_TARGET_HAS_div2_i32         1
#define TCG_TARGET_HAS_rot_i32          1
#define TCG_TARGET_HAS_ext8s_i32        1
#define TCG_TARGET_HAS_ext16s_i32       1
#define TCG_TARGET_HAS_ext8u_i32        1
#define TCG_TARGET_HAS_ext16u_i32       1
#define TCG_TARGET_HAS_bswap16_i32      1
#define TCG_TARGET_HAS_bswap32_i32      1
#define TCG_TARGET_HAS_not_i32          1
#define TCG_TARGET_HAS_andc_i32         have_bmi1
#define TCG_TARGET_HAS_orc_i32          0
#define TCG_TARGET_HAS_eqv_i32          0
#define TCG_TARGET_HAS_nand_i32         0
#define TCG_TARGET_HAS_nor_i32          0
#define TCG_TARGET_HAS_clz_i32          1
#define TCG_TARGET_HAS_ctz_i32          1
#define TCG_TARGET_HAS_ctpop_i32        have_popcnt
#define TCG_TARGET_HAS_extract2_i32     1
#define TCG_TARGET_HAS_negsetcond_i32   1
#define TCG_TARGET_HAS_add2_i32         1
#define TCG_TARGET_HAS_sub2_i32         1
#define TCG_TARGET_HAS_mulu2_i32        1
#define TCG_TARGET_HAS_muls2_i32        1
#define TCG_TARGET_HAS_muluh_i32        0
#define TCG_TARGET_HAS_mulsh_i32        0

#if TCG_TARGET_REG_BITS == 64
/* Keep 32-bit values zero-extended in a register.  */
#define TCG_TARGET_HAS_extr_i64_i32     1
#define TCG_TARGET_HAS_div2_i64         1
#define TCG_TARGET_HAS_rot_i64          1
#define TCG_TARGET_HAS_ext8s_i64        1
#define TCG_TARGET_HAS_ext16s_i64       1
#define TCG_TARGET_HAS_ext32s_i64       1
#define TCG_TARGET_HAS_ext8u_i64        1
#define TCG_TARGET_HAS_ext16u_i64       1
#define TCG_TARGET_HAS_ext32u_i64       1
#define TCG_TARGET_HAS_bswap16_i64      1
#define TCG_TARGET_HAS_bswap32_i64      1
#define TCG_TARGET_HAS_bswap64_i64      1
#define TCG_TARGET_HAS_not_i64          1
#define TCG_TARGET_HAS_andc_i64         have_bmi1
#define TCG_TARGET_HAS_orc_i64          0
#define TCG_TARGET_HAS_eqv_i64          0
#define TCG_TARGET_HAS_nand_i64         0
#define TCG_TARGET_HAS_nor_i64          0
#define TCG_TARGET_HAS_clz_i64          1
#define TCG_TARGET_HAS_ctz_i64          1
#define TCG_TARGET_HAS_ctpop_i64        have_popcnt
#define TCG_TARGET_HAS_extract2_i64     1
#define TCG_TARGET_HAS_negsetcond_i64   1
#define TCG_TARGET_HAS_add2_i64         1
#define TCG_TARGET_HAS_sub2_i64         1
#define TCG_TARGET_HAS_mulu2_i64        1
#define TCG_TARGET_HAS_muls2_i64        1
#define TCG_TARGET_HAS_muluh_i64        0
#define TCG_TARGET_HAS_mulsh_i64        0
#define TCG_TARGET_HAS_qemu_st8_i32     0
#else
#define TCG_TARGET_HAS_qemu_st8_i32     1
#endif

#define TCG_TARGET_HAS_qemu_ldst_i128 \
    (TCG_TARGET_REG_BITS == 64 && (cpuinfo & CPUINFO_ATOMIC_VMOVDQA))

#define TCG_TARGET_HAS_tst              1

/* We do not support older SSE systems, only beginning with AVX1.  */
#define TCG_TARGET_HAS_v64              have_avx1
#define TCG_TARGET_HAS_v128             have_avx1
#define TCG_TARGET_HAS_v256             have_avx2

#define TCG_TARGET_HAS_andc_vec         1
#define TCG_TARGET_HAS_orc_vec          have_avx512vl
#define TCG_TARGET_HAS_nand_vec         have_avx512vl
#define TCG_TARGET_HAS_nor_vec          have_avx512vl
#define TCG_TARGET_HAS_eqv_vec          have_avx512vl
#define TCG_TARGET_HAS_not_vec          have_avx512vl
#define TCG_TARGET_HAS_neg_vec          0
#define TCG_TARGET_HAS_abs_vec          1
#define TCG_TARGET_HAS_roti_vec         have_avx512vl
#define TCG_TARGET_HAS_rots_vec         0
#define TCG_TARGET_HAS_rotv_vec         have_avx512vl
#define TCG_TARGET_HAS_shi_vec          1
#define TCG_TARGET_HAS_shs_vec          1
#define TCG_TARGET_HAS_shv_vec          have_avx2
#define TCG_TARGET_HAS_mul_vec          1
#define TCG_TARGET_HAS_sat_vec          1
#define TCG_TARGET_HAS_minmax_vec       1
#define TCG_TARGET_HAS_bitsel_vec       have_avx512vl
#define TCG_TARGET_HAS_cmpsel_vec       1
#define TCG_TARGET_HAS_tst_vec          have_avx512bw

#define TCG_TARGET_deposit_valid(type, ofs, len) \
    (((ofs) == 0 && ((len) == 8 || (len) == 16)) || \
     (TCG_TARGET_REG_BITS == 32 && (ofs) == 8 && (len) == 8))

/*
 * Check for the possibility of low byte/word extraction, high-byte extraction
 * and zero-extending 32-bit right-shift.
 *
 * We cannot sign-extend from high byte to 64-bits without using the
 * REX prefix that explicitly excludes access to the high-byte registers.
 */
static inline bool
tcg_target_sextract_valid(TCGType type, unsigned ofs, unsigned len)
{
    switch (ofs) {
    case 0:
        switch (len) {
        case 8:
        case 16:
            return true;
        case 32:
            return type == TCG_TYPE_I64;
        }
        return false;
    case 8:
        return len == 8 && type == TCG_TYPE_I32;
    }
    return false;
}
#define TCG_TARGET_sextract_valid  tcg_target_sextract_valid

static inline bool
tcg_target_extract_valid(TCGType type, unsigned ofs, unsigned len)
{
    if (type == TCG_TYPE_I64 && ofs + len == 32) {
        return true;
    }
    switch (ofs) {
    case 0:
        return len == 8 || len == 16;
    case 8:
        return len == 8;
    }
    return false;
}
#define TCG_TARGET_extract_valid  tcg_target_extract_valid

#endif
