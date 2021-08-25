/*
 *  MIPS emulation for QEMU - Release 6 translation routines
 *
 *  Copyright (c) 2020 Philippe Mathieu-Daudé
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * This code is licensed under the LGPL v2.1 or later.
 */

#include "qemu/osdep.h"
#include "tcg/tcg-op.h"
#include "exec/helper-gen.h"
#include "translate.h"

/* Include the auto-generated decoders.  */
#include "decode-rel6.c.inc"

bool trans_REMOVED(DisasContext *ctx, arg_REMOVED *a)
{
    gen_reserved_instruction(ctx);

    return true;
}

static bool trans_LSA(DisasContext *ctx, arg_r *a)
{
    return gen_lsa(ctx, a->rd, a->rt, a->rs, a->sa);
}

static bool trans_DLSA(DisasContext *ctx, arg_r *a)
{
    if (TARGET_LONG_BITS != 64) {
        return false;
    }
    return gen_dlsa(ctx, a->rd, a->rt, a->rs, a->sa);
}
