/*
 * RISC-V translation routines for the RVXI Base Integer Instruction Set.
 *
 * Copyright (c) 2020 Western Digital
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

static bool trans_hlv_b(DisasContext *ctx, arg_hlv_b *a)
{
    REQUIRE_EXT(ctx, RVH);
#ifndef CONFIG_USER_ONLY
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGv mem_idx = tcg_temp_new();
    TCGv memop = tcg_temp_new();

    gen_get_gpr(t0, a->rs1);
    tcg_gen_movi_tl(mem_idx, ctx->mem_idx);
    tcg_gen_movi_tl(memop, MO_SB);

    gen_helper_hyp_load(t1, cpu_env, t0, mem_idx, memop);
    gen_set_gpr(a->rd, t1);

    tcg_temp_free(t0);
    tcg_temp_free(t1);
    tcg_temp_free(mem_idx);
    tcg_temp_free(memop);
    return true;
#else
    return false;
#endif
}

static bool trans_hlv_h(DisasContext *ctx, arg_hlv_h *a)
{
    REQUIRE_EXT(ctx, RVH);
#ifndef CONFIG_USER_ONLY
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGv mem_idx = tcg_temp_new();
    TCGv memop = tcg_temp_new();

    gen_get_gpr(t0, a->rs1);
    tcg_gen_movi_tl(mem_idx, ctx->mem_idx);
    tcg_gen_movi_tl(memop, MO_TESW);

    gen_helper_hyp_load(t1, cpu_env, t0, mem_idx, memop);
    gen_set_gpr(a->rd, t1);

    tcg_temp_free(t0);
    tcg_temp_free(t1);
    tcg_temp_free(mem_idx);
    tcg_temp_free(memop);
    return true;
#else
    return false;
#endif
}

static bool trans_hlv_w(DisasContext *ctx, arg_hlv_w *a)
{
    REQUIRE_EXT(ctx, RVH);
#ifndef CONFIG_USER_ONLY
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGv mem_idx = tcg_temp_new();
    TCGv memop = tcg_temp_new();

    gen_get_gpr(t0, a->rs1);
    tcg_gen_movi_tl(mem_idx, ctx->mem_idx);
    tcg_gen_movi_tl(memop, MO_TESL);

    gen_helper_hyp_load(t1, cpu_env, t0, mem_idx, memop);
    gen_set_gpr(a->rd, t1);

    tcg_temp_free(t0);
    tcg_temp_free(t1);
    tcg_temp_free(mem_idx);
    tcg_temp_free(memop);
    return true;
#else
    return false;
#endif
}

static bool trans_hlv_bu(DisasContext *ctx, arg_hlv_bu *a)
{
    REQUIRE_EXT(ctx, RVH);
#ifndef CONFIG_USER_ONLY
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGv mem_idx = tcg_temp_new();
    TCGv memop = tcg_temp_new();

    gen_get_gpr(t0, a->rs1);
    tcg_gen_movi_tl(mem_idx, ctx->mem_idx);
    tcg_gen_movi_tl(memop, MO_UB);

    gen_helper_hyp_load(t1, cpu_env, t0, mem_idx, memop);
    gen_set_gpr(a->rd, t1);

    tcg_temp_free(t0);
    tcg_temp_free(t1);
    tcg_temp_free(mem_idx);
    tcg_temp_free(memop);
    return true;
#else
    return false;
#endif
}

static bool trans_hlv_hu(DisasContext *ctx, arg_hlv_hu *a)
{
    REQUIRE_EXT(ctx, RVH);
#ifndef CONFIG_USER_ONLY
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGv mem_idx = tcg_temp_new();
    TCGv memop = tcg_temp_new();

    gen_get_gpr(t0, a->rs1);
    tcg_gen_movi_tl(mem_idx, ctx->mem_idx);
    tcg_gen_movi_tl(memop, MO_TEUW);

    gen_helper_hyp_load(t1, cpu_env, t0, mem_idx, memop);
    gen_set_gpr(a->rd, t1);

    tcg_temp_free(t0);
    tcg_temp_free(t1);
    tcg_temp_free(mem_idx);
    tcg_temp_free(memop);
    return true;
#else
    return false;
#endif
}

static bool trans_hsv_b(DisasContext *ctx, arg_hsv_b *a)
{
    REQUIRE_EXT(ctx, RVH);
#ifndef CONFIG_USER_ONLY
    TCGv t0 = tcg_temp_new();
    TCGv dat = tcg_temp_new();
    TCGv mem_idx = tcg_temp_new();
    TCGv memop = tcg_temp_new();

    gen_get_gpr(t0, a->rs1);
    gen_get_gpr(dat, a->rs2);
    tcg_gen_movi_tl(mem_idx, ctx->mem_idx);
    tcg_gen_movi_tl(memop, MO_SB);

    gen_helper_hyp_store(cpu_env, t0, dat, mem_idx, memop);

    tcg_temp_free(t0);
    tcg_temp_free(dat);
    tcg_temp_free(mem_idx);
    tcg_temp_free(memop);
    return true;
#else
    return false;
#endif
}

static bool trans_hsv_h(DisasContext *ctx, arg_hsv_h *a)
{
    REQUIRE_EXT(ctx, RVH);
#ifndef CONFIG_USER_ONLY
    TCGv t0 = tcg_temp_new();
    TCGv dat = tcg_temp_new();
    TCGv mem_idx = tcg_temp_new();
    TCGv memop = tcg_temp_new();

    gen_get_gpr(t0, a->rs1);
    gen_get_gpr(dat, a->rs2);
    tcg_gen_movi_tl(mem_idx, ctx->mem_idx);
    tcg_gen_movi_tl(memop, MO_TESW);

    gen_helper_hyp_store(cpu_env, t0, dat, mem_idx, memop);

    tcg_temp_free(t0);
    tcg_temp_free(dat);
    tcg_temp_free(mem_idx);
    tcg_temp_free(memop);
    return true;
#else
    return false;
#endif
}

static bool trans_hsv_w(DisasContext *ctx, arg_hsv_w *a)
{
    REQUIRE_EXT(ctx, RVH);
#ifndef CONFIG_USER_ONLY
    TCGv t0 = tcg_temp_new();
    TCGv dat = tcg_temp_new();
    TCGv mem_idx = tcg_temp_new();
    TCGv memop = tcg_temp_new();

    gen_get_gpr(t0, a->rs1);
    gen_get_gpr(dat, a->rs2);
    tcg_gen_movi_tl(mem_idx, ctx->mem_idx);
    tcg_gen_movi_tl(memop, MO_TESL);

    gen_helper_hyp_store(cpu_env, t0, dat, mem_idx, memop);

    tcg_temp_free(t0);
    tcg_temp_free(dat);
    tcg_temp_free(mem_idx);
    tcg_temp_free(memop);
    return true;
#else
    return false;
#endif
}

#ifdef TARGET_RISCV64
static bool trans_hlv_wu(DisasContext *ctx, arg_hlv_wu *a)
{
    REQUIRE_EXT(ctx, RVH);
#ifndef CONFIG_USER_ONLY
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGv mem_idx = tcg_temp_new();
    TCGv memop = tcg_temp_new();

    gen_get_gpr(t0, a->rs1);
    tcg_gen_movi_tl(mem_idx, ctx->mem_idx);
    tcg_gen_movi_tl(memop, MO_TEUL);

    gen_helper_hyp_load(t1, cpu_env, t0, mem_idx, memop);
    gen_set_gpr(a->rd, t1);

    tcg_temp_free(t0);
    tcg_temp_free(t1);
    tcg_temp_free(mem_idx);
    tcg_temp_free(memop);
    return true;
#else
    return false;
#endif
}

static bool trans_hlv_d(DisasContext *ctx, arg_hlv_d *a)
{
    REQUIRE_EXT(ctx, RVH);
#ifndef CONFIG_USER_ONLY
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGv mem_idx = tcg_temp_new();
    TCGv memop = tcg_temp_new();

    gen_get_gpr(t0, a->rs1);
    tcg_gen_movi_tl(mem_idx, ctx->mem_idx);
    tcg_gen_movi_tl(memop, MO_TEQ);

    gen_helper_hyp_load(t1, cpu_env, t0, mem_idx, memop);
    gen_set_gpr(a->rd, t1);

    tcg_temp_free(t0);
    tcg_temp_free(t1);
    tcg_temp_free(mem_idx);
    tcg_temp_free(memop);
    return true;
#else
    return false;
#endif
}

static bool trans_hsv_d(DisasContext *ctx, arg_hsv_d *a)
{
    REQUIRE_EXT(ctx, RVH);
#ifndef CONFIG_USER_ONLY
    TCGv t0 = tcg_temp_new();
    TCGv dat = tcg_temp_new();
    TCGv mem_idx = tcg_temp_new();
    TCGv memop = tcg_temp_new();

    gen_get_gpr(t0, a->rs1);
    gen_get_gpr(dat, a->rs2);
    tcg_gen_movi_tl(mem_idx, ctx->mem_idx);
    tcg_gen_movi_tl(memop, MO_TEQ);

    gen_helper_hyp_store(cpu_env, t0, dat, mem_idx, memop);

    tcg_temp_free(t0);
    tcg_temp_free(dat);
    tcg_temp_free(mem_idx);
    tcg_temp_free(memop);
    return true;
#else
    return false;
#endif
}
#endif

static bool trans_hlvx_hu(DisasContext *ctx, arg_hlvx_hu *a)
{
    REQUIRE_EXT(ctx, RVH);
#ifndef CONFIG_USER_ONLY
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGv mem_idx = tcg_temp_new();
    TCGv memop = tcg_temp_new();

    gen_get_gpr(t0, a->rs1);
    tcg_gen_movi_tl(mem_idx, ctx->mem_idx);
    tcg_gen_movi_tl(memop, MO_TEUW);

    gen_helper_hyp_x_load(t1, cpu_env, t0, mem_idx, memop);
    gen_set_gpr(a->rd, t1);

    tcg_temp_free(t0);
    tcg_temp_free(t1);
    tcg_temp_free(mem_idx);
    tcg_temp_free(memop);
    return true;
#else
    return false;
#endif
}

static bool trans_hlvx_wu(DisasContext *ctx, arg_hlvx_wu *a)
{
    REQUIRE_EXT(ctx, RVH);
#ifndef CONFIG_USER_ONLY
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGv mem_idx = tcg_temp_new();
    TCGv memop = tcg_temp_new();

    gen_get_gpr(t0, a->rs1);
    tcg_gen_movi_tl(mem_idx, ctx->mem_idx);
    tcg_gen_movi_tl(memop, MO_TEUL);

    gen_helper_hyp_x_load(t1, cpu_env, t0, mem_idx, memop);
    gen_set_gpr(a->rd, t1);

    tcg_temp_free(t0);
    tcg_temp_free(t1);
    tcg_temp_free(mem_idx);
    tcg_temp_free(memop);
    return true;
#else
    return false;
#endif
}

static bool trans_hfence_gvma(DisasContext *ctx, arg_sfence_vma *a)
{
    REQUIRE_EXT(ctx, RVH);
#ifndef CONFIG_USER_ONLY
    gen_helper_hyp_gvma_tlb_flush(cpu_env);
    return true;
#endif
    return false;
}

static bool trans_hfence_vvma(DisasContext *ctx, arg_sfence_vma *a)
{
    REQUIRE_EXT(ctx, RVH);
#ifndef CONFIG_USER_ONLY
    gen_helper_hyp_tlb_flush(cpu_env);
    return true;
#endif
    return false;
}
