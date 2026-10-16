/*
 * Target-specific parts of semihosting/arm-compat-semi.c.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "semihosting/common-semi.h"
#include "internal.h"

static uint64_t hexagon_semi_arg(const CPUState *cs, int argno)
{
    const CPUHexagonState *env = cpu_env(cs);
    return env->gpr[HEX_REG_R00 + argno];
}

static void hexagon_semi_set_ret(CPUState *cs, uint64_t ret)
{
    CPUHexagonState *env = cpu_env(cs);
    env->gpr[HEX_REG_R00] = ret;
}

static void hexagon_semi_set_err(CPUState *cs, int err)
{
    CPUHexagonState *env = cpu_env(cs);
    env->gpr[HEX_REG_R01] = err;
}

static bool hexagon_semi_is_64bit(const CPUState *cs)
{
    return false;
}

static bool hexagon_semi_sys_exit_is_extended(const CPUState *cs)
{
    return false;
}

static uint64_t hexagon_semi_stack_bottom(const CPUState *cs)
{
    const CPUHexagonState *env = cpu_env(cs);
    return env->gpr[HEX_REG_SP];
}

static bool hexagon_semi_has_synccache(const CPUState *cs)
{
    return false;
}

const SemihostingCPUOps hexagon_semihosting_ops = {
    .arg = hexagon_semi_arg,
    .set_ret = hexagon_semi_set_ret,
    .set_err = hexagon_semi_set_err,
    .is_64bit = hexagon_semi_is_64bit,
    .sys_exit_is_extended = hexagon_semi_sys_exit_is_extended,
    .stack_bottom = hexagon_semi_stack_bottom,
    .has_synccache = hexagon_semi_has_synccache,
};
