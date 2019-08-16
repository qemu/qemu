/*
 * QEMU ETRAX System Emulator
 *
 * Copyright (c) 2008 Edgar E. Iglesias, Axis Communications AB.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef HW_ETRAXFS_H
#define HW_ETRAXFS_H

#include "net/net.h"
#include "hw/cris/etraxfs_dma.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"

/* Instantiate an ETRAXFS Ethernet MAC.  */
static inline DeviceState *
etraxfs_eth_init(NICInfo *nd, hwaddr base, int phyaddr,
                 void *dma_out, void *dma_in)
{
    DeviceState *dev;
    qemu_check_nic_model(nd, "fseth");

    dev = qdev_create(NULL, "etraxfs-eth");
    qdev_set_nic_properties(dev, nd);
    qdev_prop_set_uint32(dev, "phyaddr", phyaddr);
    qdev_prop_set_ptr(dev, "dma_out", dma_out);
    qdev_prop_set_ptr(dev, "dma_in", dma_in);
    qdev_init_nofail(dev);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, base);
    return dev;
}

static inline DeviceState *etraxfs_ser_create(hwaddr addr,
                                              qemu_irq irq,
                                              Chardev *chr)
{
    DeviceState *dev;
    SysBusDevice *s;

    dev = qdev_create(NULL, "etraxfs,serial");
    s = SYS_BUS_DEVICE(dev);
    qdev_prop_set_chr(dev, "chardev", chr);
    qdev_init_nofail(dev);
    sysbus_mmio_map(s, 0, addr);
    sysbus_connect_irq(s, 0, irq);
    return dev;
}

#endif
