/*
 * Target-specific parts of semihosting/arm-compat-semi.c.
 *
 * Copyright(c) 2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef TARGET_HEXAGON_COMMON_SEMI_TARGET_H
#define TARGET_HEXAGON_COMMON_SEMI_TARGET_H

#include "cpu.h"
#include "cpu_helper.h"
#include "qemu/log.h"
#include "semihosting/uaccess.h"

static inline bool common_semi_read_arg_word(CPUArchState *env,
                                             target_ulong *save_to,
                                             target_ulong args_addr,
                                             int arg_num)
{
    hexagon_read_memory(env, args_addr + (arg_num) * 4, 4, save_to, 0);
    return false;
}

static inline target_ulong common_semi_arg(CPUState *cs, int argno)
{
    CPUHexagonState *env = cpu_env(cs);
    return arch_get_thread_reg(env, HEX_REG_R00 + argno);
}

static inline void common_semi_set_ret(CPUState *cs, target_ulong ret)
{
    CPUHexagonState *env = cpu_env(cs);
    arch_set_thread_reg(env, HEX_REG_R00, ret);
}

static inline void hex_semi_set_err(CPUState *cs, target_ulong err)
{
    CPUHexagonState *env = cpu_env(cs);
    arch_set_thread_reg(env, HEX_REG_R01, err);
}

static inline bool common_semi_sys_exit_extended(CPUState *cs, int nr)
{
    return false;
}

static inline bool is_64bit_semihosting(CPUArchState *env)
{
    return false;
}

static inline target_ulong common_semi_stack_bottom(CPUState *cs)
{
    CPUHexagonState *env = cpu_env(cs);
    return arch_get_thread_reg(env, HEX_REG_SP);
}

static inline bool common_semi_has_synccache(CPUArchState *env)
{
    return false;
}

static inline void hex_prepare_for_read(CPUState *cs, target_ulong fd,
                                        target_ulong buf, target_ulong len)
{
    CPUHexagonState *env = cpu_env(cs);
    /*
     * Need to make sure the page we are going to write to is available.
     * The file pointer advances with the read. If the write to bufaddr
     * faults the swi function will be restarted but the file pointer
     * will be wrong.
     */
    hexagon_touch_memory(env, buf, len, 0);
}

const struct semihosting_opt_callbacks hex_opt_callbacks = {
    .prepare_for_read = hex_prepare_for_read,
    .set_err = hex_semi_set_err,
};

SEMIHOSTING_REGISTER_OPT_CALLBACKS(hex_opt_callbacks)

#define SEMIHOSTING_EXT_OPEN_MODES

#endif
