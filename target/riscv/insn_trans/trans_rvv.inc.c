/*
 * RISC-V translation routines for the RVV Standard Extension.
 *
 * Copyright (c) 2020 T-Head Semiconductor Co., Ltd. All rights reserved.
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

static bool trans_vsetvl(DisasContext *ctx, arg_vsetvl *a)
{
    TCGv s1, s2, dst;

    if (!has_ext(ctx, RVV)) {
        return false;
    }

    s2 = tcg_temp_new();
    dst = tcg_temp_new();

    /* Using x0 as the rs1 register specifier, encodes an infinite AVL */
    if (a->rs1 == 0) {
        /* As the mask is at least one bit, RV_VLEN_MAX is >= VLMAX */
        s1 = tcg_const_tl(RV_VLEN_MAX);
    } else {
        s1 = tcg_temp_new();
        gen_get_gpr(s1, a->rs1);
    }
    gen_get_gpr(s2, a->rs2);
    gen_helper_vsetvl(dst, cpu_env, s1, s2);
    gen_set_gpr(a->rd, dst);
    tcg_gen_movi_tl(cpu_pc, ctx->pc_succ_insn);
    lookup_and_goto_ptr(ctx);
    ctx->base.is_jmp = DISAS_NORETURN;

    tcg_temp_free(s1);
    tcg_temp_free(s2);
    tcg_temp_free(dst);
    return true;
}

static bool trans_vsetvli(DisasContext *ctx, arg_vsetvli *a)
{
    TCGv s1, s2, dst;

    if (!has_ext(ctx, RVV)) {
        return false;
    }

    s2 = tcg_const_tl(a->zimm);
    dst = tcg_temp_new();

    /* Using x0 as the rs1 register specifier, encodes an infinite AVL */
    if (a->rs1 == 0) {
        /* As the mask is at least one bit, RV_VLEN_MAX is >= VLMAX */
        s1 = tcg_const_tl(RV_VLEN_MAX);
    } else {
        s1 = tcg_temp_new();
        gen_get_gpr(s1, a->rs1);
    }
    gen_helper_vsetvl(dst, cpu_env, s1, s2);
    gen_set_gpr(a->rd, dst);
    gen_goto_tb(ctx, 0, ctx->pc_succ_insn);
    ctx->base.is_jmp = DISAS_NORETURN;

    tcg_temp_free(s1);
    tcg_temp_free(s2);
    tcg_temp_free(dst);
    return true;
}
