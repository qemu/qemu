/*
 * Copyright(c) 2019-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "cpu_helper.h"
#include "sysemu/cpus.h"
#ifdef CONFIG_USER_ONLY
#include "qemu.h"
#include "exec/helper-proto.h"
#else
#include "hw/boards.h"
#include "hw/hexagon/hexagon.h"
#endif
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"
#include "qemu/log.h"
#include "tcg/tcg-op.h"
#include "internal.h"
#include "macros.h"
#include "sys_macros.h"
#include "arch.h"


#ifndef CONFIG_USER_ONLY

uint32_t hexagon_get_pmu_counter(CPUHexagonState *cur_env, int index)
{
    g_assert_not_reached();
}

uint32_t arch_get_system_reg(CPUHexagonState *env, uint32_t reg)
{
    g_assert_not_reached();
}

uint64_t hexagon_get_sys_pcycle_count(CPUHexagonState *env)
{
    g_assert_not_reached();
}

uint32_t hexagon_get_sys_pcycle_count_high(CPUHexagonState *env)
{
    g_assert_not_reached();
}

uint32_t hexagon_get_sys_pcycle_count_low(CPUHexagonState *env)
{
    g_assert_not_reached();
}

void hexagon_set_sys_pcycle_count_high(CPUHexagonState *env,
        uint32_t cycles_hi)
{
    g_assert_not_reached();
}

void hexagon_set_sys_pcycle_count_low(CPUHexagonState *env,
        uint32_t cycles_lo)
{
    g_assert_not_reached();
}

void hexagon_set_sys_pcycle_count(CPUHexagonState *env, uint64_t cycles)
{
    g_assert_not_reached();
}

void hexagon_modify_ssr(CPUHexagonState *env, uint32_t new, uint32_t old)
{
    g_assert_not_reached();
}

void clear_wait_mode(CPUHexagonState *env)
{
    g_assert(bql_locked());

    const uint32_t modectl = ARCH_GET_SYSTEM_REG(env, HEX_SREG_MODECTL);
    uint32_t thread_wait_mask = GET_FIELD(MODECTL_W, modectl);
    thread_wait_mask &= ~(0x1 << env->threadId);
    SET_SYSTEM_FIELD(env, HEX_SREG_MODECTL, MODECTL_W, thread_wait_mask);
}

void hexagon_ssr_set_cause(CPUHexagonState *env, uint32_t cause)
{
    g_assert(bql_locked());

    const uint32_t old = ARCH_GET_SYSTEM_REG(env, HEX_SREG_SSR);
    SET_SYSTEM_FIELD(env, HEX_SREG_SSR, SSR_EX, 1);
    SET_SYSTEM_FIELD(env, HEX_SREG_SSR, SSR_CAUSE, cause);
    const uint32_t new = ARCH_GET_SYSTEM_REG(env, HEX_SREG_SSR);

    hexagon_modify_ssr(env, new, old);
}


int get_exe_mode(CPUHexagonState *env)
{
    g_assert_not_reached();
}

static void set_enable_mask(CPUHexagonState *env)
{
    g_assert(bql_locked());

    const uint32_t modectl = ARCH_GET_SYSTEM_REG(env, HEX_SREG_MODECTL);
    uint32_t thread_enabled_mask = GET_FIELD(MODECTL_E, modectl);
    thread_enabled_mask |= 0x1 << env->threadId;
    SET_SYSTEM_FIELD(env, HEX_SREG_MODECTL, MODECTL_E, thread_enabled_mask);
}

static uint32_t clear_enable_mask(CPUHexagonState *env)
{
    g_assert(bql_locked());

    const uint32_t modectl = ARCH_GET_SYSTEM_REG(env, HEX_SREG_MODECTL);
    uint32_t thread_enabled_mask = GET_FIELD(MODECTL_E, modectl);
    thread_enabled_mask &= ~(0x1 << env->threadId);
    SET_SYSTEM_FIELD(env, HEX_SREG_MODECTL, MODECTL_E, thread_enabled_mask);
    return thread_enabled_mask;
}
static void do_start_thread(CPUState *cs, run_on_cpu_data tbd)
{
    BQL_LOCK_GUARD();

    HexagonCPU *cpu = HEXAGON_CPU(cs);
    CPUHexagonState *env = &cpu->env;

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
        HexagonCPU *cpu = HEXAGON_CPU(cs);
        CPUHexagonState *env = &cpu->env;
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
static target_ulong get_thread0_r2(void)
{
    CPUState *cs;
    CPU_FOREACH(cs) {
        HexagonCPU *cpu = HEXAGON_CPU(cs);
        CPUHexagonState *thread = &cpu->env;
        if (thread->threadId == 0) {
            return thread->gpr[2];
        }
    }
    g_assert_not_reached();
}

void hexagon_stop_thread(CPUHexagonState *env)

{
    BQL_LOCK_GUARD();

    uint32_t thread_enabled_mask = clear_enable_mask(env);
    CPUState *cs = env_cpu(env);
    cpu_interrupt(cs, CPU_INTERRUPT_HALT);
    if (!thread_enabled_mask) {
        /* All threads are stopped, exit */
        exit(get_thread0_r2());
    }
}

#endif
