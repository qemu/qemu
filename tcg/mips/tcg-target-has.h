/* SPDX-License-Identifier: MIT */
/*
 * Define target-specific opcode support
 * Copyright (c) 2008-2009 Arnaud Patard <arnaud.patard@rtp-net.org>
 * Copyright (c) 2009 Aurelien Jarno <aurelien@aurel32.net>
 */

#ifndef TCG_TARGET_HAS_H
#define TCG_TARGET_HAS_H

/* MOVN/MOVZ instructions detection */
#if (defined(__mips_isa_rev) && (__mips_isa_rev >= 1)) || \
    defined(_MIPS_ARCH_LOONGSON2E) || defined(_MIPS_ARCH_LOONGSON2F) || \
    defined(_MIPS_ARCH_MIPS4)
#define use_movnz_instructions  1
#else
extern bool use_movnz_instructions;
#endif

/* MIPS32 instruction set detection */
#if defined(__mips_isa_rev) && (__mips_isa_rev >= 1)
#define use_mips32_instructions  1
#else
extern bool use_mips32_instructions;
#endif

/* MIPS32R2 instruction set detection */
#if defined(__mips_isa_rev) && (__mips_isa_rev >= 2)
#define use_mips32r2_instructions  1
#else
extern bool use_mips32r2_instructions;
#endif

/* MIPS32R6 instruction set detection */
#if defined(__mips_isa_rev) && (__mips_isa_rev >= 6)
#define use_mips32r6_instructions  1
#else
#define use_mips32r6_instructions  0
#endif

/* optional instructions */
#define TCG_TARGET_HAS_div_i32          1
#define TCG_TARGET_HAS_rem_i32          1
#define TCG_TARGET_HAS_not_i32          1
#define TCG_TARGET_HAS_nor_i32          1
#define TCG_TARGET_HAS_andc_i32         0
#define TCG_TARGET_HAS_orc_i32          0
#define TCG_TARGET_HAS_eqv_i32          0
#define TCG_TARGET_HAS_nand_i32         0
#define TCG_TARGET_HAS_mulu2_i32        (!use_mips32r6_instructions)
#define TCG_TARGET_HAS_muls2_i32        (!use_mips32r6_instructions)
#define TCG_TARGET_HAS_muluh_i32        1
#define TCG_TARGET_HAS_mulsh_i32        1
#define TCG_TARGET_HAS_bswap16_i32      1
#define TCG_TARGET_HAS_bswap32_i32      1
#define TCG_TARGET_HAS_negsetcond_i32   0

#if TCG_TARGET_REG_BITS == 64
#define TCG_TARGET_HAS_add2_i32         0
#define TCG_TARGET_HAS_sub2_i32         0
#define TCG_TARGET_HAS_extr_i64_i32     1
#define TCG_TARGET_HAS_div_i64          1
#define TCG_TARGET_HAS_rem_i64          1
#define TCG_TARGET_HAS_not_i64          1
#define TCG_TARGET_HAS_nor_i64          1
#define TCG_TARGET_HAS_andc_i64         0
#define TCG_TARGET_HAS_orc_i64          0
#define TCG_TARGET_HAS_eqv_i64          0
#define TCG_TARGET_HAS_nand_i64         0
#define TCG_TARGET_HAS_add2_i64         0
#define TCG_TARGET_HAS_sub2_i64         0
#define TCG_TARGET_HAS_mulu2_i64        (!use_mips32r6_instructions)
#define TCG_TARGET_HAS_muls2_i64        (!use_mips32r6_instructions)
#define TCG_TARGET_HAS_muluh_i64        1
#define TCG_TARGET_HAS_mulsh_i64        1
#define TCG_TARGET_HAS_ext32s_i64       1
#define TCG_TARGET_HAS_ext32u_i64       1
#define TCG_TARGET_HAS_negsetcond_i64   0
#endif

/* optional instructions detected at runtime */
#define TCG_TARGET_HAS_extract2_i32     0
#define TCG_TARGET_HAS_ext8s_i32        use_mips32r2_instructions
#define TCG_TARGET_HAS_ext16s_i32       use_mips32r2_instructions
#define TCG_TARGET_HAS_rot_i32          use_mips32r2_instructions
#define TCG_TARGET_HAS_clz_i32          use_mips32r2_instructions
#define TCG_TARGET_HAS_ctz_i32          0
#define TCG_TARGET_HAS_ctpop_i32        0
#define TCG_TARGET_HAS_qemu_st8_i32     0

#if TCG_TARGET_REG_BITS == 64
#define TCG_TARGET_HAS_bswap16_i64      1
#define TCG_TARGET_HAS_bswap32_i64      1
#define TCG_TARGET_HAS_bswap64_i64      1
#define TCG_TARGET_HAS_extract2_i64     0
#define TCG_TARGET_HAS_ext8s_i64        use_mips32r2_instructions
#define TCG_TARGET_HAS_ext16s_i64       use_mips32r2_instructions
#define TCG_TARGET_HAS_rot_i64          use_mips32r2_instructions
#define TCG_TARGET_HAS_clz_i64          use_mips32r2_instructions
#define TCG_TARGET_HAS_ctz_i64          0
#define TCG_TARGET_HAS_ctpop_i64        0
#endif

/* optional instructions automatically implemented */
#define TCG_TARGET_HAS_ext8u_i32        0 /* andi rt, rs, 0xff   */
#define TCG_TARGET_HAS_ext16u_i32       0 /* andi rt, rs, 0xffff */

#if TCG_TARGET_REG_BITS == 64
#define TCG_TARGET_HAS_ext8u_i64        0 /* andi rt, rs, 0xff   */
#define TCG_TARGET_HAS_ext16u_i64       0 /* andi rt, rs, 0xffff */
#endif

#define TCG_TARGET_HAS_qemu_ldst_i128   0
#define TCG_TARGET_HAS_tst              0

#define TCG_TARGET_extract_valid(type, ofs, len)  use_mips32r2_instructions
#define TCG_TARGET_deposit_valid(type, ofs, len)  use_mips32r2_instructions

static inline bool
tcg_target_sextract_valid(TCGType type, unsigned ofs, unsigned len)
{
    if (ofs == 0) {
        switch (len) {
        case 8:
        case 16:
            return use_mips32r2_instructions;
        case 32:
            return type == TCG_TYPE_I64;
        }
    }
    return false;
}
#define TCG_TARGET_sextract_valid  tcg_target_sextract_valid

#endif
