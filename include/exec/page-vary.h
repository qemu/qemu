/*
 * Definitions for cpus with variable page sizes.
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

#ifndef EXEC_PAGE_VARY_H
#define EXEC_PAGE_VARY_H

typedef struct {
    bool decided;
    int bits;
    uint64_t mask;
} TargetPageBits;

#ifdef IN_PAGE_VARY
bool set_preferred_target_page_bits_common(int bits);
void finalize_target_page_bits_common(int min);
#endif

/**
 * set_preferred_target_page_bits:
 * @bits: number of bits needed to represent an address within the page
 *
 * Set the preferred target page size (the actual target page
 * size may be smaller than any given CPU's preference).
 * Returns true on success, false on failure (which can only happen
 * if this is called after the system has already finalized its
 * choice of page size and the requested page size is smaller than that).
 */
bool set_preferred_target_page_bits(int bits);

/**
 * finalize_target_page_bits:
 * Commit the final value set by set_preferred_target_page_bits.
 */
void finalize_target_page_bits(void);

#endif /* EXEC_PAGE_VARY_H */
