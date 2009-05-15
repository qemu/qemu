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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA  02110-1301 USA
 */

#include "sysbus.h"
#include "sysemu.h"

void sysbus_connect_irq(SysBusDevice *dev, int n, qemu_irq irq)
{
    assert(n >= 0 && n < dev->num_irq);
    dev->irqs[n] = 0;
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
        cpu_register_physical_memory(dev->mmio[n].addr, dev->mmio[n].size,
                                     IO_MEM_UNASSIGNED);
    }
    dev->mmio[n].addr = addr;
    if (dev->mmio[n].cb) {
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

void sysbus_init_mmio(SysBusDevice *dev, target_phys_addr_t size, int iofunc)
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

static void sysbus_device_init(DeviceState *dev, void *opaque)
{
    sysbus_initfn init = (sysbus_initfn)opaque;

    init(sysbus_from_qdev(dev));
}

void sysbus_register_dev(const char *name, size_t size, sysbus_initfn init)
{
    assert(size >= sizeof(SysBusDevice));
    qdev_register(name, size, sysbus_device_init, init);
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
    qdev_init(dev);
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
