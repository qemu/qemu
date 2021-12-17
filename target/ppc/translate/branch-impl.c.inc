/*
 * Power ISA decode for branch instructions
 *
 *  Copyright IBM Corp. 2021
 *
 * Authors:
 *  Daniel Henrique Barboza      <danielhb413@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#if defined(TARGET_PPC64) && !defined(CONFIG_USER_ONLY)

static bool trans_RFEBB(DisasContext *ctx, arg_XL_s *arg)
{
    REQUIRE_INSNS_FLAGS2(ctx, ISA207S);

    gen_icount_io_start(ctx);
    gen_update_cfar(ctx, ctx->cia);
    gen_helper_rfebb(cpu_env, cpu_gpr[arg->s]);

    ctx->base.is_jmp = DISAS_CHAIN;

    return true;
}
#else
static bool trans_RFEBB(DisasContext *ctx, arg_XL_s *arg)
{
    gen_invalid(ctx);
    return true;
}
#endif
