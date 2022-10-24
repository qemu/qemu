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

/* Size of the L2 (and L3, etc) page tables.  */
#define V_L2_BITS 10
#define V_L2_SIZE (1 << V_L2_BITS)

/*
 * L1 Mapping properties
 */
extern int v_l1_size;
extern int v_l1_shift;
extern int v_l2_levels;

/*
 * The bottom level has pointers to PageDesc, and is indexed by
 * anything from 4 to (V_L2_BITS + 3) bits, depending on target page size.
 */
#define V_L1_MIN_BITS 4
#define V_L1_MAX_BITS (V_L2_BITS + 3)
#define V_L1_MAX_SIZE (1 << V_L1_MAX_BITS)

extern void *l1_map[V_L1_MAX_SIZE];

PageDesc *page_find_alloc(tb_page_addr_t index, bool alloc);

static inline PageDesc *page_find(tb_page_addr_t index)
{
    return page_find_alloc(index, false);
}

/* list iterators for lists of tagged pointers in TranslationBlock */
#define TB_FOR_EACH_TAGGED(head, tb, n, field)                          \
    for (n = (head) & 1, tb = (TranslationBlock *)((head) & ~1);        \
         tb; tb = (TranslationBlock *)tb->field[n], n = (uintptr_t)tb & 1, \
             tb = (TranslationBlock *)((uintptr_t)tb & ~1))

#define PAGE_FOR_EACH_TB(pagedesc, tb, n)                       \
    TB_FOR_EACH_TAGGED((pagedesc)->first_tb, tb, n, page_next)

#define TB_FOR_EACH_JMP(head_tb, tb, n)                                 \
    TB_FOR_EACH_TAGGED((head_tb)->jmp_list_head, tb, n, jmp_list_next)

/* In user-mode page locks aren't used; mmap_lock is enough */
#ifdef CONFIG_USER_ONLY
#define assert_page_locked(pd) tcg_debug_assert(have_mmap_lock())
static inline void page_lock(PageDesc *pd) { }
static inline void page_unlock(PageDesc *pd) { }
#else
#ifdef CONFIG_DEBUG_TCG
void do_assert_page_locked(const PageDesc *pd, const char *file, int line);
#define assert_page_locked(pd) do_assert_page_locked(pd, __FILE__, __LINE__)
#else
#define assert_page_locked(pd)
#endif
void page_lock(PageDesc *pd);
void page_unlock(PageDesc *pd);
#endif
#if !defined(CONFIG_USER_ONLY) && defined(CONFIG_DEBUG_TCG)
void assert_no_pages_locked(void);
#else
static inline void assert_no_pages_locked(void) { }
#endif

TranslationBlock *tb_gen_code(CPUState *cpu, target_ulong pc,
                              target_ulong cs_base, uint32_t flags,
                              int cflags);
G_NORETURN void cpu_io_recompile(CPUState *cpu, uintptr_t retaddr);
void page_init(void);
void tb_htable_init(void);
void tb_reset_jump(TranslationBlock *tb, int n);
TranslationBlock *tb_link_page(TranslationBlock *tb, tb_page_addr_t phys_pc,
                               tb_page_addr_t phys_page2);
bool tb_invalidate_phys_page_unwind(tb_page_addr_t addr, uintptr_t pc);
void cpu_restore_state_from_tb(CPUState *cpu, TranslationBlock *tb,
                               uintptr_t host_pc);

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
