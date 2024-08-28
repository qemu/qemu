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
#include "hex_interrupts.h"
#include "hex_mmu.h"
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

static void set_wait_mode(CPUHexagonState *env)
{
    g_assert(bql_locked());

    const uint32_t modectl = ARCH_GET_SYSTEM_REG(env, HEX_SREG_MODECTL);
    uint32_t thread_wait_mask = GET_FIELD(MODECTL_W, modectl);
    thread_wait_mask |= 0x1 << env->threadId;
    SET_SYSTEM_FIELD(env, HEX_SREG_MODECTL, MODECTL_W, thread_wait_mask);
}

void hexagon_wait_thread(CPUHexagonState *env, target_ulong PC)
{
    g_assert(bql_locked());

    if (qemu_loglevel_mask(LOG_GUEST_ERROR) &&
        (env->k0_lock_state != HEX_LOCK_UNLOCKED ||
         env->tlb_lock_state != HEX_LOCK_UNLOCKED)) {
        qemu_log("WARNING: executing wait() with acquired lock"
                 "may lead to deadlock\n");
    }
    g_assert(get_exe_mode(env) != HEX_EXE_MODE_WAIT);

    CPUState *cs = env_cpu(env);
    /*
     * The addtion of cpu_has_work is borrowed from arm's wfi helper
     * and is critical for our stability
     */
    if ((cs->exception_index != HEX_EVENT_NONE) ||
        (cpu_has_work(cs))) {
        qemu_log_mask(CPU_LOG_INT,
            "%s: thread %d skipping WAIT mode, have some work\n",
            __func__, env->threadId);
        return;
    }
    set_wait_mode(env);
    env->wait_next_pc = PC + 4;

    cpu_interrupt(cs, CPU_INTERRUPT_HALT);
}

static MMVector VRegs[VECTOR_UNIT_MAX][NUM_VREGS];
static MMQReg QRegs[VECTOR_UNIT_MAX][NUM_QREGS];

/*
 *                            EXT_CONTEXTS
 * SSR.XA   2              4              6              8
 * 000      HVX Context 0  HVX Context 0  HVX Context 0  HVX Context 0
 * 001      HVX Context 1  HVX Context 1  HVX Context 1  HVX Context 1
 * 010      HVX Context 0  HVX Context 2  HVX Context 2  HVX Context 2
 * 011      HVX Context 1  HVX Context 3  HVX Context 3  HVX Context 3
 * 100      HVX Context 0  HVX Context 0  HVX Context 4  HVX Context 4
 * 101      HVX Context 1  HVX Context 1  HVX Context 5  HVX Context 5
 * 110      HVX Context 0  HVX Context 2  HVX Context 2  HVX Context 6
 * 111      HVX Context 1  HVX Context 3  HVX Context 3  HVX Context 7
 */
static int parse_context_idx(CPUHexagonState *env, uint8_t XA)
{
    int ret;
    HexagonCPU *cpu = env_archcpu(env);
    if (cpu->hvx_contexts == 6 && XA >= 6) {
        ret = XA - 6 + 2;
    } else {
        ret = XA % cpu->hvx_contexts;
    }
    g_assert(ret >= 0 && ret < VECTOR_UNIT_MAX);
    return ret;
}

static void check_overcommitted_hvx(CPUHexagonState *env, uint32_t ssr)
{
    if (!GET_FIELD(SSR_XE, ssr)) {
        return;
    }

    uint8_t XA = GET_SSR_FIELD(SSR_XA, ssr);

    CPUState *cs;
    CPU_FOREACH(cs) {
        HexagonCPU *cpu = HEXAGON_CPU(cs);
        CPUHexagonState *env_ = &cpu->env;
        if (env_ == env) {
            continue;
        }
        /* Check if another thread has the XE bit set and same XA */
        uint32_t ssr_ = ARCH_GET_SYSTEM_REG(env_, HEX_SREG_SSR);
        if (GET_SSR_FIELD(SSR_XE2, ssr_) && GET_FIELD(SSR_XA, ssr_) == XA) {
            qemu_log_mask(LOG_GUEST_ERROR,
                    "setting SSR.XA '%d' on thread %d but thread"
                    " %d has same extension active\n", XA, env->threadId,
                    env_->threadId);
        }
    }
}

void hexagon_modify_ssr(CPUHexagonState *env, uint32_t new, uint32_t old)
{
    g_assert(bql_locked());

    bool old_EX = GET_SSR_FIELD(SSR_EX, old);
    bool old_UM = GET_SSR_FIELD(SSR_UM, old);
    bool old_GM = GET_SSR_FIELD(SSR_GM, old);
    bool old_IE = GET_SSR_FIELD(SSR_IE, old);
    uint8_t old_XA = GET_SSR_FIELD(SSR_XA, old);
    bool new_EX = GET_SSR_FIELD(SSR_EX, new);
    bool new_UM = GET_SSR_FIELD(SSR_UM, new);
    bool new_GM = GET_SSR_FIELD(SSR_GM, new);
    bool new_IE = GET_SSR_FIELD(SSR_IE, new);
    uint8_t new_XA = GET_SSR_FIELD(SSR_XA, new);

    if ((old_EX != new_EX) ||
        (old_UM != new_UM) ||
        (old_GM != new_GM)) {
        hex_mmu_mode_change(env);
    }

    uint8_t old_asid = GET_SSR_FIELD(SSR_ASID, old);
    uint8_t new_asid = GET_SSR_FIELD(SSR_ASID, new);
    if (new_asid != old_asid) {
        CPUState *cs = env_cpu(env);
        tlb_flush(cs);
    }

    if (old_XA != new_XA) {
        int old_unit = parse_context_idx(env, old_XA);
        int new_unit = parse_context_idx(env, new_XA);

        /* Ownership exchange */
        memcpy(VRegs[old_unit], env->VRegs, sizeof(env->VRegs));
        memcpy(QRegs[old_unit], env->QRegs, sizeof(env->QRegs));
        memcpy(env->VRegs, VRegs[new_unit], sizeof(env->VRegs));
        memcpy(env->QRegs, QRegs[new_unit], sizeof(env->QRegs));

        check_overcommitted_hvx(env, new);
    }

    /* See if the interrupts have been enabled or we have exited EX mode */
    if ((new_IE && !old_IE) ||
        (!new_EX && old_EX)) {
        hex_interrupt_update(env);
    }
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
    g_assert(bql_locked());

    target_ulong modectl = ARCH_GET_SYSTEM_REG(env, HEX_SREG_MODECTL);
    uint32_t thread_enabled_mask = GET_FIELD(MODECTL_E, modectl);
    bool E_bit = thread_enabled_mask & (0x1 << env->threadId);
    uint32_t thread_wait_mask = GET_FIELD(MODECTL_W, modectl);
    bool W_bit = thread_wait_mask & (0x1 << env->threadId);
    target_ulong isdbst = ARCH_GET_SYSTEM_REG(env, HEX_SREG_ISDBST);
    uint32_t debugmode = GET_FIELD(ISDBST_DEBUGMODE, isdbst);
    bool D_bit = debugmode & (0x1 << env->threadId);

    /* Figure 4-2 */
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
