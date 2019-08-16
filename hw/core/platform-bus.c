/*
 *  Platform Bus device to support dynamic Sysbus devices
 *
 * Copyright (C) 2014 Freescale Semiconductor, Inc. All rights reserved.
 *
 * Author: Alexander Graf, <agraf@suse.de>
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

#include "qemu/osdep.h"
#include "hw/platform-bus.h"
#include "hw/qdev-properties.h"
#include "qemu/error-report.h"
#include "qemu/module.h"


/*
 * Returns the PlatformBus IRQ number for a SysBusDevice irq number or -1 if
 * the IRQ is not mapped on this Platform bus.
 */
int platform_bus_get_irqn(PlatformBusDevice *pbus, SysBusDevice *sbdev,
                          int n)
{
    qemu_irq sbirq = sysbus_get_connected_irq(sbdev, n);
    int i;

    for (i = 0; i < pbus->num_irqs; i++) {
        if (pbus->irqs[i] == sbirq) {
            return i;
        }
    }

    /* IRQ not mapped on platform bus */
    return -1;
}

/*
 * Returns the PlatformBus MMIO region offset for Region n of a SysBusDevice or
 * -1 if the region is not mapped on this Platform bus.
 */
hwaddr platform_bus_get_mmio_addr(PlatformBusDevice *pbus, SysBusDevice *sbdev,
                                  int n)
{
    MemoryRegion *pbus_mr = &pbus->mmio;
    MemoryRegion *sbdev_mr = sysbus_mmio_get_region(sbdev, n);
    Object *pbus_mr_obj = OBJECT(pbus_mr);
    Object *parent_mr;

    if (!memory_region_is_mapped(sbdev_mr)) {
        /* Region is not mapped? */
        return -1;
    }

    parent_mr = object_property_get_link(OBJECT(sbdev_mr), "container", NULL);

    assert(parent_mr);
    if (parent_mr != pbus_mr_obj) {
        /* MMIO region is not mapped on platform bus */
        return -1;
    }

    return object_property_get_uint(OBJECT(sbdev_mr), "addr", NULL);
}

static void platform_bus_count_irqs(SysBusDevice *sbdev, void *opaque)
{
    PlatformBusDevice *pbus = opaque;
    qemu_irq sbirq;
    int n, i;

    for (n = 0; ; n++) {
        if (!sysbus_has_irq(sbdev, n)) {
            break;
        }

        sbirq = sysbus_get_connected_irq(sbdev, n);
        for (i = 0; i < pbus->num_irqs; i++) {
            if (pbus->irqs[i] == sbirq) {
                bitmap_set(pbus->used_irqs, i, 1);
                break;
            }
        }
    }
}

/*
 * Loop through all sysbus devices and look for unassigned IRQ lines as well as
 * unassociated MMIO regions. Connect them to the platform bus if available.
 */
static void plaform_bus_refresh_irqs(PlatformBusDevice *pbus)
{
    bitmap_zero(pbus->used_irqs, pbus->num_irqs);
    foreach_dynamic_sysbus_device(platform_bus_count_irqs, pbus);
}

static void platform_bus_map_irq(PlatformBusDevice *pbus, SysBusDevice *sbdev,
                                 int n)
{
    int max_irqs = pbus->num_irqs;
    int irqn;

    if (sysbus_is_irq_connected(sbdev, n)) {
        /* IRQ is already mapped, nothing to do */
        return;
    }

    irqn = find_first_zero_bit(pbus->used_irqs, max_irqs);
    if (irqn >= max_irqs) {
        error_report("Platform Bus: Can not fit IRQ line");
        exit(1);
    }

    set_bit(irqn, pbus->used_irqs);
    sysbus_connect_irq(sbdev, n, pbus->irqs[irqn]);
}

static void platform_bus_map_mmio(PlatformBusDevice *pbus, SysBusDevice *sbdev,
                                  int n)
{
    MemoryRegion *sbdev_mr = sysbus_mmio_get_region(sbdev, n);
    uint64_t size = memory_region_size(sbdev_mr);
    uint64_t alignment = (1ULL << (63 - clz64(size + size - 1)));
    uint64_t off;
    bool found_region = false;

    if (memory_region_is_mapped(sbdev_mr)) {
        /* Region is already mapped, nothing to do */
        return;
    }

    /*
     * Look for empty space in the MMIO space that is naturally aligned with
     * the target device's memory region
     */
    for (off = 0; off < pbus->mmio_size; off += alignment) {
        if (!memory_region_find(&pbus->mmio, off, size).mr) {
            found_region = true;
            break;
        }
    }

    if (!found_region) {
        error_report("Platform Bus: Can not fit MMIO region of size %"PRIx64,
                     size);
        exit(1);
    }

    /* Map the device's region into our Platform Bus MMIO space */
    memory_region_add_subregion(&pbus->mmio, off, sbdev_mr);
}

/*
 * Look for unassigned IRQ lines as well as unassociated MMIO regions.
 * Connect them to the platform bus if available.
 */
void platform_bus_link_device(PlatformBusDevice *pbus, SysBusDevice *sbdev)
{
    int i;

    for (i = 0; sysbus_has_irq(sbdev, i); i++) {
        platform_bus_map_irq(pbus, sbdev, i);
    }

    for (i = 0; sysbus_has_mmio(sbdev, i); i++) {
        platform_bus_map_mmio(pbus, sbdev, i);
    }
}

static void platform_bus_realize(DeviceState *dev, Error **errp)
{
    PlatformBusDevice *pbus;
    SysBusDevice *d;
    int i;

    d = SYS_BUS_DEVICE(dev);
    pbus = PLATFORM_BUS_DEVICE(dev);

    memory_region_init(&pbus->mmio, NULL, "platform bus", pbus->mmio_size);
    sysbus_init_mmio(d, &pbus->mmio);

    pbus->used_irqs = bitmap_new(pbus->num_irqs);
    pbus->irqs = g_new0(qemu_irq, pbus->num_irqs);
    for (i = 0; i < pbus->num_irqs; i++) {
        sysbus_init_irq(d, &pbus->irqs[i]);
    }

    /* some devices might be initialized before so update used IRQs map */
    plaform_bus_refresh_irqs(pbus);
}

static Property platform_bus_properties[] = {
    DEFINE_PROP_UINT32("num_irqs", PlatformBusDevice, num_irqs, 0),
    DEFINE_PROP_UINT32("mmio_size", PlatformBusDevice, mmio_size, 0),
    DEFINE_PROP_END_OF_LIST()
};

static void platform_bus_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = platform_bus_realize;
    dc->props = platform_bus_properties;
}

static const TypeInfo platform_bus_info = {
    .name          = TYPE_PLATFORM_BUS_DEVICE,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PlatformBusDevice),
    .class_init    = platform_bus_class_init,
};

static void platform_bus_register_types(void)
{
    type_register_static(&platform_bus_info);
}

type_init(platform_bus_register_types)
