/*
 * MIPS emulation for QEMU - microMIPS translation routines
 *
 * Copyright (c) 2021 Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "translate.h"

static int plus_1(DisasContext *ctx, int x)
{
    return x + 1;
}

static int li16_imm7(DisasContext *ctx, int x)
{
    return x == 0x7f ? -1 : x;
}

/* Include the auto-generated decoders.  */
#include "decode-micromips16.c.inc"
#include "decode-micromips32.c.inc"
#include "decode-micromips32_r6.c.inc"

static bool trans_LSA(DisasContext *ctx, arg_r *a)
{
    return gen_lsa(ctx, a->rd, a->rt, a->rs, a->sa);
}

static bool trans_LI(DisasContext *ctx, arg_rd_imm *a)
{
    gen_li(ctx, a->rd, a->imm);

    return true;
}
