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
        if (dev->mmio[n].memory) {
            memory_region_del_subregion(get_system_memory(),
                                        dev->mmio[n].memory);
        } else {
            cpu_register_physical_memory(dev->mmio[n].addr, dev->mmio[n].size,
                                         IO_MEM_UNASSIGNED);
        }
    }
    dev->mmio[n].addr = addr;
    if (dev->mmio[n].memory) {
        memory_region_add_subregion(get_system_memory(),
                                    addr,
                                    dev->mmio[n].memory);
    } else if (dev->mmio[n].cb) {
        dev->mmio[n].cb(dev, addr);
    } else {
        cpu_register_physical_memory(addr, dev->mmio[n].size,
                                     dev->mmio[n].iofunc);
    }
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

void sysbus_init_mmio(SysBusDevice *dev, target_phys_addr_t size,
                      ram_addr_t iofunc)
{
    int n;

    assert(dev->num_mmio < QDEV_MAX_MMIO);
    n = dev->num_mmio++;
    dev->mmio[n].addr = -1;
    dev->mmio[n].size = size;
    dev->mmio[n].iofunc = iofunc;
}

void sysbus_init_mmio_cb(SysBusDevice *dev, target_phys_addr_t size,
                         mmio_mapfunc cb)
{
    int n;

    assert(dev->num_mmio < QDEV_MAX_MMIO);
    n = dev->num_mmio++;
    dev->mmio[n].addr = -1;
    dev->mmio[n].size = size;
    dev->mmio[n].cb = cb;
}

void sysbus_init_mmio_region(SysBusDevice *dev, MemoryRegion *memory)
{
    int n;

    assert(dev->num_mmio < QDEV_MAX_MMIO);
    n = dev->num_mmio++;
    dev->mmio[n].addr = -1;
    dev->mmio[n].size = memory_region_size(memory);
    dev->mmio[n].memory = memory;
}

void sysbus_init_ioports(SysBusDevice *dev, pio_addr_t ioport, pio_addr_t size)
{
    pio_addr_t i;

    for (i = 0; i < size; i++) {
        assert(dev->num_pio < QDEV_MAX_PIO);
        dev->pio[dev->num_pio++] = ioport++;
    }
}

static int sysbus_device_init(DeviceState *dev, DeviceInfo *base)
{
    SysBusDeviceInfo *info = container_of(base, SysBusDeviceInfo, qdev);

    return info->init(sysbus_from_qdev(dev));
}

void sysbus_register_withprop(SysBusDeviceInfo *info)
{
    info->qdev.init = sysbus_device_init;
    info->qdev.bus_info = &system_bus_info;

    assert(info->qdev.size >= sizeof(SysBusDevice));
    qdev_register(&info->qdev);
}

void sysbus_register_dev(const char *name, size_t size, sysbus_initfn init)
{
    SysBusDeviceInfo *info;

    info = qemu_mallocz(sizeof(*info));
    info->qdev.name = qemu_strdup(name);
    info->qdev.size = size;
    info->init = init;
    sysbus_register_withprop(info);
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
    return dev;
}

static void sysbus_dev_print(Monitor *mon, DeviceState *dev, int indent)
{
    SysBusDevice *s = sysbus_from_qdev(dev);
    int i;

    monitor_printf(mon, "%*sirq %d\n", indent, "", s->num_irq);
    for (i = 0; i < s->num_mmio; i++) {
        monitor_printf(mon, "%*smmio " TARGET_FMT_plx "/" TARGET_FMT_plx "\n",
                       indent, "", s->mmio[i].addr, s->mmio[i].size);
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
