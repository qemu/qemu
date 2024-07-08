/*
 * Copyright(c) 2019-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef HEXAGON_CPU_HELPER_H
#define HEXAGON_CPU_HELPER_H

static inline void arch_set_thread_reg(CPUHexagonState *env, uint32_t reg,
                                       uint32_t val)
{
    g_assert(reg < TOTAL_PER_THREAD_REGS);
    g_assert_not_reached();
}

static inline uint32_t arch_get_thread_reg(CPUHexagonState *env, uint32_t reg)
{
    g_assert(reg < TOTAL_PER_THREAD_REGS);
    g_assert_not_reached();
}

static inline void arch_set_system_reg(CPUHexagonState *env, uint32_t reg,
                                       uint32_t val)
{
    g_assert_not_reached();
}

uint32_t arch_get_system_reg(CPUHexagonState *env, uint32_t reg);

#define ARCH_GET_THREAD_REG(ENV, REG) \
    arch_get_thread_reg(ENV, REG)
#define ARCH_SET_THREAD_REG(ENV, REG, VAL) \
    arch_set_thread_reg(ENV, REG, VAL)
#define ARCH_GET_SYSTEM_REG(ENV, REG) \
    arch_get_system_reg(ENV, REG)
#define ARCH_SET_SYSTEM_REG(ENV, REG, VAL) \
    arch_set_system_reg(ENV, REG, VAL)

#endif

