/*
 * Helpers for HPPA instructions.
 *
 * Copyright (c) 2016 Richard Henderson <rth@twiddle.net>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "exec/cpu_ldst.h"
#include "qemu/timer.h"
#include "sysemu/runstate.h"
#include "fpu/softfloat.h"
#include "trace.h"

void QEMU_NORETURN HELPER(excp)(CPUHPPAState *env, int excp)
{
    CPUState *cs = env_cpu(env);

    cs->exception_index = excp;
    cpu_loop_exit(cs);
}

void QEMU_NORETURN hppa_dynamic_excp(CPUHPPAState *env, int excp, uintptr_t ra)
{
    CPUState *cs = env_cpu(env);

    cs->exception_index = excp;
    cpu_loop_exit_restore(cs, ra);
}

void HELPER(tsv)(CPUHPPAState *env, target_ureg cond)
{
    if (unlikely((target_sreg)cond < 0)) {
        hppa_dynamic_excp(env, EXCP_OVERFLOW, GETPC());
    }
}

void HELPER(tcond)(CPUHPPAState *env, target_ureg cond)
{
    if (unlikely(cond)) {
        hppa_dynamic_excp(env, EXCP_COND, GETPC());
    }
}

static void atomic_store_3(CPUHPPAState *env, target_ulong addr, uint32_t val,
                           uint32_t mask, uintptr_t ra)
{
#ifdef CONFIG_USER_ONLY
    uint32_t old, new, cmp;

    uint32_t *haddr = g2h(env_cpu(env), addr - 1);
    old = *haddr;
    while (1) {
        new = (old & ~mask) | (val & mask);
        cmp = qatomic_cmpxchg(haddr, old, new);
        if (cmp == old) {
            return;
        }
        old = cmp;
    }
#else
    /* FIXME -- we can do better.  */
    cpu_loop_exit_atomic(env_cpu(env), ra);
#endif
}

static void do_stby_b(CPUHPPAState *env, target_ulong addr, target_ureg val,
                      bool parallel, uintptr_t ra)
{
    switch (addr & 3) {
    case 3:
        cpu_stb_data_ra(env, addr, val, ra);
        break;
    case 2:
        cpu_stw_data_ra(env, addr, val, ra);
        break;
    case 1:
        /* The 3 byte store must appear atomic.  */
        if (parallel) {
            atomic_store_3(env, addr, val, 0x00ffffffu, ra);
        } else {
            cpu_stb_data_ra(env, addr, val >> 16, ra);
            cpu_stw_data_ra(env, addr + 1, val, ra);
        }
        break;
    default:
        cpu_stl_data_ra(env, addr, val, ra);
        break;
    }
}

void HELPER(stby_b)(CPUHPPAState *env, target_ulong addr, target_ureg val)
{
    do_stby_b(env, addr, val, false, GETPC());
}

void HELPER(stby_b_parallel)(CPUHPPAState *env, target_ulong addr,
                             target_ureg val)
{
    do_stby_b(env, addr, val, true, GETPC());
}

static void do_stby_e(CPUHPPAState *env, target_ulong addr, target_ureg val,
                      bool parallel, uintptr_t ra)
{
    switch (addr & 3) {
    case 3:
        /* The 3 byte store must appear atomic.  */
        if (parallel) {
            atomic_store_3(env, addr - 3, val, 0xffffff00u, ra);
        } else {
            cpu_stw_data_ra(env, addr - 3, val >> 16, ra);
            cpu_stb_data_ra(env, addr - 1, val >> 8, ra);
        }
        break;
    case 2:
        cpu_stw_data_ra(env, addr - 2, val >> 16, ra);
        break;
    case 1:
        cpu_stb_data_ra(env, addr - 1, val >> 24, ra);
        break;
    default:
        /* Nothing is stored, but protection is checked and the
           cacheline is marked dirty.  */
        probe_write(env, addr, 0, cpu_mmu_index(env, 0), ra);
        break;
    }
}

void HELPER(stby_e)(CPUHPPAState *env, target_ulong addr, target_ureg val)
{
    do_stby_e(env, addr, val, false, GETPC());
}

void HELPER(stby_e_parallel)(CPUHPPAState *env, target_ulong addr,
                             target_ureg val)
{
    do_stby_e(env, addr, val, true, GETPC());
}

void HELPER(ldc_check)(target_ulong addr)
{
    if (unlikely(addr & 0xf)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Undefined ldc to unaligned address mod 16: "
                      TARGET_FMT_lx "\n", addr);
    }
}

target_ureg HELPER(probe)(CPUHPPAState *env, target_ulong addr,
                          uint32_t level, uint32_t want)
{
#ifdef CONFIG_USER_ONLY
    return page_check_range(addr, 1, want);
#else
    int prot, excp;
    hwaddr phys;

    trace_hppa_tlb_probe(addr, level, want);
    /* Fail if the requested privilege level is higher than current.  */
    if (level < (env->iaoq_f & 3)) {
        return 0;
    }

    excp = hppa_get_physical_address(env, addr, level, 0, &phys, &prot);
    if (excp >= 0) {
        if (env->psw & PSW_Q) {
            /* ??? Needs tweaking for hppa64.  */
            env->cr[CR_IOR] = addr;
            env->cr[CR_ISR] = addr >> 32;
        }
        if (excp == EXCP_DTLB_MISS) {
            excp = EXCP_NA_DTLB_MISS;
        }
        hppa_dynamic_excp(env, excp, GETPC());
    }
    return (want & prot) != 0;
#endif
}

void HELPER(loaded_fr0)(CPUHPPAState *env)
{
    uint32_t shadow = env->fr[0] >> 32;
    int rm, d;

    env->fr0_shadow = shadow;

    switch (extract32(shadow, 9, 2)) {
    default:
        rm = float_round_nearest_even;
        break;
    case 1:
        rm = float_round_to_zero;
        break;
    case 2:
        rm = float_round_up;
        break;
    case 3:
        rm = float_round_down;
        break;
    }
    set_float_rounding_mode(rm, &env->fp_status);

    d = extract32(shadow, 5, 1);
    set_flush_to_zero(d, &env->fp_status);
    set_flush_inputs_to_zero(d, &env->fp_status);
}

void cpu_hppa_loaded_fr0(CPUHPPAState *env)
{
    helper_loaded_fr0(env);
}

#define CONVERT_BIT(X, SRC, DST)        \
    ((SRC) > (DST)                      \
     ? (X) / ((SRC) / (DST)) & (DST)    \
     : ((X) & (SRC)) * ((DST) / (SRC)))

static void update_fr0_op(CPUHPPAState *env, uintptr_t ra)
{
    uint32_t soft_exp = get_float_exception_flags(&env->fp_status);
    uint32_t hard_exp = 0;
    uint32_t shadow = env->fr0_shadow;

    if (likely(soft_exp == 0)) {
        env->fr[0] = (uint64_t)shadow << 32;
        return;
    }
    set_float_exception_flags(0, &env->fp_status);

    hard_exp |= CONVERT_BIT(soft_exp, float_flag_inexact,   1u << 0);
    hard_exp |= CONVERT_BIT(soft_exp, float_flag_underflow, 1u << 1);
    hard_exp |= CONVERT_BIT(soft_exp, float_flag_overflow,  1u << 2);
    hard_exp |= CONVERT_BIT(soft_exp, float_flag_divbyzero, 1u << 3);
    hard_exp |= CONVERT_BIT(soft_exp, float_flag_invalid,   1u << 4);
    shadow |= hard_exp << (32 - 5);
    env->fr0_shadow = shadow;
    env->fr[0] = (uint64_t)shadow << 32;

    if (hard_exp & shadow) {
        hppa_dynamic_excp(env, EXCP_ASSIST, ra);
    }
}

float32 HELPER(fsqrt_s)(CPUHPPAState *env, float32 arg)
{
    float32 ret = float32_sqrt(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

float32 HELPER(frnd_s)(CPUHPPAState *env, float32 arg)
{
    float32 ret = float32_round_to_int(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

float32 HELPER(fadd_s)(CPUHPPAState *env, float32 a, float32 b)
{
    float32 ret = float32_add(a, b, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

float32 HELPER(fsub_s)(CPUHPPAState *env, float32 a, float32 b)
{
    float32 ret = float32_sub(a, b, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

float32 HELPER(fmpy_s)(CPUHPPAState *env, float32 a, float32 b)
{
    float32 ret = float32_mul(a, b, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

float32 HELPER(fdiv_s)(CPUHPPAState *env, float32 a, float32 b)
{
    float32 ret = float32_div(a, b, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

float64 HELPER(fsqrt_d)(CPUHPPAState *env, float64 arg)
{
    float64 ret = float64_sqrt(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

float64 HELPER(frnd_d)(CPUHPPAState *env, float64 arg)
{
    float64 ret = float64_round_to_int(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

float64 HELPER(fadd_d)(CPUHPPAState *env, float64 a, float64 b)
{
    float64 ret = float64_add(a, b, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

float64 HELPER(fsub_d)(CPUHPPAState *env, float64 a, float64 b)
{
    float64 ret = float64_sub(a, b, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

float64 HELPER(fmpy_d)(CPUHPPAState *env, float64 a, float64 b)
{
    float64 ret = float64_mul(a, b, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

float64 HELPER(fdiv_d)(CPUHPPAState *env, float64 a, float64 b)
{
    float64 ret = float64_div(a, b, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

float64 HELPER(fcnv_s_d)(CPUHPPAState *env, float32 arg)
{
    float64 ret = float32_to_float64(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

float32 HELPER(fcnv_d_s)(CPUHPPAState *env, float64 arg)
{
    float32 ret = float64_to_float32(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

float32 HELPER(fcnv_w_s)(CPUHPPAState *env, int32_t arg)
{
    float32 ret = int32_to_float32(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

float32 HELPER(fcnv_dw_s)(CPUHPPAState *env, int64_t arg)
{
    float32 ret = int64_to_float32(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

float64 HELPER(fcnv_w_d)(CPUHPPAState *env, int32_t arg)
{
    float64 ret = int32_to_float64(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

float64 HELPER(fcnv_dw_d)(CPUHPPAState *env, int64_t arg)
{
    float64 ret = int64_to_float64(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

int32_t HELPER(fcnv_s_w)(CPUHPPAState *env, float32 arg)
{
    int32_t ret = float32_to_int32(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

int32_t HELPER(fcnv_d_w)(CPUHPPAState *env, float64 arg)
{
    int32_t ret = float64_to_int32(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

int64_t HELPER(fcnv_s_dw)(CPUHPPAState *env, float32 arg)
{
    int64_t ret = float32_to_int64(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

int64_t HELPER(fcnv_d_dw)(CPUHPPAState *env, float64 arg)
{
    int64_t ret = float64_to_int64(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

int32_t HELPER(fcnv_t_s_w)(CPUHPPAState *env, float32 arg)
{
    int32_t ret = float32_to_int32_round_to_zero(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

int32_t HELPER(fcnv_t_d_w)(CPUHPPAState *env, float64 arg)
{
    int32_t ret = float64_to_int32_round_to_zero(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

int64_t HELPER(fcnv_t_s_dw)(CPUHPPAState *env, float32 arg)
{
    int64_t ret = float32_to_int64_round_to_zero(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

int64_t HELPER(fcnv_t_d_dw)(CPUHPPAState *env, float64 arg)
{
    int64_t ret = float64_to_int64_round_to_zero(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

float32 HELPER(fcnv_uw_s)(CPUHPPAState *env, uint32_t arg)
{
    float32 ret = uint32_to_float32(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

float32 HELPER(fcnv_udw_s)(CPUHPPAState *env, uint64_t arg)
{
    float32 ret = uint64_to_float32(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

float64 HELPER(fcnv_uw_d)(CPUHPPAState *env, uint32_t arg)
{
    float64 ret = uint32_to_float64(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

float64 HELPER(fcnv_udw_d)(CPUHPPAState *env, uint64_t arg)
{
    float64 ret = uint64_to_float64(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

uint32_t HELPER(fcnv_s_uw)(CPUHPPAState *env, float32 arg)
{
    uint32_t ret = float32_to_uint32(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

uint32_t HELPER(fcnv_d_uw)(CPUHPPAState *env, float64 arg)
{
    uint32_t ret = float64_to_uint32(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

uint64_t HELPER(fcnv_s_udw)(CPUHPPAState *env, float32 arg)
{
    uint64_t ret = float32_to_uint64(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

uint64_t HELPER(fcnv_d_udw)(CPUHPPAState *env, float64 arg)
{
    uint64_t ret = float64_to_uint64(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

uint32_t HELPER(fcnv_t_s_uw)(CPUHPPAState *env, float32 arg)
{
    uint32_t ret = float32_to_uint32_round_to_zero(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

uint32_t HELPER(fcnv_t_d_uw)(CPUHPPAState *env, float64 arg)
{
    uint32_t ret = float64_to_uint32_round_to_zero(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

uint64_t HELPER(fcnv_t_s_udw)(CPUHPPAState *env, float32 arg)
{
    uint64_t ret = float32_to_uint64_round_to_zero(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

uint64_t HELPER(fcnv_t_d_udw)(CPUHPPAState *env, float64 arg)
{
    uint64_t ret = float64_to_uint64_round_to_zero(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

static void update_fr0_cmp(CPUHPPAState *env, uint32_t y,
                           uint32_t c, FloatRelation r)
{
    uint32_t shadow = env->fr0_shadow;

    switch (r) {
    case float_relation_greater:
        c = extract32(c, 4, 1);
        break;
    case float_relation_less:
        c = extract32(c, 3, 1);
        break;
    case float_relation_equal:
        c = extract32(c, 2, 1);
        break;
    case float_relation_unordered:
        c = extract32(c, 1, 1);
        break;
    default:
        g_assert_not_reached();
    }

    if (y) {
        /* targeted comparison */
        /* set fpsr[ca[y - 1]] to current compare */
        shadow = deposit32(shadow, 21 - (y - 1), 1, c);
    } else {
        /* queued comparison */
        /* shift cq right by one place */
        shadow = deposit32(shadow, 11, 10, extract32(shadow, 12, 10));
        /* move fpsr[c] to fpsr[cq[0]] */
        shadow = deposit32(shadow, 21, 1, extract32(shadow, 26, 1));
        /* set fpsr[c] to current compare */
        shadow = deposit32(shadow, 26, 1, c);
    }

    env->fr0_shadow = shadow;
    env->fr[0] = (uint64_t)shadow << 32;
}

void HELPER(fcmp_s)(CPUHPPAState *env, float32 a, float32 b,
                    uint32_t y, uint32_t c)
{
    FloatRelation r;
    if (c & 1) {
        r = float32_compare(a, b, &env->fp_status);
    } else {
        r = float32_compare_quiet(a, b, &env->fp_status);
    }
    update_fr0_op(env, GETPC());
    update_fr0_cmp(env, y, c, r);
}

void HELPER(fcmp_d)(CPUHPPAState *env, float64 a, float64 b,
                    uint32_t y, uint32_t c)
{
    FloatRelation r;
    if (c & 1) {
        r = float64_compare(a, b, &env->fp_status);
    } else {
        r = float64_compare_quiet(a, b, &env->fp_status);
    }
    update_fr0_op(env, GETPC());
    update_fr0_cmp(env, y, c, r);
}

float32 HELPER(fmpyfadd_s)(CPUHPPAState *env, float32 a, float32 b, float32 c)
{
    float32 ret = float32_muladd(a, b, c, 0, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

float32 HELPER(fmpynfadd_s)(CPUHPPAState *env, float32 a, float32 b, float32 c)
{
    float32 ret = float32_muladd(a, b, c, float_muladd_negate_product,
                                 &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

float64 HELPER(fmpyfadd_d)(CPUHPPAState *env, float64 a, float64 b, float64 c)
{
    float64 ret = float64_muladd(a, b, c, 0, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

float64 HELPER(fmpynfadd_d)(CPUHPPAState *env, float64 a, float64 b, float64 c)
{
    float64 ret = float64_muladd(a, b, c, float_muladd_negate_product,
                                 &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

target_ureg HELPER(read_interval_timer)(void)
{
#ifdef CONFIG_USER_ONLY
    /* In user-mode, QEMU_CLOCK_VIRTUAL doesn't exist.
       Just pass through the host cpu clock ticks.  */
    return cpu_get_host_ticks();
#else
    /* In system mode we have access to a decent high-resolution clock.
       In order to make OS-level time accounting work with the cr16,
       present it with a well-timed clock fixed at 250MHz.  */
    return qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) >> 2;
#endif
}

#ifndef CONFIG_USER_ONLY
void HELPER(write_interval_timer)(CPUHPPAState *env, target_ureg val)
{
    HPPACPU *cpu = env_archcpu(env);
    uint64_t current = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    uint64_t timeout;

    /* Even in 64-bit mode, the comparator is always 32-bit.  But the
       value we expose to the guest is 1/4 of the speed of the clock,
       so moosh in 34 bits.  */
    timeout = deposit64(current, 0, 34, (uint64_t)val << 2);

    /* If the mooshing puts the clock in the past, advance to next round.  */
    if (timeout < current + 1000) {
        timeout += 1ULL << 34;
    }

    cpu->env.cr[CR_IT] = timeout;
    timer_mod(cpu->alarm_timer, timeout);
}

void HELPER(halt)(CPUHPPAState *env)
{
    qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
    helper_excp(env, EXCP_HLT);
}

void HELPER(reset)(CPUHPPAState *env)
{
    qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
    helper_excp(env, EXCP_HLT);
}

target_ureg HELPER(swap_system_mask)(CPUHPPAState *env, target_ureg nsm)
{
    target_ulong psw = env->psw;
    /*
     * Setting the PSW Q bit to 1, if it was not already 1, is an
     * undefined operation.
     *
     * However, HP-UX 10.20 does this with the SSM instruction.
     * Tested this on HP9000/712 and HP9000/785/C3750 and both
     * machines set the Q bit from 0 to 1 without an exception,
     * so let this go without comment.
     */
    env->psw = (psw & ~PSW_SM) | (nsm & PSW_SM);
    return psw & PSW_SM;
}

void HELPER(rfi)(CPUHPPAState *env)
{
    env->iasq_f = (uint64_t)env->cr[CR_IIASQ] << 32;
    env->iasq_b = (uint64_t)env->cr_back[0] << 32;
    env->iaoq_f = env->cr[CR_IIAOQ];
    env->iaoq_b = env->cr_back[1];
    cpu_hppa_put_psw(env, env->cr[CR_IPSW]);
}

void HELPER(rfi_r)(CPUHPPAState *env)
{
    env->gr[1] = env->shadow[0];
    env->gr[8] = env->shadow[1];
    env->gr[9] = env->shadow[2];
    env->gr[16] = env->shadow[3];
    env->gr[17] = env->shadow[4];
    env->gr[24] = env->shadow[5];
    env->gr[25] = env->shadow[6];
    helper_rfi(env);
}
#endif
