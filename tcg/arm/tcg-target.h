/*
 * Tiny Code Generator for QEMU
 *
 * Copyright (c) 2008 Fabrice Bellard
 * Copyright (c) 2008 Andrzej Zaborowski
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

#ifndef ARM_TCG_TARGET_H
#define ARM_TCG_TARGET_H

/* The __ARM_ARCH define is provided by gcc 4.8.  Construct it otherwise.  */
#ifndef __ARM_ARCH
# if defined(__ARM_ARCH_7__) || defined(__ARM_ARCH_7A__) \
     || defined(__ARM_ARCH_7R__) || defined(__ARM_ARCH_7M__) \
     || defined(__ARM_ARCH_7EM__)
#  define __ARM_ARCH 7
# elif defined(__ARM_ARCH_6__) || defined(__ARM_ARCH_6J__) \
       || defined(__ARM_ARCH_6Z__) || defined(__ARM_ARCH_6ZK__) \
       || defined(__ARM_ARCH_6K__) || defined(__ARM_ARCH_6T2__)
#  define __ARM_ARCH 6
# elif defined(__ARM_ARCH_5__) || defined(__ARM_ARCH_5E__) \
       || defined(__ARM_ARCH_5T__) || defined(__ARM_ARCH_5TE__) \
       || defined(__ARM_ARCH_5TEJ__)
#  define __ARM_ARCH 5
# else
#  define __ARM_ARCH 4
# endif
#endif

extern int arm_arch;

#if defined(__ARM_ARCH_5T__) \
    || defined(__ARM_ARCH_5TE__) || defined(__ARM_ARCH_5TEJ__)
# define use_armv5t_instructions 1
#else
# define use_armv5t_instructions use_armv6_instructions
#endif

#define use_armv6_instructions  (__ARM_ARCH >= 6 || arm_arch >= 6)
#define use_armv7_instructions  (__ARM_ARCH >= 7 || arm_arch >= 7)

#undef TCG_TARGET_STACK_GROWSUP
#define TCG_TARGET_INSN_UNIT_SIZE 4
#define TCG_TARGET_TLB_DISPLACEMENT_BITS 16

typedef enum {
    TCG_REG_R0 = 0,
    TCG_REG_R1,
    TCG_REG_R2,
    TCG_REG_R3,
    TCG_REG_R4,
    TCG_REG_R5,
    TCG_REG_R6,
    TCG_REG_R7,
    TCG_REG_R8,
    TCG_REG_R9,
    TCG_REG_R10,
    TCG_REG_R11,
    TCG_REG_R12,
    TCG_REG_R13,
    TCG_REG_R14,
    TCG_REG_PC,
} TCGReg;

#define TCG_TARGET_NB_REGS 16

#ifdef __ARM_ARCH_EXT_IDIV__
#define use_idiv_instructions  1
#else
extern bool use_idiv_instructions;
#endif


/* used for function call generation */
#define TCG_REG_CALL_STACK		TCG_REG_R13
#define TCG_TARGET_STACK_ALIGN		8
#define TCG_TARGET_CALL_ALIGN_ARGS	1
#define TCG_TARGET_CALL_STACK_OFFSET	0

/* optional instructions */
#define TCG_TARGET_HAS_ext8s_i32        1
#define TCG_TARGET_HAS_ext16s_i32       1
#define TCG_TARGET_HAS_ext8u_i32        0 /* and r0, r1, #0xff */
#define TCG_TARGET_HAS_ext16u_i32       1
#define TCG_TARGET_HAS_bswap16_i32      1
#define TCG_TARGET_HAS_bswap32_i32      1
#define TCG_TARGET_HAS_not_i32          1
#define TCG_TARGET_HAS_neg_i32          1
#define TCG_TARGET_HAS_rot_i32          1
#define TCG_TARGET_HAS_andc_i32         1
#define TCG_TARGET_HAS_orc_i32          0
#define TCG_TARGET_HAS_eqv_i32          0
#define TCG_TARGET_HAS_nand_i32         0
#define TCG_TARGET_HAS_nor_i32          0
#define TCG_TARGET_HAS_clz_i32          use_armv5t_instructions
#define TCG_TARGET_HAS_ctz_i32          use_armv7_instructions
#define TCG_TARGET_HAS_ctpop_i32        0
#define TCG_TARGET_HAS_deposit_i32      use_armv7_instructions
#define TCG_TARGET_HAS_extract_i32      use_armv7_instructions
#define TCG_TARGET_HAS_sextract_i32     use_armv7_instructions
#define TCG_TARGET_HAS_movcond_i32      1
#define TCG_TARGET_HAS_mulu2_i32        1
#define TCG_TARGET_HAS_muls2_i32        1
#define TCG_TARGET_HAS_muluh_i32        0
#define TCG_TARGET_HAS_mulsh_i32        0
#define TCG_TARGET_HAS_div_i32          use_idiv_instructions
#define TCG_TARGET_HAS_rem_i32          0
#define TCG_TARGET_HAS_goto_ptr         1

enum {
    TCG_AREG0 = TCG_REG_R6,
};

static inline void flush_icache_range(uintptr_t start, uintptr_t stop)
{
    __builtin___clear_cache((char *) start, (char *) stop);
}

#endif
