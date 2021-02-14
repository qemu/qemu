/*
 * Toshiba TXx9 instructions translation routines
 *
 *  Copyright (c) 2021 Philippe Mathieu-Daudé
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "translate.h"

bool decode_ext_txx9(DisasContext *ctx, uint32_t insn)
{
#if defined(TARGET_MIPS64)
    if (decode_ext_tx79(ctx, insn)) {
        return true;
    }
#endif
    return false;
}
