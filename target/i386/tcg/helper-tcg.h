/*
 * TCG specific prototypes for helpers
 *
 *  Copyright (c) 2003 Fabrice Bellard
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

#ifndef I386_HELPER_TCG_H
#define I386_HELPER_TCG_H

#include "exec/exec-all.h"

/* Maximum instruction code size */
#define TARGET_MAX_INSN_SIZE 16

#if defined(TARGET_X86_64)
# define TCG_PHYS_ADDR_BITS 40
#else
# define TCG_PHYS_ADDR_BITS 36
#endif

QEMU_BUILD_BUG_ON(TCG_PHYS_ADDR_BITS > TARGET_PHYS_ADDR_SPACE_BITS);

/**
 * x86_cpu_do_interrupt:
 * @cpu: vCPU the interrupt is to be handled by.
 */
void x86_cpu_do_interrupt(CPUState *cpu);
#ifndef CONFIG_USER_ONLY
void x86_cpu_exec_halt(CPUState *cpu);
bool x86_need_replay_interrupt(int interrupt_request);
bool x86_cpu_exec_interrupt(CPUState *cpu, int int_req);
#endif

void breakpoint_handler(CPUState *cs);

/* n must be a constant to be efficient */
static inline target_long lshift(target_long x, int n)
{
    if (n >= 0) {
        return x << n;
    } else {
        return x >> (-n);
    }
}

/* translate.c */
void tcg_x86_init(void);

/* excp_helper.c */
G_NORETURN void raise_exception(CPUX86State *env, int exception_index);
G_NORETURN void raise_exception_ra(CPUX86State *env, int exception_index,
                                   uintptr_t retaddr);
G_NORETURN void raise_exception_err(CPUX86State *env, int exception_index,
                                    int error_code);
G_NORETURN void raise_exception_err_ra(CPUX86State *env, int exception_index,
                                       int error_code, uintptr_t retaddr);
G_NORETURN void raise_interrupt(CPUX86State *nenv, int intno, int next_eip_addend);
G_NORETURN void handle_unaligned_access(CPUX86State *env, vaddr vaddr,
                                        MMUAccessType access_type,
                                        uintptr_t retaddr);
#ifdef CONFIG_USER_ONLY
void x86_cpu_record_sigsegv(CPUState *cs, vaddr addr,
                            MMUAccessType access_type,
                            bool maperr, uintptr_t ra);
void x86_cpu_record_sigbus(CPUState *cs, vaddr addr,
                           MMUAccessType access_type, uintptr_t ra);
#else
bool x86_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                      MMUAccessType access_type, int mmu_idx,
                      bool probe, uintptr_t retaddr);
G_NORETURN void x86_cpu_do_unaligned_access(CPUState *cs, vaddr vaddr,
                                            MMUAccessType access_type,
                                            int mmu_idx, uintptr_t retaddr);
#endif

/* cc_helper.c */
extern const uint8_t parity_table[256];

/* misc_helper.c */
void cpu_load_eflags(CPUX86State *env, int eflags, int update_mask);
G_NORETURN void do_pause(CPUX86State *env);

/* sysemu/svm_helper.c */
#ifndef CONFIG_USER_ONLY
G_NORETURN void cpu_vmexit(CPUX86State *nenv, uint32_t exit_code,
                           uint64_t exit_info_1, uintptr_t retaddr);
void do_vmexit(CPUX86State *env);
#endif

/* seg_helper.c */
void do_interrupt_x86_hardirq(CPUX86State *env, int intno, int is_hw);
void do_interrupt_all(X86CPU *cpu, int intno, int is_int,
                      int error_code, target_ulong next_eip, int is_hw);
void handle_even_inj(CPUX86State *env, int intno, int is_int,
                     int error_code, int is_hw, int rm);
int exception_has_error_code(int intno);

/* smm_helper.c */
void do_smm_enter(X86CPU *cpu);

/* bpt_helper.c */
bool check_hw_breakpoints(CPUX86State *env, bool force_dr6_update);

#endif /* I386_HELPER_TCG_H */
