/*
 * QEMU NE2000 emulation -- isa bus windup
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_NET_NE2000_ISA_H
#define HW_NET_NE2000_ISA_H

#include "hw/hw.h"
#include "hw/qdev.h"
#include "hw/isa/isa.h"
#include "net/net.h"

#define TYPE_ISA_NE2000 "ne2k_isa"

static inline ISADevice *isa_ne2000_init(ISABus *bus, int base, int irq,
                                         NICInfo *nd)
{
    ISADevice *d;

    qemu_check_nic_model(nd, "ne2k_isa");

    d = isa_try_create(bus, TYPE_ISA_NE2000);
    if (d) {
        DeviceState *dev = DEVICE(d);

        qdev_prop_set_uint32(dev, "iobase", base);
        qdev_prop_set_uint32(dev, "irq",    irq);
        qdev_set_nic_properties(dev, nd);
        qdev_init_nofail(dev);
    }
    return d;
}

#endif
