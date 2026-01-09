/*
 * RISC-V translation routines for the ZALASR (Load-Aquire and Store-Release)
 * Extension.
 *
 * Copyright (c) 2025 Roan Richmond, roan.richmond@codethink.co.uk
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The documentation of the ISA extension can be found here:
 *   https://github.com/riscv/riscv-zalasr/tree/v0.9
 */

#define REQUIRE_ZALASR(ctx) do {     \
    if (!ctx->cfg_ptr->ext_zalasr) { \
        return false;                \
    }                                \
} while (0)

static bool gen_load_acquire(DisasContext *ctx, arg_lb_aqrl *a, MemOp memop)
{
    decode_save_opc(ctx, 0);

    TCGv addr = get_address(ctx, a->rs1, 0);
    TCGv dest = get_gpr(ctx, a->rd, EXT_NONE);
    TCGBar bar = (a->rl) ? TCG_BAR_STRL : 0;

    /* Check that AQ is set, as this is mandatory */
    if (!a->aq) {
        return false;
    }

    memop |= (ctx->cfg_ptr->ext_zama16b) ? MO_ATOM_WITHIN16 : 0;

    tcg_gen_qemu_ld_tl(dest, addr, ctx->mem_idx, memop);
    gen_set_gpr(ctx, a->rd, dest);

    /* Add a memory barrier implied by AQ (mandatory) and RL (optional) */
    tcg_gen_mb(TCG_MO_ALL | TCG_BAR_LDAQ | bar);

    return true;
}

static bool trans_lb_aqrl(DisasContext *ctx, arg_lb_aqrl *a)
{
    REQUIRE_ZALASR(ctx);
    return gen_load_acquire(ctx, a, (MO_ALIGN | MO_SB));
}

static bool trans_lh_aqrl(DisasContext *ctx, arg_lh_aqrl *a)
{
    REQUIRE_ZALASR(ctx);
    return gen_load_acquire(ctx, a, (MO_ALIGN | MO_TESW));
}

static bool trans_lw_aqrl(DisasContext *ctx, arg_lw_aqrl *a)
{
    REQUIRE_ZALASR(ctx);
    return gen_load_acquire(ctx, a, (MO_ALIGN | MO_TESL));
}

static bool trans_ld_aqrl(DisasContext *ctx, arg_ld_aqrl *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_ZALASR(ctx);
    return gen_load_acquire(ctx, a, (MO_ALIGN | MO_TEUQ));
}

static bool gen_store_release(DisasContext *ctx, arg_sb_aqrl *a, MemOp memop)
{
    decode_save_opc(ctx, 0);

    TCGv addr = get_address(ctx, a->rs1, 0);
    TCGv data = get_gpr(ctx, a->rs2, EXT_NONE);
    TCGBar bar = (a->aq) ? TCG_BAR_LDAQ : 0;

    /* Check that RL is set, as this is mandatory */
    if (!a->rl) {
        return false;
    }

    memop |= (ctx->cfg_ptr->ext_zama16b) ? MO_ATOM_WITHIN16 : 0;

    /* Add a memory barrier implied by RL (mandatory) and AQ (optional) */
    tcg_gen_mb(TCG_MO_ALL | TCG_BAR_STRL | bar);

    tcg_gen_qemu_st_tl(data, addr, ctx->mem_idx, memop);
    return true;
}

static bool trans_sb_aqrl(DisasContext *ctx, arg_sb_aqrl *a)
{
    REQUIRE_ZALASR(ctx);
    return gen_store_release(ctx, a, (MO_ALIGN | MO_SB));
}

static bool trans_sh_aqrl(DisasContext *ctx, arg_sh_aqrl *a)
{
    REQUIRE_ZALASR(ctx);
    return gen_store_release(ctx, a, (MO_ALIGN | MO_TESW));
}

static bool trans_sw_aqrl(DisasContext *ctx, arg_sw_aqrl *a)
{
    REQUIRE_ZALASR(ctx);
    return gen_store_release(ctx, a, (MO_ALIGN | MO_TESL));
}

static bool trans_sd_aqrl(DisasContext *ctx, arg_sd_aqrl *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_ZALASR(ctx);
    return gen_store_release(ctx, a, (MO_ALIGN | MO_TEUQ));
}
