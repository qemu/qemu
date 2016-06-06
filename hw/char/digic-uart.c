/*
 * QEMU model of the Canon DIGIC UART block.
 *
 * Copyright (C) 2013 Antony Pavlov <antonynpavlov@gmail.com>
 *
 * This model is based on reverse engineering efforts
 * made by CHDK (http://chdk.wikia.com) and
 * Magic Lantern (http://www.magiclantern.fm) projects
 * contributors.
 *
 * See "Serial terminal" docs here:
 *   http://magiclantern.wikia.com/wiki/Register_Map#Misc_Registers
 *
 * The QEMU model of the Milkymist UART block by Michael Walle
 * is used as a template.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */

#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "sysemu/char.h"
#include "qemu/log.h"

#include "hw/char/digic-uart.h"

enum {
    ST_RX_RDY = (1 << 0),
    ST_TX_RDY = (1 << 1),
};

static uint64_t digic_uart_read(void *opaque, hwaddr addr,
                                unsigned size)
{
    DigicUartState *s = opaque;
    uint64_t ret = 0;

    addr >>= 2;

    switch (addr) {
    case R_RX:
        s->reg_st &= ~(ST_RX_RDY);
        ret = s->reg_rx;
        break;

    case R_ST:
        ret = s->reg_st;
        break;

    default:
        qemu_log_mask(LOG_UNIMP,
                      "digic-uart: read access to unknown register 0x"
                      TARGET_FMT_plx, addr << 2);
    }

    return ret;
}

static void digic_uart_write(void *opaque, hwaddr addr, uint64_t value,
                             unsigned size)
{
    DigicUartState *s = opaque;
    unsigned char ch = value;

    addr >>= 2;

    switch (addr) {
    case R_TX:
        if (s->chr) {
            qemu_chr_fe_write_all(s->chr, &ch, 1);
        }
        break;

    case R_ST:
        /*
         * Ignore write to R_ST.
         *
         * The point is that this register is actively used
         * during receiving and transmitting symbols,
         * but we don't know the function of most of bits.
         *
         * Ignoring writes to R_ST is only a simplification
         * of the model. It has no perceptible side effects
         * for existing guests.
         */
        break;

    default:
        qemu_log_mask(LOG_UNIMP,
                      "digic-uart: write access to unknown register 0x"
                      TARGET_FMT_plx, addr << 2);
    }
}

static const MemoryRegionOps uart_mmio_ops = {
    .read = digic_uart_read,
    .write = digic_uart_write,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static int uart_can_rx(void *opaque)
{
    DigicUartState *s = opaque;

    return !(s->reg_st & ST_RX_RDY);
}

static void uart_rx(void *opaque, const uint8_t *buf, int size)
{
    DigicUartState *s = opaque;

    assert(uart_can_rx(opaque));

    s->reg_st |= ST_RX_RDY;
    s->reg_rx = *buf;
}

static void uart_event(void *opaque, int event)
{
}

static void digic_uart_reset(DeviceState *d)
{
    DigicUartState *s = DIGIC_UART(d);

    s->reg_rx = 0;
    s->reg_st = ST_TX_RDY;
}

static void digic_uart_realize(DeviceState *dev, Error **errp)
{
    DigicUartState *s = DIGIC_UART(dev);

    if (s->chr) {
        qemu_chr_add_handlers(s->chr, uart_can_rx, uart_rx, uart_event, s);
    }
}

static void digic_uart_init(Object *obj)
{
    DigicUartState *s = DIGIC_UART(obj);

    memory_region_init_io(&s->regs_region, OBJECT(s), &uart_mmio_ops, s,
                          TYPE_DIGIC_UART, 0x18);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs_region);
}

static const VMStateDescription vmstate_digic_uart = {
    .name = "digic-uart",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(reg_rx, DigicUartState),
        VMSTATE_UINT32(reg_st, DigicUartState),
        VMSTATE_END_OF_LIST()
    }
};

static Property digic_uart_properties[] = {
    DEFINE_PROP_CHR("chardev", DigicUartState, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void digic_uart_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = digic_uart_realize;
    dc->reset = digic_uart_reset;
    dc->vmsd = &vmstate_digic_uart;
    dc->props = digic_uart_properties;
}

static const TypeInfo digic_uart_info = {
    .name = TYPE_DIGIC_UART,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(DigicUartState),
    .instance_init = digic_uart_init,
    .class_init = digic_uart_class_init,
};

static void digic_uart_register_types(void)
{
    type_register_static(&digic_uart_info);
}

type_init(digic_uart_register_types)
