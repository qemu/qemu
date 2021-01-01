/*
 * ARM CMSDK APB UART emulation
 *
 * Copyright (c) 2017 Linaro Limited
 * Written by Peter Maydell
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 or
 *  (at your option) any later version.
 */

/* This is a model of the "APB UART" which is part of the Cortex-M
 * System Design Kit (CMSDK) and documented in the Cortex-M System
 * Design Kit Technical Reference Manual (ARM DDI0479C):
 * https://developer.arm.com/products/system-design/system-design-kits/cortex-m-system-design-kit
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "trace.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "hw/registerfields.h"
#include "chardev/char-fe.h"
#include "chardev/char-serial.h"
#include "hw/char/cmsdk-apb-uart.h"
#include "hw/irq.h"
#include "hw/qdev-properties-system.h"

REG32(DATA, 0)
REG32(STATE, 4)
    FIELD(STATE, TXFULL, 0, 1)
    FIELD(STATE, RXFULL, 1, 1)
    FIELD(STATE, TXOVERRUN, 2, 1)
    FIELD(STATE, RXOVERRUN, 3, 1)
REG32(CTRL, 8)
    FIELD(CTRL, TX_EN, 0, 1)
    FIELD(CTRL, RX_EN, 1, 1)
    FIELD(CTRL, TX_INTEN, 2, 1)
    FIELD(CTRL, RX_INTEN, 3, 1)
    FIELD(CTRL, TXO_INTEN, 4, 1)
    FIELD(CTRL, RXO_INTEN, 5, 1)
    FIELD(CTRL, HSTEST, 6, 1)
REG32(INTSTATUS, 0xc)
    FIELD(INTSTATUS, TX, 0, 1)
    FIELD(INTSTATUS, RX, 1, 1)
    FIELD(INTSTATUS, TXO, 2, 1)
    FIELD(INTSTATUS, RXO, 3, 1)
REG32(BAUDDIV, 0x10)
REG32(PID4, 0xFD0)
REG32(PID5, 0xFD4)
REG32(PID6, 0xFD8)
REG32(PID7, 0xFDC)
REG32(PID0, 0xFE0)
REG32(PID1, 0xFE4)
REG32(PID2, 0xFE8)
REG32(PID3, 0xFEC)
REG32(CID0, 0xFF0)
REG32(CID1, 0xFF4)
REG32(CID2, 0xFF8)
REG32(CID3, 0xFFC)

/* PID/CID values */
static const int uart_id[] = {
    0x04, 0x00, 0x00, 0x00, /* PID4..PID7 */
    0x21, 0xb8, 0x1b, 0x00, /* PID0..PID3 */
    0x0d, 0xf0, 0x05, 0xb1, /* CID0..CID3 */
};

static bool uart_baudrate_ok(CMSDKAPBUART *s)
{
    /* The minimum permitted bauddiv setting is 16, so we just ignore
     * settings below that (usually this means the device has just
     * been reset and not yet programmed).
     */
    return s->bauddiv >= 16 && s->bauddiv <= s->pclk_frq;
}

static void uart_update_parameters(CMSDKAPBUART *s)
{
    QEMUSerialSetParams ssp;

    /* This UART is always 8N1 but the baud rate is programmable. */
    if (!uart_baudrate_ok(s)) {
        return;
    }

    ssp.data_bits = 8;
    ssp.parity = 'N';
    ssp.stop_bits = 1;
    ssp.speed = s->pclk_frq / s->bauddiv;
    qemu_chr_fe_ioctl(&s->chr, CHR_IOCTL_SERIAL_SET_PARAMS, &ssp);
    trace_cmsdk_apb_uart_set_params(ssp.speed);
}

static void cmsdk_apb_uart_update(CMSDKAPBUART *s)
{
    /* update outbound irqs, including handling the way the rxo and txo
     * interrupt status bits are just logical AND of the overrun bit in
     * STATE and the overrun interrupt enable bit in CTRL.
     */
    uint32_t omask = (R_INTSTATUS_RXO_MASK | R_INTSTATUS_TXO_MASK);
    s->intstatus &= ~omask;
    s->intstatus |= (s->state & (s->ctrl >> 2) & omask);

    qemu_set_irq(s->txint, !!(s->intstatus & R_INTSTATUS_TX_MASK));
    qemu_set_irq(s->rxint, !!(s->intstatus & R_INTSTATUS_RX_MASK));
    qemu_set_irq(s->txovrint, !!(s->intstatus & R_INTSTATUS_TXO_MASK));
    qemu_set_irq(s->rxovrint, !!(s->intstatus & R_INTSTATUS_RXO_MASK));
    qemu_set_irq(s->uartint, !!(s->intstatus));
}

static int uart_can_receive(void *opaque)
{
    CMSDKAPBUART *s = CMSDK_APB_UART(opaque);

    /* We can take a char if RX is enabled and the buffer is empty */
    if (s->ctrl & R_CTRL_RX_EN_MASK && !(s->state & R_STATE_RXFULL_MASK)) {
        return 1;
    }
    return 0;
}

static void uart_receive(void *opaque, const uint8_t *buf, int size)
{
    CMSDKAPBUART *s = CMSDK_APB_UART(opaque);

    trace_cmsdk_apb_uart_receive(*buf);

    /* In fact uart_can_receive() ensures that we can't be
     * called unless RX is enabled and the buffer is empty,
     * but we include this logic as documentation of what the
     * hardware does if a character arrives in these circumstances.
     */
    if (!(s->ctrl & R_CTRL_RX_EN_MASK)) {
        /* Just drop the character on the floor */
        return;
    }

    if (s->state & R_STATE_RXFULL_MASK) {
        s->state |= R_STATE_RXOVERRUN_MASK;
    }

    s->rxbuf = *buf;
    s->state |= R_STATE_RXFULL_MASK;
    if (s->ctrl & R_CTRL_RX_INTEN_MASK) {
        s->intstatus |= R_INTSTATUS_RX_MASK;
    }
    cmsdk_apb_uart_update(s);
}

static uint64_t uart_read(void *opaque, hwaddr offset, unsigned size)
{
    CMSDKAPBUART *s = CMSDK_APB_UART(opaque);
    uint64_t r;

    switch (offset) {
    case A_DATA:
        r = s->rxbuf;
        s->state &= ~R_STATE_RXFULL_MASK;
        cmsdk_apb_uart_update(s);
        qemu_chr_fe_accept_input(&s->chr);
        break;
    case A_STATE:
        r = s->state;
        break;
    case A_CTRL:
        r = s->ctrl;
        break;
    case A_INTSTATUS:
        r = s->intstatus;
        break;
    case A_BAUDDIV:
        r = s->bauddiv;
        break;
    case A_PID4 ... A_CID3:
        r = uart_id[(offset - A_PID4) / 4];
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "CMSDK APB UART read: bad offset %x\n", (int) offset);
        r = 0;
        break;
    }
    trace_cmsdk_apb_uart_read(offset, r, size);
    return r;
}

/* Try to send tx data, and arrange to be called back later if
 * we can't (ie the char backend is busy/blocking).
 */
static gboolean uart_transmit(GIOChannel *chan, GIOCondition cond, void *opaque)
{
    CMSDKAPBUART *s = CMSDK_APB_UART(opaque);
    int ret;

    s->watch_tag = 0;

    if (!(s->ctrl & R_CTRL_TX_EN_MASK) || !(s->state & R_STATE_TXFULL_MASK)) {
        return FALSE;
    }

    ret = qemu_chr_fe_write(&s->chr, &s->txbuf, 1);
    if (ret <= 0) {
        s->watch_tag = qemu_chr_fe_add_watch(&s->chr, G_IO_OUT | G_IO_HUP,
                                             uart_transmit, s);
        if (!s->watch_tag) {
            /* Most common reason to be here is "no chardev backend":
             * just insta-drain the buffer, so the serial output
             * goes into a void, rather than blocking the guest.
             */
            goto buffer_drained;
        }
        /* Transmit pending */
        trace_cmsdk_apb_uart_tx_pending();
        return FALSE;
    }

buffer_drained:
    /* Character successfully sent */
    trace_cmsdk_apb_uart_tx(s->txbuf);
    s->state &= ~R_STATE_TXFULL_MASK;
    /* Going from TXFULL set to clear triggers the tx interrupt */
    if (s->ctrl & R_CTRL_TX_INTEN_MASK) {
        s->intstatus |= R_INTSTATUS_TX_MASK;
    }
    cmsdk_apb_uart_update(s);
    return FALSE;
}

static void uart_cancel_transmit(CMSDKAPBUART *s)
{
    if (s->watch_tag) {
        g_source_remove(s->watch_tag);
        s->watch_tag = 0;
    }
}

static void uart_write(void *opaque, hwaddr offset, uint64_t value,
                       unsigned size)
{
    CMSDKAPBUART *s = CMSDK_APB_UART(opaque);

    trace_cmsdk_apb_uart_write(offset, value, size);

    switch (offset) {
    case A_DATA:
        s->txbuf = value;
        if (s->state & R_STATE_TXFULL_MASK) {
            /* Buffer already full -- note the overrun and let the
             * existing pending transmit callback handle the new char.
             */
            s->state |= R_STATE_TXOVERRUN_MASK;
            cmsdk_apb_uart_update(s);
        } else {
            s->state |= R_STATE_TXFULL_MASK;
            uart_transmit(NULL, G_IO_OUT, s);
        }
        break;
    case A_STATE:
        /* Bits 0 and 1 are read only; bits 2 and 3 are W1C */
        s->state &= ~(value &
                      (R_STATE_TXOVERRUN_MASK | R_STATE_RXOVERRUN_MASK));
        cmsdk_apb_uart_update(s);
        break;
    case A_CTRL:
        s->ctrl = value & 0x7f;
        if ((s->ctrl & R_CTRL_TX_EN_MASK) && !uart_baudrate_ok(s)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "CMSDK APB UART: Tx enabled with invalid baudrate\n");
        }
        cmsdk_apb_uart_update(s);
        break;
    case A_INTSTATUS:
        /* All bits are W1C. Clearing the overrun interrupt bits really
         * clears the overrun status bits in the STATE register (which
         * is then reflected into the intstatus value by the update function).
         */
        s->state &= ~(value & (R_INTSTATUS_TXO_MASK | R_INTSTATUS_RXO_MASK));
        s->intstatus &= ~value;
        cmsdk_apb_uart_update(s);
        break;
    case A_BAUDDIV:
        s->bauddiv = value & 0xFFFFF;
        uart_update_parameters(s);
        break;
    case A_PID4 ... A_CID3:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "CMSDK APB UART write: write to RO offset 0x%x\n",
                      (int)offset);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "CMSDK APB UART write: bad offset 0x%x\n", (int) offset);
        break;
    }
}

static const MemoryRegionOps uart_ops = {
    .read = uart_read,
    .write = uart_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void cmsdk_apb_uart_reset(DeviceState *dev)
{
    CMSDKAPBUART *s = CMSDK_APB_UART(dev);

    trace_cmsdk_apb_uart_reset();
    uart_cancel_transmit(s);
    s->state = 0;
    s->ctrl = 0;
    s->intstatus = 0;
    s->bauddiv = 0;
    s->txbuf = 0;
    s->rxbuf = 0;
}

static void cmsdk_apb_uart_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    CMSDKAPBUART *s = CMSDK_APB_UART(obj);

    memory_region_init_io(&s->iomem, obj, &uart_ops, s, "uart", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->txint);
    sysbus_init_irq(sbd, &s->rxint);
    sysbus_init_irq(sbd, &s->txovrint);
    sysbus_init_irq(sbd, &s->rxovrint);
    sysbus_init_irq(sbd, &s->uartint);
}

static void cmsdk_apb_uart_realize(DeviceState *dev, Error **errp)
{
    CMSDKAPBUART *s = CMSDK_APB_UART(dev);

    if (s->pclk_frq == 0) {
        error_setg(errp, "CMSDK APB UART: pclk-frq property must be set");
        return;
    }

    /* This UART has no flow control, so we do not need to register
     * an event handler to deal with CHR_EVENT_BREAK.
     */
    qemu_chr_fe_set_handlers(&s->chr, uart_can_receive, uart_receive,
                             NULL, NULL, s, NULL, true);
}

static int cmsdk_apb_uart_post_load(void *opaque, int version_id)
{
    CMSDKAPBUART *s = CMSDK_APB_UART(opaque);

    /* If we have a pending character, arrange to resend it. */
    if (s->state & R_STATE_TXFULL_MASK) {
        s->watch_tag = qemu_chr_fe_add_watch(&s->chr, G_IO_OUT | G_IO_HUP,
                                             uart_transmit, s);
    }
    uart_update_parameters(s);
    return 0;
}

static const VMStateDescription cmsdk_apb_uart_vmstate = {
    .name = "cmsdk-apb-uart",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = cmsdk_apb_uart_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(state, CMSDKAPBUART),
        VMSTATE_UINT32(ctrl, CMSDKAPBUART),
        VMSTATE_UINT32(intstatus, CMSDKAPBUART),
        VMSTATE_UINT32(bauddiv, CMSDKAPBUART),
        VMSTATE_UINT8(txbuf, CMSDKAPBUART),
        VMSTATE_UINT8(rxbuf, CMSDKAPBUART),
        VMSTATE_END_OF_LIST()
    }
};

static Property cmsdk_apb_uart_properties[] = {
    DEFINE_PROP_CHR("chardev", CMSDKAPBUART, chr),
    DEFINE_PROP_UINT32("pclk-frq", CMSDKAPBUART, pclk_frq, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void cmsdk_apb_uart_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = cmsdk_apb_uart_realize;
    dc->vmsd = &cmsdk_apb_uart_vmstate;
    dc->reset = cmsdk_apb_uart_reset;
    device_class_set_props(dc, cmsdk_apb_uart_properties);
}

static const TypeInfo cmsdk_apb_uart_info = {
    .name = TYPE_CMSDK_APB_UART,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(CMSDKAPBUART),
    .instance_init = cmsdk_apb_uart_init,
    .class_init = cmsdk_apb_uart_class_init,
};

static void cmsdk_apb_uart_register_types(void)
{
    type_register_static(&cmsdk_apb_uart_info);
}

type_init(cmsdk_apb_uart_register_types);
