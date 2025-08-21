/*
 * MAX78000 UART
 *
 * Copyright (c) 2025 Jackson Donaldson <jcksn@duck.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/char/max78000_uart.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "migration/vmstate.h"
#include "trace.h"


static int max78000_uart_can_receive(void *opaque)
{
    Max78000UartState *s = opaque;
    if (!(s->ctrl & UART_BCLKEN)) {
        return 0;
    }
    return fifo8_num_free(&s->rx_fifo);
}

static void max78000_update_irq(Max78000UartState *s)
{
    int interrupt_level;

    interrupt_level = s->int_fl & s->int_en;
    qemu_set_irq(s->irq, interrupt_level);
}

static void max78000_uart_receive(void *opaque, const uint8_t *buf, int size)
{
    Max78000UartState *s = opaque;

    assert(size <= fifo8_num_free(&s->rx_fifo));

    fifo8_push_all(&s->rx_fifo, buf, size);

    uint32_t rx_threshold = s->ctrl & 0xf;

    if (fifo8_num_used(&s->rx_fifo) >= rx_threshold) {
        s->int_fl |= UART_RX_THD;
    }

    max78000_update_irq(s);
}

static void max78000_uart_reset_hold(Object *obj, ResetType type)
{
    Max78000UartState *s = MAX78000_UART(obj);

    s->ctrl = 0;
    s->status = UART_TX_EM | UART_RX_EM;
    s->int_en = 0;
    s->int_fl = 0;
    s->osr = 0;
    s->txpeek = 0;
    s->pnr = UART_RTS;
    s->fifo = 0;
    s->dma = 0;
    s->wken = 0;
    s->wkfl = 0;
    fifo8_reset(&s->rx_fifo);
}

static uint64_t max78000_uart_read(void *opaque, hwaddr addr,
                                       unsigned int size)
{
    Max78000UartState *s = opaque;
    uint64_t retvalue = 0;
    switch (addr) {
    case UART_CTRL:
        retvalue = s->ctrl;
        break;
    case UART_STATUS:
        retvalue = (fifo8_num_used(&s->rx_fifo) << UART_RX_LVL) |
                    UART_TX_EM |
                    (fifo8_is_empty(&s->rx_fifo) ? UART_RX_EM : 0);
        break;
    case UART_INT_EN:
        retvalue = s->int_en;
        break;
    case UART_INT_FL:
        retvalue = s->int_fl;
        break;
    case UART_CLKDIV:
        retvalue = s->clkdiv;
        break;
    case UART_OSR:
        retvalue = s->osr;
        break;
    case UART_TXPEEK:
        if (!fifo8_is_empty(&s->rx_fifo)) {
            retvalue = fifo8_peek(&s->rx_fifo);
        }
        break;
    case UART_PNR:
        retvalue = s->pnr;
        break;
    case UART_FIFO:
        if (!fifo8_is_empty(&s->rx_fifo)) {
            retvalue = fifo8_pop(&s->rx_fifo);
            max78000_update_irq(s);
        }
        break;
    case UART_DMA:
        /* DMA not implemented */
        retvalue = s->dma;
        break;
    case UART_WKEN:
        retvalue = s->wken;
        break;
    case UART_WKFL:
        retvalue = s->wkfl;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
            "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, addr);
        break;
    }

    return retvalue;
}

static void max78000_uart_write(void *opaque, hwaddr addr,
                                  uint64_t val64, unsigned int size)
{
    Max78000UartState *s = opaque;

    uint32_t value = val64;
    uint8_t data;

    switch (addr) {
    case UART_CTRL:
        if (value & UART_FLUSH_RX) {
            fifo8_reset(&s->rx_fifo);
        }
        if (value & UART_BCLKEN) {
            value = value | UART_BCLKRDY;
        }
        s->ctrl = value & ~(UART_FLUSH_RX | UART_FLUSH_TX);

        /*
         * Software can manage UART flow control manually by setting hfc_en
         * in UART_CTRL. This would require emulating uart at a lower level,
         * and is currently unimplemented.
         */

        return;
    case UART_STATUS:
        /* UART_STATUS is read only */
        return;
    case UART_INT_EN:
        s->int_en = value;
        return;
    case UART_INT_FL:
        s->int_fl = s->int_fl & ~(value);
        max78000_update_irq(s);
        return;
    case UART_CLKDIV:
        s->clkdiv = value;
        return;
    case UART_OSR:
        s->osr = value;
        return;
    case UART_PNR:
        s->pnr = value;
        return;
    case UART_FIFO:
        data = value & 0xff;
        /*
         * XXX this blocks entire thread. Rewrite to use
         * qemu_chr_fe_write and background I/O callbacks
         */
        qemu_chr_fe_write_all(&s->chr, &data, 1);

        /* TX is always empty */
        s->int_fl |= UART_TX_HE;
        max78000_update_irq(s);

        return;
    case UART_DMA:
        /* DMA not implemented */
        s->dma = value;
        return;
    case UART_WKEN:
        s->wken = value;
        return;
    case UART_WKFL:
        s->wkfl = value;
        return;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%"
            HWADDR_PRIx "\n", __func__, addr);
    }
}

static const MemoryRegionOps max78000_uart_ops = {
    .read = max78000_uart_read,
    .write = max78000_uart_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static const Property max78000_uart_properties[] = {
    DEFINE_PROP_CHR("chardev", Max78000UartState, chr),
};

static const VMStateDescription max78000_uart_vmstate = {
    .name = TYPE_MAX78000_UART,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(ctrl, Max78000UartState),
        VMSTATE_UINT32(status, Max78000UartState),
        VMSTATE_UINT32(int_en, Max78000UartState),
        VMSTATE_UINT32(int_fl, Max78000UartState),
        VMSTATE_UINT32(clkdiv, Max78000UartState),
        VMSTATE_UINT32(osr, Max78000UartState),
        VMSTATE_UINT32(txpeek, Max78000UartState),
        VMSTATE_UINT32(pnr, Max78000UartState),
        VMSTATE_UINT32(fifo, Max78000UartState),
        VMSTATE_UINT32(dma, Max78000UartState),
        VMSTATE_UINT32(wken, Max78000UartState),
        VMSTATE_UINT32(wkfl, Max78000UartState),
        VMSTATE_FIFO8(rx_fifo, Max78000UartState),
        VMSTATE_END_OF_LIST()
    }
};

static void max78000_uart_init(Object *obj)
{
    Max78000UartState *s = MAX78000_UART(obj);
    fifo8_create(&s->rx_fifo, 8);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);

    memory_region_init_io(&s->mmio, obj, &max78000_uart_ops, s,
                          TYPE_MAX78000_UART, 0x400);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static void max78000_uart_finalize(Object *obj)
{
    Max78000UartState *s = MAX78000_UART(obj);
    fifo8_destroy(&s->rx_fifo);
}

static void max78000_uart_realize(DeviceState *dev, Error **errp)
{
    Max78000UartState *s = MAX78000_UART(dev);

    qemu_chr_fe_set_handlers(&s->chr, max78000_uart_can_receive,
                             max78000_uart_receive, NULL, NULL,
                             s, NULL, true);
}

static void max78000_uart_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = max78000_uart_reset_hold;

    device_class_set_props(dc, max78000_uart_properties);
    dc->realize = max78000_uart_realize;

    dc->vmsd = &max78000_uart_vmstate;
}

static const TypeInfo max78000_uart_info = {
    .name          = TYPE_MAX78000_UART,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Max78000UartState),
    .instance_init = max78000_uart_init,
    .instance_finalize = max78000_uart_finalize,
    .class_init    = max78000_uart_class_init,
};

static void max78000_uart_register_types(void)
{
    type_register_static(&max78000_uart_info);
}

type_init(max78000_uart_register_types)
