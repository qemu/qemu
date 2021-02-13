/*
 * Toshiba TX79-specific instructions translation routines
 *
 *  Copyright (c) 2018 Fredrik Noring
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "tcg/tcg-op.h"
#include "exec/helper-gen.h"
#include "translate.h"

/* Include the auto-generated decoder.  */
#include "decode-tx79.c.inc"

bool decode_ext_tx79(DisasContext *ctx, uint32_t insn)
{
    if (TARGET_LONG_BITS == 64 && decode_tx79(ctx, insn)) {
        return true;
    }
    return false;
}

static bool trans_MFHI1(DisasContext *ctx, arg_rtype *a)
{
    gen_store_gpr(cpu_HI[1], a->rd);

    return true;
}

static bool trans_MFLO1(DisasContext *ctx, arg_rtype *a)
{
    gen_store_gpr(cpu_LO[1], a->rd);

    return true;
}

static bool trans_MTHI1(DisasContext *ctx, arg_rtype *a)
{
    gen_load_gpr(cpu_HI[1], a->rs);

    return true;
}

static bool trans_MTLO1(DisasContext *ctx, arg_rtype *a)
{
    gen_load_gpr(cpu_LO[1], a->rs);

    return true;
}

/* Parallel Copy Halfword */
static bool trans_PCPYH(DisasContext *s, arg_rtype *a)
{
    if (a->rd == 0) {
        /* nop */
        return true;
    }

    if (a->rt == 0) {
        tcg_gen_movi_i64(cpu_gpr[a->rd], 0);
        tcg_gen_movi_i64(cpu_gpr_hi[a->rd], 0);
        return true;
    }

    tcg_gen_deposit_i64(cpu_gpr[a->rd], cpu_gpr[a->rt], cpu_gpr[a->rt], 16, 16);
    tcg_gen_deposit_i64(cpu_gpr[a->rd], cpu_gpr[a->rd], cpu_gpr[a->rd], 32, 32);
    tcg_gen_deposit_i64(cpu_gpr_hi[a->rd], cpu_gpr_hi[a->rt], cpu_gpr_hi[a->rt], 16, 16);
    tcg_gen_deposit_i64(cpu_gpr_hi[a->rd], cpu_gpr_hi[a->rd], cpu_gpr_hi[a->rd], 32, 32);

    return true;
}
