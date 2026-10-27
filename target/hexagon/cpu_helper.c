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
#include "hw/hexagon/hexagon_hvx_context.h"
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

#ifndef CONFIG_USER_ONLY

static bool hexagon_write_memory_small(CPUHexagonState *env, target_ulong addr,
                                       int byte_count, uint64_t data,
                                       int mmu_idx, uintptr_t retaddr)
{
    /* handle small sizes */
    switch (byte_count) {
    case 1:
        cpu_stb_mmuidx_ra(env, addr, (uint8_t)data, mmu_idx, retaddr);
        return true;

    case 2:
        cpu_stw_le_mmuidx_ra(env, addr, (uint16_t)data, mmu_idx, retaddr);
        return true;

    case 4:
        cpu_stl_le_mmuidx_ra(env, addr, (uint32_t)data, mmu_idx, retaddr);
        return true;

    case 8:
        cpu_stq_le_mmuidx_ra(env, addr, (uint64_t)data, mmu_idx, retaddr);
        return true;

    default:
        /* larger request, handle elsewhere */
        return false;
    }
}

void hexagon_write_memory(CPUHexagonState *env, target_ulong vaddr,
                          int size, uint64_t data, uintptr_t retaddr)
{
    CPUState *cs = env_cpu(env);
    unsigned mmu_idx = cpu_mmu_index(cs, false);
    if (!hexagon_write_memory_small(env, vaddr, size, data, mmu_idx, retaddr)) {
        cpu_abort(cs, "%s: ERROR: bad size = %d!\n", __func__, size);
    }
}

static inline uint32_t page_start(uint32_t addr)
{
    uint32_t page_align = ~(TARGET_PAGE_SIZE - 1);
    return addr & page_align;
}

void hexagon_peek_memory_range(CPUHexagonState *env, uint32_t start_addr,
                               uint32_t length, uintptr_t retaddr)
{
    CPUState *cs = env_cpu(env);
    unsigned int mmu_idx = cpu_mmu_index(cs, false);
    uint32_t first = page_start(start_addr);
    uint32_t last = page_start(start_addr + length - 1);

    for (uint32_t page = first; page <= last; page += TARGET_PAGE_SIZE) {
        cpu_ldub_mmuidx_ra(env, page, mmu_idx, retaddr);
    }
}

#endif

uint32_t hexagon_get_pmu_counter(CPUHexagonState *cur_env, int index)
{
    g_assert_not_reached();
}

static void hexagon_resume_thread(CPUHexagonState *env)
{
    CPUState *cs = env_cpu(env);
    clear_wait_mode(env);
    cs = env_cpu(env);
    ASSERT_DIRECT_TO_GUEST_UNSET(env, cs->exception_index);
    cpu_reset_interrupt(cs, CPU_INTERRUPT_HALT);
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

static unsigned hexagon_hvx_context_count(HexagonCPU *cpu)
{
    unsigned n;

    for (n = 0; n < HVX_CONTEXTS_MAX; n++) {
        if (!cpu->hvx_ctx[n]) {
            break;
        }
    }
    return n;
}

static unsigned hexagon_hvx_context_index(HexagonCPU *cpu, uint8_t xa)
{
    unsigned n = hexagon_hvx_context_count(cpu);

    if (n == 0) {
        return 0;
    }
    if (xa >= n) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "SSR.XA %u is out of range for %u HVX contexts\n",
                      xa, n);
    }
    /*
     * The behavior here is unspecified, reference simulator uses
     * mappings that effectively keep the context in-range.  The
     * modulus seems just as good as any.
     */
    return xa % n;
}

/*
 * Diagnostic only.  Called separately from hexagon_hvx_select_context()
 * so migration post_load, where other CPUs' env->hvx may still be stale,
 * doesn't trip a false positive.
 */
static void hexagon_hvx_check_overcommit(CPUHexagonState *env, unsigned idx)
{
    CPUState *cs;
    unsigned users = 0;

    CPU_FOREACH(cs) {
        CPUHexagonState *other = cpu_env(cs);

        if (other->hvx == env->hvx &&
            GET_SSR_FIELD(SSR_XE, other->t_sreg[HEX_SREG_SSR])) {
            users++;
        }
    }

    if (users > 1) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "HVX context %u is enabled for %u hardware threads "
                      "at once, which is undefined\n", idx, users);
    }
}

unsigned hexagon_hvx_select_context(CPUHexagonState *env, uint32_t ssr)
{
    HexagonCPU *cpu = env_archcpu(env);
    unsigned idx = hexagon_hvx_context_index(cpu, GET_SSR_FIELD(SSR_XA, ssr));

    if (cpu->hvx_ctx[0]) {
        env->hvx = &cpu->hvx_ctx[idx]->regs;
    }
    return idx;
}

void hexagon_modify_ssr(CPUHexagonState *env, uint32_t new, uint32_t old)
{
    bool old_EX, old_IE;
    bool new_EX, new_IE;
    uint8_t old_asid, new_asid;

    g_assert(bql_locked());

    old_EX = GET_SSR_FIELD(SSR_EX, old);
    old_IE = GET_SSR_FIELD(SSR_IE, old);
    new_EX = GET_SSR_FIELD(SSR_EX, new);
    new_IE = GET_SSR_FIELD(SSR_IE, new);

    bool xa_changed = GET_SSR_FIELD(SSR_XA, new) != GET_SSR_FIELD(SSR_XA, old);
    bool xe_changed = GET_SSR_FIELD(SSR_XE, new) != GET_SSR_FIELD(SSR_XE, old);

    if (xa_changed || xe_changed) {
        unsigned idx = xa_changed
            ? hexagon_hvx_select_context(env, new)
            : hexagon_hvx_context_index(env_archcpu(env),
                                        GET_SSR_FIELD(SSR_XA, new));

        hexagon_hvx_check_overcommit(env, idx);
    }

    old_asid = GET_SSR_FIELD(SSR_ASID, old);
    new_asid = GET_SSR_FIELD(SSR_ASID, new);
    if (new_asid != old_asid) {
        hex_mmu_asid_change(env);
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


int get_exe_mode(const CPUHexagonState *env)
{
    const HexagonCPU *cpu;
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
    if (cpu->cfg.hex_def->hex_version >= HEX_VER_V81) {
        isdbst = cpu->globalregs ?
            hexagon_globalreg_read(cpu->globalregs, HEX_SREG_ISDBST2,
                                   env->threadId) : 0;
        debugmode = GET_FIELD(ISDBST2_DEBUGMODE, isdbst);
    } else {
        isdbst = cpu->globalregs ?
            hexagon_globalreg_read(cpu->globalregs, HEX_SREG_ISDBST,
                                   env->threadId) : 0;
        debugmode = GET_FIELD(ISDBST_DEBUGMODE, isdbst);
    }
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
    hex_interrupt_update(env);
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
    hex_interrupt_update(env);
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

    BQL_LOCK_GUARD();

    CPU_FOREACH(cs) {
        CPUHexagonState *env = cpu_env(cs);
        if (!(mask & (0x1 << env->threadId))) {
            continue;
        }

        if (current_env->threadId != env->threadId) {
            set_enable_mask(env);
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

void hexagon_nmi(CPUHexagonState *env, uint32_t thread_mask)
{
    CPUState *cs;

    BQL_LOCK_GUARD();

    CPU_FOREACH(cs) {
        CPUHexagonState *thread = cpu_env(cs);

        if (!(thread_mask & (1u << thread->threadId)) ||
            get_exe_mode(thread) == HEX_EXE_MODE_OFF) {
            continue;
        }
        cpu_interrupt(cs, CPU_INTERRUPT_NMI);
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

int get_cpu_mode(const CPUHexagonState *env)
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
