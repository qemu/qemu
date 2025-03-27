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

/*
 * Flags returned for lookup of a TLB virtual address.
 */

#ifdef CONFIG_USER_ONLY

/*
 * Allow some level of source compatibility with softmmu.
 * Invalid is set when the page does not have requested permissions.
 * MMIO is set when we want the target helper to use the functional
 * interface for load/store so that plugins see the access.
 */
#define TLB_INVALID_MASK     (1 << 0)
#define TLB_MMIO             (1 << 1)
#define TLB_WATCHPOINT       0

#else

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

/*
 * Flags stored in CPUTLBEntry.addr_idx[x].
 * These must be above the largest alignment (64 bytes),
 * and below the smallest page size (1024 bytes).
 * This leaves bits [9:6] available for use.
 */

/* Zero if TLB entry is valid.  */
#define TLB_INVALID_MASK     (1 << 6)
/* Set if TLB entry references a clean RAM page.  */
#define TLB_NOTDIRTY         (1 << 7)
/* Set if the slow path must be used; more flags in CPUTLBEntryFull. */
#define TLB_FORCE_SLOW       (1 << 8)

/*
 * Use this mask to check interception with an alignment mask
 * in a TCG backend.
 */
#define TLB_FLAGS_MASK \
    (TLB_INVALID_MASK | TLB_NOTDIRTY | TLB_FORCE_SLOW)

/* The two sets of flags must not overlap. */
QEMU_BUILD_BUG_ON(TLB_FLAGS_MASK & TLB_SLOW_FLAGS_MASK);

#endif /* !CONFIG_USER_ONLY */

#endif /* TLB_FLAGS_H */
