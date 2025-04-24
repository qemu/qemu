/*
 * Variable page size handling -- target specific part.
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#define IN_PAGE_VARY 1

#include "qemu/osdep.h"
#include "exec/page-vary.h"
#include "exec/target_page.h"


/*
 * For system mode, the minimum comes from the number of bits
 * required for maximum alignment (6) and the number of bits
 * required for TLB_FLAGS_MASK (3).
 *
 * For user mode, TARGET_PAGE_BITS_VARY is a hack to allow the target
 * page size to match the host page size.  Mostly, this reduces the
 * ordinary target page size to run on a host with 4KiB pages (i.e. x86).
 * There is no true minimum required by the implementation, but keep the
 * same minimum as for system mode for sanity.
 * See linux-user/mmap.c, mmap_h_lt_g and mmap_h_gt_g.
 */
#define TARGET_PAGE_BITS_MIN 9

#ifndef TARGET_PAGE_BITS_VARY
QEMU_BUILD_BUG_ON(TARGET_PAGE_BITS < TARGET_PAGE_BITS_MIN);
#endif

#ifndef CONFIG_USER_ONLY
#include "exec/tlb-flags.h"

QEMU_BUILD_BUG_ON(TLB_FLAGS_MASK & ((1u < TARGET_PAGE_BITS_MIN) - 1));

int migration_legacy_page_bits(void)
{
#ifdef TARGET_PAGE_BITS_VARY
    QEMU_BUILD_BUG_ON(TARGET_PAGE_BITS_LEGACY < TARGET_PAGE_BITS_MIN);
    return TARGET_PAGE_BITS_LEGACY;
#else
    return TARGET_PAGE_BITS;
#endif
}
#endif

bool set_preferred_target_page_bits(int bits)
{
    assert(bits >= TARGET_PAGE_BITS_MIN);
#ifdef TARGET_PAGE_BITS_VARY
    return set_preferred_target_page_bits_common(bits);
#else
    return true;
#endif
}

void finalize_target_page_bits(void)
{
#ifndef TARGET_PAGE_BITS_VARY
    finalize_target_page_bits_common(TARGET_PAGE_BITS);
#elif defined(CONFIG_USER_ONLY)
    assert(target_page.bits != 0);
    finalize_target_page_bits_common(target_page.bits);
#else
    finalize_target_page_bits_common(TARGET_PAGE_BITS_LEGACY);
#endif
}
