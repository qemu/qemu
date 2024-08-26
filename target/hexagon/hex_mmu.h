/*
 * Copyright(c) 2019-2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef HEXAGON_MMU_H
#define HEXAGON_MMU_H

#include "max.h"

struct CPUHexagonTLBContext {
    uint64_t entries[MAX_TLB_ENTRIES];
};

extern void hex_tlbw(CPUHexagonState *env, uint32_t index, uint64_t value);
extern uint32_t hex_tlb_lookup(CPUHexagonState *env, uint32_t ssr, uint32_t VA);
extern void hex_mmu_realize(CPUHexagonState *env);
extern void hex_mmu_on(CPUHexagonState *env);
extern void hex_mmu_off(CPUHexagonState *env);
extern void hex_mmu_mode_change(CPUHexagonState *env);
extern bool hex_tlb_find_match(CPUHexagonState *env, target_ulong VA,
                               MMUAccessType access_type, hwaddr *PA, int *prot,
                               int *size, int32_t *excp, int mmu_idx);
extern int hex_tlb_check_overlap(CPUHexagonState *env, uint64_t entry,
                                 uint64_t index);
extern void hex_tlb_lock(CPUHexagonState *env);
extern void hex_tlb_unlock(CPUHexagonState *env);
void dump_mmu(CPUHexagonState *env);
#endif
