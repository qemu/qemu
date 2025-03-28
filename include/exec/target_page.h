/*
 * Target page sizes and friends for non target files
 *
 * Copyright (c) 2017 Red Hat Inc
 *
 * Authors:
 *  David Alan Gilbert <dgilbert@redhat.com>
 *  Juan Quintela <quintela@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef EXEC_TARGET_PAGE_H
#define EXEC_TARGET_PAGE_H

/*
 * If compiling per-target, get the real values.
 * For generic code, reuse the mechanism for variable page size.
 */
#ifdef COMPILING_PER_TARGET
#include "cpu-param.h"
#include "exec/target_long.h"
#define TARGET_PAGE_TYPE  target_long
#else
#define TARGET_PAGE_BITS_VARY
#define TARGET_PAGE_TYPE  int
#endif

#ifdef TARGET_PAGE_BITS_VARY
# include "exec/page-vary.h"
extern const TargetPageBits target_page;
# ifdef CONFIG_DEBUG_TCG
#  define TARGET_PAGE_BITS   ({ assert(target_page.decided); \
                                target_page.bits; })
#  define TARGET_PAGE_MASK   ({ assert(target_page.decided); \
                                (TARGET_PAGE_TYPE)target_page.mask; })
# else
#  define TARGET_PAGE_BITS   target_page.bits
#  define TARGET_PAGE_MASK   ((TARGET_PAGE_TYPE)target_page.mask)
# endif
# define TARGET_PAGE_SIZE    (-(int)TARGET_PAGE_MASK)
#else
# define TARGET_PAGE_SIZE    (1 << TARGET_PAGE_BITS)
# define TARGET_PAGE_MASK    ((TARGET_PAGE_TYPE)-1 << TARGET_PAGE_BITS)
#endif

#define TARGET_PAGE_ALIGN(addr) ROUND_UP((addr), TARGET_PAGE_SIZE)

static inline size_t qemu_target_page_size(void)
{
    return TARGET_PAGE_SIZE;
}

static inline int qemu_target_page_mask(void)
{
    return TARGET_PAGE_MASK;
}

static inline int qemu_target_page_bits(void)
{
    return TARGET_PAGE_BITS;
}

size_t qemu_target_pages_to_MiB(size_t pages);

#endif
