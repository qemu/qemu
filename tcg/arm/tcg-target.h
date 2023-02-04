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

extern int arm_arch;

#define use_armv7_instructions  (__ARM_ARCH >= 7 || arm_arch >= 7)

#define TCG_TARGET_INSN_UNIT_SIZE 4
#define TCG_TARGET_TLB_DISPLACEMENT_BITS 16
#define MAX_CODE_GEN_BUFFER_SIZE  UINT32_MAX

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

    TCG_REG_Q0,
    TCG_REG_Q1,
    TCG_REG_Q2,
    TCG_REG_Q3,
    TCG_REG_Q4,
    TCG_REG_Q5,
    TCG_REG_Q6,
    TCG_REG_Q7,
    TCG_REG_Q8,
    TCG_REG_Q9,
    TCG_REG_Q10,
    TCG_REG_Q11,
    TCG_REG_Q12,
    TCG_REG_Q13,
    TCG_REG_Q14,
    TCG_REG_Q15,

    TCG_AREG0 = TCG_REG_R6,
    TCG_REG_CALL_STACK = TCG_REG_R13,
} TCGReg;

#define TCG_TARGET_NB_REGS 32

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

/* used for function call generation */
#define TCG_TARGET_STACK_ALIGN		8
#define TCG_TARGET_CALL_STACK_OFFSET	0
#define TCG_TARGET_CALL_ARG_I32         TCG_CALL_ARG_NORMAL
#define TCG_TARGET_CALL_ARG_I64         TCG_CALL_ARG_EVEN
#define TCG_TARGET_CALL_ARG_I128        TCG_CALL_ARG_EVEN
#define TCG_TARGET_CALL_RET_I128        TCG_CALL_RET_BY_REF

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
#define TCG_TARGET_HAS_clz_i32          1
#define TCG_TARGET_HAS_ctz_i32          use_armv7_instructions
#define TCG_TARGET_HAS_ctpop_i32        0
#define TCG_TARGET_HAS_deposit_i32      use_armv7_instructions
#define TCG_TARGET_HAS_extract_i32      use_armv7_instructions
#define TCG_TARGET_HAS_sextract_i32     use_armv7_instructions
#define TCG_TARGET_HAS_extract2_i32     1
#define TCG_TARGET_HAS_movcond_i32      1
#define TCG_TARGET_HAS_mulu2_i32        1
#define TCG_TARGET_HAS_muls2_i32        1
#define TCG_TARGET_HAS_muluh_i32        0
#define TCG_TARGET_HAS_mulsh_i32        0
#define TCG_TARGET_HAS_div_i32          use_idiv_instructions
#define TCG_TARGET_HAS_rem_i32          0
#define TCG_TARGET_HAS_qemu_st8_i32     0

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

#define TCG_TARGET_DEFAULT_MO (0)
#define TCG_TARGET_HAS_MEMORY_BSWAP     0
#define TCG_TARGET_NEED_LDST_LABELS
#define TCG_TARGET_NEED_POOL_LABELS

#endif
