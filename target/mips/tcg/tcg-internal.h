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

#include "hw/core/cpu.h"
#include "cpu.h"

void mips_cpu_do_interrupt(CPUState *cpu);
bool mips_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                       MMUAccessType access_type, int mmu_idx,
                       bool probe, uintptr_t retaddr);

#if !defined(CONFIG_USER_ONLY)

void update_pagemask(CPUMIPSState *env, target_ulong arg1, int32_t *pagemask);

uint32_t cpu_mips_get_random(CPUMIPSState *env);

#endif /* !CONFIG_USER_ONLY */

#endif
