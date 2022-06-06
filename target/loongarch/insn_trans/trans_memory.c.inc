/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */

static bool gen_load(DisasContext *ctx, arg_rr_i *a, MemOp mop)
{
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);
    TCGv addr = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv temp = NULL;

    if (a->imm) {
        temp = tcg_temp_new();
        tcg_gen_addi_tl(temp, addr, a->imm);
        addr = temp;
    }

    tcg_gen_qemu_ld_tl(dest, addr, ctx->mem_idx, mop);
    gen_set_gpr(a->rd, dest, EXT_NONE);

    if (temp) {
        tcg_temp_free(temp);
    }

    return true;
}

static bool gen_store(DisasContext *ctx, arg_rr_i *a, MemOp mop)
{
    TCGv data = gpr_src(ctx, a->rd, EXT_NONE);
    TCGv addr = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv temp = NULL;

    if (a->imm) {
        temp = tcg_temp_new();
        tcg_gen_addi_tl(temp, addr, a->imm);
        addr = temp;
    }

    tcg_gen_qemu_st_tl(data, addr, ctx->mem_idx, mop);

    if (temp) {
        tcg_temp_free(temp);
    }

    return true;
}

static bool gen_loadx(DisasContext *ctx, arg_rrr *a, MemOp mop)
{
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);
    TCGv src1 = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv src2 = gpr_src(ctx, a->rk, EXT_NONE);
    TCGv addr = tcg_temp_new();

    tcg_gen_add_tl(addr, src1, src2);
    tcg_gen_qemu_ld_tl(dest, addr, ctx->mem_idx, mop);
    gen_set_gpr(a->rd, dest, EXT_NONE);
    tcg_temp_free(addr);

    return true;
}

static bool gen_storex(DisasContext *ctx, arg_rrr *a, MemOp mop)
{
    TCGv data = gpr_src(ctx, a->rd, EXT_NONE);
    TCGv src1 = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv src2 = gpr_src(ctx, a->rk, EXT_NONE);
    TCGv addr = tcg_temp_new();

    tcg_gen_add_tl(addr, src1, src2);
    tcg_gen_qemu_st_tl(data, addr, ctx->mem_idx, mop);
    tcg_temp_free(addr);

    return true;
}

static bool gen_load_gt(DisasContext *ctx, arg_rrr *a, MemOp mop)
{
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);
    TCGv src1 = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv src2 = gpr_src(ctx, a->rk, EXT_NONE);

    gen_helper_asrtgt_d(cpu_env, src1, src2);
    tcg_gen_qemu_ld_tl(dest, src1, ctx->mem_idx, mop);
    gen_set_gpr(a->rd, dest, EXT_NONE);

    return true;
}

static bool gen_load_le(DisasContext *ctx, arg_rrr *a, MemOp mop)
{
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);
    TCGv src1 = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv src2 = gpr_src(ctx, a->rk, EXT_NONE);

    gen_helper_asrtle_d(cpu_env, src1, src2);
    tcg_gen_qemu_ld_tl(dest, src1, ctx->mem_idx, mop);
    gen_set_gpr(a->rd, dest, EXT_NONE);

    return true;
}

static bool gen_store_gt(DisasContext *ctx, arg_rrr *a, MemOp mop)
{
    TCGv data = gpr_src(ctx, a->rd, EXT_NONE);
    TCGv src1 = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv src2 = gpr_src(ctx, a->rk, EXT_NONE);

    gen_helper_asrtgt_d(cpu_env, src1, src2);
    tcg_gen_qemu_st_tl(data, src1, ctx->mem_idx, mop);

    return true;
}

static bool gen_store_le(DisasContext *ctx, arg_rrr *a, MemOp mop)
{
    TCGv data = gpr_src(ctx, a->rd, EXT_NONE);
    TCGv src1 = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv src2 = gpr_src(ctx, a->rk, EXT_NONE);

    gen_helper_asrtle_d(cpu_env, src1, src2);
    tcg_gen_qemu_st_tl(data, src1, ctx->mem_idx, mop);

    return true;
}

static bool trans_preld(DisasContext *ctx, arg_preld *a)
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
    TCGv temp = NULL;

    if (a->imm) {
        temp = tcg_temp_new();
        tcg_gen_addi_tl(temp, addr, a->imm);
        addr = temp;
    }

    tcg_gen_qemu_ld_tl(dest, addr, ctx->mem_idx, mop);
    gen_set_gpr(a->rd, dest, EXT_NONE);

    if (temp) {
        tcg_temp_free(temp);
    }

    return true;
}

static bool gen_stptr(DisasContext *ctx, arg_rr_i *a, MemOp mop)
{
    TCGv data = gpr_src(ctx, a->rd, EXT_NONE);
    TCGv addr = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv temp = NULL;

    if (a->imm) {
        temp = tcg_temp_new();
        tcg_gen_addi_tl(temp, addr, a->imm);
        addr = temp;
    }

    tcg_gen_qemu_st_tl(data, addr, ctx->mem_idx, mop);

    if (temp) {
        tcg_temp_free(temp);
    }

    return true;
}

TRANS(ld_b, gen_load, MO_SB)
TRANS(ld_h, gen_load, MO_TESW)
TRANS(ld_w, gen_load, MO_TESL)
TRANS(ld_d, gen_load, MO_TEUQ)
TRANS(st_b, gen_store, MO_UB)
TRANS(st_h, gen_store, MO_TEUW)
TRANS(st_w, gen_store, MO_TEUL)
TRANS(st_d, gen_store, MO_TEUQ)
TRANS(ld_bu, gen_load, MO_UB)
TRANS(ld_hu, gen_load, MO_TEUW)
TRANS(ld_wu, gen_load, MO_TEUL)
TRANS(ldx_b, gen_loadx, MO_SB)
TRANS(ldx_h, gen_loadx, MO_TESW)
TRANS(ldx_w, gen_loadx, MO_TESL)
TRANS(ldx_d, gen_loadx, MO_TEUQ)
TRANS(stx_b, gen_storex, MO_UB)
TRANS(stx_h, gen_storex, MO_TEUW)
TRANS(stx_w, gen_storex, MO_TEUL)
TRANS(stx_d, gen_storex, MO_TEUQ)
TRANS(ldx_bu, gen_loadx, MO_UB)
TRANS(ldx_hu, gen_loadx, MO_TEUW)
TRANS(ldx_wu, gen_loadx, MO_TEUL)
TRANS(ldptr_w, gen_ldptr, MO_TESL)
TRANS(stptr_w, gen_stptr, MO_TEUL)
TRANS(ldptr_d, gen_ldptr, MO_TEUQ)
TRANS(stptr_d, gen_stptr, MO_TEUQ)
TRANS(ldgt_b, gen_load_gt, MO_SB)
TRANS(ldgt_h, gen_load_gt, MO_TESW)
TRANS(ldgt_w, gen_load_gt, MO_TESL)
TRANS(ldgt_d, gen_load_gt, MO_TEUQ)
TRANS(ldle_b, gen_load_le, MO_SB)
TRANS(ldle_h, gen_load_le, MO_TESW)
TRANS(ldle_w, gen_load_le, MO_TESL)
TRANS(ldle_d, gen_load_le, MO_TEUQ)
TRANS(stgt_b, gen_store_gt, MO_UB)
TRANS(stgt_h, gen_store_gt, MO_TEUW)
TRANS(stgt_w, gen_store_gt, MO_TEUL)
TRANS(stgt_d, gen_store_gt, MO_TEUQ)
TRANS(stle_b, gen_store_le, MO_UB)
TRANS(stle_h, gen_store_le, MO_TEUW)
TRANS(stle_w, gen_store_le, MO_TEUL)
TRANS(stle_d, gen_store_le, MO_TEUQ)
