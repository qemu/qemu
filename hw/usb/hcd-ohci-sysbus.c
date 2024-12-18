/*
 * QEMU USB OHCI Emulation
 * Copyright (c) 2006 Openedhand Ltd.
 * Copyright (c) 2010 CodeSourcery
 * Copyright (c) 2024 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "hw/usb.h"
#include "migration/vmstate.h"
#include "hw/sysbus.h"
#include "hw/qdev-dma.h"
#include "hw/qdev-properties.h"
#include "trace.h"
#include "hcd-ohci.h"


static void ohci_sysbus_realize(DeviceState *dev, Error **errp)
{
    OHCISysBusState *s = SYSBUS_OHCI(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    Error *err = NULL;

    usb_ohci_init(&s->ohci, dev, s->num_ports, s->dma_offset,
                  s->masterbus, s->firstport,
                  &address_space_memory, ohci_sysbus_die, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    sysbus_init_irq(sbd, &s->ohci.irq);
    sysbus_init_mmio(sbd, &s->ohci.mem);
}

static void ohci_sysbus_reset(DeviceState *dev)
{
    OHCISysBusState *s = SYSBUS_OHCI(dev);
    OHCIState *ohci = &s->ohci;

    ohci_hard_reset(ohci);
}

static const Property ohci_sysbus_properties[] = {
    DEFINE_PROP_STRING("masterbus", OHCISysBusState, masterbus),
    DEFINE_PROP_UINT32("num-ports", OHCISysBusState, num_ports, 3),
    DEFINE_PROP_UINT32("firstport", OHCISysBusState, firstport, 0),
    DEFINE_PROP_DMAADDR("dma-offset", OHCISysBusState, dma_offset, 0),
};

static void ohci_sysbus_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = ohci_sysbus_realize;
    set_bit(DEVICE_CATEGORY_USB, dc->categories);
    dc->desc = "OHCI USB Controller";
    device_class_set_props(dc, ohci_sysbus_properties);
    device_class_set_legacy_reset(dc, ohci_sysbus_reset);
}

static const TypeInfo ohci_sysbus_types[] = {
    {
        .name          = TYPE_SYSBUS_OHCI,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(OHCISysBusState),
        .class_init    = ohci_sysbus_class_init,
    },
};

DEFINE_TYPES(ohci_sysbus_types);
