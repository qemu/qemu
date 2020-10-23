/*
 * PowerPC specific CPU ABI and functions for linux-user
 *
 * Copyright (c) 2003-2007 Jocelyn Mayer
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
#ifndef PPC_TARGET_CPU_H
#define PPC_TARGET_CPU_H

static inline void cpu_clone_regs_child(CPUPPCState *env, target_ulong newsp,
                                        unsigned flags)
{
    if (newsp) {
        env->gpr[1] = newsp;
    }
    env->gpr[3] = 0;
}

static inline void cpu_clone_regs_parent(CPUPPCState *env, unsigned flags)
{
}

static inline void cpu_set_tls(CPUPPCState *env, target_ulong newtls)
{
#if defined(TARGET_PPC64)
    /* The kernel checks TIF_32BIT here; we don't support loading 32-bit
       binaries on PPC64 yet. */
    env->gpr[13] = newtls;
#else
    env->gpr[2] = newtls;
#endif
}

#ifndef EF_PPC64_ABI
#define EF_PPC64_ABI           0x3
#endif

static inline uint32_t get_ppc64_abi(struct image_info *infop)
{
  return infop->elf_flags & EF_PPC64_ABI;
}

static inline abi_ulong get_sp_from_cpustate(CPUPPCState *state)
{
    return state->gpr[1];
}
#endif
