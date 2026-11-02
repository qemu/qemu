/*
 * BCM2838 Gigabit Ethernet emulation
 *
 * Copyright (C) 2022 Ovchinnikov Vitalii <vitalii.ovchinnikov@auriga.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef BCM2838_GENET_H
#define BCM2838_GENET_H

#include "net/net.h"
#include "hw/core/sysbus.h"

#define TYPE_BCM2838_GENET "bcm2838-genet"
OBJECT_DECLARE_SIMPLE_TYPE(BCM2838GenetState, BCM2838_GENET)

#define BCM2838_GENET_REV_MAJOR         6
#define BCM2838_GENET_REV_MINOR         0

typedef struct {
    uint8_t stub_area[0x10000]; /* temporary stub */
} BCM2838GenetRegs;

struct BCM2838GenetState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/

    MemoryRegion regs_mr;
    AddressSpace dma_as;

    BCM2838GenetRegs regs;

    qemu_irq irq_default;
    qemu_irq irq_prio;
};

#endif /* BCM2838_GENET_H */
