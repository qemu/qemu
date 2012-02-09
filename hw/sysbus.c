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

#include "sysbus.h"
#include "monitor.h"
#include "exec-memory.h"

static void sysbus_dev_print(Monitor *mon, DeviceState *dev, int indent);
static char *sysbus_get_fw_dev_path(DeviceState *dev);

struct BusInfo system_bus_info = {
    .name       = "System",
    .size       = sizeof(BusState),
    .print_dev  = sysbus_dev_print,
    .get_fw_dev_path = sysbus_get_fw_dev_path,
};

void sysbus_connect_irq(SysBusDevice *dev, int n, qemu_irq irq)
{
    assert(n >= 0 && n < dev->num_irq);
    dev->irqs[n] = NULL;
    if (dev->irqp[n]) {
        *dev->irqp[n] = irq;
    }
}

void sysbus_mmio_map(SysBusDevice *dev, int n, target_phys_addr_t addr)
{
    assert(n >= 0 && n < dev->num_mmio);

    if (dev->mmio[n].addr == addr) {
        /* ??? region already mapped here.  */
        return;
    }
    if (dev->mmio[n].addr != (target_phys_addr_t)-1) {
        /* Unregister previous mapping.  */
        memory_region_del_subregion(get_system_memory(), dev->mmio[n].memory);
    }
    dev->mmio[n].addr = addr;
    memory_region_add_subregion(get_system_memory(),
                                addr,
                                dev->mmio[n].memory);
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

    return sbc->init(sd);
}

DeviceState *sysbus_create_varargs(const char *name,
                                   target_phys_addr_t addr, ...)
{
    DeviceState *dev;
    SysBusDevice *s;
    va_list va;
    qemu_irq irq;
    int n;

    dev = qdev_create(NULL, name);
    s = sysbus_from_qdev(dev);
    qdev_init_nofail(dev);
    if (addr != (target_phys_addr_t)-1) {
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
                                       target_phys_addr_t addr, ...)
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
    s = sysbus_from_qdev(dev);
    qdev_init_nofail(dev);
    if (addr != (target_phys_addr_t)-1) {
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
    SysBusDevice *s = sysbus_from_qdev(dev);
    target_phys_addr_t size;
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
    SysBusDevice *s = sysbus_from_qdev(dev);
    char path[40];
    int off;

    off = snprintf(path, sizeof(path), "%s", qdev_fw_name(dev));

    if (s->num_mmio) {
        snprintf(path + off, sizeof(path) - off, "@"TARGET_FMT_plx,
                 s->mmio[0].addr);
    } else if (s->num_pio) {
        snprintf(path + off, sizeof(path) - off, "@i%04x", s->pio[0]);
    }

    return strdup(path);
}

void sysbus_add_memory(SysBusDevice *dev, target_phys_addr_t addr,
                       MemoryRegion *mem)
{
    memory_region_add_subregion(get_system_memory(), addr, mem);
}

void sysbus_add_memory_overlap(SysBusDevice *dev, target_phys_addr_t addr,
                               MemoryRegion *mem, unsigned priority)
{
    memory_region_add_subregion_overlap(get_system_memory(), addr, mem,
                                        priority);
}

void sysbus_del_memory(SysBusDevice *dev, MemoryRegion *mem)
{
    memory_region_del_subregion(get_system_memory(), mem);
}

void sysbus_add_io(SysBusDevice *dev, target_phys_addr_t addr,
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
    k->bus_info = &system_bus_info;
}

static TypeInfo sysbus_device_type_info = {
    .name = TYPE_SYS_BUS_DEVICE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(SysBusDevice),
    .abstract = true,
    .class_size = sizeof(SysBusDeviceClass),
    .class_init = sysbus_device_class_init,
};

static void sysbus_register_types(void)
{
    type_register_static(&sysbus_device_type_info);
}

type_init(sysbus_register_types)
