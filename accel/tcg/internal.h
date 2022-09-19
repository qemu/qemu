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

/*
 * Access to the various translations structures need to be serialised
 * via locks for consistency.  In user-mode emulation access to the
 * memory related structures are protected with mmap_lock.
 * In !user-mode we use per-page locks.
 */
#ifdef CONFIG_SOFTMMU
#define assert_memory_lock()
#else
#define assert_memory_lock() tcg_debug_assert(have_mmap_lock())
#endif

typedef struct PageDesc {
    /* list of TBs intersecting this ram page */
    uintptr_t first_tb;
#ifdef CONFIG_USER_ONLY
    unsigned long flags;
    void *target_data;
#endif
#ifdef CONFIG_SOFTMMU
    QemuSpin lock;
#endif
} PageDesc;

PageDesc *page_find_alloc(tb_page_addr_t index, bool alloc);

static inline PageDesc *page_find(tb_page_addr_t index)
{
    return page_find_alloc(index, false);
}

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
