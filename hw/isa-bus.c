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

/*
 * isa_reserve_irq() reserves the ISA irq and returns the corresponding
 * qemu_irq entry for the i8259.
 *
 * This function is only for special cases such as the 'ferr', and
 * temporary use for normal devices until they are converted to qdev.
 */
qemu_irq isa_reserve_irq(int isairq)
{
    if (isairq < 0 || isairq > 15) {
        fprintf(stderr, "isa irq %d invalid\n", isairq);
        exit(1);
    }
    if (isabus->assigned & (1 << isairq)) {
        fprintf(stderr, "isa irq %d already assigned\n", isairq);
        exit(1);
    }
    isabus->assigned |= (1 << isairq);
    return isabus->irqs[isairq];
}

void isa_init_irq(ISADevice *dev, qemu_irq *p, int isairq)
{
    assert(dev->nirqs < ARRAY_SIZE(dev->isairq));
    if (isabus->assigned & (1 << isairq)) {
        fprintf(stderr, "isa irq %d already assigned\n", isairq);
        exit(1);
    }
    isabus->assigned |= (1 << isairq);
    dev->isairq[dev->nirqs] = isairq;
    *p = isabus->irqs[isairq];
    dev->nirqs++;
}

static int isa_qdev_init(DeviceState *qdev, DeviceInfo *base)
{
    ISADevice *dev = DO_UPCAST(ISADevice, qdev, qdev);
    ISADeviceInfo *info = DO_UPCAST(ISADeviceInfo, qdev, base);

    dev->isairq[0] = -1;
    dev->isairq[1] = -1;

    return info->init(dev);
}

void isa_qdev_register(ISADeviceInfo *info)
{
    info->qdev.init = isa_qdev_init;
    info->qdev.bus_info = &isa_bus_info;
    qdev_register(&info->qdev);
}

ISADevice *isa_create_simple(const char *name)
{
    DeviceState *dev;

    if (!isabus) {
        fprintf(stderr, "Tried to create isa device %s with no isa bus present.\n", name);
        return NULL;
    }
    dev = qdev_create(&isabus->qbus, name);
    qdev_init(dev);
    return DO_UPCAST(ISADevice, qdev, dev);
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

static int isabus_bridge_init(SysBusDevice *dev)
{
    /* nothing */
    return 0;
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
