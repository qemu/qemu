/*
 * RISC-V translation routines for the Zb[abcs] Standard Extension.
 *
 * Copyright (c) 2020 Kito Cheng, kito.cheng@sifive.com
 * Copyright (c) 2020 Frank Chang, frank.chang@sifive.com
 * Copyright (c) 2021 Philipp Tomsich, philipp.tomsich@vrull.eu
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

#define REQUIRE_ZBA(ctx) do {                    \
    if (!ctx->cfg_ptr->ext_zba) {                \
        return false;                            \
    }                                            \
} while (0)

#define REQUIRE_ZBB(ctx) do {                    \
    if (!ctx->cfg_ptr->ext_zbb) {                \
        return false;                            \
    }                                            \
} while (0)

#define REQUIRE_ZBC(ctx) do {                    \
    if (!ctx->cfg_ptr->ext_zbc) {                \
        return false;                            \
    }                                            \
} while (0)

#define REQUIRE_ZBS(ctx) do {                    \
    if (!ctx->cfg_ptr->ext_zbs) {                \
        return false;                            \
    }                                            \
} while (0)

static void gen_clz(TCGv ret, TCGv arg1)
{
    tcg_gen_clzi_tl(ret, arg1, TARGET_LONG_BITS);
}

static void gen_clzw(TCGv ret, TCGv arg1)
{
    TCGv t = tcg_temp_new();
    tcg_gen_shli_tl(t, arg1, 32);
    tcg_gen_clzi_tl(ret, t, 32);
    tcg_temp_free(t);
}

static bool trans_clz(DisasContext *ctx, arg_clz *a)
{
    REQUIRE_ZBB(ctx);
    return gen_unary_per_ol(ctx, a, EXT_NONE, gen_clz, gen_clzw);
}

static void gen_ctz(TCGv ret, TCGv arg1)
{
    tcg_gen_ctzi_tl(ret, arg1, TARGET_LONG_BITS);
}

static void gen_ctzw(TCGv ret, TCGv arg1)
{
    tcg_gen_ctzi_tl(ret, arg1, 32);
}

static bool trans_ctz(DisasContext *ctx, arg_ctz *a)
{
    REQUIRE_ZBB(ctx);
    return gen_unary_per_ol(ctx, a, EXT_ZERO, gen_ctz, gen_ctzw);
}

static bool trans_cpop(DisasContext *ctx, arg_cpop *a)
{
    REQUIRE_ZBB(ctx);
    return gen_unary(ctx, a, EXT_ZERO, tcg_gen_ctpop_tl);
}

static bool trans_andn(DisasContext *ctx, arg_andn *a)
{
    REQUIRE_ZBB(ctx);
    return gen_logic(ctx, a, tcg_gen_andc_tl);
}

static bool trans_orn(DisasContext *ctx, arg_orn *a)
{
    REQUIRE_ZBB(ctx);
    return gen_logic(ctx, a, tcg_gen_orc_tl);
}

static bool trans_xnor(DisasContext *ctx, arg_xnor *a)
{
    REQUIRE_ZBB(ctx);
    return gen_logic(ctx, a, tcg_gen_eqv_tl);
}

static bool trans_min(DisasContext *ctx, arg_min *a)
{
    REQUIRE_ZBB(ctx);
    return gen_arith(ctx, a, EXT_SIGN, tcg_gen_smin_tl, NULL);
}

static bool trans_max(DisasContext *ctx, arg_max *a)
{
    REQUIRE_ZBB(ctx);
    return gen_arith(ctx, a, EXT_SIGN, tcg_gen_smax_tl, NULL);
}

static bool trans_minu(DisasContext *ctx, arg_minu *a)
{
    REQUIRE_ZBB(ctx);
    return gen_arith(ctx, a, EXT_SIGN, tcg_gen_umin_tl, NULL);
}

static bool trans_maxu(DisasContext *ctx, arg_maxu *a)
{
    REQUIRE_ZBB(ctx);
    return gen_arith(ctx, a, EXT_SIGN, tcg_gen_umax_tl, NULL);
}

static bool trans_sext_b(DisasContext *ctx, arg_sext_b *a)
{
    REQUIRE_ZBB(ctx);
    return gen_unary(ctx, a, EXT_NONE, tcg_gen_ext8s_tl);
}

static bool trans_sext_h(DisasContext *ctx, arg_sext_h *a)
{
    REQUIRE_ZBB(ctx);
    return gen_unary(ctx, a, EXT_NONE, tcg_gen_ext16s_tl);
}

static void gen_sbop_mask(TCGv ret, TCGv shamt)
{
    tcg_gen_movi_tl(ret, 1);
    tcg_gen_shl_tl(ret, ret, shamt);
}

static void gen_bset(TCGv ret, TCGv arg1, TCGv shamt)
{
    TCGv t = tcg_temp_new();

    gen_sbop_mask(t, shamt);
    tcg_gen_or_tl(ret, arg1, t);

    tcg_temp_free(t);
}

static bool trans_bset(DisasContext *ctx, arg_bset *a)
{
    REQUIRE_ZBS(ctx);
    return gen_shift(ctx, a, EXT_NONE, gen_bset, NULL);
}

static bool trans_bseti(DisasContext *ctx, arg_bseti *a)
{
    REQUIRE_ZBS(ctx);
    return gen_shift_imm_tl(ctx, a, EXT_NONE, gen_bset);
}

static void gen_bclr(TCGv ret, TCGv arg1, TCGv shamt)
{
    TCGv t = tcg_temp_new();

    gen_sbop_mask(t, shamt);
    tcg_gen_andc_tl(ret, arg1, t);

    tcg_temp_free(t);
}

static bool trans_bclr(DisasContext *ctx, arg_bclr *a)
{
    REQUIRE_ZBS(ctx);
    return gen_shift(ctx, a, EXT_NONE, gen_bclr, NULL);
}

static bool trans_bclri(DisasContext *ctx, arg_bclri *a)
{
    REQUIRE_ZBS(ctx);
    return gen_shift_imm_tl(ctx, a, EXT_NONE, gen_bclr);
}

static void gen_binv(TCGv ret, TCGv arg1, TCGv shamt)
{
    TCGv t = tcg_temp_new();

    gen_sbop_mask(t, shamt);
    tcg_gen_xor_tl(ret, arg1, t);

    tcg_temp_free(t);
}

static bool trans_binv(DisasContext *ctx, arg_binv *a)
{
    REQUIRE_ZBS(ctx);
    return gen_shift(ctx, a, EXT_NONE, gen_binv, NULL);
}

static bool trans_binvi(DisasContext *ctx, arg_binvi *a)
{
    REQUIRE_ZBS(ctx);
    return gen_shift_imm_tl(ctx, a, EXT_NONE, gen_binv);
}

static void gen_bext(TCGv ret, TCGv arg1, TCGv shamt)
{
    tcg_gen_shr_tl(ret, arg1, shamt);
    tcg_gen_andi_tl(ret, ret, 1);
}

static bool trans_bext(DisasContext *ctx, arg_bext *a)
{
    REQUIRE_ZBS(ctx);
    return gen_shift(ctx, a, EXT_NONE, gen_bext, NULL);
}

static bool trans_bexti(DisasContext *ctx, arg_bexti *a)
{
    REQUIRE_ZBS(ctx);
    return gen_shift_imm_tl(ctx, a, EXT_NONE, gen_bext);
}

static void gen_rorw(TCGv ret, TCGv arg1, TCGv arg2)
{
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 t2 = tcg_temp_new_i32();

    /* truncate to 32-bits */
    tcg_gen_trunc_tl_i32(t1, arg1);
    tcg_gen_trunc_tl_i32(t2, arg2);

    tcg_gen_rotr_i32(t1, t1, t2);

    /* sign-extend 64-bits */
    tcg_gen_ext_i32_tl(ret, t1);

    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t2);
}

static bool trans_ror(DisasContext *ctx, arg_ror *a)
{
    REQUIRE_ZBB(ctx);
    return gen_shift_per_ol(ctx, a, EXT_NONE, tcg_gen_rotr_tl, gen_rorw, NULL);
}

static void gen_roriw(TCGv ret, TCGv arg1, target_long shamt)
{
    TCGv_i32 t1 = tcg_temp_new_i32();

    tcg_gen_trunc_tl_i32(t1, arg1);
    tcg_gen_rotri_i32(t1, t1, shamt);
    tcg_gen_ext_i32_tl(ret, t1);

    tcg_temp_free_i32(t1);
}

static bool trans_rori(DisasContext *ctx, arg_rori *a)
{
    REQUIRE_ZBB(ctx);
    return gen_shift_imm_fn_per_ol(ctx, a, EXT_NONE,
                                   tcg_gen_rotri_tl, gen_roriw, NULL);
}

static void gen_rolw(TCGv ret, TCGv arg1, TCGv arg2)
{
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 t2 = tcg_temp_new_i32();

    /* truncate to 32-bits */
    tcg_gen_trunc_tl_i32(t1, arg1);
    tcg_gen_trunc_tl_i32(t2, arg2);

    tcg_gen_rotl_i32(t1, t1, t2);

    /* sign-extend 64-bits */
    tcg_gen_ext_i32_tl(ret, t1);

    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t2);
}

static bool trans_rol(DisasContext *ctx, arg_rol *a)
{
    REQUIRE_ZBB(ctx);
    return gen_shift_per_ol(ctx, a, EXT_NONE, tcg_gen_rotl_tl, gen_rolw, NULL);
}

static void gen_rev8_32(TCGv ret, TCGv src1)
{
    tcg_gen_bswap32_tl(ret, src1, TCG_BSWAP_OS);
}

static bool trans_rev8_32(DisasContext *ctx, arg_rev8_32 *a)
{
    REQUIRE_32BIT(ctx);
    REQUIRE_ZBB(ctx);
    return gen_unary(ctx, a, EXT_NONE, gen_rev8_32);
}

static bool trans_rev8_64(DisasContext *ctx, arg_rev8_64 *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_ZBB(ctx);
    return gen_unary(ctx, a, EXT_NONE, tcg_gen_bswap_tl);
}

static void gen_orc_b(TCGv ret, TCGv source1)
{
    TCGv  tmp = tcg_temp_new();
    TCGv  low7 = tcg_constant_tl(dup_const_tl(MO_8, 0x7f));

    /* Set msb in each byte if the byte was non-zero. */
    tcg_gen_and_tl(tmp, source1, low7);
    tcg_gen_add_tl(tmp, tmp, low7);
    tcg_gen_or_tl(tmp, tmp, source1);

    /* Extract the msb to the lsb in each byte */
    tcg_gen_andc_tl(tmp, tmp, low7);
    tcg_gen_shri_tl(tmp, tmp, 7);

    /* Replicate the lsb of each byte across the byte. */
    tcg_gen_muli_tl(ret, tmp, 0xff);

    tcg_temp_free(tmp);
}

static bool trans_orc_b(DisasContext *ctx, arg_orc_b *a)
{
    REQUIRE_ZBB(ctx);
    return gen_unary(ctx, a, EXT_ZERO, gen_orc_b);
}

#define GEN_SHADD(SHAMT)                                       \
static void gen_sh##SHAMT##add(TCGv ret, TCGv arg1, TCGv arg2) \
{                                                              \
    TCGv t = tcg_temp_new();                                   \
                                                               \
    tcg_gen_shli_tl(t, arg1, SHAMT);                           \
    tcg_gen_add_tl(ret, t, arg2);                              \
                                                               \
    tcg_temp_free(t);                                          \
}

GEN_SHADD(1)
GEN_SHADD(2)
GEN_SHADD(3)

#define GEN_TRANS_SHADD(SHAMT)                                             \
static bool trans_sh##SHAMT##add(DisasContext *ctx, arg_sh##SHAMT##add *a) \
{                                                                          \
    REQUIRE_ZBA(ctx);                                                      \
    return gen_arith(ctx, a, EXT_NONE, gen_sh##SHAMT##add, NULL);          \
}

GEN_TRANS_SHADD(1)
GEN_TRANS_SHADD(2)
GEN_TRANS_SHADD(3)

static bool trans_zext_h_32(DisasContext *ctx, arg_zext_h_32 *a)
{
    REQUIRE_32BIT(ctx);
    REQUIRE_ZBB(ctx);
    return gen_unary(ctx, a, EXT_NONE, tcg_gen_ext16u_tl);
}

static bool trans_zext_h_64(DisasContext *ctx, arg_zext_h_64 *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_ZBB(ctx);
    return gen_unary(ctx, a, EXT_NONE, tcg_gen_ext16u_tl);
}

static bool trans_clzw(DisasContext *ctx, arg_clzw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_ZBB(ctx);
    return gen_unary(ctx, a, EXT_NONE, gen_clzw);
}

static bool trans_ctzw(DisasContext *ctx, arg_ctzw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_ZBB(ctx);
    return gen_unary(ctx, a, EXT_ZERO, gen_ctzw);
}

static bool trans_cpopw(DisasContext *ctx, arg_cpopw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_ZBB(ctx);
    ctx->ol = MXL_RV32;
    return gen_unary(ctx, a, EXT_ZERO, tcg_gen_ctpop_tl);
}

static bool trans_rorw(DisasContext *ctx, arg_rorw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_ZBB(ctx);
    ctx->ol = MXL_RV32;
    return gen_shift(ctx, a, EXT_NONE, gen_rorw, NULL);
}

static bool trans_roriw(DisasContext *ctx, arg_roriw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_ZBB(ctx);
    ctx->ol = MXL_RV32;
    return gen_shift_imm_fn(ctx, a, EXT_NONE, gen_roriw, NULL);
}

static bool trans_rolw(DisasContext *ctx, arg_rolw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_ZBB(ctx);
    ctx->ol = MXL_RV32;
    return gen_shift(ctx, a, EXT_NONE, gen_rolw, NULL);
}

#define GEN_SHADD_UW(SHAMT)                                       \
static void gen_sh##SHAMT##add_uw(TCGv ret, TCGv arg1, TCGv arg2) \
{                                                                 \
    TCGv t = tcg_temp_new();                                      \
                                                                  \
    tcg_gen_ext32u_tl(t, arg1);                                   \
                                                                  \
    tcg_gen_shli_tl(t, t, SHAMT);                                 \
    tcg_gen_add_tl(ret, t, arg2);                                 \
                                                                  \
    tcg_temp_free(t);                                             \
}

GEN_SHADD_UW(1)
GEN_SHADD_UW(2)
GEN_SHADD_UW(3)

#define GEN_TRANS_SHADD_UW(SHAMT)                             \
static bool trans_sh##SHAMT##add_uw(DisasContext *ctx,        \
                                    arg_sh##SHAMT##add_uw *a) \
{                                                             \
    REQUIRE_64BIT(ctx);                                       \
    REQUIRE_ZBA(ctx);                                         \
    return gen_arith(ctx, a, EXT_NONE, gen_sh##SHAMT##add_uw, NULL); \
}

GEN_TRANS_SHADD_UW(1)
GEN_TRANS_SHADD_UW(2)
GEN_TRANS_SHADD_UW(3)

static void gen_add_uw(TCGv ret, TCGv arg1, TCGv arg2)
{
    TCGv t = tcg_temp_new();
    tcg_gen_ext32u_tl(t, arg1);
    tcg_gen_add_tl(ret, t, arg2);
    tcg_temp_free(t);
}

static bool trans_add_uw(DisasContext *ctx, arg_add_uw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_ZBA(ctx);
    return gen_arith(ctx, a, EXT_NONE, gen_add_uw, NULL);
}

static void gen_slli_uw(TCGv dest, TCGv src, target_long shamt)
{
    tcg_gen_deposit_z_tl(dest, src, shamt, MIN(32, TARGET_LONG_BITS - shamt));
}

static bool trans_slli_uw(DisasContext *ctx, arg_slli_uw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_ZBA(ctx);
    return gen_shift_imm_fn(ctx, a, EXT_NONE, gen_slli_uw, NULL);
}

static bool trans_clmul(DisasContext *ctx, arg_clmul *a)
{
    REQUIRE_ZBC(ctx);
    return gen_arith(ctx, a, EXT_NONE, gen_helper_clmul, NULL);
}

static void gen_clmulh(TCGv dst, TCGv src1, TCGv src2)
{
     gen_helper_clmulr(dst, src1, src2);
     tcg_gen_shri_tl(dst, dst, 1);
}

static bool trans_clmulh(DisasContext *ctx, arg_clmulr *a)
{
    REQUIRE_ZBC(ctx);
    return gen_arith(ctx, a, EXT_NONE, gen_clmulh, NULL);
}

static bool trans_clmulr(DisasContext *ctx, arg_clmulh *a)
{
    REQUIRE_ZBC(ctx);
    return gen_arith(ctx, a, EXT_NONE, gen_helper_clmulr, NULL);
}
