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

#include "qemu/osdep.h"
#include "tcg/tcg.h"
#include "tcg/tcg-temp-internal.h"
#include "tcg/tcg-op-common.h"
#include "exec/translation-block.h"
#include "exec/plugin-gen.h"
#include "tcg-internal.h"
#include "tcg-has.h"

/*
 * Encourage the compiler to tail-call to a function, rather than inlining.
 * Minimizes code size across 99 bottles of beer on the wall.
 */
#define NI  __attribute__((noinline))

TCGOp * NI tcg_gen_op1(TCGOpcode opc, TCGType type, TCGArg a1)
{
    TCGOp *op = tcg_emit_op(opc, 1);
    TCGOP_TYPE(op) = type;
    op->args[0] = a1;
    return op;
}

TCGOp * NI tcg_gen_op2(TCGOpcode opc, TCGType type, TCGArg a1, TCGArg a2)
{
    TCGOp *op = tcg_emit_op(opc, 2);
    TCGOP_TYPE(op) = type;
    op->args[0] = a1;
    op->args[1] = a2;
    return op;
}

TCGOp * NI tcg_gen_op3(TCGOpcode opc, TCGType type, TCGArg a1,
                       TCGArg a2, TCGArg a3)
{
    TCGOp *op = tcg_emit_op(opc, 3);
    TCGOP_TYPE(op) = type;
    op->args[0] = a1;
    op->args[1] = a2;
    op->args[2] = a3;
    return op;
}

TCGOp * NI tcg_gen_op4(TCGOpcode opc, TCGType type, TCGArg a1, TCGArg a2,
                       TCGArg a3, TCGArg a4)
{
    TCGOp *op = tcg_emit_op(opc, 4);
    TCGOP_TYPE(op) = type;
    op->args[0] = a1;
    op->args[1] = a2;
    op->args[2] = a3;
    op->args[3] = a4;
    return op;
}

TCGOp * NI tcg_gen_op5(TCGOpcode opc, TCGType type, TCGArg a1, TCGArg a2,
                       TCGArg a3, TCGArg a4, TCGArg a5)
{
    TCGOp *op = tcg_emit_op(opc, 5);
    TCGOP_TYPE(op) = type;
    op->args[0] = a1;
    op->args[1] = a2;
    op->args[2] = a3;
    op->args[3] = a4;
    op->args[4] = a5;
    return op;
}

TCGOp * NI tcg_gen_op6(TCGOpcode opc, TCGType type, TCGArg a1, TCGArg a2,
                       TCGArg a3, TCGArg a4, TCGArg a5, TCGArg a6)
{
    TCGOp *op = tcg_emit_op(opc, 6);
    TCGOP_TYPE(op) = type;
    op->args[0] = a1;
    op->args[1] = a2;
    op->args[2] = a3;
    op->args[3] = a4;
    op->args[4] = a5;
    op->args[5] = a6;
    return op;
}

/*
 * With CONFIG_DEBUG_TCG, tcgv_*_tmp via tcgv_*_arg, is an out-of-line
 * assertion check.  Force tail calls to avoid too much code expansion.
 */
#ifdef CONFIG_DEBUG_TCG
# define DNI NI
#else
# define DNI
#endif

static void DNI tcg_gen_op1_i32(TCGOpcode opc, TCGType type, TCGv_i32 a1)
{
    tcg_gen_op1(opc, type, tcgv_i32_arg(a1));
}

static void DNI tcg_gen_op1_i64(TCGOpcode opc, TCGType type, TCGv_i64 a1)
{
    tcg_gen_op1(opc, type, tcgv_i64_arg(a1));
}

static TCGOp * DNI tcg_gen_op1i(TCGOpcode opc, TCGType type, TCGArg a1)
{
    return tcg_gen_op1(opc, type, a1);
}

static void DNI tcg_gen_op2_i32(TCGOpcode opc, TCGv_i32 a1, TCGv_i32 a2)
{
    tcg_gen_op2(opc, TCG_TYPE_I32, tcgv_i32_arg(a1), tcgv_i32_arg(a2));
}

static void DNI tcg_gen_op2_i64(TCGOpcode opc, TCGv_i64 a1, TCGv_i64 a2)
{
    tcg_gen_op2(opc, TCG_TYPE_I64, tcgv_i64_arg(a1), tcgv_i64_arg(a2));
}

static void DNI tcg_gen_op3_i32(TCGOpcode opc, TCGv_i32 a1,
                                TCGv_i32 a2, TCGv_i32 a3)
{
    tcg_gen_op3(opc, TCG_TYPE_I32, tcgv_i32_arg(a1),
                tcgv_i32_arg(a2), tcgv_i32_arg(a3));
}

static void DNI tcg_gen_op3_i64(TCGOpcode opc, TCGv_i64 a1,
                                TCGv_i64 a2, TCGv_i64 a3)
{
    tcg_gen_op3(opc, TCG_TYPE_I64, tcgv_i64_arg(a1),
                tcgv_i64_arg(a2), tcgv_i64_arg(a3));
}

static void DNI tcg_gen_op3i_i32(TCGOpcode opc, TCGv_i32 a1,
                                 TCGv_i32 a2, TCGArg a3)
{
    tcg_gen_op3(opc, TCG_TYPE_I32, tcgv_i32_arg(a1), tcgv_i32_arg(a2), a3);
}

static void DNI tcg_gen_op3i_i64(TCGOpcode opc, TCGv_i64 a1,
                                 TCGv_i64 a2, TCGArg a3)
{
    tcg_gen_op3(opc, TCG_TYPE_I64, tcgv_i64_arg(a1), tcgv_i64_arg(a2), a3);
}

static void DNI tcg_gen_ldst_op_i32(TCGOpcode opc, TCGv_i32 val,
                                    TCGv_ptr base, TCGArg offset)
{
    tcg_gen_op3(opc, TCG_TYPE_I32, tcgv_i32_arg(val),
                tcgv_ptr_arg(base), offset);
}

static void DNI tcg_gen_ldst_op_i64(TCGOpcode opc, TCGv_i64 val,
                                    TCGv_ptr base, TCGArg offset)
{
    tcg_gen_op3(opc, TCG_TYPE_I64, tcgv_i64_arg(val),
                tcgv_ptr_arg(base), offset);
}

static void DNI tcg_gen_op4_i32(TCGOpcode opc, TCGv_i32 a1, TCGv_i32 a2,
                                TCGv_i32 a3, TCGv_i32 a4)
{
    tcg_gen_op4(opc, TCG_TYPE_I32, tcgv_i32_arg(a1), tcgv_i32_arg(a2),
                tcgv_i32_arg(a3), tcgv_i32_arg(a4));
}

static void DNI tcg_gen_op4_i64(TCGOpcode opc, TCGv_i64 a1, TCGv_i64 a2,
                                TCGv_i64 a3, TCGv_i64 a4)
{
    tcg_gen_op4(opc, TCG_TYPE_I64, tcgv_i64_arg(a1), tcgv_i64_arg(a2),
                tcgv_i64_arg(a3), tcgv_i64_arg(a4));
}

static void DNI tcg_gen_op4i_i32(TCGOpcode opc, TCGv_i32 a1, TCGv_i32 a2,
                                 TCGv_i32 a3, TCGArg a4)
{
    tcg_gen_op4(opc, TCG_TYPE_I32, tcgv_i32_arg(a1), tcgv_i32_arg(a2),
                tcgv_i32_arg(a3), a4);
}

static void DNI tcg_gen_op4i_i64(TCGOpcode opc, TCGv_i64 a1, TCGv_i64 a2,
                                 TCGv_i64 a3, TCGArg a4)
{
    tcg_gen_op4(opc, TCG_TYPE_I64, tcgv_i64_arg(a1), tcgv_i64_arg(a2),
                tcgv_i64_arg(a3), a4);
}

static TCGOp * DNI tcg_gen_op4ii_i32(TCGOpcode opc, TCGv_i32 a1, TCGv_i32 a2,
                                     TCGArg a3, TCGArg a4)
{
    return tcg_gen_op4(opc, TCG_TYPE_I32,
                       tcgv_i32_arg(a1), tcgv_i32_arg(a2), a3, a4);
}

static TCGOp * DNI tcg_gen_op4ii_i64(TCGOpcode opc, TCGv_i64 a1, TCGv_i64 a2,
                                     TCGArg a3, TCGArg a4)
{
    return tcg_gen_op4(opc, TCG_TYPE_I64,
                       tcgv_i64_arg(a1), tcgv_i64_arg(a2), a3, a4);
}

static void DNI tcg_gen_op5_i32(TCGOpcode opc, TCGv_i32 a1, TCGv_i32 a2,
                                TCGv_i32 a3, TCGv_i32 a4, TCGv_i32 a5)
{
    tcg_gen_op5(opc, TCG_TYPE_I32, tcgv_i32_arg(a1), tcgv_i32_arg(a2),
                tcgv_i32_arg(a3), tcgv_i32_arg(a4), tcgv_i32_arg(a5));
}

static void DNI tcg_gen_op5_i64(TCGOpcode opc, TCGv_i64 a1, TCGv_i64 a2,
                                TCGv_i64 a3, TCGv_i64 a4, TCGv_i64 a5)
{
    tcg_gen_op5(opc, TCG_TYPE_I64, tcgv_i64_arg(a1), tcgv_i64_arg(a2),
                tcgv_i64_arg(a3), tcgv_i64_arg(a4), tcgv_i64_arg(a5));
}

static void DNI tcg_gen_op5ii_i32(TCGOpcode opc, TCGv_i32 a1, TCGv_i32 a2,
                                  TCGv_i32 a3, TCGArg a4, TCGArg a5)
{
    tcg_gen_op5(opc, TCG_TYPE_I32, tcgv_i32_arg(a1), tcgv_i32_arg(a2),
                tcgv_i32_arg(a3), a4, a5);
}

static void DNI tcg_gen_op5ii_i64(TCGOpcode opc, TCGv_i64 a1, TCGv_i64 a2,
                                  TCGv_i64 a3, TCGArg a4, TCGArg a5)
{
    tcg_gen_op5(opc, TCG_TYPE_I64, tcgv_i64_arg(a1), tcgv_i64_arg(a2),
                tcgv_i64_arg(a3), a4, a5);
}

static void DNI tcg_gen_op6_i32(TCGOpcode opc, TCGv_i32 a1, TCGv_i32 a2,
                                TCGv_i32 a3, TCGv_i32 a4,
                                TCGv_i32 a5, TCGv_i32 a6)
{
    tcg_gen_op6(opc, TCG_TYPE_I32, tcgv_i32_arg(a1), tcgv_i32_arg(a2),
                tcgv_i32_arg(a3), tcgv_i32_arg(a4), tcgv_i32_arg(a5),
                tcgv_i32_arg(a6));
}

static void DNI tcg_gen_op6_i64(TCGOpcode opc, TCGv_i64 a1, TCGv_i64 a2,
                                TCGv_i64 a3, TCGv_i64 a4,
                                TCGv_i64 a5, TCGv_i64 a6)
{
    tcg_gen_op6(opc, TCG_TYPE_I64, tcgv_i64_arg(a1), tcgv_i64_arg(a2),
                tcgv_i64_arg(a3), tcgv_i64_arg(a4), tcgv_i64_arg(a5),
                tcgv_i64_arg(a6));
}

static void DNI tcg_gen_op6i_i32(TCGOpcode opc, TCGv_i32 a1, TCGv_i32 a2,
                                 TCGv_i32 a3, TCGv_i32 a4,
                                 TCGv_i32 a5, TCGArg a6)
{
    tcg_gen_op6(opc, TCG_TYPE_I32, tcgv_i32_arg(a1), tcgv_i32_arg(a2),
                tcgv_i32_arg(a3), tcgv_i32_arg(a4), tcgv_i32_arg(a5), a6);
}

static void DNI tcg_gen_op6i_i64(TCGOpcode opc, TCGv_i64 a1, TCGv_i64 a2,
                                 TCGv_i64 a3, TCGv_i64 a4,
                                 TCGv_i64 a5, TCGArg a6)
{
    tcg_gen_op6(opc, TCG_TYPE_I64, tcgv_i64_arg(a1), tcgv_i64_arg(a2),
                tcgv_i64_arg(a3), tcgv_i64_arg(a4), tcgv_i64_arg(a5), a6);
}

static TCGOp * DNI tcg_gen_op6ii_i32(TCGOpcode opc, TCGv_i32 a1, TCGv_i32 a2,
                                     TCGv_i32 a3, TCGv_i32 a4,
                                     TCGArg a5, TCGArg a6)
{
    return tcg_gen_op6(opc, TCG_TYPE_I32, tcgv_i32_arg(a1), tcgv_i32_arg(a2),
                       tcgv_i32_arg(a3), tcgv_i32_arg(a4), a5, a6);
}

/* Generic ops.  */

void gen_set_label(TCGLabel *l)
{
    l->present = 1;
    tcg_gen_op1(INDEX_op_set_label, 0, label_arg(l));
}

static void add_as_label_use(TCGLabel *l, TCGOp *op)
{
    TCGLabelUse *u = tcg_malloc(sizeof(TCGLabelUse));

    u->op = op;
    QSIMPLEQ_INSERT_TAIL(&l->branches, u, next);
}

void tcg_gen_br(TCGLabel *l)
{
    add_as_label_use(l, tcg_gen_op1(INDEX_op_br, 0, label_arg(l)));
}

void tcg_gen_mb(TCGBar mb_type)
{
#ifdef CONFIG_USER_ONLY
    bool parallel = tcg_ctx->gen_tb->cflags & CF_PARALLEL;
#else
    /*
     * It is tempting to elide the barrier in a uniprocessor context.
     * However, even with a single cpu we have i/o threads running in
     * parallel, and lack of memory order can result in e.g. virtio
     * queue entries being read incorrectly.
     */
    bool parallel = true;
#endif

    if (parallel) {
        tcg_gen_op1(INDEX_op_mb, 0, mb_type);
    }
}

void tcg_gen_plugin_cb(unsigned from)
{
    tcg_gen_op1(INDEX_op_plugin_cb, 0, from);
}

void tcg_gen_plugin_mem_cb(TCGv_i64 addr, unsigned meminfo)
{
    tcg_gen_op2(INDEX_op_plugin_mem_cb, 0, tcgv_i64_arg(addr), meminfo);
}

/* 32 bit ops */

void tcg_gen_discard_i32(TCGv_i32 arg)
{
    tcg_gen_op1_i32(INDEX_op_discard, TCG_TYPE_I32, arg);
}

void tcg_gen_mov_i32(TCGv_i32 ret, TCGv_i32 arg)
{
    if (ret != arg) {
        tcg_gen_op2_i32(INDEX_op_mov_i32, ret, arg);
    }
}

void tcg_gen_movi_i32(TCGv_i32 ret, int32_t arg)
{
    tcg_gen_mov_i32(ret, tcg_constant_i32(arg));
}

void tcg_gen_add_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    tcg_gen_op3_i32(INDEX_op_add_i32, ret, arg1, arg2);
}

void tcg_gen_addi_i32(TCGv_i32 ret, TCGv_i32 arg1, int32_t arg2)
{
    /* some cases can be optimized here */
    if (arg2 == 0) {
        tcg_gen_mov_i32(ret, arg1);
    } else {
        tcg_gen_add_i32(ret, arg1, tcg_constant_i32(arg2));
    }
}

void tcg_gen_sub_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    tcg_gen_op3_i32(INDEX_op_sub_i32, ret, arg1, arg2);
}

void tcg_gen_subfi_i32(TCGv_i32 ret, int32_t arg1, TCGv_i32 arg2)
{
    if (arg1 == 0) {
        tcg_gen_neg_i32(ret, arg2);
    } else {
        tcg_gen_sub_i32(ret, tcg_constant_i32(arg1), arg2);
    }
}

void tcg_gen_subi_i32(TCGv_i32 ret, TCGv_i32 arg1, int32_t arg2)
{
    tcg_gen_addi_i32(ret, arg1, -arg2);
}

void tcg_gen_neg_i32(TCGv_i32 ret, TCGv_i32 arg)
{
    tcg_gen_op2_i32(INDEX_op_neg_i32, ret, arg);
}

void tcg_gen_and_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    tcg_gen_op3_i32(INDEX_op_and_i32, ret, arg1, arg2);
}

void tcg_gen_andi_i32(TCGv_i32 ret, TCGv_i32 arg1, int32_t arg2)
{
    /* Some cases can be optimized here.  */
    switch (arg2) {
    case 0:
        tcg_gen_movi_i32(ret, 0);
        return;
    case -1:
        tcg_gen_mov_i32(ret, arg1);
        return;
    case 0xff:
        /* Don't recurse with tcg_gen_ext8u_i32.  */
        if (TCG_TARGET_HAS_ext8u_i32) {
            tcg_gen_op2_i32(INDEX_op_ext8u_i32, ret, arg1);
            return;
        }
        break;
    case 0xffff:
        if (TCG_TARGET_HAS_ext16u_i32) {
            tcg_gen_op2_i32(INDEX_op_ext16u_i32, ret, arg1);
            return;
        }
        break;
    }

    tcg_gen_and_i32(ret, arg1, tcg_constant_i32(arg2));
}

void tcg_gen_or_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    tcg_gen_op3_i32(INDEX_op_or_i32, ret, arg1, arg2);
}

void tcg_gen_ori_i32(TCGv_i32 ret, TCGv_i32 arg1, int32_t arg2)
{
    /* Some cases can be optimized here.  */
    if (arg2 == -1) {
        tcg_gen_movi_i32(ret, -1);
    } else if (arg2 == 0) {
        tcg_gen_mov_i32(ret, arg1);
    } else {
        tcg_gen_or_i32(ret, arg1, tcg_constant_i32(arg2));
    }
}

void tcg_gen_xor_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    tcg_gen_op3_i32(INDEX_op_xor_i32, ret, arg1, arg2);
}

void tcg_gen_xori_i32(TCGv_i32 ret, TCGv_i32 arg1, int32_t arg2)
{
    /* Some cases can be optimized here.  */
    if (arg2 == 0) {
        tcg_gen_mov_i32(ret, arg1);
    } else if (arg2 == -1 && TCG_TARGET_HAS_not_i32) {
        /* Don't recurse with tcg_gen_not_i32.  */
        tcg_gen_op2_i32(INDEX_op_not_i32, ret, arg1);
    } else {
        tcg_gen_xor_i32(ret, arg1, tcg_constant_i32(arg2));
    }
}

void tcg_gen_not_i32(TCGv_i32 ret, TCGv_i32 arg)
{
    if (TCG_TARGET_HAS_not_i32) {
        tcg_gen_op2_i32(INDEX_op_not_i32, ret, arg);
    } else {
        tcg_gen_xori_i32(ret, arg, -1);
    }
}

void tcg_gen_shl_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    tcg_gen_op3_i32(INDEX_op_shl_i32, ret, arg1, arg2);
}

void tcg_gen_shli_i32(TCGv_i32 ret, TCGv_i32 arg1, int32_t arg2)
{
    tcg_debug_assert(arg2 >= 0 && arg2 < 32);
    if (arg2 == 0) {
        tcg_gen_mov_i32(ret, arg1);
    } else {
        tcg_gen_shl_i32(ret, arg1, tcg_constant_i32(arg2));
    }
}

void tcg_gen_shr_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    tcg_gen_op3_i32(INDEX_op_shr_i32, ret, arg1, arg2);
}

void tcg_gen_shri_i32(TCGv_i32 ret, TCGv_i32 arg1, int32_t arg2)
{
    tcg_debug_assert(arg2 >= 0 && arg2 < 32);
    if (arg2 == 0) {
        tcg_gen_mov_i32(ret, arg1);
    } else {
        tcg_gen_shr_i32(ret, arg1, tcg_constant_i32(arg2));
    }
}

void tcg_gen_sar_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    tcg_gen_op3_i32(INDEX_op_sar_i32, ret, arg1, arg2);
}

void tcg_gen_sari_i32(TCGv_i32 ret, TCGv_i32 arg1, int32_t arg2)
{
    tcg_debug_assert(arg2 >= 0 && arg2 < 32);
    if (arg2 == 0) {
        tcg_gen_mov_i32(ret, arg1);
    } else {
        tcg_gen_sar_i32(ret, arg1, tcg_constant_i32(arg2));
    }
}

void tcg_gen_brcond_i32(TCGCond cond, TCGv_i32 arg1, TCGv_i32 arg2, TCGLabel *l)
{
    if (cond == TCG_COND_ALWAYS) {
        tcg_gen_br(l);
    } else if (cond != TCG_COND_NEVER) {
        TCGOp *op = tcg_gen_op4ii_i32(INDEX_op_brcond_i32,
                                      arg1, arg2, cond, label_arg(l));
        add_as_label_use(l, op);
    }
}

void tcg_gen_brcondi_i32(TCGCond cond, TCGv_i32 arg1, int32_t arg2, TCGLabel *l)
{
    if (cond == TCG_COND_ALWAYS) {
        tcg_gen_br(l);
    } else if (cond != TCG_COND_NEVER) {
        tcg_gen_brcond_i32(cond, arg1, tcg_constant_i32(arg2), l);
    }
}

void tcg_gen_setcond_i32(TCGCond cond, TCGv_i32 ret,
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

void tcg_gen_setcondi_i32(TCGCond cond, TCGv_i32 ret,
                          TCGv_i32 arg1, int32_t arg2)
{
    tcg_gen_setcond_i32(cond, ret, arg1, tcg_constant_i32(arg2));
}

void tcg_gen_negsetcond_i32(TCGCond cond, TCGv_i32 ret,
                            TCGv_i32 arg1, TCGv_i32 arg2)
{
    if (cond == TCG_COND_ALWAYS) {
        tcg_gen_movi_i32(ret, -1);
    } else if (cond == TCG_COND_NEVER) {
        tcg_gen_movi_i32(ret, 0);
    } else if (TCG_TARGET_HAS_negsetcond_i32) {
        tcg_gen_op4i_i32(INDEX_op_negsetcond_i32, ret, arg1, arg2, cond);
    } else {
        tcg_gen_setcond_i32(cond, ret, arg1, arg2);
        tcg_gen_neg_i32(ret, ret);
    }
}

void tcg_gen_negsetcondi_i32(TCGCond cond, TCGv_i32 ret,
                             TCGv_i32 arg1, int32_t arg2)
{
    tcg_gen_negsetcond_i32(cond, ret, arg1, tcg_constant_i32(arg2));
}

void tcg_gen_mul_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    tcg_gen_op3_i32(INDEX_op_mul_i32, ret, arg1, arg2);
}

void tcg_gen_muli_i32(TCGv_i32 ret, TCGv_i32 arg1, int32_t arg2)
{
    if (arg2 == 0) {
        tcg_gen_movi_i32(ret, 0);
    } else if (is_power_of_2(arg2)) {
        tcg_gen_shli_i32(ret, arg1, ctz32(arg2));
    } else {
        tcg_gen_mul_i32(ret, arg1, tcg_constant_i32(arg2));
    }
}

void tcg_gen_div_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    if (TCG_TARGET_HAS_div_i32) {
        tcg_gen_op3_i32(INDEX_op_div_i32, ret, arg1, arg2);
    } else if (TCG_TARGET_HAS_div2_i32) {
        TCGv_i32 t0 = tcg_temp_ebb_new_i32();
        tcg_gen_sari_i32(t0, arg1, 31);
        tcg_gen_op5_i32(INDEX_op_div2_i32, ret, t0, arg1, t0, arg2);
        tcg_temp_free_i32(t0);
    } else {
        gen_helper_div_i32(ret, arg1, arg2);
    }
}

void tcg_gen_rem_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    if (TCG_TARGET_HAS_rem_i32) {
        tcg_gen_op3_i32(INDEX_op_rem_i32, ret, arg1, arg2);
    } else if (TCG_TARGET_HAS_div_i32) {
        TCGv_i32 t0 = tcg_temp_ebb_new_i32();
        tcg_gen_op3_i32(INDEX_op_div_i32, t0, arg1, arg2);
        tcg_gen_mul_i32(t0, t0, arg2);
        tcg_gen_sub_i32(ret, arg1, t0);
        tcg_temp_free_i32(t0);
    } else if (TCG_TARGET_HAS_div2_i32) {
        TCGv_i32 t0 = tcg_temp_ebb_new_i32();
        tcg_gen_sari_i32(t0, arg1, 31);
        tcg_gen_op5_i32(INDEX_op_div2_i32, t0, ret, arg1, t0, arg2);
        tcg_temp_free_i32(t0);
    } else {
        gen_helper_rem_i32(ret, arg1, arg2);
    }
}

void tcg_gen_divu_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    if (TCG_TARGET_HAS_div_i32) {
        tcg_gen_op3_i32(INDEX_op_divu_i32, ret, arg1, arg2);
    } else if (TCG_TARGET_HAS_div2_i32) {
        TCGv_i32 t0 = tcg_temp_ebb_new_i32();
        TCGv_i32 zero = tcg_constant_i32(0);
        tcg_gen_op5_i32(INDEX_op_divu2_i32, ret, t0, arg1, zero, arg2);
        tcg_temp_free_i32(t0);
    } else {
        gen_helper_divu_i32(ret, arg1, arg2);
    }
}

void tcg_gen_remu_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    if (TCG_TARGET_HAS_rem_i32) {
        tcg_gen_op3_i32(INDEX_op_remu_i32, ret, arg1, arg2);
    } else if (TCG_TARGET_HAS_div_i32) {
        TCGv_i32 t0 = tcg_temp_ebb_new_i32();
        tcg_gen_op3_i32(INDEX_op_divu_i32, t0, arg1, arg2);
        tcg_gen_mul_i32(t0, t0, arg2);
        tcg_gen_sub_i32(ret, arg1, t0);
        tcg_temp_free_i32(t0);
    } else if (TCG_TARGET_HAS_div2_i32) {
        TCGv_i32 t0 = tcg_temp_ebb_new_i32();
        TCGv_i32 zero = tcg_constant_i32(0);
        tcg_gen_op5_i32(INDEX_op_divu2_i32, t0, ret, arg1, zero, arg2);
        tcg_temp_free_i32(t0);
    } else {
        gen_helper_remu_i32(ret, arg1, arg2);
    }
}

void tcg_gen_andc_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    if (TCG_TARGET_HAS_andc_i32) {
        tcg_gen_op3_i32(INDEX_op_andc_i32, ret, arg1, arg2);
    } else {
        TCGv_i32 t0 = tcg_temp_ebb_new_i32();
        tcg_gen_not_i32(t0, arg2);
        tcg_gen_and_i32(ret, arg1, t0);
        tcg_temp_free_i32(t0);
    }
}

void tcg_gen_eqv_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    if (TCG_TARGET_HAS_eqv_i32) {
        tcg_gen_op3_i32(INDEX_op_eqv_i32, ret, arg1, arg2);
    } else {
        tcg_gen_xor_i32(ret, arg1, arg2);
        tcg_gen_not_i32(ret, ret);
    }
}

void tcg_gen_nand_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    if (TCG_TARGET_HAS_nand_i32) {
        tcg_gen_op3_i32(INDEX_op_nand_i32, ret, arg1, arg2);
    } else {
        tcg_gen_and_i32(ret, arg1, arg2);
        tcg_gen_not_i32(ret, ret);
    }
}

void tcg_gen_nor_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    if (TCG_TARGET_HAS_nor_i32) {
        tcg_gen_op3_i32(INDEX_op_nor_i32, ret, arg1, arg2);
    } else {
        tcg_gen_or_i32(ret, arg1, arg2);
        tcg_gen_not_i32(ret, ret);
    }
}

void tcg_gen_orc_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    if (TCG_TARGET_HAS_orc_i32) {
        tcg_gen_op3_i32(INDEX_op_orc_i32, ret, arg1, arg2);
    } else {
        TCGv_i32 t0 = tcg_temp_ebb_new_i32();
        tcg_gen_not_i32(t0, arg2);
        tcg_gen_or_i32(ret, arg1, t0);
        tcg_temp_free_i32(t0);
    }
}

void tcg_gen_clz_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    if (TCG_TARGET_HAS_clz_i32) {
        tcg_gen_op3_i32(INDEX_op_clz_i32, ret, arg1, arg2);
    } else if (TCG_TARGET_HAS_clz_i64) {
        TCGv_i64 t1 = tcg_temp_ebb_new_i64();
        TCGv_i64 t2 = tcg_temp_ebb_new_i64();
        tcg_gen_extu_i32_i64(t1, arg1);
        tcg_gen_extu_i32_i64(t2, arg2);
        tcg_gen_addi_i64(t2, t2, 32);
        tcg_gen_clz_i64(t1, t1, t2);
        tcg_gen_extrl_i64_i32(ret, t1);
        tcg_temp_free_i64(t1);
        tcg_temp_free_i64(t2);
        tcg_gen_subi_i32(ret, ret, 32);
    } else {
        gen_helper_clz_i32(ret, arg1, arg2);
    }
}

void tcg_gen_clzi_i32(TCGv_i32 ret, TCGv_i32 arg1, uint32_t arg2)
{
    tcg_gen_clz_i32(ret, arg1, tcg_constant_i32(arg2));
}

void tcg_gen_ctz_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    if (TCG_TARGET_HAS_ctz_i32) {
        tcg_gen_op3_i32(INDEX_op_ctz_i32, ret, arg1, arg2);
    } else if (TCG_TARGET_HAS_ctz_i64) {
        TCGv_i64 t1 = tcg_temp_ebb_new_i64();
        TCGv_i64 t2 = tcg_temp_ebb_new_i64();
        tcg_gen_extu_i32_i64(t1, arg1);
        tcg_gen_extu_i32_i64(t2, arg2);
        tcg_gen_ctz_i64(t1, t1, t2);
        tcg_gen_extrl_i64_i32(ret, t1);
        tcg_temp_free_i64(t1);
        tcg_temp_free_i64(t2);
    } else if (TCG_TARGET_HAS_ctpop_i32
               || TCG_TARGET_HAS_ctpop_i64
               || TCG_TARGET_HAS_clz_i32
               || TCG_TARGET_HAS_clz_i64) {
        TCGv_i32 z, t = tcg_temp_ebb_new_i32();

        if (TCG_TARGET_HAS_ctpop_i32 || TCG_TARGET_HAS_ctpop_i64) {
            tcg_gen_subi_i32(t, arg1, 1);
            tcg_gen_andc_i32(t, t, arg1);
            tcg_gen_ctpop_i32(t, t);
        } else {
            /* Since all non-x86 hosts have clz(0) == 32, don't fight it.  */
            tcg_gen_neg_i32(t, arg1);
            tcg_gen_and_i32(t, t, arg1);
            tcg_gen_clzi_i32(t, t, 32);
            tcg_gen_xori_i32(t, t, 31);
        }
        z = tcg_constant_i32(0);
        tcg_gen_movcond_i32(TCG_COND_EQ, ret, arg1, z, arg2, t);
        tcg_temp_free_i32(t);
    } else {
        gen_helper_ctz_i32(ret, arg1, arg2);
    }
}

void tcg_gen_ctzi_i32(TCGv_i32 ret, TCGv_i32 arg1, uint32_t arg2)
{
    if (!TCG_TARGET_HAS_ctz_i32 && TCG_TARGET_HAS_ctpop_i32 && arg2 == 32) {
        /* This equivalence has the advantage of not requiring a fixup.  */
        TCGv_i32 t = tcg_temp_ebb_new_i32();
        tcg_gen_subi_i32(t, arg1, 1);
        tcg_gen_andc_i32(t, t, arg1);
        tcg_gen_ctpop_i32(ret, t);
        tcg_temp_free_i32(t);
    } else {
        tcg_gen_ctz_i32(ret, arg1, tcg_constant_i32(arg2));
    }
}

void tcg_gen_clrsb_i32(TCGv_i32 ret, TCGv_i32 arg)
{
    if (TCG_TARGET_HAS_clz_i32) {
        TCGv_i32 t = tcg_temp_ebb_new_i32();
        tcg_gen_sari_i32(t, arg, 31);
        tcg_gen_xor_i32(t, t, arg);
        tcg_gen_clzi_i32(t, t, 32);
        tcg_gen_subi_i32(ret, t, 1);
        tcg_temp_free_i32(t);
    } else {
        gen_helper_clrsb_i32(ret, arg);
    }
}

void tcg_gen_ctpop_i32(TCGv_i32 ret, TCGv_i32 arg1)
{
    if (TCG_TARGET_HAS_ctpop_i32) {
        tcg_gen_op2_i32(INDEX_op_ctpop_i32, ret, arg1);
    } else if (TCG_TARGET_HAS_ctpop_i64) {
        TCGv_i64 t = tcg_temp_ebb_new_i64();
        tcg_gen_extu_i32_i64(t, arg1);
        tcg_gen_ctpop_i64(t, t);
        tcg_gen_extrl_i64_i32(ret, t);
        tcg_temp_free_i64(t);
    } else {
        gen_helper_ctpop_i32(ret, arg1);
    }
}

void tcg_gen_rotl_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    if (TCG_TARGET_HAS_rot_i32) {
        tcg_gen_op3_i32(INDEX_op_rotl_i32, ret, arg1, arg2);
    } else {
        TCGv_i32 t0, t1;

        t0 = tcg_temp_ebb_new_i32();
        t1 = tcg_temp_ebb_new_i32();
        tcg_gen_shl_i32(t0, arg1, arg2);
        tcg_gen_subfi_i32(t1, 32, arg2);
        tcg_gen_shr_i32(t1, arg1, t1);
        tcg_gen_or_i32(ret, t0, t1);
        tcg_temp_free_i32(t0);
        tcg_temp_free_i32(t1);
    }
}

void tcg_gen_rotli_i32(TCGv_i32 ret, TCGv_i32 arg1, int32_t arg2)
{
    tcg_debug_assert(arg2 >= 0 && arg2 < 32);
    /* some cases can be optimized here */
    if (arg2 == 0) {
        tcg_gen_mov_i32(ret, arg1);
    } else if (TCG_TARGET_HAS_rot_i32) {
        tcg_gen_rotl_i32(ret, arg1, tcg_constant_i32(arg2));
    } else {
        TCGv_i32 t0, t1;
        t0 = tcg_temp_ebb_new_i32();
        t1 = tcg_temp_ebb_new_i32();
        tcg_gen_shli_i32(t0, arg1, arg2);
        tcg_gen_shri_i32(t1, arg1, 32 - arg2);
        tcg_gen_or_i32(ret, t0, t1);
        tcg_temp_free_i32(t0);
        tcg_temp_free_i32(t1);
    }
}

void tcg_gen_rotr_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    if (TCG_TARGET_HAS_rot_i32) {
        tcg_gen_op3_i32(INDEX_op_rotr_i32, ret, arg1, arg2);
    } else {
        TCGv_i32 t0, t1;

        t0 = tcg_temp_ebb_new_i32();
        t1 = tcg_temp_ebb_new_i32();
        tcg_gen_shr_i32(t0, arg1, arg2);
        tcg_gen_subfi_i32(t1, 32, arg2);
        tcg_gen_shl_i32(t1, arg1, t1);
        tcg_gen_or_i32(ret, t0, t1);
        tcg_temp_free_i32(t0);
        tcg_temp_free_i32(t1);
    }
}

void tcg_gen_rotri_i32(TCGv_i32 ret, TCGv_i32 arg1, int32_t arg2)
{
    tcg_debug_assert(arg2 >= 0 && arg2 < 32);
    /* some cases can be optimized here */
    if (arg2 == 0) {
        tcg_gen_mov_i32(ret, arg1);
    } else {
        tcg_gen_rotli_i32(ret, arg1, 32 - arg2);
    }
}

void tcg_gen_deposit_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2,
                         unsigned int ofs, unsigned int len)
{
    uint32_t mask;
    TCGv_i32 t1;

    tcg_debug_assert(ofs < 32);
    tcg_debug_assert(len > 0);
    tcg_debug_assert(len <= 32);
    tcg_debug_assert(ofs + len <= 32);

    if (len == 32) {
        tcg_gen_mov_i32(ret, arg2);
        return;
    }
    if (TCG_TARGET_deposit_valid(TCG_TYPE_I32, ofs, len)) {
        tcg_gen_op5ii_i32(INDEX_op_deposit_i32, ret, arg1, arg2, ofs, len);
        return;
    }

    t1 = tcg_temp_ebb_new_i32();

    if (TCG_TARGET_HAS_extract2_i32) {
        if (ofs + len == 32) {
            tcg_gen_shli_i32(t1, arg1, len);
            tcg_gen_extract2_i32(ret, t1, arg2, len);
            goto done;
        }
        if (ofs == 0) {
            tcg_gen_extract2_i32(ret, arg1, arg2, len);
            tcg_gen_rotli_i32(ret, ret, len);
            goto done;
        }
    }

    mask = (1u << len) - 1;
    if (ofs + len < 32) {
        tcg_gen_andi_i32(t1, arg2, mask);
        tcg_gen_shli_i32(t1, t1, ofs);
    } else {
        tcg_gen_shli_i32(t1, arg2, ofs);
    }
    tcg_gen_andi_i32(ret, arg1, ~(mask << ofs));
    tcg_gen_or_i32(ret, ret, t1);
 done:
    tcg_temp_free_i32(t1);
}

void tcg_gen_deposit_z_i32(TCGv_i32 ret, TCGv_i32 arg,
                           unsigned int ofs, unsigned int len)
{
    tcg_debug_assert(ofs < 32);
    tcg_debug_assert(len > 0);
    tcg_debug_assert(len <= 32);
    tcg_debug_assert(ofs + len <= 32);

    if (ofs + len == 32) {
        tcg_gen_shli_i32(ret, arg, ofs);
    } else if (ofs == 0) {
        tcg_gen_andi_i32(ret, arg, (1u << len) - 1);
    } else if (TCG_TARGET_deposit_valid(TCG_TYPE_I32, ofs, len)) {
        TCGv_i32 zero = tcg_constant_i32(0);
        tcg_gen_op5ii_i32(INDEX_op_deposit_i32, ret, zero, arg, ofs, len);
    } else {
        /* To help two-operand hosts we prefer to zero-extend first,
           which allows ARG to stay live.  */
        switch (len) {
        case 16:
            if (TCG_TARGET_HAS_ext16u_i32) {
                tcg_gen_ext16u_i32(ret, arg);
                tcg_gen_shli_i32(ret, ret, ofs);
                return;
            }
            break;
        case 8:
            if (TCG_TARGET_HAS_ext8u_i32) {
                tcg_gen_ext8u_i32(ret, arg);
                tcg_gen_shli_i32(ret, ret, ofs);
                return;
            }
            break;
        }
        /* Otherwise prefer zero-extension over AND for code size.  */
        switch (ofs + len) {
        case 16:
            if (TCG_TARGET_HAS_ext16u_i32) {
                tcg_gen_shli_i32(ret, arg, ofs);
                tcg_gen_ext16u_i32(ret, ret);
                return;
            }
            break;
        case 8:
            if (TCG_TARGET_HAS_ext8u_i32) {
                tcg_gen_shli_i32(ret, arg, ofs);
                tcg_gen_ext8u_i32(ret, ret);
                return;
            }
            break;
        }
        tcg_gen_andi_i32(ret, arg, (1u << len) - 1);
        tcg_gen_shli_i32(ret, ret, ofs);
    }
}

void tcg_gen_extract_i32(TCGv_i32 ret, TCGv_i32 arg,
                         unsigned int ofs, unsigned int len)
{
    tcg_debug_assert(ofs < 32);
    tcg_debug_assert(len > 0);
    tcg_debug_assert(len <= 32);
    tcg_debug_assert(ofs + len <= 32);

    /* Canonicalize certain special cases, even if extract is supported.  */
    if (ofs + len == 32) {
        tcg_gen_shri_i32(ret, arg, 32 - len);
        return;
    }
    if (ofs == 0) {
        tcg_gen_andi_i32(ret, arg, (1u << len) - 1);
        return;
    }

    if (TCG_TARGET_extract_valid(TCG_TYPE_I32, ofs, len)) {
        tcg_gen_op4ii_i32(INDEX_op_extract_i32, ret, arg, ofs, len);
        return;
    }

    /* Assume that zero-extension, if available, is cheaper than a shift.  */
    switch (ofs + len) {
    case 16:
        if (TCG_TARGET_HAS_ext16u_i32) {
            tcg_gen_ext16u_i32(ret, arg);
            tcg_gen_shri_i32(ret, ret, ofs);
            return;
        }
        break;
    case 8:
        if (TCG_TARGET_HAS_ext8u_i32) {
            tcg_gen_ext8u_i32(ret, arg);
            tcg_gen_shri_i32(ret, ret, ofs);
            return;
        }
        break;
    }

    /* ??? Ideally we'd know what values are available for immediate AND.
       Assume that 8 bits are available, plus the special case of 16,
       so that we get ext8u, ext16u.  */
    switch (len) {
    case 1 ... 8: case 16:
        tcg_gen_shri_i32(ret, arg, ofs);
        tcg_gen_andi_i32(ret, ret, (1u << len) - 1);
        break;
    default:
        tcg_gen_shli_i32(ret, arg, 32 - len - ofs);
        tcg_gen_shri_i32(ret, ret, 32 - len);
        break;
    }
}

void tcg_gen_sextract_i32(TCGv_i32 ret, TCGv_i32 arg,
                          unsigned int ofs, unsigned int len)
{
    tcg_debug_assert(ofs < 32);
    tcg_debug_assert(len > 0);
    tcg_debug_assert(len <= 32);
    tcg_debug_assert(ofs + len <= 32);

    /* Canonicalize certain special cases, even if extract is supported.  */
    if (ofs + len == 32) {
        tcg_gen_sari_i32(ret, arg, 32 - len);
        return;
    }
    if (ofs == 0) {
        switch (len) {
        case 16:
            tcg_gen_ext16s_i32(ret, arg);
            return;
        case 8:
            tcg_gen_ext8s_i32(ret, arg);
            return;
        }
    }

    if (TCG_TARGET_sextract_valid(TCG_TYPE_I32, ofs, len)) {
        tcg_gen_op4ii_i32(INDEX_op_sextract_i32, ret, arg, ofs, len);
        return;
    }

    /* Assume that sign-extension, if available, is cheaper than a shift.  */
    switch (ofs + len) {
    case 16:
        if (TCG_TARGET_HAS_ext16s_i32) {
            tcg_gen_ext16s_i32(ret, arg);
            tcg_gen_sari_i32(ret, ret, ofs);
            return;
        }
        break;
    case 8:
        if (TCG_TARGET_HAS_ext8s_i32) {
            tcg_gen_ext8s_i32(ret, arg);
            tcg_gen_sari_i32(ret, ret, ofs);
            return;
        }
        break;
    }
    switch (len) {
    case 16:
        if (TCG_TARGET_HAS_ext16s_i32) {
            tcg_gen_shri_i32(ret, arg, ofs);
            tcg_gen_ext16s_i32(ret, ret);
            return;
        }
        break;
    case 8:
        if (TCG_TARGET_HAS_ext8s_i32) {
            tcg_gen_shri_i32(ret, arg, ofs);
            tcg_gen_ext8s_i32(ret, ret);
            return;
        }
        break;
    }

    tcg_gen_shli_i32(ret, arg, 32 - len - ofs);
    tcg_gen_sari_i32(ret, ret, 32 - len);
}

/*
 * Extract 32-bits from a 64-bit input, ah:al, starting from ofs.
 * Unlike tcg_gen_extract_i32 above, len is fixed at 32.
 */
void tcg_gen_extract2_i32(TCGv_i32 ret, TCGv_i32 al, TCGv_i32 ah,
                          unsigned int ofs)
{
    tcg_debug_assert(ofs <= 32);
    if (ofs == 0) {
        tcg_gen_mov_i32(ret, al);
    } else if (ofs == 32) {
        tcg_gen_mov_i32(ret, ah);
    } else if (al == ah) {
        tcg_gen_rotri_i32(ret, al, ofs);
    } else if (TCG_TARGET_HAS_extract2_i32) {
        tcg_gen_op4i_i32(INDEX_op_extract2_i32, ret, al, ah, ofs);
    } else {
        TCGv_i32 t0 = tcg_temp_ebb_new_i32();
        tcg_gen_shri_i32(t0, al, ofs);
        tcg_gen_deposit_i32(ret, t0, ah, 32 - ofs, ofs);
        tcg_temp_free_i32(t0);
    }
}

void tcg_gen_movcond_i32(TCGCond cond, TCGv_i32 ret, TCGv_i32 c1,
                         TCGv_i32 c2, TCGv_i32 v1, TCGv_i32 v2)
{
    if (cond == TCG_COND_ALWAYS) {
        tcg_gen_mov_i32(ret, v1);
    } else if (cond == TCG_COND_NEVER) {
        tcg_gen_mov_i32(ret, v2);
    } else {
        tcg_gen_op6i_i32(INDEX_op_movcond_i32, ret, c1, c2, v1, v2, cond);
    }
}

void tcg_gen_add2_i32(TCGv_i32 rl, TCGv_i32 rh, TCGv_i32 al,
                      TCGv_i32 ah, TCGv_i32 bl, TCGv_i32 bh)
{
    if (TCG_TARGET_HAS_add2_i32) {
        tcg_gen_op6_i32(INDEX_op_add2_i32, rl, rh, al, ah, bl, bh);
    } else {
        TCGv_i64 t0 = tcg_temp_ebb_new_i64();
        TCGv_i64 t1 = tcg_temp_ebb_new_i64();
        tcg_gen_concat_i32_i64(t0, al, ah);
        tcg_gen_concat_i32_i64(t1, bl, bh);
        tcg_gen_add_i64(t0, t0, t1);
        tcg_gen_extr_i64_i32(rl, rh, t0);
        tcg_temp_free_i64(t0);
        tcg_temp_free_i64(t1);
    }
}

void tcg_gen_sub2_i32(TCGv_i32 rl, TCGv_i32 rh, TCGv_i32 al,
                      TCGv_i32 ah, TCGv_i32 bl, TCGv_i32 bh)
{
    if (TCG_TARGET_HAS_sub2_i32) {
        tcg_gen_op6_i32(INDEX_op_sub2_i32, rl, rh, al, ah, bl, bh);
    } else {
        TCGv_i64 t0 = tcg_temp_ebb_new_i64();
        TCGv_i64 t1 = tcg_temp_ebb_new_i64();
        tcg_gen_concat_i32_i64(t0, al, ah);
        tcg_gen_concat_i32_i64(t1, bl, bh);
        tcg_gen_sub_i64(t0, t0, t1);
        tcg_gen_extr_i64_i32(rl, rh, t0);
        tcg_temp_free_i64(t0);
        tcg_temp_free_i64(t1);
    }
}

void tcg_gen_mulu2_i32(TCGv_i32 rl, TCGv_i32 rh, TCGv_i32 arg1, TCGv_i32 arg2)
{
    if (TCG_TARGET_HAS_mulu2_i32) {
        tcg_gen_op4_i32(INDEX_op_mulu2_i32, rl, rh, arg1, arg2);
    } else if (TCG_TARGET_HAS_muluh_i32) {
        TCGv_i32 t = tcg_temp_ebb_new_i32();
        tcg_gen_op3_i32(INDEX_op_mul_i32, t, arg1, arg2);
        tcg_gen_op3_i32(INDEX_op_muluh_i32, rh, arg1, arg2);
        tcg_gen_mov_i32(rl, t);
        tcg_temp_free_i32(t);
    } else if (TCG_TARGET_REG_BITS == 64) {
        TCGv_i64 t0 = tcg_temp_ebb_new_i64();
        TCGv_i64 t1 = tcg_temp_ebb_new_i64();
        tcg_gen_extu_i32_i64(t0, arg1);
        tcg_gen_extu_i32_i64(t1, arg2);
        tcg_gen_mul_i64(t0, t0, t1);
        tcg_gen_extr_i64_i32(rl, rh, t0);
        tcg_temp_free_i64(t0);
        tcg_temp_free_i64(t1);
    } else {
        qemu_build_not_reached();
    }
}

void tcg_gen_muls2_i32(TCGv_i32 rl, TCGv_i32 rh, TCGv_i32 arg1, TCGv_i32 arg2)
{
    if (TCG_TARGET_HAS_muls2_i32) {
        tcg_gen_op4_i32(INDEX_op_muls2_i32, rl, rh, arg1, arg2);
    } else if (TCG_TARGET_HAS_mulsh_i32) {
        TCGv_i32 t = tcg_temp_ebb_new_i32();
        tcg_gen_op3_i32(INDEX_op_mul_i32, t, arg1, arg2);
        tcg_gen_op3_i32(INDEX_op_mulsh_i32, rh, arg1, arg2);
        tcg_gen_mov_i32(rl, t);
        tcg_temp_free_i32(t);
    } else if (TCG_TARGET_REG_BITS == 32) {
        TCGv_i32 t0 = tcg_temp_ebb_new_i32();
        TCGv_i32 t1 = tcg_temp_ebb_new_i32();
        TCGv_i32 t2 = tcg_temp_ebb_new_i32();
        TCGv_i32 t3 = tcg_temp_ebb_new_i32();
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
        TCGv_i64 t0 = tcg_temp_ebb_new_i64();
        TCGv_i64 t1 = tcg_temp_ebb_new_i64();
        tcg_gen_ext_i32_i64(t0, arg1);
        tcg_gen_ext_i32_i64(t1, arg2);
        tcg_gen_mul_i64(t0, t0, t1);
        tcg_gen_extr_i64_i32(rl, rh, t0);
        tcg_temp_free_i64(t0);
        tcg_temp_free_i64(t1);
    }
}

void tcg_gen_mulsu2_i32(TCGv_i32 rl, TCGv_i32 rh, TCGv_i32 arg1, TCGv_i32 arg2)
{
    if (TCG_TARGET_REG_BITS == 32) {
        TCGv_i32 t0 = tcg_temp_ebb_new_i32();
        TCGv_i32 t1 = tcg_temp_ebb_new_i32();
        TCGv_i32 t2 = tcg_temp_ebb_new_i32();
        tcg_gen_mulu2_i32(t0, t1, arg1, arg2);
        /* Adjust for negative input for the signed arg1.  */
        tcg_gen_sari_i32(t2, arg1, 31);
        tcg_gen_and_i32(t2, t2, arg2);
        tcg_gen_sub_i32(rh, t1, t2);
        tcg_gen_mov_i32(rl, t0);
        tcg_temp_free_i32(t0);
        tcg_temp_free_i32(t1);
        tcg_temp_free_i32(t2);
    } else {
        TCGv_i64 t0 = tcg_temp_ebb_new_i64();
        TCGv_i64 t1 = tcg_temp_ebb_new_i64();
        tcg_gen_ext_i32_i64(t0, arg1);
        tcg_gen_extu_i32_i64(t1, arg2);
        tcg_gen_mul_i64(t0, t0, t1);
        tcg_gen_extr_i64_i32(rl, rh, t0);
        tcg_temp_free_i64(t0);
        tcg_temp_free_i64(t1);
    }
}

void tcg_gen_ext8s_i32(TCGv_i32 ret, TCGv_i32 arg)
{
    if (TCG_TARGET_HAS_ext8s_i32) {
        tcg_gen_op2_i32(INDEX_op_ext8s_i32, ret, arg);
    } else {
        tcg_gen_shli_i32(ret, arg, 24);
        tcg_gen_sari_i32(ret, ret, 24);
    }
}

void tcg_gen_ext16s_i32(TCGv_i32 ret, TCGv_i32 arg)
{
    if (TCG_TARGET_HAS_ext16s_i32) {
        tcg_gen_op2_i32(INDEX_op_ext16s_i32, ret, arg);
    } else {
        tcg_gen_shli_i32(ret, arg, 16);
        tcg_gen_sari_i32(ret, ret, 16);
    }
}

void tcg_gen_ext8u_i32(TCGv_i32 ret, TCGv_i32 arg)
{
    if (TCG_TARGET_HAS_ext8u_i32) {
        tcg_gen_op2_i32(INDEX_op_ext8u_i32, ret, arg);
    } else {
        tcg_gen_andi_i32(ret, arg, 0xffu);
    }
}

void tcg_gen_ext16u_i32(TCGv_i32 ret, TCGv_i32 arg)
{
    if (TCG_TARGET_HAS_ext16u_i32) {
        tcg_gen_op2_i32(INDEX_op_ext16u_i32, ret, arg);
    } else {
        tcg_gen_andi_i32(ret, arg, 0xffffu);
    }
}

/*
 * bswap16_i32: 16-bit byte swap on the low bits of a 32-bit value.
 *
 * Byte pattern: xxab -> yyba
 *
 * With TCG_BSWAP_IZ, x == zero, else undefined.
 * With TCG_BSWAP_OZ, y == zero, with TCG_BSWAP_OS y == sign, else undefined.
 */
void tcg_gen_bswap16_i32(TCGv_i32 ret, TCGv_i32 arg, int flags)
{
    /* Only one extension flag may be present. */
    tcg_debug_assert(!(flags & TCG_BSWAP_OS) || !(flags & TCG_BSWAP_OZ));

    if (TCG_TARGET_HAS_bswap16_i32) {
        tcg_gen_op3i_i32(INDEX_op_bswap16_i32, ret, arg, flags);
    } else {
        TCGv_i32 t0 = tcg_temp_ebb_new_i32();
        TCGv_i32 t1 = tcg_temp_ebb_new_i32();

                                            /* arg = ..ab (IZ) xxab (!IZ) */
        tcg_gen_shri_i32(t0, arg, 8);       /*  t0 = ...a (IZ) .xxa (!IZ) */
        if (!(flags & TCG_BSWAP_IZ)) {
            tcg_gen_ext8u_i32(t0, t0);      /*  t0 = ...a */
        }

        if (flags & TCG_BSWAP_OS) {
            tcg_gen_shli_i32(t1, arg, 24);  /*  t1 = b... */
            tcg_gen_sari_i32(t1, t1, 16);   /*  t1 = ssb. */
        } else if (flags & TCG_BSWAP_OZ) {
            tcg_gen_ext8u_i32(t1, arg);     /*  t1 = ...b */
            tcg_gen_shli_i32(t1, t1, 8);    /*  t1 = ..b. */
        } else {
            tcg_gen_shli_i32(t1, arg, 8);   /*  t1 = xab. */
        }

        tcg_gen_or_i32(ret, t0, t1);        /* ret = ..ba (OZ) */
                                            /*     = ssba (OS) */
                                            /*     = xaba (no flag) */
        tcg_temp_free_i32(t0);
        tcg_temp_free_i32(t1);
    }
}

/*
 * bswap32_i32: 32-bit byte swap on a 32-bit value.
 *
 * Byte pattern: abcd -> dcba
 */
void tcg_gen_bswap32_i32(TCGv_i32 ret, TCGv_i32 arg)
{
    if (TCG_TARGET_HAS_bswap32_i32) {
        tcg_gen_op3i_i32(INDEX_op_bswap32_i32, ret, arg, 0);
    } else {
        TCGv_i32 t0 = tcg_temp_ebb_new_i32();
        TCGv_i32 t1 = tcg_temp_ebb_new_i32();
        TCGv_i32 t2 = tcg_constant_i32(0x00ff00ff);

                                        /* arg = abcd */
        tcg_gen_shri_i32(t0, arg, 8);   /*  t0 = .abc */
        tcg_gen_and_i32(t1, arg, t2);   /*  t1 = .b.d */
        tcg_gen_and_i32(t0, t0, t2);    /*  t0 = .a.c */
        tcg_gen_shli_i32(t1, t1, 8);    /*  t1 = b.d. */
        tcg_gen_or_i32(ret, t0, t1);    /* ret = badc */

        tcg_gen_shri_i32(t0, ret, 16);  /*  t0 = ..ba */
        tcg_gen_shli_i32(t1, ret, 16);  /*  t1 = dc.. */
        tcg_gen_or_i32(ret, t0, t1);    /* ret = dcba */

        tcg_temp_free_i32(t0);
        tcg_temp_free_i32(t1);
    }
}

/*
 * hswap_i32: Swap 16-bit halfwords within a 32-bit value.
 *
 * Byte pattern: abcd -> cdab
 */
void tcg_gen_hswap_i32(TCGv_i32 ret, TCGv_i32 arg)
{
    /* Swapping 2 16-bit elements is a rotate. */
    tcg_gen_rotli_i32(ret, arg, 16);
}

void tcg_gen_smin_i32(TCGv_i32 ret, TCGv_i32 a, TCGv_i32 b)
{
    tcg_gen_movcond_i32(TCG_COND_LT, ret, a, b, a, b);
}

void tcg_gen_umin_i32(TCGv_i32 ret, TCGv_i32 a, TCGv_i32 b)
{
    tcg_gen_movcond_i32(TCG_COND_LTU, ret, a, b, a, b);
}

void tcg_gen_smax_i32(TCGv_i32 ret, TCGv_i32 a, TCGv_i32 b)
{
    tcg_gen_movcond_i32(TCG_COND_LT, ret, a, b, b, a);
}

void tcg_gen_umax_i32(TCGv_i32 ret, TCGv_i32 a, TCGv_i32 b)
{
    tcg_gen_movcond_i32(TCG_COND_LTU, ret, a, b, b, a);
}

void tcg_gen_abs_i32(TCGv_i32 ret, TCGv_i32 a)
{
    TCGv_i32 t = tcg_temp_ebb_new_i32();

    tcg_gen_sari_i32(t, a, 31);
    tcg_gen_xor_i32(ret, a, t);
    tcg_gen_sub_i32(ret, ret, t);
    tcg_temp_free_i32(t);
}

void tcg_gen_ld8u_i32(TCGv_i32 ret, TCGv_ptr arg2, tcg_target_long offset)
{
    tcg_gen_ldst_op_i32(INDEX_op_ld8u_i32, ret, arg2, offset);
}

void tcg_gen_ld8s_i32(TCGv_i32 ret, TCGv_ptr arg2, tcg_target_long offset)
{
    tcg_gen_ldst_op_i32(INDEX_op_ld8s_i32, ret, arg2, offset);
}

void tcg_gen_ld16u_i32(TCGv_i32 ret, TCGv_ptr arg2, tcg_target_long offset)
{
    tcg_gen_ldst_op_i32(INDEX_op_ld16u_i32, ret, arg2, offset);
}

void tcg_gen_ld16s_i32(TCGv_i32 ret, TCGv_ptr arg2, tcg_target_long offset)
{
    tcg_gen_ldst_op_i32(INDEX_op_ld16s_i32, ret, arg2, offset);
}

void tcg_gen_ld_i32(TCGv_i32 ret, TCGv_ptr arg2, tcg_target_long offset)
{
    tcg_gen_ldst_op_i32(INDEX_op_ld_i32, ret, arg2, offset);
}

void tcg_gen_st8_i32(TCGv_i32 arg1, TCGv_ptr arg2, tcg_target_long offset)
{
    tcg_gen_ldst_op_i32(INDEX_op_st8_i32, arg1, arg2, offset);
}

void tcg_gen_st16_i32(TCGv_i32 arg1, TCGv_ptr arg2, tcg_target_long offset)
{
    tcg_gen_ldst_op_i32(INDEX_op_st16_i32, arg1, arg2, offset);
}

void tcg_gen_st_i32(TCGv_i32 arg1, TCGv_ptr arg2, tcg_target_long offset)
{
    tcg_gen_ldst_op_i32(INDEX_op_st_i32, arg1, arg2, offset);
}


/* 64-bit ops */

void tcg_gen_discard_i64(TCGv_i64 arg)
{
    if (TCG_TARGET_REG_BITS == 64) {
        tcg_gen_op1_i64(INDEX_op_discard, TCG_TYPE_I64, arg);
    } else {
        tcg_gen_discard_i32(TCGV_LOW(arg));
        tcg_gen_discard_i32(TCGV_HIGH(arg));
    }
}

void tcg_gen_mov_i64(TCGv_i64 ret, TCGv_i64 arg)
{
    if (ret == arg) {
        return;
    }
    if (TCG_TARGET_REG_BITS == 64) {
        tcg_gen_op2_i64(INDEX_op_mov_i64, ret, arg);
    } else {
        TCGTemp *ts = tcgv_i64_temp(arg);

        /* Canonicalize TCGv_i64 TEMP_CONST into TCGv_i32 TEMP_CONST. */
        if (ts->kind == TEMP_CONST) {
            tcg_gen_movi_i64(ret, ts->val);
        } else {
            tcg_gen_mov_i32(TCGV_LOW(ret), TCGV_LOW(arg));
            tcg_gen_mov_i32(TCGV_HIGH(ret), TCGV_HIGH(arg));
        }
    }
}

void tcg_gen_movi_i64(TCGv_i64 ret, int64_t arg)
{
    if (TCG_TARGET_REG_BITS == 64) {
        tcg_gen_mov_i64(ret, tcg_constant_i64(arg));
    } else {
        tcg_gen_movi_i32(TCGV_LOW(ret), arg);
        tcg_gen_movi_i32(TCGV_HIGH(ret), arg >> 32);
    }
}

void tcg_gen_ld8u_i64(TCGv_i64 ret, TCGv_ptr arg2, tcg_target_long offset)
{
    if (TCG_TARGET_REG_BITS == 64) {
        tcg_gen_ldst_op_i64(INDEX_op_ld8u_i64, ret, arg2, offset);
    } else {
        tcg_gen_ld8u_i32(TCGV_LOW(ret), arg2, offset);
        tcg_gen_movi_i32(TCGV_HIGH(ret), 0);
    }
}

void tcg_gen_ld8s_i64(TCGv_i64 ret, TCGv_ptr arg2, tcg_target_long offset)
{
    if (TCG_TARGET_REG_BITS == 64) {
        tcg_gen_ldst_op_i64(INDEX_op_ld8s_i64, ret, arg2, offset);
    } else {
        tcg_gen_ld8s_i32(TCGV_LOW(ret), arg2, offset);
        tcg_gen_sari_i32(TCGV_HIGH(ret), TCGV_LOW(ret), 31);
    }
}

void tcg_gen_ld16u_i64(TCGv_i64 ret, TCGv_ptr arg2, tcg_target_long offset)
{
    if (TCG_TARGET_REG_BITS == 64) {
        tcg_gen_ldst_op_i64(INDEX_op_ld16u_i64, ret, arg2, offset);
    } else {
        tcg_gen_ld16u_i32(TCGV_LOW(ret), arg2, offset);
        tcg_gen_movi_i32(TCGV_HIGH(ret), 0);
    }
}

void tcg_gen_ld16s_i64(TCGv_i64 ret, TCGv_ptr arg2, tcg_target_long offset)
{
    if (TCG_TARGET_REG_BITS == 64) {
        tcg_gen_ldst_op_i64(INDEX_op_ld16s_i64, ret, arg2, offset);
    } else {
        tcg_gen_ld16s_i32(TCGV_LOW(ret), arg2, offset);
        tcg_gen_sari_i32(TCGV_HIGH(ret), TCGV_LOW(ret), 31);
    }
}

void tcg_gen_ld32u_i64(TCGv_i64 ret, TCGv_ptr arg2, tcg_target_long offset)
{
    if (TCG_TARGET_REG_BITS == 64) {
        tcg_gen_ldst_op_i64(INDEX_op_ld32u_i64, ret, arg2, offset);
    } else {
        tcg_gen_ld_i32(TCGV_LOW(ret), arg2, offset);
        tcg_gen_movi_i32(TCGV_HIGH(ret), 0);
    }
}

void tcg_gen_ld32s_i64(TCGv_i64 ret, TCGv_ptr arg2, tcg_target_long offset)
{
    if (TCG_TARGET_REG_BITS == 64) {
        tcg_gen_ldst_op_i64(INDEX_op_ld32s_i64, ret, arg2, offset);
    } else {
        tcg_gen_ld_i32(TCGV_LOW(ret), arg2, offset);
        tcg_gen_sari_i32(TCGV_HIGH(ret), TCGV_LOW(ret), 31);
    }
}

void tcg_gen_ld_i64(TCGv_i64 ret, TCGv_ptr arg2, tcg_target_long offset)
{
    /*
     * For 32-bit host, since arg2 and ret have different types,
     * they cannot be the same temporary -- no chance of overlap.
     */
    if (TCG_TARGET_REG_BITS == 64) {
        tcg_gen_ldst_op_i64(INDEX_op_ld_i64, ret, arg2, offset);
    } else if (HOST_BIG_ENDIAN) {
        tcg_gen_ld_i32(TCGV_HIGH(ret), arg2, offset);
        tcg_gen_ld_i32(TCGV_LOW(ret), arg2, offset + 4);
    } else {
        tcg_gen_ld_i32(TCGV_LOW(ret), arg2, offset);
        tcg_gen_ld_i32(TCGV_HIGH(ret), arg2, offset + 4);
    }
}

void tcg_gen_st8_i64(TCGv_i64 arg1, TCGv_ptr arg2, tcg_target_long offset)
{
    if (TCG_TARGET_REG_BITS == 64) {
        tcg_gen_ldst_op_i64(INDEX_op_st8_i64, arg1, arg2, offset);
    } else {
        tcg_gen_st8_i32(TCGV_LOW(arg1), arg2, offset);
    }
}

void tcg_gen_st16_i64(TCGv_i64 arg1, TCGv_ptr arg2, tcg_target_long offset)
{
    if (TCG_TARGET_REG_BITS == 64) {
        tcg_gen_ldst_op_i64(INDEX_op_st16_i64, arg1, arg2, offset);
    } else {
        tcg_gen_st16_i32(TCGV_LOW(arg1), arg2, offset);
    }
}

void tcg_gen_st32_i64(TCGv_i64 arg1, TCGv_ptr arg2, tcg_target_long offset)
{
    if (TCG_TARGET_REG_BITS == 64) {
        tcg_gen_ldst_op_i64(INDEX_op_st32_i64, arg1, arg2, offset);
    } else {
        tcg_gen_st_i32(TCGV_LOW(arg1), arg2, offset);
    }
}

void tcg_gen_st_i64(TCGv_i64 arg1, TCGv_ptr arg2, tcg_target_long offset)
{
    if (TCG_TARGET_REG_BITS == 64) {
        tcg_gen_ldst_op_i64(INDEX_op_st_i64, arg1, arg2, offset);
    } else if (HOST_BIG_ENDIAN) {
        tcg_gen_st_i32(TCGV_HIGH(arg1), arg2, offset);
        tcg_gen_st_i32(TCGV_LOW(arg1), arg2, offset + 4);
    } else {
        tcg_gen_st_i32(TCGV_LOW(arg1), arg2, offset);
        tcg_gen_st_i32(TCGV_HIGH(arg1), arg2, offset + 4);
    }
}

void tcg_gen_add_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    if (TCG_TARGET_REG_BITS == 64) {
        tcg_gen_op3_i64(INDEX_op_add_i64, ret, arg1, arg2);
    } else {
        tcg_gen_add2_i32(TCGV_LOW(ret), TCGV_HIGH(ret), TCGV_LOW(arg1),
                         TCGV_HIGH(arg1), TCGV_LOW(arg2), TCGV_HIGH(arg2));
    }
}

void tcg_gen_sub_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    if (TCG_TARGET_REG_BITS == 64) {
        tcg_gen_op3_i64(INDEX_op_sub_i64, ret, arg1, arg2);
    } else {
        tcg_gen_sub2_i32(TCGV_LOW(ret), TCGV_HIGH(ret), TCGV_LOW(arg1),
                         TCGV_HIGH(arg1), TCGV_LOW(arg2), TCGV_HIGH(arg2));
    }
}

void tcg_gen_and_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    if (TCG_TARGET_REG_BITS == 64) {
        tcg_gen_op3_i64(INDEX_op_and_i64, ret, arg1, arg2);
    } else {
        tcg_gen_and_i32(TCGV_LOW(ret), TCGV_LOW(arg1), TCGV_LOW(arg2));
        tcg_gen_and_i32(TCGV_HIGH(ret), TCGV_HIGH(arg1), TCGV_HIGH(arg2));
    }
}

void tcg_gen_or_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    if (TCG_TARGET_REG_BITS == 64) {
        tcg_gen_op3_i64(INDEX_op_or_i64, ret, arg1, arg2);
    } else {
        tcg_gen_or_i32(TCGV_LOW(ret), TCGV_LOW(arg1), TCGV_LOW(arg2));
        tcg_gen_or_i32(TCGV_HIGH(ret), TCGV_HIGH(arg1), TCGV_HIGH(arg2));
    }
}

void tcg_gen_xor_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    if (TCG_TARGET_REG_BITS == 64) {
        tcg_gen_op3_i64(INDEX_op_xor_i64, ret, arg1, arg2);
    } else {
        tcg_gen_xor_i32(TCGV_LOW(ret), TCGV_LOW(arg1), TCGV_LOW(arg2));
        tcg_gen_xor_i32(TCGV_HIGH(ret), TCGV_HIGH(arg1), TCGV_HIGH(arg2));
    }
}

void tcg_gen_shl_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    if (TCG_TARGET_REG_BITS == 64) {
        tcg_gen_op3_i64(INDEX_op_shl_i64, ret, arg1, arg2);
    } else {
        gen_helper_shl_i64(ret, arg1, arg2);
    }
}

void tcg_gen_shr_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    if (TCG_TARGET_REG_BITS == 64) {
        tcg_gen_op3_i64(INDEX_op_shr_i64, ret, arg1, arg2);
    } else {
        gen_helper_shr_i64(ret, arg1, arg2);
    }
}

void tcg_gen_sar_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    if (TCG_TARGET_REG_BITS == 64) {
        tcg_gen_op3_i64(INDEX_op_sar_i64, ret, arg1, arg2);
    } else {
        gen_helper_sar_i64(ret, arg1, arg2);
    }
}

void tcg_gen_mul_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    TCGv_i64 t0;
    TCGv_i32 t1;

    if (TCG_TARGET_REG_BITS == 64) {
        tcg_gen_op3_i64(INDEX_op_mul_i64, ret, arg1, arg2);
        return;
    }


    t0 = tcg_temp_ebb_new_i64();
    t1 = tcg_temp_ebb_new_i32();

    tcg_gen_mulu2_i32(TCGV_LOW(t0), TCGV_HIGH(t0),
                      TCGV_LOW(arg1), TCGV_LOW(arg2));

    tcg_gen_mul_i32(t1, TCGV_LOW(arg1), TCGV_HIGH(arg2));
    tcg_gen_add_i32(TCGV_HIGH(t0), TCGV_HIGH(t0), t1);
    tcg_gen_mul_i32(t1, TCGV_HIGH(arg1), TCGV_LOW(arg2));
    tcg_gen_add_i32(TCGV_HIGH(t0), TCGV_HIGH(t0), t1);

    tcg_gen_mov_i64(ret, t0);
    tcg_temp_free_i64(t0);
    tcg_temp_free_i32(t1);
}

void tcg_gen_addi_i64(TCGv_i64 ret, TCGv_i64 arg1, int64_t arg2)
{
    /* some cases can be optimized here */
    if (arg2 == 0) {
        tcg_gen_mov_i64(ret, arg1);
    } else if (TCG_TARGET_REG_BITS == 64) {
        tcg_gen_add_i64(ret, arg1, tcg_constant_i64(arg2));
    } else {
        tcg_gen_add2_i32(TCGV_LOW(ret), TCGV_HIGH(ret),
                         TCGV_LOW(arg1), TCGV_HIGH(arg1),
                         tcg_constant_i32(arg2), tcg_constant_i32(arg2 >> 32));
    }
}

void tcg_gen_subfi_i64(TCGv_i64 ret, int64_t arg1, TCGv_i64 arg2)
{
    if (arg1 == 0) {
        tcg_gen_neg_i64(ret, arg2);
    } else if (TCG_TARGET_REG_BITS == 64) {
        tcg_gen_sub_i64(ret, tcg_constant_i64(arg1), arg2);
    } else {
        tcg_gen_sub2_i32(TCGV_LOW(ret), TCGV_HIGH(ret),
                         tcg_constant_i32(arg1), tcg_constant_i32(arg1 >> 32),
                         TCGV_LOW(arg2), TCGV_HIGH(arg2));
    }
}

void tcg_gen_subi_i64(TCGv_i64 ret, TCGv_i64 arg1, int64_t arg2)
{
    tcg_gen_addi_i64(ret, arg1, -arg2);
}

void tcg_gen_neg_i64(TCGv_i64 ret, TCGv_i64 arg)
{
    if (TCG_TARGET_REG_BITS == 64) {
        tcg_gen_op2_i64(INDEX_op_neg_i64, ret, arg);
    } else {
        TCGv_i32 zero = tcg_constant_i32(0);
        tcg_gen_sub2_i32(TCGV_LOW(ret), TCGV_HIGH(ret),
                         zero, zero, TCGV_LOW(arg), TCGV_HIGH(arg));
    }
}

void tcg_gen_andi_i64(TCGv_i64 ret, TCGv_i64 arg1, int64_t arg2)
{
    if (TCG_TARGET_REG_BITS == 32) {
        tcg_gen_andi_i32(TCGV_LOW(ret), TCGV_LOW(arg1), arg2);
        tcg_gen_andi_i32(TCGV_HIGH(ret), TCGV_HIGH(arg1), arg2 >> 32);
        return;
    }

    /* Some cases can be optimized here.  */
    switch (arg2) {
    case 0:
        tcg_gen_movi_i64(ret, 0);
        return;
    case -1:
        tcg_gen_mov_i64(ret, arg1);
        return;
    case 0xff:
        /* Don't recurse with tcg_gen_ext8u_i64.  */
        if (TCG_TARGET_HAS_ext8u_i64) {
            tcg_gen_op2_i64(INDEX_op_ext8u_i64, ret, arg1);
            return;
        }
        break;
    case 0xffff:
        if (TCG_TARGET_HAS_ext16u_i64) {
            tcg_gen_op2_i64(INDEX_op_ext16u_i64, ret, arg1);
            return;
        }
        break;
    case 0xffffffffu:
        if (TCG_TARGET_HAS_ext32u_i64) {
            tcg_gen_op2_i64(INDEX_op_ext32u_i64, ret, arg1);
            return;
        }
        break;
    }

    tcg_gen_and_i64(ret, arg1, tcg_constant_i64(arg2));
}

void tcg_gen_ori_i64(TCGv_i64 ret, TCGv_i64 arg1, int64_t arg2)
{
    if (TCG_TARGET_REG_BITS == 32) {
        tcg_gen_ori_i32(TCGV_LOW(ret), TCGV_LOW(arg1), arg2);
        tcg_gen_ori_i32(TCGV_HIGH(ret), TCGV_HIGH(arg1), arg2 >> 32);
        return;
    }
    /* Some cases can be optimized here.  */
    if (arg2 == -1) {
        tcg_gen_movi_i64(ret, -1);
    } else if (arg2 == 0) {
        tcg_gen_mov_i64(ret, arg1);
    } else {
        tcg_gen_or_i64(ret, arg1, tcg_constant_i64(arg2));
    }
}

void tcg_gen_xori_i64(TCGv_i64 ret, TCGv_i64 arg1, int64_t arg2)
{
    if (TCG_TARGET_REG_BITS == 32) {
        tcg_gen_xori_i32(TCGV_LOW(ret), TCGV_LOW(arg1), arg2);
        tcg_gen_xori_i32(TCGV_HIGH(ret), TCGV_HIGH(arg1), arg2 >> 32);
        return;
    }
    /* Some cases can be optimized here.  */
    if (arg2 == 0) {
        tcg_gen_mov_i64(ret, arg1);
    } else if (arg2 == -1 && TCG_TARGET_HAS_not_i64) {
        /* Don't recurse with tcg_gen_not_i64.  */
        tcg_gen_op2_i64(INDEX_op_not_i64, ret, arg1);
    } else {
        tcg_gen_xor_i64(ret, arg1, tcg_constant_i64(arg2));
    }
}

static inline void tcg_gen_shifti_i64(TCGv_i64 ret, TCGv_i64 arg1,
                                      unsigned c, bool right, bool arith)
{
    tcg_debug_assert(c < 64);
    if (c == 0) {
        tcg_gen_mov_i32(TCGV_LOW(ret), TCGV_LOW(arg1));
        tcg_gen_mov_i32(TCGV_HIGH(ret), TCGV_HIGH(arg1));
    } else if (c >= 32) {
        c -= 32;
        if (right) {
            if (arith) {
                tcg_gen_sari_i32(TCGV_LOW(ret), TCGV_HIGH(arg1), c);
                tcg_gen_sari_i32(TCGV_HIGH(ret), TCGV_HIGH(arg1), 31);
            } else {
                tcg_gen_shri_i32(TCGV_LOW(ret), TCGV_HIGH(arg1), c);
                tcg_gen_movi_i32(TCGV_HIGH(ret), 0);
            }
        } else {
            tcg_gen_shli_i32(TCGV_HIGH(ret), TCGV_LOW(arg1), c);
            tcg_gen_movi_i32(TCGV_LOW(ret), 0);
        }
    } else if (right) {
        if (TCG_TARGET_HAS_extract2_i32) {
            tcg_gen_extract2_i32(TCGV_LOW(ret),
                                 TCGV_LOW(arg1), TCGV_HIGH(arg1), c);
        } else {
            tcg_gen_shri_i32(TCGV_LOW(ret), TCGV_LOW(arg1), c);
            tcg_gen_deposit_i32(TCGV_LOW(ret), TCGV_LOW(ret),
                                TCGV_HIGH(arg1), 32 - c, c);
        }
        if (arith) {
            tcg_gen_sari_i32(TCGV_HIGH(ret), TCGV_HIGH(arg1), c);
        } else {
            tcg_gen_shri_i32(TCGV_HIGH(ret), TCGV_HIGH(arg1), c);
        }
    } else {
        if (TCG_TARGET_HAS_extract2_i32) {
            tcg_gen_extract2_i32(TCGV_HIGH(ret),
                                 TCGV_LOW(arg1), TCGV_HIGH(arg1), 32 - c);
        } else {
            TCGv_i32 t0 = tcg_temp_ebb_new_i32();
            tcg_gen_shri_i32(t0, TCGV_LOW(arg1), 32 - c);
            tcg_gen_deposit_i32(TCGV_HIGH(ret), t0,
                                TCGV_HIGH(arg1), c, 32 - c);
            tcg_temp_free_i32(t0);
        }
        tcg_gen_shli_i32(TCGV_LOW(ret), TCGV_LOW(arg1), c);
    }
}

void tcg_gen_shli_i64(TCGv_i64 ret, TCGv_i64 arg1, int64_t arg2)
{
    tcg_debug_assert(arg2 >= 0 && arg2 < 64);
    if (TCG_TARGET_REG_BITS == 32) {
        tcg_gen_shifti_i64(ret, arg1, arg2, 0, 0);
    } else if (arg2 == 0) {
        tcg_gen_mov_i64(ret, arg1);
    } else {
        tcg_gen_shl_i64(ret, arg1, tcg_constant_i64(arg2));
    }
}

void tcg_gen_shri_i64(TCGv_i64 ret, TCGv_i64 arg1, int64_t arg2)
{
    tcg_debug_assert(arg2 >= 0 && arg2 < 64);
    if (TCG_TARGET_REG_BITS == 32) {
        tcg_gen_shifti_i64(ret, arg1, arg2, 1, 0);
    } else if (arg2 == 0) {
        tcg_gen_mov_i64(ret, arg1);
    } else {
        tcg_gen_shr_i64(ret, arg1, tcg_constant_i64(arg2));
    }
}

void tcg_gen_sari_i64(TCGv_i64 ret, TCGv_i64 arg1, int64_t arg2)
{
    tcg_debug_assert(arg2 >= 0 && arg2 < 64);
    if (TCG_TARGET_REG_BITS == 32) {
        tcg_gen_shifti_i64(ret, arg1, arg2, 1, 1);
    } else if (arg2 == 0) {
        tcg_gen_mov_i64(ret, arg1);
    } else {
        tcg_gen_sar_i64(ret, arg1, tcg_constant_i64(arg2));
    }
}

void tcg_gen_brcond_i64(TCGCond cond, TCGv_i64 arg1, TCGv_i64 arg2, TCGLabel *l)
{
    if (cond == TCG_COND_ALWAYS) {
        tcg_gen_br(l);
    } else if (cond != TCG_COND_NEVER) {
        TCGOp *op;
        if (TCG_TARGET_REG_BITS == 32) {
            op = tcg_gen_op6ii_i32(INDEX_op_brcond2_i32, TCGV_LOW(arg1),
                                   TCGV_HIGH(arg1), TCGV_LOW(arg2),
                                   TCGV_HIGH(arg2), cond, label_arg(l));
        } else {
            op = tcg_gen_op4ii_i64(INDEX_op_brcond_i64, arg1, arg2, cond,
                                   label_arg(l));
        }
        add_as_label_use(l, op);
    }
}

void tcg_gen_brcondi_i64(TCGCond cond, TCGv_i64 arg1, int64_t arg2, TCGLabel *l)
{
    if (TCG_TARGET_REG_BITS == 64) {
        tcg_gen_brcond_i64(cond, arg1, tcg_constant_i64(arg2), l);
    } else if (cond == TCG_COND_ALWAYS) {
        tcg_gen_br(l);
    } else if (cond != TCG_COND_NEVER) {
        TCGOp *op = tcg_gen_op6ii_i32(INDEX_op_brcond2_i32,
                                      TCGV_LOW(arg1), TCGV_HIGH(arg1),
                                      tcg_constant_i32(arg2),
                                      tcg_constant_i32(arg2 >> 32),
                                      cond, label_arg(l));
        add_as_label_use(l, op);
    }
}

void tcg_gen_setcond_i64(TCGCond cond, TCGv_i64 ret,
                         TCGv_i64 arg1, TCGv_i64 arg2)
{
    if (cond == TCG_COND_ALWAYS) {
        tcg_gen_movi_i64(ret, 1);
    } else if (cond == TCG_COND_NEVER) {
        tcg_gen_movi_i64(ret, 0);
    } else {
        if (TCG_TARGET_REG_BITS == 32) {
            tcg_gen_op6i_i32(INDEX_op_setcond2_i32, TCGV_LOW(ret),
                             TCGV_LOW(arg1), TCGV_HIGH(arg1),
                             TCGV_LOW(arg2), TCGV_HIGH(arg2), cond);
            tcg_gen_movi_i32(TCGV_HIGH(ret), 0);
        } else {
            tcg_gen_op4i_i64(INDEX_op_setcond_i64, ret, arg1, arg2, cond);
        }
    }
}

void tcg_gen_setcondi_i64(TCGCond cond, TCGv_i64 ret,
                          TCGv_i64 arg1, int64_t arg2)
{
    if (TCG_TARGET_REG_BITS == 64) {
        tcg_gen_setcond_i64(cond, ret, arg1, tcg_constant_i64(arg2));
    } else if (cond == TCG_COND_ALWAYS) {
        tcg_gen_movi_i64(ret, 1);
    } else if (cond == TCG_COND_NEVER) {
        tcg_gen_movi_i64(ret, 0);
    } else {
        tcg_gen_op6i_i32(INDEX_op_setcond2_i32, TCGV_LOW(ret),
                         TCGV_LOW(arg1), TCGV_HIGH(arg1),
                         tcg_constant_i32(arg2),
                         tcg_constant_i32(arg2 >> 32), cond);
        tcg_gen_movi_i32(TCGV_HIGH(ret), 0);
    }
}

void tcg_gen_negsetcondi_i64(TCGCond cond, TCGv_i64 ret,
                             TCGv_i64 arg1, int64_t arg2)
{
    tcg_gen_negsetcond_i64(cond, ret, arg1, tcg_constant_i64(arg2));
}

void tcg_gen_negsetcond_i64(TCGCond cond, TCGv_i64 ret,
                            TCGv_i64 arg1, TCGv_i64 arg2)
{
    if (cond == TCG_COND_ALWAYS) {
        tcg_gen_movi_i64(ret, -1);
    } else if (cond == TCG_COND_NEVER) {
        tcg_gen_movi_i64(ret, 0);
    } else if (TCG_TARGET_HAS_negsetcond_i64) {
        tcg_gen_op4i_i64(INDEX_op_negsetcond_i64, ret, arg1, arg2, cond);
    } else if (TCG_TARGET_REG_BITS == 32) {
        tcg_gen_op6i_i32(INDEX_op_setcond2_i32, TCGV_LOW(ret),
                         TCGV_LOW(arg1), TCGV_HIGH(arg1),
                         TCGV_LOW(arg2), TCGV_HIGH(arg2), cond);
        tcg_gen_neg_i32(TCGV_LOW(ret), TCGV_LOW(ret));
        tcg_gen_mov_i32(TCGV_HIGH(ret), TCGV_LOW(ret));
    } else {
        tcg_gen_setcond_i64(cond, ret, arg1, arg2);
        tcg_gen_neg_i64(ret, ret);
    }
}

void tcg_gen_muli_i64(TCGv_i64 ret, TCGv_i64 arg1, int64_t arg2)
{
    if (arg2 == 0) {
        tcg_gen_movi_i64(ret, 0);
    } else if (is_power_of_2(arg2)) {
        tcg_gen_shli_i64(ret, arg1, ctz64(arg2));
    } else {
        tcg_gen_mul_i64(ret, arg1, tcg_constant_i64(arg2));
    }
}

void tcg_gen_div_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    if (TCG_TARGET_HAS_div_i64) {
        tcg_gen_op3_i64(INDEX_op_div_i64, ret, arg1, arg2);
    } else if (TCG_TARGET_HAS_div2_i64) {
        TCGv_i64 t0 = tcg_temp_ebb_new_i64();
        tcg_gen_sari_i64(t0, arg1, 63);
        tcg_gen_op5_i64(INDEX_op_div2_i64, ret, t0, arg1, t0, arg2);
        tcg_temp_free_i64(t0);
    } else {
        gen_helper_div_i64(ret, arg1, arg2);
    }
}

void tcg_gen_rem_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    if (TCG_TARGET_HAS_rem_i64) {
        tcg_gen_op3_i64(INDEX_op_rem_i64, ret, arg1, arg2);
    } else if (TCG_TARGET_HAS_div_i64) {
        TCGv_i64 t0 = tcg_temp_ebb_new_i64();
        tcg_gen_op3_i64(INDEX_op_div_i64, t0, arg1, arg2);
        tcg_gen_mul_i64(t0, t0, arg2);
        tcg_gen_sub_i64(ret, arg1, t0);
        tcg_temp_free_i64(t0);
    } else if (TCG_TARGET_HAS_div2_i64) {
        TCGv_i64 t0 = tcg_temp_ebb_new_i64();
        tcg_gen_sari_i64(t0, arg1, 63);
        tcg_gen_op5_i64(INDEX_op_div2_i64, t0, ret, arg1, t0, arg2);
        tcg_temp_free_i64(t0);
    } else {
        gen_helper_rem_i64(ret, arg1, arg2);
    }
}

void tcg_gen_divu_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    if (TCG_TARGET_HAS_div_i64) {
        tcg_gen_op3_i64(INDEX_op_divu_i64, ret, arg1, arg2);
    } else if (TCG_TARGET_HAS_div2_i64) {
        TCGv_i64 t0 = tcg_temp_ebb_new_i64();
        TCGv_i64 zero = tcg_constant_i64(0);
        tcg_gen_op5_i64(INDEX_op_divu2_i64, ret, t0, arg1, zero, arg2);
        tcg_temp_free_i64(t0);
    } else {
        gen_helper_divu_i64(ret, arg1, arg2);
    }
}

void tcg_gen_remu_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    if (TCG_TARGET_HAS_rem_i64) {
        tcg_gen_op3_i64(INDEX_op_remu_i64, ret, arg1, arg2);
    } else if (TCG_TARGET_HAS_div_i64) {
        TCGv_i64 t0 = tcg_temp_ebb_new_i64();
        tcg_gen_op3_i64(INDEX_op_divu_i64, t0, arg1, arg2);
        tcg_gen_mul_i64(t0, t0, arg2);
        tcg_gen_sub_i64(ret, arg1, t0);
        tcg_temp_free_i64(t0);
    } else if (TCG_TARGET_HAS_div2_i64) {
        TCGv_i64 t0 = tcg_temp_ebb_new_i64();
        TCGv_i64 zero = tcg_constant_i64(0);
        tcg_gen_op5_i64(INDEX_op_divu2_i64, t0, ret, arg1, zero, arg2);
        tcg_temp_free_i64(t0);
    } else {
        gen_helper_remu_i64(ret, arg1, arg2);
    }
}

void tcg_gen_ext8s_i64(TCGv_i64 ret, TCGv_i64 arg)
{
    if (TCG_TARGET_REG_BITS == 32) {
        tcg_gen_ext8s_i32(TCGV_LOW(ret), TCGV_LOW(arg));
        tcg_gen_sari_i32(TCGV_HIGH(ret), TCGV_LOW(ret), 31);
    } else if (TCG_TARGET_HAS_ext8s_i64) {
        tcg_gen_op2_i64(INDEX_op_ext8s_i64, ret, arg);
    } else {
        tcg_gen_shli_i64(ret, arg, 56);
        tcg_gen_sari_i64(ret, ret, 56);
    }
}

void tcg_gen_ext16s_i64(TCGv_i64 ret, TCGv_i64 arg)
{
    if (TCG_TARGET_REG_BITS == 32) {
        tcg_gen_ext16s_i32(TCGV_LOW(ret), TCGV_LOW(arg));
        tcg_gen_sari_i32(TCGV_HIGH(ret), TCGV_LOW(ret), 31);
    } else if (TCG_TARGET_HAS_ext16s_i64) {
        tcg_gen_op2_i64(INDEX_op_ext16s_i64, ret, arg);
    } else {
        tcg_gen_shli_i64(ret, arg, 48);
        tcg_gen_sari_i64(ret, ret, 48);
    }
}

void tcg_gen_ext32s_i64(TCGv_i64 ret, TCGv_i64 arg)
{
    if (TCG_TARGET_REG_BITS == 32) {
        tcg_gen_mov_i32(TCGV_LOW(ret), TCGV_LOW(arg));
        tcg_gen_sari_i32(TCGV_HIGH(ret), TCGV_LOW(ret), 31);
    } else if (TCG_TARGET_HAS_ext32s_i64) {
        tcg_gen_op2_i64(INDEX_op_ext32s_i64, ret, arg);
    } else {
        tcg_gen_shli_i64(ret, arg, 32);
        tcg_gen_sari_i64(ret, ret, 32);
    }
}

void tcg_gen_ext8u_i64(TCGv_i64 ret, TCGv_i64 arg)
{
    if (TCG_TARGET_REG_BITS == 32) {
        tcg_gen_ext8u_i32(TCGV_LOW(ret), TCGV_LOW(arg));
        tcg_gen_movi_i32(TCGV_HIGH(ret), 0);
    } else if (TCG_TARGET_HAS_ext8u_i64) {
        tcg_gen_op2_i64(INDEX_op_ext8u_i64, ret, arg);
    } else {
        tcg_gen_andi_i64(ret, arg, 0xffu);
    }
}

void tcg_gen_ext16u_i64(TCGv_i64 ret, TCGv_i64 arg)
{
    if (TCG_TARGET_REG_BITS == 32) {
        tcg_gen_ext16u_i32(TCGV_LOW(ret), TCGV_LOW(arg));
        tcg_gen_movi_i32(TCGV_HIGH(ret), 0);
    } else if (TCG_TARGET_HAS_ext16u_i64) {
        tcg_gen_op2_i64(INDEX_op_ext16u_i64, ret, arg);
    } else {
        tcg_gen_andi_i64(ret, arg, 0xffffu);
    }
}

void tcg_gen_ext32u_i64(TCGv_i64 ret, TCGv_i64 arg)
{
    if (TCG_TARGET_REG_BITS == 32) {
        tcg_gen_mov_i32(TCGV_LOW(ret), TCGV_LOW(arg));
        tcg_gen_movi_i32(TCGV_HIGH(ret), 0);
    } else if (TCG_TARGET_HAS_ext32u_i64) {
        tcg_gen_op2_i64(INDEX_op_ext32u_i64, ret, arg);
    } else {
        tcg_gen_andi_i64(ret, arg, 0xffffffffu);
    }
}

/*
 * bswap16_i64: 16-bit byte swap on the low bits of a 64-bit value.
 *
 * Byte pattern: xxxxxxxxab -> yyyyyyyyba
 *
 * With TCG_BSWAP_IZ, x == zero, else undefined.
 * With TCG_BSWAP_OZ, y == zero, with TCG_BSWAP_OS y == sign, else undefined.
 */
void tcg_gen_bswap16_i64(TCGv_i64 ret, TCGv_i64 arg, int flags)
{
    /* Only one extension flag may be present. */
    tcg_debug_assert(!(flags & TCG_BSWAP_OS) || !(flags & TCG_BSWAP_OZ));

    if (TCG_TARGET_REG_BITS == 32) {
        tcg_gen_bswap16_i32(TCGV_LOW(ret), TCGV_LOW(arg), flags);
        if (flags & TCG_BSWAP_OS) {
            tcg_gen_sari_i32(TCGV_HIGH(ret), TCGV_LOW(ret), 31);
        } else {
            tcg_gen_movi_i32(TCGV_HIGH(ret), 0);
        }
    } else if (TCG_TARGET_HAS_bswap16_i64) {
        tcg_gen_op3i_i64(INDEX_op_bswap16_i64, ret, arg, flags);
    } else {
        TCGv_i64 t0 = tcg_temp_ebb_new_i64();
        TCGv_i64 t1 = tcg_temp_ebb_new_i64();

                                            /* arg = ......ab or xxxxxxab */
        tcg_gen_shri_i64(t0, arg, 8);       /*  t0 = .......a or .xxxxxxa */
        if (!(flags & TCG_BSWAP_IZ)) {
            tcg_gen_ext8u_i64(t0, t0);      /*  t0 = .......a */
        }

        if (flags & TCG_BSWAP_OS) {
            tcg_gen_shli_i64(t1, arg, 56);  /*  t1 = b....... */
            tcg_gen_sari_i64(t1, t1, 48);   /*  t1 = ssssssb. */
        } else if (flags & TCG_BSWAP_OZ) {
            tcg_gen_ext8u_i64(t1, arg);     /*  t1 = .......b */
            tcg_gen_shli_i64(t1, t1, 8);    /*  t1 = ......b. */
        } else {
            tcg_gen_shli_i64(t1, arg, 8);   /*  t1 = xxxxxab. */
        }

        tcg_gen_or_i64(ret, t0, t1);        /* ret = ......ba (OZ) */
                                            /*       ssssssba (OS) */
                                            /*       xxxxxaba (no flag) */
        tcg_temp_free_i64(t0);
        tcg_temp_free_i64(t1);
    }
}

/*
 * bswap32_i64: 32-bit byte swap on the low bits of a 64-bit value.
 *
 * Byte pattern: xxxxabcd -> yyyydcba
 *
 * With TCG_BSWAP_IZ, x == zero, else undefined.
 * With TCG_BSWAP_OZ, y == zero, with TCG_BSWAP_OS y == sign, else undefined.
 */
void tcg_gen_bswap32_i64(TCGv_i64 ret, TCGv_i64 arg, int flags)
{
    /* Only one extension flag may be present. */
    tcg_debug_assert(!(flags & TCG_BSWAP_OS) || !(flags & TCG_BSWAP_OZ));

    if (TCG_TARGET_REG_BITS == 32) {
        tcg_gen_bswap32_i32(TCGV_LOW(ret), TCGV_LOW(arg));
        if (flags & TCG_BSWAP_OS) {
            tcg_gen_sari_i32(TCGV_HIGH(ret), TCGV_LOW(ret), 31);
        } else {
            tcg_gen_movi_i32(TCGV_HIGH(ret), 0);
        }
    } else if (TCG_TARGET_HAS_bswap32_i64) {
        tcg_gen_op3i_i64(INDEX_op_bswap32_i64, ret, arg, flags);
    } else {
        TCGv_i64 t0 = tcg_temp_ebb_new_i64();
        TCGv_i64 t1 = tcg_temp_ebb_new_i64();
        TCGv_i64 t2 = tcg_constant_i64(0x00ff00ff);

                                            /* arg = xxxxabcd */
        tcg_gen_shri_i64(t0, arg, 8);       /*  t0 = .xxxxabc */
        tcg_gen_and_i64(t1, arg, t2);       /*  t1 = .....b.d */
        tcg_gen_and_i64(t0, t0, t2);        /*  t0 = .....a.c */
        tcg_gen_shli_i64(t1, t1, 8);        /*  t1 = ....b.d. */
        tcg_gen_or_i64(ret, t0, t1);        /* ret = ....badc */

        tcg_gen_shli_i64(t1, ret, 48);      /*  t1 = dc...... */
        tcg_gen_shri_i64(t0, ret, 16);      /*  t0 = ......ba */
        if (flags & TCG_BSWAP_OS) {
            tcg_gen_sari_i64(t1, t1, 32);   /*  t1 = ssssdc.. */
        } else {
            tcg_gen_shri_i64(t1, t1, 32);   /*  t1 = ....dc.. */
        }
        tcg_gen_or_i64(ret, t0, t1);        /* ret = ssssdcba (OS) */
                                            /*       ....dcba (else) */

        tcg_temp_free_i64(t0);
        tcg_temp_free_i64(t1);
    }
}

/*
 * bswap64_i64: 64-bit byte swap on a 64-bit value.
 *
 * Byte pattern: abcdefgh -> hgfedcba
 */
void tcg_gen_bswap64_i64(TCGv_i64 ret, TCGv_i64 arg)
{
    if (TCG_TARGET_REG_BITS == 32) {
        TCGv_i32 t0, t1;
        t0 = tcg_temp_ebb_new_i32();
        t1 = tcg_temp_ebb_new_i32();

        tcg_gen_bswap32_i32(t0, TCGV_LOW(arg));
        tcg_gen_bswap32_i32(t1, TCGV_HIGH(arg));
        tcg_gen_mov_i32(TCGV_LOW(ret), t1);
        tcg_gen_mov_i32(TCGV_HIGH(ret), t0);
        tcg_temp_free_i32(t0);
        tcg_temp_free_i32(t1);
    } else if (TCG_TARGET_HAS_bswap64_i64) {
        tcg_gen_op3i_i64(INDEX_op_bswap64_i64, ret, arg, 0);
    } else {
        TCGv_i64 t0 = tcg_temp_ebb_new_i64();
        TCGv_i64 t1 = tcg_temp_ebb_new_i64();
        TCGv_i64 t2 = tcg_temp_ebb_new_i64();

                                        /* arg = abcdefgh */
        tcg_gen_movi_i64(t2, 0x00ff00ff00ff00ffull);
        tcg_gen_shri_i64(t0, arg, 8);   /*  t0 = .abcdefg */
        tcg_gen_and_i64(t1, arg, t2);   /*  t1 = .b.d.f.h */
        tcg_gen_and_i64(t0, t0, t2);    /*  t0 = .a.c.e.g */
        tcg_gen_shli_i64(t1, t1, 8);    /*  t1 = b.d.f.h. */
        tcg_gen_or_i64(ret, t0, t1);    /* ret = badcfehg */

        tcg_gen_movi_i64(t2, 0x0000ffff0000ffffull);
        tcg_gen_shri_i64(t0, ret, 16);  /*  t0 = ..badcfe */
        tcg_gen_and_i64(t1, ret, t2);   /*  t1 = ..dc..hg */
        tcg_gen_and_i64(t0, t0, t2);    /*  t0 = ..ba..fe */
        tcg_gen_shli_i64(t1, t1, 16);   /*  t1 = dc..hg.. */
        tcg_gen_or_i64(ret, t0, t1);    /* ret = dcbahgfe */

        tcg_gen_shri_i64(t0, ret, 32);  /*  t0 = ....dcba */
        tcg_gen_shli_i64(t1, ret, 32);  /*  t1 = hgfe.... */
        tcg_gen_or_i64(ret, t0, t1);    /* ret = hgfedcba */

        tcg_temp_free_i64(t0);
        tcg_temp_free_i64(t1);
        tcg_temp_free_i64(t2);
    }
}

/*
 * hswap_i64: Swap 16-bit halfwords within a 64-bit value.
 * See also include/qemu/bitops.h, hswap64.
 *
 * Byte pattern: abcdefgh -> ghefcdab
 */
void tcg_gen_hswap_i64(TCGv_i64 ret, TCGv_i64 arg)
{
    uint64_t m = 0x0000ffff0000ffffull;
    TCGv_i64 t0 = tcg_temp_ebb_new_i64();
    TCGv_i64 t1 = tcg_temp_ebb_new_i64();

                                        /* arg = abcdefgh */
    tcg_gen_rotli_i64(t1, arg, 32);     /*  t1 = efghabcd */
    tcg_gen_andi_i64(t0, t1, m);        /*  t0 = ..gh..cd */
    tcg_gen_shli_i64(t0, t0, 16);       /*  t0 = gh..cd.. */
    tcg_gen_shri_i64(t1, t1, 16);       /*  t1 = ..efghab */
    tcg_gen_andi_i64(t1, t1, m);        /*  t1 = ..ef..ab */
    tcg_gen_or_i64(ret, t0, t1);        /* ret = ghefcdab */

    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
}

/*
 * wswap_i64: Swap 32-bit words within a 64-bit value.
 *
 * Byte pattern: abcdefgh -> efghabcd
 */
void tcg_gen_wswap_i64(TCGv_i64 ret, TCGv_i64 arg)
{
    /* Swapping 2 32-bit elements is a rotate. */
    tcg_gen_rotli_i64(ret, arg, 32);
}

void tcg_gen_not_i64(TCGv_i64 ret, TCGv_i64 arg)
{
    if (TCG_TARGET_REG_BITS == 32) {
        tcg_gen_not_i32(TCGV_LOW(ret), TCGV_LOW(arg));
        tcg_gen_not_i32(TCGV_HIGH(ret), TCGV_HIGH(arg));
    } else if (TCG_TARGET_HAS_not_i64) {
        tcg_gen_op2_i64(INDEX_op_not_i64, ret, arg);
    } else {
        tcg_gen_xori_i64(ret, arg, -1);
    }
}

void tcg_gen_andc_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    if (TCG_TARGET_REG_BITS == 32) {
        tcg_gen_andc_i32(TCGV_LOW(ret), TCGV_LOW(arg1), TCGV_LOW(arg2));
        tcg_gen_andc_i32(TCGV_HIGH(ret), TCGV_HIGH(arg1), TCGV_HIGH(arg2));
    } else if (TCG_TARGET_HAS_andc_i64) {
        tcg_gen_op3_i64(INDEX_op_andc_i64, ret, arg1, arg2);
    } else {
        TCGv_i64 t0 = tcg_temp_ebb_new_i64();
        tcg_gen_not_i64(t0, arg2);
        tcg_gen_and_i64(ret, arg1, t0);
        tcg_temp_free_i64(t0);
    }
}

void tcg_gen_eqv_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    if (TCG_TARGET_REG_BITS == 32) {
        tcg_gen_eqv_i32(TCGV_LOW(ret), TCGV_LOW(arg1), TCGV_LOW(arg2));
        tcg_gen_eqv_i32(TCGV_HIGH(ret), TCGV_HIGH(arg1), TCGV_HIGH(arg2));
    } else if (TCG_TARGET_HAS_eqv_i64) {
        tcg_gen_op3_i64(INDEX_op_eqv_i64, ret, arg1, arg2);
    } else {
        tcg_gen_xor_i64(ret, arg1, arg2);
        tcg_gen_not_i64(ret, ret);
    }
}

void tcg_gen_nand_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    if (TCG_TARGET_REG_BITS == 32) {
        tcg_gen_nand_i32(TCGV_LOW(ret), TCGV_LOW(arg1), TCGV_LOW(arg2));
        tcg_gen_nand_i32(TCGV_HIGH(ret), TCGV_HIGH(arg1), TCGV_HIGH(arg2));
    } else if (TCG_TARGET_HAS_nand_i64) {
        tcg_gen_op3_i64(INDEX_op_nand_i64, ret, arg1, arg2);
    } else {
        tcg_gen_and_i64(ret, arg1, arg2);
        tcg_gen_not_i64(ret, ret);
    }
}

void tcg_gen_nor_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    if (TCG_TARGET_REG_BITS == 32) {
        tcg_gen_nor_i32(TCGV_LOW(ret), TCGV_LOW(arg1), TCGV_LOW(arg2));
        tcg_gen_nor_i32(TCGV_HIGH(ret), TCGV_HIGH(arg1), TCGV_HIGH(arg2));
    } else if (TCG_TARGET_HAS_nor_i64) {
        tcg_gen_op3_i64(INDEX_op_nor_i64, ret, arg1, arg2);
    } else {
        tcg_gen_or_i64(ret, arg1, arg2);
        tcg_gen_not_i64(ret, ret);
    }
}

void tcg_gen_orc_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    if (TCG_TARGET_REG_BITS == 32) {
        tcg_gen_orc_i32(TCGV_LOW(ret), TCGV_LOW(arg1), TCGV_LOW(arg2));
        tcg_gen_orc_i32(TCGV_HIGH(ret), TCGV_HIGH(arg1), TCGV_HIGH(arg2));
    } else if (TCG_TARGET_HAS_orc_i64) {
        tcg_gen_op3_i64(INDEX_op_orc_i64, ret, arg1, arg2);
    } else {
        TCGv_i64 t0 = tcg_temp_ebb_new_i64();
        tcg_gen_not_i64(t0, arg2);
        tcg_gen_or_i64(ret, arg1, t0);
        tcg_temp_free_i64(t0);
    }
}

void tcg_gen_clz_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    if (TCG_TARGET_HAS_clz_i64) {
        tcg_gen_op3_i64(INDEX_op_clz_i64, ret, arg1, arg2);
    } else {
        gen_helper_clz_i64(ret, arg1, arg2);
    }
}

void tcg_gen_clzi_i64(TCGv_i64 ret, TCGv_i64 arg1, uint64_t arg2)
{
    if (TCG_TARGET_REG_BITS == 32
        && TCG_TARGET_HAS_clz_i32
        && arg2 <= 0xffffffffu) {
        TCGv_i32 t = tcg_temp_ebb_new_i32();
        tcg_gen_clzi_i32(t, TCGV_LOW(arg1), arg2 - 32);
        tcg_gen_addi_i32(t, t, 32);
        tcg_gen_clz_i32(TCGV_LOW(ret), TCGV_HIGH(arg1), t);
        tcg_gen_movi_i32(TCGV_HIGH(ret), 0);
        tcg_temp_free_i32(t);
    } else {
        tcg_gen_clz_i64(ret, arg1, tcg_constant_i64(arg2));
    }
}

void tcg_gen_ctz_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    if (TCG_TARGET_HAS_ctz_i64) {
        tcg_gen_op3_i64(INDEX_op_ctz_i64, ret, arg1, arg2);
    } else if (TCG_TARGET_HAS_ctpop_i64 || TCG_TARGET_HAS_clz_i64) {
        TCGv_i64 z, t = tcg_temp_ebb_new_i64();

        if (TCG_TARGET_HAS_ctpop_i64) {
            tcg_gen_subi_i64(t, arg1, 1);
            tcg_gen_andc_i64(t, t, arg1);
            tcg_gen_ctpop_i64(t, t);
        } else {
            /* Since all non-x86 hosts have clz(0) == 64, don't fight it.  */
            tcg_gen_neg_i64(t, arg1);
            tcg_gen_and_i64(t, t, arg1);
            tcg_gen_clzi_i64(t, t, 64);
            tcg_gen_xori_i64(t, t, 63);
        }
        z = tcg_constant_i64(0);
        tcg_gen_movcond_i64(TCG_COND_EQ, ret, arg1, z, arg2, t);
        tcg_temp_free_i64(t);
        tcg_temp_free_i64(z);
    } else {
        gen_helper_ctz_i64(ret, arg1, arg2);
    }
}

void tcg_gen_ctzi_i64(TCGv_i64 ret, TCGv_i64 arg1, uint64_t arg2)
{
    if (TCG_TARGET_REG_BITS == 32
        && TCG_TARGET_HAS_ctz_i32
        && arg2 <= 0xffffffffu) {
        TCGv_i32 t32 = tcg_temp_ebb_new_i32();
        tcg_gen_ctzi_i32(t32, TCGV_HIGH(arg1), arg2 - 32);
        tcg_gen_addi_i32(t32, t32, 32);
        tcg_gen_ctz_i32(TCGV_LOW(ret), TCGV_LOW(arg1), t32);
        tcg_gen_movi_i32(TCGV_HIGH(ret), 0);
        tcg_temp_free_i32(t32);
    } else if (!TCG_TARGET_HAS_ctz_i64
               && TCG_TARGET_HAS_ctpop_i64
               && arg2 == 64) {
        /* This equivalence has the advantage of not requiring a fixup.  */
        TCGv_i64 t = tcg_temp_ebb_new_i64();
        tcg_gen_subi_i64(t, arg1, 1);
        tcg_gen_andc_i64(t, t, arg1);
        tcg_gen_ctpop_i64(ret, t);
        tcg_temp_free_i64(t);
    } else {
        tcg_gen_ctz_i64(ret, arg1, tcg_constant_i64(arg2));
    }
}

void tcg_gen_clrsb_i64(TCGv_i64 ret, TCGv_i64 arg)
{
    if (TCG_TARGET_HAS_clz_i64 || TCG_TARGET_HAS_clz_i32) {
        TCGv_i64 t = tcg_temp_ebb_new_i64();
        tcg_gen_sari_i64(t, arg, 63);
        tcg_gen_xor_i64(t, t, arg);
        tcg_gen_clzi_i64(t, t, 64);
        tcg_gen_subi_i64(ret, t, 1);
        tcg_temp_free_i64(t);
    } else {
        gen_helper_clrsb_i64(ret, arg);
    }
}

void tcg_gen_ctpop_i64(TCGv_i64 ret, TCGv_i64 arg1)
{
    if (TCG_TARGET_HAS_ctpop_i64) {
        tcg_gen_op2_i64(INDEX_op_ctpop_i64, ret, arg1);
    } else if (TCG_TARGET_REG_BITS == 32 && TCG_TARGET_HAS_ctpop_i32) {
        tcg_gen_ctpop_i32(TCGV_HIGH(ret), TCGV_HIGH(arg1));
        tcg_gen_ctpop_i32(TCGV_LOW(ret), TCGV_LOW(arg1));
        tcg_gen_add_i32(TCGV_LOW(ret), TCGV_LOW(ret), TCGV_HIGH(ret));
        tcg_gen_movi_i32(TCGV_HIGH(ret), 0);
    } else {
        gen_helper_ctpop_i64(ret, arg1);
    }
}

void tcg_gen_rotl_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    if (TCG_TARGET_HAS_rot_i64) {
        tcg_gen_op3_i64(INDEX_op_rotl_i64, ret, arg1, arg2);
    } else {
        TCGv_i64 t0, t1;
        t0 = tcg_temp_ebb_new_i64();
        t1 = tcg_temp_ebb_new_i64();
        tcg_gen_shl_i64(t0, arg1, arg2);
        tcg_gen_subfi_i64(t1, 64, arg2);
        tcg_gen_shr_i64(t1, arg1, t1);
        tcg_gen_or_i64(ret, t0, t1);
        tcg_temp_free_i64(t0);
        tcg_temp_free_i64(t1);
    }
}

void tcg_gen_rotli_i64(TCGv_i64 ret, TCGv_i64 arg1, int64_t arg2)
{
    tcg_debug_assert(arg2 >= 0 && arg2 < 64);
    /* some cases can be optimized here */
    if (arg2 == 0) {
        tcg_gen_mov_i64(ret, arg1);
    } else if (TCG_TARGET_HAS_rot_i64) {
        tcg_gen_rotl_i64(ret, arg1, tcg_constant_i64(arg2));
    } else {
        TCGv_i64 t0, t1;
        t0 = tcg_temp_ebb_new_i64();
        t1 = tcg_temp_ebb_new_i64();
        tcg_gen_shli_i64(t0, arg1, arg2);
        tcg_gen_shri_i64(t1, arg1, 64 - arg2);
        tcg_gen_or_i64(ret, t0, t1);
        tcg_temp_free_i64(t0);
        tcg_temp_free_i64(t1);
    }
}

void tcg_gen_rotr_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    if (TCG_TARGET_HAS_rot_i64) {
        tcg_gen_op3_i64(INDEX_op_rotr_i64, ret, arg1, arg2);
    } else {
        TCGv_i64 t0, t1;
        t0 = tcg_temp_ebb_new_i64();
        t1 = tcg_temp_ebb_new_i64();
        tcg_gen_shr_i64(t0, arg1, arg2);
        tcg_gen_subfi_i64(t1, 64, arg2);
        tcg_gen_shl_i64(t1, arg1, t1);
        tcg_gen_or_i64(ret, t0, t1);
        tcg_temp_free_i64(t0);
        tcg_temp_free_i64(t1);
    }
}

void tcg_gen_rotri_i64(TCGv_i64 ret, TCGv_i64 arg1, int64_t arg2)
{
    tcg_debug_assert(arg2 >= 0 && arg2 < 64);
    /* some cases can be optimized here */
    if (arg2 == 0) {
        tcg_gen_mov_i64(ret, arg1);
    } else {
        tcg_gen_rotli_i64(ret, arg1, 64 - arg2);
    }
}

void tcg_gen_deposit_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2,
                         unsigned int ofs, unsigned int len)
{
    uint64_t mask;
    TCGv_i64 t1;

    tcg_debug_assert(ofs < 64);
    tcg_debug_assert(len > 0);
    tcg_debug_assert(len <= 64);
    tcg_debug_assert(ofs + len <= 64);

    if (len == 64) {
        tcg_gen_mov_i64(ret, arg2);
        return;
    }

    if (TCG_TARGET_REG_BITS == 64) {
        if (TCG_TARGET_deposit_valid(TCG_TYPE_I64, ofs, len)) {
            tcg_gen_op5ii_i64(INDEX_op_deposit_i64, ret, arg1, arg2, ofs, len);
            return;
        }
    } else {
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
    }

    t1 = tcg_temp_ebb_new_i64();

    if (TCG_TARGET_HAS_extract2_i64) {
        if (ofs + len == 64) {
            tcg_gen_shli_i64(t1, arg1, len);
            tcg_gen_extract2_i64(ret, t1, arg2, len);
            goto done;
        }
        if (ofs == 0) {
            tcg_gen_extract2_i64(ret, arg1, arg2, len);
            tcg_gen_rotli_i64(ret, ret, len);
            goto done;
        }
    }

    mask = (1ull << len) - 1;
    if (ofs + len < 64) {
        tcg_gen_andi_i64(t1, arg2, mask);
        tcg_gen_shli_i64(t1, t1, ofs);
    } else {
        tcg_gen_shli_i64(t1, arg2, ofs);
    }
    tcg_gen_andi_i64(ret, arg1, ~(mask << ofs));
    tcg_gen_or_i64(ret, ret, t1);
 done:
    tcg_temp_free_i64(t1);
}

void tcg_gen_deposit_z_i64(TCGv_i64 ret, TCGv_i64 arg,
                           unsigned int ofs, unsigned int len)
{
    tcg_debug_assert(ofs < 64);
    tcg_debug_assert(len > 0);
    tcg_debug_assert(len <= 64);
    tcg_debug_assert(ofs + len <= 64);

    if (ofs + len == 64) {
        tcg_gen_shli_i64(ret, arg, ofs);
    } else if (ofs == 0) {
        tcg_gen_andi_i64(ret, arg, (1ull << len) - 1);
    } else if (TCG_TARGET_REG_BITS == 64 &&
               TCG_TARGET_deposit_valid(TCG_TYPE_I64, ofs, len)) {
        TCGv_i64 zero = tcg_constant_i64(0);
        tcg_gen_op5ii_i64(INDEX_op_deposit_i64, ret, zero, arg, ofs, len);
    } else {
        if (TCG_TARGET_REG_BITS == 32) {
            if (ofs >= 32) {
                tcg_gen_deposit_z_i32(TCGV_HIGH(ret), TCGV_LOW(arg),
                                      ofs - 32, len);
                tcg_gen_movi_i32(TCGV_LOW(ret), 0);
                return;
            }
            if (ofs + len <= 32) {
                tcg_gen_deposit_z_i32(TCGV_LOW(ret), TCGV_LOW(arg), ofs, len);
                tcg_gen_movi_i32(TCGV_HIGH(ret), 0);
                return;
            }
        }
        /* To help two-operand hosts we prefer to zero-extend first,
           which allows ARG to stay live.  */
        switch (len) {
        case 32:
            if (TCG_TARGET_HAS_ext32u_i64) {
                tcg_gen_ext32u_i64(ret, arg);
                tcg_gen_shli_i64(ret, ret, ofs);
                return;
            }
            break;
        case 16:
            if (TCG_TARGET_HAS_ext16u_i64) {
                tcg_gen_ext16u_i64(ret, arg);
                tcg_gen_shli_i64(ret, ret, ofs);
                return;
            }
            break;
        case 8:
            if (TCG_TARGET_HAS_ext8u_i64) {
                tcg_gen_ext8u_i64(ret, arg);
                tcg_gen_shli_i64(ret, ret, ofs);
                return;
            }
            break;
        }
        /* Otherwise prefer zero-extension over AND for code size.  */
        switch (ofs + len) {
        case 32:
            if (TCG_TARGET_HAS_ext32u_i64) {
                tcg_gen_shli_i64(ret, arg, ofs);
                tcg_gen_ext32u_i64(ret, ret);
                return;
            }
            break;
        case 16:
            if (TCG_TARGET_HAS_ext16u_i64) {
                tcg_gen_shli_i64(ret, arg, ofs);
                tcg_gen_ext16u_i64(ret, ret);
                return;
            }
            break;
        case 8:
            if (TCG_TARGET_HAS_ext8u_i64) {
                tcg_gen_shli_i64(ret, arg, ofs);
                tcg_gen_ext8u_i64(ret, ret);
                return;
            }
            break;
        }
        tcg_gen_andi_i64(ret, arg, (1ull << len) - 1);
        tcg_gen_shli_i64(ret, ret, ofs);
    }
}

void tcg_gen_extract_i64(TCGv_i64 ret, TCGv_i64 arg,
                         unsigned int ofs, unsigned int len)
{
    tcg_debug_assert(ofs < 64);
    tcg_debug_assert(len > 0);
    tcg_debug_assert(len <= 64);
    tcg_debug_assert(ofs + len <= 64);

    /* Canonicalize certain special cases, even if extract is supported.  */
    if (ofs + len == 64) {
        tcg_gen_shri_i64(ret, arg, 64 - len);
        return;
    }
    if (ofs == 0) {
        tcg_gen_andi_i64(ret, arg, (1ull << len) - 1);
        return;
    }

    if (TCG_TARGET_REG_BITS == 32) {
        /* Look for a 32-bit extract within one of the two words.  */
        if (ofs >= 32) {
            tcg_gen_extract_i32(TCGV_LOW(ret), TCGV_HIGH(arg), ofs - 32, len);
            tcg_gen_movi_i32(TCGV_HIGH(ret), 0);
            return;
        }
        if (ofs + len <= 32) {
            tcg_gen_extract_i32(TCGV_LOW(ret), TCGV_LOW(arg), ofs, len);
            tcg_gen_movi_i32(TCGV_HIGH(ret), 0);
            return;
        }
        /* The field is split across two words.  One double-word
           shift is better than two double-word shifts.  */
        goto do_shift_and;
    }

    if (TCG_TARGET_extract_valid(TCG_TYPE_I64, ofs, len)) {
        tcg_gen_op4ii_i64(INDEX_op_extract_i64, ret, arg, ofs, len);
        return;
    }

    /* Assume that zero-extension, if available, is cheaper than a shift.  */
    switch (ofs + len) {
    case 32:
        if (TCG_TARGET_HAS_ext32u_i64) {
            tcg_gen_ext32u_i64(ret, arg);
            tcg_gen_shri_i64(ret, ret, ofs);
            return;
        }
        break;
    case 16:
        if (TCG_TARGET_HAS_ext16u_i64) {
            tcg_gen_ext16u_i64(ret, arg);
            tcg_gen_shri_i64(ret, ret, ofs);
            return;
        }
        break;
    case 8:
        if (TCG_TARGET_HAS_ext8u_i64) {
            tcg_gen_ext8u_i64(ret, arg);
            tcg_gen_shri_i64(ret, ret, ofs);
            return;
        }
        break;
    }

    /* ??? Ideally we'd know what values are available for immediate AND.
       Assume that 8 bits are available, plus the special cases of 16 and 32,
       so that we get ext8u, ext16u, and ext32u.  */
    switch (len) {
    case 1 ... 8: case 16: case 32:
    do_shift_and:
        tcg_gen_shri_i64(ret, arg, ofs);
        tcg_gen_andi_i64(ret, ret, (1ull << len) - 1);
        break;
    default:
        tcg_gen_shli_i64(ret, arg, 64 - len - ofs);
        tcg_gen_shri_i64(ret, ret, 64 - len);
        break;
    }
}

void tcg_gen_sextract_i64(TCGv_i64 ret, TCGv_i64 arg,
                          unsigned int ofs, unsigned int len)
{
    tcg_debug_assert(ofs < 64);
    tcg_debug_assert(len > 0);
    tcg_debug_assert(len <= 64);
    tcg_debug_assert(ofs + len <= 64);

    /* Canonicalize certain special cases, even if sextract is supported.  */
    if (ofs + len == 64) {
        tcg_gen_sari_i64(ret, arg, 64 - len);
        return;
    }
    if (ofs == 0) {
        switch (len) {
        case 32:
            tcg_gen_ext32s_i64(ret, arg);
            return;
        case 16:
            tcg_gen_ext16s_i64(ret, arg);
            return;
        case 8:
            tcg_gen_ext8s_i64(ret, arg);
            return;
        }
    }

    if (TCG_TARGET_REG_BITS == 32) {
        /* Look for a 32-bit extract within one of the two words.  */
        if (ofs >= 32) {
            tcg_gen_sextract_i32(TCGV_LOW(ret), TCGV_HIGH(arg), ofs - 32, len);
        } else if (ofs + len <= 32) {
            tcg_gen_sextract_i32(TCGV_LOW(ret), TCGV_LOW(arg), ofs, len);
        } else if (ofs == 0) {
            tcg_gen_mov_i32(TCGV_LOW(ret), TCGV_LOW(arg));
            tcg_gen_sextract_i32(TCGV_HIGH(ret), TCGV_HIGH(arg), 0, len - 32);
            return;
        } else if (len > 32) {
            TCGv_i32 t = tcg_temp_ebb_new_i32();
            /* Extract the bits for the high word normally.  */
            tcg_gen_sextract_i32(t, TCGV_HIGH(arg), ofs + 32, len - 32);
            /* Shift the field down for the low part.  */
            tcg_gen_shri_i64(ret, arg, ofs);
            /* Overwrite the shift into the high part.  */
            tcg_gen_mov_i32(TCGV_HIGH(ret), t);
            tcg_temp_free_i32(t);
            return;
        } else {
            /* Shift the field down for the low part, such that the
               field sits at the MSB.  */
            tcg_gen_shri_i64(ret, arg, ofs + len - 32);
            /* Shift the field down from the MSB, sign extending.  */
            tcg_gen_sari_i32(TCGV_LOW(ret), TCGV_LOW(ret), 32 - len);
        }
        /* Sign-extend the field from 32 bits.  */
        tcg_gen_sari_i32(TCGV_HIGH(ret), TCGV_LOW(ret), 31);
        return;
    }

    if (TCG_TARGET_sextract_valid(TCG_TYPE_I64, ofs, len)) {
        tcg_gen_op4ii_i64(INDEX_op_sextract_i64, ret, arg, ofs, len);
        return;
    }

    /* Assume that sign-extension, if available, is cheaper than a shift.  */
    switch (ofs + len) {
    case 32:
        if (TCG_TARGET_HAS_ext32s_i64) {
            tcg_gen_ext32s_i64(ret, arg);
            tcg_gen_sari_i64(ret, ret, ofs);
            return;
        }
        break;
    case 16:
        if (TCG_TARGET_HAS_ext16s_i64) {
            tcg_gen_ext16s_i64(ret, arg);
            tcg_gen_sari_i64(ret, ret, ofs);
            return;
        }
        break;
    case 8:
        if (TCG_TARGET_HAS_ext8s_i64) {
            tcg_gen_ext8s_i64(ret, arg);
            tcg_gen_sari_i64(ret, ret, ofs);
            return;
        }
        break;
    }
    switch (len) {
    case 32:
        if (TCG_TARGET_HAS_ext32s_i64) {
            tcg_gen_shri_i64(ret, arg, ofs);
            tcg_gen_ext32s_i64(ret, ret);
            return;
        }
        break;
    case 16:
        if (TCG_TARGET_HAS_ext16s_i64) {
            tcg_gen_shri_i64(ret, arg, ofs);
            tcg_gen_ext16s_i64(ret, ret);
            return;
        }
        break;
    case 8:
        if (TCG_TARGET_HAS_ext8s_i64) {
            tcg_gen_shri_i64(ret, arg, ofs);
            tcg_gen_ext8s_i64(ret, ret);
            return;
        }
        break;
    }
    tcg_gen_shli_i64(ret, arg, 64 - len - ofs);
    tcg_gen_sari_i64(ret, ret, 64 - len);
}

/*
 * Extract 64 bits from a 128-bit input, ah:al, starting from ofs.
 * Unlike tcg_gen_extract_i64 above, len is fixed at 64.
 */
void tcg_gen_extract2_i64(TCGv_i64 ret, TCGv_i64 al, TCGv_i64 ah,
                          unsigned int ofs)
{
    tcg_debug_assert(ofs <= 64);
    if (ofs == 0) {
        tcg_gen_mov_i64(ret, al);
    } else if (ofs == 64) {
        tcg_gen_mov_i64(ret, ah);
    } else if (al == ah) {
        tcg_gen_rotri_i64(ret, al, ofs);
    } else if (TCG_TARGET_HAS_extract2_i64) {
        tcg_gen_op4i_i64(INDEX_op_extract2_i64, ret, al, ah, ofs);
    } else {
        TCGv_i64 t0 = tcg_temp_ebb_new_i64();
        tcg_gen_shri_i64(t0, al, ofs);
        tcg_gen_deposit_i64(ret, t0, ah, 64 - ofs, ofs);
        tcg_temp_free_i64(t0);
    }
}

void tcg_gen_movcond_i64(TCGCond cond, TCGv_i64 ret, TCGv_i64 c1,
                         TCGv_i64 c2, TCGv_i64 v1, TCGv_i64 v2)
{
    if (cond == TCG_COND_ALWAYS) {
        tcg_gen_mov_i64(ret, v1);
    } else if (cond == TCG_COND_NEVER) {
        tcg_gen_mov_i64(ret, v2);
    } else if (TCG_TARGET_REG_BITS == 64) {
        tcg_gen_op6i_i64(INDEX_op_movcond_i64, ret, c1, c2, v1, v2, cond);
    } else {
        TCGv_i32 t0 = tcg_temp_ebb_new_i32();
        TCGv_i32 zero = tcg_constant_i32(0);

        tcg_gen_op6i_i32(INDEX_op_setcond2_i32, t0,
                         TCGV_LOW(c1), TCGV_HIGH(c1),
                         TCGV_LOW(c2), TCGV_HIGH(c2), cond);

        tcg_gen_movcond_i32(TCG_COND_NE, TCGV_LOW(ret), t0, zero,
                            TCGV_LOW(v1), TCGV_LOW(v2));
        tcg_gen_movcond_i32(TCG_COND_NE, TCGV_HIGH(ret), t0, zero,
                            TCGV_HIGH(v1), TCGV_HIGH(v2));

        tcg_temp_free_i32(t0);
    }
}

void tcg_gen_add2_i64(TCGv_i64 rl, TCGv_i64 rh, TCGv_i64 al,
                      TCGv_i64 ah, TCGv_i64 bl, TCGv_i64 bh)
{
    if (TCG_TARGET_HAS_add2_i64) {
        tcg_gen_op6_i64(INDEX_op_add2_i64, rl, rh, al, ah, bl, bh);
    } else {
        TCGv_i64 t0 = tcg_temp_ebb_new_i64();
        TCGv_i64 t1 = tcg_temp_ebb_new_i64();
        tcg_gen_add_i64(t0, al, bl);
        tcg_gen_setcond_i64(TCG_COND_LTU, t1, t0, al);
        tcg_gen_add_i64(rh, ah, bh);
        tcg_gen_add_i64(rh, rh, t1);
        tcg_gen_mov_i64(rl, t0);
        tcg_temp_free_i64(t0);
        tcg_temp_free_i64(t1);
    }
}

void tcg_gen_sub2_i64(TCGv_i64 rl, TCGv_i64 rh, TCGv_i64 al,
                      TCGv_i64 ah, TCGv_i64 bl, TCGv_i64 bh)
{
    if (TCG_TARGET_HAS_sub2_i64) {
        tcg_gen_op6_i64(INDEX_op_sub2_i64, rl, rh, al, ah, bl, bh);
    } else {
        TCGv_i64 t0 = tcg_temp_ebb_new_i64();
        TCGv_i64 t1 = tcg_temp_ebb_new_i64();
        tcg_gen_sub_i64(t0, al, bl);
        tcg_gen_setcond_i64(TCG_COND_LTU, t1, al, bl);
        tcg_gen_sub_i64(rh, ah, bh);
        tcg_gen_sub_i64(rh, rh, t1);
        tcg_gen_mov_i64(rl, t0);
        tcg_temp_free_i64(t0);
        tcg_temp_free_i64(t1);
    }
}

void tcg_gen_mulu2_i64(TCGv_i64 rl, TCGv_i64 rh, TCGv_i64 arg1, TCGv_i64 arg2)
{
    if (TCG_TARGET_HAS_mulu2_i64) {
        tcg_gen_op4_i64(INDEX_op_mulu2_i64, rl, rh, arg1, arg2);
    } else if (TCG_TARGET_HAS_muluh_i64) {
        TCGv_i64 t = tcg_temp_ebb_new_i64();
        tcg_gen_op3_i64(INDEX_op_mul_i64, t, arg1, arg2);
        tcg_gen_op3_i64(INDEX_op_muluh_i64, rh, arg1, arg2);
        tcg_gen_mov_i64(rl, t);
        tcg_temp_free_i64(t);
    } else {
        TCGv_i64 t0 = tcg_temp_ebb_new_i64();
        tcg_gen_mul_i64(t0, arg1, arg2);
        gen_helper_muluh_i64(rh, arg1, arg2);
        tcg_gen_mov_i64(rl, t0);
        tcg_temp_free_i64(t0);
    }
}

void tcg_gen_muls2_i64(TCGv_i64 rl, TCGv_i64 rh, TCGv_i64 arg1, TCGv_i64 arg2)
{
    if (TCG_TARGET_HAS_muls2_i64) {
        tcg_gen_op4_i64(INDEX_op_muls2_i64, rl, rh, arg1, arg2);
    } else if (TCG_TARGET_HAS_mulsh_i64) {
        TCGv_i64 t = tcg_temp_ebb_new_i64();
        tcg_gen_op3_i64(INDEX_op_mul_i64, t, arg1, arg2);
        tcg_gen_op3_i64(INDEX_op_mulsh_i64, rh, arg1, arg2);
        tcg_gen_mov_i64(rl, t);
        tcg_temp_free_i64(t);
    } else if (TCG_TARGET_HAS_mulu2_i64 || TCG_TARGET_HAS_muluh_i64) {
        TCGv_i64 t0 = tcg_temp_ebb_new_i64();
        TCGv_i64 t1 = tcg_temp_ebb_new_i64();
        TCGv_i64 t2 = tcg_temp_ebb_new_i64();
        TCGv_i64 t3 = tcg_temp_ebb_new_i64();
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
        TCGv_i64 t0 = tcg_temp_ebb_new_i64();
        tcg_gen_mul_i64(t0, arg1, arg2);
        gen_helper_mulsh_i64(rh, arg1, arg2);
        tcg_gen_mov_i64(rl, t0);
        tcg_temp_free_i64(t0);
    }
}

void tcg_gen_mulsu2_i64(TCGv_i64 rl, TCGv_i64 rh, TCGv_i64 arg1, TCGv_i64 arg2)
{
    TCGv_i64 t0 = tcg_temp_ebb_new_i64();
    TCGv_i64 t1 = tcg_temp_ebb_new_i64();
    TCGv_i64 t2 = tcg_temp_ebb_new_i64();
    tcg_gen_mulu2_i64(t0, t1, arg1, arg2);
    /* Adjust for negative input for the signed arg1.  */
    tcg_gen_sari_i64(t2, arg1, 63);
    tcg_gen_and_i64(t2, t2, arg2);
    tcg_gen_sub_i64(rh, t1, t2);
    tcg_gen_mov_i64(rl, t0);
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
    tcg_temp_free_i64(t2);
}

void tcg_gen_smin_i64(TCGv_i64 ret, TCGv_i64 a, TCGv_i64 b)
{
    tcg_gen_movcond_i64(TCG_COND_LT, ret, a, b, a, b);
}

void tcg_gen_umin_i64(TCGv_i64 ret, TCGv_i64 a, TCGv_i64 b)
{
    tcg_gen_movcond_i64(TCG_COND_LTU, ret, a, b, a, b);
}

void tcg_gen_smax_i64(TCGv_i64 ret, TCGv_i64 a, TCGv_i64 b)
{
    tcg_gen_movcond_i64(TCG_COND_LT, ret, a, b, b, a);
}

void tcg_gen_umax_i64(TCGv_i64 ret, TCGv_i64 a, TCGv_i64 b)
{
    tcg_gen_movcond_i64(TCG_COND_LTU, ret, a, b, b, a);
}

void tcg_gen_abs_i64(TCGv_i64 ret, TCGv_i64 a)
{
    TCGv_i64 t = tcg_temp_ebb_new_i64();

    tcg_gen_sari_i64(t, a, 63);
    tcg_gen_xor_i64(ret, a, t);
    tcg_gen_sub_i64(ret, ret, t);
    tcg_temp_free_i64(t);
}

/* Size changing operations.  */

void tcg_gen_extrl_i64_i32(TCGv_i32 ret, TCGv_i64 arg)
{
    if (TCG_TARGET_REG_BITS == 32) {
        tcg_gen_mov_i32(ret, TCGV_LOW(arg));
    } else if (TCG_TARGET_HAS_extr_i64_i32) {
        tcg_gen_op2(INDEX_op_extrl_i64_i32, TCG_TYPE_I32,
                    tcgv_i32_arg(ret), tcgv_i64_arg(arg));
    } else {
        tcg_gen_mov_i32(ret, (TCGv_i32)arg);
    }
}

void tcg_gen_extrh_i64_i32(TCGv_i32 ret, TCGv_i64 arg)
{
    if (TCG_TARGET_REG_BITS == 32) {
        tcg_gen_mov_i32(ret, TCGV_HIGH(arg));
    } else if (TCG_TARGET_HAS_extr_i64_i32) {
        tcg_gen_op2(INDEX_op_extrh_i64_i32, TCG_TYPE_I32,
                    tcgv_i32_arg(ret), tcgv_i64_arg(arg));
    } else {
        TCGv_i64 t = tcg_temp_ebb_new_i64();
        tcg_gen_shri_i64(t, arg, 32);
        tcg_gen_mov_i32(ret, (TCGv_i32)t);
        tcg_temp_free_i64(t);
    }
}

void tcg_gen_extu_i32_i64(TCGv_i64 ret, TCGv_i32 arg)
{
    if (TCG_TARGET_REG_BITS == 32) {
        tcg_gen_mov_i32(TCGV_LOW(ret), arg);
        tcg_gen_movi_i32(TCGV_HIGH(ret), 0);
    } else {
        tcg_gen_op2(INDEX_op_extu_i32_i64, TCG_TYPE_I64,
                    tcgv_i64_arg(ret), tcgv_i32_arg(arg));
    }
}

void tcg_gen_ext_i32_i64(TCGv_i64 ret, TCGv_i32 arg)
{
    if (TCG_TARGET_REG_BITS == 32) {
        tcg_gen_mov_i32(TCGV_LOW(ret), arg);
        tcg_gen_sari_i32(TCGV_HIGH(ret), TCGV_LOW(ret), 31);
    } else {
        tcg_gen_op2(INDEX_op_ext_i32_i64, TCG_TYPE_I64,
                    tcgv_i64_arg(ret), tcgv_i32_arg(arg));
    }
}

void tcg_gen_concat_i32_i64(TCGv_i64 dest, TCGv_i32 low, TCGv_i32 high)
{
    TCGv_i64 tmp;

    if (TCG_TARGET_REG_BITS == 32) {
        tcg_gen_mov_i32(TCGV_LOW(dest), low);
        tcg_gen_mov_i32(TCGV_HIGH(dest), high);
        return;
    }

    tmp = tcg_temp_ebb_new_i64();
    /* These extensions are only needed for type correctness.
       We may be able to do better given target specific information.  */
    tcg_gen_extu_i32_i64(tmp, high);
    tcg_gen_extu_i32_i64(dest, low);
    /* If deposit is available, use it.  Otherwise use the extra
       knowledge that we have of the zero-extensions above.  */
    if (TCG_TARGET_deposit_valid(TCG_TYPE_I64, 32, 32)) {
        tcg_gen_deposit_i64(dest, dest, tmp, 32, 32);
    } else {
        tcg_gen_shli_i64(tmp, tmp, 32);
        tcg_gen_or_i64(dest, dest, tmp);
    }
    tcg_temp_free_i64(tmp);
}

void tcg_gen_extr_i64_i32(TCGv_i32 lo, TCGv_i32 hi, TCGv_i64 arg)
{
    if (TCG_TARGET_REG_BITS == 32) {
        tcg_gen_mov_i32(lo, TCGV_LOW(arg));
        tcg_gen_mov_i32(hi, TCGV_HIGH(arg));
    } else {
        tcg_gen_extrl_i64_i32(lo, arg);
        tcg_gen_extrh_i64_i32(hi, arg);
    }
}

void tcg_gen_extr32_i64(TCGv_i64 lo, TCGv_i64 hi, TCGv_i64 arg)
{
    tcg_gen_ext32u_i64(lo, arg);
    tcg_gen_shri_i64(hi, arg, 32);
}

void tcg_gen_concat32_i64(TCGv_i64 ret, TCGv_i64 lo, TCGv_i64 hi)
{
    tcg_gen_deposit_i64(ret, lo, hi, 32, 32);
}

void tcg_gen_extr_i128_i64(TCGv_i64 lo, TCGv_i64 hi, TCGv_i128 arg)
{
    tcg_gen_mov_i64(lo, TCGV128_LOW(arg));
    tcg_gen_mov_i64(hi, TCGV128_HIGH(arg));
}

void tcg_gen_concat_i64_i128(TCGv_i128 ret, TCGv_i64 lo, TCGv_i64 hi)
{
    tcg_gen_mov_i64(TCGV128_LOW(ret), lo);
    tcg_gen_mov_i64(TCGV128_HIGH(ret), hi);
}

void tcg_gen_mov_i128(TCGv_i128 dst, TCGv_i128 src)
{
    if (dst != src) {
        tcg_gen_mov_i64(TCGV128_LOW(dst), TCGV128_LOW(src));
        tcg_gen_mov_i64(TCGV128_HIGH(dst), TCGV128_HIGH(src));
    }
}

void tcg_gen_ld_i128(TCGv_i128 ret, TCGv_ptr base, tcg_target_long offset)
{
    if (HOST_BIG_ENDIAN) {
        tcg_gen_ld_i64(TCGV128_HIGH(ret), base, offset);
        tcg_gen_ld_i64(TCGV128_LOW(ret), base, offset + 8);
    } else {
        tcg_gen_ld_i64(TCGV128_LOW(ret), base, offset);
        tcg_gen_ld_i64(TCGV128_HIGH(ret), base, offset + 8);
    }
}

void tcg_gen_st_i128(TCGv_i128 val, TCGv_ptr base, tcg_target_long offset)
{
    if (HOST_BIG_ENDIAN) {
        tcg_gen_st_i64(TCGV128_HIGH(val), base, offset);
        tcg_gen_st_i64(TCGV128_LOW(val), base, offset + 8);
    } else {
        tcg_gen_st_i64(TCGV128_LOW(val), base, offset);
        tcg_gen_st_i64(TCGV128_HIGH(val), base, offset + 8);
    }
}

/* QEMU specific operations.  */

void tcg_gen_exit_tb(const TranslationBlock *tb, unsigned idx)
{
    /*
     * Let the jit code return the read-only version of the
     * TranslationBlock, so that we minimize the pc-relative
     * distance of the address of the exit_tb code to TB.
     * This will improve utilization of pc-relative address loads.
     *
     * TODO: Move this to translator_loop, so that all const
     * TranslationBlock pointers refer to read-only memory.
     * This requires coordination with targets that do not use
     * the translator_loop.
     */
    uintptr_t val = (uintptr_t)tcg_splitwx_to_rx((void *)tb) + idx;

    if (tb == NULL) {
        tcg_debug_assert(idx == 0);
    } else if (idx <= TB_EXIT_IDXMAX) {
#ifdef CONFIG_DEBUG_TCG
        /* This is an exit following a goto_tb.  Verify that we have
           seen this numbered exit before, via tcg_gen_goto_tb.  */
        tcg_debug_assert(tcg_ctx->goto_tb_issue_mask & (1 << idx));
#endif
    } else {
        /* This is an exit via the exitreq label.  */
        tcg_debug_assert(idx == TB_EXIT_REQUESTED);
    }

    tcg_gen_op1i(INDEX_op_exit_tb, 0, val);
}

void tcg_gen_goto_tb(unsigned idx)
{
    /* We tested CF_NO_GOTO_TB in translator_use_goto_tb. */
    tcg_debug_assert(!(tcg_ctx->gen_tb->cflags & CF_NO_GOTO_TB));
    /* We only support two chained exits.  */
    tcg_debug_assert(idx <= TB_EXIT_IDXMAX);
#ifdef CONFIG_DEBUG_TCG
    /* Verify that we haven't seen this numbered exit before.  */
    tcg_debug_assert((tcg_ctx->goto_tb_issue_mask & (1 << idx)) == 0);
    tcg_ctx->goto_tb_issue_mask |= 1 << idx;
#endif
    plugin_gen_disable_mem_helpers();
    tcg_gen_op1i(INDEX_op_goto_tb, 0, idx);
}

void tcg_gen_lookup_and_goto_ptr(void)
{
    TCGv_ptr ptr;

    if (tcg_ctx->gen_tb->cflags & CF_NO_GOTO_PTR) {
        tcg_gen_exit_tb(NULL, 0);
        return;
    }

    plugin_gen_disable_mem_helpers();
    ptr = tcg_temp_ebb_new_ptr();
    gen_helper_lookup_tb_ptr(ptr, tcg_env);
    tcg_gen_op1i(INDEX_op_goto_ptr, TCG_TYPE_PTR, tcgv_ptr_arg(ptr));
    tcg_temp_free_ptr(ptr);
}
