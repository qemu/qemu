/*
 * Address Computation and Large Constant Instructions
 *
 *  Copyright (c) 2004-2005 Jocelyn Mayer
 *  Copyright (c) 2006 Marius Groeger (FPU operations)
 *  Copyright (c) 2006 Thiemo Seufer (MIPS32R2 support)
 *  Copyright (c) 2009 CodeSourcery (MIPS16 and microMIPS support)
 *  Copyright (c) 2012 Jia Liu & Dongxue Zhang (MIPS ASE DSP support)
 *  Copyright (c) 2020 Philippe Mathieu-Daudé
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "qemu/osdep.h"
#include "tcg/tcg-op.h"
#include "translate.h"

bool gen_lsa(DisasContext *ctx, int rd, int rt, int rs, int sa)
{
    TCGv t0;
    TCGv t1;

    if (rd == 0) {
        /* Treat as NOP. */
        return true;
    }
    t0 = tcg_temp_new();
    t1 = tcg_temp_new();
    gen_load_gpr(t0, rs);
    gen_load_gpr(t1, rt);
    tcg_gen_shli_tl(t0, t0, sa + 1);
    tcg_gen_add_tl(cpu_gpr[rd], t0, t1);
    tcg_gen_ext32s_tl(cpu_gpr[rd], cpu_gpr[rd]);

    tcg_temp_free(t1);
    tcg_temp_free(t0);

    return true;
}

bool gen_dlsa(DisasContext *ctx, int rd, int rt, int rs, int sa)
{
    TCGv t0;
    TCGv t1;

    check_mips_64(ctx);

    if (rd == 0) {
        /* Treat as NOP. */
        return true;
    }
    t0 = tcg_temp_new();
    t1 = tcg_temp_new();
    gen_load_gpr(t0, rs);
    gen_load_gpr(t1, rt);
    tcg_gen_shli_tl(t0, t0, sa + 1);
    tcg_gen_add_tl(cpu_gpr[rd], t0, t1);
    tcg_temp_free(t1);
    tcg_temp_free(t0);

    return true;
}
