/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */

static bool trans_b(DisasContext *ctx, arg_b *a)
{
    gen_goto_tb(ctx, 0, ctx->base.pc_next + a->offs);
    ctx->base.is_jmp = DISAS_NORETURN;
    return true;
}

static bool trans_bl(DisasContext *ctx, arg_bl *a)
{
    tcg_gen_movi_tl(cpu_gpr[1], make_address_pc(ctx, ctx->base.pc_next + 4));
    gen_goto_tb(ctx, 0, ctx->base.pc_next + a->offs);
    ctx->base.is_jmp = DISAS_NORETURN;
    return true;
}

static bool trans_jirl(DisasContext *ctx, arg_jirl *a)
{
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);
    TCGv src1 = gpr_src(ctx, a->rj, EXT_NONE);

    TCGv addr = make_address_i(ctx, src1, a->imm);
    tcg_gen_mov_tl(cpu_pc, addr);
    tcg_gen_movi_tl(dest, make_address_pc(ctx, ctx->base.pc_next + 4));
    gen_set_gpr(a->rd, dest, EXT_NONE);
    tcg_gen_lookup_and_goto_ptr();
    ctx->base.is_jmp = DISAS_NORETURN;
    return true;
}

static void gen_bc(DisasContext *ctx, TCGv src1, TCGv src2,
                   target_long offs, TCGCond cond)
{
    TCGLabel *l = gen_new_label();
    tcg_gen_brcond_tl(cond, src1, src2, l);
    gen_goto_tb(ctx, 1, ctx->base.pc_next + 4);
    gen_set_label(l);
    gen_goto_tb(ctx, 0, ctx->base.pc_next + offs);
    ctx->base.is_jmp = DISAS_NORETURN;
}

static bool gen_rr_bc(DisasContext *ctx, arg_rr_offs *a, TCGCond cond)
{
    TCGv src1 = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv src2 = gpr_src(ctx, a->rd, EXT_NONE);

    gen_bc(ctx, src1, src2, a->offs, cond);
    return true;
}

static bool gen_rz_bc(DisasContext *ctx, arg_r_offs *a, TCGCond cond)
{
    TCGv src1 = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv src2 = tcg_constant_tl(0);

    gen_bc(ctx, src1, src2, a->offs, cond);
    return true;
}

static bool gen_cz_bc(DisasContext *ctx, arg_c_offs *a, TCGCond cond)
{
    TCGv src1 = tcg_temp_new();
    TCGv src2 = tcg_constant_tl(0);

    tcg_gen_ld8u_tl(src1, tcg_env,
                    offsetof(CPULoongArchState, cf[a->cj]));
    gen_bc(ctx, src1, src2, a->offs, cond);
    return true;
}

TRANS(beq, ALL, gen_rr_bc, TCG_COND_EQ)
TRANS(bne, ALL, gen_rr_bc, TCG_COND_NE)
TRANS(blt, ALL, gen_rr_bc, TCG_COND_LT)
TRANS(bge, ALL, gen_rr_bc, TCG_COND_GE)
TRANS(bltu, ALL, gen_rr_bc, TCG_COND_LTU)
TRANS(bgeu, ALL, gen_rr_bc, TCG_COND_GEU)
TRANS(beqz, ALL, gen_rz_bc, TCG_COND_EQ)
TRANS(bnez, ALL, gen_rz_bc, TCG_COND_NE)
TRANS(bceqz, 64, gen_cz_bc, TCG_COND_EQ)
TRANS(bcnez, 64, gen_cz_bc, TCG_COND_NE)
