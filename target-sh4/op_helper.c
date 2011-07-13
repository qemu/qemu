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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#include <assert.h>
#include <stdlib.h>
#include "cpu.h"
#include "dyngen-exec.h"
#include "helper.h"

static void cpu_restore_state_from_retaddr(void *retaddr)
{
    TranslationBlock *tb;
    unsigned long pc;

    if (retaddr) {
        pc = (unsigned long) retaddr;
        tb = tb_find_pc(pc);
        if (tb) {
            /* the PC is inside the translated code. It means that we have
               a virtual CPU fault */
            cpu_restore_state(tb, env, pc);
        }
    }
}

#ifndef CONFIG_USER_ONLY
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

void tlb_fill(target_ulong addr, int is_write, int mmu_idx, void *retaddr)
{
    CPUState *saved_env;
    int ret;

    /* XXX: hack to restore env in all cases, even if not called from
       generated code */
    saved_env = env;
    env = cpu_single_env;
    ret = cpu_sh4_handle_mmu_fault(env, addr, is_write, mmu_idx, 1);
    if (ret) {
        /* now we have a real cpu fault */
        cpu_restore_state_from_retaddr(retaddr);
        cpu_loop_exit(env);
    }
    env = saved_env;
}

#endif

void helper_ldtlb(void)
{
#ifdef CONFIG_USER_ONLY
    /* XXXXX */
    cpu_abort(env, "Unhandled ldtlb");
#else
    cpu_load_tlb(env);
#endif
}

static inline void raise_exception(int index, void *retaddr)
{
    env->exception_index = index;
    cpu_restore_state_from_retaddr(retaddr);
    cpu_loop_exit(env);
}

void helper_raise_illegal_instruction(void)
{
    raise_exception(0x180, GETPC());
}

void helper_raise_slot_illegal_instruction(void)
{
    raise_exception(0x1a0, GETPC());
}

void helper_raise_fpu_disable(void)
{
    raise_exception(0x800, GETPC());
}

void helper_raise_slot_fpu_disable(void)
{
    raise_exception(0x820, GETPC());
}

void helper_debug(void)
{
    env->exception_index = EXCP_DEBUG;
    cpu_loop_exit(env);
}

void helper_sleep(uint32_t next_pc)
{
    env->halted = 1;
    env->in_sleep = 1;
    env->exception_index = EXCP_HLT;
    env->pc = next_pc;
    cpu_loop_exit(env);
}

void helper_trapa(uint32_t tra)
{
    env->tra = tra << 2;
    raise_exception(0x160, GETPC());
}

void helper_movcal(uint32_t address, uint32_t value)
{
    if (cpu_sh4_is_cached (env, address))
    {
	memory_content *r = malloc (sizeof(memory_content));
	r->address = address;
	r->value = value;
	r->next = NULL;

	*(env->movcal_backup_tail) = r;
	env->movcal_backup_tail = &(r->next);
    }
}

void helper_discard_movcal_backup(void)
{
    memory_content *current = env->movcal_backup;

    while(current)
    {
	memory_content *next = current->next;
	free (current);
	env->movcal_backup = current = next;
	if (current == NULL)
	    env->movcal_backup_tail = &(env->movcal_backup);
    } 
}

void helper_ocbi(uint32_t address)
{
    memory_content **current = &(env->movcal_backup);
    while (*current)
    {
	uint32_t a = (*current)->address;
	if ((a & ~0x1F) == (address & ~0x1F))
	{
	    memory_content *next = (*current)->next;
	    stl(a, (*current)->value);
	    
	    if (next == NULL)
	    {
		env->movcal_backup_tail = current;
	    }

	    free (*current);
	    *current = next;
	    break;
	}
    }
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

static inline void set_t(void)
{
    env->sr |= SR_T;
}

static inline void clr_t(void)
{
    env->sr &= ~SR_T;
}

void helper_ld_fpscr(uint32_t val)
{
    env->fpscr = val & FPSCR_MASK;
    if ((val & FPSCR_RM_MASK) == FPSCR_RM_ZERO) {
	set_float_rounding_mode(float_round_to_zero, &env->fp_status);
    } else {
	set_float_rounding_mode(float_round_nearest_even, &env->fp_status);
    }
    set_flush_to_zero((val & FPSCR_DN) != 0, &env->fp_status);
}

static void update_fpscr(void *retaddr)
{
    int xcpt, cause, enable;

    xcpt = get_float_exception_flags(&env->fp_status);

    /* Clear the flag entries */
    env->fpscr &= ~FPSCR_FLAG_MASK;

    if (unlikely(xcpt)) {
        if (xcpt & float_flag_invalid) {
            env->fpscr |= FPSCR_FLAG_V;
        }
        if (xcpt & float_flag_divbyzero) {
            env->fpscr |= FPSCR_FLAG_Z;
        }
        if (xcpt & float_flag_overflow) {
            env->fpscr |= FPSCR_FLAG_O;
        }
        if (xcpt & float_flag_underflow) {
            env->fpscr |= FPSCR_FLAG_U;
        }
        if (xcpt & float_flag_inexact) {
            env->fpscr |= FPSCR_FLAG_I;
        }

        /* Accumulate in cause entries */
        env->fpscr |= (env->fpscr & FPSCR_FLAG_MASK)
                      << (FPSCR_CAUSE_SHIFT - FPSCR_FLAG_SHIFT);

        /* Generate an exception if enabled */
        cause = (env->fpscr & FPSCR_CAUSE_MASK) >> FPSCR_CAUSE_SHIFT;
        enable = (env->fpscr & FPSCR_ENABLE_MASK) >> FPSCR_ENABLE_SHIFT;
        if (cause & enable) {
            cpu_restore_state_from_retaddr(retaddr);
            env->exception_index = 0x120;
            cpu_loop_exit(env);
        }
    }
}

float32 helper_fabs_FT(float32 t0)
{
    return float32_abs(t0);
}

float64 helper_fabs_DT(float64 t0)
{
    return float64_abs(t0);
}

float32 helper_fadd_FT(float32 t0, float32 t1)
{
    set_float_exception_flags(0, &env->fp_status);
    t0 = float32_add(t0, t1, &env->fp_status);
    update_fpscr(GETPC());
    return t0;
}

float64 helper_fadd_DT(float64 t0, float64 t1)
{
    set_float_exception_flags(0, &env->fp_status);
    t0 = float64_add(t0, t1, &env->fp_status);
    update_fpscr(GETPC());
    return t0;
}

void helper_fcmp_eq_FT(float32 t0, float32 t1)
{
    int relation;

    set_float_exception_flags(0, &env->fp_status);
    relation = float32_compare(t0, t1, &env->fp_status);
    if (unlikely(relation == float_relation_unordered)) {
        update_fpscr(GETPC());
    } else if (relation == float_relation_equal) {
	set_t();
    } else {
	clr_t();
    }
}

void helper_fcmp_eq_DT(float64 t0, float64 t1)
{
    int relation;

    set_float_exception_flags(0, &env->fp_status);
    relation = float64_compare(t0, t1, &env->fp_status);
    if (unlikely(relation == float_relation_unordered)) {
        update_fpscr(GETPC());
    } else if (relation == float_relation_equal) {
	set_t();
    } else {
	clr_t();
    }
}

void helper_fcmp_gt_FT(float32 t0, float32 t1)
{
    int relation;

    set_float_exception_flags(0, &env->fp_status);
    relation = float32_compare(t0, t1, &env->fp_status);
    if (unlikely(relation == float_relation_unordered)) {
        update_fpscr(GETPC());
    } else if (relation == float_relation_greater) {
	set_t();
    } else {
	clr_t();
    }
}

void helper_fcmp_gt_DT(float64 t0, float64 t1)
{
    int relation;

    set_float_exception_flags(0, &env->fp_status);
    relation = float64_compare(t0, t1, &env->fp_status);
    if (unlikely(relation == float_relation_unordered)) {
        update_fpscr(GETPC());
    } else if (relation == float_relation_greater) {
	set_t();
    } else {
	clr_t();
    }
}

float64 helper_fcnvsd_FT_DT(float32 t0)
{
    float64 ret;
    set_float_exception_flags(0, &env->fp_status);
    ret = float32_to_float64(t0, &env->fp_status);
    update_fpscr(GETPC());
    return ret;
}

float32 helper_fcnvds_DT_FT(float64 t0)
{
    float32 ret;
    set_float_exception_flags(0, &env->fp_status);
    ret = float64_to_float32(t0, &env->fp_status);
    update_fpscr(GETPC());
    return ret;
}

float32 helper_fdiv_FT(float32 t0, float32 t1)
{
    set_float_exception_flags(0, &env->fp_status);
    t0 = float32_div(t0, t1, &env->fp_status);
    update_fpscr(GETPC());
    return t0;
}

float64 helper_fdiv_DT(float64 t0, float64 t1)
{
    set_float_exception_flags(0, &env->fp_status);
    t0 = float64_div(t0, t1, &env->fp_status);
    update_fpscr(GETPC());
    return t0;
}

float32 helper_float_FT(uint32_t t0)
{
    float32 ret;
    set_float_exception_flags(0, &env->fp_status);
    ret = int32_to_float32(t0, &env->fp_status);
    update_fpscr(GETPC());
    return ret;
}

float64 helper_float_DT(uint32_t t0)
{
    float64 ret;
    set_float_exception_flags(0, &env->fp_status);
    ret = int32_to_float64(t0, &env->fp_status);
    update_fpscr(GETPC());
    return ret;
}

float32 helper_fmac_FT(float32 t0, float32 t1, float32 t2)
{
    set_float_exception_flags(0, &env->fp_status);
    t0 = float32_mul(t0, t1, &env->fp_status);
    t0 = float32_add(t0, t2, &env->fp_status);
    update_fpscr(GETPC());
    return t0;
}

float32 helper_fmul_FT(float32 t0, float32 t1)
{
    set_float_exception_flags(0, &env->fp_status);
    t0 = float32_mul(t0, t1, &env->fp_status);
    update_fpscr(GETPC());
    return t0;
}

float64 helper_fmul_DT(float64 t0, float64 t1)
{
    set_float_exception_flags(0, &env->fp_status);
    t0 = float64_mul(t0, t1, &env->fp_status);
    update_fpscr(GETPC());
    return t0;
}

float32 helper_fneg_T(float32 t0)
{
    return float32_chs(t0);
}

float32 helper_fsqrt_FT(float32 t0)
{
    set_float_exception_flags(0, &env->fp_status);
    t0 = float32_sqrt(t0, &env->fp_status);
    update_fpscr(GETPC());
    return t0;
}

float64 helper_fsqrt_DT(float64 t0)
{
    set_float_exception_flags(0, &env->fp_status);
    t0 = float64_sqrt(t0, &env->fp_status);
    update_fpscr(GETPC());
    return t0;
}

float32 helper_fsub_FT(float32 t0, float32 t1)
{
    set_float_exception_flags(0, &env->fp_status);
    t0 = float32_sub(t0, t1, &env->fp_status);
    update_fpscr(GETPC());
    return t0;
}

float64 helper_fsub_DT(float64 t0, float64 t1)
{
    set_float_exception_flags(0, &env->fp_status);
    t0 = float64_sub(t0, t1, &env->fp_status);
    update_fpscr(GETPC());
    return t0;
}

uint32_t helper_ftrc_FT(float32 t0)
{
    uint32_t ret;
    set_float_exception_flags(0, &env->fp_status);
    ret = float32_to_int32_round_to_zero(t0, &env->fp_status);
    update_fpscr(GETPC());
    return ret;
}

uint32_t helper_ftrc_DT(float64 t0)
{
    uint32_t ret;
    set_float_exception_flags(0, &env->fp_status);
    ret = float64_to_int32_round_to_zero(t0, &env->fp_status);
    update_fpscr(GETPC());
    return ret;
}

void helper_fipr(uint32_t m, uint32_t n)
{
    int bank, i;
    float32 r, p;

    bank = (env->sr & FPSCR_FR) ? 16 : 0;
    r = float32_zero;
    set_float_exception_flags(0, &env->fp_status);

    for (i = 0 ; i < 4 ; i++) {
        p = float32_mul(env->fregs[bank + m + i],
                        env->fregs[bank + n + i],
                        &env->fp_status);
        r = float32_add(r, p, &env->fp_status);
    }
    update_fpscr(GETPC());

    env->fregs[bank + n + 3] = r;
}

void helper_ftrv(uint32_t n)
{
    int bank_matrix, bank_vector;
    int i, j;
    float32 r[4];
    float32 p;

    bank_matrix = (env->sr & FPSCR_FR) ? 0 : 16;
    bank_vector = (env->sr & FPSCR_FR) ? 16 : 0;
    set_float_exception_flags(0, &env->fp_status);
    for (i = 0 ; i < 4 ; i++) {
        r[i] = float32_zero;
        for (j = 0 ; j < 4 ; j++) {
            p = float32_mul(env->fregs[bank_matrix + 4 * j + i],
                            env->fregs[bank_vector + j],
                            &env->fp_status);
            r[i] = float32_add(r[i], p, &env->fp_status);
        }
    }
    update_fpscr(GETPC());

    for (i = 0 ; i < 4 ; i++) {
        env->fregs[bank_vector + i] = r[i];
    }
}
