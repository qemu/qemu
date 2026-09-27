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

static TCGOp * DNI tcg_gen_op1i(TCGOpcode opc, TCGType type, TCGArg a1)
{
    return tcg_gen_op1(opc, type, a1);
}

static void DNI tcg_gen_op2_i64(TCGOpcode opc, TCGv_i64 a1, TCGv_i64 a2)
{
    tcg_gen_op2(opc, TCG_TYPE_I64, tcgv_i64_arg(a1), tcgv_i64_arg(a2));
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

static void DNI tcg_gen_ldst_op_i64(TCGOpcode opc, TCGv_i64 val,
                                    TCGv_ptr base, TCGArg offset)
{
    tcg_gen_op3(opc, TCG_TYPE_I64, tcgv_i64_arg(val),
                tcgv_ptr_arg(base), offset);
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

/*
 * Templated TCGv_i32 and TCGv_i64 operations.
 */

#define DEF1(NAME, T1)                                                  \
    void glue(glue(tcg_gen_,NAME),TExt)(T1 a)                           \
    { tcg_gen_##NAME(TYPE, glue(C_,T1)(a)); }

#define DEF2(NAME, T1, T2)                                              \
    void glue(glue(tcg_gen_,NAME),TExt)(T1 a, T2 b)                     \
    { tcg_gen_##NAME(TYPE, glue(C_,T1)(a), glue(C_,T2)(b)); }

#define DEF3(NAME, T1, T2, T3)                                          \
    void glue(glue(tcg_gen_,NAME),TExt)(T1 a, T2 b, T3 c)               \
    { tcg_gen_##NAME(TYPE, glue(C_,T1)(a), glue(C_,T2)(b), glue(C_,T3)(c)); }

#define DEF4(NAME, T1, T2, T3, T4)                                      \
    void glue(glue(tcg_gen_,NAME),TExt)(T1 a, T2 b, T3 c, T4 d)         \
    { tcg_gen_##NAME(TYPE, glue(C_,T1)(a), glue(C_,T2)(b),              \
                     glue(C_,T3)(c), glue(C_,T4)(d)); }

#define DEF5(NAME, T1, T2, T3, T4, T5)                                  \
    void glue(glue(tcg_gen_,NAME),TExt)(T1 a, T2 b, T3 c, T4 d, T5 e)   \
    { tcg_gen_##NAME(TYPE, glue(C_,T1)(a), glue(C_,T2)(b),              \
                     glue(C_,T3)(c), glue(C_,T4)(d), glue(C_,T5)(e)); }

#define DEF6(NAME, T1, T2, T3, T4, T5, T6)                              \
    void glue(glue(tcg_gen_,NAME),TExt)(T1 a, T2 b, T3 c, T4 d, T5 e, T6 f) \
    { tcg_gen_##NAME(TYPE, glue(C_,T1)(a), glue(C_,T2)(b), glue(C_,T3)(c),  \
                     glue(C_,T4)(d), glue(C_,T5)(e), glue(C_,T6)(f)); }

#define DEF7(NAME, T1, T2, T3, T4, T5, T6, T7)                          \
    void glue(glue(tcg_gen_,NAME),TExt)(T1 a, T2 b, T3 c, T4 d, T5 e,   \
                                        T6 f, T7 g)                     \
    { tcg_gen_##NAME(TYPE, glue(C_,T1)(a), glue(C_,T2)(b),              \
                     glue(C_,T3)(c), glue(C_,T4)(d), glue(C_,T5)(e),    \
                     glue(C_,T6)(f), glue(C_,T7)(g)); }

#define C_TCGv_i32      tcgv_i32_temp
#define C_TCGv_i64      tcgv_i64_temp
#define C_TCGv_ptr      tcgv_ptr_temp
#define C_int32_t
#define C_int64_t
#define C_MemOp
#define C_TCGCond
#define C_TCGLabelPtr
#define C_TCGTempPtr
#define C_TCGType
#define C_tcg_target_long
#define C_unsigned

#define TCGV  glue(TCGv,TExt)
#define TExt  _i32
#define TINT  int32_t
#define TYPE  TCG_TYPE_I32

#include "tcg/tcg-op-def.h.inc"

#undef TExt
#undef TINT
#undef TYPE

#define TExt  _i64
#define TINT  int64_t
#define TYPE  TCG_TYPE_I64

#include "tcg/tcg-op-def.h.inc"

#undef TExt
#undef TINT
#undef TCGV
#undef TYPE

#undef C_TCGv_i32
#undef C_TCGv_i64
#undef C_TCGv_ptr
#undef C_int32_t
#undef C_int64_t
#undef C_MemOp
#undef C_TCGCond
#undef C_TCGLabelPtr
#undef C_TCGTempPtr
#undef C_TCGType
#undef C_tcg_target_long
#undef C_unsigned

#undef DEF1
#undef DEF2
#undef DEF3
#undef DEF4
#undef DEF5
#undef DEF6
#undef DEF7

/*
 * Internal helper for bit and byte reversal.
 * Given a repeating matched block of 1's and 0's, swap the bits within
 * those two blocks.  E.g.  mask=00ff00ff, shift the input bits left and
 * right 8 bits.
 */
static void tcg_gen_bitswap(TCGType type, TCGTemp *dst,
                            TCGTemp *src, uint64_t mask)
{
    g_autoptr(TCGTemp) t0 = tcg_temp_new_ebb(type);
    g_autoptr(TCGTemp) t1 = tcg_temp_new_ebb(type);
    TCGTemp *tmask = tcg_constant_internal(type, mask);
    TCGTemp *tshift = tcg_constant_internal(type, cto64(mask));

    tcg_gen_and(type, t0, src, tmask);
    tcg_gen_shr(type, t1, src, tshift);
    tcg_gen_shl(type, t0, t0, tshift);
    tcg_gen_and(type, t1, t1, tmask);
    tcg_gen_or(type, dst, t0, t1);
}

/*
 * Generic expansions for templated operations.
 * We have used compiler type checks to ensure all TCGTemp
 * are of the proper type.
 */

void tcg_gen_abs(TCGType type, TCGTemp *dst, TCGTemp *src)
{
    g_autoptr(TCGTemp) tmp = tcg_temp_new_ebb(type);

    if (tcg_op_supported(INDEX_op_smax, type, 0)) {
        tcg_gen_neg(type, tmp, src);
        tcg_gen_smax(type, dst, tmp, src);
    } else {
        tcg_gen_sari(type, tmp, src, tcg_type_bits(type) - 1);
        tcg_gen_xor(type, dst, src, tmp);
        tcg_gen_sub(type, dst, dst, tmp);
    }
}

void tcg_gen_add(TCGType type, TCGTemp *dst, TCGTemp *src1, TCGTemp *src2)
{
    tcg_gen_op_ttt(INDEX_op_add, type, dst, src1, src2);
}

void tcg_gen_addi(TCGType type, TCGTemp *dst, TCGTemp *src1, int64_t src2)
{
    if (src2 == 0) {
        tcg_gen_mov(type, dst, src1);
    } else {
        tcg_gen_add(type, dst, src1, tcg_constant_internal(type, src2));
    }
}

void tcg_gen_add2(TCGType type, TCGTemp *rl, TCGTemp *rh, TCGTemp *al,
                  TCGTemp *ah, TCGTemp *bl, TCGTemp *bh)
{
    g_autoptr(TCGTemp) t0 = tcg_temp_new_ebb(type);

    if (tcg_op_supported(INDEX_op_addci, type, 0)) {
        tcg_gen_op_ttt(INDEX_op_addco, type, t0, al, bl);
        tcg_gen_op_ttt(INDEX_op_addci, type, rh, ah, bh);
    } else {
        g_autoptr(TCGTemp) t1 = tcg_temp_new_ebb(type);

        tcg_gen_add(type, t0, al, bl);
        tcg_gen_setcond(type, TCG_COND_LTU, t1, t0, al);
        tcg_gen_add(type, rh, ah, bh);
        tcg_gen_add(type, rh, rh, t1);
    }
    tcg_gen_mov(type, rl, t0);
}

void tcg_gen_addcio(TCGType type, TCGTemp *r, TCGTemp *co,
                    TCGTemp *a, TCGTemp *b, TCGTemp *ci)
{
    g_autoptr(TCGTemp) t0 = tcg_temp_new_ebb(type);

    if (tcg_op_supported(INDEX_op_addci, type, 0)) {
        TCGTemp *zero = tcg_constant_internal(type, 0);
        TCGTemp *mone = tcg_constant_internal(type, -1);

        tcg_gen_op_ttt(INDEX_op_addco, type, t0, ci, mone);
        tcg_gen_op_ttt(INDEX_op_addcio, type, r, a, b);
        tcg_gen_op_ttt(INDEX_op_addci, type, co, zero, zero);
    } else {
        g_autoptr(TCGTemp) t1 = tcg_temp_new_ebb(type);

        tcg_gen_add(type, t0, a, b);
        tcg_gen_setcond(type, TCG_COND_LTU, t1, t0, a);
        tcg_gen_add(type, r, t0, ci);
        tcg_gen_setcond(type, TCG_COND_LTU, t0, r, t0);
        tcg_gen_or(type, co, t0, t1);
    }
}

void tcg_gen_and(TCGType type, TCGTemp *dst, TCGTemp *src1, TCGTemp *src2)
{
    tcg_gen_op_ttt(INDEX_op_and, type, dst, src1, src2);
}

void tcg_gen_andi(TCGType type, TCGTemp *dst, TCGTemp *src1, int64_t src2)
{
    if (src2 == 0) {
        tcg_gen_movi(type, dst, 0);
    } else if (src2 == -1) {
        tcg_gen_mov(type, dst, src1);
    } else {
        /*
         * Canonicalize on extract, if valid.  This aids x86 with its
         * 2 operand MOVZBL and 2 operand AND, selecting the TCGOpcode
         * which does not require matching operands.  Other backends can
         * trivially expand the extract to AND during code generation.
         */
        if (is_power_of_2(src2 + 1)) {
            unsigned len = cto64(src2);
            if (TCG_TARGET_extract_valid(type, 0, len)) {
                tcg_gen_op_ttii(INDEX_op_extract, type, dst, src1, 0, len);
                return;
            }
        }
        tcg_gen_and(type, dst, src1, tcg_constant_internal(type, src2));
    }
}

void tcg_gen_andc(TCGType type, TCGTemp *dst, TCGTemp *src1, TCGTemp *src2)
{
    if (tcg_op_supported(INDEX_op_andc, type, 0)) {
        tcg_gen_op_ttt(INDEX_op_andc, type, dst, src1, src2);
    } else {
        g_autoptr(TCGTemp) tmp = tcg_temp_new_ebb(type);

        tcg_gen_not(type, tmp, src2);
        tcg_gen_and(type, dst, src1, tmp);
    }
}

void tcg_gen_brcond(TCGType type, TCGCond cond, TCGTemp *src1,
                    TCGTemp *src2, TCGLabel *l)
{
    if (cond == TCG_COND_ALWAYS) {
        tcg_gen_br(l);
    } else if (cond != TCG_COND_NEVER) {
        TCGOp *op = tcg_gen_op_ttii(INDEX_op_brcond, type,
                                    src1, src2, cond, label_arg(l));
        add_as_label_use(l, op);
    }
}

void tcg_gen_brcondi(TCGType type, TCGCond cond, TCGTemp *src1,
                     int64_t src2, TCGLabel *l)
{
    tcg_gen_brcond(type, cond, src1, tcg_constant_internal(type, src2), l);
}

void tcg_gen_bswap16(TCGType type, TCGTemp *dst, TCGTemp *src, unsigned flags)
{
    /* Only one extension flag may be present. */
    tcg_debug_assert(!(flags & TCG_BSWAP_OS) || !(flags & TCG_BSWAP_OZ));

    if (tcg_op_supported(INDEX_op_bswap16, type, 0)) {
        tcg_gen_op_tti(INDEX_op_bswap16, type, dst, src, flags);
    } else {
        g_autoptr(TCGTemp) t0 = tcg_temp_new_ebb(type);
        g_autoptr(TCGTemp) t1 = tcg_temp_new_ebb(type);
        int width = tcg_type_bits(type);

        if (flags & TCG_BSWAP_IZ) {                     /* t0 = ...a */
            tcg_gen_shri(type, t0, src, 8);
        } else {
            tcg_gen_extract(type, t0, src, 8, 8);
        }

        if (flags & TCG_BSWAP_OS) {
            tcg_gen_shli(type, t1, src, width - 8);     /* t1 = b... */
            tcg_gen_sari(type, t1, t1, width - 16);     /* t1 = ssb. */
        } else if (flags & TCG_BSWAP_OZ) {
            tcg_gen_ext8u(type, t1, src);               /* t1 = ...b */
            tcg_gen_shli(type, t1, t1, 8);              /* t1 = ..b. */
        } else {
            tcg_gen_shli(type, t1, src, 8);             /* t1 = xab. */
        }

        tcg_gen_or(type, dst, t0, t1);                  /* ret = ..ba (OZ) */
                                                        /*     = ssba (OS) */
                                                        /*     = xaba (0)  */
    }
}

void tcg_gen_clrsb(TCGType type, TCGTemp *dst, TCGTemp *src)
{
    if (tcg_op_supported(INDEX_op_clz, TCG_TYPE_REG, 0)) {
        g_autoptr(TCGTemp) tmp = tcg_temp_new_ebb(type);
        int width = tcg_type_bits(type);

        tcg_gen_sari(type, tmp, src, width - 1);
        tcg_gen_xor(type, tmp, tmp, src);
        tcg_gen_clzi(type, tmp, tmp, width);
        tcg_gen_addi(type, dst, tmp, -1);
    } else if (type == TCG_TYPE_I32) {
        gen_helper_clrsb_i32(temp_tcgv_i32(dst), temp_tcgv_i32(src));
    } else {
        gen_helper_clrsb_i64(temp_tcgv_i64(dst), temp_tcgv_i64(src));
    }
}

void tcg_gen_clz(TCGType type, TCGTemp *dst, TCGTemp *src1, TCGTemp *src2)
{
    if (tcg_op_supported(INDEX_op_clz, type, 0)) {
        tcg_gen_op_ttt(INDEX_op_clz, type, dst, src1, src2);
    } else if (type == TCG_TYPE_I64) {
        gen_helper_clz_i64(temp_tcgv_i64(dst), temp_tcgv_i64(src1),
                           temp_tcgv_i64(src2));
    } else if (tcg_op_supported(INDEX_op_clz, TCG_TYPE_I64, 0)) {
        g_autoptr(TCGTemp) t1 = tcg_temp_new_ebb(TCG_TYPE_I64);
        g_autoptr(TCGTemp) t2 = tcg_temp_new_ebb(TCG_TYPE_I64);

        tcg_gen_extu(t1, src1);
        tcg_gen_extu(t2, src2);
        tcg_gen_addi(TCG_TYPE_I64, t2, t2, 32);
        tcg_gen_op_ttt(INDEX_op_clz, TCG_TYPE_I64, t1, t1, t2);
        tcg_gen_extrl(dst, t1);
        tcg_gen_addi(TCG_TYPE_I32, dst, dst, -32);
    } else {
        gen_helper_clz_i32(temp_tcgv_i32(dst), temp_tcgv_i32(src1),
                           temp_tcgv_i32(src2));
    }
}

void tcg_gen_clzi(TCGType type, TCGTemp *dst, TCGTemp *src1, int64_t src2)
{
    tcg_gen_clz(type, dst, src1, tcg_constant_internal(type, src2));
}

void tcg_gen_ctpop(TCGType type, TCGTemp *dst, TCGTemp *src)
{
    if (tcg_op_supported(INDEX_op_ctpop, type, 0)) {
        tcg_gen_op_tt(INDEX_op_ctpop, type, dst, src);
    } else if (type == TCG_TYPE_I64) {
        gen_helper_ctpop_i64(temp_tcgv_i64(dst), temp_tcgv_i64(src));
    } else if (tcg_op_supported(INDEX_op_ctpop, TCG_TYPE_I64, 0)) {
        g_autoptr(TCGTemp) tmp = tcg_temp_new_ebb(TCG_TYPE_I64);

        tcg_gen_extu(tmp, src);
        tcg_gen_op_tt(INDEX_op_ctpop, TCG_TYPE_I64, tmp, tmp);
        tcg_gen_extrl(dst, tmp);
    } else {
        gen_helper_ctpop_i32(temp_tcgv_i32(dst), temp_tcgv_i32(src));
    }
}

void tcg_gen_ctz(TCGType type, TCGTemp *dst, TCGTemp *src1, TCGTemp *src2)
{
    g_autoptr(TCGTemp) t1 = NULL;
    int width = tcg_type_bits(type);

    if (tcg_op_supported(INDEX_op_ctz, type, 0)) {
        tcg_gen_op_ttt(INDEX_op_ctz, type, dst, src1, src2);
        return;
    }
    if (tcg_op_supported(INDEX_op_ctpop, type, 0)) {
        t1 = tcg_temp_new_ebb(type);
        tcg_gen_addi(type, t1, src1, -1);
        tcg_gen_andc(type, t1, t1, src1);
        if (src2->kind == TEMP_CONST && src2->val == width) {
            /* No fixup required. */
            tcg_gen_ctpop(type, dst, t1);
            return;
        }
        tcg_gen_ctpop(type, t1, t1);
    } else if (tcg_op_supported(INDEX_op_clz, type, 0)) {
        t1 = tcg_temp_new_ebb(type);
        tcg_gen_neg(type, t1, src1);
        tcg_gen_and(type, t1, t1, src1);
        tcg_gen_clzi(type, t1, t1, width);
        tcg_gen_xori(type, t1, t1, width - 1);
    } else if (type == TCG_TYPE_I64) {
        gen_helper_ctz_i64(temp_tcgv_i64(dst), temp_tcgv_i64(src1),
                           temp_tcgv_i64(src2));
        return;
    } else if (tcg_op_supported(INDEX_op_ctz, TCG_TYPE_I64, 0) ||
               tcg_op_supported(INDEX_op_clz, TCG_TYPE_I64, 0) ||
               tcg_op_supported(INDEX_op_ctpop, TCG_TYPE_I64, 0)) {
        g_autoptr(TCGTemp) t2 = tcg_temp_new_ebb(TCG_TYPE_I64);

        t1 = tcg_temp_new_ebb(TCG_TYPE_I64);
        tcg_gen_extu(t1, src1);
        tcg_gen_extu(t2, src2);
        tcg_gen_ctz(TCG_TYPE_I64, t1, t1, t2);
        tcg_gen_extrl(dst, t1);
        return;
    } else {
        gen_helper_ctz_i32(temp_tcgv_i32(dst), temp_tcgv_i32(src1),
                           temp_tcgv_i32(src2));
        return;
    }

    tcg_gen_movcond(type, TCG_COND_EQ, dst, src1,
                    tcg_constant_internal(type, 0), src2, t1);
}

void tcg_gen_ctzi(TCGType type, TCGTemp *dst, TCGTemp *src1, int64_t src2)
{
    tcg_gen_ctz(type, dst, src1, tcg_constant_internal(type, src2));
}

void tcg_gen_deposit(TCGType type, TCGTemp *dst, TCGTemp *src1,
                     TCGTemp *src2, unsigned ofs, unsigned len)
{
    unsigned width = tcg_type_bits(type);

    tcg_debug_assert(ofs < width);
    tcg_debug_assert(len > 0);
    tcg_debug_assert(len <= width);
    tcg_debug_assert(ofs + len <= width);

    if (len == width) {
        tcg_gen_mov(type, dst, src2);
    } else {
        tcg_gen_op_tttii(INDEX_op_deposit, type, dst, src1, src2, ofs, len);
    }
}

void tcg_gen_deposit_z(TCGType type, TCGTemp *dst, TCGTemp *src,
                       unsigned int ofs, unsigned int len)
{
    unsigned width = tcg_type_bits(type);

    tcg_debug_assert(ofs < width);
    tcg_debug_assert(len > 0);
    tcg_debug_assert(len <= width);
    tcg_debug_assert(ofs + len <= width);

    if (ofs + len == width) {
        tcg_gen_shli(type, dst, src, ofs);
    } else if (ofs == 0) {
        tcg_gen_extract(type, dst, src, 0, len);
    } else {
        TCGTemp *zero = tcg_constant_internal(type, 0);
        tcg_gen_op_tttii(INDEX_op_deposit, type, dst, zero, src, ofs, len);
    }
}

void tcg_gen_discard(TCGType type, TCGTemp *src)
{
    tcg_gen_op1(INDEX_op_discard, type, temp_arg(src));
}

void tcg_gen_div(TCGType type, TCGTemp *dst, TCGTemp *src1, TCGTemp *src2)
{
    if (tcg_op_supported(INDEX_op_divs, type, 0)) {
        tcg_gen_op_ttt(INDEX_op_divs, type, dst, src1, src2);
    } else if (tcg_op_supported(INDEX_op_divs2, type, 0)) {
        g_autoptr(TCGTemp) tmp = tcg_temp_new_ebb(type);

        tcg_gen_sari(type, tmp, src1, tcg_type_bits(type) - 1);
        tcg_gen_op_ttttt(INDEX_op_divs2, type, dst, tmp, src1, tmp, src2);
    } else {
        g_assert_not_reached();
    }
}

void tcg_gen_divu(TCGType type, TCGTemp *dst, TCGTemp *src1, TCGTemp *src2)
{
    if (tcg_op_supported(INDEX_op_divu, type, 0)) {
        tcg_gen_op_ttt(INDEX_op_divu, type, dst, src1, src2);
    } else if (tcg_op_supported(INDEX_op_divu2, type, 0)) {
        g_autoptr(TCGTemp) tmp = tcg_temp_new_ebb(type);

        tcg_gen_op_ttttt(INDEX_op_divu2, type, dst, tmp, src1,
                         tcg_constant_internal(type, 0), src2);
    } else {
        g_assert_not_reached();
    }
}

void tcg_gen_dup(TCGType type, unsigned vece, TCGTemp *dst, TCGTemp *src)
{
    unsigned t_width = tcg_type_bits(type);
    unsigned e_width = memop_size(vece) * 8;

    tcg_debug_assert(e_width <= t_width);
    if (t_width == e_width) {
        tcg_gen_mov(type, dst, src);
    } else if (t_width == e_width * 2) {
        tcg_gen_deposit(type, dst, src, src, e_width, e_width);
    } else {
        tcg_gen_extract(type, dst, src, 0, e_width);
        tcg_gen_muli(type, dst, dst, dup_const(vece, 1));
    }
}

void tcg_gen_eqv(TCGType type, TCGTemp *dst, TCGTemp *src1, TCGTemp *src2)
{
    if (tcg_op_supported(INDEX_op_eqv, type, 0)) {
        tcg_gen_op_ttt(INDEX_op_eqv, type, dst, src1, src2);
    } else {
        tcg_gen_xor(type, dst, src1, src2);
        tcg_gen_not(type, dst, dst);
    }
}

void tcg_gen_ext(TCGType type, TCGTemp *dst, TCGTemp *src, MemOp mop)
{
    unsigned width = memop_size(mop) * 8;

    if (mop & MO_SIGN) {
        tcg_gen_sextract(type, dst, src, 0, width);
    } else {
        tcg_gen_extract(type, dst, src, 0, width);
    }
}

void tcg_gen_ext8s(TCGType type, TCGTemp *dst, TCGTemp *src)
{
    tcg_gen_sextract(type, dst, src, 0, 8);
}

void tcg_gen_ext8u(TCGType type, TCGTemp *dst, TCGTemp *src)
{
    tcg_gen_extract(type, dst, src, 0, 8);
}

void tcg_gen_ext16s(TCGType type, TCGTemp *dst, TCGTemp *src)
{
    tcg_gen_sextract(type, dst, src, 0, 16);
}

void tcg_gen_ext16u(TCGType type, TCGTemp *dst, TCGTemp *src)
{
    tcg_gen_extract(type, dst, src, 0, 16);
}

void tcg_gen_extract(TCGType type, TCGTemp *dst, TCGTemp *src,
                     unsigned int ofs, unsigned int len)
{
    unsigned width = tcg_type_bits(type);
    uint64_t mask;

    tcg_debug_assert(ofs < width);
    tcg_debug_assert(len > 0);
    tcg_debug_assert(len <= width);
    tcg_debug_assert(ofs + len <= width);

    /* Canonicalize certain special cases, even if extract is supported.  */
    if (ofs + len == width) {
        tcg_gen_shri(type, dst, src, ofs);
        return;
    }

    if (TCG_TARGET_extract_valid(type, ofs, len)) {
        tcg_gen_op_ttii(INDEX_op_extract, type, dst, src, ofs, len);
        return;
    }

    /* Assume that zero-extension, if available, is cheaper than a shift.  */
    if (TCG_TARGET_extract_valid(type, 0, ofs + len)) {
        tcg_gen_op_ttii(INDEX_op_extract, type, dst, src, 0, ofs + len);
        tcg_gen_shri(type, dst, dst, ofs);
        return;
    }
    if (TCG_TARGET_extract_valid(type, 0, len)) {
        tcg_gen_shri(type, dst, src, ofs);
        tcg_gen_op_ttii(INDEX_op_extract, type, dst, dst, 0, len);
        return;
    }

    mask = MAKE_64BIT_MASK(0, len);
    if (tcg_op_imm_match(INDEX_op_and, type, mask)) {
        tcg_gen_shri(type, dst, src, ofs);
        /* Do not recurse with gen_andi. */
        tcg_gen_and(type, dst, dst, tcg_constant_internal(type, mask));
    } else {
        tcg_gen_shli(type, dst, src, width - len - ofs);
        tcg_gen_shri(type, dst, dst, width - len);
    }
}

/* Extract one word from a pair, hi:lo, starting from ofs.  */
void tcg_gen_extract2(TCGType type, TCGTemp *dst, TCGTemp *lo, TCGTemp *hi,
                      unsigned int ofs)
{
    unsigned width = tcg_type_bits(type);

    tcg_debug_assert(ofs <= width);
    if (ofs == 0) {
        tcg_gen_mov(type, dst, lo);
    } else if (ofs == width) {
        tcg_gen_mov(type, dst, hi);
    } else if (lo == hi) {
        tcg_gen_rotri(type, dst, lo, ofs);
    } else {
        tcg_gen_op_ttti(INDEX_op_extract2, type, dst, lo, hi, ofs);
    }
}

void tcg_gen_ld(TCGType type, TCGTemp *dst, TCGTemp *base, tcg_target_long ofs)
{
    tcg_gen_op_tti(INDEX_op_ld, type, dst, base, ofs);
}

void tcg_gen_ld8s(TCGType type, TCGTemp *dst,
                  TCGTemp *base, tcg_target_long ofs)
{
    tcg_gen_op_tti(INDEX_op_ld8s, type, dst, base, ofs);
}

void tcg_gen_ld8u(TCGType type, TCGTemp *dst,
                  TCGTemp *base, tcg_target_long ofs)
{
    tcg_gen_op_tti(INDEX_op_ld8u, type, dst, base, ofs);
}

void tcg_gen_ld16s(TCGType type, TCGTemp *dst,
                   TCGTemp *base, tcg_target_long ofs)
{
    tcg_gen_op_tti(INDEX_op_ld16s, type, dst, base, ofs);
}

void tcg_gen_ld16u(TCGType type, TCGTemp *dst,
                   TCGTemp *base, tcg_target_long ofs)
{
    tcg_gen_op_tti(INDEX_op_ld16u, type, dst, base, ofs);
}

void tcg_gen_mov(TCGType type, TCGTemp *dst, TCGTemp *src)
{
    if (src != dst) {
        tcg_gen_op_tt(INDEX_op_mov, type, dst, src);
    }
}

void tcg_gen_movi(TCGType type, TCGTemp *dst, int64_t src)
{
    tcg_gen_op_tt(INDEX_op_mov, type, dst, tcg_constant_internal(type, src));
}

void tcg_gen_movcond(TCGType type, TCGCond cond, TCGTemp *dst,
                     TCGTemp *c1, TCGTemp *c2, TCGTemp *v1, TCGTemp *v2)
{
    if (cond == TCG_COND_ALWAYS) {
        tcg_gen_mov(type, dst, v1);
    } else if (cond == TCG_COND_NEVER) {
        tcg_gen_mov(type, dst, v2);
    } else {
        tcg_gen_op_ttttti(INDEX_op_movcond, type, dst, c1, c2, v1, v2, cond);
    }
}

void tcg_gen_mul(TCGType type, TCGTemp *dst, TCGTemp *src1, TCGTemp *src2)
{
    tcg_gen_op_ttt(INDEX_op_mul, type, dst, src1, src2);
}

void tcg_gen_muli(TCGType type, TCGTemp *dst, TCGTemp *src1, int64_t src2)
{
    if (src2 == 0) {
        tcg_gen_movi(type, dst, 0);
    } else if (is_power_of_2(src2)) {
        tcg_gen_shli(type, dst, src1, ctz64(src2));
    } else {
        tcg_gen_mul(type, dst, src1, tcg_constant_internal(type, src2));
    }
}

void tcg_gen_muls2(TCGType type, TCGTemp *rl, TCGTemp *rh,
                   TCGTemp *src1, TCGTemp *src2)
{
    if (tcg_op_supported(INDEX_op_muls2, type, 0)) {
        tcg_gen_op_tttt(INDEX_op_muls2, type, rl, rh, src1, src2);
    } else if (tcg_op_supported(INDEX_op_mulsh, type, 0)) {
        g_autoptr(TCGTemp) t = tcg_temp_new_ebb(type);

        tcg_gen_mul(type, t, src1, src2);
        tcg_gen_op_ttt(INDEX_op_mulsh, type, rh, src1, src2);
        tcg_gen_mov(type, rl, t);
    } else if (type == TCG_TYPE_I32) {
        g_autoptr(TCGTemp) t0 = tcg_temp_new_ebb(TCG_TYPE_I64);
        g_autoptr(TCGTemp) t1 = tcg_temp_new_ebb(TCG_TYPE_I64);

        /* Simply extend and use host 64-bit multiply. */
        tcg_gen_exts(t0, src1);
        tcg_gen_exts(t1, src2);
        tcg_gen_mul(TCG_TYPE_I64, t0, t0, t1);
        tcg_gen_extrl(rl, t0);
        tcg_gen_extrh(rh, t0);
    } else if (tcg_op_supported(INDEX_op_mulu2, TCG_TYPE_I64, 0) ||
               tcg_op_supported(INDEX_op_muluh, TCG_TYPE_I64, 0)) {
        g_autoptr(TCGTemp) t0 = tcg_temp_new_ebb(type);
        g_autoptr(TCGTemp) t1 = tcg_temp_new_ebb(type);
        g_autoptr(TCGTemp) t2 = NULL;
        g_autoptr(TCGTemp) t3 = NULL;

        tcg_gen_mulu2(TCG_TYPE_I64, t0, t1, src1, src2);

        /* Adjust for negative inputs.  */
        t2 = tcg_temp_new_ebb(type);
        t3 = tcg_temp_new_ebb(type);
        tcg_gen_sari(TCG_TYPE_I64, t2, src1, 63);
        tcg_gen_sari(TCG_TYPE_I64, t3, src2, 63);
        tcg_gen_and(TCG_TYPE_I64, t2, t2, src2);
        tcg_gen_and(TCG_TYPE_I64, t3, t3, src1);
        tcg_gen_sub(TCG_TYPE_I64, rh, t1, t2);
        tcg_gen_sub(TCG_TYPE_I64, rh, rh, t3);
        tcg_gen_mov(TCG_TYPE_I64, rl, t0);
    } else {
        g_autoptr(TCGTemp) t = tcg_temp_new_ebb(type);

        tcg_gen_mul(TCG_TYPE_I64, t, src1, src2);
        gen_helper_mulsh_i64(temp_tcgv_i64(rh), temp_tcgv_i64(src1),
                             temp_tcgv_i64(src2));
        tcg_gen_mov(TCG_TYPE_I64, rl, t);
    }
}

void tcg_gen_mulu2(TCGType type, TCGTemp *rl, TCGTemp *rh,
                   TCGTemp *src1, TCGTemp *src2)
{
    if (tcg_op_supported(INDEX_op_mulu2, type, 0)) {
        tcg_gen_op_tttt(INDEX_op_mulu2, type, rl, rh, src1, src2);
    } else if (tcg_op_supported(INDEX_op_muluh, type, 0)) {
        g_autoptr(TCGTemp) t = tcg_temp_new_ebb(type);

        tcg_gen_mul(type, t, src1, src2);
        tcg_gen_op_ttt(INDEX_op_muluh, type, rh, src1, src2);
        tcg_gen_mov(type, rl, t);
    } else if (type == TCG_TYPE_I32) {
        g_autoptr(TCGTemp) t0 = tcg_temp_new_ebb(TCG_TYPE_I64);
        g_autoptr(TCGTemp) t1 = tcg_temp_new_ebb(TCG_TYPE_I64);

        /* Simply extend and use host 64-bit multiply. */
        tcg_gen_extu(t0, src1);
        tcg_gen_extu(t1, src2);
        tcg_gen_mul(TCG_TYPE_I64, t0, t0, t1);
        tcg_gen_extrl(rl, t0);
        tcg_gen_extrh(rh, t0);
    } else {
        g_autoptr(TCGTemp) t = tcg_temp_new_ebb(type);

        tcg_gen_mul(TCG_TYPE_I64, t, src1, src2);
        gen_helper_muluh_i64(temp_tcgv_i64(rh), temp_tcgv_i64(src1),
                             temp_tcgv_i64(src2));
        tcg_gen_mov(TCG_TYPE_I64, rl, t);
    }
}

void tcg_gen_nand(TCGType type, TCGTemp *dst, TCGTemp *src1, TCGTemp *src2)
{
    if (tcg_op_supported(INDEX_op_nand, type, 0)) {
        tcg_gen_op_ttt(INDEX_op_nand, type, dst, src1, src2);
    } else {
        tcg_gen_and(type, dst, src1, src2);
        tcg_gen_not(type, dst, dst);
    }
}

void tcg_gen_neg(TCGType type, TCGTemp *dst, TCGTemp *src)
{
    tcg_gen_op_tt(INDEX_op_neg, type, dst, src);
}

void tcg_gen_negsetcond(TCGType type, TCGCond cond, TCGTemp *dst,
                        TCGTemp *src1, TCGTemp *src2)
{
    if (cond == TCG_COND_ALWAYS) {
        tcg_gen_movi(type, dst, -1);
    } else if (cond == TCG_COND_NEVER) {
        tcg_gen_movi(type, dst, 0);
    } else {
        tcg_gen_op_ttti(INDEX_op_negsetcond, type, dst, src1, src2, cond);
    }
}

void tcg_gen_negsetcondi(TCGType type, TCGCond cond, TCGTemp *dst,
                         TCGTemp *src1, int64_t src2)
{
    tcg_gen_negsetcond(type, cond, dst, src1,
                       tcg_constant_internal(type, src2));
}

void tcg_gen_nor(TCGType type, TCGTemp *dst, TCGTemp *src1, TCGTemp *src2)
{
    if (tcg_op_supported(INDEX_op_nor, type, 0)) {
        tcg_gen_op_ttt(INDEX_op_nor, type, dst, src1, src2);
    } else {
        tcg_gen_or(type, dst, src1, src2);
        tcg_gen_not(type, dst, dst);
    }
}

void tcg_gen_not(TCGType type, TCGTemp *dst, TCGTemp *src)
{
    if (tcg_op_supported(INDEX_op_not, type, 0)) {
        tcg_gen_op_tt(INDEX_op_not, type, dst, src);
    } else {
        /* Do not recurse with gen_xori. */
        tcg_gen_xor(type, dst, src, tcg_constant_internal(type, -1));
    }
}

void tcg_gen_or(TCGType type, TCGTemp *dst, TCGTemp *src1, TCGTemp *src2)
{
    tcg_gen_op_ttt(INDEX_op_or, type, dst, src1, src2);
}

void tcg_gen_ori(TCGType type, TCGTemp *dst, TCGTemp *src1, int64_t src2)
{
    if (src2 == -1) {
        tcg_gen_movi(type, dst, -1);
    } else if (src2 == 0) {
        tcg_gen_mov(type, dst, src1);
    } else {
        tcg_gen_or(type, dst, src1, tcg_constant_internal(type, src2));
    }
}

void tcg_gen_orc(TCGType type, TCGTemp *dst, TCGTemp *src1, TCGTemp *src2)
{
    if (tcg_op_supported(INDEX_op_orc, type, 0)) {
        tcg_gen_op_ttt(INDEX_op_orc, type, dst, src1, src2);
    } else {
        g_autoptr(TCGTemp) tmp = tcg_temp_new_ebb(type);

        tcg_gen_not(type, tmp, src2);
        tcg_gen_or(type, dst, src1, tmp);
    }
}

void tcg_gen_rem(TCGType type, TCGTemp *dst, TCGTemp *src1, TCGTemp *src2)
{
    if (tcg_op_supported(INDEX_op_rems, type, 0)) {
        tcg_gen_op_ttt(INDEX_op_rems, type, dst, src1, src2);
    } else {
        g_autoptr(TCGTemp) tmp = tcg_temp_new_ebb(type);

        if (tcg_op_supported(INDEX_op_divs, type, 0)) {
            tcg_gen_op_ttt(INDEX_op_divs, type, tmp, src1, src2);
            tcg_gen_mul(type, tmp, tmp, src2);
            tcg_gen_sub(type, dst, src1, tmp);
        } else if (tcg_op_supported(INDEX_op_divs2, type, 0)) {
            tcg_gen_sari(type, tmp, src1, tcg_type_bits(type) - 1);
            tcg_gen_op_ttttt(INDEX_op_divs2, type, tmp, dst, src1, tmp, src2);
        } else {
            g_assert_not_reached();
        }
    }
}

void tcg_gen_remu(TCGType type, TCGTemp *dst, TCGTemp *src1, TCGTemp *src2)
{
    if (tcg_op_supported(INDEX_op_remu, type, 0)) {
        tcg_gen_op_ttt(INDEX_op_remu, type, dst, src1, src2);
    } else {
        g_autoptr(TCGTemp) tmp = tcg_temp_new_ebb(type);

        if (tcg_op_supported(INDEX_op_divu, type, 0)) {
            tcg_gen_op_ttt(INDEX_op_divu, type, tmp, src1, src2);
            tcg_gen_mul(type, tmp, tmp, src2);
            tcg_gen_sub(type, dst, src1, tmp);
        } else if (tcg_op_supported(INDEX_op_divu2, type, 0)) {
            tcg_gen_op_ttttt(INDEX_op_divu2, type, tmp, dst, src1,
                             tcg_constant_internal(type, 0), src2);
        } else {
            g_assert_not_reached();
        }
    }
}

void tcg_gen_revbit8(TCGType type, TCGTemp *dst, TCGTemp *src)
{
    static const TCGOpcode revw[2] = { INDEX_op_revbit32, INDEX_op_revbit64 };
    static const TCGOpcode bswapw[2] = { INDEX_op_bswap32, INDEX_op_bswap64 };

    if (tcg_op_supported(INDEX_op_revbit8, type, 0)) {
        tcg_gen_op_tt(INDEX_op_revbit8, type, dst, src);
    } else if (tcg_op_supported(revw[type], type, 0) &&
               tcg_op_supported(bswapw[type], type, 0)) {
        tcg_gen_op_tt(revw[type], type, dst, src);
        tcg_gen_op_tti(bswapw[type], type, dst, dst, 0);
    } else {
        tcg_gen_bitswap(type, dst, src, 0x5555555555555555ull);
        tcg_gen_bitswap(type, dst, dst, 0x3333333333333333ull);
        tcg_gen_bitswap(type, dst, dst, 0x0f0f0f0f0f0f0f0full);
    }
}

void tcg_gen_rotl(TCGType type, TCGTemp *dst, TCGTemp *src1, TCGTemp *src2)
{
    if (tcg_op_supported(INDEX_op_rotl, type, 0)) {
        tcg_gen_op_ttt(INDEX_op_rotl, type, dst, src1, src2);
    } else if (tcg_op_supported(INDEX_op_rotr, type, 0)) {
        g_autoptr(TCGTemp) tmp = tcg_temp_new_ebb(type);

        tcg_gen_neg(type, tmp, src2);
        tcg_gen_op_ttt(INDEX_op_rotr, type, dst, src1, tmp);
    } else {
        g_autoptr(TCGTemp) t0 = tcg_temp_new_ebb(type);
        g_autoptr(TCGTemp) t1 = tcg_temp_new_ebb(type);

        tcg_gen_shl(type, t0, src1, src2);
        tcg_gen_neg(type, t1, src2);
        tcg_gen_shr(type, t1, src1, t1);
        tcg_gen_or(type, dst, t0, t1);
    }
}

void tcg_gen_rotli(TCGType type, TCGTemp *dst, TCGTemp *src1, int64_t src2)
{
    int width = tcg_type_bits(type);

    tcg_debug_assert(src2 >= 0 && src2 < width);
    if (src2 == 0) {
        tcg_gen_mov(type, dst, src1);
    } else if (tcg_op_supported(INDEX_op_rotl, type, 0)) {
        tcg_gen_op_ttt(INDEX_op_rotl, type, dst, src1,
                       tcg_constant_internal(type, src2));
    } else {
        tcg_gen_rotri(type, dst, src1, width - src2);
    }
}

void tcg_gen_rotr(TCGType type, TCGTemp *dst, TCGTemp *src1, TCGTemp *src2)
{
    if (tcg_op_supported(INDEX_op_rotr, type, 0)) {
        tcg_gen_op_ttt(INDEX_op_rotr, type, dst, src1, src2);
    } else if (tcg_op_supported(INDEX_op_rotl, type, 0)) {
        g_autoptr(TCGTemp) tmp = tcg_temp_new_ebb(type);

        tcg_gen_neg(type, tmp, src2);
        tcg_gen_op_ttt(INDEX_op_rotl, type, dst, src1, tmp);
    } else {
        g_autoptr(TCGTemp) t0 = tcg_temp_new_ebb(type);
        g_autoptr(TCGTemp) t1 = tcg_temp_new_ebb(type);

        tcg_gen_shr(type, t0, src1, src2);
        tcg_gen_neg(type, t1, src2);
        tcg_gen_shl(type, t1, src1, t1);
        tcg_gen_or(type, dst, t0, t1);
    }
}

void tcg_gen_rotri(TCGType type, TCGTemp *dst, TCGTemp *src1, int64_t src2)
{
    int width = tcg_type_bits(type);

    tcg_debug_assert(src2 >= 0 && src2 < width);
    if (src2 == 0) {
        tcg_gen_mov(type, dst, src1);
    } else if (tcg_op_supported(INDEX_op_rotr, type, 0)) {
        tcg_gen_op_ttt(INDEX_op_rotr, type, dst, src1,
                       tcg_constant_internal(type, src2));
    } else if (tcg_op_supported(INDEX_op_rotl, type, 0)) {
        tcg_gen_op_ttt(INDEX_op_rotl, type, dst, src1,
                       tcg_constant_internal(type, width - src2));
    } else {
        /* Do not recurse with the rotri simplification. */
        tcg_gen_op_ttti(INDEX_op_extract2, type, dst, src1, src1, src2);
    }
}

void tcg_gen_sar(TCGType type, TCGTemp *dst, TCGTemp *src1, TCGTemp *src2)
{
    tcg_gen_op_ttt(INDEX_op_sar, type, dst, src1, src2);
}

void tcg_gen_sari(TCGType type, TCGTemp *dst, TCGTemp *src1, int64_t src2)
{
    tcg_debug_assert(src2 >= 0 && src2 < tcg_type_bits(type));
    if (src2 == 0) {
        tcg_gen_mov(type, dst, src1);
    } else {
        tcg_gen_sar(type, dst, src1, tcg_constant_internal(type, src2));
    }
}

void tcg_gen_setcond(TCGType type, TCGCond cond, TCGTemp *dst,
                     TCGTemp *src1, TCGTemp *src2)
{
    if (cond == TCG_COND_ALWAYS) {
        tcg_gen_movi(type, dst, 1);
    } else if (cond == TCG_COND_NEVER) {
        tcg_gen_movi(type, dst, 0);
    } else {
        tcg_gen_op_ttti(INDEX_op_setcond, type, dst, src1, src2, cond);
    }
}

void tcg_gen_setcondi(TCGType type, TCGCond cond, TCGTemp *dst,
                      TCGTemp *src1, int64_t src2)
{
    tcg_gen_setcond(type, cond, dst, src1, tcg_constant_internal(type, src2));
}

void tcg_gen_sextract(TCGType type, TCGTemp *dst, TCGTemp *src,
                      unsigned int ofs, unsigned int len)
{
    unsigned width = tcg_type_bits(type);

    tcg_debug_assert(ofs < width);
    tcg_debug_assert(len > 0);
    tcg_debug_assert(len <= width);
    tcg_debug_assert(ofs + len <= width);

    /* Canonicalize certain special cases, even if extract is supported.  */
    if (ofs + len == width) {
        tcg_gen_sari(type, dst, src, width - len);
        return;
    }

    if (TCG_TARGET_sextract_valid(type, ofs, len)) {
        tcg_gen_op_ttii(INDEX_op_sextract, type, dst, src, ofs, len);
        return;
    }

    /* Assume that sign-extension, if available, is cheaper than a shift.  */
    if (TCG_TARGET_sextract_valid(type, 0, ofs + len)) {
        tcg_gen_op_ttii(INDEX_op_sextract, type, dst, src, 0, ofs + len);
        tcg_gen_sari(type, dst, dst, ofs);
        return;
    }
    if (TCG_TARGET_sextract_valid(type, 0, len)) {
        tcg_gen_shri(type, dst, src, ofs);
        tcg_gen_op_ttii(INDEX_op_sextract, type, dst, dst, 0, len);
        return;
    }

    tcg_gen_shli(type, dst, src, width - len - ofs);
    tcg_gen_sari(type, dst, dst, width - len);
}

void tcg_gen_shl(TCGType type, TCGTemp *dst, TCGTemp *src1, TCGTemp *src2)
{
    tcg_gen_op_ttt(INDEX_op_shl, type, dst, src1, src2);
}

void tcg_gen_shli(TCGType type, TCGTemp *dst, TCGTemp *src1, int64_t src2)
{
    tcg_debug_assert(src2 >= 0 && src2 < tcg_type_bits(type));
    if (src2 == 0) {
        tcg_gen_mov(type, dst, src1);
    } else {
        tcg_gen_shl(type, dst, src1, tcg_constant_internal(type, src2));
    }
}

void tcg_gen_shr(TCGType type, TCGTemp *dst, TCGTemp *src1, TCGTemp *src2)
{
    tcg_gen_op_ttt(INDEX_op_shr, type, dst, src1, src2);
}

void tcg_gen_shri(TCGType type, TCGTemp *dst, TCGTemp *src1, int64_t src2)
{
    tcg_debug_assert(src2 >= 0 && src2 < tcg_type_bits(type));
    if (src2 == 0) {
        tcg_gen_mov(type, dst, src1);
    } else {
        tcg_gen_shr(type, dst, src1, tcg_constant_internal(type, src2));
    }
}

void tcg_gen_smax(TCGType type, TCGTemp *dst, TCGTemp *src1, TCGTemp *src2)
{
    if (tcg_op_supported(INDEX_op_smax, type, 0)) {
        tcg_gen_op_ttt(INDEX_op_smax, type, dst, src1, src2);
    } else {
        tcg_gen_movcond(type, TCG_COND_GT, dst, src1, src2, src1, src2);
    }
}

void tcg_gen_smin(TCGType type, TCGTemp *dst, TCGTemp *src1, TCGTemp *src2)
{
    if (tcg_op_supported(INDEX_op_smin, type, 0)) {
        tcg_gen_op_ttt(INDEX_op_smin, type, dst, src1, src2);
    } else {
        tcg_gen_movcond(type, TCG_COND_LT, dst, src1, src2, src1, src2);
    }
}

void tcg_gen_st(TCGType type, TCGTemp *val, TCGTemp *base, tcg_target_long ofs)
{
    tcg_gen_op_tti(INDEX_op_st, type, val, base, ofs);
}

void tcg_gen_st8(TCGType type, TCGTemp *dst, TCGTemp *base, tcg_target_long ofs)
{
    tcg_gen_op_tti(INDEX_op_st8, type, dst, base, ofs);
}

void tcg_gen_st16(TCGType type, TCGTemp *dst,
                  TCGTemp *base, tcg_target_long ofs)
{
    tcg_gen_op_tti(INDEX_op_st16, type, dst, base, ofs);
}

void tcg_gen_sub(TCGType type, TCGTemp *dst, TCGTemp *src1, TCGTemp *src2)
{
    tcg_gen_op_ttt(INDEX_op_sub, type, dst, src1, src2);
}

void tcg_gen_subi(TCGType type, TCGTemp *dst, TCGTemp *src1, int64_t src2)
{
    tcg_gen_addi(type, dst, src1, -src2);
}

void tcg_gen_sub2(TCGType type, TCGTemp *rl, TCGTemp *rh, TCGTemp *al,
                     TCGTemp *ah, TCGTemp *bl, TCGTemp *bh)
{
    g_autoptr(TCGTemp) t0 = tcg_temp_new_ebb(type);

    if (tcg_op_supported(INDEX_op_subbi, type, 0)) {
        tcg_gen_op_ttt(INDEX_op_subbo, type, t0, al, bl);
        tcg_gen_op_ttt(INDEX_op_subbi, type, rh, ah, bh);
    } else {
        g_autoptr(TCGTemp) t1 = tcg_temp_new_ebb(type);

        tcg_gen_sub(type, t0, al, bl);
        tcg_gen_setcond(type, TCG_COND_LTU, t1, al, bl);
        tcg_gen_sub(type, rh, ah, bh);
        tcg_gen_sub(type, rh, rh, t1);
    }
    tcg_gen_mov(type, rl, t0);
}

void tcg_gen_subfi(TCGType type, TCGTemp *dst, int64_t src1, TCGTemp *src2)
{
    if (src1 == 0) {
        tcg_gen_neg(type, dst, src2);
    } else {
        tcg_gen_sub(type, dst, tcg_constant_internal(type, src1), src2);
    }
}

void tcg_gen_umax(TCGType type, TCGTemp *dst, TCGTemp *src1, TCGTemp *src2)
{
    if (tcg_op_supported(INDEX_op_umax, type, 0)) {
        tcg_gen_op_ttt(INDEX_op_umax, type, dst, src1, src2);
    } else {
        tcg_gen_movcond(type, TCG_COND_GTU, dst, src1, src2, src1, src2);
    }
}

void tcg_gen_umin(TCGType type, TCGTemp *dst, TCGTemp *src1, TCGTemp *src2)
{
    if (tcg_op_supported(INDEX_op_umin, type, 0)) {
        tcg_gen_op_ttt(INDEX_op_umin, type, dst, src1, src2);
    } else {
        tcg_gen_movcond(type, TCG_COND_LTU, dst, src1, src2, src1, src2);
    }
}

void tcg_gen_ussub(TCGType type, TCGTemp *dst, TCGTemp *src1, TCGTemp *src2)
{
    g_autoptr(TCGTemp) tmp = tcg_temp_new_ebb(type);

    if (tcg_op_supported(INDEX_op_umax, type, 0)) {
        tcg_gen_umax(type, tmp, src1, src2);
        tcg_gen_sub(type, dst, tmp, src2);
    } else {
        /*
         * While umax can be expanded with movcond, some hosts can
         * perform movcond with a zero source in fewer insns.
         */
        tcg_gen_sub(type, tmp, src1, src2);
        tcg_gen_movcond(type, TCG_COND_LTU, dst, src1, src2,
                        tcg_constant_internal(type, 0), tmp);
    }
}

void tcg_gen_xor(TCGType type, TCGTemp *dst, TCGTemp *src1, TCGTemp *src2)
{
    tcg_gen_op_ttt(INDEX_op_xor, type, dst, src1, src2);
}

void tcg_gen_xori(TCGType type, TCGTemp *dst, TCGTemp *src1, int64_t src2)
{
    if (src2 == 0) {
        tcg_gen_mov(type, dst, src1);
    } else if (src2 == -1 && tcg_op_supported(INDEX_op_not, type, 0)) {
        /* Do not recurse with gen_not.  */
        tcg_gen_op_tt(INDEX_op_not, type, dst, src1);
    } else {
        tcg_gen_xor(type, dst, src1, tcg_constant_internal(type, src2));
    }
}

/* 32 bit ops */

void tcg_gen_mulsu2_i32(TCGv_i32 rl, TCGv_i32 rh, TCGv_i32 arg1, TCGv_i32 arg2)
{
    TCGv_i64 t0 = tcg_temp_ebb_new_i64();
    TCGv_i64 t1 = tcg_temp_ebb_new_i64();
    tcg_gen_ext_i32_i64(t0, arg1);
    tcg_gen_extu_i32_i64(t1, arg2);
    tcg_gen_mul_i64(t0, t0, t1);
    tcg_gen_extr_i64_i32(rl, rh, t0);
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
}

static void gen_bitswap_i32(TCGv_i32 dst, TCGv_i32 src, uint32_t mask)
{
    tcg_gen_bitswap(TCG_TYPE_I32, tcgv_i32_temp(dst), tcgv_i32_temp(src), mask);
}

static void gen_bitswap_i64(TCGv_i64 dst, TCGv_i64 src, uint64_t mask)
{
    tcg_gen_bitswap(TCG_TYPE_I64, tcgv_i64_temp(dst), tcgv_i64_temp(src), mask);
}

/*
 * bswap32_i32: 32-bit byte swap on a 32-bit value.
 *
 * Byte pattern: abcd -> dcba
 */
void tcg_gen_bswap32_i32(TCGv_i32 ret, TCGv_i32 arg)
{
    if (tcg_op_supported(INDEX_op_bswap32, TCG_TYPE_I32, 0)) {
        tcg_gen_op3i_i32(INDEX_op_bswap32, ret, arg, 0);
    } else {
        gen_bitswap_i32(ret, arg, 0x00ff00ff);
        tcg_gen_hswap_i32(ret, ret);
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

void tcg_gen_revbit32_i32(TCGv_i32 ret, TCGv_i32 arg)
{
    if (tcg_op_supported(INDEX_op_revbit32, TCG_TYPE_I32, 0)) {
        tcg_gen_op3i_i32(INDEX_op_revbit32, ret, arg, 0);
    } else {
        tcg_gen_revbit8_i32(ret, arg);
        tcg_gen_bswap32_i32(ret, ret);
    }
}

/* 64-bit ops */

void tcg_gen_ld32u_i64(TCGv_i64 ret, TCGv_ptr arg2, tcg_target_long offset)
{
    tcg_gen_ldst_op_i64(INDEX_op_ld32u, ret, arg2, offset);
}

void tcg_gen_ld32s_i64(TCGv_i64 ret, TCGv_ptr arg2, tcg_target_long offset)
{
    tcg_gen_ldst_op_i64(INDEX_op_ld32s, ret, arg2, offset);
}

void tcg_gen_st32_i64(TCGv_i64 arg1, TCGv_ptr arg2, tcg_target_long offset)
{
    tcg_gen_ldst_op_i64(INDEX_op_st32, arg1, arg2, offset);
}

void tcg_gen_ext32s_i64(TCGv_i64 ret, TCGv_i64 arg)
{
    tcg_gen_sextract_i64(ret, arg, 0, 32);
}

void tcg_gen_ext32u_i64(TCGv_i64 ret, TCGv_i64 arg)
{
    tcg_gen_extract_i64(ret, arg, 0, 32);
}

/*
 * bswap32_i64: 32-bit byte swap on the low bits of a 64-bit value.
 *
 * Byte pattern: xxxxabcd -> yyyydcba
 *
 * With TCG_BSWAP_IZ, x == zero, else undefined.
 * With TCG_BSWAP_OZ, y == zero, with TCG_BSWAP_OS y == sign, else undefined.
 */
void tcg_gen_bswap32_i64(TCGv_i64 ret, TCGv_i64 arg, unsigned flags)
{
    /* Only one extension flag may be present. */
    tcg_debug_assert(!(flags & TCG_BSWAP_OS) || !(flags & TCG_BSWAP_OZ));

    if (tcg_op_supported(INDEX_op_bswap32, TCG_TYPE_I64, 0)) {
        tcg_gen_op3i_i64(INDEX_op_bswap32, ret, arg, flags);
    } else {
        TCGv_i64 t0 = tcg_temp_ebb_new_i64();
        TCGv_i64 t1 = tcg_temp_ebb_new_i64();

                                                /* arg = xxxxabcd */
        gen_bitswap_i64(ret, arg, 0x00ff00ff);  /* ret = ....badc */

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
    if (tcg_op_supported(INDEX_op_bswap64, TCG_TYPE_I64, 0)) {
        tcg_gen_op3i_i64(INDEX_op_bswap64, ret, arg, 0);
    } else {
        gen_bitswap_i64(ret, arg, 0x00ff00ff00ff00ffull);
        tcg_gen_hswap_i64(ret, ret);
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
    gen_bitswap_i64(ret, arg, 0x0000ffff0000ffffull);
    tcg_gen_wswap_i64(ret, ret);
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

void tcg_gen_revbit32_i64(TCGv_i64 ret, TCGv_i64 arg, unsigned flags)
{
    /* Only one extension flag may be present. */
    tcg_debug_assert(!(flags & TCG_BSWAP_OS) || !(flags & TCG_BSWAP_OZ));

    if (tcg_op_supported(INDEX_op_revbit32, TCG_TYPE_I64, 0)) {
        tcg_gen_op3i_i64(INDEX_op_revbit32, ret, arg, flags);
    } else if (tcg_op_supported(INDEX_op_revbit64, TCG_TYPE_I64, 0)) {
        tcg_gen_op2_i64(INDEX_op_revbit64, ret, arg);
        if (flags & TCG_BSWAP_OS) {
            tcg_gen_sari_i64(ret, ret, 32);
        } else {
            tcg_gen_shri_i64(ret, ret, 32);
        }
    } else {
        if (tcg_op_supported(INDEX_op_revbit8, TCG_TYPE_I64, 0)) {
            tcg_gen_op2_i64(INDEX_op_revbit8, ret, arg);
        } else {
            gen_bitswap_i64(ret, arg, 0x55555555ull);
            gen_bitswap_i64(ret, ret, 0x33333333ull);
            gen_bitswap_i64(ret, ret, 0x0f0f0f0full);
            flags |= TCG_BSWAP_IZ;
        }
        tcg_gen_bswap32_i64(ret, ret, flags);
    }
}

void tcg_gen_revbit64_i64(TCGv_i64 ret, TCGv_i64 arg)
{
    if (tcg_op_supported(INDEX_op_revbit64, TCG_TYPE_I64, 0)) {
        tcg_gen_op2_i64(INDEX_op_revbit64, ret, arg);
    } else {
        tcg_gen_revbit8_i64(ret, arg);
        tcg_gen_bswap64_i64(ret, ret);
    }
}

void tcg_gen_addN_i64(int n, TCGv_i64 *r, TCGv_i64 *a, TCGv_i64 *b)
{
    tcg_debug_assert(n > 2);

    /* ??? Don't allow overlap for now. */
    for (int i = 0; i < n - 1; ++i) {
        for (int j = i + 1; j < n; ++j) {
            tcg_debug_assert(r[i] != a[j]);
            tcg_debug_assert(r[i] != b[j]);
        }
    }

    if (tcg_op_supported(INDEX_op_addci, TCG_TYPE_I64, 0)) {
        tcg_gen_op3_i64(INDEX_op_addco, r[0], a[0], b[0]);
        for (int i = 1; i < n - 1; ++i) {
            tcg_gen_op3_i64(INDEX_op_addcio, r[i], a[i], b[i]);
        }
        tcg_gen_op3_i64(INDEX_op_addci, r[n - 1], a[n - 1], b[n - 1]);
    } else {
        TCGv_i64 t = tcg_temp_ebb_new_i64();
        TCGv_i64 c = tcg_temp_ebb_new_i64();

        tcg_gen_add_i64(t, a[0], b[0]);
        tcg_gen_setcond_i64(TCG_COND_LTU, c, t, a[0]);
        tcg_gen_mov_i64(r[0], t);

        for (int i = 1; i < n - 1; ++i) {
            tcg_gen_add_i64(t, a[i], c);
            tcg_gen_setcond_i64(TCG_COND_LTU, c, t, c);
            tcg_gen_add_i64(r[i], b[i], t);
            tcg_gen_setcond_i64(TCG_COND_LTU, t, r[i], t);
            tcg_gen_or_i64(c, c, t);
        }

        tcg_gen_add_i64(r[n - 1], a[n - 1], b[n - 1]);
        tcg_gen_add_i64(r[n - 1], r[n - 1], c);

        tcg_temp_free_i64(t);
        tcg_temp_free_i64(c);
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

/* Size changing operations.  */

void tcg_gen_extrl_i64_i32(TCGv_i32 ret, TCGv_i64 arg)
{
    tcg_gen_extrl(tcgv_i32_temp(ret), tcgv_i64_temp(arg));
}

void tcg_gen_extrh_i64_i32(TCGv_i32 ret, TCGv_i64 arg)
{
    tcg_gen_extrh(tcgv_i32_temp(ret), tcgv_i64_temp(arg));
}

void tcg_gen_extu_i32_i64(TCGv_i64 ret, TCGv_i32 arg)
{
    tcg_gen_extu(tcgv_i64_temp(ret), tcgv_i32_temp(arg));
}

void tcg_gen_ext_i32_i64(TCGv_i64 ret, TCGv_i32 arg)
{
    tcg_gen_exts(tcgv_i64_temp(ret), tcgv_i32_temp(arg));
}

void tcg_gen_concat_i32_i64(TCGv_i64 dest, TCGv_i32 low, TCGv_i32 high)
{
    TCGv_i64 tmp = tcg_temp_ebb_new_i64();

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
    tcg_gen_extrl_i64_i32(lo, arg);
    tcg_gen_extrh_i64_i32(hi, arg);
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

TCGv_i128 tcg_zero_i128(void)
{
    TCGv_i64 zero64 = tcg_constant_i64(0);
    TCGv_i128 zero128 = tcg_temp_new_i128();

    tcg_gen_concat_i64_i128(zero128, zero64, zero64);

    return zero128;
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

/* Host pointer operations */

void tcg_gen_add_ptr(TCGv_ptr r, TCGv_ptr a, TCGv_ptr b)
{
    tcg_gen_add(TCG_TYPE_PTR, tcgv_ptr_temp(r),
                tcgv_ptr_temp(a), tcgv_ptr_temp(b));
}

void tcg_gen_addi_ptr(TCGv_ptr r, TCGv_ptr a, intptr_t b)
{
    tcg_gen_addi(TCG_TYPE_PTR, tcgv_ptr_temp(r), tcgv_ptr_temp(a), b);
}

void tcg_gen_brcondi_ptr(TCGCond cond, TCGv_ptr a, intptr_t b, TCGLabel *label)
{
    tcg_gen_brcondi(TCG_TYPE_PTR, cond, tcgv_ptr_temp(a), b, label);
}

void tcg_gen_discard_ptr(TCGv_ptr s)
{
    tcg_gen_discard(TCG_TYPE_PTR, tcgv_ptr_temp(s));
}

void tcg_gen_ext_i32_ptr(TCGv_ptr r, TCGv_i32 a)
{
    tcg_gen_exts(tcgv_ptr_temp(r), tcgv_i32_temp(a));
}

void tcg_gen_extu_ptr_i64(TCGv_i64 r, TCGv_ptr a)
{
    tcg_gen_mov(TCG_TYPE_I64, tcgv_i64_temp(r), tcgv_ptr_temp(a));
}

void tcg_gen_mov_ptr(TCGv_ptr d, TCGv_ptr s)
{
    tcg_gen_mov(TCG_TYPE_PTR, tcgv_ptr_temp(d), tcgv_ptr_temp(s));
}

void tcg_gen_movi_ptr(TCGv_ptr d, intptr_t s)
{
    tcg_gen_movi(TCG_TYPE_PTR, tcgv_ptr_temp(d), s);
}

void tcg_gen_ld_ptr(TCGv_ptr r, TCGv_ptr a, intptr_t o)
{
    tcg_gen_ld(TCG_TYPE_PTR, tcgv_ptr_temp(r), tcgv_ptr_temp(a), o);
}

void tcg_gen_st_ptr(TCGv_ptr r, TCGv_ptr a, intptr_t o)
{
    tcg_gen_st(TCG_TYPE_PTR, tcgv_ptr_temp(r), tcgv_ptr_temp(a), o);
}

void tcg_gen_trunc_i64_ptr(TCGv_ptr r, TCGv_i64 a)
{
    tcg_gen_mov(TCG_TYPE_PTR, tcgv_ptr_temp(r), tcgv_i64_temp(a));
}

void tcg_gen_trunc_ptr_i32(TCGv_i32 r, TCGv_ptr a)
{
    tcg_gen_extrl(tcgv_i32_temp(r), tcgv_ptr_temp(a));
}
