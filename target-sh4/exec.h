/*
 *  SH4 emulation
 *
 *  Copyright (c) 2005 Samuel Tardieu
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef _EXEC_SH4_H
#define _EXEC_SH4_H

#include "config.h"
#include "dyngen-exec.h"

register struct CPUSH4State *env asm(AREG0);
register uint32_t T0 asm(AREG1);
register uint32_t T1 asm(AREG2);
//register uint32_t T2 asm(AREG3);

#define FT0 (env->ft0)
#define FT1 (env->ft1)
#define DT0 (env->dt0)
#define DT1 (env->dt1)

#include "cpu.h"
#include "exec-all.h"

static inline int cpu_halted(CPUState *env) {
    if (!env->halted)
        return 0;
    if (env->interrupt_request & CPU_INTERRUPT_HARD) {
        env->halted = 0;
        env->intr_at_halt = 1;
        return 0;
    }
    return EXCP_HALTED;
}

#ifndef CONFIG_USER_ONLY
#include "softmmu_exec.h"
#endif

#define RETURN() __asm__ __volatile__("")

static inline void regs_to_env(void)
{
    /* XXXXX */
}

static inline void env_to_regs(void)
{
    /* XXXXX */
}

int cpu_sh4_handle_mmu_fault(CPUState * env, target_ulong address, int rw,
			     int mmu_idx, int is_softmmu);
void cpu_load_tlb(CPUState * env);

int find_itlb_entry(CPUState * env, target_ulong address,
		    int use_asid, int update);
int find_utlb_entry(CPUState * env, target_ulong address, int use_asid);

void helper_div1_T0_T1(void);
void helper_rotcl(uint32_t * addr);
void helper_rotcr(uint32_t * addr);

void do_interrupt(CPUState * env);

void cpu_loop_exit(void);

#endif				/* _EXEC_SH4_H */
