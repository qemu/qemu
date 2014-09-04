/*
 *  System (CPU) Bus device support code
 *
 *  Copyright (c) 2009 CodeSourcery
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

#include "hw/sysbus.h"
#include "monitor/monitor.h"
#include "exec/address-spaces.h"

static void sysbus_dev_print(Monitor *mon, DeviceState *dev, int indent);
static char *sysbus_get_fw_dev_path(DeviceState *dev);

static void system_bus_class_init(ObjectClass *klass, void *data)
{
    BusClass *k = BUS_CLASS(klass);

    k->print_dev = sysbus_dev_print;
    k->get_fw_dev_path = sysbus_get_fw_dev_path;
}

static const TypeInfo system_bus_info = {
    .name = TYPE_SYSTEM_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(BusState),
    .class_init = system_bus_class_init,
};

void sysbus_connect_irq(SysBusDevice *dev, int n, qemu_irq irq)
{
    assert(n >= 0 && n < dev->num_irq);
    dev->irqs[n] = NULL;
    if (dev->irqp[n]) {
        *dev->irqp[n] = irq;
    }
}

static void sysbus_mmio_map_common(SysBusDevice *dev, int n, hwaddr addr,
                                   bool may_overlap, int priority)
{
    assert(n >= 0 && n < dev->num_mmio);

    if (dev->mmio[n].addr == addr) {
        /* ??? region already mapped here.  */
        return;
    }
    if (dev->mmio[n].addr != (hwaddr)-1) {
        /* Unregister previous mapping.  */
        memory_region_del_subregion(get_system_memory(), dev->mmio[n].memory);
    }
    dev->mmio[n].addr = addr;
    if (may_overlap) {
        memory_region_add_subregion_overlap(get_system_memory(),
                                            addr,
                                            dev->mmio[n].memory,
                                            priority);
    }
    else {
        memory_region_add_subregion(get_system_memory(),
                                    addr,
                                    dev->mmio[n].memory);
    }
}

void sysbus_mmio_map(SysBusDevice *dev, int n, hwaddr addr)
{
    sysbus_mmio_map_common(dev, n, addr, false, 0);
}

void sysbus_mmio_map_overlap(SysBusDevice *dev, int n, hwaddr addr,
                             int priority)
{
    sysbus_mmio_map_common(dev, n, addr, true, priority);
}

/* Request an IRQ source.  The actual IRQ object may be populated later.  */
void sysbus_init_irq(SysBusDevice *dev, qemu_irq *p)
{
    int n;

    assert(dev->num_irq < QDEV_MAX_IRQ);
    n = dev->num_irq++;
    dev->irqp[n] = p;
}

/* Pass IRQs from a target device.  */
void sysbus_pass_irq(SysBusDevice *dev, SysBusDevice *target)
{
    int i;
    assert(dev->num_irq == 0);
    dev->num_irq = target->num_irq;
    for (i = 0; i < dev->num_irq; i++) {
        dev->irqp[i] = target->irqp[i];
    }
}

void sysbus_init_mmio(SysBusDevice *dev, MemoryRegion *memory)
{
    int n;

    assert(dev->num_mmio < QDEV_MAX_MMIO);
    n = dev->num_mmio++;
    dev->mmio[n].addr = -1;
    dev->mmio[n].memory = memory;
}

MemoryRegion *sysbus_mmio_get_region(SysBusDevice *dev, int n)
{
    return dev->mmio[n].memory;
}

void sysbus_init_ioports(SysBusDevice *dev, pio_addr_t ioport, pio_addr_t size)
{
    pio_addr_t i;

    for (i = 0; i < size; i++) {
        assert(dev->num_pio < QDEV_MAX_PIO);
        dev->pio[dev->num_pio++] = ioport++;
    }
}

static int sysbus_device_init(DeviceState *dev)
{
    SysBusDevice *sd = SYS_BUS_DEVICE(dev);
    SysBusDeviceClass *sbc = SYS_BUS_DEVICE_GET_CLASS(sd);

    if (!sbc->init) {
        return 0;
    }
    return sbc->init(sd);
}

DeviceState *sysbus_create_varargs(const char *name,
                                   hwaddr addr, ...)
{
    DeviceState *dev;
    SysBusDevice *s;
    va_list va;
    qemu_irq irq;
    int n;

    dev = qdev_create(NULL, name);
    s = SYS_BUS_DEVICE(dev);
    qdev_init_nofail(dev);
    if (addr != (hwaddr)-1) {
        sysbus_mmio_map(s, 0, addr);
    }
    va_start(va, addr);
    n = 0;
    while (1) {
        irq = va_arg(va, qemu_irq);
        if (!irq) {
            break;
        }
        sysbus_connect_irq(s, n, irq);
        n++;
    }
    va_end(va);
    return dev;
}

DeviceState *sysbus_try_create_varargs(const char *name,
                                       hwaddr addr, ...)
{
    DeviceState *dev;
    SysBusDevice *s;
    va_list va;
    qemu_irq irq;
    int n;

    dev = qdev_try_create(NULL, name);
    if (!dev) {
        return NULL;
    }
    s = SYS_BUS_DEVICE(dev);
    qdev_init_nofail(dev);
    if (addr != (hwaddr)-1) {
        sysbus_mmio_map(s, 0, addr);
    }
    va_start(va, addr);
    n = 0;
    while (1) {
        irq = va_arg(va, qemu_irq);
        if (!irq) {
            break;
        }
        sysbus_connect_irq(s, n, irq);
        n++;
    }
    va_end(va);
    return dev;
}

static void sysbus_dev_print(Monitor *mon, DeviceState *dev, int indent)
{
    SysBusDevice *s = SYS_BUS_DEVICE(dev);
    hwaddr size;
    int i;

    monitor_printf(mon, "%*sirq %d\n", indent, "", s->num_irq);
    for (i = 0; i < s->num_mmio; i++) {
        size = memory_region_size(s->mmio[i].memory);
        monitor_printf(mon, "%*smmio " TARGET_FMT_plx "/" TARGET_FMT_plx "\n",
                       indent, "", s->mmio[i].addr, size);
    }
}

static char *sysbus_get_fw_dev_path(DeviceState *dev)
{
    SysBusDevice *s = SYS_BUS_DEVICE(dev);
    char path[40];
    int off;

    off = snprintf(path, sizeof(path), "%s", qdev_fw_name(dev));

    if (s->num_mmio) {
        snprintf(path + off, sizeof(path) - off, "@"TARGET_FMT_plx,
                 s->mmio[0].addr);
    } else if (s->num_pio) {
        snprintf(path + off, sizeof(path) - off, "@i%04x", s->pio[0]);
    }

    return g_strdup(path);
}

void sysbus_add_io(SysBusDevice *dev, hwaddr addr,
                       MemoryRegion *mem)
{
    memory_region_add_subregion(get_system_io(), addr, mem);
}

void sysbus_del_io(SysBusDevice *dev, MemoryRegion *mem)
{
    memory_region_del_subregion(get_system_io(), mem);
}

MemoryRegion *sysbus_address_space(SysBusDevice *dev)
{
    return get_system_memory();
}

static void sysbus_device_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *k = DEVICE_CLASS(klass);
    k->init = sysbus_device_init;
    k->bus_type = TYPE_SYSTEM_BUS;
    /*
     * device_add plugs devices into suitable bus.  For "real" buses,
     * that actually connects the device.  For sysbus, the connections
     * need to be made separately, and device_add can't do that.  The
     * device would be left unconnected, and could not possibly work.
     */
    k->cannot_instantiate_with_device_add_yet = true;
}

static const TypeInfo sysbus_device_type_info = {
    .name = TYPE_SYS_BUS_DEVICE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(SysBusDevice),
    .abstract = true,
    .class_size = sizeof(SysBusDeviceClass),
    .class_init = sysbus_device_class_init,
};

/* This is a nasty hack to allow passing a NULL bus to qdev_create.  */
static BusState *main_system_bus;

static void main_system_bus_create(void)
{
    /* assign main_system_bus before qbus_create_inplace()
     * in order to make "if (bus != sysbus_get_default())" work */
    main_system_bus = g_malloc0(system_bus_info.instance_size);
    qbus_create_inplace(main_system_bus, system_bus_info.instance_size,
                        TYPE_SYSTEM_BUS, NULL, "main-system-bus");
    OBJECT(main_system_bus)->free = g_free;
    object_property_add_child(container_get(qdev_get_machine(),
                                            "/unattached"),
                              "sysbus", OBJECT(main_system_bus), NULL);
}

BusState *sysbus_get_default(void)
{
    if (!main_system_bus) {
        main_system_bus_create();
    }
    return main_system_bus;
}

static void sysbus_register_types(void)
{
    type_register_static(&system_bus_info);
    type_register_static(&sysbus_device_type_info);
}

type_init(sysbus_register_types)
