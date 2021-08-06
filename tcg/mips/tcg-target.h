/*
 * Tiny Code Generator for QEMU
 *
 * Copyright (c) 2008-2009 Arnaud Patard <arnaud.patard@rtp-net.org>
 * Copyright (c) 2009 Aurelien Jarno <aurelien@aurel32.net>
 * Based on i386/tcg-target.c - Copyright (c) 2008 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef MIPS_TCG_TARGET_H
#define MIPS_TCG_TARGET_H

#if _MIPS_SIM == _ABIO32
# define TCG_TARGET_REG_BITS 32
#elif _MIPS_SIM == _ABIN32 || _MIPS_SIM == _ABI64
# define TCG_TARGET_REG_BITS 64
#else
# error "Unknown ABI"
#endif

#define TCG_TARGET_INSN_UNIT_SIZE 4
#define TCG_TARGET_TLB_DISPLACEMENT_BITS 16
#define TCG_TARGET_NB_REGS 32

#define MAX_CODE_GEN_BUFFER_SIZE  ((size_t)-1)

typedef enum {
    TCG_REG_ZERO = 0,
    TCG_REG_AT,
    TCG_REG_V0,
    TCG_REG_V1,
    TCG_REG_A0,
    TCG_REG_A1,
    TCG_REG_A2,
    TCG_REG_A3,
    TCG_REG_T0,
    TCG_REG_T1,
    TCG_REG_T2,
    TCG_REG_T3,
    TCG_REG_T4,
    TCG_REG_T5,
    TCG_REG_T6,
    TCG_REG_T7,
    TCG_REG_S0,
    TCG_REG_S1,
    TCG_REG_S2,
    TCG_REG_S3,
    TCG_REG_S4,
    TCG_REG_S5,
    TCG_REG_S6,
    TCG_REG_S7,
    TCG_REG_T8,
    TCG_REG_T9,
    TCG_REG_K0,
    TCG_REG_K1,
    TCG_REG_GP,
    TCG_REG_SP,
    TCG_REG_S8,
    TCG_REG_RA,

    TCG_REG_CALL_STACK = TCG_REG_SP,
    TCG_AREG0 = TCG_REG_S0,
} TCGReg;

/* used for function call generation */
#define TCG_TARGET_STACK_ALIGN        16
#if _MIPS_SIM == _ABIO32
# define TCG_TARGET_CALL_STACK_OFFSET 16
#else
# define TCG_TARGET_CALL_STACK_OFFSET 0
#endif
#define TCG_TARGET_CALL_ALIGN_ARGS    1

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
#define TCG_TARGET_HAS_bswap32_i32      1
#define TCG_TARGET_HAS_direct_jump      0

#if TCG_TARGET_REG_BITS == 64
#define TCG_TARGET_HAS_add2_i32         0
#define TCG_TARGET_HAS_sub2_i32         0
#define TCG_TARGET_HAS_extrl_i64_i32    1
#define TCG_TARGET_HAS_extrh_i64_i32    1
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
#endif

/* optional instructions detected at runtime */
#define TCG_TARGET_HAS_movcond_i32      use_movnz_instructions
#define TCG_TARGET_HAS_bswap16_i32      use_mips32r2_instructions
#define TCG_TARGET_HAS_deposit_i32      use_mips32r2_instructions
#define TCG_TARGET_HAS_extract_i32      use_mips32r2_instructions
#define TCG_TARGET_HAS_sextract_i32     0
#define TCG_TARGET_HAS_extract2_i32     0
#define TCG_TARGET_HAS_ext8s_i32        use_mips32r2_instructions
#define TCG_TARGET_HAS_ext16s_i32       use_mips32r2_instructions
#define TCG_TARGET_HAS_rot_i32          use_mips32r2_instructions
#define TCG_TARGET_HAS_clz_i32          use_mips32r2_instructions
#define TCG_TARGET_HAS_ctz_i32          0
#define TCG_TARGET_HAS_ctpop_i32        0
#define TCG_TARGET_HAS_qemu_st8_i32     0

#if TCG_TARGET_REG_BITS == 64
#define TCG_TARGET_HAS_movcond_i64      use_movnz_instructions
#define TCG_TARGET_HAS_bswap16_i64      use_mips32r2_instructions
#define TCG_TARGET_HAS_bswap32_i64      use_mips32r2_instructions
#define TCG_TARGET_HAS_bswap64_i64      use_mips32r2_instructions
#define TCG_TARGET_HAS_deposit_i64      use_mips32r2_instructions
#define TCG_TARGET_HAS_extract_i64      use_mips32r2_instructions
#define TCG_TARGET_HAS_sextract_i64     0
#define TCG_TARGET_HAS_extract2_i64     0
#define TCG_TARGET_HAS_ext8s_i64        use_mips32r2_instructions
#define TCG_TARGET_HAS_ext16s_i64       use_mips32r2_instructions
#define TCG_TARGET_HAS_rot_i64          use_mips32r2_instructions
#define TCG_TARGET_HAS_clz_i64          use_mips32r2_instructions
#define TCG_TARGET_HAS_ctz_i64          0
#define TCG_TARGET_HAS_ctpop_i64        0
#endif

/* optional instructions automatically implemented */
#define TCG_TARGET_HAS_neg_i32          0 /* sub  rd, zero, rt   */
#define TCG_TARGET_HAS_ext8u_i32        0 /* andi rt, rs, 0xff   */
#define TCG_TARGET_HAS_ext16u_i32       0 /* andi rt, rs, 0xffff */

#if TCG_TARGET_REG_BITS == 64
#define TCG_TARGET_HAS_neg_i64          0 /* sub  rd, zero, rt   */
#define TCG_TARGET_HAS_ext8u_i64        0 /* andi rt, rs, 0xff   */
#define TCG_TARGET_HAS_ext16u_i64       0 /* andi rt, rs, 0xffff */
#endif

#define TCG_TARGET_DEFAULT_MO (0)
#define TCG_TARGET_HAS_MEMORY_BSWAP     1

/* not defined -- call should be eliminated at compile time */
void tb_target_set_jmp_target(uintptr_t, uintptr_t, uintptr_t, uintptr_t)
    QEMU_ERROR("code path is reachable");

#ifdef CONFIG_SOFTMMU
#define TCG_TARGET_NEED_LDST_LABELS
#endif

#endif
