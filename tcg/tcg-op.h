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
#include "exec/helper-proto.h"

int gen_new_label(void);

static inline void tcg_gen_op0(TCGOpcode opc)
{
    *tcg_ctx.gen_opc_ptr++ = opc;
}

static inline void tcg_gen_op1_i32(TCGOpcode opc, TCGv_i32 arg1)
{
    *tcg_ctx.gen_opc_ptr++ = opc;
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I32(arg1);
}

static inline void tcg_gen_op1_i64(TCGOpcode opc, TCGv_i64 arg1)
{
    *tcg_ctx.gen_opc_ptr++ = opc;
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I64(arg1);
}

static inline void tcg_gen_op1i(TCGOpcode opc, TCGArg arg1)
{
    *tcg_ctx.gen_opc_ptr++ = opc;
    *tcg_ctx.gen_opparam_ptr++ = arg1;
}

static inline void tcg_gen_op2_i32(TCGOpcode opc, TCGv_i32 arg1, TCGv_i32 arg2)
{
    *tcg_ctx.gen_opc_ptr++ = opc;
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I32(arg1);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I32(arg2);
}

static inline void tcg_gen_op2_i64(TCGOpcode opc, TCGv_i64 arg1, TCGv_i64 arg2)
{
    *tcg_ctx.gen_opc_ptr++ = opc;
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I64(arg1);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I64(arg2);
}

static inline void tcg_gen_op2i_i32(TCGOpcode opc, TCGv_i32 arg1, TCGArg arg2)
{
    *tcg_ctx.gen_opc_ptr++ = opc;
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I32(arg1);
    *tcg_ctx.gen_opparam_ptr++ = arg2;
}

static inline void tcg_gen_op2i_i64(TCGOpcode opc, TCGv_i64 arg1, TCGArg arg2)
{
    *tcg_ctx.gen_opc_ptr++ = opc;
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I64(arg1);
    *tcg_ctx.gen_opparam_ptr++ = arg2;
}

static inline void tcg_gen_op2ii(TCGOpcode opc, TCGArg arg1, TCGArg arg2)
{
    *tcg_ctx.gen_opc_ptr++ = opc;
    *tcg_ctx.gen_opparam_ptr++ = arg1;
    *tcg_ctx.gen_opparam_ptr++ = arg2;
}

static inline void tcg_gen_op3_i32(TCGOpcode opc, TCGv_i32 arg1, TCGv_i32 arg2,
                                   TCGv_i32 arg3)
{
    *tcg_ctx.gen_opc_ptr++ = opc;
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I32(arg1);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I32(arg2);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I32(arg3);
}

static inline void tcg_gen_op3_i64(TCGOpcode opc, TCGv_i64 arg1, TCGv_i64 arg2,
                                   TCGv_i64 arg3)
{
    *tcg_ctx.gen_opc_ptr++ = opc;
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I64(arg1);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I64(arg2);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I64(arg3);
}

static inline void tcg_gen_op3i_i32(TCGOpcode opc, TCGv_i32 arg1,
                                    TCGv_i32 arg2, TCGArg arg3)
{
    *tcg_ctx.gen_opc_ptr++ = opc;
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I32(arg1);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I32(arg2);
    *tcg_ctx.gen_opparam_ptr++ = arg3;
}

static inline void tcg_gen_op3i_i64(TCGOpcode opc, TCGv_i64 arg1,
                                    TCGv_i64 arg2, TCGArg arg3)
{
    *tcg_ctx.gen_opc_ptr++ = opc;
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I64(arg1);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I64(arg2);
    *tcg_ctx.gen_opparam_ptr++ = arg3;
}

static inline void tcg_gen_ldst_op_i32(TCGOpcode opc, TCGv_i32 val,
                                       TCGv_ptr base, TCGArg offset)
{
    *tcg_ctx.gen_opc_ptr++ = opc;
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I32(val);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_PTR(base);
    *tcg_ctx.gen_opparam_ptr++ = offset;
}

static inline void tcg_gen_ldst_op_i64(TCGOpcode opc, TCGv_i64 val,
                                       TCGv_ptr base, TCGArg offset)
{
    *tcg_ctx.gen_opc_ptr++ = opc;
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I64(val);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_PTR(base);
    *tcg_ctx.gen_opparam_ptr++ = offset;
}

static inline void tcg_gen_op4_i32(TCGOpcode opc, TCGv_i32 arg1, TCGv_i32 arg2,
                                   TCGv_i32 arg3, TCGv_i32 arg4)
{
    *tcg_ctx.gen_opc_ptr++ = opc;
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I32(arg1);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I32(arg2);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I32(arg3);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I32(arg4);
}

static inline void tcg_gen_op4_i64(TCGOpcode opc, TCGv_i64 arg1, TCGv_i64 arg2,
                                   TCGv_i64 arg3, TCGv_i64 arg4)
{
    *tcg_ctx.gen_opc_ptr++ = opc;
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I64(arg1);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I64(arg2);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I64(arg3);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I64(arg4);
}

static inline void tcg_gen_op4i_i32(TCGOpcode opc, TCGv_i32 arg1, TCGv_i32 arg2,
                                    TCGv_i32 arg3, TCGArg arg4)
{
    *tcg_ctx.gen_opc_ptr++ = opc;
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I32(arg1);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I32(arg2);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I32(arg3);
    *tcg_ctx.gen_opparam_ptr++ = arg4;
}

static inline void tcg_gen_op4i_i64(TCGOpcode opc, TCGv_i64 arg1, TCGv_i64 arg2,
                                    TCGv_i64 arg3, TCGArg arg4)
{
    *tcg_ctx.gen_opc_ptr++ = opc;
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I64(arg1);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I64(arg2);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I64(arg3);
    *tcg_ctx.gen_opparam_ptr++ = arg4;
}

static inline void tcg_gen_op4ii_i32(TCGOpcode opc, TCGv_i32 arg1, TCGv_i32 arg2,
                                     TCGArg arg3, TCGArg arg4)
{
    *tcg_ctx.gen_opc_ptr++ = opc;
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I32(arg1);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I32(arg2);
    *tcg_ctx.gen_opparam_ptr++ = arg3;
    *tcg_ctx.gen_opparam_ptr++ = arg4;
}

static inline void tcg_gen_op4ii_i64(TCGOpcode opc, TCGv_i64 arg1, TCGv_i64 arg2,
                                     TCGArg arg3, TCGArg arg4)
{
    *tcg_ctx.gen_opc_ptr++ = opc;
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I64(arg1);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I64(arg2);
    *tcg_ctx.gen_opparam_ptr++ = arg3;
    *tcg_ctx.gen_opparam_ptr++ = arg4;
}

static inline void tcg_gen_op5_i32(TCGOpcode opc, TCGv_i32 arg1, TCGv_i32 arg2,
                                   TCGv_i32 arg3, TCGv_i32 arg4, TCGv_i32 arg5)
{
    *tcg_ctx.gen_opc_ptr++ = opc;
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I32(arg1);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I32(arg2);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I32(arg3);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I32(arg4);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I32(arg5);
}

static inline void tcg_gen_op5_i64(TCGOpcode opc, TCGv_i64 arg1, TCGv_i64 arg2,
                                   TCGv_i64 arg3, TCGv_i64 arg4, TCGv_i64 arg5)
{
    *tcg_ctx.gen_opc_ptr++ = opc;
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I64(arg1);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I64(arg2);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I64(arg3);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I64(arg4);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I64(arg5);
}

static inline void tcg_gen_op5i_i32(TCGOpcode opc, TCGv_i32 arg1, TCGv_i32 arg2,
                                    TCGv_i32 arg3, TCGv_i32 arg4, TCGArg arg5)
{
    *tcg_ctx.gen_opc_ptr++ = opc;
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I32(arg1);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I32(arg2);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I32(arg3);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I32(arg4);
    *tcg_ctx.gen_opparam_ptr++ = arg5;
}

static inline void tcg_gen_op5i_i64(TCGOpcode opc, TCGv_i64 arg1, TCGv_i64 arg2,
                                    TCGv_i64 arg3, TCGv_i64 arg4, TCGArg arg5)
{
    *tcg_ctx.gen_opc_ptr++ = opc;
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I64(arg1);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I64(arg2);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I64(arg3);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I64(arg4);
    *tcg_ctx.gen_opparam_ptr++ = arg5;
}

static inline void tcg_gen_op5ii_i32(TCGOpcode opc, TCGv_i32 arg1,
                                     TCGv_i32 arg2, TCGv_i32 arg3,
                                     TCGArg arg4, TCGArg arg5)
{
    *tcg_ctx.gen_opc_ptr++ = opc;
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I32(arg1);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I32(arg2);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I32(arg3);
    *tcg_ctx.gen_opparam_ptr++ = arg4;
    *tcg_ctx.gen_opparam_ptr++ = arg5;
}

static inline void tcg_gen_op5ii_i64(TCGOpcode opc, TCGv_i64 arg1,
                                     TCGv_i64 arg2, TCGv_i64 arg3,
                                     TCGArg arg4, TCGArg arg5)
{
    *tcg_ctx.gen_opc_ptr++ = opc;
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I64(arg1);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I64(arg2);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I64(arg3);
    *tcg_ctx.gen_opparam_ptr++ = arg4;
    *tcg_ctx.gen_opparam_ptr++ = arg5;
}

static inline void tcg_gen_op6_i32(TCGOpcode opc, TCGv_i32 arg1, TCGv_i32 arg2,
                                   TCGv_i32 arg3, TCGv_i32 arg4, TCGv_i32 arg5,
                                   TCGv_i32 arg6)
{
    *tcg_ctx.gen_opc_ptr++ = opc;
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I32(arg1);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I32(arg2);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I32(arg3);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I32(arg4);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I32(arg5);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I32(arg6);
}

static inline void tcg_gen_op6_i64(TCGOpcode opc, TCGv_i64 arg1, TCGv_i64 arg2,
                                   TCGv_i64 arg3, TCGv_i64 arg4, TCGv_i64 arg5,
                                   TCGv_i64 arg6)
{
    *tcg_ctx.gen_opc_ptr++ = opc;
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I64(arg1);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I64(arg2);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I64(arg3);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I64(arg4);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I64(arg5);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I64(arg6);
}

static inline void tcg_gen_op6i_i32(TCGOpcode opc, TCGv_i32 arg1, TCGv_i32 arg2,
                                    TCGv_i32 arg3, TCGv_i32 arg4,
                                    TCGv_i32 arg5, TCGArg arg6)
{
    *tcg_ctx.gen_opc_ptr++ = opc;
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I32(arg1);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I32(arg2);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I32(arg3);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I32(arg4);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I32(arg5);
    *tcg_ctx.gen_opparam_ptr++ = arg6;
}

static inline void tcg_gen_op6i_i64(TCGOpcode opc, TCGv_i64 arg1, TCGv_i64 arg2,
                                    TCGv_i64 arg3, TCGv_i64 arg4,
                                    TCGv_i64 arg5, TCGArg arg6)
{
    *tcg_ctx.gen_opc_ptr++ = opc;
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I64(arg1);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I64(arg2);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I64(arg3);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I64(arg4);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I64(arg5);
    *tcg_ctx.gen_opparam_ptr++ = arg6;
}

static inline void tcg_gen_op6ii_i32(TCGOpcode opc, TCGv_i32 arg1,
                                     TCGv_i32 arg2, TCGv_i32 arg3,
                                     TCGv_i32 arg4, TCGArg arg5, TCGArg arg6)
{
    *tcg_ctx.gen_opc_ptr++ = opc;
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I32(arg1);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I32(arg2);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I32(arg3);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I32(arg4);
    *tcg_ctx.gen_opparam_ptr++ = arg5;
    *tcg_ctx.gen_opparam_ptr++ = arg6;
}

static inline void tcg_gen_op6ii_i64(TCGOpcode opc, TCGv_i64 arg1,
                                     TCGv_i64 arg2, TCGv_i64 arg3,
                                     TCGv_i64 arg4, TCGArg arg5, TCGArg arg6)
{
    *tcg_ctx.gen_opc_ptr++ = opc;
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I64(arg1);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I64(arg2);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I64(arg3);
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I64(arg4);
    *tcg_ctx.gen_opparam_ptr++ = arg5;
    *tcg_ctx.gen_opparam_ptr++ = arg6;
}

static inline void tcg_add_param_i32(TCGv_i32 val)
{
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I32(val);
}

static inline void tcg_add_param_i64(TCGv_i64 val)
{
#if TCG_TARGET_REG_BITS == 32
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I32(TCGV_LOW(val));
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I32(TCGV_HIGH(val));
#else
    *tcg_ctx.gen_opparam_ptr++ = GET_TCGV_I64(val);
#endif
}

static inline void gen_set_label(int n)
{
    tcg_gen_op1i(INDEX_op_set_label, n);
}

static inline void tcg_gen_br(int label)
{
    tcg_gen_op1i(INDEX_op_br, label);
}

static inline void tcg_gen_mov_i32(TCGv_i32 ret, TCGv_i32 arg)
{
    if (!TCGV_EQUAL_I32(ret, arg))
        tcg_gen_op2_i32(INDEX_op_mov_i32, ret, arg);
}

static inline void tcg_gen_movi_i32(TCGv_i32 ret, int32_t arg)
{
    tcg_gen_op2i_i32(INDEX_op_movi_i32, ret, arg);
}

/* A version of dh_sizemask from def-helper.h that doesn't rely on
   preprocessor magic.  */
static inline int tcg_gen_sizemask(int n, int is_64bit, int is_signed)
{
    return (is_64bit << n*2) | (is_signed << (n*2 + 1));
}

/* helper calls */
static inline void tcg_gen_helperN(void *func, int flags, int sizemask,
                                   TCGArg ret, int nargs, TCGArg *args)
{
    tcg_gen_callN(&tcg_ctx, func, flags, sizemask, ret, nargs, args);
}

/* Note: Both tcg_gen_helper32() and tcg_gen_helper64() are currently
   reserved for helpers in tcg-runtime.c. These helpers all do not read
   globals and do not have side effects, hence the call to tcg_gen_callN()
   with TCG_CALL_NO_READ_GLOBALS | TCG_CALL_NO_SIDE_EFFECTS. This may need
   to be adjusted if these functions start to be used with other helpers. */
static inline void tcg_gen_helper32(void *func, int sizemask, TCGv_i32 ret,
                                    TCGv_i32 a, TCGv_i32 b)
{
    TCGArg args[2];
    args[0] = GET_TCGV_I32(a);
    args[1] = GET_TCGV_I32(b);
    tcg_gen_callN(&tcg_ctx, func,
                  TCG_CALL_NO_READ_GLOBALS | TCG_CALL_NO_SIDE_EFFECTS,
                  sizemask, GET_TCGV_I32(ret), 2, args);
}

static inline void tcg_gen_helper64(void *func, int sizemask, TCGv_i64 ret,
                                    TCGv_i64 a, TCGv_i64 b)
{
    TCGArg args[2];
    args[0] = GET_TCGV_I64(a);
    args[1] = GET_TCGV_I64(b);
    tcg_gen_callN(&tcg_ctx, func,
                  TCG_CALL_NO_READ_GLOBALS | TCG_CALL_NO_SIDE_EFFECTS,
                  sizemask, GET_TCGV_I64(ret), 2, args);
}

/* 32 bit ops */

static inline void tcg_gen_ld8u_i32(TCGv_i32 ret, TCGv_ptr arg2, tcg_target_long offset)
{
    tcg_gen_ldst_op_i32(INDEX_op_ld8u_i32, ret, arg2, offset);
}

static inline void tcg_gen_ld8s_i32(TCGv_i32 ret, TCGv_ptr arg2, tcg_target_long offset)
{
    tcg_gen_ldst_op_i32(INDEX_op_ld8s_i32, ret, arg2, offset);
}

static inline void tcg_gen_ld16u_i32(TCGv_i32 ret, TCGv_ptr arg2, tcg_target_long offset)
{
    tcg_gen_ldst_op_i32(INDEX_op_ld16u_i32, ret, arg2, offset);
}

static inline void tcg_gen_ld16s_i32(TCGv_i32 ret, TCGv_ptr arg2, tcg_target_long offset)
{
    tcg_gen_ldst_op_i32(INDEX_op_ld16s_i32, ret, arg2, offset);
}

static inline void tcg_gen_ld_i32(TCGv_i32 ret, TCGv_ptr arg2, tcg_target_long offset)
{
    tcg_gen_ldst_op_i32(INDEX_op_ld_i32, ret, arg2, offset);
}

static inline void tcg_gen_st8_i32(TCGv_i32 arg1, TCGv_ptr arg2, tcg_target_long offset)
{
    tcg_gen_ldst_op_i32(INDEX_op_st8_i32, arg1, arg2, offset);
}

static inline void tcg_gen_st16_i32(TCGv_i32 arg1, TCGv_ptr arg2, tcg_target_long offset)
{
    tcg_gen_ldst_op_i32(INDEX_op_st16_i32, arg1, arg2, offset);
}

static inline void tcg_gen_st_i32(TCGv_i32 arg1, TCGv_ptr arg2, tcg_target_long offset)
{
    tcg_gen_ldst_op_i32(INDEX_op_st_i32, arg1, arg2, offset);
}

static inline void tcg_gen_add_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    tcg_gen_op3_i32(INDEX_op_add_i32, ret, arg1, arg2);
}

static inline void tcg_gen_addi_i32(TCGv_i32 ret, TCGv_i32 arg1, int32_t arg2)
{
    /* some cases can be optimized here */
    if (arg2 == 0) {
        tcg_gen_mov_i32(ret, arg1);
    } else {
        TCGv_i32 t0 = tcg_const_i32(arg2);
        tcg_gen_add_i32(ret, arg1, t0);
        tcg_temp_free_i32(t0);
    }
}

static inline void tcg_gen_sub_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    tcg_gen_op3_i32(INDEX_op_sub_i32, ret, arg1, arg2);
}

static inline void tcg_gen_subfi_i32(TCGv_i32 ret, int32_t arg1, TCGv_i32 arg2)
{
    TCGv_i32 t0 = tcg_const_i32(arg1);
    tcg_gen_sub_i32(ret, t0, arg2);
    tcg_temp_free_i32(t0);
}

static inline void tcg_gen_subi_i32(TCGv_i32 ret, TCGv_i32 arg1, int32_t arg2)
{
    /* some cases can be optimized here */
    if (arg2 == 0) {
        tcg_gen_mov_i32(ret, arg1);
    } else {
        TCGv_i32 t0 = tcg_const_i32(arg2);
        tcg_gen_sub_i32(ret, arg1, t0);
        tcg_temp_free_i32(t0);
    }
}

static inline void tcg_gen_and_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    if (TCGV_EQUAL_I32(arg1, arg2)) {
        tcg_gen_mov_i32(ret, arg1);
    } else {
        tcg_gen_op3_i32(INDEX_op_and_i32, ret, arg1, arg2);
    }
}

static inline void tcg_gen_andi_i32(TCGv_i32 ret, TCGv_i32 arg1, uint32_t arg2)
{
    TCGv_i32 t0;
    /* Some cases can be optimized here.  */
    switch (arg2) {
    case 0:
        tcg_gen_movi_i32(ret, 0);
        return;
    case 0xffffffffu:
        tcg_gen_mov_i32(ret, arg1);
        return;
    case 0xffu:
        /* Don't recurse with tcg_gen_ext8u_i32.  */
        if (TCG_TARGET_HAS_ext8u_i32) {
            tcg_gen_op2_i32(INDEX_op_ext8u_i32, ret, arg1);
            return;
        }
        break;
    case 0xffffu:
        if (TCG_TARGET_HAS_ext16u_i32) {
            tcg_gen_op2_i32(INDEX_op_ext16u_i32, ret, arg1);
            return;
        }
        break;
    }
    t0 = tcg_const_i32(arg2);
    tcg_gen_and_i32(ret, arg1, t0);
    tcg_temp_free_i32(t0);
}

static inline void tcg_gen_or_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    if (TCGV_EQUAL_I32(arg1, arg2)) {
        tcg_gen_mov_i32(ret, arg1);
    } else {
        tcg_gen_op3_i32(INDEX_op_or_i32, ret, arg1, arg2);
    }
}

static inline void tcg_gen_ori_i32(TCGv_i32 ret, TCGv_i32 arg1, int32_t arg2)
{
    /* Some cases can be optimized here.  */
    if (arg2 == -1) {
        tcg_gen_movi_i32(ret, -1);
    } else if (arg2 == 0) {
        tcg_gen_mov_i32(ret, arg1);
    } else {
        TCGv_i32 t0 = tcg_const_i32(arg2);
        tcg_gen_or_i32(ret, arg1, t0);
        tcg_temp_free_i32(t0);
    }
}

static inline void tcg_gen_xor_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    if (TCGV_EQUAL_I32(arg1, arg2)) {
        tcg_gen_movi_i32(ret, 0);
    } else {
        tcg_gen_op3_i32(INDEX_op_xor_i32, ret, arg1, arg2);
    }
}

static inline void tcg_gen_xori_i32(TCGv_i32 ret, TCGv_i32 arg1, int32_t arg2)
{
    /* Some cases can be optimized here.  */
    if (arg2 == 0) {
        tcg_gen_mov_i32(ret, arg1);
    } else if (arg2 == -1 && TCG_TARGET_HAS_not_i32) {
        /* Don't recurse with tcg_gen_not_i32.  */
        tcg_gen_op2_i32(INDEX_op_not_i32, ret, arg1);
    } else {
        TCGv_i32 t0 = tcg_const_i32(arg2);
        tcg_gen_xor_i32(ret, arg1, t0);
        tcg_temp_free_i32(t0);
    }
}

static inline void tcg_gen_shl_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    tcg_gen_op3_i32(INDEX_op_shl_i32, ret, arg1, arg2);
}

static inline void tcg_gen_shli_i32(TCGv_i32 ret, TCGv_i32 arg1, int32_t arg2)
{
    if (arg2 == 0) {
        tcg_gen_mov_i32(ret, arg1);
    } else {
        TCGv_i32 t0 = tcg_const_i32(arg2);
        tcg_gen_shl_i32(ret, arg1, t0);
        tcg_temp_free_i32(t0);
    }
}

static inline void tcg_gen_shr_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    tcg_gen_op3_i32(INDEX_op_shr_i32, ret, arg1, arg2);
}

static inline void tcg_gen_shri_i32(TCGv_i32 ret, TCGv_i32 arg1, int32_t arg2)
{
    if (arg2 == 0) {
        tcg_gen_mov_i32(ret, arg1);
    } else {
        TCGv_i32 t0 = tcg_const_i32(arg2);
        tcg_gen_shr_i32(ret, arg1, t0);
        tcg_temp_free_i32(t0);
    }
}

static inline void tcg_gen_sar_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    tcg_gen_op3_i32(INDEX_op_sar_i32, ret, arg1, arg2);
}

static inline void tcg_gen_sari_i32(TCGv_i32 ret, TCGv_i32 arg1, int32_t arg2)
{
    if (arg2 == 0) {
        tcg_gen_mov_i32(ret, arg1);
    } else {
        TCGv_i32 t0 = tcg_const_i32(arg2);
        tcg_gen_sar_i32(ret, arg1, t0);
        tcg_temp_free_i32(t0);
    }
}

static inline void tcg_gen_brcond_i32(TCGCond cond, TCGv_i32 arg1,
                                      TCGv_i32 arg2, int label_index)
{
    if (cond == TCG_COND_ALWAYS) {
        tcg_gen_br(label_index);
    } else if (cond != TCG_COND_NEVER) {
        tcg_gen_op4ii_i32(INDEX_op_brcond_i32, arg1, arg2, cond, label_index);
    }
}

static inline void tcg_gen_brcondi_i32(TCGCond cond, TCGv_i32 arg1,
                                       int32_t arg2, int label_index)
{
    if (cond == TCG_COND_ALWAYS) {
        tcg_gen_br(label_index);
    } else if (cond != TCG_COND_NEVER) {
        TCGv_i32 t0 = tcg_const_i32(arg2);
        tcg_gen_brcond_i32(cond, arg1, t0, label_index);
        tcg_temp_free_i32(t0);
    }
}

static inline void tcg_gen_setcond_i32(TCGCond cond, TCGv_i32 ret,
                                       TCGv_i32 arg1, TCGv_i32 arg2)
{
    if (cond == TCG_COND_ALWAYS) {
        tcg_gen_movi_i32(ret, 1);
    } else if (cond == TCG_COND_NEVER) {
        tcg_gen_movi_i32(ret, 0);
    } else {
        tcg_gen_op4i_i32(INDEX_op_setcond_i32, ret, arg1, arg2, cond);
    }
}

static inline void tcg_gen_setcondi_i32(TCGCond cond, TCGv_i32 ret,
                                        TCGv_i32 arg1, int32_t arg2)
{
    if (cond == TCG_COND_ALWAYS) {
        tcg_gen_movi_i32(ret, 1);
    } else if (cond == TCG_COND_NEVER) {
        tcg_gen_movi_i32(ret, 0);
    } else {
        TCGv_i32 t0 = tcg_const_i32(arg2);
        tcg_gen_setcond_i32(cond, ret, arg1, t0);
        tcg_temp_free_i32(t0);
    }
}

static inline void tcg_gen_mul_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    tcg_gen_op3_i32(INDEX_op_mul_i32, ret, arg1, arg2);
}

static inline void tcg_gen_muli_i32(TCGv_i32 ret, TCGv_i32 arg1, int32_t arg2)
{
    TCGv_i32 t0 = tcg_const_i32(arg2);
    tcg_gen_mul_i32(ret, arg1, t0);
    tcg_temp_free_i32(t0);
}

static inline void tcg_gen_div_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    if (TCG_TARGET_HAS_div_i32) {
        tcg_gen_op3_i32(INDEX_op_div_i32, ret, arg1, arg2);
    } else if (TCG_TARGET_HAS_div2_i32) {
        TCGv_i32 t0 = tcg_temp_new_i32();
        tcg_gen_sari_i32(t0, arg1, 31);
        tcg_gen_op5_i32(INDEX_op_div2_i32, ret, t0, arg1, t0, arg2);
        tcg_temp_free_i32(t0);
    } else {
        int sizemask = 0;
        /* Return value and both arguments are 32-bit and signed.  */
        sizemask |= tcg_gen_sizemask(0, 0, 1);
        sizemask |= tcg_gen_sizemask(1, 0, 1);
        sizemask |= tcg_gen_sizemask(2, 0, 1);
        tcg_gen_helper32(helper_div_i32, sizemask, ret, arg1, arg2);
    }
}

static inline void tcg_gen_rem_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    if (TCG_TARGET_HAS_rem_i32) {
        tcg_gen_op3_i32(INDEX_op_rem_i32, ret, arg1, arg2);
    } else if (TCG_TARGET_HAS_div_i32) {
        TCGv_i32 t0 = tcg_temp_new_i32();
        tcg_gen_op3_i32(INDEX_op_div_i32, t0, arg1, arg2);
        tcg_gen_mul_i32(t0, t0, arg2);
        tcg_gen_sub_i32(ret, arg1, t0);
        tcg_temp_free_i32(t0);
    } else if (TCG_TARGET_HAS_div2_i32) {
        TCGv_i32 t0 = tcg_temp_new_i32();
        tcg_gen_sari_i32(t0, arg1, 31);
        tcg_gen_op5_i32(INDEX_op_div2_i32, t0, ret, arg1, t0, arg2);
        tcg_temp_free_i32(t0);
    } else {
        int sizemask = 0;
        /* Return value and both arguments are 32-bit and signed.  */
        sizemask |= tcg_gen_sizemask(0, 0, 1);
        sizemask |= tcg_gen_sizemask(1, 0, 1);
        sizemask |= tcg_gen_sizemask(2, 0, 1);
        tcg_gen_helper32(helper_rem_i32, sizemask, ret, arg1, arg2);
    }
}

static inline void tcg_gen_divu_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    if (TCG_TARGET_HAS_div_i32) {
        tcg_gen_op3_i32(INDEX_op_divu_i32, ret, arg1, arg2);
    } else if (TCG_TARGET_HAS_div2_i32) {
        TCGv_i32 t0 = tcg_temp_new_i32();
        tcg_gen_movi_i32(t0, 0);
        tcg_gen_op5_i32(INDEX_op_divu2_i32, ret, t0, arg1, t0, arg2);
        tcg_temp_free_i32(t0);
    } else {
        int sizemask = 0;
        /* Return value and both arguments are 32-bit and unsigned.  */
        sizemask |= tcg_gen_sizemask(0, 0, 0);
        sizemask |= tcg_gen_sizemask(1, 0, 0);
        sizemask |= tcg_gen_sizemask(2, 0, 0);
        tcg_gen_helper32(helper_divu_i32, sizemask, ret, arg1, arg2);
    }
}

static inline void tcg_gen_remu_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    if (TCG_TARGET_HAS_rem_i32) {
        tcg_gen_op3_i32(INDEX_op_remu_i32, ret, arg1, arg2);
    } else if (TCG_TARGET_HAS_div_i32) {
        TCGv_i32 t0 = tcg_temp_new_i32();
        tcg_gen_op3_i32(INDEX_op_divu_i32, t0, arg1, arg2);
        tcg_gen_mul_i32(t0, t0, arg2);
        tcg_gen_sub_i32(ret, arg1, t0);
        tcg_temp_free_i32(t0);
    } else if (TCG_TARGET_HAS_div2_i32) {
        TCGv_i32 t0 = tcg_temp_new_i32();
        tcg_gen_movi_i32(t0, 0);
        tcg_gen_op5_i32(INDEX_op_divu2_i32, t0, ret, arg1, t0, arg2);
        tcg_temp_free_i32(t0);
    } else {
        int sizemask = 0;
        /* Return value and both arguments are 32-bit and unsigned.  */
        sizemask |= tcg_gen_sizemask(0, 0, 0);
        sizemask |= tcg_gen_sizemask(1, 0, 0);
        sizemask |= tcg_gen_sizemask(2, 0, 0);
        tcg_gen_helper32(helper_remu_i32, sizemask, ret, arg1, arg2);
    }
}

#if TCG_TARGET_REG_BITS == 32

static inline void tcg_gen_mov_i64(TCGv_i64 ret, TCGv_i64 arg)
{
    if (!TCGV_EQUAL_I64(ret, arg)) {
        tcg_gen_mov_i32(TCGV_LOW(ret), TCGV_LOW(arg));
        tcg_gen_mov_i32(TCGV_HIGH(ret), TCGV_HIGH(arg));
    }
}

static inline void tcg_gen_movi_i64(TCGv_i64 ret, int64_t arg)
{
    tcg_gen_movi_i32(TCGV_LOW(ret), arg);
    tcg_gen_movi_i32(TCGV_HIGH(ret), arg >> 32);
}

static inline void tcg_gen_ld8u_i64(TCGv_i64 ret, TCGv_ptr arg2,
                                    tcg_target_long offset)
{
    tcg_gen_ld8u_i32(TCGV_LOW(ret), arg2, offset);
    tcg_gen_movi_i32(TCGV_HIGH(ret), 0);
}

static inline void tcg_gen_ld8s_i64(TCGv_i64 ret, TCGv_ptr arg2,
                                    tcg_target_long offset)
{
    tcg_gen_ld8s_i32(TCGV_LOW(ret), arg2, offset);
    tcg_gen_sari_i32(TCGV_HIGH(ret), TCGV_HIGH(ret), 31);
}

static inline void tcg_gen_ld16u_i64(TCGv_i64 ret, TCGv_ptr arg2,
                                     tcg_target_long offset)
{
    tcg_gen_ld16u_i32(TCGV_LOW(ret), arg2, offset);
    tcg_gen_movi_i32(TCGV_HIGH(ret), 0);
}

static inline void tcg_gen_ld16s_i64(TCGv_i64 ret, TCGv_ptr arg2,
                                     tcg_target_long offset)
{
    tcg_gen_ld16s_i32(TCGV_LOW(ret), arg2, offset);
    tcg_gen_sari_i32(TCGV_HIGH(ret), TCGV_LOW(ret), 31);
}

static inline void tcg_gen_ld32u_i64(TCGv_i64 ret, TCGv_ptr arg2,
                                     tcg_target_long offset)
{
    tcg_gen_ld_i32(TCGV_LOW(ret), arg2, offset);
    tcg_gen_movi_i32(TCGV_HIGH(ret), 0);
}

static inline void tcg_gen_ld32s_i64(TCGv_i64 ret, TCGv_ptr arg2,
                                     tcg_target_long offset)
{
    tcg_gen_ld_i32(TCGV_LOW(ret), arg2, offset);
    tcg_gen_sari_i32(TCGV_HIGH(ret), TCGV_LOW(ret), 31);
}

static inline void tcg_gen_ld_i64(TCGv_i64 ret, TCGv_ptr arg2,
                                  tcg_target_long offset)
{
    /* since arg2 and ret have different types, they cannot be the
       same temporary */
#ifdef HOST_WORDS_BIGENDIAN
    tcg_gen_ld_i32(TCGV_HIGH(ret), arg2, offset);
    tcg_gen_ld_i32(TCGV_LOW(ret), arg2, offset + 4);
#else
    tcg_gen_ld_i32(TCGV_LOW(ret), arg2, offset);
    tcg_gen_ld_i32(TCGV_HIGH(ret), arg2, offset + 4);
#endif
}

static inline void tcg_gen_st8_i64(TCGv_i64 arg1, TCGv_ptr arg2,
                                   tcg_target_long offset)
{
    tcg_gen_st8_i32(TCGV_LOW(arg1), arg2, offset);
}

static inline void tcg_gen_st16_i64(TCGv_i64 arg1, TCGv_ptr arg2,
                                    tcg_target_long offset)
{
    tcg_gen_st16_i32(TCGV_LOW(arg1), arg2, offset);
}

static inline void tcg_gen_st32_i64(TCGv_i64 arg1, TCGv_ptr arg2,
                                    tcg_target_long offset)
{
    tcg_gen_st_i32(TCGV_LOW(arg1), arg2, offset);
}

static inline void tcg_gen_st_i64(TCGv_i64 arg1, TCGv_ptr arg2,
                                  tcg_target_long offset)
{
#ifdef HOST_WORDS_BIGENDIAN
    tcg_gen_st_i32(TCGV_HIGH(arg1), arg2, offset);
    tcg_gen_st_i32(TCGV_LOW(arg1), arg2, offset + 4);
#else
    tcg_gen_st_i32(TCGV_LOW(arg1), arg2, offset);
    tcg_gen_st_i32(TCGV_HIGH(arg1), arg2, offset + 4);
#endif
}

static inline void tcg_gen_add_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    tcg_gen_op6_i32(INDEX_op_add2_i32, TCGV_LOW(ret), TCGV_HIGH(ret),
                    TCGV_LOW(arg1), TCGV_HIGH(arg1), TCGV_LOW(arg2),
                    TCGV_HIGH(arg2));
    /* Allow the optimizer room to replace add2 with two moves.  */
    tcg_gen_op0(INDEX_op_nop);
}

static inline void tcg_gen_sub_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    tcg_gen_op6_i32(INDEX_op_sub2_i32, TCGV_LOW(ret), TCGV_HIGH(ret),
                    TCGV_LOW(arg1), TCGV_HIGH(arg1), TCGV_LOW(arg2),
                    TCGV_HIGH(arg2));
    /* Allow the optimizer room to replace sub2 with two moves.  */
    tcg_gen_op0(INDEX_op_nop);
}

static inline void tcg_gen_and_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    tcg_gen_and_i32(TCGV_LOW(ret), TCGV_LOW(arg1), TCGV_LOW(arg2));
    tcg_gen_and_i32(TCGV_HIGH(ret), TCGV_HIGH(arg1), TCGV_HIGH(arg2));
}

static inline void tcg_gen_andi_i64(TCGv_i64 ret, TCGv_i64 arg1, int64_t arg2)
{
    tcg_gen_andi_i32(TCGV_LOW(ret), TCGV_LOW(arg1), arg2);
    tcg_gen_andi_i32(TCGV_HIGH(ret), TCGV_HIGH(arg1), arg2 >> 32);
}

static inline void tcg_gen_or_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    tcg_gen_or_i32(TCGV_LOW(ret), TCGV_LOW(arg1), TCGV_LOW(arg2));
    tcg_gen_or_i32(TCGV_HIGH(ret), TCGV_HIGH(arg1), TCGV_HIGH(arg2));
}

static inline void tcg_gen_ori_i64(TCGv_i64 ret, TCGv_i64 arg1, int64_t arg2)
{
    tcg_gen_ori_i32(TCGV_LOW(ret), TCGV_LOW(arg1), arg2);
    tcg_gen_ori_i32(TCGV_HIGH(ret), TCGV_HIGH(arg1), arg2 >> 32);
}

static inline void tcg_gen_xor_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    tcg_gen_xor_i32(TCGV_LOW(ret), TCGV_LOW(arg1), TCGV_LOW(arg2));
    tcg_gen_xor_i32(TCGV_HIGH(ret), TCGV_HIGH(arg1), TCGV_HIGH(arg2));
}

static inline void tcg_gen_xori_i64(TCGv_i64 ret, TCGv_i64 arg1, int64_t arg2)
{
    tcg_gen_xori_i32(TCGV_LOW(ret), TCGV_LOW(arg1), arg2);
    tcg_gen_xori_i32(TCGV_HIGH(ret), TCGV_HIGH(arg1), arg2 >> 32);
}

/* XXX: use generic code when basic block handling is OK or CPU
   specific code (x86) */
static inline void tcg_gen_shl_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    int sizemask = 0;
    /* Return value and both arguments are 64-bit and signed.  */
    sizemask |= tcg_gen_sizemask(0, 1, 1);
    sizemask |= tcg_gen_sizemask(1, 1, 1);
    sizemask |= tcg_gen_sizemask(2, 1, 1);

    tcg_gen_helper64(helper_shl_i64, sizemask, ret, arg1, arg2);
}

static inline void tcg_gen_shli_i64(TCGv_i64 ret, TCGv_i64 arg1, int64_t arg2)
{
    tcg_gen_shifti_i64(ret, arg1, arg2, 0, 0);
}

static inline void tcg_gen_shr_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    int sizemask = 0;
    /* Return value and both arguments are 64-bit and signed.  */
    sizemask |= tcg_gen_sizemask(0, 1, 1);
    sizemask |= tcg_gen_sizemask(1, 1, 1);
    sizemask |= tcg_gen_sizemask(2, 1, 1);

    tcg_gen_helper64(helper_shr_i64, sizemask, ret, arg1, arg2);
}

static inline void tcg_gen_shri_i64(TCGv_i64 ret, TCGv_i64 arg1, int64_t arg2)
{
    tcg_gen_shifti_i64(ret, arg1, arg2, 1, 0);
}

static inline void tcg_gen_sar_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    int sizemask = 0;
    /* Return value and both arguments are 64-bit and signed.  */
    sizemask |= tcg_gen_sizemask(0, 1, 1);
    sizemask |= tcg_gen_sizemask(1, 1, 1);
    sizemask |= tcg_gen_sizemask(2, 1, 1);

    tcg_gen_helper64(helper_sar_i64, sizemask, ret, arg1, arg2);
}

static inline void tcg_gen_sari_i64(TCGv_i64 ret, TCGv_i64 arg1, int64_t arg2)
{
    tcg_gen_shifti_i64(ret, arg1, arg2, 1, 1);
}

static inline void tcg_gen_brcond_i64(TCGCond cond, TCGv_i64 arg1,
                                      TCGv_i64 arg2, int label_index)
{
    if (cond == TCG_COND_ALWAYS) {
        tcg_gen_br(label_index);
    } else if (cond != TCG_COND_NEVER) {
        tcg_gen_op6ii_i32(INDEX_op_brcond2_i32,
                          TCGV_LOW(arg1), TCGV_HIGH(arg1), TCGV_LOW(arg2),
                          TCGV_HIGH(arg2), cond, label_index);
    }
}

static inline void tcg_gen_setcond_i64(TCGCond cond, TCGv_i64 ret,
                                       TCGv_i64 arg1, TCGv_i64 arg2)
{
    if (cond == TCG_COND_ALWAYS) {
        tcg_gen_movi_i32(TCGV_LOW(ret), 1);
    } else if (cond == TCG_COND_NEVER) {
        tcg_gen_movi_i32(TCGV_LOW(ret), 0);
    } else {
        tcg_gen_op6i_i32(INDEX_op_setcond2_i32, TCGV_LOW(ret),
                         TCGV_LOW(arg1), TCGV_HIGH(arg1),
                         TCGV_LOW(arg2), TCGV_HIGH(arg2), cond);
    }
    tcg_gen_movi_i32(TCGV_HIGH(ret), 0);
}

static inline void tcg_gen_mul_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    TCGv_i64 t0;
    TCGv_i32 t1;

    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i32();

    if (TCG_TARGET_HAS_mulu2_i32) {
        tcg_gen_op4_i32(INDEX_op_mulu2_i32, TCGV_LOW(t0), TCGV_HIGH(t0),
                        TCGV_LOW(arg1), TCGV_LOW(arg2));
        /* Allow the optimizer room to replace mulu2 with two moves.  */
        tcg_gen_op0(INDEX_op_nop);
    } else {
        tcg_debug_assert(TCG_TARGET_HAS_muluh_i32);
        tcg_gen_op3_i32(INDEX_op_mul_i32, TCGV_LOW(t0),
                        TCGV_LOW(arg1), TCGV_LOW(arg2));
        tcg_gen_op3_i32(INDEX_op_muluh_i32, TCGV_HIGH(t0),
                        TCGV_LOW(arg1), TCGV_LOW(arg2));
    }

    tcg_gen_mul_i32(t1, TCGV_LOW(arg1), TCGV_HIGH(arg2));
    tcg_gen_add_i32(TCGV_HIGH(t0), TCGV_HIGH(t0), t1);
    tcg_gen_mul_i32(t1, TCGV_HIGH(arg1), TCGV_LOW(arg2));
    tcg_gen_add_i32(TCGV_HIGH(t0), TCGV_HIGH(t0), t1);

    tcg_gen_mov_i64(ret, t0);
    tcg_temp_free_i64(t0);
    tcg_temp_free_i32(t1);
}

static inline void tcg_gen_div_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    int sizemask = 0;
    /* Return value and both arguments are 64-bit and signed.  */
    sizemask |= tcg_gen_sizemask(0, 1, 1);
    sizemask |= tcg_gen_sizemask(1, 1, 1);
    sizemask |= tcg_gen_sizemask(2, 1, 1);

    tcg_gen_helper64(helper_div_i64, sizemask, ret, arg1, arg2);
}

static inline void tcg_gen_rem_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    int sizemask = 0;
    /* Return value and both arguments are 64-bit and signed.  */
    sizemask |= tcg_gen_sizemask(0, 1, 1);
    sizemask |= tcg_gen_sizemask(1, 1, 1);
    sizemask |= tcg_gen_sizemask(2, 1, 1);

    tcg_gen_helper64(helper_rem_i64, sizemask, ret, arg1, arg2);
}

static inline void tcg_gen_divu_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    int sizemask = 0;
    /* Return value and both arguments are 64-bit and unsigned.  */
    sizemask |= tcg_gen_sizemask(0, 1, 0);
    sizemask |= tcg_gen_sizemask(1, 1, 0);
    sizemask |= tcg_gen_sizemask(2, 1, 0);

    tcg_gen_helper64(helper_divu_i64, sizemask, ret, arg1, arg2);
}

static inline void tcg_gen_remu_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    int sizemask = 0;
    /* Return value and both arguments are 64-bit and unsigned.  */
    sizemask |= tcg_gen_sizemask(0, 1, 0);
    sizemask |= tcg_gen_sizemask(1, 1, 0);
    sizemask |= tcg_gen_sizemask(2, 1, 0);

    tcg_gen_helper64(helper_remu_i64, sizemask, ret, arg1, arg2);
}

#else

static inline void tcg_gen_mov_i64(TCGv_i64 ret, TCGv_i64 arg)
{
    if (!TCGV_EQUAL_I64(ret, arg))
        tcg_gen_op2_i64(INDEX_op_mov_i64, ret, arg);
}

static inline void tcg_gen_movi_i64(TCGv_i64 ret, int64_t arg)
{
    tcg_gen_op2i_i64(INDEX_op_movi_i64, ret, arg);
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

static inline void tcg_gen_ld_i64(TCGv_i64 ret, TCGv_ptr arg2, tcg_target_long offset)
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

static inline void tcg_gen_st_i64(TCGv_i64 arg1, TCGv_ptr arg2, tcg_target_long offset)
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
    if (TCGV_EQUAL_I64(arg1, arg2)) {
        tcg_gen_mov_i64(ret, arg1);
    } else {
        tcg_gen_op3_i64(INDEX_op_and_i64, ret, arg1, arg2);
    }
}

static inline void tcg_gen_andi_i64(TCGv_i64 ret, TCGv_i64 arg1, uint64_t arg2)
{
    TCGv_i64 t0;
    /* Some cases can be optimized here.  */
    switch (arg2) {
    case 0:
        tcg_gen_movi_i64(ret, 0);
        return;
    case 0xffffffffffffffffull:
        tcg_gen_mov_i64(ret, arg1);
        return;
    case 0xffull:
        /* Don't recurse with tcg_gen_ext8u_i32.  */
        if (TCG_TARGET_HAS_ext8u_i64) {
            tcg_gen_op2_i64(INDEX_op_ext8u_i64, ret, arg1);
            return;
        }
        break;
    case 0xffffu:
        if (TCG_TARGET_HAS_ext16u_i64) {
            tcg_gen_op2_i64(INDEX_op_ext16u_i64, ret, arg1);
            return;
        }
        break;
    case 0xffffffffull:
        if (TCG_TARGET_HAS_ext32u_i64) {
            tcg_gen_op2_i64(INDEX_op_ext32u_i64, ret, arg1);
            return;
        }
        break;
    }
    t0 = tcg_const_i64(arg2);
    tcg_gen_and_i64(ret, arg1, t0);
    tcg_temp_free_i64(t0);
}

static inline void tcg_gen_or_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    if (TCGV_EQUAL_I64(arg1, arg2)) {
        tcg_gen_mov_i64(ret, arg1);
    } else {
        tcg_gen_op3_i64(INDEX_op_or_i64, ret, arg1, arg2);
    }
}

static inline void tcg_gen_ori_i64(TCGv_i64 ret, TCGv_i64 arg1, int64_t arg2)
{
    /* Some cases can be optimized here.  */
    if (arg2 == -1) {
        tcg_gen_movi_i64(ret, -1);
    } else if (arg2 == 0) {
        tcg_gen_mov_i64(ret, arg1);
    } else {
        TCGv_i64 t0 = tcg_const_i64(arg2);
        tcg_gen_or_i64(ret, arg1, t0);
        tcg_temp_free_i64(t0);
    }
}

static inline void tcg_gen_xor_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    if (TCGV_EQUAL_I64(arg1, arg2)) {
        tcg_gen_movi_i64(ret, 0);
    } else {
        tcg_gen_op3_i64(INDEX_op_xor_i64, ret, arg1, arg2);
    }
}

static inline void tcg_gen_xori_i64(TCGv_i64 ret, TCGv_i64 arg1, int64_t arg2)
{
    /* Some cases can be optimized here.  */
    if (arg2 == 0) {
        tcg_gen_mov_i64(ret, arg1);
    } else if (arg2 == -1 && TCG_TARGET_HAS_not_i64) {
        /* Don't recurse with tcg_gen_not_i64.  */
        tcg_gen_op2_i64(INDEX_op_not_i64, ret, arg1);
    } else {
        TCGv_i64 t0 = tcg_const_i64(arg2);
        tcg_gen_xor_i64(ret, arg1, t0);
        tcg_temp_free_i64(t0);
    }
}

static inline void tcg_gen_shl_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    tcg_gen_op3_i64(INDEX_op_shl_i64, ret, arg1, arg2);
}

static inline void tcg_gen_shli_i64(TCGv_i64 ret, TCGv_i64 arg1, int64_t arg2)
{
    if (arg2 == 0) {
        tcg_gen_mov_i64(ret, arg1);
    } else {
        TCGv_i64 t0 = tcg_const_i64(arg2);
        tcg_gen_shl_i64(ret, arg1, t0);
        tcg_temp_free_i64(t0);
    }
}

static inline void tcg_gen_shr_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    tcg_gen_op3_i64(INDEX_op_shr_i64, ret, arg1, arg2);
}

static inline void tcg_gen_shri_i64(TCGv_i64 ret, TCGv_i64 arg1, int64_t arg2)
{
    if (arg2 == 0) {
        tcg_gen_mov_i64(ret, arg1);
    } else {
        TCGv_i64 t0 = tcg_const_i64(arg2);
        tcg_gen_shr_i64(ret, arg1, t0);
        tcg_temp_free_i64(t0);
    }
}

static inline void tcg_gen_sar_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    tcg_gen_op3_i64(INDEX_op_sar_i64, ret, arg1, arg2);
}

static inline void tcg_gen_sari_i64(TCGv_i64 ret, TCGv_i64 arg1, int64_t arg2)
{
    if (arg2 == 0) {
        tcg_gen_mov_i64(ret, arg1);
    } else {
        TCGv_i64 t0 = tcg_const_i64(arg2);
        tcg_gen_sar_i64(ret, arg1, t0);
        tcg_temp_free_i64(t0);
    }
}

static inline void tcg_gen_brcond_i64(TCGCond cond, TCGv_i64 arg1,
                                      TCGv_i64 arg2, int label_index)
{
    if (cond == TCG_COND_ALWAYS) {
        tcg_gen_br(label_index);
    } else if (cond != TCG_COND_NEVER) {
        tcg_gen_op4ii_i64(INDEX_op_brcond_i64, arg1, arg2, cond, label_index);
    }
}

static inline void tcg_gen_setcond_i64(TCGCond cond, TCGv_i64 ret,
                                       TCGv_i64 arg1, TCGv_i64 arg2)
{
    if (cond == TCG_COND_ALWAYS) {
        tcg_gen_movi_i64(ret, 1);
    } else if (cond == TCG_COND_NEVER) {
        tcg_gen_movi_i64(ret, 0);
    } else {
        tcg_gen_op4i_i64(INDEX_op_setcond_i64, ret, arg1, arg2, cond);
    }
}

static inline void tcg_gen_mul_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    tcg_gen_op3_i64(INDEX_op_mul_i64, ret, arg1, arg2);
}

static inline void tcg_gen_div_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    if (TCG_TARGET_HAS_div_i64) {
        tcg_gen_op3_i64(INDEX_op_div_i64, ret, arg1, arg2);
    } else if (TCG_TARGET_HAS_div2_i64) {
        TCGv_i64 t0 = tcg_temp_new_i64();
        tcg_gen_sari_i64(t0, arg1, 63);
        tcg_gen_op5_i64(INDEX_op_div2_i64, ret, t0, arg1, t0, arg2);
        tcg_temp_free_i64(t0);
    } else {
        int sizemask = 0;
        /* Return value and both arguments are 64-bit and signed.  */
        sizemask |= tcg_gen_sizemask(0, 1, 1);
        sizemask |= tcg_gen_sizemask(1, 1, 1);
        sizemask |= tcg_gen_sizemask(2, 1, 1);
        tcg_gen_helper64(helper_div_i64, sizemask, ret, arg1, arg2);
    }
}

static inline void tcg_gen_rem_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    if (TCG_TARGET_HAS_rem_i64) {
        tcg_gen_op3_i64(INDEX_op_rem_i64, ret, arg1, arg2);
    } else if (TCG_TARGET_HAS_div_i64) {
        TCGv_i64 t0 = tcg_temp_new_i64();
        tcg_gen_op3_i64(INDEX_op_div_i64, t0, arg1, arg2);
        tcg_gen_mul_i64(t0, t0, arg2);
        tcg_gen_sub_i64(ret, arg1, t0);
        tcg_temp_free_i64(t0);
    } else if (TCG_TARGET_HAS_div2_i64) {
        TCGv_i64 t0 = tcg_temp_new_i64();
        tcg_gen_sari_i64(t0, arg1, 63);
        tcg_gen_op5_i64(INDEX_op_div2_i64, t0, ret, arg1, t0, arg2);
        tcg_temp_free_i64(t0);
    } else {
        int sizemask = 0;
        /* Return value and both arguments are 64-bit and signed.  */
        sizemask |= tcg_gen_sizemask(0, 1, 1);
        sizemask |= tcg_gen_sizemask(1, 1, 1);
        sizemask |= tcg_gen_sizemask(2, 1, 1);
        tcg_gen_helper64(helper_rem_i64, sizemask, ret, arg1, arg2);
    }
}

static inline void tcg_gen_divu_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    if (TCG_TARGET_HAS_div_i64) {
        tcg_gen_op3_i64(INDEX_op_divu_i64, ret, arg1, arg2);
    } else if (TCG_TARGET_HAS_div2_i64) {
        TCGv_i64 t0 = tcg_temp_new_i64();
        tcg_gen_movi_i64(t0, 0);
        tcg_gen_op5_i64(INDEX_op_divu2_i64, ret, t0, arg1, t0, arg2);
        tcg_temp_free_i64(t0);
    } else {
        int sizemask = 0;
        /* Return value and both arguments are 64-bit and unsigned.  */
        sizemask |= tcg_gen_sizemask(0, 1, 0);
        sizemask |= tcg_gen_sizemask(1, 1, 0);
        sizemask |= tcg_gen_sizemask(2, 1, 0);
        tcg_gen_helper64(helper_divu_i64, sizemask, ret, arg1, arg2);
    }
}

static inline void tcg_gen_remu_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    if (TCG_TARGET_HAS_rem_i64) {
        tcg_gen_op3_i64(INDEX_op_remu_i64, ret, arg1, arg2);
    } else if (TCG_TARGET_HAS_div_i64) {
        TCGv_i64 t0 = tcg_temp_new_i64();
        tcg_gen_op3_i64(INDEX_op_divu_i64, t0, arg1, arg2);
        tcg_gen_mul_i64(t0, t0, arg2);
        tcg_gen_sub_i64(ret, arg1, t0);
        tcg_temp_free_i64(t0);
    } else if (TCG_TARGET_HAS_div2_i64) {
        TCGv_i64 t0 = tcg_temp_new_i64();
        tcg_gen_movi_i64(t0, 0);
        tcg_gen_op5_i64(INDEX_op_divu2_i64, t0, ret, arg1, t0, arg2);
        tcg_temp_free_i64(t0);
    } else {
        int sizemask = 0;
        /* Return value and both arguments are 64-bit and unsigned.  */
        sizemask |= tcg_gen_sizemask(0, 1, 0);
        sizemask |= tcg_gen_sizemask(1, 1, 0);
        sizemask |= tcg_gen_sizemask(2, 1, 0);
        tcg_gen_helper64(helper_remu_i64, sizemask, ret, arg1, arg2);
    }
}
#endif /* TCG_TARGET_REG_BITS == 32 */

static inline void tcg_gen_addi_i64(TCGv_i64 ret, TCGv_i64 arg1, int64_t arg2)
{
    /* some cases can be optimized here */
    if (arg2 == 0) {
        tcg_gen_mov_i64(ret, arg1);
    } else {
        TCGv_i64 t0 = tcg_const_i64(arg2);
        tcg_gen_add_i64(ret, arg1, t0);
        tcg_temp_free_i64(t0);
    }
}

static inline void tcg_gen_subfi_i64(TCGv_i64 ret, int64_t arg1, TCGv_i64 arg2)
{
    TCGv_i64 t0 = tcg_const_i64(arg1);
    tcg_gen_sub_i64(ret, t0, arg2);
    tcg_temp_free_i64(t0);
}

static inline void tcg_gen_subi_i64(TCGv_i64 ret, TCGv_i64 arg1, int64_t arg2)
{
    /* some cases can be optimized here */
    if (arg2 == 0) {
        tcg_gen_mov_i64(ret, arg1);
    } else {
        TCGv_i64 t0 = tcg_const_i64(arg2);
        tcg_gen_sub_i64(ret, arg1, t0);
        tcg_temp_free_i64(t0);
    }
}
static inline void tcg_gen_brcondi_i64(TCGCond cond, TCGv_i64 arg1,
                                       int64_t arg2, int label_index)
{
    if (cond == TCG_COND_ALWAYS) {
        tcg_gen_br(label_index);
    } else if (cond != TCG_COND_NEVER) {
        TCGv_i64 t0 = tcg_const_i64(arg2);
        tcg_gen_brcond_i64(cond, arg1, t0, label_index);
        tcg_temp_free_i64(t0);
    }
}

static inline void tcg_gen_setcondi_i64(TCGCond cond, TCGv_i64 ret,
                                        TCGv_i64 arg1, int64_t arg2)
{
    TCGv_i64 t0 = tcg_const_i64(arg2);
    tcg_gen_setcond_i64(cond, ret, arg1, t0);
    tcg_temp_free_i64(t0);
}

static inline void tcg_gen_muli_i64(TCGv_i64 ret, TCGv_i64 arg1, int64_t arg2)
{
    TCGv_i64 t0 = tcg_const_i64(arg2);
    tcg_gen_mul_i64(ret, arg1, t0);
    tcg_temp_free_i64(t0);
}


/***************************************/
/* optional operations */

static inline void tcg_gen_ext8s_i32(TCGv_i32 ret, TCGv_i32 arg)
{
    if (TCG_TARGET_HAS_ext8s_i32) {
        tcg_gen_op2_i32(INDEX_op_ext8s_i32, ret, arg);
    } else {
        tcg_gen_shli_i32(ret, arg, 24);
        tcg_gen_sari_i32(ret, ret, 24);
    }
}

static inline void tcg_gen_ext16s_i32(TCGv_i32 ret, TCGv_i32 arg)
{
    if (TCG_TARGET_HAS_ext16s_i32) {
        tcg_gen_op2_i32(INDEX_op_ext16s_i32, ret, arg);
    } else {
        tcg_gen_shli_i32(ret, arg, 16);
        tcg_gen_sari_i32(ret, ret, 16);
    }
}

static inline void tcg_gen_ext8u_i32(TCGv_i32 ret, TCGv_i32 arg)
{
    if (TCG_TARGET_HAS_ext8u_i32) {
        tcg_gen_op2_i32(INDEX_op_ext8u_i32, ret, arg);
    } else {
        tcg_gen_andi_i32(ret, arg, 0xffu);
    }
}

static inline void tcg_gen_ext16u_i32(TCGv_i32 ret, TCGv_i32 arg)
{
    if (TCG_TARGET_HAS_ext16u_i32) {
        tcg_gen_op2_i32(INDEX_op_ext16u_i32, ret, arg);
    } else {
        tcg_gen_andi_i32(ret, arg, 0xffffu);
    }
}

/* Note: we assume the two high bytes are set to zero */
static inline void tcg_gen_bswap16_i32(TCGv_i32 ret, TCGv_i32 arg)
{
    if (TCG_TARGET_HAS_bswap16_i32) {
        tcg_gen_op2_i32(INDEX_op_bswap16_i32, ret, arg);
    } else {
        TCGv_i32 t0 = tcg_temp_new_i32();
    
        tcg_gen_ext8u_i32(t0, arg);
        tcg_gen_shli_i32(t0, t0, 8);
        tcg_gen_shri_i32(ret, arg, 8);
        tcg_gen_or_i32(ret, ret, t0);
        tcg_temp_free_i32(t0);
    }
}

static inline void tcg_gen_bswap32_i32(TCGv_i32 ret, TCGv_i32 arg)
{
    if (TCG_TARGET_HAS_bswap32_i32) {
        tcg_gen_op2_i32(INDEX_op_bswap32_i32, ret, arg);
    } else {
        TCGv_i32 t0, t1;
        t0 = tcg_temp_new_i32();
        t1 = tcg_temp_new_i32();
    
        tcg_gen_shli_i32(t0, arg, 24);
    
        tcg_gen_andi_i32(t1, arg, 0x0000ff00);
        tcg_gen_shli_i32(t1, t1, 8);
        tcg_gen_or_i32(t0, t0, t1);
    
        tcg_gen_shri_i32(t1, arg, 8);
        tcg_gen_andi_i32(t1, t1, 0x0000ff00);
        tcg_gen_or_i32(t0, t0, t1);
    
        tcg_gen_shri_i32(t1, arg, 24);
        tcg_gen_or_i32(ret, t0, t1);
        tcg_temp_free_i32(t0);
        tcg_temp_free_i32(t1);
    }
}

#if TCG_TARGET_REG_BITS == 32
static inline void tcg_gen_ext8s_i64(TCGv_i64 ret, TCGv_i64 arg)
{
    tcg_gen_ext8s_i32(TCGV_LOW(ret), TCGV_LOW(arg));
    tcg_gen_sari_i32(TCGV_HIGH(ret), TCGV_LOW(ret), 31);
}

static inline void tcg_gen_ext16s_i64(TCGv_i64 ret, TCGv_i64 arg)
{
    tcg_gen_ext16s_i32(TCGV_LOW(ret), TCGV_LOW(arg));
    tcg_gen_sari_i32(TCGV_HIGH(ret), TCGV_LOW(ret), 31);
}

static inline void tcg_gen_ext32s_i64(TCGv_i64 ret, TCGv_i64 arg)
{
    tcg_gen_mov_i32(TCGV_LOW(ret), TCGV_LOW(arg));
    tcg_gen_sari_i32(TCGV_HIGH(ret), TCGV_LOW(ret), 31);
}

static inline void tcg_gen_ext8u_i64(TCGv_i64 ret, TCGv_i64 arg)
{
    tcg_gen_ext8u_i32(TCGV_LOW(ret), TCGV_LOW(arg));
    tcg_gen_movi_i32(TCGV_HIGH(ret), 0);
}

static inline void tcg_gen_ext16u_i64(TCGv_i64 ret, TCGv_i64 arg)
{
    tcg_gen_ext16u_i32(TCGV_LOW(ret), TCGV_LOW(arg));
    tcg_gen_movi_i32(TCGV_HIGH(ret), 0);
}

static inline void tcg_gen_ext32u_i64(TCGv_i64 ret, TCGv_i64 arg)
{
    tcg_gen_mov_i32(TCGV_LOW(ret), TCGV_LOW(arg));
    tcg_gen_movi_i32(TCGV_HIGH(ret), 0);
}

static inline void tcg_gen_trunc_shr_i64_i32(TCGv_i32 ret, TCGv_i64 arg,
                                             unsigned int count)
{
    tcg_debug_assert(count < 64);
    if (count >= 32) {
        tcg_gen_shri_i32(ret, TCGV_HIGH(arg), count - 32);
    } else if (count == 0) {
        tcg_gen_mov_i32(ret, TCGV_LOW(arg));
    } else {
        TCGv_i64 t = tcg_temp_new_i64();
        tcg_gen_shri_i64(t, arg, count);
        tcg_gen_mov_i32(ret, TCGV_LOW(t));
        tcg_temp_free_i64(t);
    }
}

static inline void tcg_gen_extu_i32_i64(TCGv_i64 ret, TCGv_i32 arg)
{
    tcg_gen_mov_i32(TCGV_LOW(ret), arg);
    tcg_gen_movi_i32(TCGV_HIGH(ret), 0);
}

static inline void tcg_gen_ext_i32_i64(TCGv_i64 ret, TCGv_i32 arg)
{
    tcg_gen_mov_i32(TCGV_LOW(ret), arg);
    tcg_gen_sari_i32(TCGV_HIGH(ret), TCGV_LOW(ret), 31);
}

/* Note: we assume the six high bytes are set to zero */
static inline void tcg_gen_bswap16_i64(TCGv_i64 ret, TCGv_i64 arg)
{
    tcg_gen_mov_i32(TCGV_HIGH(ret), TCGV_HIGH(arg));
    tcg_gen_bswap16_i32(TCGV_LOW(ret), TCGV_LOW(arg));
}

/* Note: we assume the four high bytes are set to zero */
static inline void tcg_gen_bswap32_i64(TCGv_i64 ret, TCGv_i64 arg)
{
    tcg_gen_mov_i32(TCGV_HIGH(ret), TCGV_HIGH(arg));
    tcg_gen_bswap32_i32(TCGV_LOW(ret), TCGV_LOW(arg));
}

static inline void tcg_gen_bswap64_i64(TCGv_i64 ret, TCGv_i64 arg)
{
    TCGv_i32 t0, t1;
    t0 = tcg_temp_new_i32();
    t1 = tcg_temp_new_i32();

    tcg_gen_bswap32_i32(t0, TCGV_LOW(arg));
    tcg_gen_bswap32_i32(t1, TCGV_HIGH(arg));
    tcg_gen_mov_i32(TCGV_LOW(ret), t1);
    tcg_gen_mov_i32(TCGV_HIGH(ret), t0);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
}
#else

static inline void tcg_gen_ext8s_i64(TCGv_i64 ret, TCGv_i64 arg)
{
    if (TCG_TARGET_HAS_ext8s_i64) {
        tcg_gen_op2_i64(INDEX_op_ext8s_i64, ret, arg);
    } else {
        tcg_gen_shli_i64(ret, arg, 56);
        tcg_gen_sari_i64(ret, ret, 56);
    }
}

static inline void tcg_gen_ext16s_i64(TCGv_i64 ret, TCGv_i64 arg)
{
    if (TCG_TARGET_HAS_ext16s_i64) {
        tcg_gen_op2_i64(INDEX_op_ext16s_i64, ret, arg);
    } else {
        tcg_gen_shli_i64(ret, arg, 48);
        tcg_gen_sari_i64(ret, ret, 48);
    }
}

static inline void tcg_gen_ext32s_i64(TCGv_i64 ret, TCGv_i64 arg)
{
    if (TCG_TARGET_HAS_ext32s_i64) {
        tcg_gen_op2_i64(INDEX_op_ext32s_i64, ret, arg);
    } else {
        tcg_gen_shli_i64(ret, arg, 32);
        tcg_gen_sari_i64(ret, ret, 32);
    }
}

static inline void tcg_gen_ext8u_i64(TCGv_i64 ret, TCGv_i64 arg)
{
    if (TCG_TARGET_HAS_ext8u_i64) {
        tcg_gen_op2_i64(INDEX_op_ext8u_i64, ret, arg);
    } else {
        tcg_gen_andi_i64(ret, arg, 0xffu);
    }
}

static inline void tcg_gen_ext16u_i64(TCGv_i64 ret, TCGv_i64 arg)
{
    if (TCG_TARGET_HAS_ext16u_i64) {
        tcg_gen_op2_i64(INDEX_op_ext16u_i64, ret, arg);
    } else {
        tcg_gen_andi_i64(ret, arg, 0xffffu);
    }
}

static inline void tcg_gen_ext32u_i64(TCGv_i64 ret, TCGv_i64 arg)
{
    if (TCG_TARGET_HAS_ext32u_i64) {
        tcg_gen_op2_i64(INDEX_op_ext32u_i64, ret, arg);
    } else {
        tcg_gen_andi_i64(ret, arg, 0xffffffffu);
    }
}

static inline void tcg_gen_trunc_shr_i64_i32(TCGv_i32 ret, TCGv_i64 arg,
                                             unsigned int count)
{
    tcg_debug_assert(count < 64);
    if (TCG_TARGET_HAS_trunc_shr_i32) {
        tcg_gen_op3i_i32(INDEX_op_trunc_shr_i32, ret,
                         MAKE_TCGV_I32(GET_TCGV_I64(arg)), count);
    } else if (count == 0) {
        tcg_gen_mov_i32(ret, MAKE_TCGV_I32(GET_TCGV_I64(arg)));
    } else {
        TCGv_i64 t = tcg_temp_new_i64();
        tcg_gen_shri_i64(t, arg, count);
        tcg_gen_mov_i32(ret, MAKE_TCGV_I32(GET_TCGV_I64(t)));
        tcg_temp_free_i64(t);
    }
}

/* Note: we assume the target supports move between 32 and 64 bit
   registers */
static inline void tcg_gen_extu_i32_i64(TCGv_i64 ret, TCGv_i32 arg)
{
    tcg_gen_ext32u_i64(ret, MAKE_TCGV_I64(GET_TCGV_I32(arg)));
}

/* Note: we assume the target supports move between 32 and 64 bit
   registers */
static inline void tcg_gen_ext_i32_i64(TCGv_i64 ret, TCGv_i32 arg)
{
    tcg_gen_ext32s_i64(ret, MAKE_TCGV_I64(GET_TCGV_I32(arg)));
}

/* Note: we assume the six high bytes are set to zero */
static inline void tcg_gen_bswap16_i64(TCGv_i64 ret, TCGv_i64 arg)
{
    if (TCG_TARGET_HAS_bswap16_i64) {
        tcg_gen_op2_i64(INDEX_op_bswap16_i64, ret, arg);
    } else {
        TCGv_i64 t0 = tcg_temp_new_i64();

        tcg_gen_ext8u_i64(t0, arg);
        tcg_gen_shli_i64(t0, t0, 8);
        tcg_gen_shri_i64(ret, arg, 8);
        tcg_gen_or_i64(ret, ret, t0);
        tcg_temp_free_i64(t0);
    }
}

/* Note: we assume the four high bytes are set to zero */
static inline void tcg_gen_bswap32_i64(TCGv_i64 ret, TCGv_i64 arg)
{
    if (TCG_TARGET_HAS_bswap32_i64) {
        tcg_gen_op2_i64(INDEX_op_bswap32_i64, ret, arg);
    } else {
        TCGv_i64 t0, t1;
        t0 = tcg_temp_new_i64();
        t1 = tcg_temp_new_i64();

        tcg_gen_shli_i64(t0, arg, 24);
        tcg_gen_ext32u_i64(t0, t0);

        tcg_gen_andi_i64(t1, arg, 0x0000ff00);
        tcg_gen_shli_i64(t1, t1, 8);
        tcg_gen_or_i64(t0, t0, t1);

        tcg_gen_shri_i64(t1, arg, 8);
        tcg_gen_andi_i64(t1, t1, 0x0000ff00);
        tcg_gen_or_i64(t0, t0, t1);

        tcg_gen_shri_i64(t1, arg, 24);
        tcg_gen_or_i64(ret, t0, t1);
        tcg_temp_free_i64(t0);
        tcg_temp_free_i64(t1);
    }
}

static inline void tcg_gen_bswap64_i64(TCGv_i64 ret, TCGv_i64 arg)
{
    if (TCG_TARGET_HAS_bswap64_i64) {
        tcg_gen_op2_i64(INDEX_op_bswap64_i64, ret, arg);
    } else {
        TCGv_i64 t0 = tcg_temp_new_i64();
        TCGv_i64 t1 = tcg_temp_new_i64();
    
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
        tcg_temp_free_i64(t0);
        tcg_temp_free_i64(t1);
    }
}

#endif

static inline void tcg_gen_neg_i32(TCGv_i32 ret, TCGv_i32 arg)
{
    if (TCG_TARGET_HAS_neg_i32) {
        tcg_gen_op2_i32(INDEX_op_neg_i32, ret, arg);
    } else {
        TCGv_i32 t0 = tcg_const_i32(0);
        tcg_gen_sub_i32(ret, t0, arg);
        tcg_temp_free_i32(t0);
    }
}

static inline void tcg_gen_neg_i64(TCGv_i64 ret, TCGv_i64 arg)
{
    if (TCG_TARGET_HAS_neg_i64) {
        tcg_gen_op2_i64(INDEX_op_neg_i64, ret, arg);
    } else {
        TCGv_i64 t0 = tcg_const_i64(0);
        tcg_gen_sub_i64(ret, t0, arg);
        tcg_temp_free_i64(t0);
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

static inline void tcg_gen_not_i64(TCGv_i64 ret, TCGv_i64 arg)
{
#if TCG_TARGET_REG_BITS == 64
    if (TCG_TARGET_HAS_not_i64) {
        tcg_gen_op2_i64(INDEX_op_not_i64, ret, arg);
    } else {
        tcg_gen_xori_i64(ret, arg, -1);
    }
#else
    tcg_gen_not_i32(TCGV_LOW(ret), TCGV_LOW(arg));
    tcg_gen_not_i32(TCGV_HIGH(ret), TCGV_HIGH(arg));
#endif
}

static inline void tcg_gen_discard_i32(TCGv_i32 arg)
{
    tcg_gen_op1_i32(INDEX_op_discard, arg);
}

static inline void tcg_gen_discard_i64(TCGv_i64 arg)
{
#if TCG_TARGET_REG_BITS == 32
    tcg_gen_discard_i32(TCGV_LOW(arg));
    tcg_gen_discard_i32(TCGV_HIGH(arg));
#else
    tcg_gen_op1_i64(INDEX_op_discard, arg);
#endif
}

static inline void tcg_gen_andc_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    if (TCG_TARGET_HAS_andc_i32) {
        tcg_gen_op3_i32(INDEX_op_andc_i32, ret, arg1, arg2);
    } else {
        TCGv_i32 t0 = tcg_temp_new_i32();
        tcg_gen_not_i32(t0, arg2);
        tcg_gen_and_i32(ret, arg1, t0);
        tcg_temp_free_i32(t0);
    }
}

static inline void tcg_gen_andc_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
#if TCG_TARGET_REG_BITS == 64
    if (TCG_TARGET_HAS_andc_i64) {
        tcg_gen_op3_i64(INDEX_op_andc_i64, ret, arg1, arg2);
    } else {
        TCGv_i64 t0 = tcg_temp_new_i64();
        tcg_gen_not_i64(t0, arg2);
        tcg_gen_and_i64(ret, arg1, t0);
        tcg_temp_free_i64(t0);
    }
#else
    tcg_gen_andc_i32(TCGV_LOW(ret), TCGV_LOW(arg1), TCGV_LOW(arg2));
    tcg_gen_andc_i32(TCGV_HIGH(ret), TCGV_HIGH(arg1), TCGV_HIGH(arg2));
#endif
}

static inline void tcg_gen_eqv_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    if (TCG_TARGET_HAS_eqv_i32) {
        tcg_gen_op3_i32(INDEX_op_eqv_i32, ret, arg1, arg2);
    } else {
        tcg_gen_xor_i32(ret, arg1, arg2);
        tcg_gen_not_i32(ret, ret);
    }
}

static inline void tcg_gen_eqv_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
#if TCG_TARGET_REG_BITS == 64
    if (TCG_TARGET_HAS_eqv_i64) {
        tcg_gen_op3_i64(INDEX_op_eqv_i64, ret, arg1, arg2);
    } else {
        tcg_gen_xor_i64(ret, arg1, arg2);
        tcg_gen_not_i64(ret, ret);
    }
#else
    tcg_gen_eqv_i32(TCGV_LOW(ret), TCGV_LOW(arg1), TCGV_LOW(arg2));
    tcg_gen_eqv_i32(TCGV_HIGH(ret), TCGV_HIGH(arg1), TCGV_HIGH(arg2));
#endif
}

static inline void tcg_gen_nand_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    if (TCG_TARGET_HAS_nand_i32) {
        tcg_gen_op3_i32(INDEX_op_nand_i32, ret, arg1, arg2);
    } else {
        tcg_gen_and_i32(ret, arg1, arg2);
        tcg_gen_not_i32(ret, ret);
    }
}

static inline void tcg_gen_nand_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
#if TCG_TARGET_REG_BITS == 64
    if (TCG_TARGET_HAS_nand_i64) {
        tcg_gen_op3_i64(INDEX_op_nand_i64, ret, arg1, arg2);
    } else {
        tcg_gen_and_i64(ret, arg1, arg2);
        tcg_gen_not_i64(ret, ret);
    }
#else
    tcg_gen_nand_i32(TCGV_LOW(ret), TCGV_LOW(arg1), TCGV_LOW(arg2));
    tcg_gen_nand_i32(TCGV_HIGH(ret), TCGV_HIGH(arg1), TCGV_HIGH(arg2));
#endif
}

static inline void tcg_gen_nor_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    if (TCG_TARGET_HAS_nor_i32) {
        tcg_gen_op3_i32(INDEX_op_nor_i32, ret, arg1, arg2);
    } else {
        tcg_gen_or_i32(ret, arg1, arg2);
        tcg_gen_not_i32(ret, ret);
    }
}

static inline void tcg_gen_nor_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
#if TCG_TARGET_REG_BITS == 64
    if (TCG_TARGET_HAS_nor_i64) {
        tcg_gen_op3_i64(INDEX_op_nor_i64, ret, arg1, arg2);
    } else {
        tcg_gen_or_i64(ret, arg1, arg2);
        tcg_gen_not_i64(ret, ret);
    }
#else
    tcg_gen_nor_i32(TCGV_LOW(ret), TCGV_LOW(arg1), TCGV_LOW(arg2));
    tcg_gen_nor_i32(TCGV_HIGH(ret), TCGV_HIGH(arg1), TCGV_HIGH(arg2));
#endif
}

static inline void tcg_gen_orc_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    if (TCG_TARGET_HAS_orc_i32) {
        tcg_gen_op3_i32(INDEX_op_orc_i32, ret, arg1, arg2);
    } else {
        TCGv_i32 t0 = tcg_temp_new_i32();
        tcg_gen_not_i32(t0, arg2);
        tcg_gen_or_i32(ret, arg1, t0);
        tcg_temp_free_i32(t0);
    }
}

static inline void tcg_gen_orc_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
#if TCG_TARGET_REG_BITS == 64
    if (TCG_TARGET_HAS_orc_i64) {
        tcg_gen_op3_i64(INDEX_op_orc_i64, ret, arg1, arg2);
    } else {
        TCGv_i64 t0 = tcg_temp_new_i64();
        tcg_gen_not_i64(t0, arg2);
        tcg_gen_or_i64(ret, arg1, t0);
        tcg_temp_free_i64(t0);
    }
#else
    tcg_gen_orc_i32(TCGV_LOW(ret), TCGV_LOW(arg1), TCGV_LOW(arg2));
    tcg_gen_orc_i32(TCGV_HIGH(ret), TCGV_HIGH(arg1), TCGV_HIGH(arg2));
#endif
}

static inline void tcg_gen_rotl_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    if (TCG_TARGET_HAS_rot_i32) {
        tcg_gen_op3_i32(INDEX_op_rotl_i32, ret, arg1, arg2);
    } else {
        TCGv_i32 t0, t1;

        t0 = tcg_temp_new_i32();
        t1 = tcg_temp_new_i32();
        tcg_gen_shl_i32(t0, arg1, arg2);
        tcg_gen_subfi_i32(t1, 32, arg2);
        tcg_gen_shr_i32(t1, arg1, t1);
        tcg_gen_or_i32(ret, t0, t1);
        tcg_temp_free_i32(t0);
        tcg_temp_free_i32(t1);
    }
}

static inline void tcg_gen_rotl_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    if (TCG_TARGET_HAS_rot_i64) {
        tcg_gen_op3_i64(INDEX_op_rotl_i64, ret, arg1, arg2);
    } else {
        TCGv_i64 t0, t1;
        t0 = tcg_temp_new_i64();
        t1 = tcg_temp_new_i64();
        tcg_gen_shl_i64(t0, arg1, arg2);
        tcg_gen_subfi_i64(t1, 64, arg2);
        tcg_gen_shr_i64(t1, arg1, t1);
        tcg_gen_or_i64(ret, t0, t1);
        tcg_temp_free_i64(t0);
        tcg_temp_free_i64(t1);
    }
}

static inline void tcg_gen_rotli_i32(TCGv_i32 ret, TCGv_i32 arg1, int32_t arg2)
{
    /* some cases can be optimized here */
    if (arg2 == 0) {
        tcg_gen_mov_i32(ret, arg1);
    } else if (TCG_TARGET_HAS_rot_i32) {
        TCGv_i32 t0 = tcg_const_i32(arg2);
        tcg_gen_rotl_i32(ret, arg1, t0);
        tcg_temp_free_i32(t0);
    } else {
        TCGv_i32 t0, t1;
        t0 = tcg_temp_new_i32();
        t1 = tcg_temp_new_i32();
        tcg_gen_shli_i32(t0, arg1, arg2);
        tcg_gen_shri_i32(t1, arg1, 32 - arg2);
        tcg_gen_or_i32(ret, t0, t1);
        tcg_temp_free_i32(t0);
        tcg_temp_free_i32(t1);
    }
}

static inline void tcg_gen_rotli_i64(TCGv_i64 ret, TCGv_i64 arg1, int64_t arg2)
{
    /* some cases can be optimized here */
    if (arg2 == 0) {
        tcg_gen_mov_i64(ret, arg1);
    } else if (TCG_TARGET_HAS_rot_i64) {
        TCGv_i64 t0 = tcg_const_i64(arg2);
        tcg_gen_rotl_i64(ret, arg1, t0);
        tcg_temp_free_i64(t0);
    } else {
        TCGv_i64 t0, t1;
        t0 = tcg_temp_new_i64();
        t1 = tcg_temp_new_i64();
        tcg_gen_shli_i64(t0, arg1, arg2);
        tcg_gen_shri_i64(t1, arg1, 64 - arg2);
        tcg_gen_or_i64(ret, t0, t1);
        tcg_temp_free_i64(t0);
        tcg_temp_free_i64(t1);
    }
}

static inline void tcg_gen_rotr_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    if (TCG_TARGET_HAS_rot_i32) {
        tcg_gen_op3_i32(INDEX_op_rotr_i32, ret, arg1, arg2);
    } else {
        TCGv_i32 t0, t1;

        t0 = tcg_temp_new_i32();
        t1 = tcg_temp_new_i32();
        tcg_gen_shr_i32(t0, arg1, arg2);
        tcg_gen_subfi_i32(t1, 32, arg2);
        tcg_gen_shl_i32(t1, arg1, t1);
        tcg_gen_or_i32(ret, t0, t1);
        tcg_temp_free_i32(t0);
        tcg_temp_free_i32(t1);
    }
}

static inline void tcg_gen_rotr_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    if (TCG_TARGET_HAS_rot_i64) {
        tcg_gen_op3_i64(INDEX_op_rotr_i64, ret, arg1, arg2);
    } else {
        TCGv_i64 t0, t1;
        t0 = tcg_temp_new_i64();
        t1 = tcg_temp_new_i64();
        tcg_gen_shr_i64(t0, arg1, arg2);
        tcg_gen_subfi_i64(t1, 64, arg2);
        tcg_gen_shl_i64(t1, arg1, t1);
        tcg_gen_or_i64(ret, t0, t1);
        tcg_temp_free_i64(t0);
        tcg_temp_free_i64(t1);
    }
}

static inline void tcg_gen_rotri_i32(TCGv_i32 ret, TCGv_i32 arg1, int32_t arg2)
{
    /* some cases can be optimized here */
    if (arg2 == 0) {
        tcg_gen_mov_i32(ret, arg1);
    } else {
        tcg_gen_rotli_i32(ret, arg1, 32 - arg2);
    }
}

static inline void tcg_gen_rotri_i64(TCGv_i64 ret, TCGv_i64 arg1, int64_t arg2)
{
    /* some cases can be optimized here */
    if (arg2 == 0) {
        tcg_gen_mov_i64(ret, arg1);
    } else {
        tcg_gen_rotli_i64(ret, arg1, 64 - arg2);
    }
}

static inline void tcg_gen_deposit_i32(TCGv_i32 ret, TCGv_i32 arg1,
                                       TCGv_i32 arg2, unsigned int ofs,
                                       unsigned int len)
{
    uint32_t mask;
    TCGv_i32 t1;

    tcg_debug_assert(ofs < 32);
    tcg_debug_assert(len <= 32);
    tcg_debug_assert(ofs + len <= 32);

    if (ofs == 0 && len == 32) {
        tcg_gen_mov_i32(ret, arg2);
        return;
    }
    if (TCG_TARGET_HAS_deposit_i32 && TCG_TARGET_deposit_i32_valid(ofs, len)) {
        tcg_gen_op5ii_i32(INDEX_op_deposit_i32, ret, arg1, arg2, ofs, len);
        return;
    }

    mask = (1u << len) - 1;
    t1 = tcg_temp_new_i32();

    if (ofs + len < 32) {
        tcg_gen_andi_i32(t1, arg2, mask);
        tcg_gen_shli_i32(t1, t1, ofs);
    } else {
        tcg_gen_shli_i32(t1, arg2, ofs);
    }
    tcg_gen_andi_i32(ret, arg1, ~(mask << ofs));
    tcg_gen_or_i32(ret, ret, t1);

    tcg_temp_free_i32(t1);
}

static inline void tcg_gen_deposit_i64(TCGv_i64 ret, TCGv_i64 arg1,
                                       TCGv_i64 arg2, unsigned int ofs,
                                       unsigned int len)
{
    uint64_t mask;
    TCGv_i64 t1;

    tcg_debug_assert(ofs < 64);
    tcg_debug_assert(len <= 64);
    tcg_debug_assert(ofs + len <= 64);

    if (ofs == 0 && len == 64) {
        tcg_gen_mov_i64(ret, arg2);
        return;
    }
    if (TCG_TARGET_HAS_deposit_i64 && TCG_TARGET_deposit_i64_valid(ofs, len)) {
        tcg_gen_op5ii_i64(INDEX_op_deposit_i64, ret, arg1, arg2, ofs, len);
        return;
    }

#if TCG_TARGET_REG_BITS == 32
    if (ofs >= 32) {
        tcg_gen_deposit_i32(TCGV_HIGH(ret), TCGV_HIGH(arg1),
                            TCGV_LOW(arg2), ofs - 32, len);
        tcg_gen_mov_i32(TCGV_LOW(ret), TCGV_LOW(arg1));
        return;
    }
    if (ofs + len <= 32) {
        tcg_gen_deposit_i32(TCGV_LOW(ret), TCGV_LOW(arg1),
                            TCGV_LOW(arg2), ofs, len);
        tcg_gen_mov_i32(TCGV_HIGH(ret), TCGV_HIGH(arg1));
        return;
    }
#endif

    mask = (1ull << len) - 1;
    t1 = tcg_temp_new_i64();

    if (ofs + len < 64) {
        tcg_gen_andi_i64(t1, arg2, mask);
        tcg_gen_shli_i64(t1, t1, ofs);
    } else {
        tcg_gen_shli_i64(t1, arg2, ofs);
    }
    tcg_gen_andi_i64(ret, arg1, ~(mask << ofs));
    tcg_gen_or_i64(ret, ret, t1);

    tcg_temp_free_i64(t1);
}

static inline void tcg_gen_concat_i32_i64(TCGv_i64 dest, TCGv_i32 low,
                                          TCGv_i32 high)
{
#if TCG_TARGET_REG_BITS == 32
    tcg_gen_mov_i32(TCGV_LOW(dest), low);
    tcg_gen_mov_i32(TCGV_HIGH(dest), high);
#else
    TCGv_i64 tmp = tcg_temp_new_i64();
    /* These extensions are only needed for type correctness.
       We may be able to do better given target specific information.  */
    tcg_gen_extu_i32_i64(tmp, high);
    tcg_gen_extu_i32_i64(dest, low);
    /* If deposit is available, use it.  Otherwise use the extra
       knowledge that we have of the zero-extensions above.  */
    if (TCG_TARGET_HAS_deposit_i64 && TCG_TARGET_deposit_i64_valid(32, 32)) {
        tcg_gen_deposit_i64(dest, dest, tmp, 32, 32);
    } else {
        tcg_gen_shli_i64(tmp, tmp, 32);
        tcg_gen_or_i64(dest, dest, tmp);
    }
    tcg_temp_free_i64(tmp);
#endif
}

static inline void tcg_gen_concat32_i64(TCGv_i64 dest, TCGv_i64 low,
                                        TCGv_i64 high)
{
    tcg_gen_deposit_i64(dest, low, high, 32, 32);
}

static inline void tcg_gen_trunc_i64_i32(TCGv_i32 ret, TCGv_i64 arg)
{
    tcg_gen_trunc_shr_i64_i32(ret, arg, 0);
}

static inline void tcg_gen_extr_i64_i32(TCGv_i32 lo, TCGv_i32 hi, TCGv_i64 arg)
{
    tcg_gen_trunc_shr_i64_i32(lo, arg, 0);
    tcg_gen_trunc_shr_i64_i32(hi, arg, 32);
}

static inline void tcg_gen_extr32_i64(TCGv_i64 lo, TCGv_i64 hi, TCGv_i64 arg)
{
    tcg_gen_ext32u_i64(lo, arg);
    tcg_gen_shri_i64(hi, arg, 32);
}

static inline void tcg_gen_movcond_i32(TCGCond cond, TCGv_i32 ret,
                                       TCGv_i32 c1, TCGv_i32 c2,
                                       TCGv_i32 v1, TCGv_i32 v2)
{
    if (TCG_TARGET_HAS_movcond_i32) {
        tcg_gen_op6i_i32(INDEX_op_movcond_i32, ret, c1, c2, v1, v2, cond);
    } else {
        TCGv_i32 t0 = tcg_temp_new_i32();
        TCGv_i32 t1 = tcg_temp_new_i32();
        tcg_gen_setcond_i32(cond, t0, c1, c2);
        tcg_gen_neg_i32(t0, t0);
        tcg_gen_and_i32(t1, v1, t0);
        tcg_gen_andc_i32(ret, v2, t0);
        tcg_gen_or_i32(ret, ret, t1);
        tcg_temp_free_i32(t0);
        tcg_temp_free_i32(t1);
    }
}

static inline void tcg_gen_movcond_i64(TCGCond cond, TCGv_i64 ret,
                                       TCGv_i64 c1, TCGv_i64 c2,
                                       TCGv_i64 v1, TCGv_i64 v2)
{
#if TCG_TARGET_REG_BITS == 32
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    tcg_gen_op6i_i32(INDEX_op_setcond2_i32, t0,
                     TCGV_LOW(c1), TCGV_HIGH(c1),
                     TCGV_LOW(c2), TCGV_HIGH(c2), cond);

    if (TCG_TARGET_HAS_movcond_i32) {
        tcg_gen_movi_i32(t1, 0);
        tcg_gen_movcond_i32(TCG_COND_NE, TCGV_LOW(ret), t0, t1,
                            TCGV_LOW(v1), TCGV_LOW(v2));
        tcg_gen_movcond_i32(TCG_COND_NE, TCGV_HIGH(ret), t0, t1,
                            TCGV_HIGH(v1), TCGV_HIGH(v2));
    } else {
        tcg_gen_neg_i32(t0, t0);

        tcg_gen_and_i32(t1, TCGV_LOW(v1), t0);
        tcg_gen_andc_i32(TCGV_LOW(ret), TCGV_LOW(v2), t0);
        tcg_gen_or_i32(TCGV_LOW(ret), TCGV_LOW(ret), t1);

        tcg_gen_and_i32(t1, TCGV_HIGH(v1), t0);
        tcg_gen_andc_i32(TCGV_HIGH(ret), TCGV_HIGH(v2), t0);
        tcg_gen_or_i32(TCGV_HIGH(ret), TCGV_HIGH(ret), t1);
    }
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
#else
    if (TCG_TARGET_HAS_movcond_i64) {
        tcg_gen_op6i_i64(INDEX_op_movcond_i64, ret, c1, c2, v1, v2, cond);
    } else {
        TCGv_i64 t0 = tcg_temp_new_i64();
        TCGv_i64 t1 = tcg_temp_new_i64();
        tcg_gen_setcond_i64(cond, t0, c1, c2);
        tcg_gen_neg_i64(t0, t0);
        tcg_gen_and_i64(t1, v1, t0);
        tcg_gen_andc_i64(ret, v2, t0);
        tcg_gen_or_i64(ret, ret, t1);
        tcg_temp_free_i64(t0);
        tcg_temp_free_i64(t1);
    }
#endif
}

static inline void tcg_gen_add2_i32(TCGv_i32 rl, TCGv_i32 rh, TCGv_i32 al,
                                    TCGv_i32 ah, TCGv_i32 bl, TCGv_i32 bh)
{
    if (TCG_TARGET_HAS_add2_i32) {
        tcg_gen_op6_i32(INDEX_op_add2_i32, rl, rh, al, ah, bl, bh);
        /* Allow the optimizer room to replace add2 with two moves.  */
        tcg_gen_op0(INDEX_op_nop);
    } else {
        TCGv_i64 t0 = tcg_temp_new_i64();
        TCGv_i64 t1 = tcg_temp_new_i64();
        tcg_gen_concat_i32_i64(t0, al, ah);
        tcg_gen_concat_i32_i64(t1, bl, bh);
        tcg_gen_add_i64(t0, t0, t1);
        tcg_gen_extr_i64_i32(rl, rh, t0);
        tcg_temp_free_i64(t0);
        tcg_temp_free_i64(t1);
    }
}

static inline void tcg_gen_sub2_i32(TCGv_i32 rl, TCGv_i32 rh, TCGv_i32 al,
                                    TCGv_i32 ah, TCGv_i32 bl, TCGv_i32 bh)
{
    if (TCG_TARGET_HAS_sub2_i32) {
        tcg_gen_op6_i32(INDEX_op_sub2_i32, rl, rh, al, ah, bl, bh);
        /* Allow the optimizer room to replace sub2 with two moves.  */
        tcg_gen_op0(INDEX_op_nop);
    } else {
        TCGv_i64 t0 = tcg_temp_new_i64();
        TCGv_i64 t1 = tcg_temp_new_i64();
        tcg_gen_concat_i32_i64(t0, al, ah);
        tcg_gen_concat_i32_i64(t1, bl, bh);
        tcg_gen_sub_i64(t0, t0, t1);
        tcg_gen_extr_i64_i32(rl, rh, t0);
        tcg_temp_free_i64(t0);
        tcg_temp_free_i64(t1);
    }
}

static inline void tcg_gen_mulu2_i32(TCGv_i32 rl, TCGv_i32 rh,
                                     TCGv_i32 arg1, TCGv_i32 arg2)
{
    if (TCG_TARGET_HAS_mulu2_i32) {
        tcg_gen_op4_i32(INDEX_op_mulu2_i32, rl, rh, arg1, arg2);
        /* Allow the optimizer room to replace mulu2 with two moves.  */
        tcg_gen_op0(INDEX_op_nop);
    } else if (TCG_TARGET_HAS_muluh_i32) {
        TCGv_i32 t = tcg_temp_new_i32();
        tcg_gen_op3_i32(INDEX_op_mul_i32, t, arg1, arg2);
        tcg_gen_op3_i32(INDEX_op_muluh_i32, rh, arg1, arg2);
        tcg_gen_mov_i32(rl, t);
        tcg_temp_free_i32(t);
    } else {
        TCGv_i64 t0 = tcg_temp_new_i64();
        TCGv_i64 t1 = tcg_temp_new_i64();
        tcg_gen_extu_i32_i64(t0, arg1);
        tcg_gen_extu_i32_i64(t1, arg2);
        tcg_gen_mul_i64(t0, t0, t1);
        tcg_gen_extr_i64_i32(rl, rh, t0);
        tcg_temp_free_i64(t0);
        tcg_temp_free_i64(t1);
    }
}

static inline void tcg_gen_muls2_i32(TCGv_i32 rl, TCGv_i32 rh,
                                     TCGv_i32 arg1, TCGv_i32 arg2)
{
    if (TCG_TARGET_HAS_muls2_i32) {
        tcg_gen_op4_i32(INDEX_op_muls2_i32, rl, rh, arg1, arg2);
        /* Allow the optimizer room to replace muls2 with two moves.  */
        tcg_gen_op0(INDEX_op_nop);
    } else if (TCG_TARGET_HAS_mulsh_i32) {
        TCGv_i32 t = tcg_temp_new_i32();
        tcg_gen_op3_i32(INDEX_op_mul_i32, t, arg1, arg2);
        tcg_gen_op3_i32(INDEX_op_mulsh_i32, rh, arg1, arg2);
        tcg_gen_mov_i32(rl, t);
        tcg_temp_free_i32(t);
    } else if (TCG_TARGET_REG_BITS == 32) {
        TCGv_i32 t0 = tcg_temp_new_i32();
        TCGv_i32 t1 = tcg_temp_new_i32();
        TCGv_i32 t2 = tcg_temp_new_i32();
        TCGv_i32 t3 = tcg_temp_new_i32();
        tcg_gen_mulu2_i32(t0, t1, arg1, arg2);
        /* Adjust for negative inputs.  */
        tcg_gen_sari_i32(t2, arg1, 31);
        tcg_gen_sari_i32(t3, arg2, 31);
        tcg_gen_and_i32(t2, t2, arg2);
        tcg_gen_and_i32(t3, t3, arg1);
        tcg_gen_sub_i32(rh, t1, t2);
        tcg_gen_sub_i32(rh, rh, t3);
        tcg_gen_mov_i32(rl, t0);
        tcg_temp_free_i32(t0);
        tcg_temp_free_i32(t1);
        tcg_temp_free_i32(t2);
        tcg_temp_free_i32(t3);
    } else {
        TCGv_i64 t0 = tcg_temp_new_i64();
        TCGv_i64 t1 = tcg_temp_new_i64();
        tcg_gen_ext_i32_i64(t0, arg1);
        tcg_gen_ext_i32_i64(t1, arg2);
        tcg_gen_mul_i64(t0, t0, t1);
        tcg_gen_extr_i64_i32(rl, rh, t0);
        tcg_temp_free_i64(t0);
        tcg_temp_free_i64(t1);
    }
}

static inline void tcg_gen_add2_i64(TCGv_i64 rl, TCGv_i64 rh, TCGv_i64 al,
                                    TCGv_i64 ah, TCGv_i64 bl, TCGv_i64 bh)
{
    if (TCG_TARGET_HAS_add2_i64) {
        tcg_gen_op6_i64(INDEX_op_add2_i64, rl, rh, al, ah, bl, bh);
        /* Allow the optimizer room to replace add2 with two moves.  */
        tcg_gen_op0(INDEX_op_nop);
    } else {
        TCGv_i64 t0 = tcg_temp_new_i64();
        TCGv_i64 t1 = tcg_temp_new_i64();
        tcg_gen_add_i64(t0, al, bl);
        tcg_gen_setcond_i64(TCG_COND_LTU, t1, t0, al);
        tcg_gen_add_i64(rh, ah, bh);
        tcg_gen_add_i64(rh, rh, t1);
        tcg_gen_mov_i64(rl, t0);
        tcg_temp_free_i64(t0);
        tcg_temp_free_i64(t1);
    }
}

static inline void tcg_gen_sub2_i64(TCGv_i64 rl, TCGv_i64 rh, TCGv_i64 al,
                                    TCGv_i64 ah, TCGv_i64 bl, TCGv_i64 bh)
{
    if (TCG_TARGET_HAS_sub2_i64) {
        tcg_gen_op6_i64(INDEX_op_sub2_i64, rl, rh, al, ah, bl, bh);
        /* Allow the optimizer room to replace sub2 with two moves.  */
        tcg_gen_op0(INDEX_op_nop);
    } else {
        TCGv_i64 t0 = tcg_temp_new_i64();
        TCGv_i64 t1 = tcg_temp_new_i64();
        tcg_gen_sub_i64(t0, al, bl);
        tcg_gen_setcond_i64(TCG_COND_LTU, t1, al, bl);
        tcg_gen_sub_i64(rh, ah, bh);
        tcg_gen_sub_i64(rh, rh, t1);
        tcg_gen_mov_i64(rl, t0);
        tcg_temp_free_i64(t0);
        tcg_temp_free_i64(t1);
    }
}

static inline void tcg_gen_mulu2_i64(TCGv_i64 rl, TCGv_i64 rh,
                                     TCGv_i64 arg1, TCGv_i64 arg2)
{
    if (TCG_TARGET_HAS_mulu2_i64) {
        tcg_gen_op4_i64(INDEX_op_mulu2_i64, rl, rh, arg1, arg2);
        /* Allow the optimizer room to replace mulu2 with two moves.  */
        tcg_gen_op0(INDEX_op_nop);
    } else if (TCG_TARGET_HAS_muluh_i64) {
        TCGv_i64 t = tcg_temp_new_i64();
        tcg_gen_op3_i64(INDEX_op_mul_i64, t, arg1, arg2);
        tcg_gen_op3_i64(INDEX_op_muluh_i64, rh, arg1, arg2);
        tcg_gen_mov_i64(rl, t);
        tcg_temp_free_i64(t);
    } else {
        TCGv_i64 t0 = tcg_temp_new_i64();
        int sizemask = 0;
        /* Return value and both arguments are 64-bit and unsigned.  */
        sizemask |= tcg_gen_sizemask(0, 1, 0);
        sizemask |= tcg_gen_sizemask(1, 1, 0);
        sizemask |= tcg_gen_sizemask(2, 1, 0);
        tcg_gen_mul_i64(t0, arg1, arg2);
        tcg_gen_helper64(helper_muluh_i64, sizemask, rh, arg1, arg2);
        tcg_gen_mov_i64(rl, t0);
        tcg_temp_free_i64(t0);
    }
}

static inline void tcg_gen_muls2_i64(TCGv_i64 rl, TCGv_i64 rh,
                                     TCGv_i64 arg1, TCGv_i64 arg2)
{
    if (TCG_TARGET_HAS_muls2_i64) {
        tcg_gen_op4_i64(INDEX_op_muls2_i64, rl, rh, arg1, arg2);
        /* Allow the optimizer room to replace muls2 with two moves.  */
        tcg_gen_op0(INDEX_op_nop);
    } else if (TCG_TARGET_HAS_mulsh_i64) {
        TCGv_i64 t = tcg_temp_new_i64();
        tcg_gen_op3_i64(INDEX_op_mul_i64, t, arg1, arg2);
        tcg_gen_op3_i64(INDEX_op_mulsh_i64, rh, arg1, arg2);
        tcg_gen_mov_i64(rl, t);
        tcg_temp_free_i64(t);
    } else if (TCG_TARGET_HAS_mulu2_i64 || TCG_TARGET_HAS_muluh_i64) {
        TCGv_i64 t0 = tcg_temp_new_i64();
        TCGv_i64 t1 = tcg_temp_new_i64();
        TCGv_i64 t2 = tcg_temp_new_i64();
        TCGv_i64 t3 = tcg_temp_new_i64();
        tcg_gen_mulu2_i64(t0, t1, arg1, arg2);
        /* Adjust for negative inputs.  */
        tcg_gen_sari_i64(t2, arg1, 63);
        tcg_gen_sari_i64(t3, arg2, 63);
        tcg_gen_and_i64(t2, t2, arg2);
        tcg_gen_and_i64(t3, t3, arg1);
        tcg_gen_sub_i64(rh, t1, t2);
        tcg_gen_sub_i64(rh, rh, t3);
        tcg_gen_mov_i64(rl, t0);
        tcg_temp_free_i64(t0);
        tcg_temp_free_i64(t1);
        tcg_temp_free_i64(t2);
        tcg_temp_free_i64(t3);
    } else {
        TCGv_i64 t0 = tcg_temp_new_i64();
        int sizemask = 0;
        /* Return value and both arguments are 64-bit and signed.  */
        sizemask |= tcg_gen_sizemask(0, 1, 1);
        sizemask |= tcg_gen_sizemask(1, 1, 1);
        sizemask |= tcg_gen_sizemask(2, 1, 1);
        tcg_gen_mul_i64(t0, arg1, arg2);
        tcg_gen_helper64(helper_mulsh_i64, sizemask, rh, arg1, arg2);
        tcg_gen_mov_i64(rl, t0);
        tcg_temp_free_i64(t0);
    }
}

/***************************************/
/* QEMU specific operations. Their type depend on the QEMU CPU
   type. */
#ifndef TARGET_LONG_BITS
#error must include QEMU headers
#endif

#if TARGET_LONG_BITS == 32
#define TCGv TCGv_i32
#define tcg_temp_new() tcg_temp_new_i32()
#define tcg_global_reg_new tcg_global_reg_new_i32
#define tcg_global_mem_new tcg_global_mem_new_i32
#define tcg_temp_local_new() tcg_temp_local_new_i32()
#define tcg_temp_free tcg_temp_free_i32
#define TCGV_UNUSED(x) TCGV_UNUSED_I32(x)
#define TCGV_IS_UNUSED(x) TCGV_IS_UNUSED_I32(x)
#define TCGV_EQUAL(a, b) TCGV_EQUAL_I32(a, b)
#define tcg_add_param_tl tcg_add_param_i32
#define tcg_gen_qemu_ld_tl tcg_gen_qemu_ld_i32
#define tcg_gen_qemu_st_tl tcg_gen_qemu_st_i32
#else
#define TCGv TCGv_i64
#define tcg_temp_new() tcg_temp_new_i64()
#define tcg_global_reg_new tcg_global_reg_new_i64
#define tcg_global_mem_new tcg_global_mem_new_i64
#define tcg_temp_local_new() tcg_temp_local_new_i64()
#define tcg_temp_free tcg_temp_free_i64
#define TCGV_UNUSED(x) TCGV_UNUSED_I64(x)
#define TCGV_IS_UNUSED(x) TCGV_IS_UNUSED_I64(x)
#define TCGV_EQUAL(a, b) TCGV_EQUAL_I64(a, b)
#define tcg_add_param_tl tcg_add_param_i64
#define tcg_gen_qemu_ld_tl tcg_gen_qemu_ld_i64
#define tcg_gen_qemu_st_tl tcg_gen_qemu_st_i64
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

static inline void tcg_gen_exit_tb(uintptr_t val)
{
    tcg_gen_op1i(INDEX_op_exit_tb, val);
}

static inline void tcg_gen_goto_tb(unsigned idx)
{
    /* We only support two chained exits.  */
    tcg_debug_assert(idx <= 1);
#ifdef CONFIG_DEBUG_TCG
    /* Verify that we havn't seen this numbered exit before.  */
    tcg_debug_assert((tcg_ctx.goto_tb_issue_mask & (1 << idx)) == 0);
    tcg_ctx.goto_tb_issue_mask |= 1 << idx;
#endif
    tcg_gen_op1i(INDEX_op_goto_tb, idx);
}


void tcg_gen_qemu_ld_i32(TCGv_i32, TCGv, TCGArg, TCGMemOp);
void tcg_gen_qemu_st_i32(TCGv_i32, TCGv, TCGArg, TCGMemOp);
void tcg_gen_qemu_ld_i64(TCGv_i64, TCGv, TCGArg, TCGMemOp);
void tcg_gen_qemu_st_i64(TCGv_i64, TCGv, TCGArg, TCGMemOp);

static inline void tcg_gen_qemu_ld8u(TCGv ret, TCGv addr, int mem_index)
{
    tcg_gen_qemu_ld_tl(ret, addr, mem_index, MO_UB);
}

static inline void tcg_gen_qemu_ld8s(TCGv ret, TCGv addr, int mem_index)
{
    tcg_gen_qemu_ld_tl(ret, addr, mem_index, MO_SB);
}

static inline void tcg_gen_qemu_ld16u(TCGv ret, TCGv addr, int mem_index)
{
    tcg_gen_qemu_ld_tl(ret, addr, mem_index, MO_TEUW);
}

static inline void tcg_gen_qemu_ld16s(TCGv ret, TCGv addr, int mem_index)
{
    tcg_gen_qemu_ld_tl(ret, addr, mem_index, MO_TESW);
}

static inline void tcg_gen_qemu_ld32u(TCGv ret, TCGv addr, int mem_index)
{
    tcg_gen_qemu_ld_tl(ret, addr, mem_index, MO_TEUL);
}

static inline void tcg_gen_qemu_ld32s(TCGv ret, TCGv addr, int mem_index)
{
    tcg_gen_qemu_ld_tl(ret, addr, mem_index, MO_TESL);
}

static inline void tcg_gen_qemu_ld64(TCGv_i64 ret, TCGv addr, int mem_index)
{
    tcg_gen_qemu_ld_i64(ret, addr, mem_index, MO_TEQ);
}

static inline void tcg_gen_qemu_st8(TCGv arg, TCGv addr, int mem_index)
{
    tcg_gen_qemu_st_tl(arg, addr, mem_index, MO_UB);
}

static inline void tcg_gen_qemu_st16(TCGv arg, TCGv addr, int mem_index)
{
    tcg_gen_qemu_st_tl(arg, addr, mem_index, MO_TEUW);
}

static inline void tcg_gen_qemu_st32(TCGv arg, TCGv addr, int mem_index)
{
    tcg_gen_qemu_st_tl(arg, addr, mem_index, MO_TEUL);
}

static inline void tcg_gen_qemu_st64(TCGv_i64 arg, TCGv addr, int mem_index)
{
    tcg_gen_qemu_st_i64(arg, addr, mem_index, MO_TEQ);
}

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
#define tcg_gen_bswap16_tl tcg_gen_bswap16_i64
#define tcg_gen_bswap32_tl tcg_gen_bswap32_i64
#define tcg_gen_bswap64_tl tcg_gen_bswap64_i64
#define tcg_gen_concat_tl_i64 tcg_gen_concat32_i64
#define tcg_gen_extr_i64_tl tcg_gen_extr32_i64
#define tcg_gen_andc_tl tcg_gen_andc_i64
#define tcg_gen_eqv_tl tcg_gen_eqv_i64
#define tcg_gen_nand_tl tcg_gen_nand_i64
#define tcg_gen_nor_tl tcg_gen_nor_i64
#define tcg_gen_orc_tl tcg_gen_orc_i64
#define tcg_gen_rotl_tl tcg_gen_rotl_i64
#define tcg_gen_rotli_tl tcg_gen_rotli_i64
#define tcg_gen_rotr_tl tcg_gen_rotr_i64
#define tcg_gen_rotri_tl tcg_gen_rotri_i64
#define tcg_gen_deposit_tl tcg_gen_deposit_i64
#define tcg_const_tl tcg_const_i64
#define tcg_const_local_tl tcg_const_local_i64
#define tcg_gen_movcond_tl tcg_gen_movcond_i64
#define tcg_gen_add2_tl tcg_gen_add2_i64
#define tcg_gen_sub2_tl tcg_gen_sub2_i64
#define tcg_gen_mulu2_tl tcg_gen_mulu2_i64
#define tcg_gen_muls2_tl tcg_gen_muls2_i64
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
#define tcg_gen_bswap16_tl tcg_gen_bswap16_i32
#define tcg_gen_bswap32_tl tcg_gen_bswap32_i32
#define tcg_gen_concat_tl_i64 tcg_gen_concat_i32_i64
#define tcg_gen_extr_tl_i64 tcg_gen_extr_i32_i64
#define tcg_gen_andc_tl tcg_gen_andc_i32
#define tcg_gen_eqv_tl tcg_gen_eqv_i32
#define tcg_gen_nand_tl tcg_gen_nand_i32
#define tcg_gen_nor_tl tcg_gen_nor_i32
#define tcg_gen_orc_tl tcg_gen_orc_i32
#define tcg_gen_rotl_tl tcg_gen_rotl_i32
#define tcg_gen_rotli_tl tcg_gen_rotli_i32
#define tcg_gen_rotr_tl tcg_gen_rotr_i32
#define tcg_gen_rotri_tl tcg_gen_rotri_i32
#define tcg_gen_deposit_tl tcg_gen_deposit_i32
#define tcg_const_tl tcg_const_i32
#define tcg_const_local_tl tcg_const_local_i32
#define tcg_gen_movcond_tl tcg_gen_movcond_i32
#define tcg_gen_add2_tl tcg_gen_add2_i32
#define tcg_gen_sub2_tl tcg_gen_sub2_i32
#define tcg_gen_mulu2_tl tcg_gen_mulu2_i32
#define tcg_gen_muls2_tl tcg_gen_muls2_i32
#endif

#if UINTPTR_MAX == UINT32_MAX
# define tcg_gen_ld_ptr(R, A, O) \
    tcg_gen_ld_i32(TCGV_PTR_TO_NAT(R), (A), (O))
# define tcg_gen_discard_ptr(A) \
    tcg_gen_discard_i32(TCGV_PTR_TO_NAT(A))
# define tcg_gen_add_ptr(R, A, B) \
    tcg_gen_add_i32(TCGV_PTR_TO_NAT(R), TCGV_PTR_TO_NAT(A), TCGV_PTR_TO_NAT(B))
# define tcg_gen_addi_ptr(R, A, B) \
    tcg_gen_addi_i32(TCGV_PTR_TO_NAT(R), TCGV_PTR_TO_NAT(A), (B))
# define tcg_gen_ext_i32_ptr(R, A) \
    tcg_gen_mov_i32(TCGV_PTR_TO_NAT(R), (A))
#else
# define tcg_gen_ld_ptr(R, A, O) \
    tcg_gen_ld_i64(TCGV_PTR_TO_NAT(R), (A), (O))
# define tcg_gen_discard_ptr(A) \
    tcg_gen_discard_i64(TCGV_PTR_TO_NAT(A))
# define tcg_gen_add_ptr(R, A, B) \
    tcg_gen_add_i64(TCGV_PTR_TO_NAT(R), TCGV_PTR_TO_NAT(A), TCGV_PTR_TO_NAT(B))
# define tcg_gen_addi_ptr(R, A, B) \
    tcg_gen_addi_i64(TCGV_PTR_TO_NAT(R), TCGV_PTR_TO_NAT(A), (B))
# define tcg_gen_ext_i32_ptr(R, A) \
    tcg_gen_ext_i32_i64(TCGV_PTR_TO_NAT(R), (A))
#endif /* UINTPTR_MAX == UINT32_MAX */
