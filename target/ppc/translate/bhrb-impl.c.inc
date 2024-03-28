/*
 * Power ISA Decode For BHRB Instructions
 *
 *  Copyright IBM Corp. 2023
 *
 * Authors:
 *  Glenn Miles      <milesg@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#if defined(TARGET_PPC64) && !defined(CONFIG_USER_ONLY)

static bool trans_MFBHRBE(DisasContext *ctx, arg_XFX_bhrbe *arg)
{
    REQUIRE_INSNS_FLAGS2(ctx, ISA207S);
    TCGv_i32 bhrbe = tcg_constant_i32(arg->bhrbe);
    gen_helper_mfbhrbe(cpu_gpr[arg->rt], tcg_env, bhrbe);
    return true;
}

static bool trans_CLRBHRB(DisasContext *ctx, arg_CLRBHRB *arg)
{
    REQUIRE_INSNS_FLAGS2(ctx, ISA207S);
    gen_helper_clrbhrb(tcg_env);
    return true;
}

#else

static bool trans_MFBHRBE(DisasContext *ctx, arg_XFX_bhrbe *arg)
{
    gen_invalid(ctx);
    return true;
}

static bool trans_CLRBHRB(DisasContext *ctx, arg_CLRBHRB *arg)
{
    gen_invalid(ctx);
    return true;
}
#endif
