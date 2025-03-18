/*
 * Copyright(c) 2019-2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "cpu_helper.h"
#include "system/cpus.h"
#ifdef CONFIG_USER_ONLY
#include "qemu.h"
#include "exec/helper-proto.h"
#else
#include "hw/boards.h"
#include "hw/hexagon/hexagon.h"
#endif
#include "exec/exec-all.h"
#include "exec/cputlb.h"
#include "exec/cpu_ldst.h"
#include "qemu/log.h"
#include "tcg/tcg-op.h"
#include "internal.h"
#include "macros.h"
#include "sys_macros.h"
#include "arch.h"


#ifndef CONFIG_USER_ONLY

uint32_t hexagon_get_pmu_counter(CPUHexagonState *cur_env, int index)
{
    g_assert_not_reached();
}

uint32_t arch_get_system_reg(CPUHexagonState *env, uint32_t reg)
{
    g_assert_not_reached();
}

uint64_t hexagon_get_sys_pcycle_count(CPUHexagonState *env)
{
    g_assert_not_reached();
}

uint32_t hexagon_get_sys_pcycle_count_high(CPUHexagonState *env)
{
    g_assert_not_reached();
}

uint32_t hexagon_get_sys_pcycle_count_low(CPUHexagonState *env)
{
    g_assert_not_reached();
}

void hexagon_set_sys_pcycle_count_high(CPUHexagonState *env,
        uint32_t cycles_hi)
{
    g_assert_not_reached();
}

void hexagon_set_sys_pcycle_count_low(CPUHexagonState *env,
        uint32_t cycles_lo)
{
    g_assert_not_reached();
}

void hexagon_set_sys_pcycle_count(CPUHexagonState *env, uint64_t cycles)
{
    g_assert_not_reached();
}

void hexagon_modify_ssr(CPUHexagonState *env, uint32_t new, uint32_t old)
{
    g_assert_not_reached();
}

void clear_wait_mode(CPUHexagonState *env)
{
    g_assert(bql_locked());

    const uint32_t modectl = arch_get_system_reg(env, HEX_SREG_MODECTL);
    uint32_t thread_wait_mask = GET_FIELD(MODECTL_W, modectl);
    thread_wait_mask &= ~(0x1 << env->threadId);
    SET_SYSTEM_FIELD(env, HEX_SREG_MODECTL, MODECTL_W, thread_wait_mask);
}

int get_exe_mode(CPUHexagonState *env)
{
    g_assert_not_reached();
}
#endif
