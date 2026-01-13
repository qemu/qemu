/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */

static bool gen_load(DisasContext *ctx, arg_rr_i *a, MemOp mop)
{
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);
    TCGv addr = gpr_src(ctx, a->rj, EXT_NONE);

    addr = make_address_i(ctx, addr, a->imm);

    tcg_gen_qemu_ld_tl(dest, addr, ctx->mem_idx, mop);
    gen_set_gpr(a->rd, dest, EXT_NONE);
    return true;
}

static bool gen_store(DisasContext *ctx, arg_rr_i *a, MemOp mop)
{
    TCGv data = gpr_src(ctx, a->rd, EXT_NONE);
    TCGv addr = gpr_src(ctx, a->rj, EXT_NONE);

    addr = make_address_i(ctx, addr, a->imm);

    tcg_gen_qemu_st_tl(data, addr, ctx->mem_idx, mop);
    return true;
}

static bool gen_loadx(DisasContext *ctx, arg_rrr *a, MemOp mop)
{
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);
    TCGv src1 = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv src2 = gpr_src(ctx, a->rk, EXT_NONE);
    TCGv addr = make_address_x(ctx, src1, src2);

    tcg_gen_qemu_ld_tl(dest, addr, ctx->mem_idx, mop);
    gen_set_gpr(a->rd, dest, EXT_NONE);

    return true;
}

static bool gen_storex(DisasContext *ctx, arg_rrr *a, MemOp mop)
{
    TCGv data = gpr_src(ctx, a->rd, EXT_NONE);
    TCGv src1 = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv src2 = gpr_src(ctx, a->rk, EXT_NONE);
    TCGv addr = make_address_x(ctx, src1, src2);

    tcg_gen_qemu_st_tl(data, addr, ctx->mem_idx, mop);

    return true;
}

static bool gen_load_gt(DisasContext *ctx, arg_rrr *a, MemOp mop)
{
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);
    TCGv src1 = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv src2 = gpr_src(ctx, a->rk, EXT_NONE);

    gen_helper_asrtgt_d(tcg_env, src1, src2);
    src1 = make_address_i(ctx, src1, 0);
    tcg_gen_qemu_ld_tl(dest, src1, ctx->mem_idx, mop);
    gen_set_gpr(a->rd, dest, EXT_NONE);

    return true;
}

static bool gen_load_le(DisasContext *ctx, arg_rrr *a, MemOp mop)
{
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);
    TCGv src1 = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv src2 = gpr_src(ctx, a->rk, EXT_NONE);

    gen_helper_asrtle_d(tcg_env, src1, src2);
    src1 = make_address_i(ctx, src1, 0);
    tcg_gen_qemu_ld_tl(dest, src1, ctx->mem_idx, mop);
    gen_set_gpr(a->rd, dest, EXT_NONE);

    return true;
}

static bool gen_store_gt(DisasContext *ctx, arg_rrr *a, MemOp mop)
{
    TCGv data = gpr_src(ctx, a->rd, EXT_NONE);
    TCGv src1 = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv src2 = gpr_src(ctx, a->rk, EXT_NONE);

    gen_helper_asrtgt_d(tcg_env, src1, src2);
    src1 = make_address_i(ctx, src1, 0);
    tcg_gen_qemu_st_tl(data, src1, ctx->mem_idx, mop);

    return true;
}

static bool gen_store_le(DisasContext *ctx, arg_rrr *a, MemOp mop)
{
    TCGv data = gpr_src(ctx, a->rd, EXT_NONE);
    TCGv src1 = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv src2 = gpr_src(ctx, a->rk, EXT_NONE);

    gen_helper_asrtle_d(tcg_env, src1, src2);
    src1 = make_address_i(ctx, src1, 0);
    tcg_gen_qemu_st_tl(data, src1, ctx->mem_idx, mop);

    return true;
}

static bool trans_preld(DisasContext *ctx, arg_preld *a)
{
    return true;
}

static bool trans_preldx(DisasContext *ctx, arg_preldx * a)
{
    return true;
}

static bool trans_dbar(DisasContext *ctx, arg_dbar * a)
{
    tcg_gen_mb(TCG_BAR_SC | TCG_MO_ALL);
    return true;
}

static bool trans_ibar(DisasContext *ctx, arg_ibar *a)
{
    ctx->base.is_jmp = DISAS_STOP;
    return true;
}

static bool gen_ldptr(DisasContext *ctx, arg_rr_i *a, MemOp mop)
{
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);
    TCGv addr = gpr_src(ctx, a->rj, EXT_NONE);

    addr = make_address_i(ctx, addr, a->imm);

    tcg_gen_qemu_ld_tl(dest, addr, ctx->mem_idx, mop);
    gen_set_gpr(a->rd, dest, EXT_NONE);
    return true;
}

static bool gen_stptr(DisasContext *ctx, arg_rr_i *a, MemOp mop)
{
    TCGv data = gpr_src(ctx, a->rd, EXT_NONE);
    TCGv addr = gpr_src(ctx, a->rj, EXT_NONE);

    addr = make_address_i(ctx, addr, a->imm);

    tcg_gen_qemu_st_tl(data, addr, ctx->mem_idx, mop);
    return true;
}

TRANS(ld_b, ALL, gen_load, MO_SB)
TRANS(ld_h, ALL, gen_load, MO_LESW)
TRANS(ld_w, ALL, gen_load, MO_LESL)
TRANS(ld_d, 64, gen_load, MO_LEUQ)
TRANS(st_b, ALL, gen_store, MO_UB)
TRANS(st_h, ALL, gen_store, MO_LEUW)
TRANS(st_w, ALL, gen_store, MO_LEUL)
TRANS(st_d, 64, gen_store, MO_LEUQ)
TRANS(ld_bu, ALL, gen_load, MO_UB)
TRANS(ld_hu, ALL, gen_load, MO_LEUW)
TRANS(ld_wu, 64, gen_load, MO_LEUL)
TRANS(ldx_b, 64, gen_loadx, MO_SB)
TRANS(ldx_h, 64, gen_loadx, MO_LESW)
TRANS(ldx_w, 64, gen_loadx, MO_LESL)
TRANS(ldx_d, 64, gen_loadx, MO_LEUQ)
TRANS(stx_b, 64, gen_storex, MO_UB)
TRANS(stx_h, 64, gen_storex, MO_LEUW)
TRANS(stx_w, 64, gen_storex, MO_LEUL)
TRANS(stx_d, 64, gen_storex, MO_LEUQ)
TRANS(ldx_bu, 64, gen_loadx, MO_UB)
TRANS(ldx_hu, 64, gen_loadx, MO_LEUW)
TRANS(ldx_wu, 64, gen_loadx, MO_LEUL)
TRANS(ldptr_w, 64, gen_ldptr, MO_LESL)
TRANS(stptr_w, 64, gen_stptr, MO_LEUL)
TRANS(ldptr_d, 64, gen_ldptr, MO_LEUQ)
TRANS(stptr_d, 64, gen_stptr, MO_LEUQ)
TRANS(ldgt_b, 64, gen_load_gt, MO_SB)
TRANS(ldgt_h, 64, gen_load_gt, MO_LESW)
TRANS(ldgt_w, 64, gen_load_gt, MO_LESL)
TRANS(ldgt_d, 64, gen_load_gt, MO_LEUQ)
TRANS(ldle_b, 64, gen_load_le, MO_SB)
TRANS(ldle_h, 64, gen_load_le, MO_LESW)
TRANS(ldle_w, 64, gen_load_le, MO_LESL)
TRANS(ldle_d, 64, gen_load_le, MO_LEUQ)
TRANS(stgt_b, 64, gen_store_gt, MO_UB)
TRANS(stgt_h, 64, gen_store_gt, MO_LEUW)
TRANS(stgt_w, 64, gen_store_gt, MO_LEUL)
TRANS(stgt_d, 64, gen_store_gt, MO_LEUQ)
TRANS(stle_b, 64, gen_store_le, MO_UB)
TRANS(stle_h, 64, gen_store_le, MO_LEUW)
TRANS(stle_w, 64, gen_store_le, MO_LEUL)
TRANS(stle_d, 64, gen_store_le, MO_LEUQ)
