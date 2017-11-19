/*
 * CSKY signal header.
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

#ifndef TARGET_SIGNAL_H
#define TARGET_SIGNAL_H

#include "cpu.h"

/* this struct defines a stack used during syscall handling */

typedef struct target_sigaltstack {
    abi_ulong ss_sp;
    abi_long ss_flags;
    abi_ulong ss_size;
} target_stack_t;


/*
 * sigaltstack controls
 */
#define TARGET_SS_ONSTACK   1
#define TARGET_SS_DISABLE   2

#define TARGET_MINSIGSTKSZ  2048
#define TARGET_SIGSTKSZ     8192

static inline abi_ulong get_sp_from_cpustate(CPUCSKYState *state)
{
#if defined(TARGET_CSKYV1)
    return state->regs[0];
#else
    return state->regs[14];
#endif
}

#endif /* TARGET_SIGNAL_H */
