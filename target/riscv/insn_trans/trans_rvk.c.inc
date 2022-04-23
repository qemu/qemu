/*
 * RISC-V translation routines for the Zk[nd,ne,nh,sed,sh] Standard Extension.
 *
 * Copyright (c) 2021 Ruibo Lu, luruibo2000@163.com
 * Copyright (c) 2021 Zewen Ye, lustrew@foxmail.com
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

#define REQUIRE_ZKND(ctx) do {                  \
    if (!ctx->cfg_ptr->ext_zknd) {              \
        return false;                           \
    }                                           \
} while (0)

#define REQUIRE_ZKNE(ctx) do {                  \
    if (!ctx->cfg_ptr->ext_zkne) {              \
        return false;                           \
    }                                           \
} while (0)

static bool gen_aes32_sm4(DisasContext *ctx, arg_k_aes *a,
                          void (*func)(TCGv, TCGv, TCGv, TCGv))
{
    TCGv shamt = tcg_constant_tl(a->shamt);
    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);

    func(dest, src1, src2, shamt);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static bool trans_aes32esmi(DisasContext *ctx, arg_aes32esmi *a)
{
    REQUIRE_32BIT(ctx);
    REQUIRE_ZKNE(ctx);
    return gen_aes32_sm4(ctx, a, gen_helper_aes32esmi);
}

static bool trans_aes32esi(DisasContext *ctx, arg_aes32esi *a)
{
    REQUIRE_32BIT(ctx);
    REQUIRE_ZKNE(ctx);
    return gen_aes32_sm4(ctx, a, gen_helper_aes32esi);
}

static bool trans_aes32dsmi(DisasContext *ctx, arg_aes32dsmi *a)
{
    REQUIRE_32BIT(ctx);
    REQUIRE_ZKND(ctx);
    return gen_aes32_sm4(ctx, a, gen_helper_aes32dsmi);
}

static bool trans_aes32dsi(DisasContext *ctx, arg_aes32dsi *a)
{
    REQUIRE_32BIT(ctx);
    REQUIRE_ZKND(ctx);
    return gen_aes32_sm4(ctx, a, gen_helper_aes32dsi);
}

static bool trans_aes64es(DisasContext *ctx, arg_aes64es *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_ZKNE(ctx);
    return gen_arith(ctx, a, EXT_NONE, gen_helper_aes64es, NULL);
}

static bool trans_aes64esm(DisasContext *ctx, arg_aes64esm *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_ZKNE(ctx);
    return gen_arith(ctx, a, EXT_NONE, gen_helper_aes64esm, NULL);
}

static bool trans_aes64ds(DisasContext *ctx, arg_aes64ds *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_ZKND(ctx);
    return gen_arith(ctx, a, EXT_NONE, gen_helper_aes64ds, NULL);
}

static bool trans_aes64dsm(DisasContext *ctx, arg_aes64dsm *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_ZKND(ctx);
    return gen_arith(ctx, a, EXT_NONE, gen_helper_aes64dsm, NULL);
}

static bool trans_aes64ks2(DisasContext *ctx, arg_aes64ks2 *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EITHER_EXT(ctx, zknd, zkne);
    return gen_arith(ctx, a, EXT_NONE, gen_helper_aes64ks2, NULL);
}

static bool trans_aes64ks1i(DisasContext *ctx, arg_aes64ks1i *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EITHER_EXT(ctx, zknd, zkne);

    if (a->imm > 0xA) {
        return false;
    }

    return gen_arith_imm_tl(ctx, a, EXT_NONE, gen_helper_aes64ks1i, NULL);
}

static bool trans_aes64im(DisasContext *ctx, arg_aes64im *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_ZKND(ctx);
    return gen_unary(ctx, a, EXT_NONE, gen_helper_aes64im);
}
