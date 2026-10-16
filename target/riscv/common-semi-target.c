/*
 * Target-specific parts of semihosting/arm-compat-semi.c.
 *
 * Copyright (c) 2005, 2007 CodeSourcery.
 * Copyright (c) 2019, 2022 Linaro
 * Copyright © 2020 by Keith Packard <keithp@keithp.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "semihosting/common-semi.h"
#include "internals.h"

static uint64_t riscv_semi_arg(const CPUState *cs, int argno)
{
    const RISCVCPU *cpu = RISCV_CPU(cs);
    const CPURISCVState *env = &cpu->env;
    return env->gpr[xA0 + argno];
}

static void riscv_semi_set_ret(CPUState *cs, uint64_t ret)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    env->gpr[xA0] = ret;
}

static void riscv_semi_set_err(CPUState *cs, int err)
{
}

static bool riscv_semi_is_64bit(const CPUState *cs)
{
    return riscv_cpu_mxl(cpu_env(cs)) != MXL_RV32;
}

static bool riscv_semi_sys_exit_is_extended(const CPUState *cs)
{
    return riscv_semi_is_64bit(cs);
}

static uint64_t riscv_semi_stack_bottom(const CPUState *cs)
{
    const RISCVCPU *cpu = RISCV_CPU(cs);
    const CPURISCVState *env = &cpu->env;
    return env->gpr[xSP];
}

static bool riscv_semi_has_synccache(const CPUState *cs)
{
    return true;
}

const SemihostingCPUOps riscv_semihosting_ops = {
    .arg = riscv_semi_arg,
    .set_ret = riscv_semi_set_ret,
    .set_err = riscv_semi_set_err,
    .is_64bit = riscv_semi_is_64bit,
    .sys_exit_is_extended = riscv_semi_sys_exit_is_extended,
    .stack_bottom = riscv_semi_stack_bottom,
    .has_synccache = riscv_semi_has_synccache,
};
