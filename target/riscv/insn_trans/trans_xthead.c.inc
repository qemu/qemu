/*
 * RISC-V translation routines for the T-Head vendor extensions (xthead*).
 *
 * Copyright (c) 2022 VRULL GmbH.
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

#define REQUIRE_XTHEADBA(ctx) do {               \
    if (!ctx->cfg_ptr->ext_xtheadba) {           \
        return false;                            \
    }                                            \
} while (0)

#define REQUIRE_XTHEADCMO(ctx) do {              \
    if (!ctx->cfg_ptr->ext_xtheadcmo) {          \
        return false;                            \
    }                                            \
} while (0)

#define REQUIRE_XTHEADSYNC(ctx) do {             \
    if (!ctx->cfg_ptr->ext_xtheadsync) {         \
        return false;                            \
    }                                            \
} while (0)

/* XTheadBa */

/*
 * th.addsl is similar to sh[123]add (from Zba), but not an
 * alternative encoding: while sh[123] applies the shift to rs1,
 * th.addsl shifts rs2.
 */

#define GEN_TH_ADDSL(SHAMT)                                     \
static void gen_th_addsl##SHAMT(TCGv ret, TCGv arg1, TCGv arg2) \
{                                                               \
    TCGv t = tcg_temp_new();                                    \
    tcg_gen_shli_tl(t, arg2, SHAMT);                            \
    tcg_gen_add_tl(ret, t, arg1);                               \
    tcg_temp_free(t);                                           \
}

GEN_TH_ADDSL(1)
GEN_TH_ADDSL(2)
GEN_TH_ADDSL(3)

#define GEN_TRANS_TH_ADDSL(SHAMT)                                       \
static bool trans_th_addsl##SHAMT(DisasContext *ctx,                    \
                                  arg_th_addsl##SHAMT * a)              \
{                                                                       \
    REQUIRE_XTHEADBA(ctx);                                              \
    return gen_arith(ctx, a, EXT_NONE, gen_th_addsl##SHAMT, NULL);      \
}

GEN_TRANS_TH_ADDSL(1)
GEN_TRANS_TH_ADDSL(2)
GEN_TRANS_TH_ADDSL(3)

/* XTheadCmo */

static inline int priv_level(DisasContext *ctx)
{
#ifdef CONFIG_USER_ONLY
    return PRV_U;
#else
     /* Priv level is part of mem_idx. */
    return ctx->mem_idx & TB_FLAGS_PRIV_MMU_MASK;
#endif
}

/* Test if priv level is M, S, or U (cannot fail). */
#define REQUIRE_PRIV_MSU(ctx)

/* Test if priv level is M or S. */
#define REQUIRE_PRIV_MS(ctx)                                    \
do {                                                            \
    int priv = priv_level(ctx);                                 \
    if (!(priv == PRV_M ||                                      \
          priv == PRV_S)) {                                     \
        return false;                                           \
    }                                                           \
} while (0)

#define NOP_PRIVCHECK(insn, extcheck, privcheck)                \
static bool trans_ ## insn(DisasContext *ctx, arg_ ## insn * a) \
{                                                               \
    (void) a;                                                   \
    extcheck(ctx);                                              \
    privcheck(ctx);                                             \
    return true;                                                \
}

NOP_PRIVCHECK(th_dcache_call, REQUIRE_XTHEADCMO, REQUIRE_PRIV_MS)
NOP_PRIVCHECK(th_dcache_ciall, REQUIRE_XTHEADCMO, REQUIRE_PRIV_MS)
NOP_PRIVCHECK(th_dcache_iall, REQUIRE_XTHEADCMO, REQUIRE_PRIV_MS)
NOP_PRIVCHECK(th_dcache_cpa, REQUIRE_XTHEADCMO, REQUIRE_PRIV_MS)
NOP_PRIVCHECK(th_dcache_cipa, REQUIRE_XTHEADCMO, REQUIRE_PRIV_MS)
NOP_PRIVCHECK(th_dcache_ipa, REQUIRE_XTHEADCMO, REQUIRE_PRIV_MS)
NOP_PRIVCHECK(th_dcache_cva, REQUIRE_XTHEADCMO, REQUIRE_PRIV_MSU)
NOP_PRIVCHECK(th_dcache_civa, REQUIRE_XTHEADCMO, REQUIRE_PRIV_MSU)
NOP_PRIVCHECK(th_dcache_iva, REQUIRE_XTHEADCMO, REQUIRE_PRIV_MSU)
NOP_PRIVCHECK(th_dcache_csw, REQUIRE_XTHEADCMO, REQUIRE_PRIV_MS)
NOP_PRIVCHECK(th_dcache_cisw, REQUIRE_XTHEADCMO, REQUIRE_PRIV_MS)
NOP_PRIVCHECK(th_dcache_isw, REQUIRE_XTHEADCMO, REQUIRE_PRIV_MS)
NOP_PRIVCHECK(th_dcache_cpal1, REQUIRE_XTHEADCMO, REQUIRE_PRIV_MS)
NOP_PRIVCHECK(th_dcache_cval1, REQUIRE_XTHEADCMO, REQUIRE_PRIV_MS)

NOP_PRIVCHECK(th_icache_iall, REQUIRE_XTHEADCMO, REQUIRE_PRIV_MS)
NOP_PRIVCHECK(th_icache_ialls, REQUIRE_XTHEADCMO, REQUIRE_PRIV_MS)
NOP_PRIVCHECK(th_icache_ipa, REQUIRE_XTHEADCMO, REQUIRE_PRIV_MS)
NOP_PRIVCHECK(th_icache_iva, REQUIRE_XTHEADCMO, REQUIRE_PRIV_MSU)

NOP_PRIVCHECK(th_l2cache_call, REQUIRE_XTHEADCMO, REQUIRE_PRIV_MS)
NOP_PRIVCHECK(th_l2cache_ciall, REQUIRE_XTHEADCMO, REQUIRE_PRIV_MS)
NOP_PRIVCHECK(th_l2cache_iall, REQUIRE_XTHEADCMO, REQUIRE_PRIV_MS)

/* XTheadSync */

static bool trans_th_sfence_vmas(DisasContext *ctx, arg_th_sfence_vmas *a)
{
    (void) a;
    REQUIRE_XTHEADSYNC(ctx);

#ifndef CONFIG_USER_ONLY
    REQUIRE_PRIV_MS(ctx);
    gen_helper_tlb_flush_all(cpu_env);
    return true;
#else
    return false;
#endif
}

#ifndef CONFIG_USER_ONLY
static void gen_th_sync_local(DisasContext *ctx)
{
    /*
     * Emulate out-of-order barriers with pipeline flush
     * by exiting the translation block.
     */
    gen_set_pc_imm(ctx, ctx->pc_succ_insn);
    tcg_gen_exit_tb(NULL, 0);
    ctx->base.is_jmp = DISAS_NORETURN;
}
#endif

static bool trans_th_sync(DisasContext *ctx, arg_th_sync *a)
{
    (void) a;
    REQUIRE_XTHEADSYNC(ctx);

#ifndef CONFIG_USER_ONLY
    REQUIRE_PRIV_MSU(ctx);

    /*
     * th.sync is an out-of-order barrier.
     */
    gen_th_sync_local(ctx);

    return true;
#else
    return false;
#endif
}

static bool trans_th_sync_i(DisasContext *ctx, arg_th_sync_i *a)
{
    (void) a;
    REQUIRE_XTHEADSYNC(ctx);

#ifndef CONFIG_USER_ONLY
    REQUIRE_PRIV_MSU(ctx);

    /*
     * th.sync.i is th.sync plus pipeline flush.
     */
    gen_th_sync_local(ctx);

    return true;
#else
    return false;
#endif
}

static bool trans_th_sync_is(DisasContext *ctx, arg_th_sync_is *a)
{
    /* This instruction has the same behaviour like th.sync.i. */
    return trans_th_sync_i(ctx, a);
}

static bool trans_th_sync_s(DisasContext *ctx, arg_th_sync_s *a)
{
    /* This instruction has the same behaviour like th.sync. */
    return trans_th_sync(ctx, a);
}
