/*
 * RISC-V translation routines for the RV64M Standard Extension.
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
 * Copyright (c) 2018 Peer Adelt, peer.adelt@hni.uni-paderborn.de
 *                    Bastian Koppelmann, kbastian@mail.uni-paderborn.de
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


static bool trans_mul(DisasContext *ctx, arg_mul *a)
{
    REQUIRE_EXT(ctx, RVM);
    return gen_arith(ctx, a, EXT_NONE, tcg_gen_mul_tl);
}

static void gen_mulh(TCGv ret, TCGv s1, TCGv s2)
{
    TCGv discard = tcg_temp_new();

    tcg_gen_muls2_tl(discard, ret, s1, s2);
    tcg_temp_free(discard);
}

static void gen_mulh_w(TCGv ret, TCGv s1, TCGv s2)
{
    tcg_gen_mul_tl(ret, s1, s2);
    tcg_gen_sari_tl(ret, ret, 32);
}

static bool trans_mulh(DisasContext *ctx, arg_mulh *a)
{
    REQUIRE_EXT(ctx, RVM);
    return gen_arith_per_ol(ctx, a, EXT_SIGN, gen_mulh, gen_mulh_w);
}

static void gen_mulhsu(TCGv ret, TCGv arg1, TCGv arg2)
{
    TCGv rl = tcg_temp_new();
    TCGv rh = tcg_temp_new();

    tcg_gen_mulu2_tl(rl, rh, arg1, arg2);
    /* fix up for one negative */
    tcg_gen_sari_tl(rl, arg1, TARGET_LONG_BITS - 1);
    tcg_gen_and_tl(rl, rl, arg2);
    tcg_gen_sub_tl(ret, rh, rl);

    tcg_temp_free(rl);
    tcg_temp_free(rh);
}

static void gen_mulhsu_w(TCGv ret, TCGv arg1, TCGv arg2)
{
    TCGv t1 = tcg_temp_new();
    TCGv t2 = tcg_temp_new();

    tcg_gen_ext32s_tl(t1, arg1);
    tcg_gen_ext32u_tl(t2, arg2);
    tcg_gen_mul_tl(ret, t1, t2);
    tcg_temp_free(t1);
    tcg_temp_free(t2);
    tcg_gen_sari_tl(ret, ret, 32);
}

static bool trans_mulhsu(DisasContext *ctx, arg_mulhsu *a)
{
    REQUIRE_EXT(ctx, RVM);
    return gen_arith_per_ol(ctx, a, EXT_NONE, gen_mulhsu, gen_mulhsu_w);
}

static void gen_mulhu(TCGv ret, TCGv s1, TCGv s2)
{
    TCGv discard = tcg_temp_new();

    tcg_gen_mulu2_tl(discard, ret, s1, s2);
    tcg_temp_free(discard);
}

static bool trans_mulhu(DisasContext *ctx, arg_mulhu *a)
{
    REQUIRE_EXT(ctx, RVM);
    /* gen_mulh_w works for either sign as input. */
    return gen_arith_per_ol(ctx, a, EXT_ZERO, gen_mulhu, gen_mulh_w);
}

static void gen_div(TCGv ret, TCGv source1, TCGv source2)
{
    TCGv temp1, temp2, zero, one, mone, min;

    temp1 = tcg_temp_new();
    temp2 = tcg_temp_new();
    zero = tcg_constant_tl(0);
    one = tcg_constant_tl(1);
    mone = tcg_constant_tl(-1);
    min = tcg_constant_tl(1ull << (TARGET_LONG_BITS - 1));

    /*
     * If overflow, set temp2 to 1, else source2.
     * This produces the required result of min.
     */
    tcg_gen_setcond_tl(TCG_COND_EQ, temp1, source1, min);
    tcg_gen_setcond_tl(TCG_COND_EQ, temp2, source2, mone);
    tcg_gen_and_tl(temp1, temp1, temp2);
    tcg_gen_movcond_tl(TCG_COND_NE, temp2, temp1, zero, one, source2);

    /*
     * If div by zero, set temp1 to -1 and temp2 to 1 to
     * produce the required result of -1.
     */
    tcg_gen_movcond_tl(TCG_COND_EQ, temp1, source2, zero, mone, source1);
    tcg_gen_movcond_tl(TCG_COND_EQ, temp2, source2, zero, one, temp2);

    tcg_gen_div_tl(ret, temp1, temp2);

    tcg_temp_free(temp1);
    tcg_temp_free(temp2);
}

static bool trans_div(DisasContext *ctx, arg_div *a)
{
    REQUIRE_EXT(ctx, RVM);
    return gen_arith(ctx, a, EXT_SIGN, gen_div);
}

static void gen_divu(TCGv ret, TCGv source1, TCGv source2)
{
    TCGv temp1, temp2, zero, one, max;

    temp1 = tcg_temp_new();
    temp2 = tcg_temp_new();
    zero = tcg_constant_tl(0);
    one = tcg_constant_tl(1);
    max = tcg_constant_tl(~0);

    /*
     * If div by zero, set temp1 to max and temp2 to 1 to
     * produce the required result of max.
     */
    tcg_gen_movcond_tl(TCG_COND_EQ, temp1, source2, zero, max, source1);
    tcg_gen_movcond_tl(TCG_COND_EQ, temp2, source2, zero, one, source2);
    tcg_gen_divu_tl(ret, temp1, temp2);

    tcg_temp_free(temp1);
    tcg_temp_free(temp2);
}

static bool trans_divu(DisasContext *ctx, arg_divu *a)
{
    REQUIRE_EXT(ctx, RVM);
    return gen_arith(ctx, a, EXT_ZERO, gen_divu);
}

static void gen_rem(TCGv ret, TCGv source1, TCGv source2)
{
    TCGv temp1, temp2, zero, one, mone, min;

    temp1 = tcg_temp_new();
    temp2 = tcg_temp_new();
    zero = tcg_constant_tl(0);
    one = tcg_constant_tl(1);
    mone = tcg_constant_tl(-1);
    min = tcg_constant_tl(1ull << (TARGET_LONG_BITS - 1));

    /*
     * If overflow, set temp1 to 0, else source1.
     * This avoids a possible host trap, and produces the required result of 0.
     */
    tcg_gen_setcond_tl(TCG_COND_EQ, temp1, source1, min);
    tcg_gen_setcond_tl(TCG_COND_EQ, temp2, source2, mone);
    tcg_gen_and_tl(temp1, temp1, temp2);
    tcg_gen_movcond_tl(TCG_COND_NE, temp1, temp1, zero, zero, source1);

    /*
     * If div by zero, set temp2 to 1, else source2.
     * This avoids a possible host trap, but produces an incorrect result.
     */
    tcg_gen_movcond_tl(TCG_COND_EQ, temp2, source2, zero, one, source2);

    tcg_gen_rem_tl(temp1, temp1, temp2);

    /* If div by zero, the required result is the original dividend. */
    tcg_gen_movcond_tl(TCG_COND_EQ, ret, source2, zero, source1, temp1);

    tcg_temp_free(temp1);
    tcg_temp_free(temp2);
}

static bool trans_rem(DisasContext *ctx, arg_rem *a)
{
    REQUIRE_EXT(ctx, RVM);
    return gen_arith(ctx, a, EXT_SIGN, gen_rem);
}

static void gen_remu(TCGv ret, TCGv source1, TCGv source2)
{
    TCGv temp, zero, one;

    temp = tcg_temp_new();
    zero = tcg_constant_tl(0);
    one = tcg_constant_tl(1);

    /*
     * If div by zero, set temp to 1, else source2.
     * This avoids a possible host trap, but produces an incorrect result.
     */
    tcg_gen_movcond_tl(TCG_COND_EQ, temp, source2, zero, one, source2);

    tcg_gen_remu_tl(temp, source1, temp);

    /* If div by zero, the required result is the original dividend. */
    tcg_gen_movcond_tl(TCG_COND_EQ, ret, source2, zero, source1, temp);

    tcg_temp_free(temp);
}

static bool trans_remu(DisasContext *ctx, arg_remu *a)
{
    REQUIRE_EXT(ctx, RVM);
    return gen_arith(ctx, a, EXT_ZERO, gen_remu);
}

static bool trans_mulw(DisasContext *ctx, arg_mulw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVM);
    ctx->ol = MXL_RV32;
    return gen_arith(ctx, a, EXT_NONE, tcg_gen_mul_tl);
}

static bool trans_divw(DisasContext *ctx, arg_divw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVM);
    ctx->ol = MXL_RV32;
    return gen_arith(ctx, a, EXT_SIGN, gen_div);
}

static bool trans_divuw(DisasContext *ctx, arg_divuw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVM);
    ctx->ol = MXL_RV32;
    return gen_arith(ctx, a, EXT_ZERO, gen_divu);
}

static bool trans_remw(DisasContext *ctx, arg_remw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVM);
    ctx->ol = MXL_RV32;
    return gen_arith(ctx, a, EXT_SIGN, gen_rem);
}

static bool trans_remuw(DisasContext *ctx, arg_remuw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVM);
    ctx->ol = MXL_RV32;
    return gen_arith(ctx, a, EXT_ZERO, gen_remu);
}
