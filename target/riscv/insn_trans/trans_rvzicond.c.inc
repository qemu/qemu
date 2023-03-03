/*
 * RISC-V translation routines for the Zicond Standard Extension.
 *
 * Copyright (c) 2020-2023 PLCT Lab
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

#define REQUIRE_ZICOND(ctx) do {          \
    if (!ctx->cfg_ptr->ext_zicond) {      \
        return false;                     \
    }                                     \
} while (0)

static bool trans_czero_eqz(DisasContext *ctx, arg_czero_eqz *a)
{
    REQUIRE_ZICOND(ctx);

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);

    tcg_gen_movcond_tl(TCG_COND_EQ, dest, src2, ctx->zero, ctx->zero, src1);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static bool trans_czero_nez(DisasContext *ctx, arg_czero_nez *a)
{
    REQUIRE_ZICOND(ctx);

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);

    tcg_gen_movcond_tl(TCG_COND_NE, dest, src2, ctx->zero, ctx->zero, src1);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
