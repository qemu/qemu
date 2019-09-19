/*
 * Variable page size handling
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "exec/exec-all.h"

#ifdef TARGET_PAGE_BITS_VARY
int target_page_bits;
bool target_page_bits_decided;
#endif

bool set_preferred_target_page_bits(int bits)
{
    /*
     * The target page size is the lowest common denominator for all
     * the CPUs in the system, so we can only make it smaller, never
     * larger. And we can't make it smaller once we've committed to
     * a particular size.
     */
#ifdef TARGET_PAGE_BITS_VARY
    assert(bits >= TARGET_PAGE_BITS_MIN);
    if (target_page_bits == 0 || target_page_bits > bits) {
        if (target_page_bits_decided) {
            return false;
        }
        target_page_bits = bits;
    }
#endif
    return true;
}

void finalize_target_page_bits(void)
{
#ifdef TARGET_PAGE_BITS_VARY
    if (target_page_bits == 0) {
        target_page_bits = TARGET_PAGE_BITS_MIN;
    }
    target_page_bits_decided = true;
#endif
}
