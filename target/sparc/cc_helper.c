/*
 * Helpers for lazy condition code handling
 *
 *  Copyright (c) 2003-2005 Fabrice Bellard
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

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/helper-proto.h"

void helper_compute_psr(CPUSPARCState *env)
{
    if (CC_OP == CC_OP_FLAGS) {
        return;
    }
    g_assert_not_reached();
}

uint32_t helper_compute_C_icc(CPUSPARCState *env)
{
    if (CC_OP == CC_OP_FLAGS) {
#ifdef TARGET_SPARC64
        return extract64(env->icc_C, 32, 1);
#else
        return env->icc_C;
#endif
    }
    g_assert_not_reached();
}
