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
#include "monitor.h"
#include "sysbus.h"
#include "isa.h"
#include "exec-memory.h"

struct ISABus {
    BusState qbus;
    qemu_irq *irqs;
};
static ISABus *isabus;
target_phys_addr_t isa_mem_base = 0;

static void isabus_dev_print(Monitor *mon, DeviceState *dev, int indent);
static char *isabus_get_fw_dev_path(DeviceState *dev);

static struct BusInfo isa_bus_info = {
    .name      = "ISA",
    .size      = sizeof(ISABus),
    .print_dev = isabus_dev_print,
    .get_fw_dev_path = isabus_get_fw_dev_path,
};

ISABus *isa_bus_new(DeviceState *dev)
{
    if (isabus) {
        fprintf(stderr, "Can't create a second ISA bus\n");
        return NULL;
    }
    if (NULL == dev) {
        dev = qdev_create(NULL, "isabus-bridge");
        qdev_init_nofail(dev);
    }

    isabus = FROM_QBUS(ISABus, qbus_create(&isa_bus_info, dev, NULL));
    return isabus;
}

void isa_bus_irqs(qemu_irq *irqs)
{
    isabus->irqs = irqs;
}

/*
 * isa_get_irq() returns the corresponding qemu_irq entry for the i8259.
 *
 * This function is only for special cases such as the 'ferr', and
 * temporary use for normal devices until they are converted to qdev.
 */
qemu_irq isa_get_irq(int isairq)
{
    if (isairq < 0 || isairq > 15) {
        hw_error("isa irq %d invalid", isairq);
    }
    return isabus->irqs[isairq];
}

void isa_init_irq(ISADevice *dev, qemu_irq *p, int isairq)
{
    assert(dev->nirqs < ARRAY_SIZE(dev->isairq));
    dev->isairq[dev->nirqs] = isairq;
    *p = isa_get_irq(isairq);
    dev->nirqs++;
}

static void isa_init_ioport_one(ISADevice *dev, uint16_t ioport)
{
    assert(dev->nioports < ARRAY_SIZE(dev->ioports));
    dev->ioports[dev->nioports++] = ioport;
}

static int isa_cmp_ports(const void *p1, const void *p2)
{
    return *(uint16_t*)p1 - *(uint16_t*)p2;
}

void isa_init_ioport_range(ISADevice *dev, uint16_t start, uint16_t length)
{
    int i;
    for (i = start; i < start + length; i++) {
        isa_init_ioport_one(dev, i);
    }
    qsort(dev->ioports, dev->nioports, sizeof(dev->ioports[0]), isa_cmp_ports);
}

void isa_init_ioport(ISADevice *dev, uint16_t ioport)
{
    isa_init_ioport_range(dev, ioport, 1);
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

ISADevice *isa_create(const char *name)
{
    DeviceState *dev;

    if (!isabus) {
        hw_error("Tried to create isa device %s with no isa bus present.",
                 name);
    }
    dev = qdev_create(&isabus->qbus, name);
    return DO_UPCAST(ISADevice, qdev, dev);
}

ISADevice *isa_try_create(const char *name)
{
    DeviceState *dev;

    if (!isabus) {
        hw_error("Tried to create isa device %s with no isa bus present.",
                 name);
    }
    dev = qdev_try_create(&isabus->qbus, name);
    return DO_UPCAST(ISADevice, qdev, dev);
}

ISADevice *isa_create_simple(const char *name)
{
    ISADevice *dev;

    dev = isa_create(name);
    qdev_init_nofail(&dev->qdev);
    return dev;
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
    .qdev.fw_name  = "isa",
    .qdev.size  = sizeof(SysBusDevice),
    .qdev.no_user = 1,
};

static void isabus_register_devices(void)
{
    sysbus_register_withprop(&isabus_bridge_info);
}

static char *isabus_get_fw_dev_path(DeviceState *dev)
{
    ISADevice *d = (ISADevice*)dev;
    char path[40];
    int off;

    off = snprintf(path, sizeof(path), "%s", qdev_fw_name(dev));
    if (d->nioports) {
        snprintf(path + off, sizeof(path) - off, "@%04x", d->ioports[0]);
    }

    return strdup(path);
}

MemoryRegion *isa_address_space(ISADevice *dev)
{
    return get_system_memory();
}

device_init(isabus_register_devices)
