/*
 * isa bus support for qdev.
 *
 * Copyright (c) 2009 Gerd Hoffmann <kraxel@redhat.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#include "hw.h"
#include "sysemu.h"
#include "isa.h"

struct ISABus {
    BusState qbus;
};
static ISABus *isabus;

static struct BusInfo isa_bus_info = {
    .name  = "ISA",
    .size  = sizeof(ISABus),
    .props = (Property[]) {
        DEFINE_PROP_HEX32("iobase",  ISADevice, iobase[0], -1),
        DEFINE_PROP_HEX32("iobase2", ISADevice, iobase[1], -1),
        DEFINE_PROP_END_OF_LIST(),
    }
};

ISABus *isa_bus_new(DeviceState *dev)
{
    if (isabus) {
        fprintf(stderr, "Can't create a second ISA bus\n");
        return NULL;
    }

    isabus = FROM_QBUS(ISABus, qbus_create(&isa_bus_info, dev, NULL));
    return isabus;
}

void isa_connect_irq(ISADevice *dev, int n, qemu_irq irq)
{
    assert(n >= 0 && n < dev->nirqs);
    if (dev->irqs[n])
        *dev->irqs[n] = irq;
}

void isa_init_irq(ISADevice *dev, qemu_irq *p)
{
    assert(dev->nirqs < ARRAY_SIZE(dev->irqs));
    dev->irqs[dev->nirqs] = p;
    dev->nirqs++;
}

static void isa_qdev_init(DeviceState *qdev, DeviceInfo *base)
{
    ISADevice *dev = DO_UPCAST(ISADevice, qdev, qdev);
    ISADeviceInfo *info = DO_UPCAST(ISADeviceInfo, qdev, base);

    info->init(dev);
}

void isa_qdev_register(ISADeviceInfo *info)
{
    info->qdev.init = isa_qdev_init;
    info->qdev.bus_info = &isa_bus_info;
    qdev_register(&info->qdev);
}

ISADevice *isa_create_simple(const char *name, uint32_t iobase, uint32_t iobase2)
{
    DeviceState *dev;
    ISADevice *isa;

    if (!isabus) {
        fprintf(stderr, "Tried to create isa device %s with no isa bus present.\n", name);
        return NULL;
    }
    dev = qdev_create(&isabus->qbus, name);
    isa = DO_UPCAST(ISADevice, qdev, dev);
    isa->iobase[0] = iobase;
    isa->iobase[1] = iobase2;
    qdev_init(dev);
    return isa;
}
