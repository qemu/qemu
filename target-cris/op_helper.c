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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "cpu.h"
#include "dyngen-exec.h"
#include "mmu.h"
#include "helper.h"
#include "host-utils.h"

//#define CRIS_OP_HELPER_DEBUG


#ifdef CRIS_OP_HELPER_DEBUG
#define D(x) x
#define D_LOG(...) qemu_log(__VA__ARGS__)
#else
#define D(x)
#define D_LOG(...) do { } while (0)
#endif

#if !defined(CONFIG_USER_ONLY)
#include "softmmu_exec.h"

#define MMUSUFFIX _mmu

#define SHIFT 0
#include "softmmu_template.h"

#define SHIFT 1
#include "softmmu_template.h"

#define SHIFT 2
#include "softmmu_template.h"

#define SHIFT 3
#include "softmmu_template.h"

/* Try to fill the TLB and return an exception if error. If retaddr is
   NULL, it means that the function was called in C code (i.e. not
   from generated code or from helper.c) */
/* XXX: fix it to restore all registers */
void tlb_fill(CPUState *env1, target_ulong addr, int is_write, int mmu_idx,
              void *retaddr)
{
    TranslationBlock *tb;
    CPUState *saved_env;
    unsigned long pc;
    int ret;

    saved_env = env;
    env = env1;

    D_LOG("%s pc=%x tpc=%x ra=%x\n", __func__, 
	     env->pc, env->debug1, retaddr);
    ret = cpu_cris_handle_mmu_fault(env, addr, is_write, mmu_idx);
    if (unlikely(ret)) {
        if (retaddr) {
            /* now we have a real cpu fault */
            pc = (unsigned long)retaddr;
            tb = tb_find_pc(pc);
            if (tb) {
                /* the PC is inside the translated code. It means that we have
                   a virtual CPU fault */
                cpu_restore_state(tb, env, pc);

		/* Evaluate flags after retranslation.  */
                helper_top_evaluate_flags();
            }
        }
        cpu_loop_exit(env);
    }
    env = saved_env;
}

#endif

void helper_raise_exception(uint32_t index)
{
	env->exception_index = index;
        cpu_loop_exit(env);
}

void helper_tlb_flush_pid(uint32_t pid)
{
#if !defined(CONFIG_USER_ONLY)
	pid &= 0xff;
	if (pid != (env->pregs[PR_PID] & 0xff))
		cris_mmu_flush_pid(env, env->pregs[PR_PID]);
#endif
}

void helper_spc_write(uint32_t new_spc)
{
#if !defined(CONFIG_USER_ONLY)
	tlb_flush_page(env, env->pregs[PR_SPC]);
	tlb_flush_page(env, new_spc);
#endif
}

void helper_dump(uint32_t a0, uint32_t a1, uint32_t a2)
{
	qemu_log("%s: a0=%x a1=%x\n", __func__, a0, a1);
}

/* Used by the tlb decoder.  */
#define EXTRACT_FIELD(src, start, end) \
	    (((src) >> start) & ((1 << (end - start + 1)) - 1))

void helper_movl_sreg_reg (uint32_t sreg, uint32_t reg)
{
	uint32_t srs;
	srs = env->pregs[PR_SRS];
	srs &= 3;
	env->sregs[srs][sreg] = env->regs[reg];

#if !defined(CONFIG_USER_ONLY)
	if (srs == 1 || srs == 2) {
		if (sreg == 6) {
			/* Writes to tlb-hi write to mm_cause as a side 
			   effect.  */
			env->sregs[SFR_RW_MM_TLB_HI] = env->regs[reg];
			env->sregs[SFR_R_MM_CAUSE] = env->regs[reg];
		}
		else if (sreg == 5) {
			uint32_t set;
			uint32_t idx;
			uint32_t lo, hi;
			uint32_t vaddr;
			int tlb_v;

			idx = set = env->sregs[SFR_RW_MM_TLB_SEL];
			set >>= 4;
			set &= 3;

			idx &= 15;
			/* We've just made a write to tlb_lo.  */
			lo = env->sregs[SFR_RW_MM_TLB_LO];
			/* Writes are done via r_mm_cause.  */
			hi = env->sregs[SFR_R_MM_CAUSE];

			vaddr = EXTRACT_FIELD(env->tlbsets[srs-1][set][idx].hi,
					      13, 31);
			vaddr <<= TARGET_PAGE_BITS;
			tlb_v = EXTRACT_FIELD(env->tlbsets[srs-1][set][idx].lo,
					    3, 3);
			env->tlbsets[srs - 1][set][idx].lo = lo;
			env->tlbsets[srs - 1][set][idx].hi = hi;

			D_LOG("tlb flush vaddr=%x v=%d pc=%x\n", 
				  vaddr, tlb_v, env->pc);
			if (tlb_v) {
				tlb_flush_page(env, vaddr);
			}
		}
	}
#endif
}

void helper_movl_reg_sreg (uint32_t reg, uint32_t sreg)
{
	uint32_t srs;
	env->pregs[PR_SRS] &= 3;
	srs = env->pregs[PR_SRS];
	
#if !defined(CONFIG_USER_ONLY)
	if (srs == 1 || srs == 2)
	{
		uint32_t set;
		uint32_t idx;
		uint32_t lo, hi;

		idx = set = env->sregs[SFR_RW_MM_TLB_SEL];
		set >>= 4;
		set &= 3;
		idx &= 15;

		/* Update the mirror regs.  */
		hi = env->tlbsets[srs - 1][set][idx].hi;
		lo = env->tlbsets[srs - 1][set][idx].lo;
		env->sregs[SFR_RW_MM_TLB_HI] = hi;
		env->sregs[SFR_RW_MM_TLB_LO] = lo;
	}
#endif
	env->regs[reg] = env->sregs[srs][sreg];
}

static void cris_ccs_rshift(CPUState *env)
{
	uint32_t ccs;

	/* Apply the ccs shift.  */
	ccs = env->pregs[PR_CCS];
	ccs = (ccs & 0xc0000000) | ((ccs & 0x0fffffff) >> 10);
	if (ccs & U_FLAG)
	{
		/* Enter user mode.  */
		env->ksp = env->regs[R_SP];
		env->regs[R_SP] = env->pregs[PR_USP];
	}

	env->pregs[PR_CCS] = ccs;
}

void helper_rfe(void)
{
	int rflag = env->pregs[PR_CCS] & R_FLAG;

	D_LOG("rfe: erp=%x pid=%x ccs=%x btarget=%x\n", 
		 env->pregs[PR_ERP], env->pregs[PR_PID],
		 env->pregs[PR_CCS],
		 env->btarget);

	cris_ccs_rshift(env);

	/* RFE sets the P_FLAG only if the R_FLAG is not set.  */
	if (!rflag)
		env->pregs[PR_CCS] |= P_FLAG;
}

void helper_rfn(void)
{
	int rflag = env->pregs[PR_CCS] & R_FLAG;

	D_LOG("rfn: erp=%x pid=%x ccs=%x btarget=%x\n", 
		 env->pregs[PR_ERP], env->pregs[PR_PID],
		 env->pregs[PR_CCS],
		 env->btarget);

	cris_ccs_rshift(env);

	/* Set the P_FLAG only if the R_FLAG is not set.  */
	if (!rflag)
		env->pregs[PR_CCS] |= P_FLAG;

    /* Always set the M flag.  */
    env->pregs[PR_CCS] |= M_FLAG;
}

uint32_t helper_lz(uint32_t t0)
{
	return clz32(t0);
}

uint32_t helper_btst(uint32_t t0, uint32_t t1, uint32_t ccs)
{
	/* FIXME: clean this up.  */

	/* des ref:
	   The N flag is set according to the selected bit in the dest reg.
	   The Z flag is set if the selected bit and all bits to the right are
	   zero.
	   The X flag is cleared.
	   Other flags are left untouched.
	   The destination reg is not affected.*/
	unsigned int fz, sbit, bset, mask, masked_t0;

	sbit = t1 & 31;
	bset = !!(t0 & (1 << sbit));
	mask = sbit == 31 ? -1 : (1 << (sbit + 1)) - 1;
	masked_t0 = t0 & mask;
	fz = !(masked_t0 | bset);

	/* Clear the X, N and Z flags.  */
	ccs = ccs & ~(X_FLAG | N_FLAG | Z_FLAG);
	if (env->pregs[PR_VR] < 32)
		ccs &= ~(V_FLAG | C_FLAG);
	/* Set the N and Z flags accordingly.  */
	ccs |= (bset << 3) | (fz << 2);
	return ccs;
}

static inline uint32_t evaluate_flags_writeback(uint32_t flags, uint32_t ccs)
{
	unsigned int x, z, mask;

	/* Extended arithmetics, leave the z flag alone.  */
	x = env->cc_x;
	mask = env->cc_mask | X_FLAG;
        if (x) {
		z = flags & Z_FLAG;
		mask = mask & ~z;
	}
	flags &= mask;

	/* all insn clear the x-flag except setf or clrf.  */
	ccs &= ~mask;
	ccs |= flags;
	return ccs;
}

uint32_t helper_evaluate_flags_muls(uint32_t ccs, uint32_t res, uint32_t mof)
{
	uint32_t flags = 0;
	int64_t tmp;
	int dneg;

	dneg = ((int32_t)res) < 0;

	tmp = mof;
	tmp <<= 32;
	tmp |= res;
	if (tmp == 0)
		flags |= Z_FLAG;
	else if (tmp < 0)
		flags |= N_FLAG;
	if ((dneg && mof != -1)
	    || (!dneg && mof != 0))
		flags |= V_FLAG;
	return evaluate_flags_writeback(flags, ccs);
}

uint32_t helper_evaluate_flags_mulu(uint32_t ccs, uint32_t res, uint32_t mof)
{
	uint32_t flags = 0;
	uint64_t tmp;

	tmp = mof;
	tmp <<= 32;
	tmp |= res;
	if (tmp == 0)
		flags |= Z_FLAG;
	else if (tmp >> 63)
		flags |= N_FLAG;
	if (mof)
		flags |= V_FLAG;

	return evaluate_flags_writeback(flags, ccs);
}

uint32_t helper_evaluate_flags_mcp(uint32_t ccs,
				   uint32_t src, uint32_t dst, uint32_t res)
{
	uint32_t flags = 0;

	src = src & 0x80000000;
	dst = dst & 0x80000000;

	if ((res & 0x80000000L) != 0L)
	{
		flags |= N_FLAG;
		if (!src && !dst)
			flags |= V_FLAG;
		else if (src & dst)
			flags |= R_FLAG;
	}
	else
	{
		if (res == 0L)
			flags |= Z_FLAG;
		if (src & dst) 
			flags |= V_FLAG;
		if (dst | src) 
			flags |= R_FLAG;
	}

	return evaluate_flags_writeback(flags, ccs);
}

uint32_t helper_evaluate_flags_alu_4(uint32_t ccs,
				     uint32_t src, uint32_t dst, uint32_t res)
{
	uint32_t flags = 0;

	src = src & 0x80000000;
	dst = dst & 0x80000000;

	if ((res & 0x80000000L) != 0L)
	{
		flags |= N_FLAG;
		if (!src && !dst)
			flags |= V_FLAG;
		else if (src & dst)
			flags |= C_FLAG;
	}
	else
	{
		if (res == 0L)
			flags |= Z_FLAG;
		if (src & dst) 
			flags |= V_FLAG;
		if (dst | src) 
			flags |= C_FLAG;
	}

	return evaluate_flags_writeback(flags, ccs);
}

uint32_t helper_evaluate_flags_sub_4(uint32_t ccs,
				     uint32_t src, uint32_t dst, uint32_t res)
{
	uint32_t flags = 0;

	src = (~src) & 0x80000000;
	dst = dst & 0x80000000;

	if ((res & 0x80000000L) != 0L)
	{
		flags |= N_FLAG;
		if (!src && !dst)
			flags |= V_FLAG;
		else if (src & dst)
			flags |= C_FLAG;
	}
	else
	{
		if (res == 0L)
			flags |= Z_FLAG;
		if (src & dst) 
			flags |= V_FLAG;
		if (dst | src) 
			flags |= C_FLAG;
	}

	flags ^= C_FLAG;
	return evaluate_flags_writeback(flags, ccs);
}

uint32_t helper_evaluate_flags_move_4(uint32_t ccs, uint32_t res)
{
	uint32_t flags = 0;

	if ((int32_t)res < 0)
		flags |= N_FLAG;
	else if (res == 0L)
		flags |= Z_FLAG;

	return evaluate_flags_writeback(flags, ccs);
}
uint32_t helper_evaluate_flags_move_2(uint32_t ccs, uint32_t res)
{
	uint32_t flags = 0;

	if ((int16_t)res < 0L)
		flags |= N_FLAG;
	else if (res == 0)
		flags |= Z_FLAG;

	return evaluate_flags_writeback(flags, ccs);
}

/* TODO: This is expensive. We could split things up and only evaluate part of
   CCR on a need to know basis. For now, we simply re-evaluate everything.  */
void  helper_evaluate_flags(void)
{
	uint32_t src, dst, res;
	uint32_t flags = 0;

	src = env->cc_src;
	dst = env->cc_dest;
	res = env->cc_result;

	if (env->cc_op == CC_OP_SUB || env->cc_op == CC_OP_CMP)
		src = ~src;

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

	if (env->cc_op == CC_OP_SUB || env->cc_op == CC_OP_CMP)
		flags ^= C_FLAG;

	env->pregs[PR_CCS] = evaluate_flags_writeback(flags, env->pregs[PR_CCS]);
}

void helper_top_evaluate_flags(void)
{
	switch (env->cc_op)
	{
		case CC_OP_MCP:
			env->pregs[PR_CCS] = helper_evaluate_flags_mcp(
					env->pregs[PR_CCS], env->cc_src,
					env->cc_dest, env->cc_result);
			break;
		case CC_OP_MULS:
			env->pregs[PR_CCS] = helper_evaluate_flags_muls(
					env->pregs[PR_CCS], env->cc_result,
					env->pregs[PR_MOF]);
			break;
		case CC_OP_MULU:
			env->pregs[PR_CCS] = helper_evaluate_flags_mulu(
					env->pregs[PR_CCS], env->cc_result,
					env->pregs[PR_MOF]);
			break;
		case CC_OP_MOVE:
		case CC_OP_AND:
		case CC_OP_OR:
		case CC_OP_XOR:
		case CC_OP_ASR:
		case CC_OP_LSR:
		case CC_OP_LSL:
		switch (env->cc_size)
		{
			case 4:
				env->pregs[PR_CCS] =
					helper_evaluate_flags_move_4(
							env->pregs[PR_CCS],
							env->cc_result);
				break;
			case 2:
				env->pregs[PR_CCS] =
					helper_evaluate_flags_move_2(
							env->pregs[PR_CCS],
							env->cc_result);
				break;
			default:
				helper_evaluate_flags();
				break;
		}
		break;
		case CC_OP_FLAGS:
			/* live.  */
			break;
		case CC_OP_SUB:
		case CC_OP_CMP:
			if (env->cc_size == 4)
				env->pregs[PR_CCS] =
					helper_evaluate_flags_sub_4(
						env->pregs[PR_CCS],
						env->cc_src, env->cc_dest,
						env->cc_result);
			else
				helper_evaluate_flags();
			break;
		default:
		{
			switch (env->cc_size)
			{
			case 4:
				env->pregs[PR_CCS] =
					helper_evaluate_flags_alu_4(
						env->pregs[PR_CCS],
						env->cc_src, env->cc_dest,
						env->cc_result);
				break;
			default:
				helper_evaluate_flags();
				break;
			}
		}
		break;
	}
}
