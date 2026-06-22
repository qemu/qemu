/*
 * Hexagon TLB QOM Device
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_HEXAGON_TLB_H
#define HW_HEXAGON_TLB_H

#include "hw/core/sysbus.h"
#include "qom/object.h"
#include "exec/hwaddr.h"
#include "exec/mmu-access-type.h"
#include "monitor/monitor.h"
#define TYPE_HEXAGON_TLB "hexagon-tlb"
OBJECT_DECLARE_SIMPLE_TYPE(HexagonTLBState, HEXAGON_TLB)

struct HexagonTLBState {
    SysBusDevice parent_obj;

    uint32_t num_entries;
    uint64_t *entries;
};

uint64_t hexagon_tlb_read(HexagonTLBState *tlb, uint32_t index);
void hexagon_tlb_write(HexagonTLBState *tlb, uint32_t index, uint64_t value);

bool hexagon_tlb_find_match(HexagonTLBState *tlb, uint32_t asid,
                            uint32_t VA, MMUAccessType access_type,
                            hwaddr *PA, int *prot, uint64_t *size,
                            int32_t *excp, int *cause_code, int mmu_idx);

uint32_t hexagon_tlb_lookup(HexagonTLBState *tlb, uint32_t asid,
                            uint32_t VA, int *cause_code);

int hexagon_tlb_check_overlap(HexagonTLBState *tlb, uint64_t entry,
                              uint64_t index);

void hexagon_tlb_dump(Monitor *mon, HexagonTLBState *tlb);

bool hexagon_tlb_dump_entry(Monitor *mon, uint64_t entry);

uint32_t hexagon_tlb_get_num_entries(HexagonTLBState *tlb);

#endif /* HW_HEXAGON_TLB_H */
