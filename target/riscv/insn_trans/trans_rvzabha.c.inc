/*
 * RISC-V translation routines for the Zabha Standard Extension.
 *
 * Copyright (c) 2024 Alibaba Group
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

#define REQUIRE_ZABHA(ctx) do {           \
    if (!ctx->cfg_ptr->ext_zabha) {       \
        return false;                     \
    }                                     \
} while (0)

static bool trans_amoswap_b(DisasContext *ctx, arg_amoswap_b *a)
{
    REQUIRE_ZABHA(ctx);
    return gen_amo(ctx, a, &tcg_gen_atomic_xchg_tl, MO_SB);
}

static bool trans_amoadd_b(DisasContext *ctx, arg_amoadd_b *a)
{
    REQUIRE_ZABHA(ctx);
    return gen_amo(ctx, a, &tcg_gen_atomic_fetch_add_tl, MO_SB);
}

static bool trans_amoxor_b(DisasContext *ctx, arg_amoxor_b *a)
{
    REQUIRE_ZABHA(ctx);
    return gen_amo(ctx, a, &tcg_gen_atomic_fetch_xor_tl, MO_SB);
}

static bool trans_amoand_b(DisasContext *ctx, arg_amoand_b *a)
{
    REQUIRE_ZABHA(ctx);
    return gen_amo(ctx, a, &tcg_gen_atomic_fetch_and_tl, MO_SB);
}

static bool trans_amoor_b(DisasContext *ctx, arg_amoor_b *a)
{
    REQUIRE_ZABHA(ctx);
    return gen_amo(ctx, a, &tcg_gen_atomic_fetch_or_tl, MO_SB);
}

static bool trans_amomin_b(DisasContext *ctx, arg_amomin_b *a)
{
    REQUIRE_ZABHA(ctx);
    return gen_amo(ctx, a, &tcg_gen_atomic_fetch_smin_tl, MO_SB);
}

static bool trans_amomax_b(DisasContext *ctx, arg_amomax_b *a)
{
    REQUIRE_ZABHA(ctx);
    return gen_amo(ctx, a, &tcg_gen_atomic_fetch_smax_tl, MO_SB);
}

static bool trans_amominu_b(DisasContext *ctx, arg_amominu_b *a)
{
    REQUIRE_ZABHA(ctx);
    return gen_amo(ctx, a, &tcg_gen_atomic_fetch_umin_tl, MO_SB);
}

static bool trans_amomaxu_b(DisasContext *ctx, arg_amomaxu_b *a)
{
    REQUIRE_ZABHA(ctx);
    return gen_amo(ctx, a, &tcg_gen_atomic_fetch_umax_tl, MO_SB);
}

static bool trans_amoswap_h(DisasContext *ctx, arg_amoswap_h *a)
{
    REQUIRE_ZABHA(ctx);
    return gen_amo(ctx, a, &tcg_gen_atomic_xchg_tl, MO_TESW);
}

static bool trans_amoadd_h(DisasContext *ctx, arg_amoadd_h *a)
{
    REQUIRE_ZABHA(ctx);
    return gen_amo(ctx, a, &tcg_gen_atomic_fetch_add_tl, MO_TESW);
}

static bool trans_amoxor_h(DisasContext *ctx, arg_amoxor_h *a)
{
    REQUIRE_ZABHA(ctx);
    return gen_amo(ctx, a, &tcg_gen_atomic_fetch_xor_tl, MO_TESW);
}

static bool trans_amoand_h(DisasContext *ctx, arg_amoand_h *a)
{
    REQUIRE_ZABHA(ctx);
    return gen_amo(ctx, a, &tcg_gen_atomic_fetch_and_tl, MO_TESW);
}

static bool trans_amoor_h(DisasContext *ctx, arg_amoor_h *a)
{
    REQUIRE_ZABHA(ctx);
    return gen_amo(ctx, a, &tcg_gen_atomic_fetch_or_tl, MO_TESW);
}

static bool trans_amomin_h(DisasContext *ctx, arg_amomin_h *a)
{
    REQUIRE_ZABHA(ctx);
    return gen_amo(ctx, a, &tcg_gen_atomic_fetch_smin_tl, MO_TESW);
}

static bool trans_amomax_h(DisasContext *ctx, arg_amomax_h *a)
{
    REQUIRE_ZABHA(ctx);
    return gen_amo(ctx, a, &tcg_gen_atomic_fetch_smax_tl, MO_TESW);
}

static bool trans_amominu_h(DisasContext *ctx, arg_amominu_h *a)
{
    REQUIRE_ZABHA(ctx);
    return gen_amo(ctx, a, &tcg_gen_atomic_fetch_umin_tl, MO_TESW);
}

static bool trans_amomaxu_h(DisasContext *ctx, arg_amomaxu_h *a)
{
    REQUIRE_ZABHA(ctx);
    return gen_amo(ctx, a, &tcg_gen_atomic_fetch_umax_tl, MO_TESW);
}

static bool trans_amocas_b(DisasContext *ctx, arg_amocas_b *a)
{
    REQUIRE_ZACAS(ctx);
    REQUIRE_ZABHA(ctx);
    return gen_cmpxchg(ctx, a, MO_SB);
}

static bool trans_amocas_h(DisasContext *ctx, arg_amocas_h *a)
{
    REQUIRE_ZACAS(ctx);
    REQUIRE_ZABHA(ctx);
    return gen_cmpxchg(ctx, a, MO_ALIGN | MO_TESW);
}
