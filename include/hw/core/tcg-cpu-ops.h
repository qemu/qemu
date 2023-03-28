/*
 * TCG CPU-specific operations
 *
 * Copyright 2021 SUSE LLC
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef TCG_CPU_OPS_H
#define TCG_CPU_OPS_H

#include "hw/core/cpu.h"

struct TCGCPUOps {
    /**
     * @initialize: Initalize TCG state
     *
     * Called when the first CPU is realized.
     */
    void (*initialize)(void);
    /**
     * @synchronize_from_tb: Synchronize state from a TCG #TranslationBlock
     *
     * This is called when we abandon execution of a TB before starting it,
     * and must set all parts of the CPU state which the previous TB in the
     * chain may not have updated.
     * By default, when this is NULL, a call is made to @set_pc(tb->pc).
     *
     * If more state needs to be restored, the target must implement a
     * function to restore all the state, and register it here.
     */
    void (*synchronize_from_tb)(CPUState *cpu, const TranslationBlock *tb);
    /**
     * @restore_state_to_opc: Synchronize state from INDEX_op_start_insn
     *
     * This is called when we unwind state in the middle of a TB,
     * usually before raising an exception.  Set all part of the CPU
     * state which are tracked insn-by-insn in the target-specific
     * arguments to start_insn, passed as @data.
     */
    void (*restore_state_to_opc)(CPUState *cpu, const TranslationBlock *tb,
                                 const uint64_t *data);

    /** @cpu_exec_enter: Callback for cpu_exec preparation */
    void (*cpu_exec_enter)(CPUState *cpu);
    /** @cpu_exec_exit: Callback for cpu_exec cleanup */
    void (*cpu_exec_exit)(CPUState *cpu);
    /** @debug_excp_handler: Callback for handling debug exceptions */
    void (*debug_excp_handler)(CPUState *cpu);

#ifdef NEED_CPU_H
#if defined(CONFIG_USER_ONLY) && defined(TARGET_I386)
    /**
     * @fake_user_interrupt: Callback for 'fake exception' handling.
     *
     * Simulate 'fake exception' which will be handled outside the
     * cpu execution loop (hack for x86 user mode).
     */
    void (*fake_user_interrupt)(CPUState *cpu);
#else
    /**
     * @do_interrupt: Callback for interrupt handling.
     */
    void (*do_interrupt)(CPUState *cpu);
#endif /* !CONFIG_USER_ONLY || !TARGET_I386 */
#ifdef CONFIG_SOFTMMU
    /** @cpu_exec_interrupt: Callback for processing interrupts in cpu_exec */
    bool (*cpu_exec_interrupt)(CPUState *cpu, int interrupt_request);
    /**
     * @tlb_fill: Handle a softmmu tlb miss
     *
     * If the access is valid, call tlb_set_page and return true;
     * if the access is invalid and probe is true, return false;
     * otherwise raise an exception and do not return.
     */
    bool (*tlb_fill)(CPUState *cpu, vaddr address, int size,
                     MMUAccessType access_type, int mmu_idx,
                     bool probe, uintptr_t retaddr);
    /**
     * @do_transaction_failed: Callback for handling failed memory transactions
     * (ie bus faults or external aborts; not MMU faults)
     */
    void (*do_transaction_failed)(CPUState *cpu, hwaddr physaddr, vaddr addr,
                                  unsigned size, MMUAccessType access_type,
                                  int mmu_idx, MemTxAttrs attrs,
                                  MemTxResult response, uintptr_t retaddr);
    /**
     * @do_unaligned_access: Callback for unaligned access handling
     * The callback must exit via raising an exception.
     */
    G_NORETURN void (*do_unaligned_access)(CPUState *cpu, vaddr addr,
                                           MMUAccessType access_type,
                                           int mmu_idx, uintptr_t retaddr);

    /**
     * @adjust_watchpoint_address: hack for cpu_check_watchpoint used by ARM
     */
    vaddr (*adjust_watchpoint_address)(CPUState *cpu, vaddr addr, int len);

    /**
     * @debug_check_watchpoint: return true if the architectural
     * watchpoint whose address has matched should really fire, used by ARM
     * and RISC-V
     */
    bool (*debug_check_watchpoint)(CPUState *cpu, CPUWatchpoint *wp);

    /**
     * @debug_check_breakpoint: return true if the architectural
     * breakpoint whose PC has matched should really fire.
     */
    bool (*debug_check_breakpoint)(CPUState *cpu);

    /**
     * @io_recompile_replay_branch: Callback for cpu_io_recompile.
     *
     * The cpu has been stopped, and cpu_restore_state_from_tb has been
     * called.  If the faulting instruction is in a delay slot, and the
     * target architecture requires re-execution of the branch, then
     * adjust the cpu state as required and return true.
     */
    bool (*io_recompile_replay_branch)(CPUState *cpu,
                                       const TranslationBlock *tb);
#else
    /**
     * record_sigsegv:
     * @cpu: cpu context
     * @addr: faulting guest address
     * @access_type: access was read/write/execute
     * @maperr: true for invalid page, false for permission fault
     * @ra: host pc for unwinding
     *
     * We are about to raise SIGSEGV with si_code set for @maperr,
     * and si_addr set for @addr.  Record anything further needed
     * for the signal ucontext_t.
     *
     * If the emulated kernel does not provide anything to the signal
     * handler with anything besides the user context registers, and
     * the siginfo_t, then this hook need do nothing and may be omitted.
     * Otherwise, record the data and return; the caller will raise
     * the signal, unwind the cpu state, and return to the main loop.
     *
     * If it is simpler to re-use the sysemu tlb_fill code, @ra is provided
     * so that a "normal" cpu exception can be raised.  In this case,
     * the signal must be raised by the architecture cpu_loop.
     */
    void (*record_sigsegv)(CPUState *cpu, vaddr addr,
                           MMUAccessType access_type,
                           bool maperr, uintptr_t ra);
    /**
     * record_sigbus:
     * @cpu: cpu context
     * @addr: misaligned guest address
     * @access_type: access was read/write/execute
     * @ra: host pc for unwinding
     *
     * We are about to raise SIGBUS with si_code BUS_ADRALN,
     * and si_addr set for @addr.  Record anything further needed
     * for the signal ucontext_t.
     *
     * If the emulated kernel does not provide the signal handler with
     * anything besides the user context registers, and the siginfo_t,
     * then this hook need do nothing and may be omitted.
     * Otherwise, record the data and return; the caller will raise
     * the signal, unwind the cpu state, and return to the main loop.
     *
     * If it is simpler to re-use the sysemu do_unaligned_access code,
     * @ra is provided so that a "normal" cpu exception can be raised.
     * In this case, the signal must be raised by the architecture cpu_loop.
     */
    void (*record_sigbus)(CPUState *cpu, vaddr addr,
                          MMUAccessType access_type, uintptr_t ra);
#endif /* CONFIG_SOFTMMU */
#endif /* NEED_CPU_H */

};

#if defined(CONFIG_USER_ONLY)

static inline void cpu_check_watchpoint(CPUState *cpu, vaddr addr, vaddr len,
                                        MemTxAttrs atr, int fl, uintptr_t ra)
{
}

static inline int cpu_watchpoint_address_matches(CPUState *cpu,
                                                 vaddr addr, vaddr len)
{
    return 0;
}

#else

/**
 * cpu_check_watchpoint:
 * @cpu: cpu context
 * @addr: guest virtual address
 * @len: access length
 * @attrs: memory access attributes
 * @flags: watchpoint access type
 * @ra: unwind return address
 *
 * Check for a watchpoint hit in [addr, addr+len) of the type
 * specified by @flags.  Exit via exception with a hit.
 */
void cpu_check_watchpoint(CPUState *cpu, vaddr addr, vaddr len,
                          MemTxAttrs attrs, int flags, uintptr_t ra);

/**
 * cpu_watchpoint_address_matches:
 * @cpu: cpu context
 * @addr: guest virtual address
 * @len: access length
 *
 * Return the watchpoint flags that apply to [addr, addr+len).
 * If no watchpoint is registered for the range, the result is 0.
 */
int cpu_watchpoint_address_matches(CPUState *cpu, vaddr addr, vaddr len);

#endif

#endif /* TCG_CPU_OPS_H */
