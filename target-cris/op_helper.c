/*
 *  CRIS helper routines
 *
 *  Copyright (c) 2007 AXIS Communications
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
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <assert.h>
#include "exec.h"
#include "mmu.h"

#define MMUSUFFIX _mmu
#ifdef __s390__
# define GETPC() ((void*)((unsigned long)__builtin_return_address(0) & 0x7fffffffUL))
#else
# define GETPC() (__builtin_return_address(0))
#endif

#define SHIFT 0
#include "softmmu_template.h"

#define SHIFT 1
#include "softmmu_template.h"

#define SHIFT 2
#include "softmmu_template.h"

#define SHIFT 3
#include "softmmu_template.h"

#define D(x)

/* Try to fill the TLB and return an exception if error. If retaddr is
   NULL, it means that the function was called in C code (i.e. not
   from generated code or from helper.c) */
/* XXX: fix it to restore all registers */
void tlb_fill (target_ulong addr, int is_write, int mmu_idx, void *retaddr)
{
    TranslationBlock *tb;
    CPUState *saved_env;
    unsigned long pc;
    int ret;

    /* XXX: hack to restore env in all cases, even if not called from
       generated code */
    saved_env = env;
    env = cpu_single_env;

    D(fprintf(logfile, "%s ra=%x acr=%x %x\n", __func__, retaddr,
	    env->regs[R_ACR], saved_env->regs[R_ACR]));
    ret = cpu_cris_handle_mmu_fault(env, addr, is_write, mmu_idx, 1);
    if (__builtin_expect(ret, 0)) {
        if (retaddr) {
            /* now we have a real cpu fault */
            pc = (unsigned long)retaddr;
            tb = tb_find_pc(pc);
            if (tb) {
                /* the PC is inside the translated code. It means that we have
                   a virtual CPU fault */
                cpu_restore_state(tb, env, pc, NULL);
            }
        }
        cpu_loop_exit();
    }
    env = saved_env;
}

void helper_tlb_update(uint32_t T0)
{
#if !defined(CONFIG_USER_ONLY)
	uint32_t vaddr;
	uint32_t srs = env->pregs[PR_SRS];

	if (srs != 1 && srs != 2)
		return;

	vaddr = cris_mmu_tlb_latest_update(env, T0);
	D(printf("flush old_vaddr=%x vaddr=%x T0=%x\n", vaddr, 
		 env->sregs[SFR_R_MM_CAUSE] & TARGET_PAGE_MASK, T0));
	tlb_flush_page(env, vaddr);
#endif
}

void helper_tlb_flush(void)
{
	tlb_flush(env, 1);
}

void helper_dump(uint32_t a0, uint32_t a1)
{
	(fprintf(logfile, "%s: a0=%x a1=%x\n", __func__, a0, a1)); 
}

void helper_dummy(void)
{

}

/* Only used for debugging at the moment.  */
void helper_rfe(void)
{
	D(fprintf(logfile, "rfe: erp=%x pid=%x ccs=%x btarget=%x\n", 
		 env->pregs[PR_ERP], env->pregs[PR_PID],
		 env->pregs[PR_CCS],
		 env->btarget));
}

void helper_store(uint32_t a0)
{
	if (env->pregs[PR_CCS] & P_FLAG )
	{
		cpu_abort(env, "cond_store_failed! pc=%x a0=%x\n",
			  env->pc, a0);
	}
}

void do_unassigned_access(target_phys_addr_t addr, int is_write, int is_exec,
                          int is_asi)
{
	D(printf("%s addr=%x w=%d ex=%d asi=%d\n", 
		__func__, addr, is_write, is_exec, is_asi));
}

static void evaluate_flags_writeback(uint32_t flags)
{
	int x;

	/* Extended arithmetics, leave the z flag alone.  */
	env->debug3 = env->pregs[PR_CCS];

	if (env->cc_x_live)
		x = env->cc_x;
	else
		x = env->pregs[PR_CCS] & X_FLAG;

	if ((x || env->cc_op == CC_OP_ADDC)
	    && flags & Z_FLAG)
		env->cc_mask &= ~Z_FLAG;

	/* all insn clear the x-flag except setf or clrf.  */
	env->pregs[PR_CCS] &= ~(env->cc_mask | X_FLAG);
	flags &= env->cc_mask;
	env->pregs[PR_CCS] |= flags;
	RETURN();
}

void helper_evaluate_flags_muls(void)
{
	uint32_t src;
	uint32_t dst;
	uint32_t res;
	uint32_t flags = 0;
	/* were gonna have to redo the muls.  */
	int64_t tmp, t0 ,t1;
	int32_t mof;
	int dneg;

	src = env->cc_src;
	dst = env->cc_dest;
	res = env->cc_result;


	/* cast into signed values to make GCC sign extend.  */
	t0 = (int32_t)src;
	t1 = (int32_t)dst;
	dneg = ((int32_t)res) < 0;

	tmp = t0 * t1;
	mof = tmp >> 32;
	if (tmp == 0)
		flags |= Z_FLAG;
	else if (tmp < 0)
		flags |= N_FLAG;
	if ((dneg && mof != -1)
	    || (!dneg && mof != 0))
		flags |= V_FLAG;
	evaluate_flags_writeback(flags);
}

void  helper_evaluate_flags_mulu(void)
{
	uint32_t src;
	uint32_t dst;
	uint32_t res;
	uint32_t flags = 0;
	/* were gonna have to redo the muls.  */
	uint64_t tmp, t0 ,t1;
	uint32_t mof;

	src = env->cc_src;
	dst = env->cc_dest;
	res = env->cc_result;


	/* cast into signed values to make GCC sign extend.  */
	t0 = src;
	t1 = dst;

	tmp = t0 * t1;
	mof = tmp >> 32;
	if (tmp == 0)
		flags |= Z_FLAG;
	else if (tmp >> 63)
		flags |= N_FLAG;
	if (mof)
		flags |= V_FLAG;

	evaluate_flags_writeback(flags);
}

void  helper_evaluate_flags_mcp(void)
{
	uint32_t src;
	uint32_t dst;
	uint32_t res;
	uint32_t flags = 0;

	src = env->cc_src;
	dst = env->cc_dest;
	res = env->cc_result;

	if ((res & 0x80000000L) != 0L)
	{
		flags |= N_FLAG;
		if (((src & 0x80000000L) == 0L)
		    && ((dst & 0x80000000L) == 0L))
		{
			flags |= V_FLAG;
		}
		else if (((src & 0x80000000L) != 0L) &&
			 ((dst & 0x80000000L) != 0L))
		{
			flags |= R_FLAG;
		}
	}
	else
	{
		if (res == 0L)
			flags |= Z_FLAG;
		if (((src & 0x80000000L) != 0L)
		    && ((dst & 0x80000000L) != 0L))
			flags |= V_FLAG;
		if ((dst & 0x80000000L) != 0L
		    || (src & 0x80000000L) != 0L)
			flags |= R_FLAG;
	}

	evaluate_flags_writeback(flags);
}

void  helper_evaluate_flags_alu_4(void)
{
	uint32_t src;
	uint32_t dst;
	uint32_t res;
	uint32_t flags = 0;

	src = env->cc_src;
	dst = env->cc_dest;
	res = env->cc_result;

	if ((res & 0x80000000L) != 0L)
	{
		flags |= N_FLAG;
		if (((src & 0x80000000L) == 0L)
		    && ((dst & 0x80000000L) == 0L))
		{
			flags |= V_FLAG;
		}
		else if (((src & 0x80000000L) != 0L) &&
			 ((dst & 0x80000000L) != 0L))
		{
			flags |= C_FLAG;
		}
	}
	else
	{
		if (res == 0L)
			flags |= Z_FLAG;
		if (((src & 0x80000000L) != 0L)
		    && ((dst & 0x80000000L) != 0L))
			flags |= V_FLAG;
		if ((dst & 0x80000000L) != 0L
		    || (src & 0x80000000L) != 0L)
			flags |= C_FLAG;
	}

	if (env->cc_op == CC_OP_SUB
	    || env->cc_op == CC_OP_CMP) {
		flags ^= C_FLAG;
	}
	evaluate_flags_writeback(flags);
}

void  helper_evaluate_flags_move_4 (void)
{
	uint32_t src;
	uint32_t res;
	uint32_t flags = 0;

	src = env->cc_src;
	res = env->cc_result;

	if ((int32_t)res < 0)
		flags |= N_FLAG;
	else if (res == 0L)
		flags |= Z_FLAG;

	evaluate_flags_writeback(flags);
}
void  helper_evaluate_flags_move_2 (void)
{
	uint32_t src;
	uint32_t flags = 0;
	uint16_t res;

	src = env->cc_src;
	res = env->cc_result;

	if ((int16_t)res < 0L)
		flags |= N_FLAG;
	else if (res == 0)
		flags |= Z_FLAG;

	evaluate_flags_writeback(flags);
}

/* TODO: This is expensive. We could split things up and only evaluate part of
   CCR on a need to know basis. For now, we simply re-evaluate everything.  */
void helper_evaluate_flags (void)
{
	uint32_t src;
	uint32_t dst;
	uint32_t res;
	uint32_t flags = 0;

	src = env->cc_src;
	dst = env->cc_dest;
	res = env->cc_result;


	/* Now, evaluate the flags. This stuff is based on
	   Per Zander's CRISv10 simulator.  */
	switch (env->cc_size)
	{
		case 1:
			if ((res & 0x80L) != 0L)
			{
				flags |= N_FLAG;
				if (((src & 0x80L) == 0L)
				    && ((dst & 0x80L) == 0L))
				{
					flags |= V_FLAG;
				}
				else if (((src & 0x80L) != 0L)
					 && ((dst & 0x80L) != 0L))
				{
					flags |= C_FLAG;
				}
			}
			else
			{
				if ((res & 0xFFL) == 0L)
				{
					flags |= Z_FLAG;
				}
				if (((src & 0x80L) != 0L)
				    && ((dst & 0x80L) != 0L))
				{
					flags |= V_FLAG;
				}
				if ((dst & 0x80L) != 0L
				    || (src & 0x80L) != 0L)
				{
					flags |= C_FLAG;
				}
			}
			break;
		case 2:
			if ((res & 0x8000L) != 0L)
			{
				flags |= N_FLAG;
				if (((src & 0x8000L) == 0L)
				    && ((dst & 0x8000L) == 0L))
				{
					flags |= V_FLAG;
				}
				else if (((src & 0x8000L) != 0L)
					 && ((dst & 0x8000L) != 0L))
				{
					flags |= C_FLAG;
				}
			}
			else
			{
				if ((res & 0xFFFFL) == 0L)
				{
					flags |= Z_FLAG;
				}
				if (((src & 0x8000L) != 0L)
				    && ((dst & 0x8000L) != 0L))
				{
					flags |= V_FLAG;
				}
				if ((dst & 0x8000L) != 0L
				    || (src & 0x8000L) != 0L)
				{
					flags |= C_FLAG;
				}
			}
			break;
		case 4:
			if ((res & 0x80000000L) != 0L)
			{
				flags |= N_FLAG;
				if (((src & 0x80000000L) == 0L)
				    && ((dst & 0x80000000L) == 0L))
				{
					flags |= V_FLAG;
				}
				else if (((src & 0x80000000L) != 0L) &&
					 ((dst & 0x80000000L) != 0L))
				{
					flags |= C_FLAG;
				}
			}
			else
			{
				if (res == 0L)
					flags |= Z_FLAG;
				if (((src & 0x80000000L) != 0L)
				    && ((dst & 0x80000000L) != 0L))
					flags |= V_FLAG;
				if ((dst & 0x80000000L) != 0L
				    || (src & 0x80000000L) != 0L)
					flags |= C_FLAG;
			}
			break;
		default:
			break;
	}

	if (env->cc_op == CC_OP_SUB
	    || env->cc_op == CC_OP_CMP) {
		flags ^= C_FLAG;
	}
	evaluate_flags_writeback(flags);
}
