/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "cpu_helper.h"
#include "system/cpus.h"
#include "hw/core/boards.h"
#include "hw/hexagon/hexagon.h"
#include "hw/hexagon/hexagon_globalreg.h"
#include "hex_interrupts.h"
#include "hex_mmu.h"
#include "system/runstate.h"
#include "exec/cpu-interrupt.h"
#include "exec/target_page.h"
#include "accel/tcg/cpu-ldst.h"
#include "exec/cputlb.h"
#include "qemu/log.h"
#include "tcg/tcg-op.h"
#include "internal.h"
#include "macros.h"
#include "sys_macros.h"
#include "arch.h"


uint32_t hexagon_get_pmu_counter(CPUHexagonState *cur_env, int index)
{
    g_assert_not_reached();
}

uint64_t hexagon_get_sys_pcycle_count(CPUHexagonState *env)
{
    uint64_t total = 0;
    CPUState *cs;

    g_assert(bql_locked());
    CPU_FOREACH(cs) {
        CPUHexagonState *thread_env = cpu_env(cs);
        total += thread_env->t_cycle_count;
    }
    return total;
}

uint32_t hexagon_get_sys_pcycle_count_high(CPUHexagonState *env)
{
    return (uint32_t)(hexagon_get_sys_pcycle_count(env) >> 32);
}

uint32_t hexagon_get_sys_pcycle_count_low(CPUHexagonState *env)
{
    return (uint32_t)(hexagon_get_sys_pcycle_count(env));
}

void hexagon_set_sys_pcycle_count_high(CPUHexagonState *env, uint32_t val)
{
    uint64_t old;

    g_assert(bql_locked());
    old = hexagon_get_sys_pcycle_count(env);
    old = deposit64(old, 32, 32, val);
    hexagon_set_sys_pcycle_count(env, old);
}

void hexagon_set_sys_pcycle_count_low(CPUHexagonState *env, uint32_t val)
{
    uint64_t old;

    g_assert(bql_locked());
    old = hexagon_get_sys_pcycle_count(env);
    old = deposit64(old, 0, 32, val);
    hexagon_set_sys_pcycle_count(env, old);
}

void hexagon_set_sys_pcycle_count(CPUHexagonState *env, uint64_t val)
{
    CPUState *cs;
    uint64_t total;
    int num_threads;
    int64_t delta, per_thread, remainder;

    g_assert(bql_locked());
    total = hexagon_get_sys_pcycle_count(env);

    /* Count active threads */
    num_threads = 0;
    CPU_FOREACH(cs) {
        num_threads++;
    }
    g_assert(num_threads > 0);

    /*
     * Distribute the delta evenly across all threads.
     * Any remainder goes to the calling thread.
     */
    delta = (int64_t)(val - total);
    per_thread = delta / num_threads;
    remainder = delta - per_thread * num_threads;

    CPU_FOREACH(cs) {
        CPUHexagonState *thread_env = cpu_env(cs);
        thread_env->t_cycle_count += per_thread;
    }
    env->t_cycle_count += remainder;
}

static void hexagon_resume_thread(CPUHexagonState *env)
{
    CPUState *cs = env_cpu(env);
    clear_wait_mode(env);
    /*
     * The wait instruction keeps the PC pointing to itself
     * so that it has an opportunity to check for interrupts.
     *
     * When we come out of wait mode, adjust the PC to the
     * next executable instruction.
     */
    env->gpr[HEX_REG_PC] = env->wait_next_pc;
    cs = env_cpu(env);
    ASSERT_DIRECT_TO_GUEST_UNSET(env, cs->exception_index);
    cs->halted = false;
    cs->exception_index = HEX_EVENT_NONE;
    qemu_cpu_kick(cs);
}

void hexagon_resume_threads(CPUHexagonState *current_env, uint32_t mask)
{
    CPUState *cs;
    CPUHexagonState *env;

    g_assert(bql_locked());
    CPU_FOREACH(cs) {
        env = cpu_env(cs);
        g_assert(env->threadId < THREADS_MAX);
        if ((mask & (0x1 << env->threadId))) {
            if (get_exe_mode(env) == HEX_EXE_MODE_WAIT) {
                hexagon_resume_thread(env);
            }
        }
    }
}

void hexagon_modify_ssr(CPUHexagonState *env, uint32_t new, uint32_t old)
{
    bool old_EX, old_UM, old_GM, old_IE;
    bool new_EX, new_UM, new_GM, new_IE;
    uint8_t old_asid, new_asid;

    g_assert(bql_locked());

    old_EX = GET_SSR_FIELD(SSR_EX, old);
    old_UM = GET_SSR_FIELD(SSR_UM, old);
    old_GM = GET_SSR_FIELD(SSR_GM, old);
    old_IE = GET_SSR_FIELD(SSR_IE, old);
    new_EX = GET_SSR_FIELD(SSR_EX, new);
    new_UM = GET_SSR_FIELD(SSR_UM, new);
    new_GM = GET_SSR_FIELD(SSR_GM, new);
    new_IE = GET_SSR_FIELD(SSR_IE, new);

    if ((old_EX != new_EX) ||
        (old_UM != new_UM) ||
        (old_GM != new_GM)) {
        hex_mmu_mode_change(env);
    }

    old_asid = GET_SSR_FIELD(SSR_ASID, old);
    new_asid = GET_SSR_FIELD(SSR_ASID, new);
    if (new_asid != old_asid) {
        CPUState *cs = env_cpu(env);
        tlb_flush(cs);
    }

    /* See if the interrupts have been enabled or we have exited EX mode */
    if ((new_IE && !old_IE) ||
        (!new_EX && old_EX)) {
        hex_interrupt_update(env);
    }
}

void clear_wait_mode(CPUHexagonState *env)
{
    HexagonCPU *cpu;
    uint32_t modectl, thread_wait_mask;

    g_assert(bql_locked());

    cpu = env_archcpu(env);
    if (cpu->globalregs) {
        modectl =
            hexagon_globalreg_read(cpu->globalregs, HEX_SREG_MODECTL,
                                   env->threadId);
        thread_wait_mask = GET_FIELD(MODECTL_W, modectl);
        thread_wait_mask &= ~(0x1 << env->threadId);
        SET_SYSTEM_FIELD(env, HEX_SREG_MODECTL, MODECTL_W, thread_wait_mask);
    }
}

void hexagon_ssr_set_cause(CPUHexagonState *env, uint32_t cause)
{
    uint32_t old, new;

    g_assert(bql_locked());

    old = env->t_sreg[HEX_SREG_SSR];
    SET_SYSTEM_FIELD(env, HEX_SREG_SSR, SSR_EX, 1);
    SET_SYSTEM_FIELD(env, HEX_SREG_SSR, SSR_CAUSE, cause);
    new = env->t_sreg[HEX_SREG_SSR];

    hexagon_modify_ssr(env, new, old);
}


int get_exe_mode(CPUHexagonState *env)
{
    HexagonCPU *cpu;
    uint32_t modectl, thread_enabled_mask, thread_wait_mask;
    uint32_t isdbst, debugmode;
    bool E_bit, W_bit, D_bit;

    g_assert(bql_locked());

    cpu = env_archcpu(env);
    modectl = cpu->globalregs ?
        hexagon_globalreg_read(cpu->globalregs, HEX_SREG_MODECTL,
                               env->threadId) : 0;
    thread_enabled_mask = GET_FIELD(MODECTL_E, modectl);
    E_bit = thread_enabled_mask & (0x1 << env->threadId);
    thread_wait_mask = GET_FIELD(MODECTL_W, modectl);
    W_bit = thread_wait_mask & (0x1 << env->threadId);
    isdbst = cpu->globalregs ?
        hexagon_globalreg_read(cpu->globalregs, HEX_SREG_ISDBST,
                               env->threadId) : 0;
    debugmode = GET_FIELD(ISDBST_DEBUGMODE, isdbst);
    D_bit = debugmode & (0x1 << env->threadId);

    if (!D_bit && !W_bit && !E_bit) {
        return HEX_EXE_MODE_OFF;
    }
    if (!D_bit && !W_bit && E_bit) {
        return HEX_EXE_MODE_RUN;
    }
    if (!D_bit && W_bit && E_bit) {
        return HEX_EXE_MODE_WAIT;
    }
    if (D_bit && !W_bit && E_bit) {
        return HEX_EXE_MODE_DEBUG;
    }
    g_assert_not_reached();
}

static uint32_t set_enable_mask(CPUHexagonState *env)
{
    HexagonCPU *cpu;
    uint32_t modectl, thread_enabled_mask;

    g_assert(bql_locked());

    cpu = env_archcpu(env);
    if (!cpu->globalregs) {
        return 0;
    }
    modectl =
        hexagon_globalreg_read(cpu->globalregs, HEX_SREG_MODECTL,
                               env->threadId);
    thread_enabled_mask = GET_FIELD(MODECTL_E, modectl);
    thread_enabled_mask |= 0x1 << env->threadId;
    SET_SYSTEM_FIELD(env, HEX_SREG_MODECTL, MODECTL_E, thread_enabled_mask);
    return thread_enabled_mask;
}

static uint32_t clear_enable_mask(CPUHexagonState *env)
{
    HexagonCPU *cpu;
    uint32_t modectl, thread_enabled_mask;

    g_assert(bql_locked());

    cpu = env_archcpu(env);
    if (!cpu->globalregs) {
        return 0;
    }
    modectl =
        hexagon_globalreg_read(cpu->globalregs, HEX_SREG_MODECTL,
                               env->threadId);
    thread_enabled_mask = GET_FIELD(MODECTL_E, modectl);
    thread_enabled_mask &= ~(0x1 << env->threadId);
    SET_SYSTEM_FIELD(env, HEX_SREG_MODECTL, MODECTL_E, thread_enabled_mask);
    return thread_enabled_mask;
}
static void do_start_thread(CPUState *cs, run_on_cpu_data tbd)
{
    CPUHexagonState *env;

    BQL_LOCK_GUARD();

    env = cpu_env(cs);

    hexagon_cpu_soft_reset(env);

    set_enable_mask(env);

    cs->halted = 0;
    cs->exception_index = HEX_EVENT_NONE;
    cpu_resume(cs);
}

void hexagon_start_threads(CPUHexagonState *current_env, uint32_t mask)
{
    CPUState *cs;
    CPU_FOREACH(cs) {
        CPUHexagonState *env = cpu_env(cs);
        if (!(mask & (0x1 << env->threadId))) {
            continue;
        }

        if (current_env->threadId != env->threadId) {
            async_safe_run_on_cpu(cs, do_start_thread, RUN_ON_CPU_NULL);
        }
    }
}

/*
 * When we have all threads stopped, the return
 * value to the shell is register 2 from thread 0.
 */
static uint32_t get_thread0_r2(void)
{
    CPUState *cs;
    CPU_FOREACH(cs) {
        CPUHexagonState *thread = cpu_env(cs);
        if (thread->threadId == 0) {
            return thread->gpr[2];
        }
    }
    g_assert_not_reached();
}

void hexagon_stop_thread(CPUHexagonState *env)
{
    uint32_t thread_enabled_mask;
    CPUState *cs;

    BQL_LOCK_GUARD();

    thread_enabled_mask = clear_enable_mask(env);
    cs = env_cpu(env);
    cpu_interrupt(cs, CPU_INTERRUPT_HALT);
    if (!thread_enabled_mask) {
        /* All threads are stopped, request shutdown */
        qemu_system_shutdown_request_with_code(
            SHUTDOWN_CAUSE_GUEST_SHUTDOWN, get_thread0_r2());
    }
}

static int sys_in_monitor_mode_ssr(uint32_t ssr)
{
    if ((GET_SSR_FIELD(SSR_EX, ssr) != 0) ||
        ((GET_SSR_FIELD(SSR_EX, ssr) == 0) &&
         (GET_SSR_FIELD(SSR_UM, ssr) == 0))) {
        return 1;
    }
    return 0;
}

static int sys_in_guest_mode_ssr(uint32_t ssr)
{
    if ((GET_SSR_FIELD(SSR_EX, ssr) == 0) &&
        (GET_SSR_FIELD(SSR_UM, ssr) != 0) &&
        (GET_SSR_FIELD(SSR_GM, ssr) != 0)) {
        return 1;
    }
    return 0;
}

static int sys_in_user_mode_ssr(uint32_t ssr)
{
    if ((GET_SSR_FIELD(SSR_EX, ssr) == 0) &&
        (GET_SSR_FIELD(SSR_UM, ssr) != 0) &&
        (GET_SSR_FIELD(SSR_GM, ssr) == 0)) {
        return 1;
    }
    return 0;
}

int get_cpu_mode(CPUHexagonState *env)
{
    uint32_t ssr = env->t_sreg[HEX_SREG_SSR];

    if (sys_in_monitor_mode_ssr(ssr)) {
        return HEX_CPU_MODE_MONITOR;
    } else if (sys_in_guest_mode_ssr(ssr)) {
        return HEX_CPU_MODE_GUEST;
    } else if (sys_in_user_mode_ssr(ssr)) {
        return HEX_CPU_MODE_USER;
    }
    return HEX_CPU_MODE_MONITOR;
}
