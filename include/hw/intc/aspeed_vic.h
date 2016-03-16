/*
 * ASPEED Interrupt Controller (New)
 *
 * Andrew Jeffery <andrew@aj.id.au>
 *
 * Copyright 2016 IBM Corp.
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 *
 * Need to add SVIC and CVIC support
 */
#ifndef ASPEED_VIC_H
#define ASPEED_VIC_H

#include "hw/sysbus.h"

#define TYPE_ASPEED_VIC "aspeed.vic"
#define ASPEED_VIC(obj) OBJECT_CHECK(AspeedVICState, (obj), TYPE_ASPEED_VIC)

#define ASPEED_VIC_NR_IRQS 51

typedef struct AspeedVICState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    qemu_irq irq;
    qemu_irq fiq;

    uint64_t level;
    uint64_t raw;
    uint64_t select;
    uint64_t enable;
    uint64_t trigger;

    /* 0=edge, 1=level */
    uint64_t sense;

    /* 0=single-edge, 1=dual-edge */
    uint64_t dual_edge;

    /* 0=low-sensitive/falling-edge, 1=high-sensitive/rising-edge */
    uint64_t event;
} AspeedVICState;

#endif /* ASPEED_VIC_H */
