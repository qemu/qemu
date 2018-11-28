/*
 * ARM load/store instructions for code (armeb-user support)
 *
 *  Copyright (c) 2012 CodeSourcery, LLC
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

#ifndef ARM_LDST_H
#define ARM_LDST_H

#include "exec/translator.h"
#include "qemu/bswap.h"

/* Load an instruction and return it in the standard little-endian order */
static inline uint32_t arm_ldl_code(CPUARMState *env, target_ulong addr,
                                    bool sctlr_b)
{
    return translator_ldl_swap(env, addr, bswap_code(sctlr_b));
}

/* Ditto, for a halfword (Thumb) instruction */
static inline uint16_t arm_lduw_code(CPUARMState *env, target_ulong addr,
                                     bool sctlr_b)
{
#ifndef CONFIG_USER_ONLY
    /* In big-endian (BE32) mode, adjacent Thumb instructions have been swapped
       within each word.  Undo that now.  */
    if (sctlr_b) {
        addr ^= 2;
    }
#endif
    return translator_lduw_swap(env, addr, bswap_code(sctlr_b));
}

#endif
