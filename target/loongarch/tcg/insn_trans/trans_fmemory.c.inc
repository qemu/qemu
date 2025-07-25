/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */

static void maybe_nanbox_load(TCGv freg, MemOp mop)
{
    if ((mop & MO_SIZE) == MO_32) {
        gen_nanbox_s(freg, freg);
    }
}

static bool gen_fload_i(DisasContext *ctx, arg_fr_i *a, MemOp mop)
{
    TCGv addr = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv dest = get_fpr(ctx, a->fd);

    CHECK_FPE;

    addr = make_address_i(ctx, addr, a->imm);

    tcg_gen_qemu_ld_tl(dest, addr, ctx->mem_idx, mop);
    maybe_nanbox_load(dest, mop);
    set_fpr(a->fd, dest);

    return true;
}

static bool gen_fstore_i(DisasContext *ctx, arg_fr_i *a, MemOp mop)
{
    TCGv addr = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv src = get_fpr(ctx, a->fd);

    CHECK_FPE;

    addr = make_address_i(ctx, addr, a->imm);

    tcg_gen_qemu_st_tl(src, addr, ctx->mem_idx, mop);

    return true;
}

static bool gen_floadx(DisasContext *ctx, arg_frr *a, MemOp mop)
{
    TCGv src1 = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv src2 = gpr_src(ctx, a->rk, EXT_NONE);
    TCGv dest = get_fpr(ctx, a->fd);
    TCGv addr;

    CHECK_FPE;

    addr = make_address_x(ctx, src1, src2);
    tcg_gen_qemu_ld_tl(dest, addr, ctx->mem_idx, mop);
    maybe_nanbox_load(dest, mop);
    set_fpr(a->fd, dest);

    return true;
}

static bool gen_fstorex(DisasContext *ctx, arg_frr *a, MemOp mop)
{
    TCGv src1 = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv src2 = gpr_src(ctx, a->rk, EXT_NONE);
    TCGv src3 = get_fpr(ctx, a->fd);
    TCGv addr;

    CHECK_FPE;

    addr = make_address_x(ctx, src1, src2);
    tcg_gen_qemu_st_tl(src3, addr, ctx->mem_idx, mop);

    return true;
}

static bool gen_fload_gt(DisasContext *ctx, arg_frr *a, MemOp mop)
{
    TCGv src1 = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv src2 = gpr_src(ctx, a->rk, EXT_NONE);
    TCGv dest = get_fpr(ctx, a->fd);
    TCGv addr;

    CHECK_FPE;

    gen_helper_asrtgt_d(tcg_env, src1, src2);
    addr = make_address_x(ctx, src1, src2);
    tcg_gen_qemu_ld_tl(dest, addr, ctx->mem_idx, mop);
    maybe_nanbox_load(dest, mop);
    set_fpr(a->fd, dest);

    return true;
}

static bool gen_fstore_gt(DisasContext *ctx, arg_frr *a, MemOp mop)
{
    TCGv src1 = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv src2 = gpr_src(ctx, a->rk, EXT_NONE);
    TCGv src3 = get_fpr(ctx, a->fd);
    TCGv addr;

    CHECK_FPE;

    gen_helper_asrtgt_d(tcg_env, src1, src2);
    addr = make_address_x(ctx, src1, src2);
    tcg_gen_qemu_st_tl(src3, addr, ctx->mem_idx, mop);

    return true;
}

static bool gen_fload_le(DisasContext *ctx, arg_frr *a, MemOp mop)
{
    TCGv src1 = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv src2 = gpr_src(ctx, a->rk, EXT_NONE);
    TCGv dest = get_fpr(ctx, a->fd);
    TCGv addr;

    CHECK_FPE;

    gen_helper_asrtle_d(tcg_env, src1, src2);
    addr = make_address_x(ctx, src1, src2);
    tcg_gen_qemu_ld_tl(dest, addr, ctx->mem_idx, mop);
    maybe_nanbox_load(dest, mop);
    set_fpr(a->fd, dest);

    return true;
}

static bool gen_fstore_le(DisasContext *ctx, arg_frr *a, MemOp mop)
{
    TCGv src1 = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv src2 = gpr_src(ctx, a->rk, EXT_NONE);
    TCGv src3 = get_fpr(ctx, a->fd);
    TCGv addr;

    CHECK_FPE;

    gen_helper_asrtle_d(tcg_env, src1, src2);
    addr = make_address_x(ctx, src1, src2);
    tcg_gen_qemu_st_tl(src3, addr, ctx->mem_idx, mop);

    return true;
}

TRANS(fld_s, FP_SP, gen_fload_i, MO_TEUL)
TRANS(fst_s, FP_SP, gen_fstore_i, MO_TEUL)
TRANS(fld_d, FP_DP, gen_fload_i, MO_TEUQ)
TRANS(fst_d, FP_DP, gen_fstore_i, MO_TEUQ)
TRANS(fldx_s, FP_SP, gen_floadx, MO_TEUL)
TRANS(fldx_d, FP_DP, gen_floadx, MO_TEUQ)
TRANS(fstx_s, FP_SP, gen_fstorex, MO_TEUL)
TRANS(fstx_d, FP_DP, gen_fstorex, MO_TEUQ)
TRANS64(fldgt_s, FP_SP, gen_fload_gt, MO_TEUL)
TRANS64(fldgt_d, FP_DP, gen_fload_gt, MO_TEUQ)
TRANS64(fldle_s, FP_SP, gen_fload_le, MO_TEUL)
TRANS64(fldle_d, FP_DP, gen_fload_le, MO_TEUQ)
TRANS64(fstgt_s, FP_SP, gen_fstore_gt, MO_TEUL)
TRANS64(fstgt_d, FP_DP, gen_fstore_gt, MO_TEUQ)
TRANS64(fstle_s, FP_SP, gen_fstore_le, MO_TEUL)
TRANS64(fstle_d, FP_DP, gen_fstore_le, MO_TEUQ)
