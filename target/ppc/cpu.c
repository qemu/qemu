/*
 *  PowerPC CPU routines for qemu.
 *
 * Copyright (c) 2017 Nikunj A Dadhania, IBM Corporation.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "cpu-models.h"
#include "cpu-qom.h"
#include "exec/log.h"
#include "fpu/softfloat-helpers.h"
#include "mmu-hash64.h"
#include "helper_regs.h"
#include "sysemu/tcg.h"

target_ulong cpu_read_xer(CPUPPCState *env)
{
    if (is_isa300(env)) {
        return env->xer | (env->so << XER_SO) |
            (env->ov << XER_OV) | (env->ca << XER_CA) |
            (env->ov32 << XER_OV32) | (env->ca32 << XER_CA32);
    }

    return env->xer | (env->so << XER_SO) | (env->ov << XER_OV) |
        (env->ca << XER_CA);
}

void cpu_write_xer(CPUPPCState *env, target_ulong xer)
{
    env->so = (xer >> XER_SO) & 1;
    env->ov = (xer >> XER_OV) & 1;
    env->ca = (xer >> XER_CA) & 1;
    /* write all the flags, while reading back check of isa300 */
    env->ov32 = (xer >> XER_OV32) & 1;
    env->ca32 = (xer >> XER_CA32) & 1;
    env->xer = xer & ~((1ul << XER_SO) |
                       (1ul << XER_OV) | (1ul << XER_CA) |
                       (1ul << XER_OV32) | (1ul << XER_CA32));
}

void ppc_store_vscr(CPUPPCState *env, uint32_t vscr)
{
    env->vscr = vscr & ~(1u << VSCR_SAT);
    /* Which bit we set is completely arbitrary, but clear the rest.  */
    env->vscr_sat.u64[0] = vscr & (1u << VSCR_SAT);
    env->vscr_sat.u64[1] = 0;
    set_flush_to_zero((vscr >> VSCR_NJ) & 1, &env->vec_status);
}

uint32_t ppc_get_vscr(CPUPPCState *env)
{
    uint32_t sat = (env->vscr_sat.u64[0] | env->vscr_sat.u64[1]) != 0;
    return env->vscr | (sat << VSCR_SAT);
}

/* GDBstub can read and write MSR... */
void ppc_store_msr(CPUPPCState *env, target_ulong value)
{
    hreg_store_msr(env, value, 0);
}

void ppc_store_lpcr(PowerPCCPU *cpu, target_ulong val)
{
    PowerPCCPUClass *pcc = POWERPC_CPU_GET_CLASS(cpu);
    CPUPPCState *env = &cpu->env;

    env->spr[SPR_LPCR] = val & pcc->lpcr_mask;
    /* The gtse bit affects hflags */
    hreg_compute_hflags(env);
}

static inline void fpscr_set_rounding_mode(CPUPPCState *env)
{
    int rnd_type;

    /* Set rounding mode */
    switch (fpscr_rn) {
    case 0:
        /* Best approximation (round to nearest) */
        rnd_type = float_round_nearest_even;
        break;
    case 1:
        /* Smaller magnitude (round toward zero) */
        rnd_type = float_round_to_zero;
        break;
    case 2:
        /* Round toward +infinite */
        rnd_type = float_round_up;
        break;
    default:
    case 3:
        /* Round toward -infinite */
        rnd_type = float_round_down;
        break;
    }
    set_float_rounding_mode(rnd_type, &env->fp_status);
}

void ppc_store_fpscr(CPUPPCState *env, target_ulong val)
{
    val &= ~(FP_VX | FP_FEX);
    if (val & FPSCR_IX) {
        val |= FP_VX;
    }
    if ((val >> FPSCR_XX) & (val >> FPSCR_XE) & 0x1f) {
        val |= FP_FEX;
    }
    env->fpscr = val;
    if (tcg_enabled()) {
        fpscr_set_rounding_mode(env);
    }
}
