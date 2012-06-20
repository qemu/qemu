/*
 *  PowerPC emulation helpers for QEMU.
 *
 *  Copyright (c) 2003-2007 Jocelyn Mayer
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
#include "cpu.h"
#include "helper.h"

/*****************************************************************************/
/* SPR accesses */

#if !defined(CONFIG_USER_ONLY)
/*
 * This is an ugly helper for EPR, which is basically the same as accessing
 * the IACK (PIAC) register on the MPIC. Because we model the MPIC as a device
 * that can only talk to the CPU through MMIO, let's access it that way!
 */
target_ulong helper_load_epr(CPUPPCState *env)
{
    return ldl_phys(env->mpic_cpu_base + 0xA0);
}
#endif
