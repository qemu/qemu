/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_PL011_H
#define HW_PL011_H

#include "hw/sysbus.h"
#include "chardev/char-fe.h"

#define TYPE_PL011 "pl011"
#define PL011(obj) OBJECT_CHECK(PL011State, (obj), TYPE_PL011)

/* This shares the same struct (and cast macro) as the base pl011 device */
#define TYPE_PL011_LUMINARY "pl011_luminary"

typedef struct PL011State {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t readbuff;
    uint32_t flags;
    uint32_t lcr;
    uint32_t rsr;
    uint32_t cr;
    uint32_t dmacr;
    uint32_t int_enabled;
    uint32_t int_level;
    uint32_t read_fifo[16];
    uint32_t ilpr;
    uint32_t ibrd;
    uint32_t fbrd;
    uint32_t ifl;
    int read_pos;
    int read_count;
    int read_trigger;
    CharBackend chr;
    qemu_irq irq[6];
    const unsigned char *id;
} PL011State;

static inline DeviceState *pl011_create(hwaddr addr,
                                        qemu_irq irq,
                                        Chardev *chr)
{
    DeviceState *dev;
    SysBusDevice *s;

    dev = qdev_create(NULL, "pl011");
    s = SYS_BUS_DEVICE(dev);
    qdev_prop_set_chr(dev, "chardev", chr);
    qdev_init_nofail(dev);
    sysbus_mmio_map(s, 0, addr);
    sysbus_connect_irq(s, 0, irq);

    return dev;
}

static inline DeviceState *pl011_luminary_create(hwaddr addr,
                                                 qemu_irq irq,
                                                 Chardev *chr)
{
    DeviceState *dev;
    SysBusDevice *s;

    dev = qdev_create(NULL, "pl011_luminary");
    s = SYS_BUS_DEVICE(dev);
    qdev_prop_set_chr(dev, "chardev", chr);
    qdev_init_nofail(dev);
    sysbus_mmio_map(s, 0, addr);
    sysbus_connect_irq(s, 0, irq);

    return dev;
}

#endif
