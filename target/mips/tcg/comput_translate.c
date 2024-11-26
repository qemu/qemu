/*
 * MIPS emulation for QEMU - computational translation routines
 *
 * Copyright (c) 2021 Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "translate.h"

/* logical instructions */

void gen_li(DisasContext *ctx, int rd, int imm)
{
    if (rd == 0) {
        /* Treat as NOP. */
        return;
    }
    tcg_gen_movi_tl(cpu_gpr[rd], imm);
}
