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
#include <assert.h>
#include "exec.h"

#ifndef CONFIG_USER_ONLY

#define MMUSUFFIX _mmu

#define SHIFT 0
#include "softmmu_template.h"

#define SHIFT 1
#include "softmmu_template.h"

#define SHIFT 2
#include "softmmu_template.h"

#define SHIFT 3
#include "softmmu_template.h"

void tlb_fill(target_ulong addr, int is_write, int mmu_idx, void *retaddr)
{
    TranslationBlock *tb;
    CPUState *saved_env;
    unsigned long pc;
    int ret;

    /* XXX: hack to restore env in all cases, even if not called from
       generated code */
    saved_env = env;
    env = cpu_single_env;
    ret = cpu_sh4_handle_mmu_fault(env, addr, is_write, mmu_idx, 1);
    if (ret) {
	if (retaddr) {
	    /* now we have a real cpu fault */
	    pc = (unsigned long) retaddr;
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

#endif

void helper_ldtlb(void)
{
#ifdef CONFIG_USER_ONLY
    /* XXXXX */
    assert(0);
#else
    cpu_load_tlb(env);
#endif
}

void helper_raise_illegal_instruction(void)
{
    env->exception_index = 0x180;
    cpu_loop_exit();
}

void helper_raise_slot_illegal_instruction(void)
{
    env->exception_index = 0x1a0;
    cpu_loop_exit();
}

void helper_debug(void)
{
    env->exception_index = EXCP_DEBUG;
    cpu_loop_exit();
}

void helper_sleep(void)
{
    env->halted = 1;
    env->exception_index = EXCP_HLT;
    cpu_loop_exit();
}

void helper_trapa(uint32_t tra)
{
    env->tra = tra << 2;
    env->exception_index = 0x160;
    cpu_loop_exit();
}

uint32_t helper_addc(uint32_t arg0, uint32_t arg1)
{
    uint32_t tmp0, tmp1;

    tmp1 = arg0 + arg1;
    tmp0 = arg1;
    arg1 = tmp1 + (env->sr & 1);
    if (tmp0 > tmp1)
	env->sr |= SR_T;
    else
	env->sr &= ~SR_T;
    if (tmp1 > arg1)
	env->sr |= SR_T;
    return arg1;
}

uint32_t helper_addv(uint32_t arg0, uint32_t arg1)
{
    uint32_t dest, src, ans;

    if ((int32_t) arg1 >= 0)
	dest = 0;
    else
	dest = 1;
    if ((int32_t) arg0 >= 0)
	src = 0;
    else
	src = 1;
    src += dest;
    arg1 += arg0;
    if ((int32_t) arg1 >= 0)
	ans = 0;
    else
	ans = 1;
    ans += dest;
    if (src == 0 || src == 2) {
	if (ans == 1)
	    env->sr |= SR_T;
	else
	    env->sr &= ~SR_T;
    } else
	env->sr &= ~SR_T;
    return arg1;
}

#define T (env->sr & SR_T)
#define Q (env->sr & SR_Q ? 1 : 0)
#define M (env->sr & SR_M ? 1 : 0)
#define SETT env->sr |= SR_T
#define CLRT env->sr &= ~SR_T
#define SETQ env->sr |= SR_Q
#define CLRQ env->sr &= ~SR_Q
#define SETM env->sr |= SR_M
#define CLRM env->sr &= ~SR_M

uint32_t helper_div1(uint32_t arg0, uint32_t arg1)
{
    uint32_t tmp0, tmp2;
    uint8_t old_q, tmp1 = 0xff;

    //printf("div1 arg0=0x%08x arg1=0x%08x M=%d Q=%d T=%d\n", arg0, arg1, M, Q, T);
    old_q = Q;
    if ((0x80000000 & arg1) != 0)
	SETQ;
    else
	CLRQ;
    tmp2 = arg0;
    arg1 <<= 1;
    arg1 |= T;
    switch (old_q) {
    case 0:
	switch (M) {
	case 0:
	    tmp0 = arg1;
	    arg1 -= tmp2;
	    tmp1 = arg1 > tmp0;
	    switch (Q) {
	    case 0:
		if (tmp1)
		    SETQ;
		else
		    CLRQ;
		break;
	    case 1:
		if (tmp1 == 0)
		    SETQ;
		else
		    CLRQ;
		break;
	    }
	    break;
	case 1:
	    tmp0 = arg1;
	    arg1 += tmp2;
	    tmp1 = arg1 < tmp0;
	    switch (Q) {
	    case 0:
		if (tmp1 == 0)
		    SETQ;
		else
		    CLRQ;
		break;
	    case 1:
		if (tmp1)
		    SETQ;
		else
		    CLRQ;
		break;
	    }
	    break;
	}
	break;
    case 1:
	switch (M) {
	case 0:
	    tmp0 = arg1;
	    arg1 += tmp2;
	    tmp1 = arg1 < tmp0;
	    switch (Q) {
	    case 0:
		if (tmp1)
		    SETQ;
		else
		    CLRQ;
		break;
	    case 1:
		if (tmp1 == 0)
		    SETQ;
		else
		    CLRQ;
		break;
	    }
	    break;
	case 1:
	    tmp0 = arg1;
	    arg1 -= tmp2;
	    tmp1 = arg1 > tmp0;
	    switch (Q) {
	    case 0:
		if (tmp1 == 0)
		    SETQ;
		else
		    CLRQ;
		break;
	    case 1:
		if (tmp1)
		    SETQ;
		else
		    CLRQ;
		break;
	    }
	    break;
	}
	break;
    }
    if (Q == M)
	SETT;
    else
	CLRT;
    //printf("Output: arg1=0x%08x M=%d Q=%d T=%d\n", arg1, M, Q, T);
    return arg1;
}

void helper_macl(uint32_t arg0, uint32_t arg1)
{
    int64_t res;

    res = ((uint64_t) env->mach << 32) | env->macl;
    res += (int64_t) (int32_t) arg0 *(int64_t) (int32_t) arg1;
    env->mach = (res >> 32) & 0xffffffff;
    env->macl = res & 0xffffffff;
    if (env->sr & SR_S) {
	if (res < 0)
	    env->mach |= 0xffff0000;
	else
	    env->mach &= 0x00007fff;
    }
}

void helper_macw(uint32_t arg0, uint32_t arg1)
{
    int64_t res;

    res = ((uint64_t) env->mach << 32) | env->macl;
    res += (int64_t) (int16_t) arg0 *(int64_t) (int16_t) arg1;
    env->mach = (res >> 32) & 0xffffffff;
    env->macl = res & 0xffffffff;
    if (env->sr & SR_S) {
	if (res < -0x80000000) {
	    env->mach = 1;
	    env->macl = 0x80000000;
	} else if (res > 0x000000007fffffff) {
	    env->mach = 1;
	    env->macl = 0x7fffffff;
	}
    }
}

uint32_t helper_negc(uint32_t arg)
{
    uint32_t temp;

    temp = -arg;
    arg = temp - (env->sr & SR_T);
    if (0 < temp)
	env->sr |= SR_T;
    else
	env->sr &= ~SR_T;
    if (temp < arg)
	env->sr |= SR_T;
    return arg;
}

uint32_t helper_subc(uint32_t arg0, uint32_t arg1)
{
    uint32_t tmp0, tmp1;

    tmp1 = arg1 - arg0;
    tmp0 = arg1;
    arg1 = tmp1 - (env->sr & SR_T);
    if (tmp0 < tmp1)
	env->sr |= SR_T;
    else
	env->sr &= ~SR_T;
    if (tmp1 < arg1)
	env->sr |= SR_T;
    return arg1;
}

uint32_t helper_subv(uint32_t arg0, uint32_t arg1)
{
    int32_t dest, src, ans;

    if ((int32_t) arg1 >= 0)
	dest = 0;
    else
	dest = 1;
    if ((int32_t) arg0 >= 0)
	src = 0;
    else
	src = 1;
    src += dest;
    arg1 -= arg0;
    if ((int32_t) arg1 >= 0)
	ans = 0;
    else
	ans = 1;
    ans += dest;
    if (src == 1) {
	if (ans == 1)
	    env->sr |= SR_T;
	else
	    env->sr &= ~SR_T;
    } else
	env->sr &= ~SR_T;
    return arg1;
}

void helper_ld_fpscr(uint32_t val)
{
    env->fpscr = val & 0x003fffff;
    if (val & 0x01)
	set_float_rounding_mode(float_round_to_zero, &env->fp_status);
    else
	set_float_rounding_mode(float_round_nearest_even, &env->fp_status);
}
