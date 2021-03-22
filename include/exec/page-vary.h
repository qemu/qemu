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
extern bool set_preferred_target_page_bits_common(int bits);
extern void finalize_target_page_bits_common(int min);
#endif

#endif /* EXEC_PAGE_VARY_H */
