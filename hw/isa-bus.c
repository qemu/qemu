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
#include "monitor.h"
#include "sysbus.h"
#include "isa.h"

struct ISABus {
    BusState qbus;
    qemu_irq *irqs;
    uint32_t assigned;
};
static ISABus *isabus;

static void isabus_dev_print(Monitor *mon, DeviceState *dev, int indent);

static struct BusInfo isa_bus_info = {
    .name      = "ISA",
    .size      = sizeof(ISABus),
    .print_dev = isabus_dev_print,
    .props     = (Property[]) {
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
    if (NULL == dev) {
        dev = qdev_create(NULL, "isabus-bridge");
        qdev_init(dev);
    }

    isabus = FROM_QBUS(ISABus, qbus_create(&isa_bus_info, dev, NULL));
    return isabus;
}

void isa_bus_irqs(qemu_irq *irqs)
{
    isabus->irqs = irqs;
}

void isa_connect_irq(ISADevice *dev, int devnr, int isairq)
{
    assert(devnr >= 0 && devnr < dev->nirqs);
    if (isabus->assigned & (1 << isairq)) {
        fprintf(stderr, "isa irq %d already assigned\n", isairq);
        exit(1);
    }
    if (dev->irqs[devnr]) {
        isabus->assigned |= (1 << isairq);
        dev->isairq[devnr] = isairq;
        *dev->irqs[devnr] = isabus->irqs[isairq];
    }
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

    dev->isairq[0] = -1;
    dev->isairq[1] = -1;
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

static void isabus_dev_print(Monitor *mon, DeviceState *dev, int indent)
{
    ISADevice *d = DO_UPCAST(ISADevice, qdev, dev);

    if (d->isairq[1] != -1) {
        monitor_printf(mon, "%*sisa irqs %d,%d\n", indent, "",
                       d->isairq[0], d->isairq[1]);
    } else if (d->isairq[0] != -1) {
        monitor_printf(mon, "%*sisa irq %d\n", indent, "",
                       d->isairq[0]);
    }
}

static void isabus_bridge_init(SysBusDevice *dev)
{
    /* nothing */
}

static SysBusDeviceInfo isabus_bridge_info = {
    .init = isabus_bridge_init,
    .qdev.name  = "isabus-bridge",
    .qdev.size  = sizeof(SysBusDevice),
};

static void isabus_register_devices(void)
{
    sysbus_register_withprop(&isabus_bridge_info);
}

device_init(isabus_register_devices)
