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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA  02110-1301 USA
 */
#include "dyngen-exec.h"

register struct CPUCRISState *env asm(AREG0);

#include "cpu.h"
#include "exec-all.h"

static inline void env_to_regs(void)
{
}

static inline void regs_to_env(void)
{
}

#if !defined(CONFIG_USER_ONLY)
#include "softmmu_exec.h"
#endif

void cpu_cris_flush_flags(CPUCRISState *env, int cc_op);
void helper_movec(CPUCRISState *env, int reg, uint32_t val);

static inline int cpu_halted(CPUState *env) {
	if (!env->halted)
		return 0;

	/* IRQ, NMI and GURU execeptions wakes us up.  */
	if (env->interrupt_request
	    & (CPU_INTERRUPT_HARD | CPU_INTERRUPT_NMI)) {
		env->halted = 0;
		return 0;
	}
	return EXCP_HALTED;
}
