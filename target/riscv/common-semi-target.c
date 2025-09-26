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

uint64_t common_semi_arg(CPUState *cs, int argno)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    return env->gpr[xA0 + argno];
}

void common_semi_set_ret(CPUState *cs, uint64_t ret)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    env->gpr[xA0] = ret;
}

bool is_64bit_semihosting(CPUArchState *env)
{
    return riscv_cpu_mxl(env) != MXL_RV32;
}

bool common_semi_sys_exit_is_extended(CPUState *cs)
{
    return is_64bit_semihosting(cpu_env(cs));
}

uint64_t common_semi_stack_bottom(CPUState *cs)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    return env->gpr[xSP];
}

bool common_semi_has_synccache(CPUArchState *env)
{
    return true;
}
