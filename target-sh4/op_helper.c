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
#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/helper-proto.h"
#include "exec/cpu_ldst.h"

#ifndef CONFIG_USER_ONLY

void tlb_fill(CPUState *cs, target_ulong addr, int is_write, int mmu_idx,
              uintptr_t retaddr)
{
    int ret;

    ret = superh_cpu_handle_mmu_fault(cs, addr, is_write, mmu_idx);
    if (ret) {
        /* now we have a real cpu fault */
        if (retaddr) {
            cpu_restore_state(cs, retaddr);
        }
        cpu_loop_exit(cs);
    }
}

#endif

void helper_ldtlb(CPUSH4State *env)
{
#ifdef CONFIG_USER_ONLY
    SuperHCPU *cpu = sh_env_get_cpu(env);

    /* XXXXX */
    cpu_abort(CPU(cpu), "Unhandled ldtlb");
#else
    cpu_load_tlb(env);
#endif
}

static inline void QEMU_NORETURN raise_exception(CPUSH4State *env, int index,
                                                 uintptr_t retaddr)
{
    CPUState *cs = CPU(sh_env_get_cpu(env));

    cs->exception_index = index;
    if (retaddr) {
        cpu_restore_state(cs, retaddr);
    }
    cpu_loop_exit(cs);
}

void helper_raise_illegal_instruction(CPUSH4State *env)
{
    raise_exception(env, 0x180, 0);
}

void helper_raise_slot_illegal_instruction(CPUSH4State *env)
{
    raise_exception(env, 0x1a0, 0);
}

void helper_raise_fpu_disable(CPUSH4State *env)
{
    raise_exception(env, 0x800, 0);
}

void helper_raise_slot_fpu_disable(CPUSH4State *env)
{
    raise_exception(env, 0x820, 0);
}

void helper_debug(CPUSH4State *env)
{
    raise_exception(env, EXCP_DEBUG, 0);
}

void helper_sleep(CPUSH4State *env)
{
    CPUState *cs = CPU(sh_env_get_cpu(env));

    cs->halted = 1;
    env->in_sleep = 1;
    raise_exception(env, EXCP_HLT, 0);
}

void helper_trapa(CPUSH4State *env, uint32_t tra)
{
    env->tra = tra << 2;
    raise_exception(env, 0x160, 0);
}

void helper_movcal(CPUSH4State *env, uint32_t address, uint32_t value)
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

void helper_discard_movcal_backup(CPUSH4State *env)
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

void helper_ocbi(CPUSH4State *env, uint32_t address)
{
    memory_content **current = &(env->movcal_backup);
    while (*current)
    {
	uint32_t a = (*current)->address;
	if ((a & ~0x1F) == (address & ~0x1F))
	{
	    memory_content *next = (*current)->next;
            cpu_stl_data(env, a, (*current)->value);
	    
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

void helper_macl(CPUSH4State *env, uint32_t arg0, uint32_t arg1)
{
    int64_t res;

    res = ((uint64_t) env->mach << 32) | env->macl;
    res += (int64_t) (int32_t) arg0 *(int64_t) (int32_t) arg1;
    env->mach = (res >> 32) & 0xffffffff;
    env->macl = res & 0xffffffff;
    if (env->sr & (1u << SR_S)) {
	if (res < 0)
	    env->mach |= 0xffff0000;
	else
	    env->mach &= 0x00007fff;
    }
}

void helper_macw(CPUSH4State *env, uint32_t arg0, uint32_t arg1)
{
    int64_t res;

    res = ((uint64_t) env->mach << 32) | env->macl;
    res += (int64_t) (int16_t) arg0 *(int64_t) (int16_t) arg1;
    env->mach = (res >> 32) & 0xffffffff;
    env->macl = res & 0xffffffff;
    if (env->sr & (1u << SR_S)) {
	if (res < -0x80000000) {
	    env->mach = 1;
	    env->macl = 0x80000000;
	} else if (res > 0x000000007fffffff) {
	    env->mach = 1;
	    env->macl = 0x7fffffff;
	}
    }
}

void helper_ld_fpscr(CPUSH4State *env, uint32_t val)
{
    env->fpscr = val & FPSCR_MASK;
    if ((val & FPSCR_RM_MASK) == FPSCR_RM_ZERO) {
	set_float_rounding_mode(float_round_to_zero, &env->fp_status);
    } else {
	set_float_rounding_mode(float_round_nearest_even, &env->fp_status);
    }
    set_flush_to_zero((val & FPSCR_DN) != 0, &env->fp_status);
}

static void update_fpscr(CPUSH4State *env, uintptr_t retaddr)
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
            raise_exception(env, 0x120, retaddr);
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

float32 helper_fadd_FT(CPUSH4State *env, float32 t0, float32 t1)
{
    set_float_exception_flags(0, &env->fp_status);
    t0 = float32_add(t0, t1, &env->fp_status);
    update_fpscr(env, GETPC());
    return t0;
}

float64 helper_fadd_DT(CPUSH4State *env, float64 t0, float64 t1)
{
    set_float_exception_flags(0, &env->fp_status);
    t0 = float64_add(t0, t1, &env->fp_status);
    update_fpscr(env, GETPC());
    return t0;
}

void helper_fcmp_eq_FT(CPUSH4State *env, float32 t0, float32 t1)
{
    int relation;

    set_float_exception_flags(0, &env->fp_status);
    relation = float32_compare(t0, t1, &env->fp_status);
    if (unlikely(relation == float_relation_unordered)) {
        update_fpscr(env, GETPC());
    } else {
        env->sr_t = (relation == float_relation_equal);
    }
}

void helper_fcmp_eq_DT(CPUSH4State *env, float64 t0, float64 t1)
{
    int relation;

    set_float_exception_flags(0, &env->fp_status);
    relation = float64_compare(t0, t1, &env->fp_status);
    if (unlikely(relation == float_relation_unordered)) {
        update_fpscr(env, GETPC());
    } else {
        env->sr_t = (relation == float_relation_equal);
    }
}

void helper_fcmp_gt_FT(CPUSH4State *env, float32 t0, float32 t1)
{
    int relation;

    set_float_exception_flags(0, &env->fp_status);
    relation = float32_compare(t0, t1, &env->fp_status);
    if (unlikely(relation == float_relation_unordered)) {
        update_fpscr(env, GETPC());
    } else {
        env->sr_t = (relation == float_relation_greater);
    }
}

void helper_fcmp_gt_DT(CPUSH4State *env, float64 t0, float64 t1)
{
    int relation;

    set_float_exception_flags(0, &env->fp_status);
    relation = float64_compare(t0, t1, &env->fp_status);
    if (unlikely(relation == float_relation_unordered)) {
        update_fpscr(env, GETPC());
    } else {
        env->sr_t = (relation == float_relation_greater);
    }
}

float64 helper_fcnvsd_FT_DT(CPUSH4State *env, float32 t0)
{
    float64 ret;
    set_float_exception_flags(0, &env->fp_status);
    ret = float32_to_float64(t0, &env->fp_status);
    update_fpscr(env, GETPC());
    return ret;
}

float32 helper_fcnvds_DT_FT(CPUSH4State *env, float64 t0)
{
    float32 ret;
    set_float_exception_flags(0, &env->fp_status);
    ret = float64_to_float32(t0, &env->fp_status);
    update_fpscr(env, GETPC());
    return ret;
}

float32 helper_fdiv_FT(CPUSH4State *env, float32 t0, float32 t1)
{
    set_float_exception_flags(0, &env->fp_status);
    t0 = float32_div(t0, t1, &env->fp_status);
    update_fpscr(env, GETPC());
    return t0;
}

float64 helper_fdiv_DT(CPUSH4State *env, float64 t0, float64 t1)
{
    set_float_exception_flags(0, &env->fp_status);
    t0 = float64_div(t0, t1, &env->fp_status);
    update_fpscr(env, GETPC());
    return t0;
}

float32 helper_float_FT(CPUSH4State *env, uint32_t t0)
{
    float32 ret;
    set_float_exception_flags(0, &env->fp_status);
    ret = int32_to_float32(t0, &env->fp_status);
    update_fpscr(env, GETPC());
    return ret;
}

float64 helper_float_DT(CPUSH4State *env, uint32_t t0)
{
    float64 ret;
    set_float_exception_flags(0, &env->fp_status);
    ret = int32_to_float64(t0, &env->fp_status);
    update_fpscr(env, GETPC());
    return ret;
}

float32 helper_fmac_FT(CPUSH4State *env, float32 t0, float32 t1, float32 t2)
{
    set_float_exception_flags(0, &env->fp_status);
    t0 = float32_muladd(t0, t1, t2, 0, &env->fp_status);
    update_fpscr(env, GETPC());
    return t0;
}

float32 helper_fmul_FT(CPUSH4State *env, float32 t0, float32 t1)
{
    set_float_exception_flags(0, &env->fp_status);
    t0 = float32_mul(t0, t1, &env->fp_status);
    update_fpscr(env, GETPC());
    return t0;
}

float64 helper_fmul_DT(CPUSH4State *env, float64 t0, float64 t1)
{
    set_float_exception_flags(0, &env->fp_status);
    t0 = float64_mul(t0, t1, &env->fp_status);
    update_fpscr(env, GETPC());
    return t0;
}

float32 helper_fneg_T(float32 t0)
{
    return float32_chs(t0);
}

float32 helper_fsqrt_FT(CPUSH4State *env, float32 t0)
{
    set_float_exception_flags(0, &env->fp_status);
    t0 = float32_sqrt(t0, &env->fp_status);
    update_fpscr(env, GETPC());
    return t0;
}

float64 helper_fsqrt_DT(CPUSH4State *env, float64 t0)
{
    set_float_exception_flags(0, &env->fp_status);
    t0 = float64_sqrt(t0, &env->fp_status);
    update_fpscr(env, GETPC());
    return t0;
}

float32 helper_fsub_FT(CPUSH4State *env, float32 t0, float32 t1)
{
    set_float_exception_flags(0, &env->fp_status);
    t0 = float32_sub(t0, t1, &env->fp_status);
    update_fpscr(env, GETPC());
    return t0;
}

float64 helper_fsub_DT(CPUSH4State *env, float64 t0, float64 t1)
{
    set_float_exception_flags(0, &env->fp_status);
    t0 = float64_sub(t0, t1, &env->fp_status);
    update_fpscr(env, GETPC());
    return t0;
}

uint32_t helper_ftrc_FT(CPUSH4State *env, float32 t0)
{
    uint32_t ret;
    set_float_exception_flags(0, &env->fp_status);
    ret = float32_to_int32_round_to_zero(t0, &env->fp_status);
    update_fpscr(env, GETPC());
    return ret;
}

uint32_t helper_ftrc_DT(CPUSH4State *env, float64 t0)
{
    uint32_t ret;
    set_float_exception_flags(0, &env->fp_status);
    ret = float64_to_int32_round_to_zero(t0, &env->fp_status);
    update_fpscr(env, GETPC());
    return ret;
}

void helper_fipr(CPUSH4State *env, uint32_t m, uint32_t n)
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
    update_fpscr(env, GETPC());

    env->fregs[bank + n + 3] = r;
}

void helper_ftrv(CPUSH4State *env, uint32_t n)
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
    update_fpscr(env, GETPC());

    for (i = 0 ; i < 4 ; i++) {
        env->fregs[bank_vector + i] = r[i];
    }
}
