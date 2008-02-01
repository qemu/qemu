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
#include "tcg.h"

/* legacy dyngen operations */
#include "gen-op.h"

int gen_new_label(void);

static inline void tcg_gen_op1(int opc, TCGArg arg1)
{
    *gen_opc_ptr++ = opc;
    *gen_opparam_ptr++ = arg1;
}

static inline void tcg_gen_op2(int opc, TCGArg arg1, TCGArg arg2)
{
    *gen_opc_ptr++ = opc;
    *gen_opparam_ptr++ = arg1;
    *gen_opparam_ptr++ = arg2;
}

static inline void tcg_gen_op3(int opc, TCGArg arg1, TCGArg arg2, TCGArg arg3)
{
    *gen_opc_ptr++ = opc;
    *gen_opparam_ptr++ = arg1;
    *gen_opparam_ptr++ = arg2;
    *gen_opparam_ptr++ = arg3;
}

static inline void tcg_gen_op4(int opc, TCGArg arg1, TCGArg arg2, TCGArg arg3, 
                               TCGArg arg4)
{
    *gen_opc_ptr++ = opc;
    *gen_opparam_ptr++ = arg1;
    *gen_opparam_ptr++ = arg2;
    *gen_opparam_ptr++ = arg3;
    *gen_opparam_ptr++ = arg4;
}

static inline void tcg_gen_op5(int opc, TCGArg arg1, TCGArg arg2, 
                               TCGArg arg3, TCGArg arg4,
                               TCGArg arg5)
{
    *gen_opc_ptr++ = opc;
    *gen_opparam_ptr++ = arg1;
    *gen_opparam_ptr++ = arg2;
    *gen_opparam_ptr++ = arg3;
    *gen_opparam_ptr++ = arg4;
    *gen_opparam_ptr++ = arg5;
}

static inline void tcg_gen_op6(int opc, TCGArg arg1, TCGArg arg2, 
                               TCGArg arg3, TCGArg arg4,
                               TCGArg arg5, TCGArg arg6)
{
    *gen_opc_ptr++ = opc;
    *gen_opparam_ptr++ = arg1;
    *gen_opparam_ptr++ = arg2;
    *gen_opparam_ptr++ = arg3;
    *gen_opparam_ptr++ = arg4;
    *gen_opparam_ptr++ = arg5;
    *gen_opparam_ptr++ = arg6;
}

static inline void gen_set_label(int n)
{
    tcg_gen_op1(INDEX_op_set_label, n);
}

static inline void tcg_gen_mov_i32(int ret, int arg)
{
    tcg_gen_op2(INDEX_op_mov_i32, ret, arg);
}

static inline void tcg_gen_movi_i32(int ret, int32_t arg)
{
    tcg_gen_op2(INDEX_op_movi_i32, ret, arg);
}

/* helper calls */
#define TCG_HELPER_CALL_FLAGS 0

static inline void tcg_gen_helper_0_0(void *func)
{
    tcg_gen_call(&tcg_ctx, 
                 tcg_const_ptr((tcg_target_long)func), TCG_HELPER_CALL_FLAGS,
                 0, NULL, 0, NULL);
}

static inline void tcg_gen_helper_0_1(void *func, TCGArg arg)
{
    tcg_gen_call(&tcg_ctx,
                 tcg_const_ptr((tcg_target_long)func), TCG_HELPER_CALL_FLAGS,
                 0, NULL, 1, &arg);
}

static inline void tcg_gen_helper_0_2(void *func, TCGArg arg1, TCGArg arg2)
{
    TCGArg args[2];
    args[0] = arg1;
    args[1] = arg2;
    tcg_gen_call(&tcg_ctx, 
                 tcg_const_ptr((tcg_target_long)func), TCG_HELPER_CALL_FLAGS,
                 0, NULL, 2, args);
}

static inline void tcg_gen_helper_1_2(void *func, TCGArg ret, 
                                      TCGArg arg1, TCGArg arg2)
{
    TCGArg args[2];
    args[0] = arg1;
    args[1] = arg2;
    tcg_gen_call(&tcg_ctx, 
                 tcg_const_ptr((tcg_target_long)func), TCG_HELPER_CALL_FLAGS,
                 1, &ret, 2, args);
}

/* 32 bit ops */

static inline void tcg_gen_ld8u_i32(int ret, int arg2, tcg_target_long offset)
{
    tcg_gen_op3(INDEX_op_ld8u_i32, ret, arg2, offset);
}

static inline void tcg_gen_ld8s_i32(int ret, int arg2, tcg_target_long offset)
{
    tcg_gen_op3(INDEX_op_ld8s_i32, ret, arg2, offset);
}

static inline void tcg_gen_ld16u_i32(int ret, int arg2, tcg_target_long offset)
{
    tcg_gen_op3(INDEX_op_ld16u_i32, ret, arg2, offset);
}

static inline void tcg_gen_ld16s_i32(int ret, int arg2, tcg_target_long offset)
{
    tcg_gen_op3(INDEX_op_ld16s_i32, ret, arg2, offset);
}

static inline void tcg_gen_ld_i32(int ret, int arg2, tcg_target_long offset)
{
    tcg_gen_op3(INDEX_op_ld_i32, ret, arg2, offset);
}

static inline void tcg_gen_st8_i32(int arg1, int arg2, tcg_target_long offset)
{
    tcg_gen_op3(INDEX_op_st8_i32, arg1, arg2, offset);
}

static inline void tcg_gen_st16_i32(int arg1, int arg2, tcg_target_long offset)
{
    tcg_gen_op3(INDEX_op_st16_i32, arg1, arg2, offset);
}

static inline void tcg_gen_st_i32(int arg1, int arg2, tcg_target_long offset)
{
    tcg_gen_op3(INDEX_op_st_i32, arg1, arg2, offset);
}

static inline void tcg_gen_add_i32(int ret, int arg1, int arg2)
{
    tcg_gen_op3(INDEX_op_add_i32, ret, arg1, arg2);
}

static inline void tcg_gen_addi_i32(int ret, int arg1, int32_t arg2)
{
    tcg_gen_add_i32(ret, arg1, tcg_const_i32(arg2));
}

static inline void tcg_gen_sub_i32(int ret, int arg1, int arg2)
{
    tcg_gen_op3(INDEX_op_sub_i32, ret, arg1, arg2);
}

static inline void tcg_gen_subi_i32(int ret, int arg1, int32_t arg2)
{
    tcg_gen_sub_i32(ret, arg1, tcg_const_i32(arg2));
}

static inline void tcg_gen_and_i32(int ret, int arg1, int arg2)
{
    tcg_gen_op3(INDEX_op_and_i32, ret, arg1, arg2);
}

static inline void tcg_gen_andi_i32(int ret, int arg1, int32_t arg2)
{
    /* some cases can be optimized here */
    if (arg2 == 0) {
        tcg_gen_movi_i32(ret, 0);
    } else if (arg2 == 0xffffffff) {
        tcg_gen_mov_i32(ret, arg1);
    } else {
        tcg_gen_and_i32(ret, arg1, tcg_const_i32(arg2));
    }
}

static inline void tcg_gen_or_i32(int ret, int arg1, int arg2)
{
    tcg_gen_op3(INDEX_op_or_i32, ret, arg1, arg2);
}

static inline void tcg_gen_ori_i32(int ret, int arg1, int32_t arg2)
{
    /* some cases can be optimized here */
    if (arg2 == 0xffffffff) {
        tcg_gen_movi_i32(ret, 0);
    } else if (arg2 == 0) {
        tcg_gen_mov_i32(ret, arg1);
    } else {
        tcg_gen_or_i32(ret, arg1, tcg_const_i32(arg2));
    }
}

static inline void tcg_gen_xor_i32(int ret, int arg1, int arg2)
{
    tcg_gen_op3(INDEX_op_xor_i32, ret, arg1, arg2);
}

static inline void tcg_gen_xori_i32(int ret, int arg1, int32_t arg2)
{
    /* some cases can be optimized here */
    if (arg2 == 0) {
        tcg_gen_mov_i32(ret, arg1);
    } else {
        tcg_gen_xor_i32(ret, arg1, tcg_const_i32(arg2));
    }
}

static inline void tcg_gen_shl_i32(int ret, int arg1, int arg2)
{
    tcg_gen_op3(INDEX_op_shl_i32, ret, arg1, arg2);
}

static inline void tcg_gen_shli_i32(int ret, int arg1, int32_t arg2)
{
    tcg_gen_shl_i32(ret, arg1, tcg_const_i32(arg2));
}

static inline void tcg_gen_shr_i32(int ret, int arg1, int arg2)
{
    tcg_gen_op3(INDEX_op_shr_i32, ret, arg1, arg2);
}

static inline void tcg_gen_shri_i32(int ret, int arg1, int32_t arg2)
{
    tcg_gen_shr_i32(ret, arg1, tcg_const_i32(arg2));
}

static inline void tcg_gen_sar_i32(int ret, int arg1, int arg2)
{
    tcg_gen_op3(INDEX_op_sar_i32, ret, arg1, arg2);
}

static inline void tcg_gen_sari_i32(int ret, int arg1, int32_t arg2)
{
    tcg_gen_sar_i32(ret, arg1, tcg_const_i32(arg2));
}

static inline void tcg_gen_brcond_i32(int cond, TCGArg arg1, TCGArg arg2, 
                                      int label_index)
{
    tcg_gen_op4(INDEX_op_brcond_i32, arg1, arg2, cond, label_index);
}

static inline void tcg_gen_mul_i32(int ret, int arg1, int arg2)
{
    tcg_gen_op3(INDEX_op_mul_i32, ret, arg1, arg2);
}

#ifdef TCG_TARGET_HAS_div_i32
static inline void tcg_gen_div_i32(int ret, int arg1, int arg2)
{
    tcg_gen_op3(INDEX_op_div_i32, ret, arg1, arg2);
}

static inline void tcg_gen_rem_i32(int ret, int arg1, int arg2)
{
    tcg_gen_op3(INDEX_op_rem_i32, ret, arg1, arg2);
}

static inline void tcg_gen_divu_i32(int ret, int arg1, int arg2)
{
    tcg_gen_op3(INDEX_op_divu_i32, ret, arg1, arg2);
}

static inline void tcg_gen_remu_i32(int ret, int arg1, int arg2)
{
    tcg_gen_op3(INDEX_op_remu_i32, ret, arg1, arg2);
}
#else
static inline void tcg_gen_div_i32(int ret, int arg1, int arg2)
{
    int t0;
    t0 = tcg_temp_new(TCG_TYPE_I32);
    tcg_gen_sari_i32(t0, arg1, 31);
    tcg_gen_op5(INDEX_op_div2_i32, ret, t0, arg1, t0, arg2);
}

static inline void tcg_gen_rem_i32(int ret, int arg1, int arg2)
{
    int t0;
    t0 = tcg_temp_new(TCG_TYPE_I32);
    tcg_gen_sari_i32(t0, arg1, 31);
    tcg_gen_op5(INDEX_op_div2_i32, t0, ret, arg1, t0, arg2);
}

static inline void tcg_gen_divu_i32(int ret, int arg1, int arg2)
{
    int t0;
    t0 = tcg_temp_new(TCG_TYPE_I32);
    tcg_gen_movi_i32(t0, 0);
    tcg_gen_op5(INDEX_op_divu2_i32, ret, t0, arg1, t0, arg2);
}

static inline void tcg_gen_remu_i32(int ret, int arg1, int arg2)
{
    int t0;
    t0 = tcg_temp_new(TCG_TYPE_I32);
    tcg_gen_movi_i32(t0, 0);
    tcg_gen_op5(INDEX_op_divu2_i32, t0, ret, arg1, t0, arg2);
}
#endif

#if TCG_TARGET_REG_BITS == 32

static inline void tcg_gen_mov_i64(int ret, int arg)
{
    tcg_gen_mov_i32(ret, arg);
    tcg_gen_mov_i32(ret + 1, arg + 1);
}

static inline void tcg_gen_movi_i64(int ret, int64_t arg)
{
    tcg_gen_movi_i32(ret, arg);
    tcg_gen_movi_i32(ret + 1, arg >> 32);
}

static inline void tcg_gen_ld8u_i64(int ret, int arg2, tcg_target_long offset)
{
    tcg_gen_ld8u_i32(ret, arg2, offset);
    tcg_gen_movi_i32(ret + 1, 0);
}

static inline void tcg_gen_ld8s_i64(int ret, int arg2, tcg_target_long offset)
{
    tcg_gen_ld8s_i32(ret, arg2, offset);
    tcg_gen_sari_i32(ret + 1, ret, 31);
}

static inline void tcg_gen_ld16u_i64(int ret, int arg2, tcg_target_long offset)
{
    tcg_gen_ld16u_i32(ret, arg2, offset);
    tcg_gen_movi_i32(ret + 1, 0);
}

static inline void tcg_gen_ld16s_i64(int ret, int arg2, tcg_target_long offset)
{
    tcg_gen_ld16s_i32(ret, arg2, offset);
    tcg_gen_sari_i32(ret + 1, ret, 31);
}

static inline void tcg_gen_ld32u_i64(int ret, int arg2, tcg_target_long offset)
{
    tcg_gen_ld_i32(ret, arg2, offset);
    tcg_gen_movi_i32(ret + 1, 0);
}

static inline void tcg_gen_ld32s_i64(int ret, int arg2, tcg_target_long offset)
{
    tcg_gen_ld_i32(ret, arg2, offset);
    tcg_gen_sari_i32(ret + 1, ret, 31);
}

static inline void tcg_gen_ld_i64(int ret, int arg2, tcg_target_long offset)
{
    /* since arg2 and ret have different types, they cannot be the
       same temporary */
#ifdef TCG_TARGET_WORDS_BIGENDIAN
    tcg_gen_ld_i32(ret + 1, arg2, offset);
    tcg_gen_ld_i32(ret, arg2, offset + 4);
#else
    tcg_gen_ld_i32(ret, arg2, offset);
    tcg_gen_ld_i32(ret + 1, arg2, offset + 4);
#endif
}

static inline void tcg_gen_st8_i64(int arg1, int arg2, tcg_target_long offset)
{
    tcg_gen_st8_i32(arg1, arg2, offset);
}

static inline void tcg_gen_st16_i64(int arg1, int arg2, tcg_target_long offset)
{
    tcg_gen_st16_i32(arg1, arg2, offset);
}

static inline void tcg_gen_st32_i64(int arg1, int arg2, tcg_target_long offset)
{
    tcg_gen_st_i32(arg1, arg2, offset);
}

static inline void tcg_gen_st_i64(int arg1, int arg2, tcg_target_long offset)
{
#ifdef TCG_TARGET_WORDS_BIGENDIAN
    tcg_gen_st_i32(arg1 + 1, arg2, offset);
    tcg_gen_st_i32(arg1, arg2, offset + 4);
#else
    tcg_gen_st_i32(arg1, arg2, offset);
    tcg_gen_st_i32(arg1 + 1, arg2, offset + 4);
#endif
}

static inline void tcg_gen_add_i64(int ret, int arg1, int arg2)
{
    tcg_gen_op6(INDEX_op_add2_i32, ret, ret + 1, 
                arg1, arg1 + 1, arg2, arg2 + 1);
}

static inline void tcg_gen_addi_i64(int ret, int arg1, int64_t arg2)
{
    tcg_gen_add_i64(ret, arg1, tcg_const_i64(arg2));
}

static inline void tcg_gen_sub_i64(int ret, int arg1, int arg2)
{
    tcg_gen_op6(INDEX_op_sub2_i32, ret, ret + 1, 
                arg1, arg1 + 1, arg2, arg2 + 1);
}

static inline void tcg_gen_subi_i64(int ret, int arg1, int64_t arg2)
{
    tcg_gen_sub_i64(ret, arg1, tcg_const_i64(arg2));
}

static inline void tcg_gen_and_i64(int ret, int arg1, int arg2)
{
    tcg_gen_and_i32(ret, arg1, arg2);
    tcg_gen_and_i32(ret + 1, arg1 + 1, arg2 + 1);
}

static inline void tcg_gen_andi_i64(int ret, int arg1, int64_t arg2)
{
    tcg_gen_andi_i32(ret, arg1, arg2);
    tcg_gen_andi_i32(ret + 1, arg1 + 1, arg2 >> 32);
}

static inline void tcg_gen_or_i64(int ret, int arg1, int arg2)
{
    tcg_gen_or_i32(ret, arg1, arg2);
    tcg_gen_or_i32(ret + 1, arg1 + 1, arg2 + 1);
}

static inline void tcg_gen_ori_i64(int ret, int arg1, int64_t arg2)
{
    tcg_gen_ori_i32(ret, arg1, arg2);
    tcg_gen_ori_i32(ret + 1, arg1 + 1, arg2 >> 32);
}

static inline void tcg_gen_xor_i64(int ret, int arg1, int arg2)
{
    tcg_gen_xor_i32(ret, arg1, arg2);
    tcg_gen_xor_i32(ret + 1, arg1 + 1, arg2 + 1);
}

static inline void tcg_gen_xori_i64(int ret, int arg1, int64_t arg2)
{
    tcg_gen_xori_i32(ret, arg1, arg2);
    tcg_gen_xori_i32(ret + 1, arg1 + 1, arg2 >> 32);
}

/* XXX: use generic code when basic block handling is OK or CPU
   specific code (x86) */
static inline void tcg_gen_shl_i64(int ret, int arg1, int64_t arg2)
{
    tcg_gen_helper_1_2(tcg_helper_shl_i64, ret, arg1, arg2);
}

static inline void tcg_gen_shli_i64(int ret, int arg1, int64_t arg2)
{
    tcg_gen_shifti_i64(ret, arg1, arg2, 0, 0);
}

static inline void tcg_gen_shr_i64(int ret, int arg1, int64_t arg2)
{
    tcg_gen_helper_1_2(tcg_helper_shr_i64, ret, arg1, arg2);
}

static inline void tcg_gen_shri_i64(int ret, int arg1, int64_t arg2)
{
    tcg_gen_shifti_i64(ret, arg1, arg2, 1, 0);
}

static inline void tcg_gen_sar_i64(int ret, int arg1, int64_t arg2)
{
    tcg_gen_helper_1_2(tcg_helper_sar_i64, ret, arg1, arg2);
}

static inline void tcg_gen_sari_i64(int ret, int arg1, int64_t arg2)
{
    tcg_gen_shifti_i64(ret, arg1, arg2, 1, 1);
}

static inline void tcg_gen_brcond_i64(int cond, TCGArg arg1, TCGArg arg2, 
                                      int label_index)
{
    tcg_gen_op6(INDEX_op_brcond2_i32, 
                arg1, arg1 + 1, arg2, arg2 + 1, cond, label_index);
}

static inline void tcg_gen_mul_i64(int ret, int arg1, int arg2)
{
    int t0, t1;
    
    t0 = tcg_temp_new(TCG_TYPE_I64);
    t1 = tcg_temp_new(TCG_TYPE_I32);

    tcg_gen_op4(INDEX_op_mulu2_i32, t0, t0 + 1, arg1, arg2);
    
    tcg_gen_mul_i32(t1, arg1, arg2 + 1);
    tcg_gen_add_i32(t0 + 1, t0 + 1, t1);
    tcg_gen_mul_i32(t1, arg1 + 1, arg2);
    tcg_gen_add_i32(t0 + 1, t0 + 1, t1);
    
    tcg_gen_mov_i64(ret, t0);
}

static inline void tcg_gen_div_i64(int ret, int arg1, int64_t arg2)
{
    tcg_gen_helper_1_2(tcg_helper_div_i64, ret, arg1, arg2);
}

static inline void tcg_gen_rem_i64(int ret, int arg1, int64_t arg2)
{
    tcg_gen_helper_1_2(tcg_helper_rem_i64, ret, arg1, arg2);
}

static inline void tcg_gen_divu_i64(int ret, int arg1, int64_t arg2)
{
    tcg_gen_helper_1_2(tcg_helper_divu_i64, ret, arg1, arg2);
}

static inline void tcg_gen_remu_i64(int ret, int arg1, int64_t arg2)
{
    tcg_gen_helper_1_2(tcg_helper_remu_i64, ret, arg1, arg2);
}

#else

static inline void tcg_gen_mov_i64(int ret, int arg)
{
    tcg_gen_op2(INDEX_op_mov_i64, ret, arg);
}

static inline void tcg_gen_movi_i64(int ret, int64_t arg)
{
    tcg_gen_op2(INDEX_op_movi_i64, ret, arg);
}

static inline void tcg_gen_ld8u_i64(int ret, int arg2, tcg_target_long offset)
{
    tcg_gen_op3(INDEX_op_ld8u_i64, ret, arg2, offset);
}

static inline void tcg_gen_ld8s_i64(int ret, int arg2, tcg_target_long offset)
{
    tcg_gen_op3(INDEX_op_ld8s_i64, ret, arg2, offset);
}

static inline void tcg_gen_ld16u_i64(int ret, int arg2, tcg_target_long offset)
{
    tcg_gen_op3(INDEX_op_ld16u_i64, ret, arg2, offset);
}

static inline void tcg_gen_ld16s_i64(int ret, int arg2, tcg_target_long offset)
{
    tcg_gen_op3(INDEX_op_ld16s_i64, ret, arg2, offset);
}

static inline void tcg_gen_ld32u_i64(int ret, int arg2, tcg_target_long offset)
{
    tcg_gen_op3(INDEX_op_ld32u_i64, ret, arg2, offset);
}

static inline void tcg_gen_ld32s_i64(int ret, int arg2, tcg_target_long offset)
{
    tcg_gen_op3(INDEX_op_ld32s_i64, ret, arg2, offset);
}

static inline void tcg_gen_ld_i64(int ret, int arg2, tcg_target_long offset)
{
    tcg_gen_op3(INDEX_op_ld_i64, ret, arg2, offset);
}

static inline void tcg_gen_st8_i64(int arg1, int arg2, tcg_target_long offset)
{
    tcg_gen_op3(INDEX_op_st8_i64, arg1, arg2, offset);
}

static inline void tcg_gen_st16_i64(int arg1, int arg2, tcg_target_long offset)
{
    tcg_gen_op3(INDEX_op_st16_i64, arg1, arg2, offset);
}

static inline void tcg_gen_st32_i64(int arg1, int arg2, tcg_target_long offset)
{
    tcg_gen_op3(INDEX_op_st32_i64, arg1, arg2, offset);
}

static inline void tcg_gen_st_i64(int arg1, int arg2, tcg_target_long offset)
{
    tcg_gen_op3(INDEX_op_st_i64, arg1, arg2, offset);
}

static inline void tcg_gen_add_i64(int ret, int arg1, int arg2)
{
    tcg_gen_op3(INDEX_op_add_i64, ret, arg1, arg2);
}

static inline void tcg_gen_addi_i64(int ret, int arg1, int64_t arg2)
{
    tcg_gen_add_i64(ret, arg1, tcg_const_i64(arg2));
}

static inline void tcg_gen_sub_i64(int ret, int arg1, int arg2)
{
    tcg_gen_op3(INDEX_op_sub_i64, ret, arg1, arg2);
}

static inline void tcg_gen_subi_i64(int ret, int arg1, int64_t arg2)
{
    tcg_gen_sub_i64(ret, arg1, tcg_const_i64(arg2));
}

static inline void tcg_gen_and_i64(int ret, int arg1, int arg2)
{
    tcg_gen_op3(INDEX_op_and_i64, ret, arg1, arg2);
}

static inline void tcg_gen_andi_i64(int ret, int arg1, int64_t arg2)
{
    tcg_gen_and_i64(ret, arg1, tcg_const_i64(arg2));
}

static inline void tcg_gen_or_i64(int ret, int arg1, int arg2)
{
    tcg_gen_op3(INDEX_op_or_i64, ret, arg1, arg2);
}

static inline void tcg_gen_ori_i64(int ret, int arg1, int64_t arg2)
{
    tcg_gen_or_i64(ret, arg1, tcg_const_i64(arg2));
}

static inline void tcg_gen_xor_i64(int ret, int arg1, int arg2)
{
    tcg_gen_op3(INDEX_op_xor_i64, ret, arg1, arg2);
}

static inline void tcg_gen_xori_i64(int ret, int arg1, int64_t arg2)
{
    tcg_gen_xor_i64(ret, arg1, tcg_const_i64(arg2));
}

static inline void tcg_gen_shl_i64(int ret, int arg1, int arg2)
{
    tcg_gen_op3(INDEX_op_shl_i64, ret, arg1, arg2);
}

static inline void tcg_gen_shli_i64(int ret, int arg1, int64_t arg2)
{
    tcg_gen_shl_i64(ret, arg1, tcg_const_i64(arg2));
}

static inline void tcg_gen_shr_i64(int ret, int arg1, int arg2)
{
    tcg_gen_op3(INDEX_op_shr_i64, ret, arg1, arg2);
}

static inline void tcg_gen_shri_i64(int ret, int arg1, int64_t arg2)
{
    tcg_gen_shr_i64(ret, arg1, tcg_const_i64(arg2));
}

static inline void tcg_gen_sar_i64(int ret, int arg1, int arg2)
{
    tcg_gen_op3(INDEX_op_sar_i64, ret, arg1, arg2);
}

static inline void tcg_gen_sari_i64(int ret, int arg1, int64_t arg2)
{
    tcg_gen_sar_i64(ret, arg1, tcg_const_i64(arg2));
}

static inline void tcg_gen_brcond_i64(int cond, TCGArg arg1, TCGArg arg2, 
                                      int label_index)
{
    tcg_gen_op4(INDEX_op_brcond_i64, arg1, arg2, cond, label_index);
}

static inline void tcg_gen_mul_i64(int ret, int arg1, int arg2)
{
    tcg_gen_op3(INDEX_op_mul_i64, ret, arg1, arg2);
}

#ifdef TCG_TARGET_HAS_div_i64
static inline void tcg_gen_div_i64(int ret, int arg1, int arg2)
{
    tcg_gen_op3(INDEX_op_div_i64, ret, arg1, arg2);
}

static inline void tcg_gen_rem_i64(int ret, int arg1, int arg2)
{
    tcg_gen_op3(INDEX_op_rem_i64, ret, arg1, arg2);
}

static inline void tcg_gen_divu_i64(int ret, int arg1, int arg2)
{
    tcg_gen_op3(INDEX_op_divu_i64, ret, arg1, arg2);
}

static inline void tcg_gen_remu_i64(int ret, int arg1, int arg2)
{
    tcg_gen_op3(INDEX_op_remu_i64, ret, arg1, arg2);
}
#else
static inline void tcg_gen_div_i64(int ret, int arg1, int arg2)
{
    int t0;
    t0 = tcg_temp_new(TCG_TYPE_I64);
    tcg_gen_sari_i64(t0, arg1, 63);
    tcg_gen_op5(INDEX_op_div2_i64, ret, t0, arg1, t0, arg2);
}

static inline void tcg_gen_rem_i64(int ret, int arg1, int arg2)
{
    int t0;
    t0 = tcg_temp_new(TCG_TYPE_I64);
    tcg_gen_sari_i64(t0, arg1, 63);
    tcg_gen_op5(INDEX_op_div2_i64, t0, ret, arg1, t0, arg2);
}

static inline void tcg_gen_divu_i64(int ret, int arg1, int arg2)
{
    int t0;
    t0 = tcg_temp_new(TCG_TYPE_I64);
    tcg_gen_movi_i64(t0, 0);
    tcg_gen_op5(INDEX_op_divu2_i64, ret, t0, arg1, t0, arg2);
}

static inline void tcg_gen_remu_i64(int ret, int arg1, int arg2)
{
    int t0;
    t0 = tcg_temp_new(TCG_TYPE_I64);
    tcg_gen_movi_i64(t0, 0);
    tcg_gen_op5(INDEX_op_divu2_i64, t0, ret, arg1, t0, arg2);
}
#endif

#endif

/***************************************/
/* optional operations */

static inline void tcg_gen_ext8s_i32(int ret, int arg)
{
#ifdef TCG_TARGET_HAS_ext8s_i32
    tcg_gen_op2(INDEX_op_ext8s_i32, ret, arg);
#else
    tcg_gen_shli_i32(ret, arg, 24);
    tcg_gen_sari_i32(ret, arg, 24);
#endif
}

static inline void tcg_gen_ext16s_i32(int ret, int arg)
{
#ifdef TCG_TARGET_HAS_ext16s_i32
    tcg_gen_op2(INDEX_op_ext16s_i32, ret, arg);
#else
    tcg_gen_shli_i32(ret, arg, 16);
    tcg_gen_sari_i32(ret, arg, 16);
#endif
}

/* Note: we assume the two high bytes are set to zero */
static inline void tcg_gen_bswap16_i32(TCGArg ret, TCGArg arg)
{
#ifdef TCG_TARGET_HAS_bswap16_i32
    tcg_gen_op2(INDEX_op_bswap16_i32, ret, arg);
#else
    TCGArg t0, t1;
    t0 = tcg_temp_new(TCG_TYPE_I32);
    t1 = tcg_temp_new(TCG_TYPE_I32);
    
    tcg_gen_shri_i32(t0, arg, 8);
    tcg_gen_andi_i32(t1, arg, 0x000000ff);
    tcg_gen_shli_i32(t1, t1, 8);
    tcg_gen_or_i32(ret, t0, t1);
#endif
}

static inline void tcg_gen_bswap_i32(TCGArg ret, TCGArg arg)
{
#ifdef TCG_TARGET_HAS_bswap_i32
    tcg_gen_op2(INDEX_op_bswap_i32, ret, arg);
#else
    TCGArg t0, t1;
    t0 = tcg_temp_new(TCG_TYPE_I32);
    t1 = tcg_temp_new(TCG_TYPE_I32);
    
    tcg_gen_shli_i32(t0, arg, 24);
    
    tcg_gen_andi_i32(t1, arg, 0x0000ff00);
    tcg_gen_shli_i32(t1, t1, 8);
    tcg_gen_or_i32(t0, t0, t1);
    
    tcg_gen_shri_i32(t1, arg, 8);
    tcg_gen_andi_i32(t1, t1, 0x0000ff00);
    tcg_gen_or_i32(t0, t0, t1);
    
    tcg_gen_shri_i32(t1, arg, 24);
    tcg_gen_or_i32(ret, t0, t1);
#endif
}

#if TCG_TARGET_REG_BITS == 32
static inline void tcg_gen_ext8s_i64(int ret, int arg)
{
    tcg_gen_ext8s_i32(ret, arg);
    tcg_gen_sari_i32(ret + 1, ret, 31);
}

static inline void tcg_gen_ext16s_i64(int ret, int arg)
{
    tcg_gen_ext16s_i32(ret, arg);
    tcg_gen_sari_i32(ret + 1, ret, 31);
}

static inline void tcg_gen_ext32s_i64(int ret, int arg)
{
    tcg_gen_mov_i32(ret, arg);
    tcg_gen_sari_i32(ret + 1, ret, 31);
}

static inline void tcg_gen_trunc_i64_i32(int ret, int arg)
{
    tcg_gen_mov_i32(ret, arg);
}

static inline void tcg_gen_extu_i32_i64(int ret, int arg)
{
    tcg_gen_mov_i32(ret, arg);
    tcg_gen_movi_i32(ret + 1, 0);
}

static inline void tcg_gen_ext_i32_i64(int ret, int arg)
{
    tcg_gen_mov_i32(ret, arg);
    tcg_gen_sari_i32(ret + 1, ret, 31);
}

static inline void tcg_gen_bswap_i64(int ret, int arg)
{
    int t0, t1;
    t0 = tcg_temp_new(TCG_TYPE_I32);
    t1 = tcg_temp_new(TCG_TYPE_I32);

    tcg_gen_bswap_i32(t0, arg);
    tcg_gen_bswap_i32(t1, arg + 1);
    tcg_gen_mov_i32(ret, t1);
    tcg_gen_mov_i32(ret + 1, t0);
}
#else

static inline void tcg_gen_ext8s_i64(int ret, int arg)
{
#ifdef TCG_TARGET_HAS_ext8s_i64
    tcg_gen_op2(INDEX_op_ext8s_i64, ret, arg);
#else
    tcg_gen_shli_i64(ret, arg, 56);
    tcg_gen_sari_i64(ret, arg, 56);
#endif
}

static inline void tcg_gen_ext16s_i64(int ret, int arg)
{
#ifdef TCG_TARGET_HAS_ext16s_i64
    tcg_gen_op2(INDEX_op_ext16s_i64, ret, arg);
#else
    tcg_gen_shli_i64(ret, arg, 48);
    tcg_gen_sari_i64(ret, arg, 48);
#endif
}

static inline void tcg_gen_ext32s_i64(int ret, int arg)
{
#ifdef TCG_TARGET_HAS_ext32s_i64
    tcg_gen_op2(INDEX_op_ext32s_i64, ret, arg);
#else
    tcg_gen_shli_i64(ret, arg, 32);
    tcg_gen_sari_i64(ret, arg, 32);
#endif
}

/* Note: we assume the target supports move between 32 and 64 bit
   registers */
static inline void tcg_gen_trunc_i64_i32(int ret, int arg)
{
    tcg_gen_mov_i32(ret, arg);
}

/* Note: we assume the target supports move between 32 and 64 bit
   registers */
static inline void tcg_gen_extu_i32_i64(int ret, int arg)
{
    tcg_gen_andi_i64(ret, arg, 0xffffffff);
}

/* Note: we assume the target supports move between 32 and 64 bit
   registers */
static inline void tcg_gen_ext_i32_i64(int ret, int arg)
{
    tcg_gen_ext32s_i64(ret, arg);
}

static inline void tcg_gen_bswap_i64(TCGArg ret, TCGArg arg)
{
#ifdef TCG_TARGET_HAS_bswap_i64
    tcg_gen_op2(INDEX_op_bswap_i64, ret, arg);
#else
    TCGArg t0, t1;
    t0 = tcg_temp_new(TCG_TYPE_I32);
    t1 = tcg_temp_new(TCG_TYPE_I32);
    
    tcg_gen_shli_i64(t0, arg, 56);
    
    tcg_gen_andi_i64(t1, arg, 0x0000ff00);
    tcg_gen_shli_i64(t1, t1, 40);
    tcg_gen_or_i64(t0, t0, t1);
    
    tcg_gen_andi_i64(t1, arg, 0x00ff0000);
    tcg_gen_shli_i64(t1, t1, 24);
    tcg_gen_or_i64(t0, t0, t1);

    tcg_gen_andi_i64(t1, arg, 0xff000000);
    tcg_gen_shli_i64(t1, t1, 8);
    tcg_gen_or_i64(t0, t0, t1);

    tcg_gen_shri_i64(t1, arg, 8);
    tcg_gen_andi_i64(t1, t1, 0xff000000);
    tcg_gen_or_i64(t0, t0, t1);
    
    tcg_gen_shri_i64(t1, arg, 24);
    tcg_gen_andi_i64(t1, t1, 0x00ff0000);
    tcg_gen_or_i64(t0, t0, t1);

    tcg_gen_shri_i64(t1, arg, 40);
    tcg_gen_andi_i64(t1, t1, 0x0000ff00);
    tcg_gen_or_i64(t0, t0, t1);

    tcg_gen_shri_i64(t1, arg, 56);
    tcg_gen_or_i64(ret, t0, t1);
#endif
}

#endif

/***************************************/
static inline void tcg_gen_macro_2(int ret0, int ret1, int macro_id)
{
    tcg_gen_op3(INDEX_op_macro_2, ret0, ret1, macro_id);
}

/***************************************/
/* QEMU specific operations. Their type depend on the QEMU CPU
   type. */
#ifndef TARGET_LONG_BITS
#error must include QEMU headers
#endif

static inline void tcg_gen_exit_tb(tcg_target_long val)
{
    tcg_gen_op1(INDEX_op_exit_tb, val);
}

static inline void tcg_gen_goto_tb(int idx)
{
    tcg_gen_op1(INDEX_op_goto_tb, idx);
}

#if TCG_TARGET_REG_BITS == 32
static inline void tcg_gen_qemu_ld8u(int ret, int addr, int mem_index)
{
#if TARGET_LONG_BITS == 32
    tcg_gen_op3(INDEX_op_qemu_ld8u, ret, addr, mem_index);
#else
    tcg_gen_op4(INDEX_op_qemu_ld8u, ret, addr, addr + 1, mem_index);
    tcg_gen_movi_i32(ret + 1, 0);
#endif
}

static inline void tcg_gen_qemu_ld8s(int ret, int addr, int mem_index)
{
#if TARGET_LONG_BITS == 32
    tcg_gen_op3(INDEX_op_qemu_ld8s, ret, addr, mem_index);
#else
    tcg_gen_op4(INDEX_op_qemu_ld8s, ret, addr, addr + 1, mem_index);
    tcg_gen_ext8s_i32(ret + 1, ret);
#endif
}

static inline void tcg_gen_qemu_ld16u(int ret, int addr, int mem_index)
{
#if TARGET_LONG_BITS == 32
    tcg_gen_op3(INDEX_op_qemu_ld16u, ret, addr, mem_index);
#else
    tcg_gen_op4(INDEX_op_qemu_ld16u, ret, addr, addr + 1, mem_index);
    tcg_gen_movi_i32(ret + 1, 0);
#endif
}

static inline void tcg_gen_qemu_ld16s(int ret, int addr, int mem_index)
{
#if TARGET_LONG_BITS == 32
    tcg_gen_op3(INDEX_op_qemu_ld16s, ret, addr, mem_index);
#else
    tcg_gen_op4(INDEX_op_qemu_ld16s, ret, addr, addr + 1, mem_index);
    tcg_gen_ext16s_i32(ret + 1, ret);
#endif
}

static inline void tcg_gen_qemu_ld32u(int ret, int addr, int mem_index)
{
#if TARGET_LONG_BITS == 32
    tcg_gen_op3(INDEX_op_qemu_ld32u, ret, addr, mem_index);
#else
    tcg_gen_op4(INDEX_op_qemu_ld32u, ret, addr, addr + 1, mem_index);
    tcg_gen_movi_i32(ret + 1, 0);
#endif
}

static inline void tcg_gen_qemu_ld32s(int ret, int addr, int mem_index)
{
#if TARGET_LONG_BITS == 32
    tcg_gen_op3(INDEX_op_qemu_ld32u, ret, addr, mem_index);
#else
    tcg_gen_op4(INDEX_op_qemu_ld32u, ret, addr, addr + 1, mem_index);
    tcg_gen_sari_i32(ret + 1, ret, 31);
#endif
}

static inline void tcg_gen_qemu_ld64(int ret, int addr, int mem_index)
{
#if TARGET_LONG_BITS == 32
    tcg_gen_op4(INDEX_op_qemu_ld64, ret, ret + 1, addr, mem_index);
#else
    tcg_gen_op5(INDEX_op_qemu_ld64, ret, ret + 1, addr, addr + 1, mem_index);
#endif
}

static inline void tcg_gen_qemu_st8(int arg, int addr, int mem_index)
{
#if TARGET_LONG_BITS == 32
    tcg_gen_op3(INDEX_op_qemu_st8, arg, addr, mem_index);
#else
    tcg_gen_op4(INDEX_op_qemu_st8, arg, addr, addr + 1, mem_index);
#endif
}

static inline void tcg_gen_qemu_st16(int arg, int addr, int mem_index)
{
#if TARGET_LONG_BITS == 32
    tcg_gen_op3(INDEX_op_qemu_st16, arg, addr, mem_index);
#else
    tcg_gen_op4(INDEX_op_qemu_st16, arg, addr, addr + 1, mem_index);
#endif
}

static inline void tcg_gen_qemu_st32(int arg, int addr, int mem_index)
{
#if TARGET_LONG_BITS == 32
    tcg_gen_op3(INDEX_op_qemu_st32, arg, addr, mem_index);
#else
    tcg_gen_op4(INDEX_op_qemu_st32, arg, addr, addr + 1, mem_index);
#endif
}

static inline void tcg_gen_qemu_st64(int arg, int addr, int mem_index)
{
#if TARGET_LONG_BITS == 32
    tcg_gen_op4(INDEX_op_qemu_st64, arg, arg + 1, addr, mem_index);
#else
    tcg_gen_op5(INDEX_op_qemu_st64, arg, arg + 1, addr, addr + 1, mem_index);
#endif
}

#else /* TCG_TARGET_REG_BITS == 32 */

static inline void tcg_gen_qemu_ld8u(int ret, int addr, int mem_index)
{
    tcg_gen_op3(INDEX_op_qemu_ld8u, ret, addr, mem_index);
}

static inline void tcg_gen_qemu_ld8s(int ret, int addr, int mem_index)
{
    tcg_gen_op3(INDEX_op_qemu_ld8s, ret, addr, mem_index);
}

static inline void tcg_gen_qemu_ld16u(int ret, int addr, int mem_index)
{
    tcg_gen_op3(INDEX_op_qemu_ld16u, ret, addr, mem_index);
}

static inline void tcg_gen_qemu_ld16s(int ret, int addr, int mem_index)
{
    tcg_gen_op3(INDEX_op_qemu_ld16s, ret, addr, mem_index);
}

static inline void tcg_gen_qemu_ld32u(int ret, int addr, int mem_index)
{
    tcg_gen_op3(INDEX_op_qemu_ld32u, ret, addr, mem_index);
}

static inline void tcg_gen_qemu_ld32s(int ret, int addr, int mem_index)
{
    tcg_gen_op3(INDEX_op_qemu_ld32s, ret, addr, mem_index);
}

static inline void tcg_gen_qemu_ld64(int ret, int addr, int mem_index)
{
    tcg_gen_op3(INDEX_op_qemu_ld64, ret, addr, mem_index);
}

static inline void tcg_gen_qemu_st8(int arg, int addr, int mem_index)
{
    tcg_gen_op3(INDEX_op_qemu_st8, arg, addr, mem_index);
}

static inline void tcg_gen_qemu_st16(int arg, int addr, int mem_index)
{
    tcg_gen_op3(INDEX_op_qemu_st16, arg, addr, mem_index);
}

static inline void tcg_gen_qemu_st32(int arg, int addr, int mem_index)
{
    tcg_gen_op3(INDEX_op_qemu_st32, arg, addr, mem_index);
}

static inline void tcg_gen_qemu_st64(int arg, int addr, int mem_index)
{
    tcg_gen_op3(INDEX_op_qemu_st64, arg, addr, mem_index);
}

#endif /* TCG_TARGET_REG_BITS != 32 */
