/*
 *  arm VM parameters definitions
 *
 *  Copyright (c) 2013 Stacey D. Son
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef TARGET_ARCH_VMPARAM_H
#define TARGET_ARCH_VMPARAM_H

#include "cpu.h"

/* compare to sys/arm/include/vmparam.h */
#define TARGET_MAXTSIZ      (64 * MiB)           /* max text size */
#define TARGET_DFLDSIZ      (128 * MiB)          /* initial data size limit */
#define TARGET_MAXDSIZ      (512 * MiB)          /* max data size */
#define TARGET_DFLSSIZ      (4 * MiB)            /* initial stack size limit */
#define TARGET_MAXSSIZ      (64 * MiB)           /* max stack size */
#define TARGET_SGROWSIZ     (128 * KiB)          /* amount to grow stack */

#define TARGET_RESERVED_VA  0xf7000000

                /* KERNBASE - 512 MB */
#define TARGET_VM_MAXUSER_ADDRESS   (0xc0000000 - (512 * MiB))
#define TARGET_USRSTACK             TARGET_VM_MAXUSER_ADDRESS

static inline abi_ulong get_sp_from_cpustate(CPUARMState *state)
{
    return state->regs[13]; /* sp */
}

static inline void set_second_rval(CPUARMState *state, abi_ulong retval2)
{
    state->regs[1] = retval2;
}

#endif /* TARGET_ARCH_VMPARAM_H */
