/*
 * VR5432 extensions translation routines
 *
 * Reference: VR5432 Microprocessor User’s Manual
 *            (Document Number U13751EU5V0UM00)
 *
 *  Copyright (c) 2021 Philippe Mathieu-Daudé
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "tcg/tcg-op.h"
#include "exec/helper-gen.h"
#include "translate.h"
#include "internal.h"

/* Include the auto-generated decoder. */
#include "decode-vr54xx.c.inc"

/*
 * Integer Multiply-Accumulate Instructions
 *
 * MACC         Multiply, accumulate, and move LO
 * MACCHI       Multiply, accumulate, and move HI
 * MACCHIU      Unsigned multiply, accumulate, and move HI
 * MACCU        Unsigned multiply, accumulate, and move LO
 * MSAC         Multiply, negate, accumulate, and move LO
 * MSACHI       Multiply, negate, accumulate, and move HI
 * MSACHIU      Unsigned multiply, negate, accumulate, and move HI
 * MSACU        Unsigned multiply, negate, accumulate, and move LO
 * MULHI        Multiply and move HI
 * MULHIU       Unsigned multiply and move HI
 * MULS         Multiply, negate, and move LO
 * MULSHI       Multiply, negate, and move HI
 * MULSHIU      Unsigned multiply, negate, and move HI
 * MULSU        Unsigned multiply, negate, and move LO
 */

static bool trans_mult_acc(DisasContext *ctx, arg_r *a,
                           void (*gen_helper_mult_acc)(TCGv, TCGv_ptr, TCGv, TCGv))
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();

    gen_load_gpr(t0, a->rs);
    gen_load_gpr(t1, a->rt);

    gen_helper_mult_acc(t0, cpu_env, t0, t1);

    gen_store_gpr(t0, a->rd);
    return true;
}

TRANS(MACC,     trans_mult_acc, gen_helper_macc);
TRANS(MACCHI,   trans_mult_acc, gen_helper_macchi);
TRANS(MACCHIU,  trans_mult_acc, gen_helper_macchiu);
TRANS(MACCU,    trans_mult_acc, gen_helper_maccu);
TRANS(MSAC,     trans_mult_acc, gen_helper_msac);
TRANS(MSACHI,   trans_mult_acc, gen_helper_msachi);
TRANS(MSACHIU,  trans_mult_acc, gen_helper_msachiu);
TRANS(MSACU,    trans_mult_acc, gen_helper_msacu);
TRANS(MULHI,    trans_mult_acc, gen_helper_mulhi);
TRANS(MULHIU,   trans_mult_acc, gen_helper_mulhiu);
TRANS(MULS,     trans_mult_acc, gen_helper_muls);
TRANS(MULSHI,   trans_mult_acc, gen_helper_mulshi);
TRANS(MULSHIU,  trans_mult_acc, gen_helper_mulshiu);
TRANS(MULSU,    trans_mult_acc, gen_helper_mulsu);
