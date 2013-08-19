/*
 * UniCore32 specific CPU ABI and functions for linux-user
 *
 * Copyright (C) 2010-2012 Guan Xuetao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation, or (at your option) any
 * later version. See the COPYING file in the top-level directory.
 */
#ifndef TARGET_CPU_H
#define TARGET_CPU_H

static inline void cpu_clone_regs(CPUUniCore32State *env, target_ulong newsp)
{
    if (newsp) {
        env->regs[29] = newsp;
    }
    env->regs[0] = 0;
}

static inline void cpu_set_tls(CPUUniCore32State *env, target_ulong newtls)
{
    env->regs[16] = newtls;
}

#endif
