/*
 * RISC-V translation routines for the Control-Flow Integrity Extension
 *
 * Copyright (c) 2024 Rivos Inc.
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

#define REQUIRE_ZICFISS(ctx) do {        \
    if (!ctx->cfg_ptr->ext_zicfiss) {    \
        return false;                    \
    }                                    \
} while (0)

static bool trans_sspopchk(DisasContext *ctx, arg_sspopchk *a)
{
    if (!ctx->bcfi_enabled) {
        return false;
    }

    TCGv addr = tcg_temp_new();
    TCGLabel *skip = gen_new_label();
    uint32_t tmp = (get_xl(ctx) == MXL_RV64) ? 8 : 4;
    TCGv data = tcg_temp_new();
    tcg_gen_ld_tl(addr, tcg_env, offsetof(CPURISCVState, ssp));
    decode_save_opc(ctx, RISCV_UW2_ALWAYS_STORE_AMO);
    tcg_gen_qemu_ld_tl(data, addr, SS_MMU_INDEX(ctx),
                       mxl_memop(ctx) | MO_ALIGN);
    TCGv rs1 = get_gpr(ctx, a->rs1, EXT_NONE);
    tcg_gen_brcond_tl(TCG_COND_EQ, data, rs1, skip);
    tcg_gen_st_tl(tcg_constant_tl(RISCV_EXCP_SW_CHECK_BCFI_TVAL),
                  tcg_env, offsetof(CPURISCVState, sw_check_code));
    gen_helper_raise_exception(tcg_env,
                  tcg_constant_i32(RISCV_EXCP_SW_CHECK));
    gen_set_label(skip);
    tcg_gen_addi_tl(addr, addr, tmp);
    tcg_gen_st_tl(addr, tcg_env, offsetof(CPURISCVState, ssp));

    return true;
}

static bool trans_sspush(DisasContext *ctx, arg_sspush *a)
{
    if (!ctx->bcfi_enabled) {
        return false;
    }

    TCGv addr = tcg_temp_new();
    int tmp = (get_xl(ctx) == MXL_RV64) ? -8 : -4;
    TCGv data = get_gpr(ctx, a->rs2, EXT_NONE);
    decode_save_opc(ctx, RISCV_UW2_ALWAYS_STORE_AMO);
    tcg_gen_ld_tl(addr, tcg_env, offsetof(CPURISCVState, ssp));
    tcg_gen_addi_tl(addr, addr, tmp);
    tcg_gen_qemu_st_tl(data, addr, SS_MMU_INDEX(ctx),
                       mxl_memop(ctx) | MO_ALIGN);
    tcg_gen_st_tl(addr, tcg_env, offsetof(CPURISCVState, ssp));

    return true;
}

static bool trans_ssrdp(DisasContext *ctx, arg_ssrdp *a)
{
    if (!ctx->bcfi_enabled || a->rd == 0) {
        return false;
    }

    TCGv dest = dest_gpr(ctx, a->rd);
    tcg_gen_ld_tl(dest, tcg_env, offsetof(CPURISCVState, ssp));
    gen_set_gpr(ctx, a->rd, dest);

    return true;
}

static bool trans_ssamoswap_w(DisasContext *ctx, arg_amoswap_w *a)
{
    REQUIRE_A_OR_ZAAMO(ctx);
    REQUIRE_ZICFISS(ctx);
    if (ctx->priv == PRV_M) {
        generate_exception(ctx, RISCV_EXCP_STORE_AMO_ACCESS_FAULT);
    }

    if (!ctx->bcfi_enabled) {
        return false;
    }

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1, src2 = get_gpr(ctx, a->rs2, EXT_NONE);

    decode_save_opc(ctx, RISCV_UW2_ALWAYS_STORE_AMO);
    src1 = get_address(ctx, a->rs1, 0);

    tcg_gen_atomic_xchg_tl(dest, src1, src2, SS_MMU_INDEX(ctx),
                           (MO_ALIGN | MO_TESL));
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static bool trans_ssamoswap_d(DisasContext *ctx, arg_amoswap_w *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_A_OR_ZAAMO(ctx);
    REQUIRE_ZICFISS(ctx);
    if (ctx->priv == PRV_M) {
        generate_exception(ctx, RISCV_EXCP_STORE_AMO_ACCESS_FAULT);
    }

    if (!ctx->bcfi_enabled) {
        return false;
    }

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1, src2 = get_gpr(ctx, a->rs2, EXT_NONE);

    decode_save_opc(ctx, RISCV_UW2_ALWAYS_STORE_AMO);
    src1 = get_address(ctx, a->rs1, 0);

    tcg_gen_atomic_xchg_tl(dest, src1, src2, SS_MMU_INDEX(ctx),
                           (MO_ALIGN | MO_TESQ));
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
