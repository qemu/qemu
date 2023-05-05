/*
 * RISC-V translation routines for the Zicond Standard Extension.
 *
 * Copyright (c) 2020-2023 PLCT Lab
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

#define REQUIRE_ZICOND(ctx) do {          \
    if (!ctx->cfg_ptr->ext_zicond) {      \
        return false;                     \
    }                                     \
} while (0)

/* Emits "$rd = ($rs2 <cond> $zero) ? $zero : $rs1" */
static void gen_czero(TCGv dest, TCGv src1, TCGv src2, TCGCond cond)
{
    TCGv zero = tcg_constant_tl(0);
    tcg_gen_movcond_tl(cond, dest, src2, zero, zero, src1);
}

static void gen_czero_eqz(TCGv dest, TCGv src1, TCGv src2)
{
    gen_czero(dest, src1, src2, TCG_COND_EQ);
}

static void gen_czero_nez(TCGv dest, TCGv src1, TCGv src2)
{
    gen_czero(dest, src1, src2, TCG_COND_NE);
}

static bool trans_czero_eqz(DisasContext *ctx, arg_r *a)
{
    REQUIRE_ZICOND(ctx);

    return gen_logic(ctx, a, gen_czero_eqz);
}

static bool trans_czero_nez(DisasContext *ctx, arg_r *a)
{
    REQUIRE_ZICOND(ctx);

    return gen_logic(ctx, a, gen_czero_nez);
}
