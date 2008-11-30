/*
 *  CRIS execution defines
 *
 *  Copyright (c) 2007 AXIS Communications AB
 *  Written by Edgar E. Iglesias
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "dyngen-exec.h"

register struct CPUCRISState *env asm(AREG0);

#include "cpu.h"
#include "exec-all.h"

#define RETURN() __asm__ __volatile__("" : : : "memory");

static inline void env_to_regs(void)
{
}

static inline void regs_to_env(void)
{
}

int cpu_cris_handle_mmu_fault (CPUState *env, target_ulong address, int rw,
                              int mmu_idx, int is_softmmu);

#if !defined(CONFIG_USER_ONLY)
#include "softmmu_exec.h"
#endif

void cpu_cris_flush_flags(CPUCRISState *env, int cc_op);
void helper_movec(CPUCRISState *env, int reg, uint32_t val);

static inline int cpu_halted(CPUState *env) {
	if (!env->halted)
		return 0;
	if (env->interrupt_request & CPU_INTERRUPT_HARD) {
		env->halted = 0;
		return 0;
	}
	return EXCP_HALTED;
}
