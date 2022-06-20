/*
 * Octeon-specific instructions translation routines
 *
 *  Copyright (c) 2022 Pavel Dovgalyuk
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "tcg/tcg-op.h"
#include "tcg/tcg-op-gvec.h"
#include "exec/helper-gen.h"
#include "translate.h"

/* Include the auto-generated decoder.  */
#include "decode-octeon.c.inc"

static bool trans_BBIT(DisasContext *ctx, arg_BBIT *a)
{
    TCGv p;

    if (ctx->hflags & MIPS_HFLAG_BMASK) {
        LOG_DISAS("Branch in delay / forbidden slot at PC 0x"
                  TARGET_FMT_lx "\n", ctx->base.pc_next);
        generate_exception_end(ctx, EXCP_RI);
        return true;
    }

    /* Load needed operands */
    TCGv t0 = tcg_temp_new();
    gen_load_gpr(t0, a->rs);

    p = tcg_constant_tl(1ULL << a->p);
    if (a->set) {
        tcg_gen_and_tl(bcond, p, t0);
    } else {
        tcg_gen_andc_tl(bcond, p, t0);
    }

    ctx->hflags |= MIPS_HFLAG_BC;
    ctx->btarget = ctx->base.pc_next + 4 + a->offset * 4;
    ctx->hflags |= MIPS_HFLAG_BDS32;

    tcg_temp_free(t0);
    return true;
}
