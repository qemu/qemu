/*
 *  i386 VM parameters definitions
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
#ifndef _TARGET_ARCH_VMPARAM_H_
#define _TARGET_ARCH_VMPARAM_H_

#include "cpu.h"

/* compare to i386/include/vmparam.h */
#define TARGET_MAXTSIZ  (128 * MiB)             /* max text size */
#define TARGET_DFLDSIZ  (128 * MiB)             /* initial data size limit */
#define TARGET_MAXDSIZ  (512 * MiB)             /* max data size */
#define TARGET_DFLSSIZ  (8 * MiB)               /* initial stack size limit */
#define TARGET_MAXSSIZ  (64 * MiB)              /* max stack size */
#define TARGET_SGROWSIZ (128 * KiB)             /* amount to grow stack */

#define TARGET_RESERVED_VA 0xf7000000

#define TARGET_USRSTACK (0xbfc00000)

static inline abi_ulong get_sp_from_cpustate(CPUX86State *state)
{
    return state->regs[R_ESP];
}

static inline void set_second_rval(CPUX86State *state, abi_ulong retval2)
{
    state->regs[R_EDX] = retval2;
}

#endif /* !_TARGET_ARCH_VMPARAM_H_ */
