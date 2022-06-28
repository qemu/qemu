/*
 * Target-specific parts of semihosting/arm-compat-semi.c.
 *
 * Copyright (c) 2005, 2007 CodeSourcery.
 * Copyright (c) 2019, 2022 Linaro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef TARGET_ARM_COMMON_SEMI_TARGET_H
#define TARGET_ARM_COMMON_SEMI_TARGET_H

#ifndef CONFIG_USER_ONLY
#include "hw/arm/boot.h"
#endif

static inline target_ulong common_semi_arg(CPUState *cs, int argno)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    if (is_a64(env)) {
        return env->xregs[argno];
    } else {
        return env->regs[argno];
    }
}

static inline void common_semi_set_ret(CPUState *cs, target_ulong ret)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    if (is_a64(env)) {
        env->xregs[0] = ret;
    } else {
        env->regs[0] = ret;
    }
}

static inline bool common_semi_sys_exit_extended(CPUState *cs, int nr)
{
    return (nr == TARGET_SYS_EXIT_EXTENDED || is_a64(cs->env_ptr));
}

static inline bool is_64bit_semihosting(CPUArchState *env)
{
    return is_a64(env);
}

static inline target_ulong common_semi_stack_bottom(CPUState *cs)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    return is_a64(env) ? env->xregs[31] : env->regs[13];
}

static inline bool common_semi_has_synccache(CPUArchState *env)
{
    /* Ok for A64, invalid for A32/T32 */
    return is_a64(env);
}

#endif
