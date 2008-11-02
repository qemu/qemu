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

#ifdef CONFIG_DYNGEN_OP
/* legacy dyngen operations */
#include "gen-op.h"
#endif

int gen_new_label(void);

static inline void tcg_gen_op1(int opc, TCGv arg1)
{
    *gen_opc_ptr++ = opc;
    *gen_opparam_ptr++ = GET_TCGV(arg1);
}

static inline void tcg_gen_op1i(int opc, TCGArg arg1)
{
    *gen_opc_ptr++ = opc;
    *gen_opparam_ptr++ = arg1;
}

static inline void tcg_gen_op2(int opc, TCGv arg1, TCGv arg2)
{
    *gen_opc_ptr++ = opc;
    *gen_opparam_ptr++ = GET_TCGV(arg1);
    *gen_opparam_ptr++ = GET_TCGV(arg2);
}

static inline void tcg_gen_op2i(int opc, TCGv arg1, TCGArg arg2)
{
    *gen_opc_ptr++ = opc;
    *gen_opparam_ptr++ = GET_TCGV(arg1);
    *gen_opparam_ptr++ = arg2;
}

static inline void tcg_gen_op2ii(int opc, TCGArg arg1, TCGArg arg2)
{
    *gen_opc_ptr++ = opc;
    *gen_opparam_ptr++ = arg1;
    *gen_opparam_ptr++ = arg2;
}

static inline void tcg_gen_op3(int opc, TCGv arg1, TCGv arg2, TCGv arg3)
{
    *gen_opc_ptr++ = opc;
    *gen_opparam_ptr++ = GET_TCGV(arg1);
    *gen_opparam_ptr++ = GET_TCGV(arg2);
    *gen_opparam_ptr++ = GET_TCGV(arg3);
}

static inline void tcg_gen_op3i(int opc, TCGv arg1, TCGv arg2, TCGArg arg3)
{
    *gen_opc_ptr++ = opc;
    *gen_opparam_ptr++ = GET_TCGV(arg1);
    *gen_opparam_ptr++ = GET_TCGV(arg2);
    *gen_opparam_ptr++ = arg3;
}

static inline void tcg_gen_op4(int opc, TCGv arg1, TCGv arg2, TCGv arg3, 
                               TCGv arg4)
{
    *gen_opc_ptr++ = opc;
    *gen_opparam_ptr++ = GET_TCGV(arg1);
    *gen_opparam_ptr++ = GET_TCGV(arg2);
    *gen_opparam_ptr++ = GET_TCGV(arg3);
    *gen_opparam_ptr++ = GET_TCGV(arg4);
}

static inline void tcg_gen_op4i(int opc, TCGv arg1, TCGv arg2, TCGv arg3, 
                                TCGArg arg4)
{
    *gen_opc_ptr++ = opc;
    *gen_opparam_ptr++ = GET_TCGV(arg1);
    *gen_opparam_ptr++ = GET_TCGV(arg2);
    *gen_opparam_ptr++ = GET_TCGV(arg3);
    *gen_opparam_ptr++ = arg4;
}

static inline void tcg_gen_op4ii(int opc, TCGv arg1, TCGv arg2, TCGArg arg3, 
                                 TCGArg arg4)
{
    *gen_opc_ptr++ = opc;
    *gen_opparam_ptr++ = GET_TCGV(arg1);
    *gen_opparam_ptr++ = GET_TCGV(arg2);
    *gen_opparam_ptr++ = arg3;
    *gen_opparam_ptr++ = arg4;
}

static inline void tcg_gen_op5(int opc, TCGv arg1, TCGv arg2, 
                               TCGv arg3, TCGv arg4,
                               TCGv arg5)
{
    *gen_opc_ptr++ = opc;
    *gen_opparam_ptr++ = GET_TCGV(arg1);
    *gen_opparam_ptr++ = GET_TCGV(arg2);
    *gen_opparam_ptr++ = GET_TCGV(arg3);
    *gen_opparam_ptr++ = GET_TCGV(arg4);
    *gen_opparam_ptr++ = GET_TCGV(arg5);
}

static inline void tcg_gen_op5i(int opc, TCGv arg1, TCGv arg2, 
                                TCGv arg3, TCGv arg4,
                                TCGArg arg5)
{
    *gen_opc_ptr++ = opc;
    *gen_opparam_ptr++ = GET_TCGV(arg1);
    *gen_opparam_ptr++ = GET_TCGV(arg2);
    *gen_opparam_ptr++ = GET_TCGV(arg3);
    *gen_opparam_ptr++ = GET_TCGV(arg4);
    *gen_opparam_ptr++ = arg5;
}

static inline void tcg_gen_op6(int opc, TCGv arg1, TCGv arg2, 
                               TCGv arg3, TCGv arg4,
                               TCGv arg5, TCGv arg6)
{
    *gen_opc_ptr++ = opc;
    *gen_opparam_ptr++ = GET_TCGV(arg1);
    *gen_opparam_ptr++ = GET_TCGV(arg2);
    *gen_opparam_ptr++ = GET_TCGV(arg3);
    *gen_opparam_ptr++ = GET_TCGV(arg4);
    *gen_opparam_ptr++ = GET_TCGV(arg5);
    *gen_opparam_ptr++ = GET_TCGV(arg6);
}

static inline void tcg_gen_op6ii(int opc, TCGv arg1, TCGv arg2, 
                                 TCGv arg3, TCGv arg4,
                                 TCGArg arg5, TCGArg arg6)
{
    *gen_opc_ptr++ = opc;
    *gen_opparam_ptr++ = GET_TCGV(arg1);
    *gen_opparam_ptr++ = GET_TCGV(arg2);
    *gen_opparam_ptr++ = GET_TCGV(arg3);
    *gen_opparam_ptr++ = GET_TCGV(arg4);
    *gen_opparam_ptr++ = arg5;
    *gen_opparam_ptr++ = arg6;
}

static inline void gen_set_label(int n)
{
    tcg_gen_op1i(INDEX_op_set_label, n);
}

static inline void tcg_gen_br(int label)
{
    tcg_gen_op1i(INDEX_op_br, label);
}

static inline void tcg_gen_mov_i32(TCGv ret, TCGv arg)
{
    if (GET_TCGV(ret) != GET_TCGV(arg))
        tcg_gen_op2(INDEX_op_mov_i32, ret, arg);
}

static inline void tcg_gen_movi_i32(TCGv ret, int32_t arg)
{
    tcg_gen_op2i(INDEX_op_movi_i32, ret, arg);
}

/* helper calls */
#define TCG_HELPER_CALL_FLAGS 0

static inline void tcg_gen_helper_0_0(void *func)
{
    TCGv t0;
    t0 = tcg_const_ptr((tcg_target_long)func);
    tcg_gen_call(&tcg_ctx, 
                 t0, TCG_HELPER_CALL_FLAGS,
                 0, NULL, 0, NULL);
    tcg_temp_free(t0);
}

static inline void tcg_gen_helper_0_1(void *func, TCGv arg)
{
    TCGv t0;
    t0 = tcg_const_ptr((tcg_target_long)func);
    tcg_gen_call(&tcg_ctx,
                 t0, TCG_HELPER_CALL_FLAGS,
                 0, NULL, 1, &arg);
    tcg_temp_free(t0);
}

static inline void tcg_gen_helper_0_2(void *func, TCGv arg1, TCGv arg2)
{
    TCGv args[2];
    TCGv t0;
    args[0] = arg1;
    args[1] = arg2;
    t0 = tcg_const_ptr((tcg_target_long)func);
    tcg_gen_call(&tcg_ctx, 
                 t0, TCG_HELPER_CALL_FLAGS,
                 0, NULL, 2, args);
    tcg_temp_free(t0);
}

static inline void tcg_gen_helper_0_3(void *func,
                                      TCGv arg1, TCGv arg2, TCGv arg3)
{
    TCGv args[3];
    TCGv t0;
    args[0] = arg1;
    args[1] = arg2;
    args[2] = arg3;
    t0 = tcg_const_ptr((tcg_target_long)func);
    tcg_gen_call(&tcg_ctx,
                 t0, TCG_HELPER_CALL_FLAGS,
                 0, NULL, 3, args);
    tcg_temp_free(t0);
}

static inline void tcg_gen_helper_0_4(void *func, TCGv arg1, TCGv arg2,
                                      TCGv arg3, TCGv arg4)
{
    TCGv args[4];
    TCGv t0;
    args[0] = arg1;
    args[1] = arg2;
    args[2] = arg3;
    args[3] = arg4;
    t0 = tcg_const_ptr((tcg_target_long)func);
    tcg_gen_call(&tcg_ctx,
                 t0, TCG_HELPER_CALL_FLAGS,
                 0, NULL, 4, args);
    tcg_temp_free(t0);
}

static inline void tcg_gen_helper_1_0(void *func, TCGv ret)
{
    TCGv t0;
    t0 = tcg_const_ptr((tcg_target_long)func);
    tcg_gen_call(&tcg_ctx,
                 t0, TCG_HELPER_CALL_FLAGS,
                 1, &ret, 0, NULL);
    tcg_temp_free(t0);
}

static inline void tcg_gen_helper_1_1(void *func, TCGv ret, TCGv arg1)
{
    TCGv t0;
    t0 = tcg_const_ptr((tcg_target_long)func);
    tcg_gen_call(&tcg_ctx,
                 t0, TCG_HELPER_CALL_FLAGS,
                 1, &ret, 1, &arg1);
    tcg_temp_free(t0);
}

static inline void tcg_gen_helper_1_2(void *func, TCGv ret, 
                                      TCGv arg1, TCGv arg2)
{
    TCGv args[2];
    TCGv t0;
    args[0] = arg1;
    args[1] = arg2;
    t0 = tcg_const_ptr((tcg_target_long)func);
    tcg_gen_call(&tcg_ctx, 
                 t0, TCG_HELPER_CALL_FLAGS,
                 1, &ret, 2, args);
    tcg_temp_free(t0);
}

static inline void tcg_gen_helper_1_3(void *func, TCGv ret,
                                      TCGv arg1, TCGv arg2, TCGv arg3)
{
    TCGv args[3];
    TCGv t0;
    args[0] = arg1;
    args[1] = arg2;
    args[2] = arg3;
    t0 = tcg_const_ptr((tcg_target_long)func);
    tcg_gen_call(&tcg_ctx,
                 t0, TCG_HELPER_CALL_FLAGS,
                 1, &ret, 3, args);
    tcg_temp_free(t0);
}

static inline void tcg_gen_helper_1_4(void *func, TCGv ret,
                                      TCGv arg1, TCGv arg2, TCGv arg3,
                                      TCGv arg4)
{
    TCGv args[4];
    TCGv t0;
    args[0] = arg1;
    args[1] = arg2;
    args[2] = arg3;
    args[3] = arg4;
    t0 = tcg_const_ptr((tcg_target_long)func);
    tcg_gen_call(&tcg_ctx,
                 t0, TCG_HELPER_CALL_FLAGS,
                 1, &ret, 4, args);
    tcg_temp_free(t0);
}

/* 32 bit ops */

static inline void tcg_gen_ld8u_i32(TCGv ret, TCGv arg2, tcg_target_long offset)
{
    tcg_gen_op3i(INDEX_op_ld8u_i32, ret, arg2, offset);
}

static inline void tcg_gen_ld8s_i32(TCGv ret, TCGv arg2, tcg_target_long offset)
{
    tcg_gen_op3i(INDEX_op_ld8s_i32, ret, arg2, offset);
}

static inline void tcg_gen_ld16u_i32(TCGv ret, TCGv arg2, tcg_target_long offset)
{
    tcg_gen_op3i(INDEX_op_ld16u_i32, ret, arg2, offset);
}

static inline void tcg_gen_ld16s_i32(TCGv ret, TCGv arg2, tcg_target_long offset)
{
    tcg_gen_op3i(INDEX_op_ld16s_i32, ret, arg2, offset);
}

static inline void tcg_gen_ld_i32(TCGv ret, TCGv arg2, tcg_target_long offset)
{
    tcg_gen_op3i(INDEX_op_ld_i32, ret, arg2, offset);
}

static inline void tcg_gen_st8_i32(TCGv arg1, TCGv arg2, tcg_target_long offset)
{
    tcg_gen_op3i(INDEX_op_st8_i32, arg1, arg2, offset);
}

static inline void tcg_gen_st16_i32(TCGv arg1, TCGv arg2, tcg_target_long offset)
{
    tcg_gen_op3i(INDEX_op_st16_i32, arg1, arg2, offset);
}

static inline void tcg_gen_st_i32(TCGv arg1, TCGv arg2, tcg_target_long offset)
{
    tcg_gen_op3i(INDEX_op_st_i32, arg1, arg2, offset);
}

static inline void tcg_gen_add_i32(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_op3(INDEX_op_add_i32, ret, arg1, arg2);
}

static inline void tcg_gen_addi_i32(TCGv ret, TCGv arg1, int32_t arg2)
{
    /* some cases can be optimized here */
    if (arg2 == 0) {
        tcg_gen_mov_i32(ret, arg1);
    } else {
        TCGv t0 = tcg_const_i32(arg2);
        tcg_gen_add_i32(ret, arg1, t0);
        tcg_temp_free(t0);
    }
}

static inline void tcg_gen_sub_i32(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_op3(INDEX_op_sub_i32, ret, arg1, arg2);
}

static inline void tcg_gen_subfi_i32(TCGv ret, int32_t arg1, TCGv arg2)
{
    TCGv t0 = tcg_const_i32(arg1);
    tcg_gen_sub_i32(ret, t0, arg2);
    tcg_temp_free(t0);
}

static inline void tcg_gen_subi_i32(TCGv ret, TCGv arg1, int32_t arg2)
{
    /* some cases can be optimized here */
    if (arg2 == 0) {
        tcg_gen_mov_i32(ret, arg1);
    } else {
        TCGv t0 = tcg_const_i32(arg2);
        tcg_gen_sub_i32(ret, arg1, t0);
        tcg_temp_free(t0);
    }
}

static inline void tcg_gen_and_i32(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_op3(INDEX_op_and_i32, ret, arg1, arg2);
}

static inline void tcg_gen_andi_i32(TCGv ret, TCGv arg1, int32_t arg2)
{
    /* some cases can be optimized here */
    if (arg2 == 0) {
        tcg_gen_movi_i32(ret, 0);
    } else if (arg2 == 0xffffffff) {
        tcg_gen_mov_i32(ret, arg1);
    } else {
        TCGv t0 = tcg_const_i32(arg2);
        tcg_gen_and_i32(ret, arg1, t0);
        tcg_temp_free(t0);
    }
}

static inline void tcg_gen_or_i32(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_op3(INDEX_op_or_i32, ret, arg1, arg2);
}

static inline void tcg_gen_ori_i32(TCGv ret, TCGv arg1, int32_t arg2)
{
    /* some cases can be optimized here */
    if (arg2 == 0xffffffff) {
        tcg_gen_movi_i32(ret, 0xffffffff);
    } else if (arg2 == 0) {
        tcg_gen_mov_i32(ret, arg1);
    } else {
        TCGv t0 = tcg_const_i32(arg2);
        tcg_gen_or_i32(ret, arg1, t0);
        tcg_temp_free(t0);
    }
}

static inline void tcg_gen_xor_i32(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_op3(INDEX_op_xor_i32, ret, arg1, arg2);
}

static inline void tcg_gen_xori_i32(TCGv ret, TCGv arg1, int32_t arg2)
{
    /* some cases can be optimized here */
    if (arg2 == 0) {
        tcg_gen_mov_i32(ret, arg1);
    } else {
        TCGv t0 = tcg_const_i32(arg2);
        tcg_gen_xor_i32(ret, arg1, t0);
        tcg_temp_free(t0);
    }
}

static inline void tcg_gen_shl_i32(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_op3(INDEX_op_shl_i32, ret, arg1, arg2);
}

static inline void tcg_gen_shli_i32(TCGv ret, TCGv arg1, int32_t arg2)
{
    if (arg2 == 0) {
        tcg_gen_mov_i32(ret, arg1);
    } else {
        TCGv t0 = tcg_const_i32(arg2);
        tcg_gen_shl_i32(ret, arg1, t0);
        tcg_temp_free(t0);
    }
}

static inline void tcg_gen_shr_i32(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_op3(INDEX_op_shr_i32, ret, arg1, arg2);
}

static inline void tcg_gen_shri_i32(TCGv ret, TCGv arg1, int32_t arg2)
{
    if (arg2 == 0) {
        tcg_gen_mov_i32(ret, arg1);
    } else {
        TCGv t0 = tcg_const_i32(arg2);
        tcg_gen_shr_i32(ret, arg1, t0);
        tcg_temp_free(t0);
    }
}

static inline void tcg_gen_sar_i32(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_op3(INDEX_op_sar_i32, ret, arg1, arg2);
}

static inline void tcg_gen_sari_i32(TCGv ret, TCGv arg1, int32_t arg2)
{
    if (arg2 == 0) {
        tcg_gen_mov_i32(ret, arg1);
    } else {
        TCGv t0 = tcg_const_i32(arg2);
        tcg_gen_sar_i32(ret, arg1, t0);
        tcg_temp_free(t0);
    }
}

static inline void tcg_gen_brcond_i32(int cond, TCGv arg1, TCGv arg2, 
                                      int label_index)
{
    tcg_gen_op4ii(INDEX_op_brcond_i32, arg1, arg2, cond, label_index);
}

static inline void tcg_gen_brcondi_i32(int cond, TCGv arg1, int32_t arg2, 
                                       int label_index)
{
    TCGv t0 = tcg_const_i32(arg2);
    tcg_gen_brcond_i32(cond, arg1, t0, label_index);
    tcg_temp_free(t0);
}

static inline void tcg_gen_mul_i32(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_op3(INDEX_op_mul_i32, ret, arg1, arg2);
}

static inline void tcg_gen_muli_i32(TCGv ret, TCGv arg1, int32_t arg2)
{
    TCGv t0 = tcg_const_i32(arg2);
    tcg_gen_mul_i32(ret, arg1, t0);
    tcg_temp_free(t0);
}

#ifdef TCG_TARGET_HAS_div_i32
static inline void tcg_gen_div_i32(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_op3(INDEX_op_div_i32, ret, arg1, arg2);
}

static inline void tcg_gen_rem_i32(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_op3(INDEX_op_rem_i32, ret, arg1, arg2);
}

static inline void tcg_gen_divu_i32(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_op3(INDEX_op_divu_i32, ret, arg1, arg2);
}

static inline void tcg_gen_remu_i32(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_op3(INDEX_op_remu_i32, ret, arg1, arg2);
}
#else
static inline void tcg_gen_div_i32(TCGv ret, TCGv arg1, TCGv arg2)
{
    TCGv t0;
    t0 = tcg_temp_new(TCG_TYPE_I32);
    tcg_gen_sari_i32(t0, arg1, 31);
    tcg_gen_op5(INDEX_op_div2_i32, ret, t0, arg1, t0, arg2);
    tcg_temp_free(t0);
}

static inline void tcg_gen_rem_i32(TCGv ret, TCGv arg1, TCGv arg2)
{
    TCGv t0;
    t0 = tcg_temp_new(TCG_TYPE_I32);
    tcg_gen_sari_i32(t0, arg1, 31);
    tcg_gen_op5(INDEX_op_div2_i32, t0, ret, arg1, t0, arg2);
    tcg_temp_free(t0);
}

static inline void tcg_gen_divu_i32(TCGv ret, TCGv arg1, TCGv arg2)
{
    TCGv t0;
    t0 = tcg_temp_new(TCG_TYPE_I32);
    tcg_gen_movi_i32(t0, 0);
    tcg_gen_op5(INDEX_op_divu2_i32, ret, t0, arg1, t0, arg2);
    tcg_temp_free(t0);
}

static inline void tcg_gen_remu_i32(TCGv ret, TCGv arg1, TCGv arg2)
{
    TCGv t0;
    t0 = tcg_temp_new(TCG_TYPE_I32);
    tcg_gen_movi_i32(t0, 0);
    tcg_gen_op5(INDEX_op_divu2_i32, t0, ret, arg1, t0, arg2);
    tcg_temp_free(t0);
}
#endif

#if TCG_TARGET_REG_BITS == 32

static inline void tcg_gen_mov_i64(TCGv ret, TCGv arg)
{
    if (GET_TCGV(ret) != GET_TCGV(arg)) {
        tcg_gen_mov_i32(ret, arg);
        tcg_gen_mov_i32(TCGV_HIGH(ret), TCGV_HIGH(arg));
    }
}

static inline void tcg_gen_movi_i64(TCGv ret, int64_t arg)
{
    tcg_gen_movi_i32(ret, arg);
    tcg_gen_movi_i32(TCGV_HIGH(ret), arg >> 32);
}

static inline void tcg_gen_ld8u_i64(TCGv ret, TCGv arg2, tcg_target_long offset)
{
    tcg_gen_ld8u_i32(ret, arg2, offset);
    tcg_gen_movi_i32(TCGV_HIGH(ret), 0);
}

static inline void tcg_gen_ld8s_i64(TCGv ret, TCGv arg2, tcg_target_long offset)
{
    tcg_gen_ld8s_i32(ret, arg2, offset);
    tcg_gen_sari_i32(TCGV_HIGH(ret), ret, 31);
}

static inline void tcg_gen_ld16u_i64(TCGv ret, TCGv arg2, tcg_target_long offset)
{
    tcg_gen_ld16u_i32(ret, arg2, offset);
    tcg_gen_movi_i32(TCGV_HIGH(ret), 0);
}

static inline void tcg_gen_ld16s_i64(TCGv ret, TCGv arg2, tcg_target_long offset)
{
    tcg_gen_ld16s_i32(ret, arg2, offset);
    tcg_gen_sari_i32(TCGV_HIGH(ret), ret, 31);
}

static inline void tcg_gen_ld32u_i64(TCGv ret, TCGv arg2, tcg_target_long offset)
{
    tcg_gen_ld_i32(ret, arg2, offset);
    tcg_gen_movi_i32(TCGV_HIGH(ret), 0);
}

static inline void tcg_gen_ld32s_i64(TCGv ret, TCGv arg2, tcg_target_long offset)
{
    tcg_gen_ld_i32(ret, arg2, offset);
    tcg_gen_sari_i32(TCGV_HIGH(ret), ret, 31);
}

static inline void tcg_gen_ld_i64(TCGv ret, TCGv arg2, tcg_target_long offset)
{
    /* since arg2 and ret have different types, they cannot be the
       same temporary */
#ifdef TCG_TARGET_WORDS_BIGENDIAN
    tcg_gen_ld_i32(TCGV_HIGH(ret), arg2, offset);
    tcg_gen_ld_i32(ret, arg2, offset + 4);
#else
    tcg_gen_ld_i32(ret, arg2, offset);
    tcg_gen_ld_i32(TCGV_HIGH(ret), arg2, offset + 4);
#endif
}

static inline void tcg_gen_st8_i64(TCGv arg1, TCGv arg2, tcg_target_long offset)
{
    tcg_gen_st8_i32(arg1, arg2, offset);
}

static inline void tcg_gen_st16_i64(TCGv arg1, TCGv arg2, tcg_target_long offset)
{
    tcg_gen_st16_i32(arg1, arg2, offset);
}

static inline void tcg_gen_st32_i64(TCGv arg1, TCGv arg2, tcg_target_long offset)
{
    tcg_gen_st_i32(arg1, arg2, offset);
}

static inline void tcg_gen_st_i64(TCGv arg1, TCGv arg2, tcg_target_long offset)
{
#ifdef TCG_TARGET_WORDS_BIGENDIAN
    tcg_gen_st_i32(TCGV_HIGH(arg1), arg2, offset);
    tcg_gen_st_i32(arg1, arg2, offset + 4);
#else
    tcg_gen_st_i32(arg1, arg2, offset);
    tcg_gen_st_i32(TCGV_HIGH(arg1), arg2, offset + 4);
#endif
}

static inline void tcg_gen_add_i64(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_op6(INDEX_op_add2_i32, ret, TCGV_HIGH(ret), 
                arg1, TCGV_HIGH(arg1), arg2, TCGV_HIGH(arg2));
}

static inline void tcg_gen_sub_i64(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_op6(INDEX_op_sub2_i32, ret, TCGV_HIGH(ret), 
                arg1, TCGV_HIGH(arg1), arg2, TCGV_HIGH(arg2));
}

static inline void tcg_gen_and_i64(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_and_i32(ret, arg1, arg2);
    tcg_gen_and_i32(TCGV_HIGH(ret), TCGV_HIGH(arg1), TCGV_HIGH(arg2));
}

static inline void tcg_gen_andi_i64(TCGv ret, TCGv arg1, int64_t arg2)
{
    tcg_gen_andi_i32(ret, arg1, arg2);
    tcg_gen_andi_i32(TCGV_HIGH(ret), TCGV_HIGH(arg1), arg2 >> 32);
}

static inline void tcg_gen_or_i64(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_or_i32(ret, arg1, arg2);
    tcg_gen_or_i32(TCGV_HIGH(ret), TCGV_HIGH(arg1), TCGV_HIGH(arg2));
}

static inline void tcg_gen_ori_i64(TCGv ret, TCGv arg1, int64_t arg2)
{
    tcg_gen_ori_i32(ret, arg1, arg2);
    tcg_gen_ori_i32(TCGV_HIGH(ret), TCGV_HIGH(arg1), arg2 >> 32);
}

static inline void tcg_gen_xor_i64(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_xor_i32(ret, arg1, arg2);
    tcg_gen_xor_i32(TCGV_HIGH(ret), TCGV_HIGH(arg1), TCGV_HIGH(arg2));
}

static inline void tcg_gen_xori_i64(TCGv ret, TCGv arg1, int64_t arg2)
{
    tcg_gen_xori_i32(ret, arg1, arg2);
    tcg_gen_xori_i32(TCGV_HIGH(ret), TCGV_HIGH(arg1), arg2 >> 32);
}

/* XXX: use generic code when basic block handling is OK or CPU
   specific code (x86) */
static inline void tcg_gen_shl_i64(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_helper_1_2(tcg_helper_shl_i64, ret, arg1, arg2);
}

static inline void tcg_gen_shli_i64(TCGv ret, TCGv arg1, int64_t arg2)
{
    tcg_gen_shifti_i64(ret, arg1, arg2, 0, 0);
}

static inline void tcg_gen_shr_i64(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_helper_1_2(tcg_helper_shr_i64, ret, arg1, arg2);
}

static inline void tcg_gen_shri_i64(TCGv ret, TCGv arg1, int64_t arg2)
{
    tcg_gen_shifti_i64(ret, arg1, arg2, 1, 0);
}

static inline void tcg_gen_sar_i64(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_helper_1_2(tcg_helper_sar_i64, ret, arg1, arg2);
}

static inline void tcg_gen_sari_i64(TCGv ret, TCGv arg1, int64_t arg2)
{
    tcg_gen_shifti_i64(ret, arg1, arg2, 1, 1);
}

static inline void tcg_gen_brcond_i64(int cond, TCGv arg1, TCGv arg2, 
                                      int label_index)
{
    tcg_gen_op6ii(INDEX_op_brcond2_i32, 
                  arg1, TCGV_HIGH(arg1), arg2, TCGV_HIGH(arg2),
                  cond, label_index);
}

static inline void tcg_gen_mul_i64(TCGv ret, TCGv arg1, TCGv arg2)
{
    TCGv t0, t1;
    
    t0 = tcg_temp_new(TCG_TYPE_I64);
    t1 = tcg_temp_new(TCG_TYPE_I32);

    tcg_gen_op4(INDEX_op_mulu2_i32, t0, TCGV_HIGH(t0), arg1, arg2);
    
    tcg_gen_mul_i32(t1, arg1, TCGV_HIGH(arg2));
    tcg_gen_add_i32(TCGV_HIGH(t0), TCGV_HIGH(t0), t1);
    tcg_gen_mul_i32(t1, TCGV_HIGH(arg1), arg2);
    tcg_gen_add_i32(TCGV_HIGH(t0), TCGV_HIGH(t0), t1);
    
    tcg_gen_mov_i64(ret, t0);
    tcg_temp_free(t0);
    tcg_temp_free(t1);
}

static inline void tcg_gen_div_i64(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_helper_1_2(tcg_helper_div_i64, ret, arg1, arg2);
}

static inline void tcg_gen_rem_i64(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_helper_1_2(tcg_helper_rem_i64, ret, arg1, arg2);
}

static inline void tcg_gen_divu_i64(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_helper_1_2(tcg_helper_divu_i64, ret, arg1, arg2);
}

static inline void tcg_gen_remu_i64(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_helper_1_2(tcg_helper_remu_i64, ret, arg1, arg2);
}

#else

static inline void tcg_gen_mov_i64(TCGv ret, TCGv arg)
{
    if (GET_TCGV(ret) != GET_TCGV(arg))
        tcg_gen_op2(INDEX_op_mov_i64, ret, arg);
}

static inline void tcg_gen_movi_i64(TCGv ret, int64_t arg)
{
    tcg_gen_op2i(INDEX_op_movi_i64, ret, arg);
}

static inline void tcg_gen_ld8u_i64(TCGv ret, TCGv arg2,
                                    tcg_target_long offset)
{
    tcg_gen_op3i(INDEX_op_ld8u_i64, ret, arg2, offset);
}

static inline void tcg_gen_ld8s_i64(TCGv ret, TCGv arg2,
                                    tcg_target_long offset)
{
    tcg_gen_op3i(INDEX_op_ld8s_i64, ret, arg2, offset);
}

static inline void tcg_gen_ld16u_i64(TCGv ret, TCGv arg2,
                                     tcg_target_long offset)
{
    tcg_gen_op3i(INDEX_op_ld16u_i64, ret, arg2, offset);
}

static inline void tcg_gen_ld16s_i64(TCGv ret, TCGv arg2,
                                     tcg_target_long offset)
{
    tcg_gen_op3i(INDEX_op_ld16s_i64, ret, arg2, offset);
}

static inline void tcg_gen_ld32u_i64(TCGv ret, TCGv arg2,
                                     tcg_target_long offset)
{
    tcg_gen_op3i(INDEX_op_ld32u_i64, ret, arg2, offset);
}

static inline void tcg_gen_ld32s_i64(TCGv ret, TCGv arg2,
                                     tcg_target_long offset)
{
    tcg_gen_op3i(INDEX_op_ld32s_i64, ret, arg2, offset);
}

static inline void tcg_gen_ld_i64(TCGv ret, TCGv arg2, tcg_target_long offset)
{
    tcg_gen_op3i(INDEX_op_ld_i64, ret, arg2, offset);
}

static inline void tcg_gen_st8_i64(TCGv arg1, TCGv arg2,
                                   tcg_target_long offset)
{
    tcg_gen_op3i(INDEX_op_st8_i64, arg1, arg2, offset);
}

static inline void tcg_gen_st16_i64(TCGv arg1, TCGv arg2,
                                    tcg_target_long offset)
{
    tcg_gen_op3i(INDEX_op_st16_i64, arg1, arg2, offset);
}

static inline void tcg_gen_st32_i64(TCGv arg1, TCGv arg2,
                                    tcg_target_long offset)
{
    tcg_gen_op3i(INDEX_op_st32_i64, arg1, arg2, offset);
}

static inline void tcg_gen_st_i64(TCGv arg1, TCGv arg2, tcg_target_long offset)
{
    tcg_gen_op3i(INDEX_op_st_i64, arg1, arg2, offset);
}

static inline void tcg_gen_add_i64(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_op3(INDEX_op_add_i64, ret, arg1, arg2);
}

static inline void tcg_gen_sub_i64(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_op3(INDEX_op_sub_i64, ret, arg1, arg2);
}

static inline void tcg_gen_and_i64(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_op3(INDEX_op_and_i64, ret, arg1, arg2);
}

static inline void tcg_gen_andi_i64(TCGv ret, TCGv arg1, int64_t arg2)
{
    TCGv t0 = tcg_const_i64(arg2);
    tcg_gen_and_i64(ret, arg1, t0);
    tcg_temp_free(t0);
}

static inline void tcg_gen_or_i64(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_op3(INDEX_op_or_i64, ret, arg1, arg2);
}

static inline void tcg_gen_ori_i64(TCGv ret, TCGv arg1, int64_t arg2)
{
    TCGv t0 = tcg_const_i64(arg2);
    tcg_gen_or_i64(ret, arg1, t0);
    tcg_temp_free(t0);
}

static inline void tcg_gen_xor_i64(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_op3(INDEX_op_xor_i64, ret, arg1, arg2);
}

static inline void tcg_gen_xori_i64(TCGv ret, TCGv arg1, int64_t arg2)
{
    TCGv t0 = tcg_const_i64(arg2);
    tcg_gen_xor_i64(ret, arg1, t0);
    tcg_temp_free(t0);
}

static inline void tcg_gen_shl_i64(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_op3(INDEX_op_shl_i64, ret, arg1, arg2);
}

static inline void tcg_gen_shli_i64(TCGv ret, TCGv arg1, int64_t arg2)
{
    if (arg2 == 0) {
        tcg_gen_mov_i64(ret, arg1);
    } else {
        TCGv t0 = tcg_const_i64(arg2);
        tcg_gen_shl_i64(ret, arg1, t0);
        tcg_temp_free(t0);
    }
}

static inline void tcg_gen_shr_i64(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_op3(INDEX_op_shr_i64, ret, arg1, arg2);
}

static inline void tcg_gen_shri_i64(TCGv ret, TCGv arg1, int64_t arg2)
{
    if (arg2 == 0) {
        tcg_gen_mov_i64(ret, arg1);
    } else {
        TCGv t0 = tcg_const_i64(arg2);
        tcg_gen_shr_i64(ret, arg1, t0);
        tcg_temp_free(t0);
    }
}

static inline void tcg_gen_sar_i64(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_op3(INDEX_op_sar_i64, ret, arg1, arg2);
}

static inline void tcg_gen_sari_i64(TCGv ret, TCGv arg1, int64_t arg2)
{
    if (arg2 == 0) {
        tcg_gen_mov_i64(ret, arg1);
    } else {
        TCGv t0 = tcg_const_i64(arg2);
        tcg_gen_sar_i64(ret, arg1, t0);
        tcg_temp_free(t0);
    }
}

static inline void tcg_gen_brcond_i64(int cond, TCGv arg1, TCGv arg2, 
                                      int label_index)
{
    tcg_gen_op4ii(INDEX_op_brcond_i64, arg1, arg2, cond, label_index);
}

static inline void tcg_gen_mul_i64(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_op3(INDEX_op_mul_i64, ret, arg1, arg2);
}

#ifdef TCG_TARGET_HAS_div_i64
static inline void tcg_gen_div_i64(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_op3(INDEX_op_div_i64, ret, arg1, arg2);
}

static inline void tcg_gen_rem_i64(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_op3(INDEX_op_rem_i64, ret, arg1, arg2);
}

static inline void tcg_gen_divu_i64(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_op3(INDEX_op_divu_i64, ret, arg1, arg2);
}

static inline void tcg_gen_remu_i64(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_op3(INDEX_op_remu_i64, ret, arg1, arg2);
}
#else
static inline void tcg_gen_div_i64(TCGv ret, TCGv arg1, TCGv arg2)
{
    TCGv t0;
    t0 = tcg_temp_new(TCG_TYPE_I64);
    tcg_gen_sari_i64(t0, arg1, 63);
    tcg_gen_op5(INDEX_op_div2_i64, ret, t0, arg1, t0, arg2);
    tcg_temp_free(t0);
}

static inline void tcg_gen_rem_i64(TCGv ret, TCGv arg1, TCGv arg2)
{
    TCGv t0;
    t0 = tcg_temp_new(TCG_TYPE_I64);
    tcg_gen_sari_i64(t0, arg1, 63);
    tcg_gen_op5(INDEX_op_div2_i64, t0, ret, arg1, t0, arg2);
    tcg_temp_free(t0);
}

static inline void tcg_gen_divu_i64(TCGv ret, TCGv arg1, TCGv arg2)
{
    TCGv t0;
    t0 = tcg_temp_new(TCG_TYPE_I64);
    tcg_gen_movi_i64(t0, 0);
    tcg_gen_op5(INDEX_op_divu2_i64, ret, t0, arg1, t0, arg2);
    tcg_temp_free(t0);
}

static inline void tcg_gen_remu_i64(TCGv ret, TCGv arg1, TCGv arg2)
{
    TCGv t0;
    t0 = tcg_temp_new(TCG_TYPE_I64);
    tcg_gen_movi_i64(t0, 0);
    tcg_gen_op5(INDEX_op_divu2_i64, t0, ret, arg1, t0, arg2);
    tcg_temp_free(t0);
}
#endif

#endif

static inline void tcg_gen_addi_i64(TCGv ret, TCGv arg1, int64_t arg2)
{
    /* some cases can be optimized here */
    if (arg2 == 0) {
        tcg_gen_mov_i64(ret, arg1);
    } else {
        TCGv t0 = tcg_const_i64(arg2);
        tcg_gen_add_i64(ret, arg1, t0);
        tcg_temp_free(t0);
    }
}

static inline void tcg_gen_brcondi_i64(int cond, TCGv arg1, int64_t arg2, 
                                       int label_index)
{
    TCGv t0 = tcg_const_i64(arg2);
    tcg_gen_brcond_i64(cond, arg1, t0, label_index);
    tcg_temp_free(t0);
}

static inline void tcg_gen_muli_i64(TCGv ret, TCGv arg1, int64_t arg2)
{
    TCGv t0 = tcg_const_i64(arg2);
    tcg_gen_mul_i64(ret, arg1, t0);
    tcg_temp_free(t0);
}

static inline void tcg_gen_subfi_i64(TCGv ret, int64_t arg1, TCGv arg2)
{
    TCGv t0 = tcg_const_i64(arg1);
    tcg_gen_sub_i64(ret, t0, arg2);
    tcg_temp_free(t0);
}

static inline void tcg_gen_subi_i64(TCGv ret, TCGv arg1, int64_t arg2)
{
    /* some cases can be optimized here */
    if (arg2 == 0) {
        tcg_gen_mov_i64(ret, arg1);
    } else {
        TCGv t0 = tcg_const_i64(arg2);
        tcg_gen_sub_i64(ret, arg1, t0);
        tcg_temp_free(t0);
    }
}

/***************************************/
/* optional operations */

static inline void tcg_gen_ext8s_i32(TCGv ret, TCGv arg)
{
#ifdef TCG_TARGET_HAS_ext8s_i32
    tcg_gen_op2(INDEX_op_ext8s_i32, ret, arg);
#else
    tcg_gen_shli_i32(ret, arg, 24);
    tcg_gen_sari_i32(ret, ret, 24);
#endif
}

static inline void tcg_gen_ext16s_i32(TCGv ret, TCGv arg)
{
#ifdef TCG_TARGET_HAS_ext16s_i32
    tcg_gen_op2(INDEX_op_ext16s_i32, ret, arg);
#else
    tcg_gen_shli_i32(ret, arg, 16);
    tcg_gen_sari_i32(ret, ret, 16);
#endif
}

/* These are currently just for convenience.
   We assume a target will recognise these automatically .  */
static inline void tcg_gen_ext8u_i32(TCGv ret, TCGv arg)
{
    tcg_gen_andi_i32(ret, arg, 0xffu);
}

static inline void tcg_gen_ext16u_i32(TCGv ret, TCGv arg)
{
    tcg_gen_andi_i32(ret, arg, 0xffffu);
}

/* Note: we assume the two high bytes are set to zero */
static inline void tcg_gen_bswap16_i32(TCGv ret, TCGv arg)
{
#ifdef TCG_TARGET_HAS_bswap16_i32
    tcg_gen_op2(INDEX_op_bswap16_i32, ret, arg);
#else
    TCGv t0, t1;
    t0 = tcg_temp_new(TCG_TYPE_I32);
    t1 = tcg_temp_new(TCG_TYPE_I32);
    
    tcg_gen_shri_i32(t0, arg, 8);
    tcg_gen_andi_i32(t1, arg, 0x000000ff);
    tcg_gen_shli_i32(t1, t1, 8);
    tcg_gen_or_i32(ret, t0, t1);
    tcg_temp_free(t0);
    tcg_temp_free(t1);
#endif
}

static inline void tcg_gen_bswap_i32(TCGv ret, TCGv arg)
{
#ifdef TCG_TARGET_HAS_bswap_i32
    tcg_gen_op2(INDEX_op_bswap_i32, ret, arg);
#else
    TCGv t0, t1;
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
    tcg_temp_free(t0);
    tcg_temp_free(t1);
#endif
}

#if TCG_TARGET_REG_BITS == 32
static inline void tcg_gen_ext8s_i64(TCGv ret, TCGv arg)
{
    tcg_gen_ext8s_i32(ret, arg);
    tcg_gen_sari_i32(TCGV_HIGH(ret), ret, 31);
}

static inline void tcg_gen_ext16s_i64(TCGv ret, TCGv arg)
{
    tcg_gen_ext16s_i32(ret, arg);
    tcg_gen_sari_i32(TCGV_HIGH(ret), ret, 31);
}

static inline void tcg_gen_ext32s_i64(TCGv ret, TCGv arg)
{
    tcg_gen_mov_i32(ret, arg);
    tcg_gen_sari_i32(TCGV_HIGH(ret), ret, 31);
}

static inline void tcg_gen_ext8u_i64(TCGv ret, TCGv arg)
{
    tcg_gen_ext8u_i32(ret, arg);
    tcg_gen_movi_i32(TCGV_HIGH(ret), 0);
}

static inline void tcg_gen_ext16u_i64(TCGv ret, TCGv arg)
{
    tcg_gen_ext16u_i32(ret, arg);
    tcg_gen_movi_i32(TCGV_HIGH(ret), 0);
}

static inline void tcg_gen_ext32u_i64(TCGv ret, TCGv arg)
{
    tcg_gen_mov_i32(ret, arg);
    tcg_gen_movi_i32(TCGV_HIGH(ret), 0);
}

static inline void tcg_gen_trunc_i64_i32(TCGv ret, TCGv arg)
{
    tcg_gen_mov_i32(ret, arg);
}

static inline void tcg_gen_extu_i32_i64(TCGv ret, TCGv arg)
{
    tcg_gen_mov_i32(ret, arg);
    tcg_gen_movi_i32(TCGV_HIGH(ret), 0);
}

static inline void tcg_gen_ext_i32_i64(TCGv ret, TCGv arg)
{
    tcg_gen_mov_i32(ret, arg);
    tcg_gen_sari_i32(TCGV_HIGH(ret), ret, 31);
}

static inline void tcg_gen_bswap_i64(TCGv ret, TCGv arg)
{
    TCGv t0, t1;
    t0 = tcg_temp_new(TCG_TYPE_I32);
    t1 = tcg_temp_new(TCG_TYPE_I32);

    tcg_gen_bswap_i32(t0, arg);
    tcg_gen_bswap_i32(t1, TCGV_HIGH(arg));
    tcg_gen_mov_i32(ret, t1);
    tcg_gen_mov_i32(TCGV_HIGH(ret), t0);
    tcg_temp_free(t0);
    tcg_temp_free(t1);
}
#else

static inline void tcg_gen_ext8s_i64(TCGv ret, TCGv arg)
{
#ifdef TCG_TARGET_HAS_ext8s_i64
    tcg_gen_op2(INDEX_op_ext8s_i64, ret, arg);
#else
    tcg_gen_shli_i64(ret, arg, 56);
    tcg_gen_sari_i64(ret, ret, 56);
#endif
}

static inline void tcg_gen_ext16s_i64(TCGv ret, TCGv arg)
{
#ifdef TCG_TARGET_HAS_ext16s_i64
    tcg_gen_op2(INDEX_op_ext16s_i64, ret, arg);
#else
    tcg_gen_shli_i64(ret, arg, 48);
    tcg_gen_sari_i64(ret, ret, 48);
#endif
}

static inline void tcg_gen_ext32s_i64(TCGv ret, TCGv arg)
{
#ifdef TCG_TARGET_HAS_ext32s_i64
    tcg_gen_op2(INDEX_op_ext32s_i64, ret, arg);
#else
    tcg_gen_shli_i64(ret, arg, 32);
    tcg_gen_sari_i64(ret, ret, 32);
#endif
}

static inline void tcg_gen_ext8u_i64(TCGv ret, TCGv arg)
{
    tcg_gen_andi_i64(ret, arg, 0xffu);
}

static inline void tcg_gen_ext16u_i64(TCGv ret, TCGv arg)
{
    tcg_gen_andi_i64(ret, arg, 0xffffu);
}

static inline void tcg_gen_ext32u_i64(TCGv ret, TCGv arg)
{
    tcg_gen_andi_i64(ret, arg, 0xffffffffu);
}

/* Note: we assume the target supports move between 32 and 64 bit
   registers.  This will probably break MIPS64 targets.  */
static inline void tcg_gen_trunc_i64_i32(TCGv ret, TCGv arg)
{
    tcg_gen_mov_i32(ret, arg);
}

/* Note: we assume the target supports move between 32 and 64 bit
   registers */
static inline void tcg_gen_extu_i32_i64(TCGv ret, TCGv arg)
{
    tcg_gen_andi_i64(ret, arg, 0xffffffffu);
}

/* Note: we assume the target supports move between 32 and 64 bit
   registers */
static inline void tcg_gen_ext_i32_i64(TCGv ret, TCGv arg)
{
    tcg_gen_ext32s_i64(ret, arg);
}

static inline void tcg_gen_bswap_i64(TCGv ret, TCGv arg)
{
#ifdef TCG_TARGET_HAS_bswap_i64
    tcg_gen_op2(INDEX_op_bswap_i64, ret, arg);
#else
    TCGv t0, t1;
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
    tcg_temp_free(t0);
    tcg_temp_free(t1);
#endif
}

#endif

static inline void tcg_gen_neg_i32(TCGv ret, TCGv arg)
{
#ifdef TCG_TARGET_HAS_neg_i32
    tcg_gen_op2(INDEX_op_neg_i32, ret, arg);
#else
    TCGv t0 = tcg_const_i32(0);
    tcg_gen_sub_i32(ret, t0, arg);
    tcg_temp_free(t0);
#endif
}

static inline void tcg_gen_neg_i64(TCGv ret, TCGv arg)
{
#ifdef TCG_TARGET_HAS_neg_i64
    tcg_gen_op2(INDEX_op_neg_i64, ret, arg);
#else
    TCGv t0 = tcg_const_i64(0);
    tcg_gen_sub_i64(ret, t0, arg);
    tcg_temp_free(t0);
#endif
}

static inline void tcg_gen_not_i32(TCGv ret, TCGv arg)
{
    tcg_gen_xori_i32(ret, arg, -1);
}

static inline void tcg_gen_not_i64(TCGv ret, TCGv arg)
{
    tcg_gen_xori_i64(ret, arg, -1);
}

static inline void tcg_gen_discard_i32(TCGv arg)
{
    tcg_gen_op1(INDEX_op_discard, arg);
}

#if TCG_TARGET_REG_BITS == 32
static inline void tcg_gen_discard_i64(TCGv arg)
{
    tcg_gen_discard_i32(arg);
    tcg_gen_discard_i32(TCGV_HIGH(arg));
}
#else
static inline void tcg_gen_discard_i64(TCGv arg)
{
    tcg_gen_op1(INDEX_op_discard, arg);
}
#endif

static inline void tcg_gen_concat_i32_i64(TCGv dest, TCGv low, TCGv high)
{
#if TCG_TARGET_REG_BITS == 32
    tcg_gen_mov_i32(dest, low);
    tcg_gen_mov_i32(TCGV_HIGH(dest), high);
#else
    TCGv tmp = tcg_temp_new (TCG_TYPE_I64);
    /* This extension is only needed for type correctness.
       We may be able to do better given target specific information.  */
    tcg_gen_extu_i32_i64(tmp, high);
    tcg_gen_shli_i64(tmp, tmp, 32);
    tcg_gen_extu_i32_i64(dest, low);
    tcg_gen_or_i64(dest, dest, tmp);
    tcg_temp_free(tmp);
#endif
}

static inline void tcg_gen_concat32_i64(TCGv dest, TCGv low, TCGv high)
{
#if TCG_TARGET_REG_BITS == 32
    tcg_gen_concat_i32_i64(dest, low, high);
#else
    TCGv tmp = tcg_temp_new(TCG_TYPE_I64);
    tcg_gen_ext32u_i64(dest, low);
    tcg_gen_shli_i64(tmp, high, 32);
    tcg_gen_or_i64(dest, dest, tmp);
    tcg_temp_free(tmp);
#endif
}

static inline void tcg_gen_andc_i32(TCGv ret, TCGv arg1, TCGv arg2)
{
    TCGv t0;
    t0 = tcg_temp_new(TCG_TYPE_I32);
    tcg_gen_not_i32(t0, arg2);
    tcg_gen_and_i32(ret, arg1, t0);
    tcg_temp_free(t0);
}

static inline void tcg_gen_andc_i64(TCGv ret, TCGv arg1, TCGv arg2)
{
    TCGv t0;
    t0 = tcg_temp_new(TCG_TYPE_I64);
    tcg_gen_not_i64(t0, arg2);
    tcg_gen_and_i64(ret, arg1, t0);
    tcg_temp_free(t0);
}

static inline void tcg_gen_eqv_i32(TCGv ret, TCGv arg1, TCGv arg2)
{
    TCGv t0;
    t0 = tcg_temp_new(TCG_TYPE_I32);
    tcg_gen_xor_i32(t0, arg1, arg2);
    tcg_gen_not_i32(ret, t0);
    tcg_temp_free(t0);
}

static inline void tcg_gen_eqv_i64(TCGv ret, TCGv arg1, TCGv arg2)
{
    TCGv t0;
    t0 = tcg_temp_new(TCG_TYPE_I64);
    tcg_gen_xor_i64(t0, arg1, arg2);
    tcg_gen_not_i64(ret, t0);
    tcg_temp_free(t0);
}

static inline void tcg_gen_nand_i32(TCGv ret, TCGv arg1, TCGv arg2)
{
    TCGv t0;
    t0 = tcg_temp_new(TCG_TYPE_I32);
    tcg_gen_and_i32(t0, arg1, arg2);
    tcg_gen_not_i32(ret, t0);
    tcg_temp_free(t0);
}

static inline void tcg_gen_nand_i64(TCGv ret, TCGv arg1, TCGv arg2)
{
    TCGv t0;
    t0 = tcg_temp_new(TCG_TYPE_I64);
    tcg_gen_and_i64(t0, arg1, arg2);
    tcg_gen_not_i64(ret, t0);
    tcg_temp_free(t0);
}

static inline void tcg_gen_nor_i32(TCGv ret, TCGv arg1, TCGv arg2)
{
    TCGv t0;
    t0 = tcg_temp_new(TCG_TYPE_I32);
    tcg_gen_or_i32(t0, arg1, arg2);
    tcg_gen_not_i32(ret, t0);
    tcg_temp_free(t0);
}

static inline void tcg_gen_nor_i64(TCGv ret, TCGv arg1, TCGv arg2)
{
    TCGv t0;
    t0 = tcg_temp_new(TCG_TYPE_I64);
    tcg_gen_or_i64(t0, arg1, arg2);
    tcg_gen_not_i64(ret, t0);
    tcg_temp_free(t0);
}

static inline void tcg_gen_orc_i32(TCGv ret, TCGv arg1, TCGv arg2)
{
    TCGv t0;
    t0 = tcg_temp_new(TCG_TYPE_I32);
    tcg_gen_not_i32(t0, arg2);
    tcg_gen_or_i32(ret, arg1, t0);
    tcg_temp_free(t0);
}

static inline void tcg_gen_orc_i64(TCGv ret, TCGv arg1, TCGv arg2)
{
    TCGv t0;
    t0 = tcg_temp_new(TCG_TYPE_I64);
    tcg_gen_not_i64(t0, arg2);
    tcg_gen_or_i64(ret, arg1, t0);
    tcg_temp_free(t0);
}

/***************************************/
/* QEMU specific operations. Their type depend on the QEMU CPU
   type. */
#ifndef TARGET_LONG_BITS
#error must include QEMU headers
#endif

/* debug info: write the PC of the corresponding QEMU CPU instruction */
static inline void tcg_gen_debug_insn_start(uint64_t pc)
{
    /* XXX: must really use a 32 bit size for TCGArg in all cases */
#if TARGET_LONG_BITS > TCG_TARGET_REG_BITS
    tcg_gen_op2ii(INDEX_op_debug_insn_start, 
                  (uint32_t)(pc), (uint32_t)(pc >> 32));
#else
    tcg_gen_op1i(INDEX_op_debug_insn_start, pc);
#endif
}

static inline void tcg_gen_exit_tb(tcg_target_long val)
{
    tcg_gen_op1i(INDEX_op_exit_tb, val);
}

static inline void tcg_gen_goto_tb(int idx)
{
    tcg_gen_op1i(INDEX_op_goto_tb, idx);
}

#if TCG_TARGET_REG_BITS == 32
static inline void tcg_gen_qemu_ld8u(TCGv ret, TCGv addr, int mem_index)
{
#if TARGET_LONG_BITS == 32
    tcg_gen_op3i(INDEX_op_qemu_ld8u, ret, addr, mem_index);
#else
    tcg_gen_op4i(INDEX_op_qemu_ld8u, ret, addr, TCGV_HIGH(addr), mem_index);
    tcg_gen_movi_i32(TCGV_HIGH(ret), 0);
#endif
}

static inline void tcg_gen_qemu_ld8s(TCGv ret, TCGv addr, int mem_index)
{
#if TARGET_LONG_BITS == 32
    tcg_gen_op3i(INDEX_op_qemu_ld8s, ret, addr, mem_index);
#else
    tcg_gen_op4i(INDEX_op_qemu_ld8s, ret, addr, TCGV_HIGH(addr), mem_index);
    tcg_gen_sari_i32(TCGV_HIGH(ret), ret, 31);
#endif
}

static inline void tcg_gen_qemu_ld16u(TCGv ret, TCGv addr, int mem_index)
{
#if TARGET_LONG_BITS == 32
    tcg_gen_op3i(INDEX_op_qemu_ld16u, ret, addr, mem_index);
#else
    tcg_gen_op4i(INDEX_op_qemu_ld16u, ret, addr, TCGV_HIGH(addr), mem_index);
    tcg_gen_movi_i32(TCGV_HIGH(ret), 0);
#endif
}

static inline void tcg_gen_qemu_ld16s(TCGv ret, TCGv addr, int mem_index)
{
#if TARGET_LONG_BITS == 32
    tcg_gen_op3i(INDEX_op_qemu_ld16s, ret, addr, mem_index);
#else
    tcg_gen_op4i(INDEX_op_qemu_ld16s, ret, addr, TCGV_HIGH(addr), mem_index);
    tcg_gen_sari_i32(TCGV_HIGH(ret), ret, 31);
#endif
}

static inline void tcg_gen_qemu_ld32u(TCGv ret, TCGv addr, int mem_index)
{
#if TARGET_LONG_BITS == 32
    tcg_gen_op3i(INDEX_op_qemu_ld32u, ret, addr, mem_index);
#else
    tcg_gen_op4i(INDEX_op_qemu_ld32u, ret, addr, TCGV_HIGH(addr), mem_index);
    tcg_gen_movi_i32(TCGV_HIGH(ret), 0);
#endif
}

static inline void tcg_gen_qemu_ld32s(TCGv ret, TCGv addr, int mem_index)
{
#if TARGET_LONG_BITS == 32
    tcg_gen_op3i(INDEX_op_qemu_ld32u, ret, addr, mem_index);
#else
    tcg_gen_op4i(INDEX_op_qemu_ld32u, ret, addr, TCGV_HIGH(addr), mem_index);
    tcg_gen_sari_i32(TCGV_HIGH(ret), ret, 31);
#endif
}

static inline void tcg_gen_qemu_ld64(TCGv ret, TCGv addr, int mem_index)
{
#if TARGET_LONG_BITS == 32
    tcg_gen_op4i(INDEX_op_qemu_ld64, ret, TCGV_HIGH(ret), addr, mem_index);
#else
    tcg_gen_op5i(INDEX_op_qemu_ld64, ret, TCGV_HIGH(ret),
                 addr, TCGV_HIGH(addr), mem_index);
#endif
}

static inline void tcg_gen_qemu_st8(TCGv arg, TCGv addr, int mem_index)
{
#if TARGET_LONG_BITS == 32
    tcg_gen_op3i(INDEX_op_qemu_st8, arg, addr, mem_index);
#else
    tcg_gen_op4i(INDEX_op_qemu_st8, arg, addr, TCGV_HIGH(addr), mem_index);
#endif
}

static inline void tcg_gen_qemu_st16(TCGv arg, TCGv addr, int mem_index)
{
#if TARGET_LONG_BITS == 32
    tcg_gen_op3i(INDEX_op_qemu_st16, arg, addr, mem_index);
#else
    tcg_gen_op4i(INDEX_op_qemu_st16, arg, addr, TCGV_HIGH(addr), mem_index);
#endif
}

static inline void tcg_gen_qemu_st32(TCGv arg, TCGv addr, int mem_index)
{
#if TARGET_LONG_BITS == 32
    tcg_gen_op3i(INDEX_op_qemu_st32, arg, addr, mem_index);
#else
    tcg_gen_op4i(INDEX_op_qemu_st32, arg, addr, TCGV_HIGH(addr), mem_index);
#endif
}

static inline void tcg_gen_qemu_st64(TCGv arg, TCGv addr, int mem_index)
{
#if TARGET_LONG_BITS == 32
    tcg_gen_op4i(INDEX_op_qemu_st64, arg, TCGV_HIGH(arg), addr, mem_index);
#else
    tcg_gen_op5i(INDEX_op_qemu_st64, arg, TCGV_HIGH(arg),
                 addr, TCGV_HIGH(addr), mem_index);
#endif
}

#define tcg_gen_ld_ptr tcg_gen_ld_i32
#define tcg_gen_discard_ptr tcg_gen_discard_i32

#else /* TCG_TARGET_REG_BITS == 32 */

static inline void tcg_gen_qemu_ld8u(TCGv ret, TCGv addr, int mem_index)
{
    tcg_gen_op3i(INDEX_op_qemu_ld8u, ret, addr, mem_index);
}

static inline void tcg_gen_qemu_ld8s(TCGv ret, TCGv addr, int mem_index)
{
    tcg_gen_op3i(INDEX_op_qemu_ld8s, ret, addr, mem_index);
}

static inline void tcg_gen_qemu_ld16u(TCGv ret, TCGv addr, int mem_index)
{
    tcg_gen_op3i(INDEX_op_qemu_ld16u, ret, addr, mem_index);
}

static inline void tcg_gen_qemu_ld16s(TCGv ret, TCGv addr, int mem_index)
{
    tcg_gen_op3i(INDEX_op_qemu_ld16s, ret, addr, mem_index);
}

static inline void tcg_gen_qemu_ld32u(TCGv ret, TCGv addr, int mem_index)
{
    tcg_gen_op3i(INDEX_op_qemu_ld32u, ret, addr, mem_index);
}

static inline void tcg_gen_qemu_ld32s(TCGv ret, TCGv addr, int mem_index)
{
    tcg_gen_op3i(INDEX_op_qemu_ld32s, ret, addr, mem_index);
}

static inline void tcg_gen_qemu_ld64(TCGv ret, TCGv addr, int mem_index)
{
    tcg_gen_op3i(INDEX_op_qemu_ld64, ret, addr, mem_index);
}

static inline void tcg_gen_qemu_st8(TCGv arg, TCGv addr, int mem_index)
{
    tcg_gen_op3i(INDEX_op_qemu_st8, arg, addr, mem_index);
}

static inline void tcg_gen_qemu_st16(TCGv arg, TCGv addr, int mem_index)
{
    tcg_gen_op3i(INDEX_op_qemu_st16, arg, addr, mem_index);
}

static inline void tcg_gen_qemu_st32(TCGv arg, TCGv addr, int mem_index)
{
    tcg_gen_op3i(INDEX_op_qemu_st32, arg, addr, mem_index);
}

static inline void tcg_gen_qemu_st64(TCGv arg, TCGv addr, int mem_index)
{
    tcg_gen_op3i(INDEX_op_qemu_st64, arg, addr, mem_index);
}

#define tcg_gen_ld_ptr tcg_gen_ld_i64
#define tcg_gen_discard_ptr tcg_gen_discard_i64

#endif /* TCG_TARGET_REG_BITS != 32 */

#if TARGET_LONG_BITS == 64
#define TCG_TYPE_TL TCG_TYPE_I64
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
#define tcg_gen_mul_tl tcg_gen_mul_i64
#define tcg_gen_muli_tl tcg_gen_muli_i64
#define tcg_gen_discard_tl tcg_gen_discard_i64
#define tcg_gen_trunc_tl_i32 tcg_gen_trunc_i64_i32
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
#define tcg_gen_concat_tl_i64 tcg_gen_concat32_i64
#define tcg_gen_andc_tl tcg_gen_andc_i64
#define tcg_gen_eqv_tl tcg_gen_eqv_i64
#define tcg_gen_nand_tl tcg_gen_nand_i64
#define tcg_gen_nor_tl tcg_gen_nor_i64
#define tcg_gen_orc_tl tcg_gen_orc_i64
#define tcg_const_tl tcg_const_i64
#define tcg_const_local_tl tcg_const_local_i64
#else
#define TCG_TYPE_TL TCG_TYPE_I32
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
#define tcg_gen_mul_tl tcg_gen_mul_i32
#define tcg_gen_muli_tl tcg_gen_muli_i32
#define tcg_gen_discard_tl tcg_gen_discard_i32
#define tcg_gen_trunc_tl_i32 tcg_gen_mov_i32
#define tcg_gen_trunc_i64_tl tcg_gen_trunc_i64_i32
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
#define tcg_gen_concat_tl_i64 tcg_gen_concat_i32_i64
#define tcg_gen_andc_tl tcg_gen_andc_i32
#define tcg_gen_eqv_tl tcg_gen_eqv_i32
#define tcg_gen_nand_tl tcg_gen_nand_i32
#define tcg_gen_nor_tl tcg_gen_nor_i32
#define tcg_gen_orc_tl tcg_gen_orc_i32
#define tcg_const_tl tcg_const_i32
#define tcg_const_local_tl tcg_const_local_i32
#endif

#if TCG_TARGET_REG_BITS == 32
#define tcg_gen_add_ptr tcg_gen_add_i32
#define tcg_gen_addi_ptr tcg_gen_addi_i32
#define tcg_gen_ext_i32_ptr tcg_gen_mov_i32
#else /* TCG_TARGET_REG_BITS == 32 */
#define tcg_gen_add_ptr tcg_gen_add_i64
#define tcg_gen_addi_ptr tcg_gen_addi_i64
#define tcg_gen_ext_i32_ptr tcg_gen_ext_i32_i64
#endif /* TCG_TARGET_REG_BITS != 32 */

