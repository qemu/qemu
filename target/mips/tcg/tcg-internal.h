/*
 * MIPS internal definitions and helpers (TCG accelerator)
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef MIPS_TCG_INTERNAL_H
#define MIPS_TCG_INTERNAL_H

#include "tcg/tcg.h"
#include "exec/memattrs.h"
#include "hw/core/cpu.h"
#include "cpu.h"

void mips_tcg_init(void);

void mips_cpu_synchronize_from_tb(CPUState *cs, const TranslationBlock *tb);
bool mips_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                       MMUAccessType access_type, int mmu_idx,
                       bool probe, uintptr_t retaddr);
void mips_cpu_do_unaligned_access(CPUState *cpu, vaddr addr,
                                  MMUAccessType access_type, int mmu_idx,
                                  uintptr_t retaddr) QEMU_NORETURN;

const char *mips_exception_name(int32_t exception);

void QEMU_NORETURN do_raise_exception_err(CPUMIPSState *env, uint32_t exception,
                                          int error_code, uintptr_t pc);

static inline void QEMU_NORETURN do_raise_exception(CPUMIPSState *env,
                                                    uint32_t exception,
                                                    uintptr_t pc)
{
    do_raise_exception_err(env, exception, 0, pc);
}

#if !defined(CONFIG_USER_ONLY)

void mips_cpu_do_interrupt(CPUState *cpu);
bool mips_cpu_exec_interrupt(CPUState *cpu, int int_req);

void mmu_init(CPUMIPSState *env, const mips_def_t *def);

void update_pagemask(CPUMIPSState *env, target_ulong arg1, int32_t *pagemask);

void r4k_invalidate_tlb(CPUMIPSState *env, int idx, int use_extra);
uint32_t cpu_mips_get_random(CPUMIPSState *env);

bool mips_io_recompile_replay_branch(CPUState *cs, const TranslationBlock *tb);

hwaddr cpu_mips_translate_address(CPUMIPSState *env, target_ulong address,
                                  MMUAccessType access_type, uintptr_t retaddr);
void mips_cpu_do_transaction_failed(CPUState *cs, hwaddr physaddr,
                                    vaddr addr, unsigned size,
                                    MMUAccessType access_type,
                                    int mmu_idx, MemTxAttrs attrs,
                                    MemTxResult response, uintptr_t retaddr);
void cpu_mips_tlb_flush(CPUMIPSState *env);

#endif /* !CONFIG_USER_ONLY */

#endif
