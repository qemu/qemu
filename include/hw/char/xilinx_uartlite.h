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

#ifndef XILINX_UARTLITE_H
#define XILINX_UARTLITE_H

#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "qapi/error.h"

static inline DeviceState *xilinx_uartlite_create(hwaddr addr,
                                        qemu_irq irq,
                                        Chardev *chr)
{
    DeviceState *dev;
    SysBusDevice *s;

    dev = qdev_new("xlnx.xps-uartlite");
    s = SYS_BUS_DEVICE(dev);
    qdev_prop_set_chr(dev, "chardev", chr);
    sysbus_realize_and_unref(s, &error_fatal);
    sysbus_mmio_map(s, 0, addr);
    sysbus_connect_irq(s, 0, irq);

    return dev;
}

#endif
