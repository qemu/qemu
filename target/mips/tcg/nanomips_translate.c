/*
 * MIPS emulation for QEMU - nanoMIPS translation routines
 *
 * Copyright (c) 2021 Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "translate.h"

/* Include the auto-generated decoders.  */
#include "decode-nanomips16.c.inc"
#include "decode-nanomips32.c.inc"
#include "decode-nanomips48.c.inc"

static bool trans_LSA(DisasContext *ctx, arg_r *a)
{
    return gen_lsa(ctx, a->rd, a->rt, a->rs, a->sa);
}
