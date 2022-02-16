/*
 * RISC-V translation routines for the XVentanaCondOps extension.
 *
 * Copyright (c) 2021-2022 VRULL GmbH.
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

static bool gen_vt_condmask(DisasContext *ctx, arg_r *a, TCGCond cond)
{
    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);

    tcg_gen_movcond_tl(cond, dest, src2, ctx->zero, src1, ctx->zero);

    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static bool trans_vt_maskc(DisasContext *ctx, arg_r *a)
{
    return gen_vt_condmask(ctx, a, TCG_COND_NE);
}

static bool trans_vt_maskcn(DisasContext *ctx, arg_r *a)
{
    return gen_vt_condmask(ctx, a, TCG_COND_EQ);
}
