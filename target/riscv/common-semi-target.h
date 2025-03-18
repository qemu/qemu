/*
 * Target-specific parts of semihosting/arm-compat-semi.c.
 *
 * Copyright (c) 2005, 2007 CodeSourcery.
 * Copyright (c) 2019, 2022 Linaro
 * Copyright © 2020 by Keith Packard <keithp@keithp.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef TARGET_RISCV_COMMON_SEMI_TARGET_H
#define TARGET_RISCV_COMMON_SEMI_TARGET_H

static inline bool common_semi_read_arg_word(CPUArchState *env,
                                             target_ulong *save_to,
                                             target_ulong args_addr,
                                             int arg_num)
{
    if (is_64bit_semihosting(env)) {
        return get_user_u64(*save_to, args_addr + (arg_num) * 8));
    }
    return get_user_u32(*save_to, args_addr + (arg_num) * 4));
}

static inline target_ulong common_semi_arg(CPUState *cs, int argno)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    return env->gpr[xA0 + argno];
}

static inline void common_semi_set_ret(CPUState *cs, target_ulong ret)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    env->gpr[xA0] = ret;
}

static inline bool common_semi_sys_exit_extended(CPUState *cs, int nr)
{
    return (nr == TARGET_SYS_EXIT_EXTENDED || sizeof(target_ulong) == 8);
}

static inline bool is_64bit_semihosting(CPUArchState *env)
{
    return riscv_cpu_mxl(env) != MXL_RV32;
}

static inline target_ulong common_semi_stack_bottom(CPUState *cs)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    return env->gpr[xSP];
}

static inline bool common_semi_has_synccache(CPUArchState *env)
{
    return true;
}

#endif
