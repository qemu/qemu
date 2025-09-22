/*
 * Target-specific parts of semihosting/arm-compat-semi.c.
 *
 * Copyright (c) 2005, 2007 CodeSourcery.
 * Copyright (c) 2019, 2022 Linaro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "semihosting/common-semi.h"
#include "target/arm/cpu-qom.h"

uint64_t common_semi_arg(CPUState *cs, int argno)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    if (is_a64(env)) {
        return env->xregs[argno];
    } else {
        return env->regs[argno];
    }
}

void common_semi_set_ret(CPUState *cs, uint64_t ret)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    if (is_a64(env)) {
        env->xregs[0] = ret;
    } else {
        env->regs[0] = ret;
    }
}

bool common_semi_sys_exit_is_extended(CPUState *cs)
{
    return is_a64(cpu_env(cs));
}

bool is_64bit_semihosting(CPUArchState *env)
{
    return is_a64(env);
}

uint64_t common_semi_stack_bottom(CPUState *cs)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    return is_a64(env) ? env->xregs[31] : env->regs[13];
}

bool common_semi_has_synccache(CPUArchState *env)
{
    /* Ok for A64, invalid for A32/T32 */
    return is_a64(env);
}
