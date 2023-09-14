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
#include "exec/exec-all.h"

bool set_preferred_target_page_bits(int bits)
{
#ifdef TARGET_PAGE_BITS_VARY
    assert(bits >= TARGET_PAGE_BITS_MIN);
    return set_preferred_target_page_bits_common(bits);
#else
    return true;
#endif
}

void finalize_target_page_bits(void)
{
#ifdef TARGET_PAGE_BITS_VARY
    finalize_target_page_bits_common(TARGET_PAGE_BITS_MIN);
#endif
}
