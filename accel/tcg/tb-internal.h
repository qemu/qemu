/*
 * TranslationBlock internal declarations (target specific)
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef ACCEL_TCG_TB_INTERNAL_TARGET_H
#define ACCEL_TCG_TB_INTERNAL_TARGET_H

#include "exec/cpu-all.h"
#include "exec/exec-all.h"
#include "exec/translation-block.h"

#ifdef CONFIG_USER_ONLY
#include "user/page-protection.h"
/*
 * For user-only, page_protect sets the page read-only.
 * Since most execution is already on read-only pages, and we'd need to
 * account for other TBs on the same page, defer undoing any page protection
 * until we receive the write fault.
 */
static inline void tb_lock_page0(tb_page_addr_t p0)
{
    page_protect(p0);
}

static inline void tb_lock_page1(tb_page_addr_t p0, tb_page_addr_t p1)
{
    page_protect(p1);
}

static inline void tb_unlock_page1(tb_page_addr_t p0, tb_page_addr_t p1) { }
static inline void tb_unlock_pages(TranslationBlock *tb) { }
#else
void tb_lock_page0(tb_page_addr_t);
void tb_lock_page1(tb_page_addr_t, tb_page_addr_t);
void tb_unlock_page1(tb_page_addr_t, tb_page_addr_t);
void tb_unlock_pages(TranslationBlock *);
#endif

#ifdef CONFIG_SOFTMMU
void tb_invalidate_phys_range_fast(ram_addr_t ram_addr,
                                   unsigned size,
                                   uintptr_t retaddr);
#endif /* CONFIG_SOFTMMU */

bool tb_invalidate_phys_page_unwind(tb_page_addr_t addr, uintptr_t pc);

void tb_check_watchpoint(CPUState *cpu, uintptr_t retaddr);

#endif
