/*
 * Octeon-specific instructions translation routines
 *
 *  Copyright (c) 2022 Pavel Dovgalyuk
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "translate.h"
#include "tcg/tcg-op-gvec.h"

/* Include the auto-generated decoder.  */
#include "decode-octeon.c.inc"

static bool trans_BBIT(DisasContext *ctx, arg_BBIT *a)
{
    TCGv_i64 p;

    if (ctx->hflags & MIPS_HFLAG_BMASK) {
        LOG_DISAS("Branch in delay / forbidden slot at PC 0x%" VADDR_PRIx "\n",
                  ctx->base.pc_next);
        generate_exception_end(ctx, EXCP_RI);
        return true;
    }

    /* Load needed operands */
    TCGv_i64 t0 = tcg_temp_new_i64();
    gen_load_gpr(t0, a->rs);

    p = tcg_constant_i64(1ULL << a->p);
    if (a->set) {
        tcg_gen_and_i64(bcond, p, t0);
    } else {
        tcg_gen_andc_i64(bcond, p, t0);
    }

    ctx->hflags |= MIPS_HFLAG_BC;
    ctx->btarget = ctx->base.pc_next + 4 + a->offset * 4;
    ctx->hflags |= MIPS_HFLAG_BDS32;
    return true;
}

static bool trans_BADDU(DisasContext *ctx, arg_BADDU *a)
{
    TCGv_i64 t0, t1;

    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();
    gen_load_gpr(t0, a->rs);
    gen_load_gpr(t1, a->rt);

    tcg_gen_add_i64(t0, t0, t1);
    tcg_gen_andi_i64(t0, t0, 0xff);
    gen_store_gpr(t0, a->rd);
    return true;
}

static bool trans_DMUL(DisasContext *ctx, arg_DMUL *a)
{
    TCGv_i64 t0, t1;

    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();
    gen_load_gpr(t0, a->rs);
    gen_load_gpr(t1, a->rt);

    tcg_gen_mul_i64(t0, t0, t1);
    gen_store_gpr(t0, a->rd);
    return true;
}

static bool trans_EXTS(DisasContext *ctx, arg_EXTS *a)
{
    TCGv_i64 t0;

    t0 = tcg_temp_new_i64();
    gen_load_gpr(t0, a->rs);
    tcg_gen_sextract_i64(t0, t0, a->p, a->lenm1 + 1);
    gen_store_gpr(t0, a->rt);
    return true;
}

static bool trans_CINS(DisasContext *ctx, arg_CINS *a)
{
    TCGv_i64 t0;

    t0 = tcg_temp_new_i64();
    gen_load_gpr(t0, a->rs);
    tcg_gen_deposit_z_i64(t0, t0, a->p, a->lenm1 + 1);
    gen_store_gpr(t0, a->rt);
    return true;
}

static bool trans_POP(DisasContext *ctx, arg_POP *a)
{
    TCGv_i64 t0;

    t0 = tcg_temp_new_i64();
    gen_load_gpr(t0, a->rs);
    if (!a->dw) {
        tcg_gen_andi_i64(t0, t0, 0xffffffff);
    }
    tcg_gen_ctpop_i64(t0, t0);
    gen_store_gpr(t0, a->rd);
    return true;
}

static bool do_seq_sne(DisasContext *ctx, const arg_decode_ext_octeon1 *a,
                       TCGCond cond)
{
    TCGv_i64 t0, t1;

    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();

    gen_load_gpr(t0, a->rs);
    gen_load_gpr(t1, a->rt);

    tcg_gen_setcond_i64(cond, t0, t1, t0);
    gen_store_gpr(t0, a->rd);
    return true;
}

static bool trans_SEQ(DisasContext *ctx, arg_SEQ *a)
{
    return do_seq_sne(ctx, a, TCG_COND_EQ);
}

static bool trans_SNE(DisasContext *ctx, arg_SNE *a)
{
    return do_seq_sne(ctx, a, TCG_COND_NE);
}

static bool do_seqi_snei(DisasContext *ctx, const arg_cmpi *a, TCGCond cond)
{
    TCGv_i64 t0;

    t0 = tcg_temp_new_i64();
    gen_load_gpr(t0, a->rs);

    tcg_gen_setcondi_i64(cond, t0, t0, a->imm);
    gen_store_gpr(t0, a->rt);
    return true;
}

static bool trans_SEQI(DisasContext *ctx, arg_SEQI *a)
{
    return do_seqi_snei(ctx, a, TCG_COND_EQ);
}

static bool trans_SNEI(DisasContext *ctx, arg_SNEI *a)
{
    return do_seqi_snei(ctx, a, TCG_COND_NE);
}

static bool trans_lx(DisasContext *ctx, arg_lx *a, MemOp mop)
{
    gen_lx(ctx, a->rd, a->base, a->index, mop);

    return true;
}

TRANS(LBX,  trans_lx, MO_SB);
TRANS(LBUX, trans_lx, MO_UB);
TRANS(LHX,  trans_lx, MO_SW);
TRANS(LHUX, trans_lx, MO_UW);
TRANS(LWX,  trans_lx, MO_SL);
TRANS(LDX,  trans_lx, MO_UQ);
