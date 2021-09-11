/*
 * RISC-V translation routines for the RVB draft and Zba Standard Extension.
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
    if (!RISCV_CPU(ctx->cs)->cfg.ext_zba) {      \
        return false;                            \
    }                                            \
} while (0)

static void gen_clz(TCGv ret, TCGv arg1)
{
    tcg_gen_clzi_tl(ret, arg1, TARGET_LONG_BITS);
}

static bool trans_clz(DisasContext *ctx, arg_clz *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_unary(ctx, a, EXT_ZERO, gen_clz);
}

static void gen_ctz(TCGv ret, TCGv arg1)
{
    tcg_gen_ctzi_tl(ret, arg1, TARGET_LONG_BITS);
}

static bool trans_ctz(DisasContext *ctx, arg_ctz *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_unary(ctx, a, EXT_ZERO, gen_ctz);
}

static bool trans_cpop(DisasContext *ctx, arg_cpop *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_unary(ctx, a, EXT_ZERO, tcg_gen_ctpop_tl);
}

static bool trans_andn(DisasContext *ctx, arg_andn *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_arith(ctx, a, EXT_NONE, tcg_gen_andc_tl);
}

static bool trans_orn(DisasContext *ctx, arg_orn *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_arith(ctx, a, EXT_NONE, tcg_gen_orc_tl);
}

static bool trans_xnor(DisasContext *ctx, arg_xnor *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_arith(ctx, a, EXT_NONE, tcg_gen_eqv_tl);
}

static void gen_pack(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_deposit_tl(ret, arg1, arg2,
                       TARGET_LONG_BITS / 2,
                       TARGET_LONG_BITS / 2);
}

static bool trans_pack(DisasContext *ctx, arg_pack *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_arith(ctx, a, EXT_NONE, gen_pack);
}

static void gen_packu(TCGv ret, TCGv arg1, TCGv arg2)
{
    TCGv t = tcg_temp_new();
    tcg_gen_shri_tl(t, arg1, TARGET_LONG_BITS / 2);
    tcg_gen_deposit_tl(ret, arg2, t, 0, TARGET_LONG_BITS / 2);
    tcg_temp_free(t);
}

static bool trans_packu(DisasContext *ctx, arg_packu *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_arith(ctx, a, EXT_NONE, gen_packu);
}

static void gen_packh(TCGv ret, TCGv arg1, TCGv arg2)
{
    TCGv t = tcg_temp_new();
    tcg_gen_ext8u_tl(t, arg2);
    tcg_gen_deposit_tl(ret, arg1, t, 8, TARGET_LONG_BITS - 8);
    tcg_temp_free(t);
}

static bool trans_packh(DisasContext *ctx, arg_packh *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_arith(ctx, a, EXT_NONE, gen_packh);
}

static bool trans_min(DisasContext *ctx, arg_min *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_arith(ctx, a, EXT_SIGN, tcg_gen_smin_tl);
}

static bool trans_max(DisasContext *ctx, arg_max *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_arith(ctx, a, EXT_SIGN, tcg_gen_smax_tl);
}

static bool trans_minu(DisasContext *ctx, arg_minu *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_arith(ctx, a, EXT_SIGN, tcg_gen_umin_tl);
}

static bool trans_maxu(DisasContext *ctx, arg_maxu *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_arith(ctx, a, EXT_SIGN, tcg_gen_umax_tl);
}

static bool trans_sext_b(DisasContext *ctx, arg_sext_b *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_unary(ctx, a, EXT_NONE, tcg_gen_ext8s_tl);
}

static bool trans_sext_h(DisasContext *ctx, arg_sext_h *a)
{
    REQUIRE_EXT(ctx, RVB);
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
    REQUIRE_EXT(ctx, RVB);
    return gen_shift(ctx, a, EXT_NONE, gen_bset);
}

static bool trans_bseti(DisasContext *ctx, arg_bseti *a)
{
    REQUIRE_EXT(ctx, RVB);
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
    REQUIRE_EXT(ctx, RVB);
    return gen_shift(ctx, a, EXT_NONE, gen_bclr);
}

static bool trans_bclri(DisasContext *ctx, arg_bclri *a)
{
    REQUIRE_EXT(ctx, RVB);
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
    REQUIRE_EXT(ctx, RVB);
    return gen_shift(ctx, a, EXT_NONE, gen_binv);
}

static bool trans_binvi(DisasContext *ctx, arg_binvi *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_shift_imm_tl(ctx, a, EXT_NONE, gen_binv);
}

static void gen_bext(TCGv ret, TCGv arg1, TCGv shamt)
{
    tcg_gen_shr_tl(ret, arg1, shamt);
    tcg_gen_andi_tl(ret, ret, 1);
}

static bool trans_bext(DisasContext *ctx, arg_bext *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_shift(ctx, a, EXT_NONE, gen_bext);
}

static bool trans_bexti(DisasContext *ctx, arg_bexti *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_shift_imm_tl(ctx, a, EXT_NONE, gen_bext);
}

static void gen_slo(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_not_tl(ret, arg1);
    tcg_gen_shl_tl(ret, ret, arg2);
    tcg_gen_not_tl(ret, ret);
}

static bool trans_slo(DisasContext *ctx, arg_slo *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_shift(ctx, a, EXT_NONE, gen_slo);
}

static bool trans_sloi(DisasContext *ctx, arg_sloi *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_shift_imm_tl(ctx, a, EXT_NONE, gen_slo);
}

static void gen_sro(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_not_tl(ret, arg1);
    tcg_gen_shr_tl(ret, ret, arg2);
    tcg_gen_not_tl(ret, ret);
}

static bool trans_sro(DisasContext *ctx, arg_sro *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_shift(ctx, a, EXT_ZERO, gen_sro);
}

static bool trans_sroi(DisasContext *ctx, arg_sroi *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_shift_imm_tl(ctx, a, EXT_ZERO, gen_sro);
}

static bool trans_ror(DisasContext *ctx, arg_ror *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_shift(ctx, a, EXT_NONE, tcg_gen_rotr_tl);
}

static bool trans_rori(DisasContext *ctx, arg_rori *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_shift_imm_fn(ctx, a, EXT_NONE, tcg_gen_rotri_tl);
}

static bool trans_rol(DisasContext *ctx, arg_rol *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_shift(ctx, a, EXT_NONE, tcg_gen_rotl_tl);
}

static bool trans_grev(DisasContext *ctx, arg_grev *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_shift(ctx, a, EXT_NONE, gen_helper_grev);
}

static void gen_grevi(TCGv dest, TCGv src, target_long shamt)
{
    if (shamt == TARGET_LONG_BITS - 8) {
        /* rev8, byte swaps */
        tcg_gen_bswap_tl(dest, src);
    } else {
        gen_helper_grev(dest, src, tcg_constant_tl(shamt));
    }
}

static bool trans_grevi(DisasContext *ctx, arg_grevi *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_shift_imm_fn(ctx, a, EXT_NONE, gen_grevi);
}

static bool trans_gorc(DisasContext *ctx, arg_gorc *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_shift(ctx, a, EXT_ZERO, gen_helper_gorc);
}

static bool trans_gorci(DisasContext *ctx, arg_gorci *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_shift_imm_tl(ctx, a, EXT_ZERO, gen_helper_gorc);
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
    return gen_arith(ctx, a, EXT_NONE, gen_sh##SHAMT##add);                \
}

GEN_TRANS_SHADD(1)
GEN_TRANS_SHADD(2)
GEN_TRANS_SHADD(3)

static void gen_clzw(TCGv ret, TCGv arg1)
{
    TCGv t = tcg_temp_new();
    tcg_gen_shli_tl(t, arg1, 32);
    tcg_gen_clzi_tl(ret, t, 32);
    tcg_temp_free(t);
}

static bool trans_clzw(DisasContext *ctx, arg_clzw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVB);
    return gen_unary(ctx, a, EXT_NONE, gen_clzw);
}

static void gen_ctzw(TCGv ret, TCGv arg1)
{
    tcg_gen_ori_tl(ret, arg1, (target_ulong)MAKE_64BIT_MASK(32, 32));
    tcg_gen_ctzi_tl(ret, ret, 64);
}

static bool trans_ctzw(DisasContext *ctx, arg_ctzw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVB);
    return gen_unary(ctx, a, EXT_NONE, gen_ctzw);
}

static bool trans_cpopw(DisasContext *ctx, arg_cpopw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVB);
    ctx->w = true;
    return gen_unary(ctx, a, EXT_ZERO, tcg_gen_ctpop_tl);
}

static void gen_packw(TCGv ret, TCGv arg1, TCGv arg2)
{
    TCGv t = tcg_temp_new();
    tcg_gen_ext16s_tl(t, arg2);
    tcg_gen_deposit_tl(ret, arg1, t, 16, 48);
    tcg_temp_free(t);
}

static bool trans_packw(DisasContext *ctx, arg_packw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVB);
    return gen_arith(ctx, a, EXT_NONE, gen_packw);
}

static void gen_packuw(TCGv ret, TCGv arg1, TCGv arg2)
{
    TCGv t = tcg_temp_new();
    tcg_gen_shri_tl(t, arg1, 16);
    tcg_gen_deposit_tl(ret, arg2, t, 0, 16);
    tcg_gen_ext32s_tl(ret, ret);
    tcg_temp_free(t);
}

static bool trans_packuw(DisasContext *ctx, arg_packuw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVB);
    return gen_arith(ctx, a, EXT_NONE, gen_packuw);
}

static bool trans_bsetw(DisasContext *ctx, arg_bsetw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVB);
    ctx->w = true;
    return gen_shift(ctx, a, EXT_NONE, gen_bset);
}

static bool trans_bsetiw(DisasContext *ctx, arg_bsetiw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVB);
    ctx->w = true;
    return gen_shift_imm_tl(ctx, a, EXT_NONE, gen_bset);
}

static bool trans_bclrw(DisasContext *ctx, arg_bclrw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVB);
    ctx->w = true;
    return gen_shift(ctx, a, EXT_NONE, gen_bclr);
}

static bool trans_bclriw(DisasContext *ctx, arg_bclriw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVB);
    ctx->w = true;
    return gen_shift_imm_tl(ctx, a, EXT_NONE, gen_bclr);
}

static bool trans_binvw(DisasContext *ctx, arg_binvw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVB);
    ctx->w = true;
    return gen_shift(ctx, a, EXT_NONE, gen_binv);
}

static bool trans_binviw(DisasContext *ctx, arg_binviw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVB);
    ctx->w = true;
    return gen_shift_imm_tl(ctx, a, EXT_NONE, gen_binv);
}

static bool trans_bextw(DisasContext *ctx, arg_bextw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVB);
    ctx->w = true;
    return gen_shift(ctx, a, EXT_NONE, gen_bext);
}

static bool trans_slow(DisasContext *ctx, arg_slow *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVB);
    ctx->w = true;
    return gen_shift(ctx, a, EXT_NONE, gen_slo);
}

static bool trans_sloiw(DisasContext *ctx, arg_sloiw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVB);
    ctx->w = true;
    return gen_shift_imm_tl(ctx, a, EXT_NONE, gen_slo);
}

static bool trans_srow(DisasContext *ctx, arg_srow *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVB);
    ctx->w = true;
    return gen_shift(ctx, a, EXT_ZERO, gen_sro);
}

static bool trans_sroiw(DisasContext *ctx, arg_sroiw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVB);
    ctx->w = true;
    return gen_shift_imm_tl(ctx, a, EXT_ZERO, gen_sro);
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

static bool trans_rorw(DisasContext *ctx, arg_rorw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVB);
    ctx->w = true;
    return gen_shift(ctx, a, EXT_NONE, gen_rorw);
}

static bool trans_roriw(DisasContext *ctx, arg_roriw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVB);
    ctx->w = true;
    return gen_shift_imm_tl(ctx, a, EXT_NONE, gen_rorw);
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

static bool trans_rolw(DisasContext *ctx, arg_rolw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVB);
    ctx->w = true;
    return gen_shift(ctx, a, EXT_NONE, gen_rolw);
}

static bool trans_grevw(DisasContext *ctx, arg_grevw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVB);
    ctx->w = true;
    return gen_shift(ctx, a, EXT_ZERO, gen_helper_grev);
}

static bool trans_greviw(DisasContext *ctx, arg_greviw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVB);
    ctx->w = true;
    return gen_shift_imm_tl(ctx, a, EXT_ZERO, gen_helper_grev);
}

static bool trans_gorcw(DisasContext *ctx, arg_gorcw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVB);
    ctx->w = true;
    return gen_shift(ctx, a, EXT_ZERO, gen_helper_gorc);
}

static bool trans_gorciw(DisasContext *ctx, arg_gorciw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVB);
    ctx->w = true;
    return gen_shift_imm_tl(ctx, a, EXT_ZERO, gen_helper_gorc);
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
    return gen_arith(ctx, a, EXT_NONE, gen_sh##SHAMT##add_uw);  \
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
    return gen_arith(ctx, a, EXT_NONE, gen_add_uw);
}

static void gen_slli_uw(TCGv dest, TCGv src, target_long shamt)
{
    tcg_gen_deposit_z_tl(dest, src, shamt, MIN(32, TARGET_LONG_BITS - shamt));
}

static bool trans_slli_uw(DisasContext *ctx, arg_slli_uw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_ZBA(ctx);
    return gen_shift_imm_fn(ctx, a, EXT_NONE, gen_slli_uw);
}
