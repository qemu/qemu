/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "qemu/qemu-print.h"
#include "cpu.h"
#include "system/cpus.h"
#include "internal.h"
#include "exec/cpu-interrupt.h"
#include "cpu_helper.h"
#include "exec/cputlb.h"
#include "hex_mmu.h"
#include "macros.h"
#include "sys_macros.h"
#include "hw/hexagon/hexagon_tlb.h"
#include "hw/hexagon/hexagon_globalreg.h"

static inline void hex_log_tlbw(uint32_t index, uint64_t entry)
{
    qemu_log_mask(CPU_LOG_MMU,
                  "tlbw[%03" PRIu32 "]: 0x%016" PRIx64 "\n",
                  index, entry);
}

void hex_tlbw(CPUHexagonState *env, uint32_t index, uint64_t value)
{
    uint32_t myidx = fTLB_NONPOW2WRAP(fTLB_IDXMASK(index));
    HexagonTLBState *tlb = env_archcpu(env)->tlb;
    uint64_t old_entry = hexagon_tlb_read(tlb, myidx);

    bool old_entry_valid = extract64(old_entry, 63, 1);
    if (old_entry_valid && hexagon_cpu_mmu_enabled(env)) {
        CPUState *cs = env_cpu(env);
        tlb_flush(cs);
    }
    hexagon_tlb_write(tlb, myidx, value);
    hex_log_tlbw(myidx, value);
}

void hex_mmu_on(CPUHexagonState *env)
{
    CPUState *cs = env_cpu(env);
    qemu_log_mask(CPU_LOG_MMU, "Hexagon MMU turned on!\n");
    tlb_flush(cs);
}

void hex_mmu_off(CPUHexagonState *env)
{
    CPUState *cs = env_cpu(env);
    qemu_log_mask(CPU_LOG_MMU, "Hexagon MMU turned off!\n");
    tlb_flush(cs);
}

void hex_mmu_mode_change(CPUHexagonState *env)
{
    qemu_log_mask(CPU_LOG_MMU, "Hexagon mode change!\n");
    CPUState *cs = env_cpu(env);
    tlb_flush(cs);
}

bool hex_tlb_find_match(CPUHexagonState *env, uint32_t VA,
                        MMUAccessType access_type, hwaddr *PA, int *prot,
                        uint64_t *size, int32_t *excp, int mmu_idx)
{
    HexagonCPU *cpu = env_archcpu(env);
    uint32_t ssr = env->t_sreg[HEX_SREG_SSR];
    uint8_t asid = GET_SSR_FIELD(SSR_ASID, ssr);
    int cause_code = 0;

    bool found = hexagon_tlb_find_match(cpu->tlb, asid, VA, access_type,
                                        PA, prot, size, excp, &cause_code,
                                        mmu_idx);
    if (cause_code) {
        env->cause_code = cause_code;
    }
    return found;
}

/* Called from tlbp instruction */
uint32_t hex_tlb_lookup(CPUHexagonState *env, uint32_t ssr, uint32_t VA)
{
    HexagonCPU *cpu = env_archcpu(env);
    uint8_t asid = GET_SSR_FIELD(SSR_ASID, ssr);
    int cause_code = 0;

    uint32_t result = hexagon_tlb_lookup(cpu->tlb, asid, VA, &cause_code);
    if (cause_code) {
        env->cause_code = cause_code;
    }
    return result;
}

/*
 * Return codes:
 * 0 or positive             index of match
 * -1                        multiple matches
 * -2                        no match
 */
int hex_tlb_check_overlap(CPUHexagonState *env, uint64_t entry, uint64_t index)
{
    HexagonCPU *cpu = env_archcpu(env);
    return hexagon_tlb_check_overlap(cpu->tlb, entry, index);
}

void dump_mmu(Monitor *mon, CPUHexagonState *env)
{
    HexagonCPU *cpu = env_archcpu(env);
    hexagon_tlb_dump(mon, cpu->tlb);
}

static inline void print_thread(const char *str, CPUState *cs)
{
    g_assert(bql_locked());
    CPUHexagonState *thread = cpu_env(cs);
    bool is_stopped = cpu_is_stopped(cs);
    int exe_mode = get_exe_mode(thread);
    hex_lock_state_t lock_state = thread->tlb_lock_state;
    qemu_log_mask(CPU_LOG_MMU,
           "%s: threadId = %" PRIu32 ": %s,"
           " exe_mode = %s, tlb_lock_state = %s\n",
           str,
           thread->threadId,
           is_stopped ? "stopped" : "running",
           exe_mode == HEX_EXE_MODE_OFF ? "off" :
           exe_mode == HEX_EXE_MODE_RUN ? "run" :
           exe_mode == HEX_EXE_MODE_WAIT ? "wait" :
           exe_mode == HEX_EXE_MODE_DEBUG ? "debug" :
           "unknown",
           lock_state == HEX_LOCK_UNLOCKED ? "unlocked" :
           lock_state == HEX_LOCK_WAITING ? "waiting" :
           lock_state == HEX_LOCK_OWNER ? "owner" :
           "unknown");
}

static inline void print_thread_states(const char *str)
{
    CPUState *cs;
    CPU_FOREACH(cs) {
        print_thread(str, cs);
    }
}

void hex_tlb_lock(CPUHexagonState *env)
{
    qemu_log_mask(CPU_LOG_MMU, "hex_tlb_lock: " TARGET_FMT_ld "\n",
                  env->threadId);
    BQL_LOCK_GUARD();
    g_assert((env->tlb_lock_count == 0) || (env->tlb_lock_count == 1));

    HexagonCPU *cpu = env_archcpu(env);
    uint32_t syscfg = cpu->globalregs ?
        hexagon_globalreg_read(cpu->globalregs, HEX_SREG_SYSCFG,
                               env->threadId) : 0;
    uint8_t tlb_lock = GET_SYSCFG_FIELD(SYSCFG_TLBLOCK, syscfg);
    if (tlb_lock) {
        if (env->tlb_lock_state == HEX_LOCK_QUEUED) {
            env->next_PC += 4;
            env->tlb_lock_count++;
            env->tlb_lock_state = HEX_LOCK_OWNER;
            SET_SYSCFG_FIELD(env, SYSCFG_TLBLOCK, 1);
            return;
        }
        if (env->tlb_lock_state == HEX_LOCK_OWNER) {
            qemu_log_mask(CPU_LOG_MMU | LOG_GUEST_ERROR,
                          "Double tlblock at PC: 0x%" PRIx32
                          ", thread may hang\n",
                          env->next_PC);
            env->next_PC += 4;
            CPUState *cs = env_cpu(env);
            cpu_interrupt(cs, CPU_INTERRUPT_HALT);
            return;
        }
        env->tlb_lock_state = HEX_LOCK_WAITING;
        CPUState *cs = env_cpu(env);
        cpu_interrupt(cs, CPU_INTERRUPT_HALT);
    } else {
        env->next_PC += 4;
        env->tlb_lock_count++;
        env->tlb_lock_state = HEX_LOCK_OWNER;
        SET_SYSCFG_FIELD(env, SYSCFG_TLBLOCK, 1);
    }

    if (qemu_loglevel_mask(CPU_LOG_MMU)) {
        qemu_log_mask(CPU_LOG_MMU, "Threads after hex_tlb_lock:\n");
        print_thread_states("\tThread");
    }
}

void hex_tlb_unlock(CPUHexagonState *env)
{
    BQL_LOCK_GUARD();
    g_assert((env->tlb_lock_count == 0) || (env->tlb_lock_count == 1));

    /* Nothing to do if the TLB isn't locked by this thread */
    HexagonCPU *cpu = env_archcpu(env);
    uint32_t syscfg = cpu->globalregs ?
        hexagon_globalreg_read(cpu->globalregs, HEX_SREG_SYSCFG,
                               env->threadId) : 0;
    uint8_t tlb_lock = GET_SYSCFG_FIELD(SYSCFG_TLBLOCK, syscfg);
    if ((tlb_lock == 0) ||
        (env->tlb_lock_state != HEX_LOCK_OWNER)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "thread %" PRIu32 " attempted to tlbunlock"
                      " without having the lock, tlb_lock state = %d\n",
                      env->threadId, env->tlb_lock_state);
        g_assert(env->tlb_lock_state != HEX_LOCK_WAITING);
        return;
    }

    env->tlb_lock_count--;
    env->tlb_lock_state = HEX_LOCK_UNLOCKED;
    SET_SYSCFG_FIELD(env, SYSCFG_TLBLOCK, 0);

    /* Look for a thread to unlock */
    unsigned int this_threadId = env->threadId;
    CPUHexagonState *unlock_thread = NULL;
    CPUState *cs;
    CPU_FOREACH(cs) {
        CPUHexagonState *thread = cpu_env(cs);

        /*
         * The hardware implements round-robin fairness, so we look for threads
         * starting at env->threadId + 1 and incrementing modulo the number of
         * threads.
         *
         * To implement this, we check if thread is a earlier in the modulo
         * sequence than unlock_thread.
         *     if unlock thread is higher than this thread
         *         thread must be between this thread and unlock_thread
         *     else
         *         thread higher than this thread is ahead of unlock_thread
         *         thread must be lower then unlock thread
         */
        if (thread->tlb_lock_state == HEX_LOCK_WAITING) {
            if (!unlock_thread) {
                unlock_thread = thread;
            } else if (unlock_thread->threadId > this_threadId) {
                if (this_threadId < thread->threadId &&
                    thread->threadId < unlock_thread->threadId) {
                    unlock_thread = thread;
                }
            } else {
                if (thread->threadId > this_threadId) {
                    unlock_thread = thread;
                }
                if (thread->threadId < unlock_thread->threadId) {
                    unlock_thread = thread;
                }
            }
        }
    }
    if (unlock_thread) {
        cs = env_cpu(unlock_thread);
        print_thread("\tWaiting thread found", cs);
        unlock_thread->tlb_lock_state = HEX_LOCK_QUEUED;
        SET_SYSCFG_FIELD(unlock_thread, SYSCFG_TLBLOCK, 1);
        cpu_interrupt(cs, CPU_INTERRUPT_TLB_UNLOCK);
    }

    if (qemu_loglevel_mask(CPU_LOG_MMU)) {
        qemu_log_mask(CPU_LOG_MMU, "Threads after hex_tlb_unlock:\n");
        print_thread_states("\tThread");
    }

}
