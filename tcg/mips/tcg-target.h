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
#ifndef TCG_TARGET_MIPS 
#define TCG_TARGET_MIPS 1

#ifdef __MIPSEB__
# define TCG_TARGET_WORDS_BIGENDIAN
#endif

#define TCG_TARGET_NB_REGS 32

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
    TCG_REG_FP,
    TCG_REG_RA,
} TCGReg;

#define TCG_CT_CONST_ZERO 0x100
#define TCG_CT_CONST_U16  0x200
#define TCG_CT_CONST_S16  0x400

/* used for function call generation */
#define TCG_REG_CALL_STACK TCG_REG_SP
#define TCG_TARGET_STACK_ALIGN 8
#define TCG_TARGET_CALL_STACK_OFFSET 16
#define TCG_TARGET_CALL_ALIGN_ARGS 1

/* optional instructions */
#define TCG_TARGET_HAS_div_i32          1
#define TCG_TARGET_HAS_not_i32          1
#define TCG_TARGET_HAS_nor_i32          1
#define TCG_TARGET_HAS_ext8s_i32        1
#define TCG_TARGET_HAS_ext16s_i32       1
#define TCG_TARGET_HAS_andc_i32         0
#define TCG_TARGET_HAS_orc_i32          0
#define TCG_TARGET_HAS_eqv_i32          0
#define TCG_TARGET_HAS_nand_i32         0
#define TCG_TARGET_HAS_muls2_i32        1

/* optional instructions only implemented on MIPS4, MIPS32 and Loongson 2 */
#if (defined(__mips_isa_rev) && (__mips_isa_rev >= 1)) || \
    defined(_MIPS_ARCH_LOONGSON2E) || defined(_MIPS_ARCH_LOONGSON2F) || \
    defined(_MIPS_ARCH_MIPS4)
#define TCG_TARGET_HAS_movcond_i32      1
#else
#define TCG_TARGET_HAS_movcond_i32      0
#endif

/* optional instructions only implemented on MIPS32R2 */
#if defined(__mips_isa_rev) && (__mips_isa_rev >= 2)
#define TCG_TARGET_HAS_bswap16_i32      1
#define TCG_TARGET_HAS_bswap32_i32      1
#define TCG_TARGET_HAS_rot_i32          1
#define TCG_TARGET_HAS_deposit_i32      1
#else
#define TCG_TARGET_HAS_bswap16_i32      0
#define TCG_TARGET_HAS_bswap32_i32      0
#define TCG_TARGET_HAS_rot_i32          0
#define TCG_TARGET_HAS_deposit_i32      0
#endif

/* optional instructions automatically implemented */
#define TCG_TARGET_HAS_neg_i32          0 /* sub  rd, zero, rt   */
#define TCG_TARGET_HAS_ext8u_i32        0 /* andi rt, rs, 0xff   */
#define TCG_TARGET_HAS_ext16u_i32       0 /* andi rt, rs, 0xffff */

#define TCG_AREG0 TCG_REG_S0

#ifdef __OpenBSD__
#include <machine/sysarch.h>
#else
#include <sys/cachectl.h>
#endif

static inline void flush_icache_range(tcg_target_ulong start,
                                      tcg_target_ulong stop)
{
    cacheflush ((void *)start, stop-start, ICACHE);
}

#endif
