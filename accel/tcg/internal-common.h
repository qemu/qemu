/*
 * Internal execution defines for qemu (target agnostic)
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef ACCEL_TCG_INTERNAL_COMMON_H
#define ACCEL_TCG_INTERNAL_COMMON_H

#include "exec/cpu-common.h"
#include "exec/translation-block.h"

extern int64_t max_delay;
extern int64_t max_advance;

extern bool one_insn_per_tb;

/*
 * Return true if CS is not running in parallel with other cpus, either
 * because there are no other cpus or we are within an exclusive context.
 */
static inline bool cpu_in_serial_context(CPUState *cs)
{
    return !tcg_cflags_has(cs, CF_PARALLEL) || cpu_in_exclusive_context(cs);
}

/**
 * cpu_plugin_mem_cbs_enabled() - are plugin memory callbacks enabled?
 * @cs: CPUState pointer
 *
 * The memory callbacks are installed if a plugin has instrumented an
 * instruction for memory. This can be useful to know if you want to
 * force a slow path for a series of memory accesses.
 */
static inline bool cpu_plugin_mem_cbs_enabled(const CPUState *cpu)
{
#ifdef CONFIG_PLUGIN
    return !!cpu->neg.plugin_mem_cbs;
#else
    return false;
#endif
}

TranslationBlock *tb_gen_code(CPUState *cpu, vaddr pc,
                              uint64_t cs_base, uint32_t flags,
                              int cflags);
void page_init(void);
void tb_htable_init(void);
void tb_reset_jump(TranslationBlock *tb, int n);
TranslationBlock *tb_link_page(TranslationBlock *tb);
void cpu_restore_state_from_tb(CPUState *cpu, TranslationBlock *tb,
                               uintptr_t host_pc);

bool tcg_exec_realizefn(CPUState *cpu, Error **errp);
void tcg_exec_unrealizefn(CPUState *cpu);

#endif
