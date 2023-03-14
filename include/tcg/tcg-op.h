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

#ifndef TCG_TCG_OP_H
#define TCG_TCG_OP_H

#include "tcg/tcg.h"
#include "exec/helper-proto.h"
#include "exec/helper-gen.h"

/* Basic output routines.  Not for general consumption.  */

void tcg_gen_op1(TCGOpcode, TCGArg);
void tcg_gen_op2(TCGOpcode, TCGArg, TCGArg);
void tcg_gen_op3(TCGOpcode, TCGArg, TCGArg, TCGArg);
void tcg_gen_op4(TCGOpcode, TCGArg, TCGArg, TCGArg, TCGArg);
void tcg_gen_op5(TCGOpcode, TCGArg, TCGArg, TCGArg, TCGArg, TCGArg);
void tcg_gen_op6(TCGOpcode, TCGArg, TCGArg, TCGArg, TCGArg, TCGArg, TCGArg);

void vec_gen_2(TCGOpcode, TCGType, unsigned, TCGArg, TCGArg);
void vec_gen_3(TCGOpcode, TCGType, unsigned, TCGArg, TCGArg, TCGArg);
void vec_gen_4(TCGOpcode, TCGType, unsigned, TCGArg, TCGArg, TCGArg, TCGArg);

static inline void tcg_gen_op1_i32(TCGOpcode opc, TCGv_i32 a1)
{
    tcg_gen_op1(opc, tcgv_i32_arg(a1));
}

static inline void tcg_gen_op1_i64(TCGOpcode opc, TCGv_i64 a1)
{
    tcg_gen_op1(opc, tcgv_i64_arg(a1));
}

static inline void tcg_gen_op1i(TCGOpcode opc, TCGArg a1)
{
    tcg_gen_op1(opc, a1);
}

static inline void tcg_gen_op2_i32(TCGOpcode opc, TCGv_i32 a1, TCGv_i32 a2)
{
    tcg_gen_op2(opc, tcgv_i32_arg(a1), tcgv_i32_arg(a2));
}

static inline void tcg_gen_op2_i64(TCGOpcode opc, TCGv_i64 a1, TCGv_i64 a2)
{
    tcg_gen_op2(opc, tcgv_i64_arg(a1), tcgv_i64_arg(a2));
}

static inline void tcg_gen_op2i_i32(TCGOpcode opc, TCGv_i32 a1, TCGArg a2)
{
    tcg_gen_op2(opc, tcgv_i32_arg(a1), a2);
}

static inline void tcg_gen_op2i_i64(TCGOpcode opc, TCGv_i64 a1, TCGArg a2)
{
    tcg_gen_op2(opc, tcgv_i64_arg(a1), a2);
}

static inline void tcg_gen_op2ii(TCGOpcode opc, TCGArg a1, TCGArg a2)
{
    tcg_gen_op2(opc, a1, a2);
}

static inline void tcg_gen_op3_i32(TCGOpcode opc, TCGv_i32 a1,
                                   TCGv_i32 a2, TCGv_i32 a3)
{
    tcg_gen_op3(opc, tcgv_i32_arg(a1), tcgv_i32_arg(a2), tcgv_i32_arg(a3));
}

static inline void tcg_gen_op3_i64(TCGOpcode opc, TCGv_i64 a1,
                                   TCGv_i64 a2, TCGv_i64 a3)
{
    tcg_gen_op3(opc, tcgv_i64_arg(a1), tcgv_i64_arg(a2), tcgv_i64_arg(a3));
}

static inline void tcg_gen_op3i_i32(TCGOpcode opc, TCGv_i32 a1,
                                    TCGv_i32 a2, TCGArg a3)
{
    tcg_gen_op3(opc, tcgv_i32_arg(a1), tcgv_i32_arg(a2), a3);
}

static inline void tcg_gen_op3i_i64(TCGOpcode opc, TCGv_i64 a1,
                                    TCGv_i64 a2, TCGArg a3)
{
    tcg_gen_op3(opc, tcgv_i64_arg(a1), tcgv_i64_arg(a2), a3);
}

static inline void tcg_gen_ldst_op_i32(TCGOpcode opc, TCGv_i32 val,
                                       TCGv_ptr base, TCGArg offset)
{
    tcg_gen_op3(opc, tcgv_i32_arg(val), tcgv_ptr_arg(base), offset);
}

static inline void tcg_gen_ldst_op_i64(TCGOpcode opc, TCGv_i64 val,
                                       TCGv_ptr base, TCGArg offset)
{
    tcg_gen_op3(opc, tcgv_i64_arg(val), tcgv_ptr_arg(base), offset);
}

static inline void tcg_gen_op4_i32(TCGOpcode opc, TCGv_i32 a1, TCGv_i32 a2,
                                   TCGv_i32 a3, TCGv_i32 a4)
{
    tcg_gen_op4(opc, tcgv_i32_arg(a1), tcgv_i32_arg(a2),
                tcgv_i32_arg(a3), tcgv_i32_arg(a4));
}

static inline void tcg_gen_op4_i64(TCGOpcode opc, TCGv_i64 a1, TCGv_i64 a2,
                                   TCGv_i64 a3, TCGv_i64 a4)
{
    tcg_gen_op4(opc, tcgv_i64_arg(a1), tcgv_i64_arg(a2),
                tcgv_i64_arg(a3), tcgv_i64_arg(a4));
}

static inline void tcg_gen_op4i_i32(TCGOpcode opc, TCGv_i32 a1, TCGv_i32 a2,
                                    TCGv_i32 a3, TCGArg a4)
{
    tcg_gen_op4(opc, tcgv_i32_arg(a1), tcgv_i32_arg(a2),
                tcgv_i32_arg(a3), a4);
}

static inline void tcg_gen_op4i_i64(TCGOpcode opc, TCGv_i64 a1, TCGv_i64 a2,
                                    TCGv_i64 a3, TCGArg a4)
{
    tcg_gen_op4(opc, tcgv_i64_arg(a1), tcgv_i64_arg(a2),
                tcgv_i64_arg(a3), a4);
}

static inline void tcg_gen_op4ii_i32(TCGOpcode opc, TCGv_i32 a1, TCGv_i32 a2,
                                     TCGArg a3, TCGArg a4)
{
    tcg_gen_op4(opc, tcgv_i32_arg(a1), tcgv_i32_arg(a2), a3, a4);
}

static inline void tcg_gen_op4ii_i64(TCGOpcode opc, TCGv_i64 a1, TCGv_i64 a2,
                                     TCGArg a3, TCGArg a4)
{
    tcg_gen_op4(opc, tcgv_i64_arg(a1), tcgv_i64_arg(a2), a3, a4);
}

static inline void tcg_gen_op5_i32(TCGOpcode opc, TCGv_i32 a1, TCGv_i32 a2,
                                   TCGv_i32 a3, TCGv_i32 a4, TCGv_i32 a5)
{
    tcg_gen_op5(opc, tcgv_i32_arg(a1), tcgv_i32_arg(a2),
                tcgv_i32_arg(a3), tcgv_i32_arg(a4), tcgv_i32_arg(a5));
}

static inline void tcg_gen_op5_i64(TCGOpcode opc, TCGv_i64 a1, TCGv_i64 a2,
                                   TCGv_i64 a3, TCGv_i64 a4, TCGv_i64 a5)
{
    tcg_gen_op5(opc, tcgv_i64_arg(a1), tcgv_i64_arg(a2),
                tcgv_i64_arg(a3), tcgv_i64_arg(a4), tcgv_i64_arg(a5));
}

static inline void tcg_gen_op5i_i32(TCGOpcode opc, TCGv_i32 a1, TCGv_i32 a2,
                                    TCGv_i32 a3, TCGv_i32 a4, TCGArg a5)
{
    tcg_gen_op5(opc, tcgv_i32_arg(a1), tcgv_i32_arg(a2),
                tcgv_i32_arg(a3), tcgv_i32_arg(a4), a5);
}

static inline void tcg_gen_op5i_i64(TCGOpcode opc, TCGv_i64 a1, TCGv_i64 a2,
                                    TCGv_i64 a3, TCGv_i64 a4, TCGArg a5)
{
    tcg_gen_op5(opc, tcgv_i64_arg(a1), tcgv_i64_arg(a2),
                tcgv_i64_arg(a3), tcgv_i64_arg(a4), a5);
}

static inline void tcg_gen_op5ii_i32(TCGOpcode opc, TCGv_i32 a1, TCGv_i32 a2,
                                     TCGv_i32 a3, TCGArg a4, TCGArg a5)
{
    tcg_gen_op5(opc, tcgv_i32_arg(a1), tcgv_i32_arg(a2),
                tcgv_i32_arg(a3), a4, a5);
}

static inline void tcg_gen_op5ii_i64(TCGOpcode opc, TCGv_i64 a1, TCGv_i64 a2,
                                     TCGv_i64 a3, TCGArg a4, TCGArg a5)
{
    tcg_gen_op5(opc, tcgv_i64_arg(a1), tcgv_i64_arg(a2),
                tcgv_i64_arg(a3), a4, a5);
}

static inline void tcg_gen_op6_i32(TCGOpcode opc, TCGv_i32 a1, TCGv_i32 a2,
                                   TCGv_i32 a3, TCGv_i32 a4,
                                   TCGv_i32 a5, TCGv_i32 a6)
{
    tcg_gen_op6(opc, tcgv_i32_arg(a1), tcgv_i32_arg(a2),
                tcgv_i32_arg(a3), tcgv_i32_arg(a4), tcgv_i32_arg(a5),
                tcgv_i32_arg(a6));
}

static inline void tcg_gen_op6_i64(TCGOpcode opc, TCGv_i64 a1, TCGv_i64 a2,
                                   TCGv_i64 a3, TCGv_i64 a4,
                                   TCGv_i64 a5, TCGv_i64 a6)
{
    tcg_gen_op6(opc, tcgv_i64_arg(a1), tcgv_i64_arg(a2),
                tcgv_i64_arg(a3), tcgv_i64_arg(a4), tcgv_i64_arg(a5),
                tcgv_i64_arg(a6));
}

static inline void tcg_gen_op6i_i32(TCGOpcode opc, TCGv_i32 a1, TCGv_i32 a2,
                                    TCGv_i32 a3, TCGv_i32 a4,
                                    TCGv_i32 a5, TCGArg a6)
{
    tcg_gen_op6(opc, tcgv_i32_arg(a1), tcgv_i32_arg(a2),
                tcgv_i32_arg(a3), tcgv_i32_arg(a4), tcgv_i32_arg(a5), a6);
}

static inline void tcg_gen_op6i_i64(TCGOpcode opc, TCGv_i64 a1, TCGv_i64 a2,
                                    TCGv_i64 a3, TCGv_i64 a4,
                                    TCGv_i64 a5, TCGArg a6)
{
    tcg_gen_op6(opc, tcgv_i64_arg(a1), tcgv_i64_arg(a2),
                tcgv_i64_arg(a3), tcgv_i64_arg(a4), tcgv_i64_arg(a5), a6);
}

static inline void tcg_gen_op6ii_i32(TCGOpcode opc, TCGv_i32 a1, TCGv_i32 a2,
                                     TCGv_i32 a3, TCGv_i32 a4,
                                     TCGArg a5, TCGArg a6)
{
    tcg_gen_op6(opc, tcgv_i32_arg(a1), tcgv_i32_arg(a2),
                tcgv_i32_arg(a3), tcgv_i32_arg(a4), a5, a6);
}

static inline void tcg_gen_op6ii_i64(TCGOpcode opc, TCGv_i64 a1, TCGv_i64 a2,
                                     TCGv_i64 a3, TCGv_i64 a4,
                                     TCGArg a5, TCGArg a6)
{
    tcg_gen_op6(opc, tcgv_i64_arg(a1), tcgv_i64_arg(a2),
                tcgv_i64_arg(a3), tcgv_i64_arg(a4), a5, a6);
}


/* Generic ops.  */

static inline void gen_set_label(TCGLabel *l)
{
    l->present = 1;
    tcg_gen_op1(INDEX_op_set_label, label_arg(l));
}

void tcg_gen_br(TCGLabel *l);
void tcg_gen_mb(TCGBar);

/* Helper calls. */

/* 32 bit ops */

void tcg_gen_movi_i32(TCGv_i32 ret, int32_t arg);
void tcg_gen_addi_i32(TCGv_i32 ret, TCGv_i32 arg1, int32_t arg2);
void tcg_gen_subfi_i32(TCGv_i32 ret, int32_t arg1, TCGv_i32 arg2);
void tcg_gen_subi_i32(TCGv_i32 ret, TCGv_i32 arg1, int32_t arg2);
void tcg_gen_andi_i32(TCGv_i32 ret, TCGv_i32 arg1, int32_t arg2);
void tcg_gen_ori_i32(TCGv_i32 ret, TCGv_i32 arg1, int32_t arg2);
void tcg_gen_xori_i32(TCGv_i32 ret, TCGv_i32 arg1, int32_t arg2);
void tcg_gen_shli_i32(TCGv_i32 ret, TCGv_i32 arg1, int32_t arg2);
void tcg_gen_shri_i32(TCGv_i32 ret, TCGv_i32 arg1, int32_t arg2);
void tcg_gen_sari_i32(TCGv_i32 ret, TCGv_i32 arg1, int32_t arg2);
void tcg_gen_muli_i32(TCGv_i32 ret, TCGv_i32 arg1, int32_t arg2);
void tcg_gen_div_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2);
void tcg_gen_rem_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2);
void tcg_gen_divu_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2);
void tcg_gen_remu_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2);
void tcg_gen_andc_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2);
void tcg_gen_eqv_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2);
void tcg_gen_nand_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2);
void tcg_gen_nor_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2);
void tcg_gen_orc_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2);
void tcg_gen_clz_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2);
void tcg_gen_ctz_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2);
void tcg_gen_clzi_i32(TCGv_i32 ret, TCGv_i32 arg1, uint32_t arg2);
void tcg_gen_ctzi_i32(TCGv_i32 ret, TCGv_i32 arg1, uint32_t arg2);
void tcg_gen_clrsb_i32(TCGv_i32 ret, TCGv_i32 arg);
void tcg_gen_ctpop_i32(TCGv_i32 a1, TCGv_i32 a2);
void tcg_gen_rotl_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2);
void tcg_gen_rotli_i32(TCGv_i32 ret, TCGv_i32 arg1, int32_t arg2);
void tcg_gen_rotr_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2);
void tcg_gen_rotri_i32(TCGv_i32 ret, TCGv_i32 arg1, int32_t arg2);
void tcg_gen_deposit_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2,
                         unsigned int ofs, unsigned int len);
void tcg_gen_deposit_z_i32(TCGv_i32 ret, TCGv_i32 arg,
                           unsigned int ofs, unsigned int len);
void tcg_gen_extract_i32(TCGv_i32 ret, TCGv_i32 arg,
                         unsigned int ofs, unsigned int len);
void tcg_gen_sextract_i32(TCGv_i32 ret, TCGv_i32 arg,
                          unsigned int ofs, unsigned int len);
void tcg_gen_extract2_i32(TCGv_i32 ret, TCGv_i32 al, TCGv_i32 ah,
                          unsigned int ofs);
void tcg_gen_brcond_i32(TCGCond cond, TCGv_i32 arg1, TCGv_i32 arg2, TCGLabel *);
void tcg_gen_brcondi_i32(TCGCond cond, TCGv_i32 arg1, int32_t arg2, TCGLabel *);
void tcg_gen_setcond_i32(TCGCond cond, TCGv_i32 ret,
                         TCGv_i32 arg1, TCGv_i32 arg2);
void tcg_gen_setcondi_i32(TCGCond cond, TCGv_i32 ret,
                          TCGv_i32 arg1, int32_t arg2);
void tcg_gen_movcond_i32(TCGCond cond, TCGv_i32 ret, TCGv_i32 c1,
                         TCGv_i32 c2, TCGv_i32 v1, TCGv_i32 v2);
void tcg_gen_add2_i32(TCGv_i32 rl, TCGv_i32 rh, TCGv_i32 al,
                      TCGv_i32 ah, TCGv_i32 bl, TCGv_i32 bh);
void tcg_gen_sub2_i32(TCGv_i32 rl, TCGv_i32 rh, TCGv_i32 al,
                      TCGv_i32 ah, TCGv_i32 bl, TCGv_i32 bh);
void tcg_gen_mulu2_i32(TCGv_i32 rl, TCGv_i32 rh, TCGv_i32 arg1, TCGv_i32 arg2);
void tcg_gen_muls2_i32(TCGv_i32 rl, TCGv_i32 rh, TCGv_i32 arg1, TCGv_i32 arg2);
void tcg_gen_mulsu2_i32(TCGv_i32 rl, TCGv_i32 rh, TCGv_i32 arg1, TCGv_i32 arg2);
void tcg_gen_ext8s_i32(TCGv_i32 ret, TCGv_i32 arg);
void tcg_gen_ext16s_i32(TCGv_i32 ret, TCGv_i32 arg);
void tcg_gen_ext8u_i32(TCGv_i32 ret, TCGv_i32 arg);
void tcg_gen_ext16u_i32(TCGv_i32 ret, TCGv_i32 arg);
void tcg_gen_bswap16_i32(TCGv_i32 ret, TCGv_i32 arg, int flags);
void tcg_gen_bswap32_i32(TCGv_i32 ret, TCGv_i32 arg);
void tcg_gen_hswap_i32(TCGv_i32 ret, TCGv_i32 arg);
void tcg_gen_smin_i32(TCGv_i32, TCGv_i32 arg1, TCGv_i32 arg2);
void tcg_gen_smax_i32(TCGv_i32, TCGv_i32 arg1, TCGv_i32 arg2);
void tcg_gen_umin_i32(TCGv_i32, TCGv_i32 arg1, TCGv_i32 arg2);
void tcg_gen_umax_i32(TCGv_i32, TCGv_i32 arg1, TCGv_i32 arg2);
void tcg_gen_abs_i32(TCGv_i32, TCGv_i32);

/* Replicate a value of size @vece from @in to all the lanes in @out */
void tcg_gen_dup_i32(unsigned vece, TCGv_i32 out, TCGv_i32 in);

static inline void tcg_gen_discard_i32(TCGv_i32 arg)
{
    tcg_gen_op1_i32(INDEX_op_discard, arg);
}

static inline void tcg_gen_mov_i32(TCGv_i32 ret, TCGv_i32 arg)
{
    if (ret != arg) {
        tcg_gen_op2_i32(INDEX_op_mov_i32, ret, arg);
    }
}

static inline void tcg_gen_ld8u_i32(TCGv_i32 ret, TCGv_ptr arg2,
                                    tcg_target_long offset)
{
    tcg_gen_ldst_op_i32(INDEX_op_ld8u_i32, ret, arg2, offset);
}

static inline void tcg_gen_ld8s_i32(TCGv_i32 ret, TCGv_ptr arg2,
                                    tcg_target_long offset)
{
    tcg_gen_ldst_op_i32(INDEX_op_ld8s_i32, ret, arg2, offset);
}

static inline void tcg_gen_ld16u_i32(TCGv_i32 ret, TCGv_ptr arg2,
                                     tcg_target_long offset)
{
    tcg_gen_ldst_op_i32(INDEX_op_ld16u_i32, ret, arg2, offset);
}

static inline void tcg_gen_ld16s_i32(TCGv_i32 ret, TCGv_ptr arg2,
                                     tcg_target_long offset)
{
    tcg_gen_ldst_op_i32(INDEX_op_ld16s_i32, ret, arg2, offset);
}

static inline void tcg_gen_ld_i32(TCGv_i32 ret, TCGv_ptr arg2,
                                  tcg_target_long offset)
{
    tcg_gen_ldst_op_i32(INDEX_op_ld_i32, ret, arg2, offset);
}

static inline void tcg_gen_st8_i32(TCGv_i32 arg1, TCGv_ptr arg2,
                                   tcg_target_long offset)
{
    tcg_gen_ldst_op_i32(INDEX_op_st8_i32, arg1, arg2, offset);
}

static inline void tcg_gen_st16_i32(TCGv_i32 arg1, TCGv_ptr arg2,
                                    tcg_target_long offset)
{
    tcg_gen_ldst_op_i32(INDEX_op_st16_i32, arg1, arg2, offset);
}

static inline void tcg_gen_st_i32(TCGv_i32 arg1, TCGv_ptr arg2,
                                  tcg_target_long offset)
{
    tcg_gen_ldst_op_i32(INDEX_op_st_i32, arg1, arg2, offset);
}

static inline void tcg_gen_add_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    tcg_gen_op3_i32(INDEX_op_add_i32, ret, arg1, arg2);
}

static inline void tcg_gen_sub_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    tcg_gen_op3_i32(INDEX_op_sub_i32, ret, arg1, arg2);
}

static inline void tcg_gen_and_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    tcg_gen_op3_i32(INDEX_op_and_i32, ret, arg1, arg2);
}

static inline void tcg_gen_or_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    tcg_gen_op3_i32(INDEX_op_or_i32, ret, arg1, arg2);
}

static inline void tcg_gen_xor_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    tcg_gen_op3_i32(INDEX_op_xor_i32, ret, arg1, arg2);
}

static inline void tcg_gen_shl_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    tcg_gen_op3_i32(INDEX_op_shl_i32, ret, arg1, arg2);
}

static inline void tcg_gen_shr_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    tcg_gen_op3_i32(INDEX_op_shr_i32, ret, arg1, arg2);
}

static inline void tcg_gen_sar_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    tcg_gen_op3_i32(INDEX_op_sar_i32, ret, arg1, arg2);
}

static inline void tcg_gen_mul_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    tcg_gen_op3_i32(INDEX_op_mul_i32, ret, arg1, arg2);
}

static inline void tcg_gen_neg_i32(TCGv_i32 ret, TCGv_i32 arg)
{
    if (TCG_TARGET_HAS_neg_i32) {
        tcg_gen_op2_i32(INDEX_op_neg_i32, ret, arg);
    } else {
        tcg_gen_subfi_i32(ret, 0, arg);
    }
}

static inline void tcg_gen_not_i32(TCGv_i32 ret, TCGv_i32 arg)
{
    if (TCG_TARGET_HAS_not_i32) {
        tcg_gen_op2_i32(INDEX_op_not_i32, ret, arg);
    } else {
        tcg_gen_xori_i32(ret, arg, -1);
    }
}

/* 64 bit ops */

void tcg_gen_movi_i64(TCGv_i64 ret, int64_t arg);
void tcg_gen_addi_i64(TCGv_i64 ret, TCGv_i64 arg1, int64_t arg2);
void tcg_gen_subfi_i64(TCGv_i64 ret, int64_t arg1, TCGv_i64 arg2);
void tcg_gen_subi_i64(TCGv_i64 ret, TCGv_i64 arg1, int64_t arg2);
void tcg_gen_andi_i64(TCGv_i64 ret, TCGv_i64 arg1, int64_t arg2);
void tcg_gen_ori_i64(TCGv_i64 ret, TCGv_i64 arg1, int64_t arg2);
void tcg_gen_xori_i64(TCGv_i64 ret, TCGv_i64 arg1, int64_t arg2);
void tcg_gen_shli_i64(TCGv_i64 ret, TCGv_i64 arg1, int64_t arg2);
void tcg_gen_shri_i64(TCGv_i64 ret, TCGv_i64 arg1, int64_t arg2);
void tcg_gen_sari_i64(TCGv_i64 ret, TCGv_i64 arg1, int64_t arg2);
void tcg_gen_muli_i64(TCGv_i64 ret, TCGv_i64 arg1, int64_t arg2);
void tcg_gen_div_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_rem_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_divu_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_remu_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_andc_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_eqv_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_nand_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_nor_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_orc_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_clz_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_ctz_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_clzi_i64(TCGv_i64 ret, TCGv_i64 arg1, uint64_t arg2);
void tcg_gen_ctzi_i64(TCGv_i64 ret, TCGv_i64 arg1, uint64_t arg2);
void tcg_gen_clrsb_i64(TCGv_i64 ret, TCGv_i64 arg);
void tcg_gen_ctpop_i64(TCGv_i64 a1, TCGv_i64 a2);
void tcg_gen_rotl_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_rotli_i64(TCGv_i64 ret, TCGv_i64 arg1, int64_t arg2);
void tcg_gen_rotr_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_rotri_i64(TCGv_i64 ret, TCGv_i64 arg1, int64_t arg2);
void tcg_gen_deposit_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2,
                         unsigned int ofs, unsigned int len);
void tcg_gen_deposit_z_i64(TCGv_i64 ret, TCGv_i64 arg,
                           unsigned int ofs, unsigned int len);
void tcg_gen_extract_i64(TCGv_i64 ret, TCGv_i64 arg,
                         unsigned int ofs, unsigned int len);
void tcg_gen_sextract_i64(TCGv_i64 ret, TCGv_i64 arg,
                          unsigned int ofs, unsigned int len);
void tcg_gen_extract2_i64(TCGv_i64 ret, TCGv_i64 al, TCGv_i64 ah,
                          unsigned int ofs);
void tcg_gen_brcond_i64(TCGCond cond, TCGv_i64 arg1, TCGv_i64 arg2, TCGLabel *);
void tcg_gen_brcondi_i64(TCGCond cond, TCGv_i64 arg1, int64_t arg2, TCGLabel *);
void tcg_gen_setcond_i64(TCGCond cond, TCGv_i64 ret,
                         TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_setcondi_i64(TCGCond cond, TCGv_i64 ret,
                          TCGv_i64 arg1, int64_t arg2);
void tcg_gen_movcond_i64(TCGCond cond, TCGv_i64 ret, TCGv_i64 c1,
                         TCGv_i64 c2, TCGv_i64 v1, TCGv_i64 v2);
void tcg_gen_add2_i64(TCGv_i64 rl, TCGv_i64 rh, TCGv_i64 al,
                      TCGv_i64 ah, TCGv_i64 bl, TCGv_i64 bh);
void tcg_gen_sub2_i64(TCGv_i64 rl, TCGv_i64 rh, TCGv_i64 al,
                      TCGv_i64 ah, TCGv_i64 bl, TCGv_i64 bh);
void tcg_gen_mulu2_i64(TCGv_i64 rl, TCGv_i64 rh, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_muls2_i64(TCGv_i64 rl, TCGv_i64 rh, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_mulsu2_i64(TCGv_i64 rl, TCGv_i64 rh, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_not_i64(TCGv_i64 ret, TCGv_i64 arg);
void tcg_gen_ext8s_i64(TCGv_i64 ret, TCGv_i64 arg);
void tcg_gen_ext16s_i64(TCGv_i64 ret, TCGv_i64 arg);
void tcg_gen_ext32s_i64(TCGv_i64 ret, TCGv_i64 arg);
void tcg_gen_ext8u_i64(TCGv_i64 ret, TCGv_i64 arg);
void tcg_gen_ext16u_i64(TCGv_i64 ret, TCGv_i64 arg);
void tcg_gen_ext32u_i64(TCGv_i64 ret, TCGv_i64 arg);
void tcg_gen_bswap16_i64(TCGv_i64 ret, TCGv_i64 arg, int flags);
void tcg_gen_bswap32_i64(TCGv_i64 ret, TCGv_i64 arg, int flags);
void tcg_gen_bswap64_i64(TCGv_i64 ret, TCGv_i64 arg);
void tcg_gen_hswap_i64(TCGv_i64 ret, TCGv_i64 arg);
void tcg_gen_wswap_i64(TCGv_i64 ret, TCGv_i64 arg);
void tcg_gen_smin_i64(TCGv_i64, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_smax_i64(TCGv_i64, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_umin_i64(TCGv_i64, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_umax_i64(TCGv_i64, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_abs_i64(TCGv_i64, TCGv_i64);

/* Replicate a value of size @vece from @in to all the lanes in @out */
void tcg_gen_dup_i64(unsigned vece, TCGv_i64 out, TCGv_i64 in);

#if TCG_TARGET_REG_BITS == 64
static inline void tcg_gen_discard_i64(TCGv_i64 arg)
{
    tcg_gen_op1_i64(INDEX_op_discard, arg);
}

static inline void tcg_gen_mov_i64(TCGv_i64 ret, TCGv_i64 arg)
{
    if (ret != arg) {
        tcg_gen_op2_i64(INDEX_op_mov_i64, ret, arg);
    }
}

static inline void tcg_gen_ld8u_i64(TCGv_i64 ret, TCGv_ptr arg2,
                                    tcg_target_long offset)
{
    tcg_gen_ldst_op_i64(INDEX_op_ld8u_i64, ret, arg2, offset);
}

static inline void tcg_gen_ld8s_i64(TCGv_i64 ret, TCGv_ptr arg2,
                                    tcg_target_long offset)
{
    tcg_gen_ldst_op_i64(INDEX_op_ld8s_i64, ret, arg2, offset);
}

static inline void tcg_gen_ld16u_i64(TCGv_i64 ret, TCGv_ptr arg2,
                                     tcg_target_long offset)
{
    tcg_gen_ldst_op_i64(INDEX_op_ld16u_i64, ret, arg2, offset);
}

static inline void tcg_gen_ld16s_i64(TCGv_i64 ret, TCGv_ptr arg2,
                                     tcg_target_long offset)
{
    tcg_gen_ldst_op_i64(INDEX_op_ld16s_i64, ret, arg2, offset);
}

static inline void tcg_gen_ld32u_i64(TCGv_i64 ret, TCGv_ptr arg2,
                                     tcg_target_long offset)
{
    tcg_gen_ldst_op_i64(INDEX_op_ld32u_i64, ret, arg2, offset);
}

static inline void tcg_gen_ld32s_i64(TCGv_i64 ret, TCGv_ptr arg2,
                                     tcg_target_long offset)
{
    tcg_gen_ldst_op_i64(INDEX_op_ld32s_i64, ret, arg2, offset);
}

static inline void tcg_gen_ld_i64(TCGv_i64 ret, TCGv_ptr arg2,
                                  tcg_target_long offset)
{
    tcg_gen_ldst_op_i64(INDEX_op_ld_i64, ret, arg2, offset);
}

static inline void tcg_gen_st8_i64(TCGv_i64 arg1, TCGv_ptr arg2,
                                   tcg_target_long offset)
{
    tcg_gen_ldst_op_i64(INDEX_op_st8_i64, arg1, arg2, offset);
}

static inline void tcg_gen_st16_i64(TCGv_i64 arg1, TCGv_ptr arg2,
                                    tcg_target_long offset)
{
    tcg_gen_ldst_op_i64(INDEX_op_st16_i64, arg1, arg2, offset);
}

static inline void tcg_gen_st32_i64(TCGv_i64 arg1, TCGv_ptr arg2,
                                    tcg_target_long offset)
{
    tcg_gen_ldst_op_i64(INDEX_op_st32_i64, arg1, arg2, offset);
}

static inline void tcg_gen_st_i64(TCGv_i64 arg1, TCGv_ptr arg2,
                                  tcg_target_long offset)
{
    tcg_gen_ldst_op_i64(INDEX_op_st_i64, arg1, arg2, offset);
}

static inline void tcg_gen_add_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    tcg_gen_op3_i64(INDEX_op_add_i64, ret, arg1, arg2);
}

static inline void tcg_gen_sub_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    tcg_gen_op3_i64(INDEX_op_sub_i64, ret, arg1, arg2);
}

static inline void tcg_gen_and_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    tcg_gen_op3_i64(INDEX_op_and_i64, ret, arg1, arg2);
}

static inline void tcg_gen_or_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    tcg_gen_op3_i64(INDEX_op_or_i64, ret, arg1, arg2);
}

static inline void tcg_gen_xor_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    tcg_gen_op3_i64(INDEX_op_xor_i64, ret, arg1, arg2);
}

static inline void tcg_gen_shl_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    tcg_gen_op3_i64(INDEX_op_shl_i64, ret, arg1, arg2);
}

static inline void tcg_gen_shr_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    tcg_gen_op3_i64(INDEX_op_shr_i64, ret, arg1, arg2);
}

static inline void tcg_gen_sar_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    tcg_gen_op3_i64(INDEX_op_sar_i64, ret, arg1, arg2);
}

static inline void tcg_gen_mul_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    tcg_gen_op3_i64(INDEX_op_mul_i64, ret, arg1, arg2);
}
#else /* TCG_TARGET_REG_BITS == 32 */
void tcg_gen_st8_i64(TCGv_i64 arg1, TCGv_ptr arg2, tcg_target_long offset);
void tcg_gen_st16_i64(TCGv_i64 arg1, TCGv_ptr arg2, tcg_target_long offset);
void tcg_gen_st32_i64(TCGv_i64 arg1, TCGv_ptr arg2, tcg_target_long offset);

void tcg_gen_add_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_sub_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2);

void tcg_gen_discard_i64(TCGv_i64 arg);
void tcg_gen_mov_i64(TCGv_i64 ret, TCGv_i64 arg);
void tcg_gen_ld8u_i64(TCGv_i64 ret, TCGv_ptr arg2, tcg_target_long offset);
void tcg_gen_ld8s_i64(TCGv_i64 ret, TCGv_ptr arg2, tcg_target_long offset);
void tcg_gen_ld16u_i64(TCGv_i64 ret, TCGv_ptr arg2, tcg_target_long offset);
void tcg_gen_ld16s_i64(TCGv_i64 ret, TCGv_ptr arg2, tcg_target_long offset);
void tcg_gen_ld32u_i64(TCGv_i64 ret, TCGv_ptr arg2, tcg_target_long offset);
void tcg_gen_ld32s_i64(TCGv_i64 ret, TCGv_ptr arg2, tcg_target_long offset);
void tcg_gen_ld_i64(TCGv_i64 ret, TCGv_ptr arg2, tcg_target_long offset);
void tcg_gen_st_i64(TCGv_i64 arg1, TCGv_ptr arg2, tcg_target_long offset);
void tcg_gen_and_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_or_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_xor_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_shl_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_shr_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_sar_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_mul_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2);
#endif /* TCG_TARGET_REG_BITS */

static inline void tcg_gen_neg_i64(TCGv_i64 ret, TCGv_i64 arg)
{
    if (TCG_TARGET_HAS_neg_i64) {
        tcg_gen_op2_i64(INDEX_op_neg_i64, ret, arg);
    } else {
        tcg_gen_subfi_i64(ret, 0, arg);
    }
}

/* Size changing operations.  */

void tcg_gen_extu_i32_i64(TCGv_i64 ret, TCGv_i32 arg);
void tcg_gen_ext_i32_i64(TCGv_i64 ret, TCGv_i32 arg);
void tcg_gen_concat_i32_i64(TCGv_i64 dest, TCGv_i32 low, TCGv_i32 high);
void tcg_gen_extrl_i64_i32(TCGv_i32 ret, TCGv_i64 arg);
void tcg_gen_extrh_i64_i32(TCGv_i32 ret, TCGv_i64 arg);
void tcg_gen_extr_i64_i32(TCGv_i32 lo, TCGv_i32 hi, TCGv_i64 arg);
void tcg_gen_extr32_i64(TCGv_i64 lo, TCGv_i64 hi, TCGv_i64 arg);

void tcg_gen_mov_i128(TCGv_i128 dst, TCGv_i128 src);
void tcg_gen_extr_i128_i64(TCGv_i64 lo, TCGv_i64 hi, TCGv_i128 arg);
void tcg_gen_concat_i64_i128(TCGv_i128 ret, TCGv_i64 lo, TCGv_i64 hi);

static inline void tcg_gen_concat32_i64(TCGv_i64 ret, TCGv_i64 lo, TCGv_i64 hi)
{
    tcg_gen_deposit_i64(ret, lo, hi, 32, 32);
}

/* QEMU specific operations.  */

#ifndef TARGET_LONG_BITS
#error must include QEMU headers
#endif

#if TARGET_INSN_START_WORDS == 1
static inline void tcg_gen_insn_start(target_ulong pc)
{
    TCGOp *op = tcg_emit_op(INDEX_op_insn_start, 64 / TCG_TARGET_REG_BITS);
    tcg_set_insn_start_param(op, 0, pc);
}
#elif TARGET_INSN_START_WORDS == 2
static inline void tcg_gen_insn_start(target_ulong pc, target_ulong a1)
{
    TCGOp *op = tcg_emit_op(INDEX_op_insn_start, 2 * 64 / TCG_TARGET_REG_BITS);
    tcg_set_insn_start_param(op, 0, pc);
    tcg_set_insn_start_param(op, 1, a1);
}
#elif TARGET_INSN_START_WORDS == 3
static inline void tcg_gen_insn_start(target_ulong pc, target_ulong a1,
                                      target_ulong a2)
{
    TCGOp *op = tcg_emit_op(INDEX_op_insn_start, 3 * 64 / TCG_TARGET_REG_BITS);
    tcg_set_insn_start_param(op, 0, pc);
    tcg_set_insn_start_param(op, 1, a1);
    tcg_set_insn_start_param(op, 2, a2);
}
#else
# error "Unhandled number of operands to insn_start"
#endif

/**
 * tcg_gen_exit_tb() - output exit_tb TCG operation
 * @tb: The TranslationBlock from which we are exiting
 * @idx: Direct jump slot index, or exit request
 *
 * See tcg/README for more info about this TCG operation.
 * See also tcg.h and the block comment above TB_EXIT_MASK.
 *
 * For a normal exit from the TB, back to the main loop, @tb should
 * be NULL and @idx should be 0.  Otherwise, @tb should be valid and
 * @idx should be one of the TB_EXIT_ values.
 */
void tcg_gen_exit_tb(const TranslationBlock *tb, unsigned idx);

/**
 * tcg_gen_goto_tb() - output goto_tb TCG operation
 * @idx: Direct jump slot index (0 or 1)
 *
 * See tcg/README for more info about this TCG operation.
 *
 * NOTE: In softmmu emulation, direct jumps with goto_tb are only safe within
 * the pages this TB resides in because we don't take care of direct jumps when
 * address mapping changes, e.g. in tlb_flush(). In user mode, there's only a
 * static address translation, so the destination address is always valid, TBs
 * are always invalidated properly, and direct jumps are reset when mapping
 * changes.
 */
void tcg_gen_goto_tb(unsigned idx);

/**
 * tcg_gen_lookup_and_goto_ptr() - look up the current TB, jump to it if valid
 * @addr: Guest address of the target TB
 *
 * If the TB is not valid, jump to the epilogue.
 *
 * This operation is optional. If the TCG backend does not implement goto_ptr,
 * this op is equivalent to calling tcg_gen_exit_tb() with 0 as the argument.
 */
void tcg_gen_lookup_and_goto_ptr(void);

static inline void tcg_gen_plugin_cb_start(unsigned from, unsigned type,
                                           unsigned wr)
{
    tcg_gen_op3(INDEX_op_plugin_cb_start, from, type, wr);
}

static inline void tcg_gen_plugin_cb_end(void)
{
    tcg_emit_op(INDEX_op_plugin_cb_end, 0);
}

#if TARGET_LONG_BITS == 32
#define tcg_temp_new() tcg_temp_new_i32()
#define tcg_global_mem_new tcg_global_mem_new_i32
#define tcg_temp_free tcg_temp_free_i32
#define tcgv_tl_temp tcgv_i32_temp
#define tcg_gen_qemu_ld_tl tcg_gen_qemu_ld_i32
#define tcg_gen_qemu_st_tl tcg_gen_qemu_st_i32
#else
#define tcg_temp_new() tcg_temp_new_i64()
#define tcg_global_mem_new tcg_global_mem_new_i64
#define tcg_temp_free tcg_temp_free_i64
#define tcgv_tl_temp tcgv_i64_temp
#define tcg_gen_qemu_ld_tl tcg_gen_qemu_ld_i64
#define tcg_gen_qemu_st_tl tcg_gen_qemu_st_i64
#endif

void tcg_gen_qemu_ld_i32_chk(TCGv_i32, TCGTemp *, TCGArg, MemOp, TCGType);
void tcg_gen_qemu_st_i32_chk(TCGv_i32, TCGTemp *, TCGArg, MemOp, TCGType);
void tcg_gen_qemu_ld_i64_chk(TCGv_i64, TCGTemp *, TCGArg, MemOp, TCGType);
void tcg_gen_qemu_st_i64_chk(TCGv_i64, TCGTemp *, TCGArg, MemOp, TCGType);
void tcg_gen_qemu_ld_i128_chk(TCGv_i128, TCGTemp *, TCGArg, MemOp, TCGType);
void tcg_gen_qemu_st_i128_chk(TCGv_i128, TCGTemp *, TCGArg, MemOp, TCGType);

static inline void
tcg_gen_qemu_ld_i32(TCGv_i32 v, TCGv a, TCGArg i, MemOp m)
{
    tcg_gen_qemu_ld_i32_chk(v, tcgv_tl_temp(a), i, m, TCG_TYPE_TL);
}

static inline void
tcg_gen_qemu_st_i32(TCGv_i32 v, TCGv a, TCGArg i, MemOp m)
{
    tcg_gen_qemu_st_i32_chk(v, tcgv_tl_temp(a), i, m, TCG_TYPE_TL);
}

static inline void
tcg_gen_qemu_ld_i64(TCGv_i64 v, TCGv a, TCGArg i, MemOp m)
{
    tcg_gen_qemu_ld_i64_chk(v, tcgv_tl_temp(a), i, m, TCG_TYPE_TL);
}

static inline void
tcg_gen_qemu_st_i64(TCGv_i64 v, TCGv a, TCGArg i, MemOp m)
{
    tcg_gen_qemu_st_i64_chk(v, tcgv_tl_temp(a), i, m, TCG_TYPE_TL);
}

static inline void
tcg_gen_qemu_ld_i128(TCGv_i128 v, TCGv a, TCGArg i, MemOp m)
{
    tcg_gen_qemu_ld_i128_chk(v, tcgv_tl_temp(a), i, m, TCG_TYPE_TL);
}

static inline void
tcg_gen_qemu_st_i128(TCGv_i128 v, TCGv a, TCGArg i, MemOp m)
{
    tcg_gen_qemu_st_i128_chk(v, tcgv_tl_temp(a), i, m, TCG_TYPE_TL);
}

void tcg_gen_atomic_cmpxchg_i32(TCGv_i32, TCGv, TCGv_i32, TCGv_i32,
                                TCGArg, MemOp);
void tcg_gen_atomic_cmpxchg_i64(TCGv_i64, TCGv, TCGv_i64, TCGv_i64,
                                TCGArg, MemOp);
void tcg_gen_atomic_cmpxchg_i128(TCGv_i128, TCGv, TCGv_i128, TCGv_i128,
                                 TCGArg, MemOp);

void tcg_gen_nonatomic_cmpxchg_i32(TCGv_i32, TCGv, TCGv_i32, TCGv_i32,
                                   TCGArg, MemOp);
void tcg_gen_nonatomic_cmpxchg_i64(TCGv_i64, TCGv, TCGv_i64, TCGv_i64,
                                   TCGArg, MemOp);
void tcg_gen_nonatomic_cmpxchg_i128(TCGv_i128, TCGv, TCGv_i128, TCGv_i128,
                                    TCGArg, MemOp);

void tcg_gen_atomic_xchg_i32(TCGv_i32, TCGv, TCGv_i32, TCGArg, MemOp);
void tcg_gen_atomic_xchg_i64(TCGv_i64, TCGv, TCGv_i64, TCGArg, MemOp);

void tcg_gen_atomic_fetch_add_i32(TCGv_i32, TCGv, TCGv_i32, TCGArg, MemOp);
void tcg_gen_atomic_fetch_add_i64(TCGv_i64, TCGv, TCGv_i64, TCGArg, MemOp);
void tcg_gen_atomic_fetch_and_i32(TCGv_i32, TCGv, TCGv_i32, TCGArg, MemOp);
void tcg_gen_atomic_fetch_and_i64(TCGv_i64, TCGv, TCGv_i64, TCGArg, MemOp);
void tcg_gen_atomic_fetch_or_i32(TCGv_i32, TCGv, TCGv_i32, TCGArg, MemOp);
void tcg_gen_atomic_fetch_or_i64(TCGv_i64, TCGv, TCGv_i64, TCGArg, MemOp);
void tcg_gen_atomic_fetch_xor_i32(TCGv_i32, TCGv, TCGv_i32, TCGArg, MemOp);
void tcg_gen_atomic_fetch_xor_i64(TCGv_i64, TCGv, TCGv_i64, TCGArg, MemOp);
void tcg_gen_atomic_fetch_smin_i32(TCGv_i32, TCGv, TCGv_i32, TCGArg, MemOp);
void tcg_gen_atomic_fetch_smin_i64(TCGv_i64, TCGv, TCGv_i64, TCGArg, MemOp);
void tcg_gen_atomic_fetch_umin_i32(TCGv_i32, TCGv, TCGv_i32, TCGArg, MemOp);
void tcg_gen_atomic_fetch_umin_i64(TCGv_i64, TCGv, TCGv_i64, TCGArg, MemOp);
void tcg_gen_atomic_fetch_smax_i32(TCGv_i32, TCGv, TCGv_i32, TCGArg, MemOp);
void tcg_gen_atomic_fetch_smax_i64(TCGv_i64, TCGv, TCGv_i64, TCGArg, MemOp);
void tcg_gen_atomic_fetch_umax_i32(TCGv_i32, TCGv, TCGv_i32, TCGArg, MemOp);
void tcg_gen_atomic_fetch_umax_i64(TCGv_i64, TCGv, TCGv_i64, TCGArg, MemOp);

void tcg_gen_atomic_add_fetch_i32(TCGv_i32, TCGv, TCGv_i32, TCGArg, MemOp);
void tcg_gen_atomic_add_fetch_i64(TCGv_i64, TCGv, TCGv_i64, TCGArg, MemOp);
void tcg_gen_atomic_and_fetch_i32(TCGv_i32, TCGv, TCGv_i32, TCGArg, MemOp);
void tcg_gen_atomic_and_fetch_i64(TCGv_i64, TCGv, TCGv_i64, TCGArg, MemOp);
void tcg_gen_atomic_or_fetch_i32(TCGv_i32, TCGv, TCGv_i32, TCGArg, MemOp);
void tcg_gen_atomic_or_fetch_i64(TCGv_i64, TCGv, TCGv_i64, TCGArg, MemOp);
void tcg_gen_atomic_xor_fetch_i32(TCGv_i32, TCGv, TCGv_i32, TCGArg, MemOp);
void tcg_gen_atomic_xor_fetch_i64(TCGv_i64, TCGv, TCGv_i64, TCGArg, MemOp);
void tcg_gen_atomic_smin_fetch_i32(TCGv_i32, TCGv, TCGv_i32, TCGArg, MemOp);
void tcg_gen_atomic_smin_fetch_i64(TCGv_i64, TCGv, TCGv_i64, TCGArg, MemOp);
void tcg_gen_atomic_umin_fetch_i32(TCGv_i32, TCGv, TCGv_i32, TCGArg, MemOp);
void tcg_gen_atomic_umin_fetch_i64(TCGv_i64, TCGv, TCGv_i64, TCGArg, MemOp);
void tcg_gen_atomic_smax_fetch_i32(TCGv_i32, TCGv, TCGv_i32, TCGArg, MemOp);
void tcg_gen_atomic_smax_fetch_i64(TCGv_i64, TCGv, TCGv_i64, TCGArg, MemOp);
void tcg_gen_atomic_umax_fetch_i32(TCGv_i32, TCGv, TCGv_i32, TCGArg, MemOp);
void tcg_gen_atomic_umax_fetch_i64(TCGv_i64, TCGv, TCGv_i64, TCGArg, MemOp);

void tcg_gen_mov_vec(TCGv_vec, TCGv_vec);
void tcg_gen_dup_i32_vec(unsigned vece, TCGv_vec, TCGv_i32);
void tcg_gen_dup_i64_vec(unsigned vece, TCGv_vec, TCGv_i64);
void tcg_gen_dup_mem_vec(unsigned vece, TCGv_vec, TCGv_ptr, tcg_target_long);
void tcg_gen_dupi_vec(unsigned vece, TCGv_vec, uint64_t);
void tcg_gen_add_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b);
void tcg_gen_sub_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b);
void tcg_gen_mul_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b);
void tcg_gen_and_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b);
void tcg_gen_or_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b);
void tcg_gen_xor_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b);
void tcg_gen_andc_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b);
void tcg_gen_orc_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b);
void tcg_gen_nand_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b);
void tcg_gen_nor_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b);
void tcg_gen_eqv_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b);
void tcg_gen_not_vec(unsigned vece, TCGv_vec r, TCGv_vec a);
void tcg_gen_neg_vec(unsigned vece, TCGv_vec r, TCGv_vec a);
void tcg_gen_abs_vec(unsigned vece, TCGv_vec r, TCGv_vec a);
void tcg_gen_ssadd_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b);
void tcg_gen_usadd_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b);
void tcg_gen_sssub_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b);
void tcg_gen_ussub_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b);
void tcg_gen_smin_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b);
void tcg_gen_umin_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b);
void tcg_gen_smax_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b);
void tcg_gen_umax_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b);

void tcg_gen_shli_vec(unsigned vece, TCGv_vec r, TCGv_vec a, int64_t i);
void tcg_gen_shri_vec(unsigned vece, TCGv_vec r, TCGv_vec a, int64_t i);
void tcg_gen_sari_vec(unsigned vece, TCGv_vec r, TCGv_vec a, int64_t i);
void tcg_gen_rotli_vec(unsigned vece, TCGv_vec r, TCGv_vec a, int64_t i);
void tcg_gen_rotri_vec(unsigned vece, TCGv_vec r, TCGv_vec a, int64_t i);

void tcg_gen_shls_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_i32 s);
void tcg_gen_shrs_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_i32 s);
void tcg_gen_sars_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_i32 s);
void tcg_gen_rotls_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_i32 s);

void tcg_gen_shlv_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec s);
void tcg_gen_shrv_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec s);
void tcg_gen_sarv_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec s);
void tcg_gen_rotlv_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec s);
void tcg_gen_rotrv_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec s);

void tcg_gen_cmp_vec(TCGCond cond, unsigned vece, TCGv_vec r,
                     TCGv_vec a, TCGv_vec b);

void tcg_gen_bitsel_vec(unsigned vece, TCGv_vec r, TCGv_vec a,
                        TCGv_vec b, TCGv_vec c);
void tcg_gen_cmpsel_vec(TCGCond cond, unsigned vece, TCGv_vec r,
                        TCGv_vec a, TCGv_vec b, TCGv_vec c, TCGv_vec d);

void tcg_gen_ld_vec(TCGv_vec r, TCGv_ptr base, TCGArg offset);
void tcg_gen_st_vec(TCGv_vec r, TCGv_ptr base, TCGArg offset);
void tcg_gen_stl_vec(TCGv_vec r, TCGv_ptr base, TCGArg offset, TCGType t);

#if TARGET_LONG_BITS == 64
#define tcg_gen_movi_tl tcg_gen_movi_i64
#define tcg_gen_mov_tl tcg_gen_mov_i64
#define tcg_gen_ld8u_tl tcg_gen_ld8u_i64
#define tcg_gen_ld8s_tl tcg_gen_ld8s_i64
#define tcg_gen_ld16u_tl tcg_gen_ld16u_i64
#define tcg_gen_ld16s_tl tcg_gen_ld16s_i64
#define tcg_gen_ld32u_tl tcg_gen_ld32u_i64
#define tcg_gen_ld32s_tl tcg_gen_ld32s_i64
#define tcg_gen_ld_tl tcg_gen_ld_i64
#define tcg_gen_st8_tl tcg_gen_st8_i64
#define tcg_gen_st16_tl tcg_gen_st16_i64
#define tcg_gen_st32_tl tcg_gen_st32_i64
#define tcg_gen_st_tl tcg_gen_st_i64
#define tcg_gen_add_tl tcg_gen_add_i64
#define tcg_gen_addi_tl tcg_gen_addi_i64
#define tcg_gen_sub_tl tcg_gen_sub_i64
#define tcg_gen_neg_tl tcg_gen_neg_i64
#define tcg_gen_abs_tl tcg_gen_abs_i64
#define tcg_gen_subfi_tl tcg_gen_subfi_i64
#define tcg_gen_subi_tl tcg_gen_subi_i64
#define tcg_gen_and_tl tcg_gen_and_i64
#define tcg_gen_andi_tl tcg_gen_andi_i64
#define tcg_gen_or_tl tcg_gen_or_i64
#define tcg_gen_ori_tl tcg_gen_ori_i64
#define tcg_gen_xor_tl tcg_gen_xor_i64
#define tcg_gen_xori_tl tcg_gen_xori_i64
#define tcg_gen_not_tl tcg_gen_not_i64
#define tcg_gen_shl_tl tcg_gen_shl_i64
#define tcg_gen_shli_tl tcg_gen_shli_i64
#define tcg_gen_shr_tl tcg_gen_shr_i64
#define tcg_gen_shri_tl tcg_gen_shri_i64
#define tcg_gen_sar_tl tcg_gen_sar_i64
#define tcg_gen_sari_tl tcg_gen_sari_i64
#define tcg_gen_brcond_tl tcg_gen_brcond_i64
#define tcg_gen_brcondi_tl tcg_gen_brcondi_i64
#define tcg_gen_setcond_tl tcg_gen_setcond_i64
#define tcg_gen_setcondi_tl tcg_gen_setcondi_i64
#define tcg_gen_mul_tl tcg_gen_mul_i64
#define tcg_gen_muli_tl tcg_gen_muli_i64
#define tcg_gen_div_tl tcg_gen_div_i64
#define tcg_gen_rem_tl tcg_gen_rem_i64
#define tcg_gen_divu_tl tcg_gen_divu_i64
#define tcg_gen_remu_tl tcg_gen_remu_i64
#define tcg_gen_discard_tl tcg_gen_discard_i64
#define tcg_gen_trunc_tl_i32 tcg_gen_extrl_i64_i32
#define tcg_gen_trunc_i64_tl tcg_gen_mov_i64
#define tcg_gen_extu_i32_tl tcg_gen_extu_i32_i64
#define tcg_gen_ext_i32_tl tcg_gen_ext_i32_i64
#define tcg_gen_extu_tl_i64 tcg_gen_mov_i64
#define tcg_gen_ext_tl_i64 tcg_gen_mov_i64
#define tcg_gen_ext8u_tl tcg_gen_ext8u_i64
#define tcg_gen_ext8s_tl tcg_gen_ext8s_i64
#define tcg_gen_ext16u_tl tcg_gen_ext16u_i64
#define tcg_gen_ext16s_tl tcg_gen_ext16s_i64
#define tcg_gen_ext32u_tl tcg_gen_ext32u_i64
#define tcg_gen_ext32s_tl tcg_gen_ext32s_i64
#define tcg_gen_bswap16_tl tcg_gen_bswap16_i64
#define tcg_gen_bswap32_tl tcg_gen_bswap32_i64
#define tcg_gen_bswap64_tl tcg_gen_bswap64_i64
#define tcg_gen_bswap_tl tcg_gen_bswap64_i64
#define tcg_gen_hswap_tl tcg_gen_hswap_i64
#define tcg_gen_wswap_tl tcg_gen_wswap_i64
#define tcg_gen_concat_tl_i64 tcg_gen_concat32_i64
#define tcg_gen_extr_i64_tl tcg_gen_extr32_i64
#define tcg_gen_andc_tl tcg_gen_andc_i64
#define tcg_gen_eqv_tl tcg_gen_eqv_i64
#define tcg_gen_nand_tl tcg_gen_nand_i64
#define tcg_gen_nor_tl tcg_gen_nor_i64
#define tcg_gen_orc_tl tcg_gen_orc_i64
#define tcg_gen_clz_tl tcg_gen_clz_i64
#define tcg_gen_ctz_tl tcg_gen_ctz_i64
#define tcg_gen_clzi_tl tcg_gen_clzi_i64
#define tcg_gen_ctzi_tl tcg_gen_ctzi_i64
#define tcg_gen_clrsb_tl tcg_gen_clrsb_i64
#define tcg_gen_ctpop_tl tcg_gen_ctpop_i64
#define tcg_gen_rotl_tl tcg_gen_rotl_i64
#define tcg_gen_rotli_tl tcg_gen_rotli_i64
#define tcg_gen_rotr_tl tcg_gen_rotr_i64
#define tcg_gen_rotri_tl tcg_gen_rotri_i64
#define tcg_gen_deposit_tl tcg_gen_deposit_i64
#define tcg_gen_deposit_z_tl tcg_gen_deposit_z_i64
#define tcg_gen_extract_tl tcg_gen_extract_i64
#define tcg_gen_sextract_tl tcg_gen_sextract_i64
#define tcg_gen_extract2_tl tcg_gen_extract2_i64
#define tcg_constant_tl tcg_constant_i64
#define tcg_gen_movcond_tl tcg_gen_movcond_i64
#define tcg_gen_add2_tl tcg_gen_add2_i64
#define tcg_gen_sub2_tl tcg_gen_sub2_i64
#define tcg_gen_mulu2_tl tcg_gen_mulu2_i64
#define tcg_gen_muls2_tl tcg_gen_muls2_i64
#define tcg_gen_mulsu2_tl tcg_gen_mulsu2_i64
#define tcg_gen_smin_tl tcg_gen_smin_i64
#define tcg_gen_umin_tl tcg_gen_umin_i64
#define tcg_gen_smax_tl tcg_gen_smax_i64
#define tcg_gen_umax_tl tcg_gen_umax_i64
#define tcg_gen_atomic_cmpxchg_tl tcg_gen_atomic_cmpxchg_i64
#define tcg_gen_atomic_xchg_tl tcg_gen_atomic_xchg_i64
#define tcg_gen_atomic_fetch_add_tl tcg_gen_atomic_fetch_add_i64
#define tcg_gen_atomic_fetch_and_tl tcg_gen_atomic_fetch_and_i64
#define tcg_gen_atomic_fetch_or_tl tcg_gen_atomic_fetch_or_i64
#define tcg_gen_atomic_fetch_xor_tl tcg_gen_atomic_fetch_xor_i64
#define tcg_gen_atomic_fetch_smin_tl tcg_gen_atomic_fetch_smin_i64
#define tcg_gen_atomic_fetch_umin_tl tcg_gen_atomic_fetch_umin_i64
#define tcg_gen_atomic_fetch_smax_tl tcg_gen_atomic_fetch_smax_i64
#define tcg_gen_atomic_fetch_umax_tl tcg_gen_atomic_fetch_umax_i64
#define tcg_gen_atomic_add_fetch_tl tcg_gen_atomic_add_fetch_i64
#define tcg_gen_atomic_and_fetch_tl tcg_gen_atomic_and_fetch_i64
#define tcg_gen_atomic_or_fetch_tl tcg_gen_atomic_or_fetch_i64
#define tcg_gen_atomic_xor_fetch_tl tcg_gen_atomic_xor_fetch_i64
#define tcg_gen_atomic_smin_fetch_tl tcg_gen_atomic_smin_fetch_i64
#define tcg_gen_atomic_umin_fetch_tl tcg_gen_atomic_umin_fetch_i64
#define tcg_gen_atomic_smax_fetch_tl tcg_gen_atomic_smax_fetch_i64
#define tcg_gen_atomic_umax_fetch_tl tcg_gen_atomic_umax_fetch_i64
#define tcg_gen_dup_tl_vec  tcg_gen_dup_i64_vec
#define tcg_gen_dup_tl tcg_gen_dup_i64
#else
#define tcg_gen_movi_tl tcg_gen_movi_i32
#define tcg_gen_mov_tl tcg_gen_mov_i32
#define tcg_gen_ld8u_tl tcg_gen_ld8u_i32
#define tcg_gen_ld8s_tl tcg_gen_ld8s_i32
#define tcg_gen_ld16u_tl tcg_gen_ld16u_i32
#define tcg_gen_ld16s_tl tcg_gen_ld16s_i32
#define tcg_gen_ld32u_tl tcg_gen_ld_i32
#define tcg_gen_ld32s_tl tcg_gen_ld_i32
#define tcg_gen_ld_tl tcg_gen_ld_i32
#define tcg_gen_st8_tl tcg_gen_st8_i32
#define tcg_gen_st16_tl tcg_gen_st16_i32
#define tcg_gen_st32_tl tcg_gen_st_i32
#define tcg_gen_st_tl tcg_gen_st_i32
#define tcg_gen_add_tl tcg_gen_add_i32
#define tcg_gen_addi_tl tcg_gen_addi_i32
#define tcg_gen_sub_tl tcg_gen_sub_i32
#define tcg_gen_neg_tl tcg_gen_neg_i32
#define tcg_gen_abs_tl tcg_gen_abs_i32
#define tcg_gen_subfi_tl tcg_gen_subfi_i32
#define tcg_gen_subi_tl tcg_gen_subi_i32
#define tcg_gen_and_tl tcg_gen_and_i32
#define tcg_gen_andi_tl tcg_gen_andi_i32
#define tcg_gen_or_tl tcg_gen_or_i32
#define tcg_gen_ori_tl tcg_gen_ori_i32
#define tcg_gen_xor_tl tcg_gen_xor_i32
#define tcg_gen_xori_tl tcg_gen_xori_i32
#define tcg_gen_not_tl tcg_gen_not_i32
#define tcg_gen_shl_tl tcg_gen_shl_i32
#define tcg_gen_shli_tl tcg_gen_shli_i32
#define tcg_gen_shr_tl tcg_gen_shr_i32
#define tcg_gen_shri_tl tcg_gen_shri_i32
#define tcg_gen_sar_tl tcg_gen_sar_i32
#define tcg_gen_sari_tl tcg_gen_sari_i32
#define tcg_gen_brcond_tl tcg_gen_brcond_i32
#define tcg_gen_brcondi_tl tcg_gen_brcondi_i32
#define tcg_gen_setcond_tl tcg_gen_setcond_i32
#define tcg_gen_setcondi_tl tcg_gen_setcondi_i32
#define tcg_gen_mul_tl tcg_gen_mul_i32
#define tcg_gen_muli_tl tcg_gen_muli_i32
#define tcg_gen_div_tl tcg_gen_div_i32
#define tcg_gen_rem_tl tcg_gen_rem_i32
#define tcg_gen_divu_tl tcg_gen_divu_i32
#define tcg_gen_remu_tl tcg_gen_remu_i32
#define tcg_gen_discard_tl tcg_gen_discard_i32
#define tcg_gen_trunc_tl_i32 tcg_gen_mov_i32
#define tcg_gen_trunc_i64_tl tcg_gen_extrl_i64_i32
#define tcg_gen_extu_i32_tl tcg_gen_mov_i32
#define tcg_gen_ext_i32_tl tcg_gen_mov_i32
#define tcg_gen_extu_tl_i64 tcg_gen_extu_i32_i64
#define tcg_gen_ext_tl_i64 tcg_gen_ext_i32_i64
#define tcg_gen_ext8u_tl tcg_gen_ext8u_i32
#define tcg_gen_ext8s_tl tcg_gen_ext8s_i32
#define tcg_gen_ext16u_tl tcg_gen_ext16u_i32
#define tcg_gen_ext16s_tl tcg_gen_ext16s_i32
#define tcg_gen_ext32u_tl tcg_gen_mov_i32
#define tcg_gen_ext32s_tl tcg_gen_mov_i32
#define tcg_gen_bswap16_tl tcg_gen_bswap16_i32
#define tcg_gen_bswap32_tl(D, S, F) tcg_gen_bswap32_i32(D, S)
#define tcg_gen_bswap_tl tcg_gen_bswap32_i32
#define tcg_gen_hswap_tl tcg_gen_hswap_i32
#define tcg_gen_concat_tl_i64 tcg_gen_concat_i32_i64
#define tcg_gen_extr_i64_tl tcg_gen_extr_i64_i32
#define tcg_gen_andc_tl tcg_gen_andc_i32
#define tcg_gen_eqv_tl tcg_gen_eqv_i32
#define tcg_gen_nand_tl tcg_gen_nand_i32
#define tcg_gen_nor_tl tcg_gen_nor_i32
#define tcg_gen_orc_tl tcg_gen_orc_i32
#define tcg_gen_clz_tl tcg_gen_clz_i32
#define tcg_gen_ctz_tl tcg_gen_ctz_i32
#define tcg_gen_clzi_tl tcg_gen_clzi_i32
#define tcg_gen_ctzi_tl tcg_gen_ctzi_i32
#define tcg_gen_clrsb_tl tcg_gen_clrsb_i32
#define tcg_gen_ctpop_tl tcg_gen_ctpop_i32
#define tcg_gen_rotl_tl tcg_gen_rotl_i32
#define tcg_gen_rotli_tl tcg_gen_rotli_i32
#define tcg_gen_rotr_tl tcg_gen_rotr_i32
#define tcg_gen_rotri_tl tcg_gen_rotri_i32
#define tcg_gen_deposit_tl tcg_gen_deposit_i32
#define tcg_gen_deposit_z_tl tcg_gen_deposit_z_i32
#define tcg_gen_extract_tl tcg_gen_extract_i32
#define tcg_gen_sextract_tl tcg_gen_sextract_i32
#define tcg_gen_extract2_tl tcg_gen_extract2_i32
#define tcg_constant_tl tcg_constant_i32
#define tcg_gen_movcond_tl tcg_gen_movcond_i32
#define tcg_gen_add2_tl tcg_gen_add2_i32
#define tcg_gen_sub2_tl tcg_gen_sub2_i32
#define tcg_gen_mulu2_tl tcg_gen_mulu2_i32
#define tcg_gen_muls2_tl tcg_gen_muls2_i32
#define tcg_gen_mulsu2_tl tcg_gen_mulsu2_i32
#define tcg_gen_smin_tl tcg_gen_smin_i32
#define tcg_gen_umin_tl tcg_gen_umin_i32
#define tcg_gen_smax_tl tcg_gen_smax_i32
#define tcg_gen_umax_tl tcg_gen_umax_i32
#define tcg_gen_atomic_cmpxchg_tl tcg_gen_atomic_cmpxchg_i32
#define tcg_gen_atomic_xchg_tl tcg_gen_atomic_xchg_i32
#define tcg_gen_atomic_fetch_add_tl tcg_gen_atomic_fetch_add_i32
#define tcg_gen_atomic_fetch_and_tl tcg_gen_atomic_fetch_and_i32
#define tcg_gen_atomic_fetch_or_tl tcg_gen_atomic_fetch_or_i32
#define tcg_gen_atomic_fetch_xor_tl tcg_gen_atomic_fetch_xor_i32
#define tcg_gen_atomic_fetch_smin_tl tcg_gen_atomic_fetch_smin_i32
#define tcg_gen_atomic_fetch_umin_tl tcg_gen_atomic_fetch_umin_i32
#define tcg_gen_atomic_fetch_smax_tl tcg_gen_atomic_fetch_smax_i32
#define tcg_gen_atomic_fetch_umax_tl tcg_gen_atomic_fetch_umax_i32
#define tcg_gen_atomic_add_fetch_tl tcg_gen_atomic_add_fetch_i32
#define tcg_gen_atomic_and_fetch_tl tcg_gen_atomic_and_fetch_i32
#define tcg_gen_atomic_or_fetch_tl tcg_gen_atomic_or_fetch_i32
#define tcg_gen_atomic_xor_fetch_tl tcg_gen_atomic_xor_fetch_i32
#define tcg_gen_atomic_smin_fetch_tl tcg_gen_atomic_smin_fetch_i32
#define tcg_gen_atomic_umin_fetch_tl tcg_gen_atomic_umin_fetch_i32
#define tcg_gen_atomic_smax_fetch_tl tcg_gen_atomic_smax_fetch_i32
#define tcg_gen_atomic_umax_fetch_tl tcg_gen_atomic_umax_fetch_i32
#define tcg_gen_dup_tl_vec  tcg_gen_dup_i32_vec
#define tcg_gen_dup_tl tcg_gen_dup_i32
#endif

#if UINTPTR_MAX == UINT32_MAX
# define PTR  i32
# define NAT  TCGv_i32
#else
# define PTR  i64
# define NAT  TCGv_i64
#endif

static inline void tcg_gen_ld_ptr(TCGv_ptr r, TCGv_ptr a, intptr_t o)
{
    glue(tcg_gen_ld_,PTR)((NAT)r, a, o);
}

static inline void tcg_gen_st_ptr(TCGv_ptr r, TCGv_ptr a, intptr_t o)
{
    glue(tcg_gen_st_, PTR)((NAT)r, a, o);
}

static inline void tcg_gen_discard_ptr(TCGv_ptr a)
{
    glue(tcg_gen_discard_,PTR)((NAT)a);
}

static inline void tcg_gen_add_ptr(TCGv_ptr r, TCGv_ptr a, TCGv_ptr b)
{
    glue(tcg_gen_add_,PTR)((NAT)r, (NAT)a, (NAT)b);
}

static inline void tcg_gen_addi_ptr(TCGv_ptr r, TCGv_ptr a, intptr_t b)
{
    glue(tcg_gen_addi_,PTR)((NAT)r, (NAT)a, b);
}

static inline void tcg_gen_mov_ptr(TCGv_ptr d, TCGv_ptr s)
{
    glue(tcg_gen_mov_,PTR)((NAT)d, (NAT)s);
}

static inline void tcg_gen_movi_ptr(TCGv_ptr d, intptr_t s)
{
    glue(tcg_gen_movi_,PTR)((NAT)d, s);
}

static inline void tcg_gen_brcondi_ptr(TCGCond cond, TCGv_ptr a,
                                       intptr_t b, TCGLabel *label)
{
    glue(tcg_gen_brcondi_,PTR)(cond, (NAT)a, b, label);
}

static inline void tcg_gen_ext_i32_ptr(TCGv_ptr r, TCGv_i32 a)
{
#if UINTPTR_MAX == UINT32_MAX
    tcg_gen_mov_i32((NAT)r, a);
#else
    tcg_gen_ext_i32_i64((NAT)r, a);
#endif
}

static inline void tcg_gen_trunc_i64_ptr(TCGv_ptr r, TCGv_i64 a)
{
#if UINTPTR_MAX == UINT32_MAX
    tcg_gen_extrl_i64_i32((NAT)r, a);
#else
    tcg_gen_mov_i64((NAT)r, a);
#endif
}

static inline void tcg_gen_extu_ptr_i64(TCGv_i64 r, TCGv_ptr a)
{
#if UINTPTR_MAX == UINT32_MAX
    tcg_gen_extu_i32_i64(r, (NAT)a);
#else
    tcg_gen_mov_i64(r, (NAT)a);
#endif
}

static inline void tcg_gen_trunc_ptr_i32(TCGv_i32 r, TCGv_ptr a)
{
#if UINTPTR_MAX == UINT32_MAX
    tcg_gen_mov_i32(r, (NAT)a);
#else
    tcg_gen_extrl_i64_i32(r, (NAT)a);
#endif
}

#undef PTR
#undef NAT

#endif /* TCG_TCG_OP_H */
