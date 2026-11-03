/*
 * ARM GICv5 emulation: Interrupt Routing Service (IRS)
 *
 * Copyright (c) 2025 Linaro Limited
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_INTC_ARM_GICV5_H
#define HW_INTC_ARM_GICV5_H

#include "qom/object.h"
#include "hw/core/sysbus.h"
#include "hw/intc/arm_gicv5_common.h"

#define TYPE_ARM_GICV5 "arm-gicv5"

OBJECT_DECLARE_TYPE(GICv5, GICv5Class, ARM_GICV5)

typedef struct GICv5ISTConfig {
    hwaddr base; /* Base address */
    MemTxAttrs txattrs; /* TX attrs to use for this table */
    uint8_t id_bits; /* number of bits in an ID for this table */
    uint8_t l2_idx_bits; /* number of ID bits that index into L2 table */
    uint8_t istsz; /* L2 ISTE size in bytes */
    uint8_t l2bits; /* bits needed to index L2 table in bytes */
    bool structure; /* true if using 2-level table */
    bool valid; /* true if this table is valid and usable */
    /* This caches IST information about pending LPIs */
    GHashTable *lpi_cache;
} GICv5ISTConfig;

/*
 * This class is for TCG-specific state for the GICv5.
 */
struct GICv5 {
    GICv5Common parent_obj;

    /* This is the info from IRS_IST_BASER and IRS_IST_CFGR */
    GICv5ISTConfig phys_lpi_config[NUM_GICV5_DOMAINS];

    /* We cache the HPPI for each CPU for each domain here */
    GICv5PendingIrq *hppi[NUM_GICV5_DOMAINS];
};

struct GICv5Class {
    GICv5CommonClass parent_class;
    DeviceRealize parent_realize;
    ResettablePhases parent_phases;
};

#endif
