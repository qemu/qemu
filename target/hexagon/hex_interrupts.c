/*
 * Copyright(c) 2022-2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "cpu.h"
#include "hex_interrupts.h"
#include "macros.h"
#include "sys_macros.h"
#include "sysemu/cpus.h"
#include "cpu_helper.h"

static bool hex_is_qualified_for_int(CPUHexagonState *env, int int_num);

static bool get_syscfg_gie(CPUHexagonState *env)
{
    target_ulong syscfg = ARCH_GET_SYSTEM_REG(env, HEX_SREG_SYSCFG);
    return GET_SYSCFG_FIELD(SYSCFG_GIE, syscfg);
}

static bool get_ssr_ex(CPUHexagonState *env)
{
    target_ulong ssr = ARCH_GET_SYSTEM_REG(env, HEX_SREG_SSR);
    return GET_SSR_FIELD(SSR_EX, ssr);
}

static bool get_ssr_ie(CPUHexagonState *env)
{
    target_ulong ssr = ARCH_GET_SYSTEM_REG(env, HEX_SREG_SSR);
    return GET_SSR_FIELD(SSR_IE, ssr);
}

/* Do these together so we only have to call hexagon_modify_ssr once */
static void set_ssr_ex_cause(CPUHexagonState *env, int ex, uint32_t cause)
{
    target_ulong old = ARCH_GET_SYSTEM_REG(env, HEX_SREG_SSR);
    SET_SYSTEM_FIELD(env, HEX_SREG_SSR, SSR_EX, ex);
    SET_SYSTEM_FIELD(env, HEX_SREG_SSR, SSR_CAUSE, cause);
    target_ulong new = ARCH_GET_SYSTEM_REG(env, HEX_SREG_SSR);
    hexagon_modify_ssr(env, new, old);
}

static bool get_iad_bit(CPUHexagonState *env, int int_num)
{
    target_ulong ipendad = ARCH_GET_SYSTEM_REG(env, HEX_SREG_IPENDAD);
    target_ulong iad = GET_FIELD(IPENDAD_IAD, ipendad);
    return extract32(iad, int_num, 1);
}

static void set_iad_bit(CPUHexagonState *env, int int_num, int val)
{
    target_ulong ipendad = ARCH_GET_SYSTEM_REG(env, HEX_SREG_IPENDAD);
    target_ulong iad = GET_FIELD(IPENDAD_IAD, ipendad);
    iad = deposit32(iad, int_num, 1, val);
    fSET_FIELD(ipendad, IPENDAD_IAD, iad);
    ARCH_SET_SYSTEM_REG(env, HEX_SREG_IPENDAD, ipendad);
}

static uint32_t get_ipend(CPUHexagonState *env)
{
    target_ulong ipendad = ARCH_GET_SYSTEM_REG(env, HEX_SREG_IPENDAD);
    return GET_FIELD(IPENDAD_IPEND, ipendad);
}

static inline bool get_ipend_bit(CPUHexagonState *env, int int_num)
{
    target_ulong ipendad = ARCH_GET_SYSTEM_REG(env, HEX_SREG_IPENDAD);
    target_ulong ipend = GET_FIELD(IPENDAD_IPEND, ipendad);
    return extract32(ipend, int_num, 1);
}

static void clear_ipend(CPUHexagonState *env, uint32_t mask)
{
    target_ulong ipendad = ARCH_GET_SYSTEM_REG(env, HEX_SREG_IPENDAD);
    target_ulong ipend = GET_FIELD(IPENDAD_IPEND, ipendad);
    ipend &= ~mask;
    fSET_FIELD(ipendad, IPENDAD_IPEND, ipend);
    ARCH_SET_SYSTEM_REG(env, HEX_SREG_IPENDAD, ipendad);
}

static void set_ipend(CPUHexagonState *env, uint32_t mask)
{
    target_ulong ipendad = ARCH_GET_SYSTEM_REG(env, HEX_SREG_IPENDAD);
    target_ulong ipend = GET_FIELD(IPENDAD_IPEND, ipendad);
    ipend |= mask;
    fSET_FIELD(ipendad, IPENDAD_IPEND, ipend);
    ARCH_SET_SYSTEM_REG(env, HEX_SREG_IPENDAD, ipendad);
}

static void set_ipend_bit(CPUHexagonState *env, int int_num, int val)
{
    target_ulong ipendad = ARCH_GET_SYSTEM_REG(env, HEX_SREG_IPENDAD);
    target_ulong ipend = GET_FIELD(IPENDAD_IPEND, ipendad);
    ipend = deposit32(ipend, int_num, 1, val);
    fSET_FIELD(ipendad, IPENDAD_IPEND, ipend);
    ARCH_SET_SYSTEM_REG(env, HEX_SREG_IPENDAD, ipendad);
}

static bool get_imask_bit(CPUHexagonState *env, int int_num)
{
    target_ulong imask = ARCH_GET_SYSTEM_REG(env, HEX_SREG_IMASK);
    return extract32(imask, int_num, 1);
}

static uint32_t get_prio(CPUHexagonState *env)
{
    target_ulong stid = ARCH_GET_SYSTEM_REG(env, HEX_SREG_STID);
    return extract32(stid, reg_field_info[STID_PRIO].offset,
                     reg_field_info[STID_PRIO].width);
}

static void set_elr(CPUHexagonState *env, target_ulong val)
{
    ARCH_SET_SYSTEM_REG(env, HEX_SREG_ELR, val);
}

static bool get_schedcfgen(CPUHexagonState *env)
{
    target_ulong schedcfg = ARCH_GET_SYSTEM_REG(env, HEX_SREG_SCHEDCFG);
    return extract32(schedcfg, reg_field_info[SCHEDCFG_EN].offset,
                     reg_field_info[SCHEDCFG_EN].width);
}

static bool is_lowest_prio(CPUHexagonState *env, int int_num)
{
    uint32_t my_prio = get_prio(env);
    CPUState *cs;

    CPU_FOREACH(cs) {
        HexagonCPU *hex_cpu = HEXAGON_CPU(cs);
        CPUHexagonState *hex_env = &hex_cpu->env;
        if (!hex_is_qualified_for_int(hex_env, int_num)) {
            continue;
        }

        /* Note that lower values indicate *higher* priority */
        if (my_prio < get_prio(hex_env)) {
            return false;
        }
    }
    return true;
}

static bool hex_is_qualified_for_int(CPUHexagonState *env, int int_num)
{
    bool syscfg_gie = get_syscfg_gie(env);
    bool iad = get_iad_bit(env, int_num);
    bool ssr_ie = get_ssr_ie(env);
    bool ssr_ex = get_ssr_ex(env);
    bool imask = get_imask_bit(env, int_num);

    return syscfg_gie && !iad && ssr_ie && !ssr_ex && !imask;
}

static void clear_pending_locks(CPUHexagonState *env)
{
    g_assert(bql_locked());
    if (env->k0_lock_state == HEX_LOCK_WAITING) {
        env->k0_lock_state = HEX_LOCK_UNLOCKED;
    }
    if (env->tlb_lock_state == HEX_LOCK_WAITING) {
        env->tlb_lock_state = HEX_LOCK_UNLOCKED;
    }
}

static bool should_not_exec(CPUHexagonState *env)
{
    return (get_exe_mode(env) == HEX_EXE_MODE_WAIT);
}

static void restore_state(CPUHexagonState *env, bool int_accepted)
{
    CPUState *cs = env_cpu(env);
    cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD | CPU_INTERRUPT_SWI);
    if (!int_accepted && should_not_exec(env)) {
        cpu_interrupt(cs, CPU_INTERRUPT_HALT);
    }
}

static void hex_accept_int(CPUHexagonState *env, int int_num)
{
    CPUState *cs = env_cpu(env);
    target_ulong evb = ARCH_GET_SYSTEM_REG(env, HEX_SREG_EVB);
    const int exe_mode = get_exe_mode(env);
    const bool in_wait_mode = exe_mode == HEX_EXE_MODE_WAIT;

    set_ipend_bit(env, int_num, 0);
    set_iad_bit(env, int_num, 1);
    set_ssr_ex_cause(env, 1, HEX_CAUSE_INT0 | int_num);
    cs->exception_index = HEX_EVENT_INT0 + int_num;
    env->cause_code = HEX_EVENT_INT0 + int_num;
    clear_pending_locks(env);
    if (in_wait_mode) {
        qemu_log_mask(CPU_LOG_INT,
            "%s: thread %d resuming, exiting WAIT mode\n",
            __func__, env->threadId);
        set_elr(env, env->wait_next_pc);
        clear_wait_mode(env);
        cs->halted = false;
    } else if (env->k0_lock_state == HEX_LOCK_WAITING) {
        g_assert_not_reached();
    } else {
        set_elr(env, env->gpr[HEX_REG_PC]);
    }
    env->gpr[HEX_REG_PC] = evb | (cs->exception_index << 2);
    if (get_ipend(env) == 0) {
        restore_state(env, true);
    }
}


bool hex_check_interrupts(CPUHexagonState *env)
{
    CPUState *cs = env_cpu(env);
    bool int_handled = false;
    bool ssr_ex = get_ssr_ex(env);
    int max_ints;
    bool schedcfgen;

    /* Early exit if nothing pending */
    if (get_ipend(env) == 0) {
        restore_state(env, false);
        return false;
    }

    max_ints = reg_field_info[IPENDAD_IPEND].width;
    BQL_LOCK_GUARD();
    /* Only check priorities when schedcfgen is set */
    schedcfgen = get_schedcfgen(env);
    for (int i = 0; i < max_ints; i++) {
        if (!get_iad_bit(env, i) && get_ipend_bit(env, i)) {
            qemu_log_mask(CPU_LOG_INT,
                          "%s: thread[%d] pc = 0x%x found int %d\n", __func__,
                          env->threadId, env->gpr[HEX_REG_PC], i);
            if (hex_is_qualified_for_int(env, i) &&
                (!schedcfgen || is_lowest_prio(env, i))) {
                qemu_log_mask(CPU_LOG_INT, "%s: thread[%d] int %d handled_\n",
                    __func__, env->threadId, i);
                hex_accept_int(env, i);
                int_handled = true;
                break;
            }
            bool syscfg_gie = get_syscfg_gie(env);
            bool iad = get_iad_bit(env, i);
            bool ssr_ie = get_ssr_ie(env);
            bool imask = get_imask_bit(env, i);

            qemu_log_mask(CPU_LOG_INT,
                          "%s: thread[%d] int %d not handled, qualified: %d, "
                          "schedcfg_en: %d, low prio %d\n",
                          __func__, env->threadId, i,
                          hex_is_qualified_for_int(env, i), schedcfgen,
                          is_lowest_prio(env, i));

            qemu_log_mask(CPU_LOG_INT,
                          "%s: thread[%d] int %d not handled, GIE %d, iad %d, "
                          "SSR:IE %d, SSR:EX: %d, imask bit %d\n",
                          __func__, env->threadId, i, syscfg_gie, iad, ssr_ie,
                          ssr_ex, imask);
        }
    }

    /*
     * If we didn't handle the interrupt and it wasn't
     * because we were in EX state, then we won't be able
     * to execute the interrupt on this CPU unless something
     * changes in the CPU state.  Clear the interrupt_request bits
     * while preserving the IPEND bits, and we can re-assert the
     * interrupt_request bit(s) when we execute one of those instructions.
     */
    if (!int_handled && !ssr_ex) {
        restore_state(env, int_handled);
    } else if (int_handled) {
        assert(!cs->halted);
    }

    return int_handled;
}

void hex_clear_interrupts(CPUHexagonState *env, uint32_t mask, uint32_t type)
{
    if (mask == 0) {
        return;
    }

    /*
     * Notify all CPUs that the interrupt has happened
     */
    BQL_LOCK_GUARD();
    clear_ipend(env, mask);
    hex_interrupt_update(env);
}

void hex_raise_interrupts(CPUHexagonState *env, uint32_t mask, uint32_t type)
{
    g_assert(bql_locked());
    if (mask == 0) {
        return;
    }

    /*
     * Notify all CPUs that the interrupt has happened
     */
    set_ipend(env, mask);
    hex_interrupt_update(env);
}

void hex_interrupt_update(CPUHexagonState *env)
{
    CPUState *cs;

    g_assert(bql_locked());
    if (get_ipend(env) != 0) {
        CPU_FOREACH(cs) {
            HexagonCPU *hex_cpu = HEXAGON_CPU(cs);
            CPUHexagonState *hex_env = &hex_cpu->env;
            const int exe_mode = get_exe_mode(hex_env);
            if (exe_mode != HEX_EXE_MODE_OFF) {
                cs->interrupt_request |= CPU_INTERRUPT_SWI;
                cpu_resume(cs);
            }
        }
    }
}
