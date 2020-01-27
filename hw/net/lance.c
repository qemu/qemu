/*
 * QEMU AMD PC-Net II (Am79C970A) emulation
 *
 * Copyright (c) 2004 Antony T Curtis
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

/* This software was written to be compatible with the specification:
 * AMD Am79C970A PCnet-PCI II Ethernet Controller Data-Sheet
 * AMD Publication# 19436  Rev:E  Amendment/0  Issue Date: June 2000
 */

/*
 * On Sparc32, this is the Lance (Am7990) part of chip STP2000 (Master I/O), also
 * produced as NCR89C100. See
 * http://www.ibiblio.org/pub/historic-linux/early-ports/Sparc/NCR/NCR89C100.txt
 * and
 * http://www.ibiblio.org/pub/historic-linux/early-ports/Sparc/NCR/NCR92C990.txt
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "hw/sparc/sparc32_dma.h"
#include "migration/vmstate.h"
#include "hw/net/lance.h"
#include "hw/qdev-properties.h"
#include "trace.h"
#include "sysemu/sysemu.h"


static void parent_lance_reset(void *opaque, int irq, int level)
{
    SysBusPCNetState *d = opaque;
    if (level)
        pcnet_h_reset(&d->state);
}

static void lance_mem_write(void *opaque, hwaddr addr,
                            uint64_t val, unsigned size)
{
    SysBusPCNetState *d = opaque;

    trace_lance_mem_writew(addr, val & 0xffff);
    pcnet_ioport_writew(&d->state, addr, val & 0xffff);
}

static uint64_t lance_mem_read(void *opaque, hwaddr addr,
                               unsigned size)
{
    SysBusPCNetState *d = opaque;
    uint32_t val;

    val = pcnet_ioport_readw(&d->state, addr);
    trace_lance_mem_readw(addr, val & 0xffff);
    return val & 0xffff;
}

static const MemoryRegionOps lance_mem_ops = {
    .read = lance_mem_read,
    .write = lance_mem_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 2,
        .max_access_size = 2,
    },
};

static NetClientInfo net_lance_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .receive = pcnet_receive,
    .link_status_changed = pcnet_set_link_status,
};

static const VMStateDescription vmstate_lance = {
    .name = "pcnet",
    .version_id = 3,
    .minimum_version_id = 2,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT(state, SysBusPCNetState, 0, vmstate_pcnet, PCNetState),
        VMSTATE_END_OF_LIST()
    }
};

static void lance_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    SysBusPCNetState *d = SYSBUS_PCNET(dev);
    PCNetState *s = &d->state;

    memory_region_init_io(&s->mmio, OBJECT(d), &lance_mem_ops, d,
                          "lance-mmio", 4);

    qdev_init_gpio_in(dev, parent_lance_reset, 1);

    sysbus_init_mmio(sbd, &s->mmio);

    sysbus_init_irq(sbd, &s->irq);

    s->phys_mem_read = ledma_memory_read;
    s->phys_mem_write = ledma_memory_write;
    pcnet_common_init(dev, s, &net_lance_info);
}

static void lance_reset(DeviceState *dev)
{
    SysBusPCNetState *d = SYSBUS_PCNET(dev);

    pcnet_h_reset(&d->state);
}

static void lance_instance_init(Object *obj)
{
    SysBusPCNetState *d = SYSBUS_PCNET(obj);
    PCNetState *s = &d->state;

    device_add_bootindex_property(obj, &s->conf.bootindex,
                                  "bootindex", "/ethernet-phy@0",
                                  DEVICE(obj), NULL);
}

static Property lance_properties[] = {
    DEFINE_PROP_LINK("dma", SysBusPCNetState, state.dma_opaque,
                     TYPE_DEVICE, DeviceState *),
    DEFINE_NIC_PROPERTIES(SysBusPCNetState, state.conf),
    DEFINE_PROP_END_OF_LIST(),
};

static void lance_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = lance_realize;
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
    dc->fw_name = "ethernet";
    dc->reset = lance_reset;
    dc->vmsd = &vmstate_lance;
    device_class_set_props(dc, lance_properties);
}

static const TypeInfo lance_info = {
    .name          = TYPE_LANCE,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SysBusPCNetState),
    .class_init    = lance_class_init,
    .instance_init = lance_instance_init,
};

static void lance_register_types(void)
{
    type_register_static(&lance_info);
}

type_init(lance_register_types)
