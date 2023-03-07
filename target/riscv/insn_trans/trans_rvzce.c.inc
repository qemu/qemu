/*
 * RISC-V translation routines for the Zcb Standard Extension.
 *
 * Copyright (c) 2021-2022 PLCT Lab
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

#define REQUIRE_ZCB(ctx) do {   \
    if (!ctx->cfg_ptr->ext_zcb) \
        return false;           \
} while (0)

static bool trans_c_zext_b(DisasContext *ctx, arg_c_zext_b *a)
{
    REQUIRE_ZCB(ctx);
    return gen_unary(ctx, a, EXT_NONE, tcg_gen_ext8u_tl);
}

static bool trans_c_zext_h(DisasContext *ctx, arg_c_zext_h *a)
{
    REQUIRE_ZCB(ctx);
    REQUIRE_ZBB(ctx);
    return gen_unary(ctx, a, EXT_NONE, tcg_gen_ext16u_tl);
}

static bool trans_c_sext_b(DisasContext *ctx, arg_c_sext_b *a)
{
    REQUIRE_ZCB(ctx);
    REQUIRE_ZBB(ctx);
    return gen_unary(ctx, a, EXT_NONE, tcg_gen_ext8s_tl);
}

static bool trans_c_sext_h(DisasContext *ctx, arg_c_sext_h *a)
{
    REQUIRE_ZCB(ctx);
    REQUIRE_ZBB(ctx);
    return gen_unary(ctx, a, EXT_NONE, tcg_gen_ext16s_tl);
}

static bool trans_c_zext_w(DisasContext *ctx, arg_c_zext_w *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_ZCB(ctx);
    REQUIRE_ZBA(ctx);
    return gen_unary(ctx, a, EXT_NONE, tcg_gen_ext32u_tl);
}

static bool trans_c_not(DisasContext *ctx, arg_c_not *a)
{
    REQUIRE_ZCB(ctx);
    return gen_unary(ctx, a, EXT_NONE, tcg_gen_not_tl);
}

static bool trans_c_mul(DisasContext *ctx, arg_c_mul *a)
{
    REQUIRE_ZCB(ctx);
    REQUIRE_M_OR_ZMMUL(ctx);
    return gen_arith(ctx, a, EXT_NONE, tcg_gen_mul_tl, NULL);
}

static bool trans_c_lbu(DisasContext *ctx, arg_c_lbu *a)
{
    REQUIRE_ZCB(ctx);
    return gen_load(ctx, a, MO_UB);
}

static bool trans_c_lhu(DisasContext *ctx, arg_c_lhu *a)
{
    REQUIRE_ZCB(ctx);
    return gen_load(ctx, a, MO_UW);
}

static bool trans_c_lh(DisasContext *ctx, arg_c_lh *a)
{
    REQUIRE_ZCB(ctx);
    return gen_load(ctx, a, MO_SW);
}

static bool trans_c_sb(DisasContext *ctx, arg_c_sb *a)
{
    REQUIRE_ZCB(ctx);
    return gen_store(ctx, a, MO_UB);
}

static bool trans_c_sh(DisasContext *ctx, arg_c_sh *a)
{
    REQUIRE_ZCB(ctx);
    return gen_store(ctx, a, MO_UW);
}
