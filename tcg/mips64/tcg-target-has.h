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
#define TCG_TARGET_HAS_extr_i64_i32     1
#define TCG_TARGET_HAS_ext32s_i64       1
#define TCG_TARGET_HAS_ext32u_i64       1

/* optional instructions detected at runtime */
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
