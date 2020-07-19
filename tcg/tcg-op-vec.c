/*
 * Tiny Code Generator for QEMU
 *
 * Copyright (c) 2018 Linaro, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "tcg/tcg.h"
#include "tcg/tcg-op.h"
#include "tcg/tcg-mo.h"

/* Reduce the number of ifdefs below.  This assumes that all uses of
   TCGV_HIGH and TCGV_LOW are properly protected by a conditional that
   the compiler can eliminate.  */
#if TCG_TARGET_REG_BITS == 64
extern TCGv_i32 TCGV_LOW_link_error(TCGv_i64);
extern TCGv_i32 TCGV_HIGH_link_error(TCGv_i64);
#define TCGV_LOW  TCGV_LOW_link_error
#define TCGV_HIGH TCGV_HIGH_link_error
#endif

/*
 * Vector optional opcode tracking.
 * Except for the basic logical operations (and, or, xor), and
 * data movement (mov, ld, st, dupi), many vector opcodes are
 * optional and may not be supported on the host.  Thank Intel
 * for the irregularity in their instruction set.
 *
 * The gvec expanders allow custom vector operations to be composed,
 * generally via the .fniv callback in the GVecGen* structures.  At
 * the same time, in deciding whether to use this hook we need to
 * know if the host supports the required operations.  This is
 * presented as an array of opcodes, terminated by 0.  Each opcode
 * is assumed to be expanded with the given VECE.
 *
 * For debugging, we want to validate this array.  Therefore, when
 * tcg_ctx->vec_opt_opc is non-NULL, the tcg_gen_*_vec expanders
 * will validate that their opcode is present in the list.
 */
#ifdef CONFIG_DEBUG_TCG
void tcg_assert_listed_vecop(TCGOpcode op)
{
    const TCGOpcode *p = tcg_ctx->vecop_list;
    if (p) {
        for (; *p; ++p) {
            if (*p == op) {
                return;
            }
        }
        g_assert_not_reached();
    }
}
#endif

bool tcg_can_emit_vecop_list(const TCGOpcode *list,
                             TCGType type, unsigned vece)
{
    if (list == NULL) {
        return true;
    }

    for (; *list; ++list) {
        TCGOpcode opc = *list;

#ifdef CONFIG_DEBUG_TCG
        switch (opc) {
        case INDEX_op_and_vec:
        case INDEX_op_or_vec:
        case INDEX_op_xor_vec:
        case INDEX_op_mov_vec:
        case INDEX_op_dup_vec:
        case INDEX_op_dupi_vec:
        case INDEX_op_dup2_vec:
        case INDEX_op_ld_vec:
        case INDEX_op_st_vec:
        case INDEX_op_bitsel_vec:
            /* These opcodes are mandatory and should not be listed.  */
            g_assert_not_reached();
        case INDEX_op_not_vec:
            /* These opcodes have generic expansions using the above.  */
            g_assert_not_reached();
        default:
            break;
        }
#endif

        if (tcg_can_emit_vec_op(opc, type, vece)) {
            continue;
        }

        /*
         * The opcode list is created by front ends based on what they
         * actually invoke.  We must mirror the logic in the routines
         * below for generic expansions using other opcodes.
         */
        switch (opc) {
        case INDEX_op_neg_vec:
            if (tcg_can_emit_vec_op(INDEX_op_sub_vec, type, vece)) {
                continue;
            }
            break;
        case INDEX_op_abs_vec:
            if (tcg_can_emit_vec_op(INDEX_op_sub_vec, type, vece)
                && (tcg_can_emit_vec_op(INDEX_op_smax_vec, type, vece) > 0
                    || tcg_can_emit_vec_op(INDEX_op_sari_vec, type, vece) > 0
                    || tcg_can_emit_vec_op(INDEX_op_cmp_vec, type, vece))) {
                continue;
            }
            break;
        case INDEX_op_cmpsel_vec:
        case INDEX_op_smin_vec:
        case INDEX_op_smax_vec:
        case INDEX_op_umin_vec:
        case INDEX_op_umax_vec:
            if (tcg_can_emit_vec_op(INDEX_op_cmp_vec, type, vece)) {
                continue;
            }
            break;
        default:
            break;
        }
        return false;
    }
    return true;
}

void vec_gen_2(TCGOpcode opc, TCGType type, unsigned vece, TCGArg r, TCGArg a)
{
    TCGOp *op = tcg_emit_op(opc);
    TCGOP_VECL(op) = type - TCG_TYPE_V64;
    TCGOP_VECE(op) = vece;
    op->args[0] = r;
    op->args[1] = a;
}

void vec_gen_3(TCGOpcode opc, TCGType type, unsigned vece,
               TCGArg r, TCGArg a, TCGArg b)
{
    TCGOp *op = tcg_emit_op(opc);
    TCGOP_VECL(op) = type - TCG_TYPE_V64;
    TCGOP_VECE(op) = vece;
    op->args[0] = r;
    op->args[1] = a;
    op->args[2] = b;
}

void vec_gen_4(TCGOpcode opc, TCGType type, unsigned vece,
               TCGArg r, TCGArg a, TCGArg b, TCGArg c)
{
    TCGOp *op = tcg_emit_op(opc);
    TCGOP_VECL(op) = type - TCG_TYPE_V64;
    TCGOP_VECE(op) = vece;
    op->args[0] = r;
    op->args[1] = a;
    op->args[2] = b;
    op->args[3] = c;
}

static void vec_gen_6(TCGOpcode opc, TCGType type, unsigned vece, TCGArg r,
                      TCGArg a, TCGArg b, TCGArg c, TCGArg d, TCGArg e)
{
    TCGOp *op = tcg_emit_op(opc);
    TCGOP_VECL(op) = type - TCG_TYPE_V64;
    TCGOP_VECE(op) = vece;
    op->args[0] = r;
    op->args[1] = a;
    op->args[2] = b;
    op->args[3] = c;
    op->args[4] = d;
    op->args[5] = e;
}

static void vec_gen_op2(TCGOpcode opc, unsigned vece, TCGv_vec r, TCGv_vec a)
{
    TCGTemp *rt = tcgv_vec_temp(r);
    TCGTemp *at = tcgv_vec_temp(a);
    TCGType type = rt->base_type;

    /* Must enough inputs for the output.  */
    tcg_debug_assert(at->base_type >= type);
    vec_gen_2(opc, type, vece, temp_arg(rt), temp_arg(at));
}

static void vec_gen_op3(TCGOpcode opc, unsigned vece,
                        TCGv_vec r, TCGv_vec a, TCGv_vec b)
{
    TCGTemp *rt = tcgv_vec_temp(r);
    TCGTemp *at = tcgv_vec_temp(a);
    TCGTemp *bt = tcgv_vec_temp(b);
    TCGType type = rt->base_type;

    /* Must enough inputs for the output.  */
    tcg_debug_assert(at->base_type >= type);
    tcg_debug_assert(bt->base_type >= type);
    vec_gen_3(opc, type, vece, temp_arg(rt), temp_arg(at), temp_arg(bt));
}

void tcg_gen_mov_vec(TCGv_vec r, TCGv_vec a)
{
    if (r != a) {
        vec_gen_op2(INDEX_op_mov_vec, 0, r, a);
    }
}

#define MO_REG  (TCG_TARGET_REG_BITS == 64 ? MO_64 : MO_32)

static void do_dupi_vec(TCGv_vec r, unsigned vece, TCGArg a)
{
    TCGTemp *rt = tcgv_vec_temp(r);
    vec_gen_2(INDEX_op_dupi_vec, rt->base_type, vece, temp_arg(rt), a);
}

TCGv_vec tcg_const_zeros_vec(TCGType type)
{
    TCGv_vec ret = tcg_temp_new_vec(type);
    do_dupi_vec(ret, MO_REG, 0);
    return ret;
}

TCGv_vec tcg_const_ones_vec(TCGType type)
{
    TCGv_vec ret = tcg_temp_new_vec(type);
    do_dupi_vec(ret, MO_REG, -1);
    return ret;
}

TCGv_vec tcg_const_zeros_vec_matching(TCGv_vec m)
{
    TCGTemp *t = tcgv_vec_temp(m);
    return tcg_const_zeros_vec(t->base_type);
}

TCGv_vec tcg_const_ones_vec_matching(TCGv_vec m)
{
    TCGTemp *t = tcgv_vec_temp(m);
    return tcg_const_ones_vec(t->base_type);
}

void tcg_gen_dup64i_vec(TCGv_vec r, uint64_t a)
{
    if (TCG_TARGET_REG_BITS == 32 && a == deposit64(a, 32, 32, a)) {
        do_dupi_vec(r, MO_32, a);
    } else if (TCG_TARGET_REG_BITS == 64 || a == (uint64_t)(int32_t)a) {
        do_dupi_vec(r, MO_64, a);
    } else {
        TCGv_i64 c = tcg_const_i64(a);
        tcg_gen_dup_i64_vec(MO_64, r, c);
        tcg_temp_free_i64(c);
    }
}

void tcg_gen_dup32i_vec(TCGv_vec r, uint32_t a)
{
    do_dupi_vec(r, MO_REG, dup_const(MO_32, a));
}

void tcg_gen_dup16i_vec(TCGv_vec r, uint32_t a)
{
    do_dupi_vec(r, MO_REG, dup_const(MO_16, a));
}

void tcg_gen_dup8i_vec(TCGv_vec r, uint32_t a)
{
    do_dupi_vec(r, MO_REG, dup_const(MO_8, a));
}

void tcg_gen_dupi_vec(unsigned vece, TCGv_vec r, uint64_t a)
{
    do_dupi_vec(r, MO_REG, dup_const(vece, a));
}

void tcg_gen_dup_i64_vec(unsigned vece, TCGv_vec r, TCGv_i64 a)
{
    TCGArg ri = tcgv_vec_arg(r);
    TCGTemp *rt = arg_temp(ri);
    TCGType type = rt->base_type;

    if (TCG_TARGET_REG_BITS == 64) {
        TCGArg ai = tcgv_i64_arg(a);
        vec_gen_2(INDEX_op_dup_vec, type, vece, ri, ai);
    } else if (vece == MO_64) {
        TCGArg al = tcgv_i32_arg(TCGV_LOW(a));
        TCGArg ah = tcgv_i32_arg(TCGV_HIGH(a));
        vec_gen_3(INDEX_op_dup2_vec, type, MO_64, ri, al, ah);
    } else {
        TCGArg ai = tcgv_i32_arg(TCGV_LOW(a));
        vec_gen_2(INDEX_op_dup_vec, type, vece, ri, ai);
    }
}

void tcg_gen_dup_i32_vec(unsigned vece, TCGv_vec r, TCGv_i32 a)
{
    TCGArg ri = tcgv_vec_arg(r);
    TCGArg ai = tcgv_i32_arg(a);
    TCGTemp *rt = arg_temp(ri);
    TCGType type = rt->base_type;

    vec_gen_2(INDEX_op_dup_vec, type, vece, ri, ai);
}

void tcg_gen_dup_mem_vec(unsigned vece, TCGv_vec r, TCGv_ptr b,
                         tcg_target_long ofs)
{
    TCGArg ri = tcgv_vec_arg(r);
    TCGArg bi = tcgv_ptr_arg(b);
    TCGTemp *rt = arg_temp(ri);
    TCGType type = rt->base_type;

    vec_gen_3(INDEX_op_dupm_vec, type, vece, ri, bi, ofs);
}

static void vec_gen_ldst(TCGOpcode opc, TCGv_vec r, TCGv_ptr b, TCGArg o)
{
    TCGArg ri = tcgv_vec_arg(r);
    TCGArg bi = tcgv_ptr_arg(b);
    TCGTemp *rt = arg_temp(ri);
    TCGType type = rt->base_type;

    vec_gen_3(opc, type, 0, ri, bi, o);
}

void tcg_gen_ld_vec(TCGv_vec r, TCGv_ptr b, TCGArg o)
{
    vec_gen_ldst(INDEX_op_ld_vec, r, b, o);
}

void tcg_gen_st_vec(TCGv_vec r, TCGv_ptr b, TCGArg o)
{
    vec_gen_ldst(INDEX_op_st_vec, r, b, o);
}

void tcg_gen_stl_vec(TCGv_vec r, TCGv_ptr b, TCGArg o, TCGType low_type)
{
    TCGArg ri = tcgv_vec_arg(r);
    TCGArg bi = tcgv_ptr_arg(b);
    TCGTemp *rt = arg_temp(ri);
    TCGType type = rt->base_type;

    tcg_debug_assert(low_type >= TCG_TYPE_V64);
    tcg_debug_assert(low_type <= type);
    vec_gen_3(INDEX_op_st_vec, low_type, 0, ri, bi, o);
}

void tcg_gen_and_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b)
{
    vec_gen_op3(INDEX_op_and_vec, 0, r, a, b);
}

void tcg_gen_or_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b)
{
    vec_gen_op3(INDEX_op_or_vec, 0, r, a, b);
}

void tcg_gen_xor_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b)
{
    vec_gen_op3(INDEX_op_xor_vec, 0, r, a, b);
}

void tcg_gen_andc_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b)
{
    if (TCG_TARGET_HAS_andc_vec) {
        vec_gen_op3(INDEX_op_andc_vec, 0, r, a, b);
    } else {
        TCGv_vec t = tcg_temp_new_vec_matching(r);
        tcg_gen_not_vec(0, t, b);
        tcg_gen_and_vec(0, r, a, t);
        tcg_temp_free_vec(t);
    }
}

void tcg_gen_orc_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b)
{
    if (TCG_TARGET_HAS_orc_vec) {
        vec_gen_op3(INDEX_op_orc_vec, 0, r, a, b);
    } else {
        TCGv_vec t = tcg_temp_new_vec_matching(r);
        tcg_gen_not_vec(0, t, b);
        tcg_gen_or_vec(0, r, a, t);
        tcg_temp_free_vec(t);
    }
}

void tcg_gen_nand_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b)
{
    /* TODO: Add TCG_TARGET_HAS_nand_vec when adding a backend supports it. */
    tcg_gen_and_vec(0, r, a, b);
    tcg_gen_not_vec(0, r, r);
}

void tcg_gen_nor_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b)
{
    /* TODO: Add TCG_TARGET_HAS_nor_vec when adding a backend supports it. */
    tcg_gen_or_vec(0, r, a, b);
    tcg_gen_not_vec(0, r, r);
}

void tcg_gen_eqv_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b)
{
    /* TODO: Add TCG_TARGET_HAS_eqv_vec when adding a backend supports it. */
    tcg_gen_xor_vec(0, r, a, b);
    tcg_gen_not_vec(0, r, r);
}

static bool do_op2(unsigned vece, TCGv_vec r, TCGv_vec a, TCGOpcode opc)
{
    TCGTemp *rt = tcgv_vec_temp(r);
    TCGTemp *at = tcgv_vec_temp(a);
    TCGArg ri = temp_arg(rt);
    TCGArg ai = temp_arg(at);
    TCGType type = rt->base_type;
    int can;

    tcg_debug_assert(at->base_type >= type);
    tcg_assert_listed_vecop(opc);
    can = tcg_can_emit_vec_op(opc, type, vece);
    if (can > 0) {
        vec_gen_2(opc, type, vece, ri, ai);
    } else if (can < 0) {
        const TCGOpcode *hold_list = tcg_swap_vecop_list(NULL);
        tcg_expand_vec_op(opc, type, vece, ri, ai);
        tcg_swap_vecop_list(hold_list);
    } else {
        return false;
    }
    return true;
}

void tcg_gen_not_vec(unsigned vece, TCGv_vec r, TCGv_vec a)
{
    const TCGOpcode *hold_list = tcg_swap_vecop_list(NULL);

    if (!TCG_TARGET_HAS_not_vec || !do_op2(vece, r, a, INDEX_op_not_vec)) {
        TCGv_vec t = tcg_const_ones_vec_matching(r);
        tcg_gen_xor_vec(0, r, a, t);
        tcg_temp_free_vec(t);
    }
    tcg_swap_vecop_list(hold_list);
}

void tcg_gen_neg_vec(unsigned vece, TCGv_vec r, TCGv_vec a)
{
    const TCGOpcode *hold_list;

    tcg_assert_listed_vecop(INDEX_op_neg_vec);
    hold_list = tcg_swap_vecop_list(NULL);

    if (!TCG_TARGET_HAS_neg_vec || !do_op2(vece, r, a, INDEX_op_neg_vec)) {
        TCGv_vec t = tcg_const_zeros_vec_matching(r);
        tcg_gen_sub_vec(vece, r, t, a);
        tcg_temp_free_vec(t);
    }
    tcg_swap_vecop_list(hold_list);
}

void tcg_gen_abs_vec(unsigned vece, TCGv_vec r, TCGv_vec a)
{
    const TCGOpcode *hold_list;

    tcg_assert_listed_vecop(INDEX_op_abs_vec);
    hold_list = tcg_swap_vecop_list(NULL);

    if (!do_op2(vece, r, a, INDEX_op_abs_vec)) {
        TCGType type = tcgv_vec_temp(r)->base_type;
        TCGv_vec t = tcg_temp_new_vec(type);

        tcg_debug_assert(tcg_can_emit_vec_op(INDEX_op_sub_vec, type, vece));
        if (tcg_can_emit_vec_op(INDEX_op_smax_vec, type, vece) > 0) {
            tcg_gen_neg_vec(vece, t, a);
            tcg_gen_smax_vec(vece, r, a, t);
        } else {
            if (tcg_can_emit_vec_op(INDEX_op_sari_vec, type, vece) > 0) {
                tcg_gen_sari_vec(vece, t, a, (8 << vece) - 1);
            } else {
                do_dupi_vec(t, MO_REG, 0);
                tcg_gen_cmp_vec(TCG_COND_LT, vece, t, a, t);
            }
            tcg_gen_xor_vec(vece, r, a, t);
            tcg_gen_sub_vec(vece, r, r, t);
        }

        tcg_temp_free_vec(t);
    }
    tcg_swap_vecop_list(hold_list);
}

static void do_shifti(TCGOpcode opc, unsigned vece,
                      TCGv_vec r, TCGv_vec a, int64_t i)
{
    TCGTemp *rt = tcgv_vec_temp(r);
    TCGTemp *at = tcgv_vec_temp(a);
    TCGArg ri = temp_arg(rt);
    TCGArg ai = temp_arg(at);
    TCGType type = rt->base_type;
    int can;

    tcg_debug_assert(at->base_type == type);
    tcg_debug_assert(i >= 0 && i < (8 << vece));
    tcg_assert_listed_vecop(opc);

    if (i == 0) {
        tcg_gen_mov_vec(r, a);
        return;
    }

    can = tcg_can_emit_vec_op(opc, type, vece);
    if (can > 0) {
        vec_gen_3(opc, type, vece, ri, ai, i);
    } else {
        /* We leave the choice of expansion via scalar or vector shift
           to the target.  Often, but not always, dupi can feed a vector
           shift easier than a scalar.  */
        const TCGOpcode *hold_list = tcg_swap_vecop_list(NULL);
        tcg_debug_assert(can < 0);
        tcg_expand_vec_op(opc, type, vece, ri, ai, i);
        tcg_swap_vecop_list(hold_list);
    }
}

void tcg_gen_shli_vec(unsigned vece, TCGv_vec r, TCGv_vec a, int64_t i)
{
    do_shifti(INDEX_op_shli_vec, vece, r, a, i);
}

void tcg_gen_shri_vec(unsigned vece, TCGv_vec r, TCGv_vec a, int64_t i)
{
    do_shifti(INDEX_op_shri_vec, vece, r, a, i);
}

void tcg_gen_sari_vec(unsigned vece, TCGv_vec r, TCGv_vec a, int64_t i)
{
    do_shifti(INDEX_op_sari_vec, vece, r, a, i);
}

void tcg_gen_rotli_vec(unsigned vece, TCGv_vec r, TCGv_vec a, int64_t i)
{
    do_shifti(INDEX_op_rotli_vec, vece, r, a, i);
}

void tcg_gen_rotri_vec(unsigned vece, TCGv_vec r, TCGv_vec a, int64_t i)
{
    int bits = 8 << vece;
    tcg_debug_assert(i >= 0 && i < bits);
    do_shifti(INDEX_op_rotli_vec, vece, r, a, -i & (bits - 1));
}

void tcg_gen_cmp_vec(TCGCond cond, unsigned vece,
                     TCGv_vec r, TCGv_vec a, TCGv_vec b)
{
    TCGTemp *rt = tcgv_vec_temp(r);
    TCGTemp *at = tcgv_vec_temp(a);
    TCGTemp *bt = tcgv_vec_temp(b);
    TCGArg ri = temp_arg(rt);
    TCGArg ai = temp_arg(at);
    TCGArg bi = temp_arg(bt);
    TCGType type = rt->base_type;
    int can;

    tcg_debug_assert(at->base_type >= type);
    tcg_debug_assert(bt->base_type >= type);
    tcg_assert_listed_vecop(INDEX_op_cmp_vec);
    can = tcg_can_emit_vec_op(INDEX_op_cmp_vec, type, vece);
    if (can > 0) {
        vec_gen_4(INDEX_op_cmp_vec, type, vece, ri, ai, bi, cond);
    } else {
        const TCGOpcode *hold_list = tcg_swap_vecop_list(NULL);
        tcg_debug_assert(can < 0);
        tcg_expand_vec_op(INDEX_op_cmp_vec, type, vece, ri, ai, bi, cond);
        tcg_swap_vecop_list(hold_list);
    }
}

static bool do_op3(unsigned vece, TCGv_vec r, TCGv_vec a,
                   TCGv_vec b, TCGOpcode opc)
{
    TCGTemp *rt = tcgv_vec_temp(r);
    TCGTemp *at = tcgv_vec_temp(a);
    TCGTemp *bt = tcgv_vec_temp(b);
    TCGArg ri = temp_arg(rt);
    TCGArg ai = temp_arg(at);
    TCGArg bi = temp_arg(bt);
    TCGType type = rt->base_type;
    int can;

    tcg_debug_assert(at->base_type >= type);
    tcg_debug_assert(bt->base_type >= type);
    tcg_assert_listed_vecop(opc);
    can = tcg_can_emit_vec_op(opc, type, vece);
    if (can > 0) {
        vec_gen_3(opc, type, vece, ri, ai, bi);
    } else if (can < 0) {
        const TCGOpcode *hold_list = tcg_swap_vecop_list(NULL);
        tcg_expand_vec_op(opc, type, vece, ri, ai, bi);
        tcg_swap_vecop_list(hold_list);
    } else {
        return false;
    }
    return true;
}

static void do_op3_nofail(unsigned vece, TCGv_vec r, TCGv_vec a,
                          TCGv_vec b, TCGOpcode opc)
{
    bool ok = do_op3(vece, r, a, b, opc);
    tcg_debug_assert(ok);
}

void tcg_gen_add_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b)
{
    do_op3_nofail(vece, r, a, b, INDEX_op_add_vec);
}

void tcg_gen_sub_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b)
{
    do_op3_nofail(vece, r, a, b, INDEX_op_sub_vec);
}

void tcg_gen_mul_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b)
{
    do_op3_nofail(vece, r, a, b, INDEX_op_mul_vec);
}

void tcg_gen_ssadd_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b)
{
    do_op3_nofail(vece, r, a, b, INDEX_op_ssadd_vec);
}

void tcg_gen_usadd_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b)
{
    do_op3_nofail(vece, r, a, b, INDEX_op_usadd_vec);
}

void tcg_gen_sssub_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b)
{
    do_op3_nofail(vece, r, a, b, INDEX_op_sssub_vec);
}

void tcg_gen_ussub_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b)
{
    do_op3_nofail(vece, r, a, b, INDEX_op_ussub_vec);
}

static void do_minmax(unsigned vece, TCGv_vec r, TCGv_vec a,
                      TCGv_vec b, TCGOpcode opc, TCGCond cond)
{
    if (!do_op3(vece, r, a, b, opc)) {
        const TCGOpcode *hold_list = tcg_swap_vecop_list(NULL);
        tcg_gen_cmpsel_vec(cond, vece, r, a, b, a, b);
        tcg_swap_vecop_list(hold_list);
    }
}

void tcg_gen_smin_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b)
{
    do_minmax(vece, r, a, b, INDEX_op_smin_vec, TCG_COND_LT);
}

void tcg_gen_umin_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b)
{
    do_minmax(vece, r, a, b, INDEX_op_umin_vec, TCG_COND_LTU);
}

void tcg_gen_smax_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b)
{
    do_minmax(vece, r, a, b, INDEX_op_smax_vec, TCG_COND_GT);
}

void tcg_gen_umax_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b)
{
    do_minmax(vece, r, a, b, INDEX_op_umax_vec, TCG_COND_GTU);
}

void tcg_gen_shlv_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b)
{
    do_op3_nofail(vece, r, a, b, INDEX_op_shlv_vec);
}

void tcg_gen_shrv_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b)
{
    do_op3_nofail(vece, r, a, b, INDEX_op_shrv_vec);
}

void tcg_gen_sarv_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b)
{
    do_op3_nofail(vece, r, a, b, INDEX_op_sarv_vec);
}

void tcg_gen_rotlv_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b)
{
    do_op3_nofail(vece, r, a, b, INDEX_op_rotlv_vec);
}

void tcg_gen_rotrv_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b)
{
    do_op3_nofail(vece, r, a, b, INDEX_op_rotrv_vec);
}

static void do_shifts(unsigned vece, TCGv_vec r, TCGv_vec a,
                      TCGv_i32 s, TCGOpcode opc)
{
    TCGTemp *rt = tcgv_vec_temp(r);
    TCGTemp *at = tcgv_vec_temp(a);
    TCGTemp *st = tcgv_i32_temp(s);
    TCGArg ri = temp_arg(rt);
    TCGArg ai = temp_arg(at);
    TCGArg si = temp_arg(st);
    TCGType type = rt->base_type;
    int can;

    tcg_debug_assert(at->base_type >= type);
    tcg_assert_listed_vecop(opc);
    can = tcg_can_emit_vec_op(opc, type, vece);
    if (can > 0) {
        vec_gen_3(opc, type, vece, ri, ai, si);
    } else if (can < 0) {
        const TCGOpcode *hold_list = tcg_swap_vecop_list(NULL);
        tcg_expand_vec_op(opc, type, vece, ri, ai, si);
        tcg_swap_vecop_list(hold_list);
    } else {
        g_assert_not_reached();
    }
}

void tcg_gen_shls_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_i32 b)
{
    do_shifts(vece, r, a, b, INDEX_op_shls_vec);
}

void tcg_gen_shrs_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_i32 b)
{
    do_shifts(vece, r, a, b, INDEX_op_shrs_vec);
}

void tcg_gen_sars_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_i32 b)
{
    do_shifts(vece, r, a, b, INDEX_op_sars_vec);
}

void tcg_gen_rotls_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_i32 s)
{
    do_shifts(vece, r, a, s, INDEX_op_rotls_vec);
}

void tcg_gen_bitsel_vec(unsigned vece, TCGv_vec r, TCGv_vec a,
                        TCGv_vec b, TCGv_vec c)
{
    TCGTemp *rt = tcgv_vec_temp(r);
    TCGTemp *at = tcgv_vec_temp(a);
    TCGTemp *bt = tcgv_vec_temp(b);
    TCGTemp *ct = tcgv_vec_temp(c);
    TCGType type = rt->base_type;

    tcg_debug_assert(at->base_type >= type);
    tcg_debug_assert(bt->base_type >= type);
    tcg_debug_assert(ct->base_type >= type);

    if (TCG_TARGET_HAS_bitsel_vec) {
        vec_gen_4(INDEX_op_bitsel_vec, type, MO_8,
                  temp_arg(rt), temp_arg(at), temp_arg(bt), temp_arg(ct));
    } else {
        TCGv_vec t = tcg_temp_new_vec(type);
        tcg_gen_and_vec(MO_8, t, a, b);
        tcg_gen_andc_vec(MO_8, r, c, a);
        tcg_gen_or_vec(MO_8, r, r, t);
        tcg_temp_free_vec(t);
    }
}

void tcg_gen_cmpsel_vec(TCGCond cond, unsigned vece, TCGv_vec r,
                        TCGv_vec a, TCGv_vec b, TCGv_vec c, TCGv_vec d)
{
    TCGTemp *rt = tcgv_vec_temp(r);
    TCGTemp *at = tcgv_vec_temp(a);
    TCGTemp *bt = tcgv_vec_temp(b);
    TCGTemp *ct = tcgv_vec_temp(c);
    TCGTemp *dt = tcgv_vec_temp(d);
    TCGArg ri = temp_arg(rt);
    TCGArg ai = temp_arg(at);
    TCGArg bi = temp_arg(bt);
    TCGArg ci = temp_arg(ct);
    TCGArg di = temp_arg(dt);
    TCGType type = rt->base_type;
    const TCGOpcode *hold_list;
    int can;

    tcg_debug_assert(at->base_type >= type);
    tcg_debug_assert(bt->base_type >= type);
    tcg_debug_assert(ct->base_type >= type);
    tcg_debug_assert(dt->base_type >= type);

    tcg_assert_listed_vecop(INDEX_op_cmpsel_vec);
    hold_list = tcg_swap_vecop_list(NULL);
    can = tcg_can_emit_vec_op(INDEX_op_cmpsel_vec, type, vece);

    if (can > 0) {
        vec_gen_6(INDEX_op_cmpsel_vec, type, vece, ri, ai, bi, ci, di, cond);
    } else if (can < 0) {
        tcg_expand_vec_op(INDEX_op_cmpsel_vec, type, vece,
                          ri, ai, bi, ci, di, cond);
    } else {
        TCGv_vec t = tcg_temp_new_vec(type);
        tcg_gen_cmp_vec(cond, vece, t, a, b);
        tcg_gen_bitsel_vec(vece, r, t, c, d);
        tcg_temp_free_vec(t);
    }
    tcg_swap_vecop_list(hold_list);
}
