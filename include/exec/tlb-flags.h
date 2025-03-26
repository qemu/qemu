/*
 * TLB flags definition
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
#ifndef TLB_FLAGS_H
#define TLB_FLAGS_H

#include "exec/cpu-defs.h"

#ifdef CONFIG_USER_ONLY

/*
 * Allow some level of source compatibility with softmmu.  We do not
 * support any of the more exotic features, so only invalid pages may
 * be signaled by probe_access_flags().
 */
#define TLB_INVALID_MASK    (1 << (TARGET_PAGE_BITS_MIN - 1))
#define TLB_MMIO            (1 << (TARGET_PAGE_BITS_MIN - 2))
#define TLB_WATCHPOINT      0

#else

/*
 * Flags stored in the low bits of the TLB virtual address.
 * These are defined so that fast path ram access is all zeros.
 * The flags all must be between TARGET_PAGE_BITS and
 * maximum address alignment bit.
 *
 * Use TARGET_PAGE_BITS_MIN so that these bits are constant
 * when TARGET_PAGE_BITS_VARY is in effect.
 *
 * The count, if not the placement of these bits is known
 * to tcg/tcg-op-ldst.c, check_max_alignment().
 */
/* Zero if TLB entry is valid.  */
#define TLB_INVALID_MASK    (1 << (TARGET_PAGE_BITS_MIN - 1))
/*
 * Set if TLB entry references a clean RAM page.  The iotlb entry will
 * contain the page physical address.
 */
#define TLB_NOTDIRTY        (1 << (TARGET_PAGE_BITS_MIN - 2))
/* Set if the slow path must be used; more flags in CPUTLBEntryFull. */
#define TLB_FORCE_SLOW      (1 << (TARGET_PAGE_BITS_MIN - 3))

/*
 * Use this mask to check interception with an alignment mask
 * in a TCG backend.
 */
#define TLB_FLAGS_MASK \
    (TLB_INVALID_MASK | TLB_NOTDIRTY | TLB_FORCE_SLOW)

/*
 * Flags stored in CPUTLBEntryFull.slow_flags[x].
 * TLB_FORCE_SLOW must be set in CPUTLBEntry.addr_idx[x].
 */
/* Set if TLB entry requires byte swap.  */
#define TLB_BSWAP            (1 << 0)
/* Set if TLB entry contains a watchpoint.  */
#define TLB_WATCHPOINT       (1 << 1)
/* Set if TLB entry requires aligned accesses.  */
#define TLB_CHECK_ALIGNED    (1 << 2)
/* Set if TLB entry writes ignored.  */
#define TLB_DISCARD_WRITE    (1 << 3)
/* Set if TLB entry is an IO callback.  */
#define TLB_MMIO             (1 << 4)

#define TLB_SLOW_FLAGS_MASK \
    (TLB_BSWAP | TLB_WATCHPOINT | TLB_CHECK_ALIGNED | \
     TLB_DISCARD_WRITE | TLB_MMIO)

/* The two sets of flags must not overlap. */
QEMU_BUILD_BUG_ON(TLB_FLAGS_MASK & TLB_SLOW_FLAGS_MASK);

#endif /* !CONFIG_USER_ONLY */

#endif /* TLB_FLAGS_H */
