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
#include "internals.h"
#include "target/arm/cpu-qom.h"

static uint64_t arm_semi_arg(const CPUState *cs, int argno)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    if (is_a64(env)) {
        return env->xregs[argno];
    } else {
        return env->regs[argno];
    }
}

static void arm_semi_set_ret(CPUState *cs, uint64_t ret)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    if (is_a64(env)) {
        env->xregs[0] = ret;
    } else {
        env->regs[0] = ret;
    }
}

static void arm_semi_set_err(CPUState *cs, int err)
{
}

static bool arm_semi_is_64bit(const CPUState *cs)
{
    return is_a64(cpu_env(cs));
}

static bool arm_semi_sys_exit_is_extended(const CPUState *cs)
{
    return is_a64(cpu_env(cs));
}

static uint64_t arm_semi_stack_bottom(const CPUState *cs)
{
    const ARMCPU *cpu = ARM_CPU(cs);
    const CPUARMState *env = &cpu->env;
    return is_a64(env) ? env->xregs[31] : env->regs[13];
}

static bool arm_semi_has_synccache(const CPUState *cs)
{
    /* Ok for A64, invalid for A32/T32 */
    return is_a64(cpu_env(cs));
}

const SemihostingCPUOps arm_semihosting_ops = {
    .arg = arm_semi_arg,
    .set_ret = arm_semi_set_ret,
    .set_err = arm_semi_set_err,
    .is_64bit = arm_semi_is_64bit,
    .sys_exit_is_extended = arm_semi_sys_exit_is_extended,
    .stack_bottom = arm_semi_stack_bottom,
    .has_synccache = arm_semi_has_synccache,
};
