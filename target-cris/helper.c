/*
 *  CRIS helper routines.
 *
 *  Copyright (c) 2007 AXIS Communications AB
 *  Written by Edgar E. Iglesias.
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

#include <stdio.h>
#include <string.h>

#include "config.h"
#include "cpu.h"
#include "mmu.h"
#include "exec-all.h"
#include "host-utils.h"

#define D(x)

#if defined(CONFIG_USER_ONLY)

void do_interrupt (CPUState *env)
{
	env->exception_index = -1;
	env->pregs[PR_ERP] = env->pc;
}

int cpu_cris_handle_mmu_fault(CPUState * env, target_ulong address, int rw,
                             int mmu_idx, int is_softmmu)
{
	env->exception_index = 0xaa;
	env->debug1 = address;
	cpu_dump_state(env, stderr, fprintf, 0);
	env->pregs[PR_ERP] = env->pc;
	return 1;
}

target_phys_addr_t cpu_get_phys_page_debug(CPUState * env, target_ulong addr)
{
	return addr;
}

#else /* !CONFIG_USER_ONLY */


static void cris_shift_ccs(CPUState *env)
{
	uint32_t ccs;
	/* Apply the ccs shift.  */
	ccs = env->pregs[PR_CCS];
	ccs = (ccs & 0xc0000000) | ((ccs << 12) >> 2);
	env->pregs[PR_CCS] = ccs;
}

int cpu_cris_handle_mmu_fault (CPUState *env, target_ulong address, int rw,
                               int mmu_idx, int is_softmmu)
{
	struct cris_mmu_result_t res;
	int prot, miss;
	int r = -1;
	target_ulong phy;

	D(printf ("%s addr=%x pc=%x\n", __func__, address, env->pc));
	address &= TARGET_PAGE_MASK;
	prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
	miss = cris_mmu_translate(&res, env, address, rw, mmu_idx);
	if (miss)
	{
		env->exception_index = EXCP_MMU_FAULT;
		env->fault_vector = res.bf_vec;
		r = 1;
	}
	else
	{
		phy = res.phy;
		prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
		r = tlb_set_page(env, address, phy, prot, mmu_idx, is_softmmu);
	}
	D(printf("%s returns %d irqreq=%x addr=%x ismmu=%d\n", 
			__func__, r, env->interrupt_request, 
			address, is_softmmu));
	return r;
}

void do_interrupt(CPUState *env)
{
	int ex_vec = -1;

	D(fprintf (stderr, "exception index=%d interrupt_req=%d\n",
		 env->exception_index,
		 env->interrupt_request));

	switch (env->exception_index)
	{
		case EXCP_BREAK:
			/* These exceptions are genereated by the core itself.
			   ERP should point to the insn following the brk.  */
			ex_vec = env->trap_vector;
			env->pregs[PR_ERP] = env->pc + 2;
			break;

		case EXCP_MMU_FAULT:
			/* ERP is already setup by translate-all.c through
			   re-translation of the aborted TB combined with 
			   pc searching.  */
			ex_vec = env->fault_vector;
			break;

		default:
		{
			/* Maybe the irq was acked by sw before we got a
			   change to take it.  */
			if (env->interrupt_request & CPU_INTERRUPT_HARD) {
				/* Vectors below 0x30 are internal
				   exceptions, i.e not interrupt requests
				   from the interrupt controller.  */
				if (env->interrupt_vector < 0x30)
					return;
				/* Is the core accepting interrupts?  */
				if (!(env->pregs[PR_CCS] & I_FLAG)) {
					return;
				}
				/* The interrupt controller gives us the
				   vector.  */
				ex_vec = env->interrupt_vector;
				/* Normal interrupts are taken between
				   TB's.  env->pc is valid here.  */
				env->pregs[PR_ERP] = env->pc;
			}
		}
		break;
	}
	env->pc = ldl_code(env->pregs[PR_EBP] + ex_vec * 4);
	/* Apply the CRIS CCS shift.  */
	cris_shift_ccs(env);
	D(printf ("%s ebp=%x isr=%x vec=%x\n", __func__, ebp, isr, ex_vec));
}

target_phys_addr_t cpu_get_phys_page_debug(CPUState * env, target_ulong addr)
{
	uint32_t phy = addr;
	struct cris_mmu_result_t res;
	int miss;
	miss = cris_mmu_translate(&res, env, addr, 0, 0);
	if (!miss)
		phy = res.phy;
	D(fprintf(stderr, "%s %x -> %x\n", __func__, addr, phy));
	return phy;
}
#endif
