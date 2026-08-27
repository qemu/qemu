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

    translator_io_start(&ctx->base);
    gen_update_branch_history(ctx, ctx->cia, NULL, BHRB_TYPE_NORECORD);
    gen_helper_rfebb(tcg_env, cpu_gpr[arg->s]);

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

static bool trans_B(DisasContext *ctx, arg_I_b *a)
{
    target_ulong target, li;

    /* sign extend LI */
    li = (a->li ^ 0x02000000) - 0x02000000;

    if (likely(a->aa == 0)) {
        target = ctx->cia + li;
    } else {
        target = li;
    }
    if (a->lk) {
        gen_setlr(ctx, ctx->base.pc_next);
        gen_update_branch_history(ctx, ctx->cia, NULL, BHRB_TYPE_CALL);
    } else {
        gen_update_branch_history(ctx, ctx->cia, NULL, BHRB_TYPE_OTHER);
    }
    gen_goto_tb(ctx, 0, target);
    ctx->base.is_jmp = DISAS_NORETURN;

    return true;
}
