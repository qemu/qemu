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
OBJECT_DECLARE_TYPE(AspeedINTCState, AspeedINTCClass, ASPEED_INTC)

#define ASPEED_INTC_NR_REGS (0x2000 >> 2)
#define ASPEED_INTC_NR_INTS 9

struct AspeedINTCState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    uint32_t regs[ASPEED_INTC_NR_REGS];
    OrIRQState orgates[ASPEED_INTC_NR_INTS];
    qemu_irq output_pins[ASPEED_INTC_NR_INTS];

    uint32_t enable[ASPEED_INTC_NR_INTS];
    uint32_t mask[ASPEED_INTC_NR_INTS];
    uint32_t pending[ASPEED_INTC_NR_INTS];
};

struct AspeedINTCClass {
    SysBusDeviceClass parent_class;

    uint32_t num_lines;
    uint32_t num_ints;
};

#endif /* ASPEED_INTC_H */
