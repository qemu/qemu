/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HEXAGON_CPU_HELPER_H
#define HEXAGON_CPU_HELPER_H

void hexagon_read_memory(CPUHexagonState *env, target_ulong vaddr, int size,
                         void *retptr, uintptr_t retaddr);
void hexagon_write_memory(CPUHexagonState *env, target_ulong vaddr,
                          int size, uint64_t data, uintptr_t retaddr);
void hexagon_peek_memory_range(CPUHexagonState *env, uint32_t start_addr,
                               uint32_t length, uintptr_t retaddr);
uint32_t hexagon_get_pmu_counter(CPUHexagonState *cur_env, int index);
void hexagon_modify_ssr(CPUHexagonState *env, uint32_t new, uint32_t old);
int get_cpu_mode(CPUHexagonState *env);
int get_exe_mode(CPUHexagonState *env);
void clear_wait_mode(CPUHexagonState *env);
void hexagon_ssr_set_cause(CPUHexagonState *env, uint32_t cause);
void hexagon_start_threads(CPUHexagonState *env, uint32_t mask);
void hexagon_stop_thread(CPUHexagonState *env);
void hexagon_resume_threads(CPUHexagonState *env, uint32_t mask);
uint64_t hexagon_get_sys_pcycle_count(CPUHexagonState *env);
uint32_t hexagon_get_sys_pcycle_count_high(CPUHexagonState *env);
uint32_t hexagon_get_sys_pcycle_count_low(CPUHexagonState *env);
void hexagon_set_sys_pcycle_count_high(CPUHexagonState *env, uint32_t val);
void hexagon_set_sys_pcycle_count_low(CPUHexagonState *env, uint32_t val);
void hexagon_set_sys_pcycle_count(CPUHexagonState *env, uint64_t val);

#endif
