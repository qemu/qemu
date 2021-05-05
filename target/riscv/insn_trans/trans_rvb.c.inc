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
