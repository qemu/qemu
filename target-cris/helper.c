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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <string.h>

#include "config.h"
#include "cpu.h"
#include "mmu.h"
#include "host-utils.h"


//#define CRIS_HELPER_DEBUG


#ifdef CRIS_HELPER_DEBUG
#define D(x) x
#define D_LOG(...) qemu_log(__VA__ARGS__)
#else
#define D(x)
#define D_LOG(...) do { } while (0)
#endif

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
	env->pregs[PR_EDA] = address;
	cpu_dump_state(env, stderr, fprintf, 0);
	return 1;
}

#else /* !CONFIG_USER_ONLY */


static void cris_shift_ccs(CPUState *env)
{
	uint32_t ccs;
	/* Apply the ccs shift.  */
	ccs = env->pregs[PR_CCS];
	ccs = ((ccs & 0xc0000000) | ((ccs << 12) >> 2)) & ~0x3ff;
	env->pregs[PR_CCS] = ccs;
}

int cpu_cris_handle_mmu_fault (CPUState *env, target_ulong address, int rw,
                               int mmu_idx, int is_softmmu)
{
	struct cris_mmu_result res;
	int prot, miss;
	int r = -1;
	target_ulong phy;

	D(printf ("%s addr=%x pc=%x rw=%x\n", __func__, address, env->pc, rw));
	miss = cris_mmu_translate(&res, env, address & TARGET_PAGE_MASK,
				  rw, mmu_idx, 0);
	if (miss)
	{
		if (env->exception_index == EXCP_BUSFAULT)
			cpu_abort(env,
				  "CRIS: Illegal recursive bus fault."
				 "addr=%x rw=%d\n",
				 address, rw);

		env->pregs[PR_EDA] = address;
		env->exception_index = EXCP_BUSFAULT;
		env->fault_vector = res.bf_vec;
		r = 1;
	}
	else
	{
		/*
		 * Mask off the cache selection bit. The ETRAX busses do not
		 * see the top bit.
		 */
		phy = res.phy & ~0x80000000;
		prot = res.prot;
		tlb_set_page(env, address & TARGET_PAGE_MASK, phy,
                             prot, mmu_idx, TARGET_PAGE_SIZE);
                r = 0;
	}
	if (r > 0)
		D_LOG("%s returns %d irqreq=%x addr=%x"
			  " phy=%x ismmu=%d vec=%x pc=%x\n", 
			  __func__, r, env->interrupt_request, 
			  address, res.phy, is_softmmu, res.bf_vec, env->pc);
	return r;
}

static void do_interruptv10(CPUState *env)
{
	int ex_vec = -1;

	D_LOG( "exception index=%d interrupt_req=%d\n",
		   env->exception_index,
		   env->interrupt_request);

	assert(!(env->pregs[PR_CCS] & PFIX_FLAG));
	switch (env->exception_index)
	{
		case EXCP_BREAK:
			/* These exceptions are genereated by the core itself.
			   ERP should point to the insn following the brk.  */
			ex_vec = env->trap_vector;
			env->pregs[PR_ERP] = env->pc;
			break;

		case EXCP_NMI:
			/* NMI is hardwired to vector zero.  */
			ex_vec = 0;
			env->pregs[PR_CCS] &= ~M_FLAG;
			env->pregs[PR_NRP] = env->pc;
			break;

		case EXCP_BUSFAULT:
                        cpu_abort(env, "Unhandled busfault");
			break;

		default:
			/* The interrupt controller gives us the vector.  */
			ex_vec = env->interrupt_vector;
			/* Normal interrupts are taken between
			   TB's.  env->pc is valid here.  */
			env->pregs[PR_ERP] = env->pc;
			break;
	}

	if (env->pregs[PR_CCS] & U_FLAG) {
		/* Swap stack pointers.  */
		env->pregs[PR_USP] = env->regs[R_SP];
		env->regs[R_SP] = env->ksp;
	}

	/* Now that we are in kernel mode, load the handlers address.  */
	env->pc = ldl_code(env->pregs[PR_EBP] + ex_vec * 4);
	env->locked_irq = 1;

	qemu_log_mask(CPU_LOG_INT, "%s isr=%x vec=%x ccs=%x pid=%d erp=%x\n", 
		      __func__, env->pc, ex_vec, 
		      env->pregs[PR_CCS],
		      env->pregs[PR_PID], 
		      env->pregs[PR_ERP]);
}

void do_interrupt(CPUState *env)
{
	int ex_vec = -1;

	if (env->pregs[PR_VR] < 32)
		return do_interruptv10(env);

	D_LOG( "exception index=%d interrupt_req=%d\n",
		   env->exception_index,
		   env->interrupt_request);

	switch (env->exception_index)
	{
		case EXCP_BREAK:
			/* These exceptions are genereated by the core itself.
			   ERP should point to the insn following the brk.  */
			ex_vec = env->trap_vector;
			env->pregs[PR_ERP] = env->pc;
			break;

		case EXCP_NMI:
			/* NMI is hardwired to vector zero.  */
			ex_vec = 0;
			env->pregs[PR_CCS] &= ~M_FLAG;
			env->pregs[PR_NRP] = env->pc;
			break;

		case EXCP_BUSFAULT:
			ex_vec = env->fault_vector;
			env->pregs[PR_ERP] = env->pc;
			break;

		default:
			/* The interrupt controller gives us the vector.  */
			ex_vec = env->interrupt_vector;
			/* Normal interrupts are taken between
			   TB's.  env->pc is valid here.  */
			env->pregs[PR_ERP] = env->pc;
			break;
	}

	/* Fill in the IDX field.  */
	env->pregs[PR_EXS] = (ex_vec & 0xff) << 8;

	if (env->dslot) {
		D_LOG("excp isr=%x PC=%x ds=%d SP=%x"
			  " ERP=%x pid=%x ccs=%x cc=%d %x\n",
			  ex_vec, env->pc, env->dslot,
			  env->regs[R_SP],
			  env->pregs[PR_ERP], env->pregs[PR_PID],
			  env->pregs[PR_CCS],
			  env->cc_op, env->cc_mask);
		/* We loose the btarget, btaken state here so rexec the
		   branch.  */
		env->pregs[PR_ERP] -= env->dslot;
		/* Exception starts with dslot cleared.  */
		env->dslot = 0;
	}
	
	if (env->pregs[PR_CCS] & U_FLAG) {
		/* Swap stack pointers.  */
		env->pregs[PR_USP] = env->regs[R_SP];
		env->regs[R_SP] = env->ksp;
	}

	/* Apply the CRIS CCS shift. Clears U if set.  */
	cris_shift_ccs(env);

	/* Now that we are in kernel mode, load the handlers address.
	   This load may not fault, real hw leaves that behaviour as
	   undefined.  */
	env->pc = ldl_code(env->pregs[PR_EBP] + ex_vec * 4);

	/* Clear the excption_index to avoid spurios hw_aborts for recursive
	   bus faults.  */
	env->exception_index = -1;

	D_LOG("%s isr=%x vec=%x ccs=%x pid=%d erp=%x\n",
		   __func__, env->pc, ex_vec,
		   env->pregs[PR_CCS],
		   env->pregs[PR_PID], 
		   env->pregs[PR_ERP]);
}

target_phys_addr_t cpu_get_phys_page_debug(CPUState * env, target_ulong addr)
{
	uint32_t phy = addr;
	struct cris_mmu_result res;
	int miss;

	miss = cris_mmu_translate(&res, env, addr, 0, 0, 1);
	/* If D TLB misses, try I TLB.  */
	if (miss) {
		miss = cris_mmu_translate(&res, env, addr, 2, 0, 1);
	}

	if (!miss)
		phy = res.phy;
	D(fprintf(stderr, "%s %x -> %x\n", __func__, addr, phy));
	return phy;
}
#endif
