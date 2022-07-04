/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch specific CPU ABI and functions for linux-user
 *
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */

#ifndef LOONGARCH_TARGET_CPU_H
#define LOONGARCH_TARGET_CPU_H

static inline void cpu_clone_regs_child(CPULoongArchState *env,
                                        target_ulong newsp, unsigned flags)
{
    if (newsp) {
        env->gpr[3] = newsp;
    }
    env->gpr[4] = 0;
}

static inline void cpu_clone_regs_parent(CPULoongArchState *env,
                                         unsigned flags)
{
}

static inline void cpu_set_tls(CPULoongArchState *env, target_ulong newtls)
{
    env->gpr[2] = newtls;
}

static inline abi_ulong get_sp_from_cpustate(CPULoongArchState *state)
{
    return state->gpr[3];
}
#endif
