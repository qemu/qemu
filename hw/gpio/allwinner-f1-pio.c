/*
 * Allwinner F1 PIO Unit emulation
 *
 * Copyright (C) 2022 froloff
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "migration/vmstate.h"
#include "hw/sysbus.h"
#include "hw/gpio/allwinner-f1-pio.h"

#define PIO_INT_CFG          0x0200
enum {
    REG_PIO_INT_CFG0         = 0x0000,
    REG_PIO_INT_CFG1         = 0x0004,
    REG_PIO_INT_CFG2         = 0x0008,
    REG_PIO_INT_CFG3         = 0x000c,
    REG_PIO_INT_CTRL         = 0x0010,
    REG_PIO_INT_STA          = 0x0014,
    REG_PIO_INT_DEB          = 0x0018,
};

enum {
    REG_PIO_SDR_PAD_DRV      = 0x02c0,
    REG_PIO_SDR_PAD_PUL      = 0x02c4,
};

#define REG_INDEX(offset)    ((offset) / sizeof(uint32_t))

static uint64_t allwinner_f1_pio_read(void *opaque, hwaddr offset,
                                      unsigned size)
{
    const AwPIOState *s = AW_F1_PIO(opaque);
    const uint32_t *regs = 0;
    const uint32_t idx = REG_INDEX(offset);
    fn_pio_read fn = 0;
    uint32_t port, ofs;

    switch (offset) {
    case REG_PIO_CFG0 ... AW_F1_PORTS*0x24 + REG_PIO_CFG0:
        ofs  = offset % 0x24;
        port = offset / 0x24;
        regs = &s->regs[REG_INDEX(port * 0x24)];
        fn = s->cb[port].fn_rd;
        break;
    case PIO_INT_CFG ... PIO_INT_CFG + AW_F1_PORTS_IRQ*0x20 + REG_PIO_INT_DEB:
        ofs  = (offset - PIO_INT_CFG) % 0x20;
        port = (offset - PIO_INT_CFG) / 0x20;
        regs = &s->regs[REG_INDEX(PIO_INT_CFG + port * 0x20)];
        port += PIO_D; // Only Ports D,E.F
        // TODO
        break;
    case REG_PIO_SDR_PAD_PUL + 4 ... AW_PIO_IOSIZE:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        return 0;
    }
    if (fn) fn(s->cb[port].opaque, regs, ofs);
    return s->regs[idx];
}

static void allwinner_f1_pio_write(void *opaque, hwaddr offset,
                                   uint64_t val, unsigned size)
{
    AwPIOState *s = AW_F1_PIO(opaque);
    uint32_t *regs = 0;
    const uint32_t idx = REG_INDEX(offset);
    fn_pio_write fn = 0;
    uint32_t port, ofs;

    switch (offset) {
    case REG_PIO_CFG0 ... AW_F1_PORTS*0x24 + REG_PIO_CFG0:
        ofs  = offset % 0x24;
        port = offset / 0x24;
        regs = &s->regs[REG_INDEX(port * 0x24)];
        fn = s->cb[port].fn_wr;
        break;
    case PIO_INT_CFG ... PIO_INT_CFG + AW_F1_PORTS_IRQ*0x20 + REG_PIO_INT_DEB:
        ofs  = (offset - PIO_INT_CFG) % 0x20;
        port = (offset - PIO_INT_CFG) / 0x20;
        regs = &s->regs[REG_INDEX(PIO_INT_CFG + port * 0x20)];
        port += PIO_D; // Only Ports D,E.F
        // TODO
        break;
    case REG_PIO_SDR_PAD_PUL + 4 ... AW_PIO_IOSIZE:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        return;
    }
    if (fn) val = fn(s->cb[port].opaque, regs, ofs, (uint32_t)val);
    s->regs[idx] = (uint32_t) val;
}

static const MemoryRegionOps allwinner_f1_pio_ops = {
    .read = allwinner_f1_pio_read,
    .write = allwinner_f1_pio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl.min_access_size = 4,
};

static void allwinner_f1_pio_reset(DeviceState *dev)
{
    AwPIOState *s = AW_F1_PIO(dev);

    /* TODO: Set default values for registers */
}

static void allwinner_f1_pio_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    AwPIOState *s = AW_F1_PIO(obj);

    /* Memory mapping */
    memory_region_init_io(&s->iomem, OBJECT(s), &allwinner_f1_pio_ops, s,
                          TYPE_AW_F1_PIO, AW_PIO_IOSIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    for (int port = 0; port < AW_F1_PORTS; port++) {
        s->cb[port].opaque = 0;
        s->cb[port].fn_rd = 0;
        s->cb[port].fn_wr = 0;
    }
}

void allwinner_set_pio_port_cb(AwPIOState *s, uint32_t port,
                               void *opaque,
                               fn_pio_read  fn_rd,
                               fn_pio_write fn_wr)
{
    if (port < AW_F1_PORTS) {
        s->cb[port].opaque = opaque;
        s->cb[port].fn_rd  = fn_rd;
        s->cb[port].fn_wr  = fn_wr;
    }
}

static const VMStateDescription allwinner_f1_pio_vmstate = {
    .name = "allwinner-f1-pio",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        // TODO?: VMSTATE_STRUCT(cb.opaque) 
        VMSTATE_UINT32_ARRAY(regs, AwPIOState, AW_PIO_REGS_NUM),
        VMSTATE_END_OF_LIST()
    }
};

static void allwinner_f1_pio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = allwinner_f1_pio_reset;
    dc->vmsd = &allwinner_f1_pio_vmstate;
}

static const TypeInfo allwinner_f1_pio_info = {
    .name          = TYPE_AW_F1_PIO,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_init = allwinner_f1_pio_init,
    .instance_size = sizeof(AwPIOState),
    .class_init    = allwinner_f1_pio_class_init,
};

static void allwinner_f1_pio_register(void)
{
    type_register_static(&allwinner_f1_pio_info);
}

type_init(allwinner_f1_pio_register)
