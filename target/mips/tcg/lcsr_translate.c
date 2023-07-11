/*
 * Loongson CSR instructions translation routines
 *
 *  Copyright (c) 2023 Jiaxun Yang <jiaxun.yang@flygoat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "tcg/tcg-op.h"
#include "tcg/tcg-op-gvec.h"
#include "exec/helper-gen.h"
#include "translate.h"

/* Include the auto-generated decoder.  */
#include "decode-lcsr.c.inc"

static bool trans_CPUCFG(DisasContext *ctx, arg_CPUCFG *a)
{
    TCGv dest = tcg_temp_new();
    TCGv src1 = tcg_temp_new();

    gen_load_gpr(src1, a->rs);
    gen_helper_lcsr_cpucfg(dest, cpu_env, src1);
    gen_store_gpr(dest, a->rd);

    return true;
}

#ifndef CONFIG_USER_ONLY
static bool gen_rdcsr(DisasContext *ctx, arg_r *a,
                        void (*func)(TCGv, TCGv_ptr, TCGv))
{
    TCGv dest = tcg_temp_new();
    TCGv src1 = tcg_temp_new();

    check_cp0_enabled(ctx);
    gen_load_gpr(src1, a->rs);
    func(dest, cpu_env, src1);
    gen_store_gpr(dest, a->rd);

    return true;
}

static bool gen_wrcsr(DisasContext *ctx, arg_r *a,
                        void (*func)(TCGv_ptr, TCGv, TCGv))
{
    TCGv val = tcg_temp_new();
    TCGv addr = tcg_temp_new();

    check_cp0_enabled(ctx);
    gen_load_gpr(addr, a->rs);
    gen_load_gpr(val, a->rd);
    func(cpu_env, addr, val);

    return true;
}

TRANS(RDCSR, gen_rdcsr, gen_helper_lcsr_rdcsr)
TRANS(DRDCSR, gen_rdcsr, gen_helper_lcsr_drdcsr)
TRANS(WRCSR, gen_wrcsr, gen_helper_lcsr_wrcsr)
TRANS(DWRCSR, gen_wrcsr, gen_helper_lcsr_dwrcsr)
#else
#define GEN_FALSE_TRANS(name)   \
static bool trans_##name(DisasContext *ctx, arg_##name * a)  \
{   \
    return false;   \
}

GEN_FALSE_TRANS(RDCSR)
GEN_FALSE_TRANS(DRDCSR)
GEN_FALSE_TRANS(WRCSR)
GEN_FALSE_TRANS(DWRCSR)
#endif
