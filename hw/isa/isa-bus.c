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
#include "hw/hw.h"
#include "monitor/monitor.h"
#include "hw/sysbus.h"
#include "sysemu/sysemu.h"
#include "hw/isa/isa.h"
#include "exec/address-spaces.h"

static ISABus *isabus;
hwaddr isa_mem_base = 0;

static void isabus_dev_print(Monitor *mon, DeviceState *dev, int indent);
static char *isabus_get_fw_dev_path(DeviceState *dev);

static void isa_bus_class_init(ObjectClass *klass, void *data)
{
    BusClass *k = BUS_CLASS(klass);

    k->print_dev = isabus_dev_print;
    k->get_fw_dev_path = isabus_get_fw_dev_path;
}

static const TypeInfo isa_bus_info = {
    .name = TYPE_ISA_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(ISABus),
    .class_init = isa_bus_class_init,
};

ISABus *isa_bus_new(DeviceState *dev, MemoryRegion *address_space_io)
{
    if (isabus) {
        fprintf(stderr, "Can't create a second ISA bus\n");
        return NULL;
    }
    if (NULL == dev) {
        dev = qdev_create(NULL, "isabus-bridge");
        qdev_init_nofail(dev);
    }

    isabus = ISA_BUS(qbus_create(TYPE_ISA_BUS, dev, NULL));
    isabus->address_space_io = address_space_io;
    return isabus;
}

void isa_bus_irqs(ISABus *bus, qemu_irq *irqs)
{
    if (!bus) {
        hw_error("Can't set isa irqs with no isa bus present.");
    }
    bus->irqs = irqs;
}

/*
 * isa_get_irq() returns the corresponding qemu_irq entry for the i8259.
 *
 * This function is only for special cases such as the 'ferr', and
 * temporary use for normal devices until they are converted to qdev.
 */
qemu_irq isa_get_irq(ISADevice *dev, int isairq)
{
    assert(!dev || ISA_BUS(qdev_get_parent_bus(DEVICE(dev))) == isabus);
    if (isairq < 0 || isairq > 15) {
        hw_error("isa irq %d invalid", isairq);
    }
    return isabus->irqs[isairq];
}

void isa_init_irq(ISADevice *dev, qemu_irq *p, int isairq)
{
    assert(dev->nirqs < ARRAY_SIZE(dev->isairq));
    dev->isairq[dev->nirqs] = isairq;
    *p = isa_get_irq(dev, isairq);
    dev->nirqs++;
}

static inline void isa_init_ioport(ISADevice *dev, uint16_t ioport)
{
    if (dev && (dev->ioport_id == 0 || ioport < dev->ioport_id)) {
        dev->ioport_id = ioport;
    }
}

void isa_register_ioport(ISADevice *dev, MemoryRegion *io, uint16_t start)
{
    memory_region_add_subregion(isabus->address_space_io, start, io);
    isa_init_ioport(dev, start);
}

void isa_register_portio_list(ISADevice *dev, uint16_t start,
                              const MemoryRegionPortio *pio_start,
                              void *opaque, const char *name)
{
    PortioList *piolist = g_new(PortioList, 1);

    /* START is how we should treat DEV, regardless of the actual
       contents of the portio array.  This is how the old code
       actually handled e.g. the FDC device.  */
    isa_init_ioport(dev, start);

    portio_list_init(piolist, OBJECT(dev), pio_start, opaque, name);
    portio_list_add(piolist, isabus->address_space_io, start);
}

static void isa_device_init(Object *obj)
{
    ISADevice *dev = ISA_DEVICE(obj);

    dev->isairq[0] = -1;
    dev->isairq[1] = -1;
}

ISADevice *isa_create(ISABus *bus, const char *name)
{
    DeviceState *dev;

    if (!bus) {
        hw_error("Tried to create isa device %s with no isa bus present.",
                 name);
    }
    dev = qdev_create(BUS(bus), name);
    return ISA_DEVICE(dev);
}

ISADevice *isa_try_create(ISABus *bus, const char *name)
{
    DeviceState *dev;

    if (!bus) {
        hw_error("Tried to create isa device %s with no isa bus present.",
                 name);
    }
    dev = qdev_try_create(BUS(bus), name);
    return ISA_DEVICE(dev);
}

ISADevice *isa_create_simple(ISABus *bus, const char *name)
{
    ISADevice *dev;

    dev = isa_create(bus, name);
    qdev_init_nofail(DEVICE(dev));
    return dev;
}

ISADevice *isa_vga_init(ISABus *bus)
{
    switch (vga_interface_type) {
    case VGA_CIRRUS:
        return isa_create_simple(bus, "isa-cirrus-vga");
    case VGA_QXL:
        fprintf(stderr, "%s: qxl: no PCI bus\n", __func__);
        return NULL;
    case VGA_STD:
        return isa_create_simple(bus, "isa-vga");
    case VGA_VMWARE:
        fprintf(stderr, "%s: vmware_vga: no PCI bus\n", __func__);
        return NULL;
    case VGA_NONE:
    default:
        return NULL;
    }
}

static void isabus_dev_print(Monitor *mon, DeviceState *dev, int indent)
{
    ISADevice *d = ISA_DEVICE(dev);

    if (d->isairq[1] != -1) {
        monitor_printf(mon, "%*sisa irqs %d,%d\n", indent, "",
                       d->isairq[0], d->isairq[1]);
    } else if (d->isairq[0] != -1) {
        monitor_printf(mon, "%*sisa irq %d\n", indent, "",
                       d->isairq[0]);
    }
}

static void isabus_bridge_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->fw_name = "isa";
}

static const TypeInfo isabus_bridge_info = {
    .name          = "isabus-bridge",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SysBusDevice),
    .class_init    = isabus_bridge_class_init,
};

static void isa_device_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *k = DEVICE_CLASS(klass);
    k->bus_type = TYPE_ISA_BUS;
}

static const TypeInfo isa_device_type_info = {
    .name = TYPE_ISA_DEVICE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(ISADevice),
    .instance_init = isa_device_init,
    .abstract = true,
    .class_size = sizeof(ISADeviceClass),
    .class_init = isa_device_class_init,
};

static void isabus_register_types(void)
{
    type_register_static(&isa_bus_info);
    type_register_static(&isabus_bridge_info);
    type_register_static(&isa_device_type_info);
}

static char *isabus_get_fw_dev_path(DeviceState *dev)
{
    ISADevice *d = ISA_DEVICE(dev);
    char path[40];
    int off;

    off = snprintf(path, sizeof(path), "%s", qdev_fw_name(dev));
    if (d->ioport_id) {
        snprintf(path + off, sizeof(path) - off, "@%04x", d->ioport_id);
    }

    return g_strdup(path);
}

MemoryRegion *isa_address_space(ISADevice *dev)
{
    return get_system_memory();
}

MemoryRegion *isa_address_space_io(ISADevice *dev)
{
    if (dev) {
        return isa_bus_from_device(dev)->address_space_io;
    }

    return isabus->address_space_io;
}

type_init(isabus_register_types)
