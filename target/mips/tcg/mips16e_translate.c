/*
 * MIPS emulation for QEMU - MIPS16e translation routines
 *
 * Copyright (c) 2021 Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "translate.h"

/* Include the auto-generated decoders.  */
#include "decode-mips16e_16.c.inc"
#include "decode-mips16e_32.c.inc"

static bool trans_LI(DisasContext *ctx, arg_rd_imm *a)
{
    gen_li(ctx, a->rd, a->imm);

    return true;
}
