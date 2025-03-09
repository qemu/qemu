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

/*
 * The true return address will often point to a host insn that is part of
 * the next translated guest insn.  Adjust the address backward to point to
 * the middle of the call insn.  Subtracting one would do the job except for
 * several compressed mode architectures (arm, mips) which set the low bit
 * to indicate the compressed mode; subtracting two works around that.  It
 * is also the case that there are no host isas that contain a call insn
 * smaller than 4 bytes, so we don't worry about special-casing this.
 */
#define GETPC_ADJ   2

#ifdef CONFIG_SOFTMMU

#define CPU_TLB_DYN_MIN_BITS 6
#define CPU_TLB_DYN_DEFAULT_BITS 8

# if HOST_LONG_BITS == 32
/* Make sure we do not require a double-word shift for the TLB load */
#  define CPU_TLB_DYN_MAX_BITS (32 - TARGET_PAGE_BITS)
# else /* HOST_LONG_BITS == 64 */
/*
 * Assuming TARGET_PAGE_BITS==12, with 2**22 entries we can cover 2**(22+12) ==
 * 2**34 == 16G of address space. This is roughly what one would expect a
 * TLB to cover in a modern (as of 2018) x86_64 CPU. For instance, Intel
 * Skylake's Level-2 STLB has 16 1G entries.
 * Also, make sure we do not size the TLB past the guest's address space.
 */
#  ifdef TARGET_PAGE_BITS_VARY
#   define CPU_TLB_DYN_MAX_BITS                                  \
    MIN(22, TARGET_VIRT_ADDR_SPACE_BITS - TARGET_PAGE_BITS)
#  else
#   define CPU_TLB_DYN_MAX_BITS                                  \
    MIN_CONST(22, TARGET_VIRT_ADDR_SPACE_BITS - TARGET_PAGE_BITS)
#  endif
# endif

#endif /* CONFIG_SOFTMMU */

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

#endif
