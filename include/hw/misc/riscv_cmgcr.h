/*
 * Coherent Manager Global Control Register
 *
 * Copyright (C) 2015 Imagination Technologies
 *
 * Copyright (C) 2025 MIPS
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#ifndef RISCV_CMGCR_H
#define RISCV_CMGCR_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_RISCV_GCR "riscv-gcr"
OBJECT_DECLARE_SIMPLE_TYPE(RISCVGCRState, RISCV_GCR)

#define GCR_BASE_ADDR           0x1fb80000ULL
#define GCR_MAX_VPS             256

typedef struct RISCVGCRVPState RISCVGCRVPState;
struct RISCVGCRVPState {
    uint64_t reset_base;
};

typedef struct RISCVGCRState RISCVGCRState;
struct RISCVGCRState {
    SysBusDevice parent_obj;

    int32_t gcr_rev;
    uint32_t cluster_id;
    uint32_t num_vps;
    uint32_t num_hart;
    uint32_t num_core;
    hwaddr gcr_base;
    MemoryRegion iomem;
    MemoryRegion *cpc_mr;

    uint64_t cpc_base;

    /* VP Local/Other Registers */
    RISCVGCRVPState *vps;
};

#endif /* RISCV_CMGCR_H */
