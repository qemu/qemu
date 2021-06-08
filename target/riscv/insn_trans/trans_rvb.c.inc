/*
 * RISC-V translation routines for the RVB Standard Extension.
 *
 * Copyright (c) 2020 Kito Cheng, kito.cheng@sifive.com
 * Copyright (c) 2020 Frank Chang, frank.chang@sifive.com
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

static bool trans_clz(DisasContext *ctx, arg_clz *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_unary(ctx, a, gen_clz);
}

static bool trans_ctz(DisasContext *ctx, arg_ctz *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_unary(ctx, a, gen_ctz);
}

static bool trans_cpop(DisasContext *ctx, arg_cpop *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_unary(ctx, a, tcg_gen_ctpop_tl);
}

static bool trans_andn(DisasContext *ctx, arg_andn *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_arith(ctx, a, tcg_gen_andc_tl);
}

static bool trans_orn(DisasContext *ctx, arg_orn *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_arith(ctx, a, tcg_gen_orc_tl);
}

static bool trans_xnor(DisasContext *ctx, arg_xnor *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_arith(ctx, a, tcg_gen_eqv_tl);
}

static bool trans_pack(DisasContext *ctx, arg_pack *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_arith(ctx, a, gen_pack);
}

static bool trans_packu(DisasContext *ctx, arg_packu *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_arith(ctx, a, gen_packu);
}

static bool trans_packh(DisasContext *ctx, arg_packh *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_arith(ctx, a, gen_packh);
}

static bool trans_min(DisasContext *ctx, arg_min *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_arith(ctx, a, tcg_gen_smin_tl);
}

static bool trans_max(DisasContext *ctx, arg_max *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_arith(ctx, a, tcg_gen_smax_tl);
}

static bool trans_minu(DisasContext *ctx, arg_minu *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_arith(ctx, a, tcg_gen_umin_tl);
}

static bool trans_maxu(DisasContext *ctx, arg_maxu *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_arith(ctx, a, tcg_gen_umax_tl);
}

static bool trans_sext_b(DisasContext *ctx, arg_sext_b *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_unary(ctx, a, tcg_gen_ext8s_tl);
}

static bool trans_sext_h(DisasContext *ctx, arg_sext_h *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_unary(ctx, a, tcg_gen_ext16s_tl);
}

static bool trans_bset(DisasContext *ctx, arg_bset *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_shift(ctx, a, gen_bset);
}

static bool trans_bseti(DisasContext *ctx, arg_bseti *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_shifti(ctx, a, gen_bset);
}

static bool trans_bclr(DisasContext *ctx, arg_bclr *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_shift(ctx, a, gen_bclr);
}

static bool trans_bclri(DisasContext *ctx, arg_bclri *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_shifti(ctx, a, gen_bclr);
}

static bool trans_binv(DisasContext *ctx, arg_binv *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_shift(ctx, a, gen_binv);
}

static bool trans_binvi(DisasContext *ctx, arg_binvi *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_shifti(ctx, a, gen_binv);
}

static bool trans_bext(DisasContext *ctx, arg_bext *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_shift(ctx, a, gen_bext);
}

static bool trans_bexti(DisasContext *ctx, arg_bexti *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_shifti(ctx, a, gen_bext);
}

static bool trans_slo(DisasContext *ctx, arg_slo *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_shift(ctx, a, gen_slo);
}

static bool trans_sloi(DisasContext *ctx, arg_sloi *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_shifti(ctx, a, gen_slo);
}

static bool trans_sro(DisasContext *ctx, arg_sro *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_shift(ctx, a, gen_sro);
}

static bool trans_sroi(DisasContext *ctx, arg_sroi *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_shifti(ctx, a, gen_sro);
}

static bool trans_ror(DisasContext *ctx, arg_ror *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_shift(ctx, a, tcg_gen_rotr_tl);
}

static bool trans_rori(DisasContext *ctx, arg_rori *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_shifti(ctx, a, tcg_gen_rotr_tl);
}

static bool trans_rol(DisasContext *ctx, arg_rol *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_shift(ctx, a, tcg_gen_rotl_tl);
}

static bool trans_grev(DisasContext *ctx, arg_grev *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_shift(ctx, a, gen_helper_grev);
}

static bool trans_grevi(DisasContext *ctx, arg_grevi *a)
{
    REQUIRE_EXT(ctx, RVB);

    if (a->shamt >= TARGET_LONG_BITS) {
        return false;
    }

    return gen_grevi(ctx, a);
}

static bool trans_gorc(DisasContext *ctx, arg_gorc *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_shift(ctx, a, gen_helper_gorc);
}

static bool trans_gorci(DisasContext *ctx, arg_gorci *a)
{
    REQUIRE_EXT(ctx, RVB);
    return gen_shifti(ctx, a, gen_helper_gorc);
}

#define GEN_TRANS_SHADD(SHAMT)                                             \
static bool trans_sh##SHAMT##add(DisasContext *ctx, arg_sh##SHAMT##add *a) \
{                                                                          \
    REQUIRE_EXT(ctx, RVB);                                                 \
    return gen_arith(ctx, a, gen_sh##SHAMT##add);                          \
}

GEN_TRANS_SHADD(1)
GEN_TRANS_SHADD(2)
GEN_TRANS_SHADD(3)

static bool trans_clzw(DisasContext *ctx, arg_clzw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVB);
    return gen_unary(ctx, a, gen_clzw);
}

static bool trans_ctzw(DisasContext *ctx, arg_ctzw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVB);
    return gen_unary(ctx, a, gen_ctzw);
}

static bool trans_cpopw(DisasContext *ctx, arg_cpopw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVB);
    return gen_unary(ctx, a, gen_cpopw);
}

static bool trans_packw(DisasContext *ctx, arg_packw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVB);
    return gen_arith(ctx, a, gen_packw);
}

static bool trans_packuw(DisasContext *ctx, arg_packuw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVB);
    return gen_arith(ctx, a, gen_packuw);
}

static bool trans_bsetw(DisasContext *ctx, arg_bsetw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVB);
    return gen_shiftw(ctx, a, gen_bset);
}

static bool trans_bsetiw(DisasContext *ctx, arg_bsetiw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVB);
    return gen_shiftiw(ctx, a, gen_bset);
}

static bool trans_bclrw(DisasContext *ctx, arg_bclrw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVB);
    return gen_shiftw(ctx, a, gen_bclr);
}

static bool trans_bclriw(DisasContext *ctx, arg_bclriw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVB);
    return gen_shiftiw(ctx, a, gen_bclr);
}

static bool trans_binvw(DisasContext *ctx, arg_binvw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVB);
    return gen_shiftw(ctx, a, gen_binv);
}

static bool trans_binviw(DisasContext *ctx, arg_binviw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVB);
    return gen_shiftiw(ctx, a, gen_binv);
}

static bool trans_bextw(DisasContext *ctx, arg_bextw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVB);
    return gen_shiftw(ctx, a, gen_bext);
}

static bool trans_slow(DisasContext *ctx, arg_slow *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVB);
    return gen_shiftw(ctx, a, gen_slo);
}

static bool trans_sloiw(DisasContext *ctx, arg_sloiw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVB);
    return gen_shiftiw(ctx, a, gen_slo);
}

static bool trans_srow(DisasContext *ctx, arg_srow *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVB);
    return gen_shiftw(ctx, a, gen_sro);
}

static bool trans_sroiw(DisasContext *ctx, arg_sroiw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVB);
    return gen_shiftiw(ctx, a, gen_sro);
}

static bool trans_rorw(DisasContext *ctx, arg_rorw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVB);
    return gen_shiftw(ctx, a, gen_rorw);
}

static bool trans_roriw(DisasContext *ctx, arg_roriw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVB);
    return gen_shiftiw(ctx, a, gen_rorw);
}

static bool trans_rolw(DisasContext *ctx, arg_rolw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVB);
    return gen_shiftw(ctx, a, gen_rolw);
}

static bool trans_grevw(DisasContext *ctx, arg_grevw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVB);
    return gen_shiftw(ctx, a, gen_grevw);
}

static bool trans_greviw(DisasContext *ctx, arg_greviw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVB);
    return gen_shiftiw(ctx, a, gen_grevw);
}

static bool trans_gorcw(DisasContext *ctx, arg_gorcw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVB);
    return gen_shiftw(ctx, a, gen_gorcw);
}

static bool trans_gorciw(DisasContext *ctx, arg_gorciw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVB);
    return gen_shiftiw(ctx, a, gen_gorcw);
}

#define GEN_TRANS_SHADD_UW(SHAMT)                             \
static bool trans_sh##SHAMT##add_uw(DisasContext *ctx,        \
                                    arg_sh##SHAMT##add_uw *a) \
{                                                             \
    REQUIRE_64BIT(ctx);                                       \
    REQUIRE_EXT(ctx, RVB);                                    \
    return gen_arith(ctx, a, gen_sh##SHAMT##add_uw);          \
}

GEN_TRANS_SHADD_UW(1)
GEN_TRANS_SHADD_UW(2)
GEN_TRANS_SHADD_UW(3)

static bool trans_add_uw(DisasContext *ctx, arg_add_uw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVB);
    return gen_arith(ctx, a, gen_add_uw);
}

static bool trans_slli_uw(DisasContext *ctx, arg_slli_uw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVB);

    TCGv source1 = tcg_temp_new();
    gen_get_gpr(source1, a->rs1);

    if (a->shamt < 32) {
        tcg_gen_deposit_z_tl(source1, source1, a->shamt, 32);
    } else {
        tcg_gen_shli_tl(source1, source1, a->shamt);
    }

    gen_set_gpr(a->rd, source1);
    tcg_temp_free(source1);
    return true;
}
