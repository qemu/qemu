/*
 * Target-specific parts of semihosting/arm-compat-semi.c.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "cpu_helper.h"
#include "semihosting/common-semi.h"

uint64_t common_semi_arg(CPUState *cs, int argno)
{
    CPUHexagonState *env = cpu_env(cs);
    return env->gpr[HEX_REG_R00 + argno];
}

void common_semi_set_ret(CPUState *cs, uint64_t ret)
{
    CPUHexagonState *env = cpu_env(cs);
    env->gpr[HEX_REG_R00] = ret;
}

void common_semi_set_err(CPUState *cs, int err)
{
    CPUHexagonState *env = cpu_env(cs);
    env->gpr[HEX_REG_R01] = err;
}

bool common_semi_sys_exit_is_extended(CPUState *cs)
{
    return false;
}

bool is_64bit_semihosting(CPUArchState *env)
{
    return false;
}

uint64_t common_semi_stack_bottom(CPUState *cs)
{
    CPUHexagonState *env = cpu_env(cs);
    return env->gpr[HEX_REG_SP];
}

bool common_semi_has_synccache(CPUArchState *env)
{
    return false;
}
