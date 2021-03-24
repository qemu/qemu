/*
 * Variable page size handling -- target independent part.
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
#include "qemu-common.h"
#include "exec/page-vary.h"

/* WARNING: This file must *not* be complied with -flto. */

TargetPageBits target_page;

bool set_preferred_target_page_bits_common(int bits)
{
    /*
     * The target page size is the lowest common denominator for all
     * the CPUs in the system, so we can only make it smaller, never
     * larger. And we can't make it smaller once we've committed to
     * a particular size.
     */
    if (target_page.bits == 0 || target_page.bits > bits) {
        if (target_page.decided) {
            return false;
        }
        target_page.bits = bits;
    }
    return true;
}

void finalize_target_page_bits_common(int min)
{
    if (target_page.bits == 0) {
        target_page.bits = min;
    }
    target_page.mask = -1ull << target_page.bits;
    target_page.decided = true;
}
