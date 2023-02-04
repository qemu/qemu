/*
 * Tiny Code Generator for QEMU
 *
 * Copyright (c) 2009 Ulrich Hecht <uli@suse.de>
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

#ifndef S390_TCG_TARGET_H
#define S390_TCG_TARGET_H

#define TCG_TARGET_INSN_UNIT_SIZE 2
#define TCG_TARGET_TLB_DISPLACEMENT_BITS 19

/* We have a +- 4GB range on the branches; leave some slop.  */
#define MAX_CODE_GEN_BUFFER_SIZE  (3 * GiB)

typedef enum TCGReg {
    TCG_REG_R0,  TCG_REG_R1,  TCG_REG_R2,  TCG_REG_R3,
    TCG_REG_R4,  TCG_REG_R5,  TCG_REG_R6,  TCG_REG_R7,
    TCG_REG_R8,  TCG_REG_R9,  TCG_REG_R10, TCG_REG_R11,
    TCG_REG_R12, TCG_REG_R13, TCG_REG_R14, TCG_REG_R15,

    TCG_REG_V0 = 32, TCG_REG_V1,  TCG_REG_V2,  TCG_REG_V3,
    TCG_REG_V4,  TCG_REG_V5,  TCG_REG_V6,  TCG_REG_V7,
    TCG_REG_V8,  TCG_REG_V9,  TCG_REG_V10, TCG_REG_V11,
    TCG_REG_V12, TCG_REG_V13, TCG_REG_V14, TCG_REG_V15,
    TCG_REG_V16, TCG_REG_V17, TCG_REG_V18, TCG_REG_V19,
    TCG_REG_V20, TCG_REG_V21, TCG_REG_V22, TCG_REG_V23,
    TCG_REG_V24, TCG_REG_V25, TCG_REG_V26, TCG_REG_V27,
    TCG_REG_V28, TCG_REG_V29, TCG_REG_V30, TCG_REG_V31,

    TCG_AREG0 = TCG_REG_R10,
    TCG_REG_CALL_STACK = TCG_REG_R15
} TCGReg;

#define TCG_TARGET_NB_REGS 64

/* Facilities required for proper operation; checked at startup. */

#define FACILITY_ZARCH_ACTIVE         2
#define FACILITY_LONG_DISP            18
#define FACILITY_EXT_IMM              21
#define FACILITY_GEN_INST_EXT         34
#define FACILITY_45                   45

/* Facilities that are checked at runtime. */

#define FACILITY_LOAD_ON_COND2        53
#define FACILITY_MISC_INSN_EXT2       58
#define FACILITY_MISC_INSN_EXT3       61
#define FACILITY_VECTOR               129
#define FACILITY_VECTOR_ENH1          135

extern uint64_t s390_facilities[3];

#define HAVE_FACILITY(X) \
    ((s390_facilities[FACILITY_##X / 64] >> (63 - FACILITY_##X % 64)) & 1)

/* optional instructions */
#define TCG_TARGET_HAS_div2_i32       1
#define TCG_TARGET_HAS_rot_i32        1
#define TCG_TARGET_HAS_ext8s_i32      1
#define TCG_TARGET_HAS_ext16s_i32     1
#define TCG_TARGET_HAS_ext8u_i32      1
#define TCG_TARGET_HAS_ext16u_i32     1
#define TCG_TARGET_HAS_bswap16_i32    1
#define TCG_TARGET_HAS_bswap32_i32    1
#define TCG_TARGET_HAS_not_i32        HAVE_FACILITY(MISC_INSN_EXT3)
#define TCG_TARGET_HAS_neg_i32        1
#define TCG_TARGET_HAS_andc_i32       HAVE_FACILITY(MISC_INSN_EXT3)
#define TCG_TARGET_HAS_orc_i32        HAVE_FACILITY(MISC_INSN_EXT3)
#define TCG_TARGET_HAS_eqv_i32        HAVE_FACILITY(MISC_INSN_EXT3)
#define TCG_TARGET_HAS_nand_i32       HAVE_FACILITY(MISC_INSN_EXT3)
#define TCG_TARGET_HAS_nor_i32        HAVE_FACILITY(MISC_INSN_EXT3)
#define TCG_TARGET_HAS_clz_i32        0
#define TCG_TARGET_HAS_ctz_i32        0
#define TCG_TARGET_HAS_ctpop_i32      1
#define TCG_TARGET_HAS_deposit_i32    1
#define TCG_TARGET_HAS_extract_i32    1
#define TCG_TARGET_HAS_sextract_i32   0
#define TCG_TARGET_HAS_extract2_i32   0
#define TCG_TARGET_HAS_movcond_i32    1
#define TCG_TARGET_HAS_add2_i32       1
#define TCG_TARGET_HAS_sub2_i32       1
#define TCG_TARGET_HAS_mulu2_i32      0
#define TCG_TARGET_HAS_muls2_i32      0
#define TCG_TARGET_HAS_muluh_i32      0
#define TCG_TARGET_HAS_mulsh_i32      0
#define TCG_TARGET_HAS_extrl_i64_i32  0
#define TCG_TARGET_HAS_extrh_i64_i32  0
#define TCG_TARGET_HAS_qemu_st8_i32   0

#define TCG_TARGET_HAS_div2_i64       1
#define TCG_TARGET_HAS_rot_i64        1
#define TCG_TARGET_HAS_ext8s_i64      1
#define TCG_TARGET_HAS_ext16s_i64     1
#define TCG_TARGET_HAS_ext32s_i64     1
#define TCG_TARGET_HAS_ext8u_i64      1
#define TCG_TARGET_HAS_ext16u_i64     1
#define TCG_TARGET_HAS_ext32u_i64     1
#define TCG_TARGET_HAS_bswap16_i64    1
#define TCG_TARGET_HAS_bswap32_i64    1
#define TCG_TARGET_HAS_bswap64_i64    1
#define TCG_TARGET_HAS_not_i64        HAVE_FACILITY(MISC_INSN_EXT3)
#define TCG_TARGET_HAS_neg_i64        1
#define TCG_TARGET_HAS_andc_i64       HAVE_FACILITY(MISC_INSN_EXT3)
#define TCG_TARGET_HAS_orc_i64        HAVE_FACILITY(MISC_INSN_EXT3)
#define TCG_TARGET_HAS_eqv_i64        HAVE_FACILITY(MISC_INSN_EXT3)
#define TCG_TARGET_HAS_nand_i64       HAVE_FACILITY(MISC_INSN_EXT3)
#define TCG_TARGET_HAS_nor_i64        HAVE_FACILITY(MISC_INSN_EXT3)
#define TCG_TARGET_HAS_clz_i64        1
#define TCG_TARGET_HAS_ctz_i64        0
#define TCG_TARGET_HAS_ctpop_i64      1
#define TCG_TARGET_HAS_deposit_i64    1
#define TCG_TARGET_HAS_extract_i64    1
#define TCG_TARGET_HAS_sextract_i64   0
#define TCG_TARGET_HAS_extract2_i64   0
#define TCG_TARGET_HAS_movcond_i64    1
#define TCG_TARGET_HAS_add2_i64       1
#define TCG_TARGET_HAS_sub2_i64       1
#define TCG_TARGET_HAS_mulu2_i64      1
#define TCG_TARGET_HAS_muls2_i64      HAVE_FACILITY(MISC_INSN_EXT2)
#define TCG_TARGET_HAS_muluh_i64      0
#define TCG_TARGET_HAS_mulsh_i64      0

#define TCG_TARGET_HAS_v64            HAVE_FACILITY(VECTOR)
#define TCG_TARGET_HAS_v128           HAVE_FACILITY(VECTOR)
#define TCG_TARGET_HAS_v256           0

#define TCG_TARGET_HAS_andc_vec       1
#define TCG_TARGET_HAS_orc_vec        HAVE_FACILITY(VECTOR_ENH1)
#define TCG_TARGET_HAS_nand_vec       HAVE_FACILITY(VECTOR_ENH1)
#define TCG_TARGET_HAS_nor_vec        1
#define TCG_TARGET_HAS_eqv_vec        HAVE_FACILITY(VECTOR_ENH1)
#define TCG_TARGET_HAS_not_vec        1
#define TCG_TARGET_HAS_neg_vec        1
#define TCG_TARGET_HAS_abs_vec        1
#define TCG_TARGET_HAS_roti_vec       1
#define TCG_TARGET_HAS_rots_vec       1
#define TCG_TARGET_HAS_rotv_vec       1
#define TCG_TARGET_HAS_shi_vec        1
#define TCG_TARGET_HAS_shs_vec        1
#define TCG_TARGET_HAS_shv_vec        1
#define TCG_TARGET_HAS_mul_vec        1
#define TCG_TARGET_HAS_sat_vec        0
#define TCG_TARGET_HAS_minmax_vec     1
#define TCG_TARGET_HAS_bitsel_vec     1
#define TCG_TARGET_HAS_cmpsel_vec     0

/* used for function call generation */
#define TCG_TARGET_STACK_ALIGN		8
#define TCG_TARGET_CALL_STACK_OFFSET	160
#define TCG_TARGET_CALL_ARG_I32         TCG_CALL_ARG_EXTEND
#define TCG_TARGET_CALL_ARG_I64         TCG_CALL_ARG_NORMAL
#define TCG_TARGET_CALL_ARG_I128        TCG_CALL_ARG_BY_REF
#define TCG_TARGET_CALL_RET_I128        TCG_CALL_RET_BY_REF

#define TCG_TARGET_HAS_MEMORY_BSWAP   1

#define TCG_TARGET_DEFAULT_MO (TCG_MO_ALL & ~TCG_MO_ST_LD)
#define TCG_TARGET_NEED_LDST_LABELS
#define TCG_TARGET_NEED_POOL_LABELS

#endif
