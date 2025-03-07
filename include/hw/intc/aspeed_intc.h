/*
 * ASPEED INTC Controller
 *
 * Copyright (C) 2024 ASPEED Technology Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef ASPEED_INTC_H
#define ASPEED_INTC_H

#include "hw/sysbus.h"
#include "qom/object.h"
#include "hw/or-irq.h"

#define TYPE_ASPEED_INTC "aspeed.intc"
#define TYPE_ASPEED_2700_INTC TYPE_ASPEED_INTC "-ast2700"
#define TYPE_ASPEED_2700_INTCIO TYPE_ASPEED_INTC "io-ast2700"
OBJECT_DECLARE_TYPE(AspeedINTCState, AspeedINTCClass, ASPEED_INTC)

#define ASPEED_INTC_MAX_INPINS 10
#define ASPEED_INTC_MAX_OUTPINS 19

typedef struct AspeedINTCIRQ {
    int inpin_idx;
    int outpin_idx;
    int num_outpins;
    uint32_t enable_reg;
    uint32_t status_reg;
} AspeedINTCIRQ;

struct AspeedINTCState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    MemoryRegion iomem_container;

    uint32_t *regs;
    OrIRQState orgates[ASPEED_INTC_MAX_INPINS];
    qemu_irq output_pins[ASPEED_INTC_MAX_OUTPINS];

    uint32_t enable[ASPEED_INTC_MAX_INPINS];
    uint32_t mask[ASPEED_INTC_MAX_INPINS];
    uint32_t pending[ASPEED_INTC_MAX_INPINS];
};

struct AspeedINTCClass {
    SysBusDeviceClass parent_class;

    uint32_t num_lines;
    uint32_t num_inpins;
    uint32_t num_outpins;
    uint64_t mem_size;
    uint64_t nr_regs;
    uint64_t reg_offset;
    const MemoryRegionOps *reg_ops;
    const AspeedINTCIRQ *irq_table;
    int irq_table_count;
};

#endif /* ASPEED_INTC_H */
