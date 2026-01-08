/*
 * RISC-V translation routines for the MIPS extensions (xmips*).
 *
 * Copyright (c) 2025 MIPS
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Reference: MIPS P8700 instructions
 *            (https://mips.com/products/hardware/p8700/)
 */

#define REQUIRE_XMIPSCBOP(ctx) do {              \
    if (!ctx->cfg_ptr->ext_xmipscbop) {          \
        return false;                            \
    }                                            \
} while (0)

#define REQUIRE_XMIPSCMOV(ctx) do {              \
    if (!ctx->cfg_ptr->ext_xmipscmov) {          \
        return false;                            \
    }                                            \
} while (0)

/* Conditional move by MIPS. */
static bool trans_ccmov(DisasContext *ctx, arg_ccmov *a)
{
    REQUIRE_XMIPSCMOV(ctx);

    TCGv zero, source1, source2, source3;
    zero = tcg_constant_tl(0);
    source1 = get_gpr(ctx, a->rs1, EXT_NONE);
    source2 = get_gpr(ctx, a->rs2, EXT_NONE);
    source3 = get_gpr(ctx, a->rs3, EXT_NONE);

    tcg_gen_movcond_tl(TCG_COND_NE, cpu_gpr[a->rd],
                       source2, zero, source1, source3);

    return true;
}

/* Move data from memory into cache. */
static bool trans_pref(DisasContext *ctx, arg_pref *a)
{
    REQUIRE_XMIPSCBOP(ctx);

    /* Nop */
    return true;
}
