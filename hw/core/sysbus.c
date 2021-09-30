/*
 *  System (CPU) Bus device support code
 *
 *  Copyright (c) 2009 CodeSourcery
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
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/sysbus.h"
#include "monitor/monitor.h"
#include "exec/address-spaces.h"

static void sysbus_dev_print(Monitor *mon, DeviceState *dev, int indent);
static char *sysbus_get_fw_dev_path(DeviceState *dev);

typedef struct SysBusFind {
    void *opaque;
    FindSysbusDeviceFunc *func;
} SysBusFind;

/* Run func() for every sysbus device, traverse the tree for everything else */
static int find_sysbus_device(Object *obj, void *opaque)
{
    SysBusFind *find = opaque;
    Object *dev;
    SysBusDevice *sbdev;

    dev = object_dynamic_cast(obj, TYPE_SYS_BUS_DEVICE);
    sbdev = (SysBusDevice *)dev;

    if (!sbdev) {
        /* Container, traverse it for children */
        return object_child_foreach(obj, find_sysbus_device, opaque);
    }

    find->func(sbdev, find->opaque);

    return 0;
}

/*
 * Loop through all dynamically created sysbus devices and call
 * func() for each instance.
 */
void foreach_dynamic_sysbus_device(FindSysbusDeviceFunc *func, void *opaque)
{
    Object *container;
    SysBusFind find = {
        .func = func,
        .opaque = opaque,
    };

    /* Loop through all sysbus devices that were spawned outside the machine */
    container = container_get(qdev_get_machine(), "/peripheral");
    find_sysbus_device(container, &find);
    container = container_get(qdev_get_machine(), "/peripheral-anon");
    find_sysbus_device(container, &find);
}


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

/* Check whether an IRQ source exists */
bool sysbus_has_irq(SysBusDevice *dev, int n)
{
    char *prop = g_strdup_printf("%s[%d]", SYSBUS_DEVICE_GPIO_IRQ, n);
    ObjectProperty *r;

    r = object_property_find(OBJECT(dev), prop);
    g_free(prop);

    return (r != NULL);
}

bool sysbus_is_irq_connected(SysBusDevice *dev, int n)
{
    return !!sysbus_get_connected_irq(dev, n);
}

qemu_irq sysbus_get_connected_irq(SysBusDevice *dev, int n)
{
    DeviceState *d = DEVICE(dev);
    return qdev_get_gpio_out_connector(d, SYSBUS_DEVICE_GPIO_IRQ, n);
}

void sysbus_connect_irq(SysBusDevice *dev, int n, qemu_irq irq)
{
    SysBusDeviceClass *sbd = SYS_BUS_DEVICE_GET_CLASS(dev);

    qdev_connect_gpio_out_named(DEVICE(dev), SYSBUS_DEVICE_GPIO_IRQ, n, irq);

    if (sbd->connect_irq_notifier) {
        sbd->connect_irq_notifier(dev, irq);
    }
}

/* Check whether an MMIO region exists */
bool sysbus_has_mmio(SysBusDevice *dev, unsigned int n)
{
    return (n < dev->num_mmio);
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

void sysbus_mmio_unmap(SysBusDevice *dev, int n)
{
    assert(n >= 0 && n < dev->num_mmio);

    if (dev->mmio[n].addr != (hwaddr)-1) {
        memory_region_del_subregion(get_system_memory(), dev->mmio[n].memory);
        dev->mmio[n].addr = (hwaddr)-1;
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
    qdev_init_gpio_out_named(DEVICE(dev), p, SYSBUS_DEVICE_GPIO_IRQ, 1);
}

/* Pass IRQs from a target device.  */
void sysbus_pass_irq(SysBusDevice *dev, SysBusDevice *target)
{
    qdev_pass_gpios(DEVICE(target), DEVICE(dev), SYSBUS_DEVICE_GPIO_IRQ);
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
    assert(n >= 0 && n < QDEV_MAX_MMIO);
    return dev->mmio[n].memory;
}

void sysbus_init_ioports(SysBusDevice *dev, uint32_t ioport, uint32_t size)
{
    uint32_t i;

    for (i = 0; i < size; i++) {
        assert(dev->num_pio < QDEV_MAX_PIO);
        dev->pio[dev->num_pio++] = ioport++;
    }
}

/* The purpose of preserving this empty realize function
 * is to prevent the parent_realize field of some subclasses
 * from being set to NULL to break the normal init/realize
 * of some devices.
 */
static void sysbus_device_realize(DeviceState *dev, Error **errp)
{
}

DeviceState *sysbus_create_varargs(const char *name,
                                   hwaddr addr, ...)
{
    DeviceState *dev;
    SysBusDevice *s;
    va_list va;
    qemu_irq irq;
    int n;

    dev = qdev_new(name);
    s = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(s, &error_fatal);
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

bool sysbus_realize(SysBusDevice *dev, Error **errp)
{
    return qdev_realize(DEVICE(dev), sysbus_get_default(), errp);
}

bool sysbus_realize_and_unref(SysBusDevice *dev, Error **errp)
{
    return qdev_realize_and_unref(DEVICE(dev), sysbus_get_default(), errp);
}

static void sysbus_dev_print(Monitor *mon, DeviceState *dev, int indent)
{
    SysBusDevice *s = SYS_BUS_DEVICE(dev);
    hwaddr size;
    int i;

    for (i = 0; i < s->num_mmio; i++) {
        size = memory_region_size(s->mmio[i].memory);
        monitor_printf(mon, "%*smmio " TARGET_FMT_plx "/" TARGET_FMT_plx "\n",
                       indent, "", s->mmio[i].addr, size);
    }
}

static char *sysbus_get_fw_dev_path(DeviceState *dev)
{
    SysBusDevice *s = SYS_BUS_DEVICE(dev);
    SysBusDeviceClass *sbc = SYS_BUS_DEVICE_GET_CLASS(s);
    char *addr, *fw_dev_path;

    if (sbc->explicit_ofw_unit_address) {
        addr = sbc->explicit_ofw_unit_address(s);
        if (addr) {
            fw_dev_path = g_strdup_printf("%s@%s", qdev_fw_name(dev), addr);
            g_free(addr);
            return fw_dev_path;
        }
    }
    if (s->num_mmio) {
        return g_strdup_printf("%s@" TARGET_FMT_plx, qdev_fw_name(dev),
                               s->mmio[0].addr);
    }
    if (s->num_pio) {
        return g_strdup_printf("%s@i%04x", qdev_fw_name(dev), s->pio[0]);
    }
    return g_strdup(qdev_fw_name(dev));
}

void sysbus_add_io(SysBusDevice *dev, hwaddr addr,
                       MemoryRegion *mem)
{
    memory_region_add_subregion(get_system_io(), addr, mem);
}

MemoryRegion *sysbus_address_space(SysBusDevice *dev)
{
    return get_system_memory();
}

static void sysbus_device_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *k = DEVICE_CLASS(klass);
    k->realize = sysbus_device_realize;
    k->bus_type = TYPE_SYSTEM_BUS;
    /*
     * device_add plugs devices into a suitable bus.  For "real" buses,
     * that actually connects the device.  For sysbus, the connections
     * need to be made separately, and device_add can't do that.  The
     * device would be left unconnected, and will probably not work
     *
     * However, a few machines can handle device_add/-device with
     * a few specific sysbus devices. In those cases, the device
     * subclass needs to override it and set user_creatable=true.
     */
    k->user_creatable = false;
}

static const TypeInfo sysbus_device_type_info = {
    .name = TYPE_SYS_BUS_DEVICE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(SysBusDevice),
    .abstract = true,
    .class_size = sizeof(SysBusDeviceClass),
    .class_init = sysbus_device_class_init,
};

static BusState *main_system_bus;

static void main_system_bus_create(void)
{
    /*
     * assign main_system_bus before qbus_init()
     * in order to make "if (bus != sysbus_get_default())" work
     */
    main_system_bus = g_malloc0(system_bus_info.instance_size);
    qbus_init(main_system_bus, system_bus_info.instance_size,
              TYPE_SYSTEM_BUS, NULL, "main-system-bus");
    OBJECT(main_system_bus)->free = g_free;
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
