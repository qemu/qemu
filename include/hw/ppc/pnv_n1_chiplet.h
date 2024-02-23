/*
 * QEMU PowerPC N1 chiplet model
 *
 * Copyright (c) 2023, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PPC_PNV_N1_CHIPLET_H
#define PPC_PNV_N1_CHIPLET_H

#include "hw/ppc/pnv_nest_pervasive.h"

#define TYPE_PNV_N1_CHIPLET "pnv-N1-chiplet"
#define PNV_N1_CHIPLET(obj) OBJECT_CHECK(PnvN1Chiplet, (obj), TYPE_PNV_N1_CHIPLET)

typedef struct PnvPbScom {
    uint64_t mode;
    uint64_t hp_mode2_curr;
} PnvPbScom;

typedef struct PnvN1Chiplet {
    DeviceState  parent;
    MemoryRegion xscom_pb_eq_mr;
    MemoryRegion xscom_pb_es_mr;
    PnvNestChipletPervasive nest_pervasive; /* common pervasive chiplet unit */
#define PNV_PB_SCOM_EQ_SIZE 8
    PnvPbScom eq[PNV_PB_SCOM_EQ_SIZE];
#define PNV_PB_SCOM_ES_SIZE 4
    PnvPbScom es[PNV_PB_SCOM_ES_SIZE];
} PnvN1Chiplet;
#endif /*PPC_PNV_N1_CHIPLET_H */
