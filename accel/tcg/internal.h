/*
 * Internal execution defines for qemu
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef ACCEL_TCG_INTERNAL_H
#define ACCEL_TCG_INTERNAL_H

#include "exec/exec-all.h"

TranslationBlock *tb_gen_code(CPUState *cpu, target_ulong pc,
                              target_ulong cs_base, uint32_t flags,
                              int cflags);
G_NORETURN void cpu_io_recompile(CPUState *cpu, uintptr_t retaddr);
void page_init(void);
void tb_htable_init(void);

/* Return the current PC from CPU, which may be cached in TB. */
static inline target_ulong log_pc(CPUState *cpu, const TranslationBlock *tb)
{
#if TARGET_TB_PCREL
    return cpu->cc->get_pc(cpu);
#else
    return tb_pc(tb);
#endif
}

#endif /* ACCEL_TCG_INTERNAL_H */
