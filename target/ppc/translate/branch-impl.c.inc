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

#define BCOND_IM  0
#define BCOND_LR  1
#define BCOND_CTR 2
#define BCOND_TAR 3

static bool bcond_helper(DisasContext *ctx, int type, uint32_t bo, int bi,
                         int bd, int bh, bool aa, bool lk)
{
    TCGLabel *l1;
    TCGv target;
    target_long bhrb_type = BHRB_TYPE_OTHER;

    if (type == BCOND_IM && bh != -1) {
        /* BCOND_IM should never use bh */
        return false;
    } else if (type != BCOND_IM && (bd != -1 || aa != 0)) {
        /* Other BCOND types should never use bd or aa */
        return false;
    }

    if (type == BCOND_LR || type == BCOND_CTR || type == BCOND_TAR) {
        target = tcg_temp_new();
        if (type == BCOND_CTR) {
            tcg_gen_mov_tl(target, cpu_ctr);
        } else if (type == BCOND_TAR) {
            gen_load_spr(target, SPR_TAR);
        } else {
            tcg_gen_mov_tl(target, cpu_lr);
        }
        if (!lk) {
            bhrb_type |= BHRB_TYPE_INDIRECT;
        }
        bhrb_type |= BHRB_TYPE_XL_FORM;
    } else {
        target = NULL;
    }
    if (lk) {
        gen_setlr(ctx, ctx->base.pc_next);
        bhrb_type |= BHRB_TYPE_CALL;
    }
    l1 = gen_new_label();
    if ((bo & 0x4) == 0) {
        /* Decrement and test CTR */
        TCGv temp = tcg_temp_new();

        if (type == BCOND_CTR) {
            /*
             * All ISAs up to v3 describe this form of bcctr as invalid but
             * some processors, ie. 64-bit server processors compliant with
             * arch 2.x, do implement a "test and decrement" logic instead,
             * as described in their respective UMs. This logic involves CTR
             * to act as both the branch target and a counter, which makes
             * it basically useless and thus never used in real code.
             *
             * This form was hence chosen to trigger extra micro-architectural
             * side-effect on real HW needed for the Spectre v2 workaround.
             * It is up to guests that implement such workaround, ie. linux, to
             * use this form in a way it just triggers the side-effect without
             * doing anything else harmful.
             */
            if (unlikely(!is_book3s_arch2x(ctx))) {
                gen_inval_exception(ctx, POWERPC_EXCP_INVAL_INVAL);
                return true;
            }

            if (NARROW_MODE(ctx)) {
                tcg_gen_ext32u_tl(temp, cpu_ctr);
            } else {
                tcg_gen_mov_tl(temp, cpu_ctr);
            }
            if (bo & 0x2) {
                tcg_gen_brcondi_tl(TCG_COND_NE, temp, 0, l1);
            } else {
                tcg_gen_brcondi_tl(TCG_COND_EQ, temp, 0, l1);
            }
            tcg_gen_subi_tl(cpu_ctr, cpu_ctr, 1);
        } else {
            tcg_gen_subi_tl(cpu_ctr, cpu_ctr, 1);
            if (NARROW_MODE(ctx)) {
                tcg_gen_ext32u_tl(temp, cpu_ctr);
            } else {
                tcg_gen_mov_tl(temp, cpu_ctr);
            }
            if (bo & 0x2) {
                tcg_gen_brcondi_tl(TCG_COND_NE, temp, 0, l1);
            } else {
                tcg_gen_brcondi_tl(TCG_COND_EQ, temp, 0, l1);
            }
        }
        bhrb_type |= BHRB_TYPE_COND;
    }
    if ((bo & 0x10) == 0) {
        /* Test CR */
        uint32_t mask = 0x08 >> (bi & 0x03);
        TCGv_i32 temp = tcg_temp_new_i32();

        if (bo & 0x8) {
            tcg_gen_andi_i32(temp, cpu_crf[bi >> 2], mask);
            tcg_gen_brcondi_i32(TCG_COND_EQ, temp, 0, l1);
        } else {
            tcg_gen_andi_i32(temp, cpu_crf[bi >> 2], mask);
            tcg_gen_brcondi_i32(TCG_COND_NE, temp, 0, l1);
        }
        bhrb_type |= BHRB_TYPE_COND;
    }

    gen_update_branch_history(ctx, ctx->cia, target, bhrb_type);

    if (type == BCOND_IM) {
        target_ulong li = (target_long)((int16_t)(bd));
        if (likely(aa == 0)) {
            gen_goto_tb(ctx, 0, ctx->cia + li);
        } else {
            gen_goto_tb(ctx, 0, li);
        }
    } else {
        if (NARROW_MODE(ctx)) {
            tcg_gen_andi_tl(cpu_nip, target, (uint32_t)~3);
        } else {
            tcg_gen_andi_tl(cpu_nip, target, ~3);
        }
        gen_lookup_and_goto_ptr(ctx);
    }
    if ((bo & 0x14) != 0x14) {
        /* fallthrough case */
        gen_set_label(l1);
        gen_goto_tb(ctx, 1, ctx->base.pc_next);
    }
    ctx->base.is_jmp = DISAS_NORETURN;

    return true;
}

static bool trans_BC(DisasContext *ctx, arg_bcond *a)
{
    /*
     * bh is not used for bc variants hence we pass -1
     */
    return bcond_helper(ctx, BCOND_IM, a->bo, a->bi, a->bd, -1, a->aa, a->lk);
}

/*
 * This helper is shared by bclr, bcctr and bctar.
 */
static bool bclr_helper(DisasContext *ctx, arg_bclr *a, int type)
{
    /*
     * bd and aa is not used for bc variants hence we pass -1 and 0 respectively
     */
    return bcond_helper(ctx, type, a->bo, a->bi, -1, a->bh, 0, a->lk);
}

TRANS(BCLR, bclr_helper, BCOND_LR)
TRANS(BCCTR, bclr_helper, BCOND_CTR)
TRANS_FLAGS2(BCTAR_ISA207, BCTAR, bclr_helper, BCOND_TAR)
