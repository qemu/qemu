/*
 * MIPS emulation for QEMU - nanoMIPS translation routines
 *
 * Copyright (c) 2021 Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "translate.h"

static int gpr3(DisasContext *ctx, int x)
{
    static const int map[8] = { 16, 17, 18, 19, 4, 5, 6, 7 };

    return map[x];
}

static int s_eu(DisasContext *ctx, int x)
{
    return x == 0x7f ? -1 : x;
}

/* Include the auto-generated decoders.  */
#include "decode-nanomips16.c.inc"
#include "decode-nanomips32.c.inc"
#include "decode-nanomips48.c.inc"

static bool trans_LSA(DisasContext *ctx, arg_r *a)
{
    return gen_lsa(ctx, a->rd, a->rt, a->rs, a->sa);
}

static bool trans_LI(DisasContext *ctx, arg_rd_imm *a)
{
    gen_li(ctx, a->rd, a->imm);

    return true;
}

static bool trans_LI_nnms(DisasContext *ctx, arg_rd_imm *a)
{
    check_nms(ctx);

    return trans_LI(ctx, a);
}
