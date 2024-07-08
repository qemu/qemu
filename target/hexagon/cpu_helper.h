/*
 * Copyright(c) 2019-2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
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

#endif

