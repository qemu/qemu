/*
 * Tiny Code Generator for QEMU
 *
 * Copyright (c) 2008 Fabrice Bellard
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

#ifndef PPC_TCG_TARGET_H
#define PPC_TCG_TARGET_H

#ifdef _ARCH_PPC64
# define TCG_TARGET_REG_BITS  64
#else
# define TCG_TARGET_REG_BITS  32
#endif
#define MAX_CODE_GEN_BUFFER_SIZE  ((size_t)-1)

#define TCG_TARGET_NB_REGS 64
#define TCG_TARGET_INSN_UNIT_SIZE 4
#define TCG_TARGET_TLB_DISPLACEMENT_BITS 16

typedef enum {
    TCG_REG_R0,  TCG_REG_R1,  TCG_REG_R2,  TCG_REG_R3,
    TCG_REG_R4,  TCG_REG_R5,  TCG_REG_R6,  TCG_REG_R7,
    TCG_REG_R8,  TCG_REG_R9,  TCG_REG_R10, TCG_REG_R11,
    TCG_REG_R12, TCG_REG_R13, TCG_REG_R14, TCG_REG_R15,
    TCG_REG_R16, TCG_REG_R17, TCG_REG_R18, TCG_REG_R19,
    TCG_REG_R20, TCG_REG_R21, TCG_REG_R22, TCG_REG_R23,
    TCG_REG_R24, TCG_REG_R25, TCG_REG_R26, TCG_REG_R27,
    TCG_REG_R28, TCG_REG_R29, TCG_REG_R30, TCG_REG_R31,

    TCG_REG_V0,  TCG_REG_V1,  TCG_REG_V2,  TCG_REG_V3,
    TCG_REG_V4,  TCG_REG_V5,  TCG_REG_V6,  TCG_REG_V7,
    TCG_REG_V8,  TCG_REG_V9,  TCG_REG_V10, TCG_REG_V11,
    TCG_REG_V12, TCG_REG_V13, TCG_REG_V14, TCG_REG_V15,
    TCG_REG_V16, TCG_REG_V17, TCG_REG_V18, TCG_REG_V19,
    TCG_REG_V20, TCG_REG_V21, TCG_REG_V22, TCG_REG_V23,
    TCG_REG_V24, TCG_REG_V25, TCG_REG_V26, TCG_REG_V27,
    TCG_REG_V28, TCG_REG_V29, TCG_REG_V30, TCG_REG_V31,

    TCG_REG_CALL_STACK = TCG_REG_R1,
    TCG_AREG0 = TCG_REG_R27
} TCGReg;

typedef enum {
    tcg_isa_base,
    tcg_isa_2_06,
    tcg_isa_2_07,
    tcg_isa_3_00,
    tcg_isa_3_10,
} TCGPowerISA;

extern TCGPowerISA have_isa;
extern bool have_altivec;
extern bool have_vsx;

#define have_isa_2_06  (have_isa >= tcg_isa_2_06)
#define have_isa_2_07  (have_isa >= tcg_isa_2_07)
#define have_isa_3_00  (have_isa >= tcg_isa_3_00)
#define have_isa_3_10  (have_isa >= tcg_isa_3_10)

/* optional instructions automatically implemented */
#define TCG_TARGET_HAS_ext8u_i32        0 /* andi */
#define TCG_TARGET_HAS_ext16u_i32       0

/* optional instructions */
#define TCG_TARGET_HAS_div_i32          1
#define TCG_TARGET_HAS_rem_i32          have_isa_3_00
#define TCG_TARGET_HAS_rot_i32          1
#define TCG_TARGET_HAS_ext8s_i32        1
#define TCG_TARGET_HAS_ext16s_i32       1
#define TCG_TARGET_HAS_bswap16_i32      1
#define TCG_TARGET_HAS_bswap32_i32      1
#define TCG_TARGET_HAS_not_i32          1
#define TCG_TARGET_HAS_neg_i32          1
#define TCG_TARGET_HAS_andc_i32         1
#define TCG_TARGET_HAS_orc_i32          1
#define TCG_TARGET_HAS_eqv_i32          1
#define TCG_TARGET_HAS_nand_i32         1
#define TCG_TARGET_HAS_nor_i32          1
#define TCG_TARGET_HAS_clz_i32          1
#define TCG_TARGET_HAS_ctz_i32          have_isa_3_00
#define TCG_TARGET_HAS_ctpop_i32        have_isa_2_06
#define TCG_TARGET_HAS_deposit_i32      1
#define TCG_TARGET_HAS_extract_i32      1
#define TCG_TARGET_HAS_sextract_i32     0
#define TCG_TARGET_HAS_extract2_i32     0
#define TCG_TARGET_HAS_movcond_i32      1
#define TCG_TARGET_HAS_mulu2_i32        0
#define TCG_TARGET_HAS_muls2_i32        0
#define TCG_TARGET_HAS_muluh_i32        1
#define TCG_TARGET_HAS_mulsh_i32        1
#define TCG_TARGET_HAS_qemu_st8_i32     0

#if TCG_TARGET_REG_BITS == 64
#define TCG_TARGET_HAS_add2_i32         0
#define TCG_TARGET_HAS_sub2_i32         0
#define TCG_TARGET_HAS_extrl_i64_i32    0
#define TCG_TARGET_HAS_extrh_i64_i32    0
#define TCG_TARGET_HAS_div_i64          1
#define TCG_TARGET_HAS_rem_i64          have_isa_3_00
#define TCG_TARGET_HAS_rot_i64          1
#define TCG_TARGET_HAS_ext8s_i64        1
#define TCG_TARGET_HAS_ext16s_i64       1
#define TCG_TARGET_HAS_ext32s_i64       1
#define TCG_TARGET_HAS_ext8u_i64        0
#define TCG_TARGET_HAS_ext16u_i64       0
#define TCG_TARGET_HAS_ext32u_i64       0
#define TCG_TARGET_HAS_bswap16_i64      1
#define TCG_TARGET_HAS_bswap32_i64      1
#define TCG_TARGET_HAS_bswap64_i64      1
#define TCG_TARGET_HAS_not_i64          1
#define TCG_TARGET_HAS_neg_i64          1
#define TCG_TARGET_HAS_andc_i64         1
#define TCG_TARGET_HAS_orc_i64          1
#define TCG_TARGET_HAS_eqv_i64          1
#define TCG_TARGET_HAS_nand_i64         1
#define TCG_TARGET_HAS_nor_i64          1
#define TCG_TARGET_HAS_clz_i64          1
#define TCG_TARGET_HAS_ctz_i64          have_isa_3_00
#define TCG_TARGET_HAS_ctpop_i64        have_isa_2_06
#define TCG_TARGET_HAS_deposit_i64      1
#define TCG_TARGET_HAS_extract_i64      1
#define TCG_TARGET_HAS_sextract_i64     0
#define TCG_TARGET_HAS_extract2_i64     0
#define TCG_TARGET_HAS_movcond_i64      1
#define TCG_TARGET_HAS_add2_i64         1
#define TCG_TARGET_HAS_sub2_i64         1
#define TCG_TARGET_HAS_mulu2_i64        0
#define TCG_TARGET_HAS_muls2_i64        0
#define TCG_TARGET_HAS_muluh_i64        1
#define TCG_TARGET_HAS_mulsh_i64        1
#endif

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
#define TCG_TARGET_HAS_cmpsel_vec       0

#define TCG_TARGET_DEFAULT_MO (0)
#define TCG_TARGET_NEED_LDST_LABELS
#define TCG_TARGET_NEED_POOL_LABELS

#endif
