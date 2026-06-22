/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HEXAGON_CPU_HELPER_H
#define HEXAGON_CPU_HELPER_H

uint32_t hexagon_get_pmu_counter(CPUHexagonState *cur_env, int index);
uint64_t hexagon_get_sys_pcycle_count(CPUHexagonState *env);
uint32_t hexagon_get_sys_pcycle_count_high(CPUHexagonState *env);
uint32_t hexagon_get_sys_pcycle_count_low(CPUHexagonState *env);
void hexagon_set_sys_pcycle_count_high(CPUHexagonState *env, uint32_t val);
void hexagon_set_sys_pcycle_count_low(CPUHexagonState *env, uint32_t val);
void hexagon_set_sys_pcycle_count(CPUHexagonState *env, uint64_t val);

#endif
