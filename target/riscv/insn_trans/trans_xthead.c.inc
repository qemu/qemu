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

#define REQUIRE_XTHEADBB(ctx) do {               \
    if (!ctx->cfg_ptr->ext_xtheadbb) {           \
        return false;                            \
    }                                            \
} while (0)

#define REQUIRE_XTHEADBS(ctx) do {               \
    if (!ctx->cfg_ptr->ext_xtheadbs) {           \
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

/* XTheadBb */

/* th.srri is an alternate encoding for rori (from Zbb) */
static bool trans_th_srri(DisasContext *ctx, arg_th_srri * a)
{
    REQUIRE_XTHEADBB(ctx);
    return gen_shift_imm_fn_per_ol(ctx, a, EXT_NONE,
                                   tcg_gen_rotri_tl, gen_roriw, NULL);
}

/* th.srriw is an alternate encoding for roriw (from Zbb) */
static bool trans_th_srriw(DisasContext *ctx, arg_th_srriw *a)
{
    REQUIRE_XTHEADBB(ctx);
    REQUIRE_64BIT(ctx);
    ctx->ol = MXL_RV32;
    return gen_shift_imm_fn(ctx, a, EXT_NONE, gen_roriw, NULL);
}

/* th.ext and th.extu perform signed/unsigned bitfield extraction */
static bool gen_th_bfextract(DisasContext *ctx, arg_th_bfext *a,
                             void (*f)(TCGv, TCGv, unsigned int, unsigned int))
{
    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv source = get_gpr(ctx, a->rs1, EXT_ZERO);

    if (a->lsb <= a->msb) {
        f(dest, source, a->lsb, a->msb - a->lsb + 1);
        gen_set_gpr(ctx, a->rd, dest);
    }
    return true;
}

static bool trans_th_ext(DisasContext *ctx, arg_th_ext *a)
{
    REQUIRE_XTHEADBB(ctx);
    return gen_th_bfextract(ctx, a, tcg_gen_sextract_tl);
}

static bool trans_th_extu(DisasContext *ctx, arg_th_extu *a)
{
    REQUIRE_XTHEADBB(ctx);
    return gen_th_bfextract(ctx, a, tcg_gen_extract_tl);
}

/* th.ff0: find first zero (clz on an inverted input) */
static bool gen_th_ff0(DisasContext *ctx, arg_th_ff0 *a, DisasExtend ext)
{
    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, ext);

    int olen = get_olen(ctx);
    TCGv t = tcg_temp_new();

    tcg_gen_not_tl(t, src1);
    if (olen != TARGET_LONG_BITS) {
        if (olen == 32) {
            gen_clzw(dest, t);
        } else {
            g_assert_not_reached();
        }
    } else {
        gen_clz(dest, t);
    }

    tcg_temp_free(t);
    gen_set_gpr(ctx, a->rd, dest);

    return true;
}

static bool trans_th_ff0(DisasContext *ctx, arg_th_ff0 *a)
{
    REQUIRE_XTHEADBB(ctx);
    return gen_th_ff0(ctx, a, EXT_NONE);
}

/* th.ff1 is an alternate encoding for clz (from Zbb) */
static bool trans_th_ff1(DisasContext *ctx, arg_th_ff1 *a)
{
    REQUIRE_XTHEADBB(ctx);
    return gen_unary_per_ol(ctx, a, EXT_NONE, gen_clz, gen_clzw);
}

static void gen_th_revw(TCGv ret, TCGv arg1)
{
    tcg_gen_bswap32_tl(ret, arg1, TCG_BSWAP_OS);
}

/* th.rev is an alternate encoding for the RV64 rev8 (from Zbb) */
static bool trans_th_rev(DisasContext *ctx, arg_th_rev *a)
{
    REQUIRE_XTHEADBB(ctx);

    return gen_unary_per_ol(ctx, a, EXT_NONE, tcg_gen_bswap_tl, gen_th_revw);
}

/* th.revw is a sign-extended byte-swap of the lower word */
static bool trans_th_revw(DisasContext *ctx, arg_th_revw *a)
{
    REQUIRE_XTHEADBB(ctx);
    REQUIRE_64BIT(ctx);
    return gen_unary(ctx, a, EXT_NONE, gen_th_revw);
}

/* th.tstnbz is equivalent to an orc.b (from Zbb) with inverted result */
static void gen_th_tstnbz(TCGv ret, TCGv source1)
{
    gen_orc_b(ret, source1);
    tcg_gen_not_tl(ret, ret);
}

static bool trans_th_tstnbz(DisasContext *ctx, arg_th_tstnbz *a)
{
    REQUIRE_XTHEADBB(ctx);
    return gen_unary(ctx, a, EXT_ZERO, gen_th_tstnbz);
}

/* XTheadBs */

/* th.tst is an alternate encoding for bexti (from Zbs) */
static bool trans_th_tst(DisasContext *ctx, arg_th_tst *a)
{
    REQUIRE_XTHEADBS(ctx);
    return gen_shift_imm_tl(ctx, a, EXT_NONE, gen_bext);
}

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
