/*
 * Copyright (c) 2011, Max Filippov, Open Source and Linux Lab.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Open Source and Linux Lab nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "cpu.h"
#include "exec/helper-proto.h"
#include "qemu/host-utils.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"
#include "exec/address-spaces.h"
#include "qemu/timer.h"

#ifndef CONFIG_USER_ONLY

void xtensa_cpu_do_unaligned_access(CPUState *cs,
        vaddr addr, MMUAccessType access_type,
        int mmu_idx, uintptr_t retaddr)
{
    XtensaCPU *cpu = XTENSA_CPU(cs);
    CPUXtensaState *env = &cpu->env;

    if (xtensa_option_enabled(env->config, XTENSA_OPTION_UNALIGNED_EXCEPTION) &&
            !xtensa_option_enabled(env->config, XTENSA_OPTION_HW_ALIGNMENT)) {
        cpu_restore_state(CPU(cpu), retaddr, true);
        HELPER(exception_cause_vaddr)(env,
                env->pc, LOAD_STORE_ALIGNMENT_CAUSE, addr);
    }
}

void tlb_fill(CPUState *cs, target_ulong vaddr, int size,
              MMUAccessType access_type, int mmu_idx, uintptr_t retaddr)
{
    XtensaCPU *cpu = XTENSA_CPU(cs);
    CPUXtensaState *env = &cpu->env;
    uint32_t paddr;
    uint32_t page_size;
    unsigned access;
    int ret = xtensa_get_physical_addr(env, true, vaddr, access_type, mmu_idx,
            &paddr, &page_size, &access);

    qemu_log_mask(CPU_LOG_MMU, "%s(%08x, %d, %d) -> %08x, ret = %d\n",
                  __func__, vaddr, access_type, mmu_idx, paddr, ret);

    if (ret == 0) {
        tlb_set_page(cs,
                     vaddr & TARGET_PAGE_MASK,
                     paddr & TARGET_PAGE_MASK,
                     access, mmu_idx, page_size);
    } else {
        cpu_restore_state(cs, retaddr, true);
        HELPER(exception_cause_vaddr)(env, env->pc, ret, vaddr);
    }
}

void xtensa_cpu_do_transaction_failed(CPUState *cs, hwaddr physaddr, vaddr addr,
                                      unsigned size, MMUAccessType access_type,
                                      int mmu_idx, MemTxAttrs attrs,
                                      MemTxResult response, uintptr_t retaddr)
{
    XtensaCPU *cpu = XTENSA_CPU(cs);
    CPUXtensaState *env = &cpu->env;

    cpu_restore_state(cs, retaddr, true);
    HELPER(exception_cause_vaddr)(env, env->pc,
                                  access_type == MMU_INST_FETCH ?
                                  INSTR_PIF_ADDR_ERROR_CAUSE :
                                  LOAD_STORE_PIF_ADDR_ERROR_CAUSE,
                                  addr);
}

static void tb_invalidate_virtual_addr(CPUXtensaState *env, uint32_t vaddr)
{
    uint32_t paddr;
    uint32_t page_size;
    unsigned access;
    int ret = xtensa_get_physical_addr(env, false, vaddr, 2, 0,
            &paddr, &page_size, &access);
    if (ret == 0) {
        tb_invalidate_phys_addr(&address_space_memory, paddr,
                                MEMTXATTRS_UNSPECIFIED);
    }
}

#endif

void HELPER(exception)(CPUXtensaState *env, uint32_t excp)
{
    CPUState *cs = CPU(xtensa_env_get_cpu(env));

    cs->exception_index = excp;
    if (excp == EXCP_YIELD) {
        env->yield_needed = 0;
    }
    if (excp == EXCP_DEBUG) {
        env->exception_taken = 0;
    }
    cpu_loop_exit(cs);
}

void HELPER(exception_cause)(CPUXtensaState *env, uint32_t pc, uint32_t cause)
{
    uint32_t vector;

    env->pc = pc;
    if (env->sregs[PS] & PS_EXCM) {
        if (env->config->ndepc) {
            env->sregs[DEPC] = pc;
        } else {
            env->sregs[EPC1] = pc;
        }
        vector = EXC_DOUBLE;
    } else {
        env->sregs[EPC1] = pc;
        vector = (env->sregs[PS] & PS_UM) ? EXC_USER : EXC_KERNEL;
    }

    env->sregs[EXCCAUSE] = cause;
    env->sregs[PS] |= PS_EXCM;

    HELPER(exception)(env, vector);
}

void HELPER(exception_cause_vaddr)(CPUXtensaState *env,
        uint32_t pc, uint32_t cause, uint32_t vaddr)
{
    env->sregs[EXCVADDR] = vaddr;
    HELPER(exception_cause)(env, pc, cause);
}

void debug_exception_env(CPUXtensaState *env, uint32_t cause)
{
    if (xtensa_get_cintlevel(env) < env->config->debug_level) {
        HELPER(debug_exception)(env, env->pc, cause);
    }
}

void HELPER(debug_exception)(CPUXtensaState *env, uint32_t pc, uint32_t cause)
{
    unsigned level = env->config->debug_level;

    env->pc = pc;
    env->sregs[DEBUGCAUSE] = cause;
    env->sregs[EPC1 + level - 1] = pc;
    env->sregs[EPS2 + level - 2] = env->sregs[PS];
    env->sregs[PS] = (env->sregs[PS] & ~PS_INTLEVEL) | PS_EXCM |
        (level << PS_INTLEVEL_SHIFT);
    HELPER(exception)(env, EXC_DEBUG);
}

void HELPER(dump_state)(CPUXtensaState *env)
{
    XtensaCPU *cpu = xtensa_env_get_cpu(env);

    cpu_dump_state(CPU(cpu), stderr, fprintf, 0);
}

#ifndef CONFIG_USER_ONLY

void HELPER(waiti)(CPUXtensaState *env, uint32_t pc, uint32_t intlevel)
{
    CPUState *cpu;

    env->pc = pc;
    env->sregs[PS] = (env->sregs[PS] & ~PS_INTLEVEL) |
        (intlevel << PS_INTLEVEL_SHIFT);

    qemu_mutex_lock_iothread();
    check_interrupts(env);
    qemu_mutex_unlock_iothread();

    if (env->pending_irq_level) {
        cpu_loop_exit(CPU(xtensa_env_get_cpu(env)));
        return;
    }

    cpu = CPU(xtensa_env_get_cpu(env));
    cpu->halted = 1;
    HELPER(exception)(env, EXCP_HLT);
}

void HELPER(update_ccount)(CPUXtensaState *env)
{
    uint64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    env->ccount_time = now;
    env->sregs[CCOUNT] = env->ccount_base +
        (uint32_t)((now - env->time_base) *
                   env->config->clock_freq_khz / 1000000);
}

void HELPER(wsr_ccount)(CPUXtensaState *env, uint32_t v)
{
    int i;

    HELPER(update_ccount)(env);
    env->ccount_base += v - env->sregs[CCOUNT];
    for (i = 0; i < env->config->nccompare; ++i) {
        HELPER(update_ccompare)(env, i);
    }
}

void HELPER(update_ccompare)(CPUXtensaState *env, uint32_t i)
{
    uint64_t dcc;

    HELPER(update_ccount)(env);
    dcc = (uint64_t)(env->sregs[CCOMPARE + i] - env->sregs[CCOUNT] - 1) + 1;
    timer_mod(env->ccompare[i].timer,
              env->ccount_time + (dcc * 1000000) / env->config->clock_freq_khz);
    env->yield_needed = 1;
}

void HELPER(check_interrupts)(CPUXtensaState *env)
{
    qemu_mutex_lock_iothread();
    check_interrupts(env);
    qemu_mutex_unlock_iothread();
}

/*!
 * Check vaddr accessibility/cache attributes and raise an exception if
 * specified by the ATOMCTL SR.
 *
 * Note: local memory exclusion is not implemented
 */
void HELPER(check_atomctl)(CPUXtensaState *env, uint32_t pc, uint32_t vaddr)
{
    uint32_t paddr, page_size, access;
    uint32_t atomctl = env->sregs[ATOMCTL];
    int rc = xtensa_get_physical_addr(env, true, vaddr, 1,
            xtensa_get_cring(env), &paddr, &page_size, &access);

    /*
     * s32c1i never causes LOAD_PROHIBITED_CAUSE exceptions,
     * see opcode description in the ISA
     */
    if (rc == 0 &&
            (access & (PAGE_READ | PAGE_WRITE)) != (PAGE_READ | PAGE_WRITE)) {
        rc = STORE_PROHIBITED_CAUSE;
    }

    if (rc) {
        HELPER(exception_cause_vaddr)(env, pc, rc, vaddr);
    }

    /*
     * When data cache is not configured use ATOMCTL bypass field.
     * See ISA, 4.3.12.4 The Atomic Operation Control Register (ATOMCTL)
     * under the Conditional Store Option.
     */
    if (!xtensa_option_enabled(env->config, XTENSA_OPTION_DCACHE)) {
        access = PAGE_CACHE_BYPASS;
    }

    switch (access & PAGE_CACHE_MASK) {
    case PAGE_CACHE_WB:
        atomctl >>= 2;
        /* fall through */
    case PAGE_CACHE_WT:
        atomctl >>= 2;
        /* fall through */
    case PAGE_CACHE_BYPASS:
        if ((atomctl & 0x3) == 0) {
            HELPER(exception_cause_vaddr)(env, pc,
                    LOAD_STORE_ERROR_CAUSE, vaddr);
        }
        break;

    case PAGE_CACHE_ISOLATE:
        HELPER(exception_cause_vaddr)(env, pc,
                LOAD_STORE_ERROR_CAUSE, vaddr);
        break;

    default:
        break;
    }
}

void HELPER(wsr_memctl)(CPUXtensaState *env, uint32_t v)
{
    if (xtensa_option_enabled(env->config, XTENSA_OPTION_ICACHE)) {
        if (extract32(v, MEMCTL_IUSEWAYS_SHIFT, MEMCTL_IUSEWAYS_LEN) >
            env->config->icache_ways) {
            deposit32(v, MEMCTL_IUSEWAYS_SHIFT, MEMCTL_IUSEWAYS_LEN,
                      env->config->icache_ways);
        }
    }
    if (xtensa_option_enabled(env->config, XTENSA_OPTION_DCACHE)) {
        if (extract32(v, MEMCTL_DUSEWAYS_SHIFT, MEMCTL_DUSEWAYS_LEN) >
            env->config->dcache_ways) {
            deposit32(v, MEMCTL_DUSEWAYS_SHIFT, MEMCTL_DUSEWAYS_LEN,
                      env->config->dcache_ways);
        }
        if (extract32(v, MEMCTL_DALLOCWAYS_SHIFT, MEMCTL_DALLOCWAYS_LEN) >
            env->config->dcache_ways) {
            deposit32(v, MEMCTL_DALLOCWAYS_SHIFT, MEMCTL_DALLOCWAYS_LEN,
                      env->config->dcache_ways);
        }
    }
    env->sregs[MEMCTL] = v & env->config->memctl_mask;
}

void HELPER(wsr_ibreakenable)(CPUXtensaState *env, uint32_t v)
{
    uint32_t change = v ^ env->sregs[IBREAKENABLE];
    unsigned i;

    for (i = 0; i < env->config->nibreak; ++i) {
        if (change & (1 << i)) {
            tb_invalidate_virtual_addr(env, env->sregs[IBREAKA + i]);
        }
    }
    env->sregs[IBREAKENABLE] = v & ((1 << env->config->nibreak) - 1);
}

void HELPER(wsr_ibreaka)(CPUXtensaState *env, uint32_t i, uint32_t v)
{
    if (env->sregs[IBREAKENABLE] & (1 << i) && env->sregs[IBREAKA + i] != v) {
        tb_invalidate_virtual_addr(env, env->sregs[IBREAKA + i]);
        tb_invalidate_virtual_addr(env, v);
    }
    env->sregs[IBREAKA + i] = v;
}

static void set_dbreak(CPUXtensaState *env, unsigned i, uint32_t dbreaka,
        uint32_t dbreakc)
{
    CPUState *cs = CPU(xtensa_env_get_cpu(env));
    int flags = BP_CPU | BP_STOP_BEFORE_ACCESS;
    uint32_t mask = dbreakc | ~DBREAKC_MASK;

    if (env->cpu_watchpoint[i]) {
        cpu_watchpoint_remove_by_ref(cs, env->cpu_watchpoint[i]);
    }
    if (dbreakc & DBREAKC_SB) {
        flags |= BP_MEM_WRITE;
    }
    if (dbreakc & DBREAKC_LB) {
        flags |= BP_MEM_READ;
    }
    /* contiguous mask after inversion is one less than some power of 2 */
    if ((~mask + 1) & ~mask) {
        qemu_log_mask(LOG_GUEST_ERROR, "DBREAKC mask is not contiguous: 0x%08x\n", dbreakc);
        /* cut mask after the first zero bit */
        mask = 0xffffffff << (32 - clo32(mask));
    }
    if (cpu_watchpoint_insert(cs, dbreaka & mask, ~mask + 1,
            flags, &env->cpu_watchpoint[i])) {
        env->cpu_watchpoint[i] = NULL;
        qemu_log_mask(LOG_GUEST_ERROR, "Failed to set data breakpoint at 0x%08x/%d\n",
                      dbreaka & mask, ~mask + 1);
    }
}

void HELPER(wsr_dbreaka)(CPUXtensaState *env, uint32_t i, uint32_t v)
{
    uint32_t dbreakc = env->sregs[DBREAKC + i];

    if ((dbreakc & DBREAKC_SB_LB) &&
            env->sregs[DBREAKA + i] != v) {
        set_dbreak(env, i, v, dbreakc);
    }
    env->sregs[DBREAKA + i] = v;
}

void HELPER(wsr_dbreakc)(CPUXtensaState *env, uint32_t i, uint32_t v)
{
    if ((env->sregs[DBREAKC + i] ^ v) & (DBREAKC_SB_LB | DBREAKC_MASK)) {
        if (v & DBREAKC_SB_LB) {
            set_dbreak(env, i, env->sregs[DBREAKA + i], v);
        } else {
            if (env->cpu_watchpoint[i]) {
                CPUState *cs = CPU(xtensa_env_get_cpu(env));

                cpu_watchpoint_remove_by_ref(cs, env->cpu_watchpoint[i]);
                env->cpu_watchpoint[i] = NULL;
            }
        }
    }
    env->sregs[DBREAKC + i] = v;
}
#endif

uint32_t HELPER(rer)(CPUXtensaState *env, uint32_t addr)
{
#ifndef CONFIG_USER_ONLY
    return address_space_ldl(env->address_space_er, addr,
                             MEMTXATTRS_UNSPECIFIED, NULL);
#else
    return 0;
#endif
}

void HELPER(wer)(CPUXtensaState *env, uint32_t data, uint32_t addr)
{
#ifndef CONFIG_USER_ONLY
    address_space_stl(env->address_space_er, addr, data,
                      MEMTXATTRS_UNSPECIFIED, NULL);
#endif
}
