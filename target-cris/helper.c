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

#if defined(CONFIG_USER_ONLY)

void do_interrupt (CPUState *env)
{
  env->exception_index = -1;
}

int cpu_cris_handle_mmu_fault(CPUState * env, target_ulong address, int rw,
                             int mmu_idx, int is_softmmu)
{
    env->exception_index = 0xaa;
    env->debug1 = address;
    cpu_dump_state(env, stderr, fprintf, 0);
    printf("%s addr=%x env->pc=%x\n", __func__, address, env->pc);
    return 1;
}

target_phys_addr_t cpu_get_phys_page_debug(CPUState * env, target_ulong addr)
{
    return addr;
}

#else /* !CONFIG_USER_ONLY */

int cpu_cris_handle_mmu_fault (CPUState *env, target_ulong address, int rw,
                               int mmu_idx, int is_softmmu)
{
	struct cris_mmu_result_t res;
	int prot, miss;
	target_ulong phy;

	address &= TARGET_PAGE_MASK;
	prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
//	printf ("%s pc=%x %x w=%d smmu=%d\n", __func__, env->pc, address, rw, is_softmmu);
	miss = cris_mmu_translate(&res, env, address, rw, mmu_idx);
	if (miss)
	{
		/* handle the miss.  */
		phy = 0;
		env->exception_index = EXCP_MMU_MISS;
	}
	else
	{
		phy = res.phy;
	}
//	printf ("a=%x phy=%x\n", address, phy);
	return tlb_set_page(env, address, phy, prot, mmu_idx, is_softmmu);
}


static void cris_shift_ccs(CPUState *env)
{
	uint32_t ccs;
	/* Apply the ccs shift.  */
	ccs = env->pregs[SR_CCS];
	ccs = (ccs & 0xc0000000) | ((ccs << 12) >> 2);
//	printf ("ccs=%x %x\n", env->pregs[SR_CCS], ccs);
	env->pregs[SR_CCS] = ccs;
}

void do_interrupt(CPUState *env)
{
	uint32_t ebp, isr;
	int irqnum;

	fflush(NULL);

#if 0
	printf ("exception index=%d interrupt_req=%d\n",
		env->exception_index,
		env->interrupt_request);
#endif

	switch (env->exception_index)
	{
		case EXCP_BREAK:
//			printf ("BREAK! %d\n", env->trapnr);
			irqnum = env->trapnr;
			ebp = env->pregs[SR_EBP];
			isr = ldl_code(ebp + irqnum * 4);
			env->pregs[SR_ERP] = env->pc + 2;
			env->pc = isr;

			cris_shift_ccs(env);

			break;
		case EXCP_MMU_MISS:
//			printf ("MMU miss\n");
			irqnum = 4;
			ebp = env->pregs[SR_EBP];
			isr = ldl_code(ebp + irqnum * 4);
			env->pregs[SR_ERP] = env->pc;
			env->pc = isr;
			cris_shift_ccs(env);
			break;

		default:
		{
			/* Maybe the irq was acked by sw before we got a
			   change to take it.  */
			if (env->interrupt_request & CPU_INTERRUPT_HARD) {
				if (!env->pending_interrupts)
					return;
				if (!(env->pregs[SR_CCS] & I_FLAG)) {
					return;
				}

				irqnum = 31 - clz32(env->pending_interrupts);
				irqnum += 0x30;
				ebp = env->pregs[SR_EBP];
				isr = ldl_code(ebp + irqnum * 4);
				env->pregs[SR_ERP] = env->pc;
				env->pc = isr;

				cris_shift_ccs(env);
#if 0
				printf ("%s ebp=%x %x isr=%x %d"
					" ir=%x pending=%x\n",
					__func__,
					ebp, ebp + irqnum * 4,
					isr, env->exception_index,
					env->interrupt_request,
					env->pending_interrupts);
#endif
			}

		}
		break;
	}
}

target_phys_addr_t cpu_get_phys_page_debug(CPUState * env, target_ulong addr)
{
//	printf ("%s\n", __func__);
	uint32_t phy = addr;
	struct cris_mmu_result_t res;
	int miss;
	miss = cris_mmu_translate(&res, env, addr, 0, 0);
	if (!miss)
		phy = res.phy;
	return phy;
}
#endif
