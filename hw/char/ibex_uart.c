/*
 * QEMU lowRISC Ibex UART device
 *
 * Copyright (c) 2020 Western Digital
 *
 * For details check the documentation here:
 *    https://docs.opentitan.org/hw/ip/uart/doc/
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

#include "qemu/osdep.h"
#include "hw/char/ibex_uart.h"
#include "hw/irq.h"
#include "hw/qdev-clock.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"

static void ibex_uart_update_irqs(IbexUartState *s)
{
    if (s->uart_intr_state & s->uart_intr_enable & R_INTR_STATE_TX_WATERMARK_MASK) {
        qemu_set_irq(s->tx_watermark, 1);
    } else {
        qemu_set_irq(s->tx_watermark, 0);
    }

    if (s->uart_intr_state & s->uart_intr_enable & R_INTR_STATE_RX_WATERMARK_MASK) {
        qemu_set_irq(s->rx_watermark, 1);
    } else {
        qemu_set_irq(s->rx_watermark, 0);
    }

    if (s->uart_intr_state & s->uart_intr_enable & R_INTR_STATE_TX_EMPTY_MASK) {
        qemu_set_irq(s->tx_empty, 1);
    } else {
        qemu_set_irq(s->tx_empty, 0);
    }

    if (s->uart_intr_state & s->uart_intr_enable & R_INTR_STATE_RX_OVERFLOW_MASK) {
        qemu_set_irq(s->rx_overflow, 1);
    } else {
        qemu_set_irq(s->rx_overflow, 0);
    }
}

static int ibex_uart_can_receive(void *opaque)
{
    IbexUartState *s = opaque;

    if (s->uart_ctrl & R_CTRL_RX_ENABLE_MASK) {
        return 1;
    }

    return 0;
}

static void ibex_uart_receive(void *opaque, const uint8_t *buf, int size)
{
    IbexUartState *s = opaque;
    uint8_t rx_fifo_level = (s->uart_fifo_ctrl & R_FIFO_CTRL_RXILVL_MASK)
                            >> R_FIFO_CTRL_RXILVL_SHIFT;

    s->uart_rdata = *buf;

    s->uart_status &= ~R_STATUS_RXIDLE_MASK;
    s->uart_status &= ~R_STATUS_RXEMPTY_MASK;

    if (size > rx_fifo_level) {
        s->uart_intr_state |= R_INTR_STATE_RX_WATERMARK_MASK;
    }

    ibex_uart_update_irqs(s);
}

static gboolean ibex_uart_xmit(GIOChannel *chan, GIOCondition cond,
                               void *opaque)
{
    IbexUartState *s = opaque;
    uint8_t tx_fifo_level = (s->uart_fifo_ctrl & R_FIFO_CTRL_TXILVL_MASK)
                            >> R_FIFO_CTRL_TXILVL_SHIFT;
    int ret;

    /* instant drain the fifo when there's no back-end */
    if (!qemu_chr_fe_backend_connected(&s->chr)) {
        s->tx_level = 0;
        return FALSE;
    }

    if (!s->tx_level) {
        s->uart_status &= ~R_STATUS_TXFULL_MASK;
        s->uart_status |= R_STATUS_TXEMPTY_MASK;
        s->uart_intr_state |= R_INTR_STATE_TX_EMPTY_MASK;
        s->uart_intr_state &= ~R_INTR_STATE_TX_WATERMARK_MASK;
        ibex_uart_update_irqs(s);
        return FALSE;
    }

    ret = qemu_chr_fe_write(&s->chr, s->tx_fifo, s->tx_level);

    if (ret >= 0) {
        s->tx_level -= ret;
        memmove(s->tx_fifo, s->tx_fifo + ret, s->tx_level);
    }

    if (s->tx_level) {
        guint r = qemu_chr_fe_add_watch(&s->chr, G_IO_OUT | G_IO_HUP,
                                        ibex_uart_xmit, s);
        if (!r) {
            s->tx_level = 0;
            return FALSE;
        }
    }

    /* Clear the TX Full bit */
    if (s->tx_level != IBEX_UART_TX_FIFO_SIZE) {
        s->uart_status &= ~R_STATUS_TXFULL_MASK;
    }

    /* Disable the TX_WATERMARK IRQ */
    if (s->tx_level < tx_fifo_level) {
        s->uart_intr_state &= ~R_INTR_STATE_TX_WATERMARK_MASK;
    }

    /* Set TX empty */
    if (s->tx_level == 0) {
        s->uart_status |= R_STATUS_TXEMPTY_MASK;
        s->uart_intr_state |= R_INTR_STATE_TX_EMPTY_MASK;
    }

    ibex_uart_update_irqs(s);
    return FALSE;
}

static void uart_write_tx_fifo(IbexUartState *s, const uint8_t *buf,
                               int size)
{
    uint64_t current_time = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    uint8_t tx_fifo_level = (s->uart_fifo_ctrl & R_FIFO_CTRL_TXILVL_MASK)
                            >> R_FIFO_CTRL_TXILVL_SHIFT;

    if (size > IBEX_UART_TX_FIFO_SIZE - s->tx_level) {
        size = IBEX_UART_TX_FIFO_SIZE - s->tx_level;
        qemu_log_mask(LOG_GUEST_ERROR, "ibex_uart: TX FIFO overflow");
    }

    memcpy(s->tx_fifo + s->tx_level, buf, size);
    s->tx_level += size;

    if (s->tx_level > 0) {
        s->uart_status &= ~R_STATUS_TXEMPTY_MASK;
    }

    if (s->tx_level >= tx_fifo_level) {
        s->uart_intr_state |= R_INTR_STATE_TX_WATERMARK_MASK;
        ibex_uart_update_irqs(s);
    }

    if (s->tx_level == IBEX_UART_TX_FIFO_SIZE) {
        s->uart_status |= R_STATUS_TXFULL_MASK;
    }

    timer_mod(s->fifo_trigger_handle, current_time +
              (s->char_tx_time * 4));
}

static void ibex_uart_reset(DeviceState *dev)
{
    IbexUartState *s = IBEX_UART(dev);

    s->uart_intr_state = 0x00000000;
    s->uart_intr_state = 0x00000000;
    s->uart_intr_enable = 0x00000000;
    s->uart_ctrl = 0x00000000;
    s->uart_status = 0x0000003c;
    s->uart_rdata = 0x00000000;
    s->uart_fifo_ctrl = 0x00000000;
    s->uart_fifo_status = 0x00000000;
    s->uart_ovrd = 0x00000000;
    s->uart_val = 0x00000000;
    s->uart_timeout_ctrl = 0x00000000;

    s->tx_level = 0;

    s->char_tx_time = (NANOSECONDS_PER_SECOND / 230400) * 10;

    ibex_uart_update_irqs(s);
}

static uint64_t ibex_uart_get_baud(IbexUartState *s)
{
    uint64_t baud;

    baud = ((s->uart_ctrl & R_CTRL_NCO_MASK) >> 16);
    baud *= clock_get_hz(s->f_clk);
    baud >>= 20;

    return baud;
}

static uint64_t ibex_uart_read(void *opaque, hwaddr addr,
                                       unsigned int size)
{
    IbexUartState *s = opaque;
    uint64_t retvalue = 0;

    switch (addr >> 2) {
    case R_INTR_STATE:
        retvalue = s->uart_intr_state;
        break;
    case R_INTR_ENABLE:
        retvalue = s->uart_intr_enable;
        break;
    case R_INTR_TEST:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: wdata is write only\n", __func__);
        break;

    case R_CTRL:
        retvalue = s->uart_ctrl;
        break;
    case R_STATUS:
        retvalue = s->uart_status;
        break;

    case R_RDATA:
        retvalue = s->uart_rdata;
        if (s->uart_ctrl & R_CTRL_RX_ENABLE_MASK) {
            qemu_chr_fe_accept_input(&s->chr);

            s->uart_status |= R_STATUS_RXIDLE_MASK;
            s->uart_status |= R_STATUS_RXEMPTY_MASK;
        }
        break;
    case R_WDATA:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: wdata is write only\n", __func__);
        break;

    case R_FIFO_CTRL:
        retvalue = s->uart_fifo_ctrl;
        break;
    case R_FIFO_STATUS:
        retvalue = s->uart_fifo_status;

        retvalue |= s->tx_level & 0x1F;

        qemu_log_mask(LOG_UNIMP,
                      "%s: RX fifos are not supported\n", __func__);
        break;

    case R_OVRD:
        retvalue = s->uart_ovrd;
        qemu_log_mask(LOG_UNIMP,
                      "%s: ovrd is not supported\n", __func__);
        break;
    case R_VAL:
        retvalue = s->uart_val;
        qemu_log_mask(LOG_UNIMP,
                      "%s: val is not supported\n", __func__);
        break;
    case R_TIMEOUT_CTRL:
        retvalue = s->uart_timeout_ctrl;
        qemu_log_mask(LOG_UNIMP,
                      "%s: timeout_ctrl is not supported\n", __func__);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, addr);
        return 0;
    }

    return retvalue;
}

static void ibex_uart_write(void *opaque, hwaddr addr,
                                  uint64_t val64, unsigned int size)
{
    IbexUartState *s = opaque;
    uint32_t value = val64;

    switch (addr >> 2) {
    case R_INTR_STATE:
        /* Write 1 clear */
        s->uart_intr_state &= ~value;
        ibex_uart_update_irqs(s);
        break;
    case R_INTR_ENABLE:
        s->uart_intr_enable = value;
        ibex_uart_update_irqs(s);
        break;
    case R_INTR_TEST:
        s->uart_intr_state |= value;
        ibex_uart_update_irqs(s);
        break;

    case R_CTRL:
        s->uart_ctrl = value;

        if (value & R_CTRL_NF_MASK) {
            qemu_log_mask(LOG_UNIMP,
                          "%s: UART_CTRL_NF is not supported\n", __func__);
        }
        if (value & R_CTRL_SLPBK_MASK) {
            qemu_log_mask(LOG_UNIMP,
                          "%s: UART_CTRL_SLPBK is not supported\n", __func__);
        }
        if (value & R_CTRL_LLPBK_MASK) {
            qemu_log_mask(LOG_UNIMP,
                          "%s: UART_CTRL_LLPBK is not supported\n", __func__);
        }
        if (value & R_CTRL_PARITY_EN_MASK) {
            qemu_log_mask(LOG_UNIMP,
                          "%s: UART_CTRL_PARITY_EN is not supported\n",
                          __func__);
        }
        if (value & R_CTRL_PARITY_ODD_MASK) {
            qemu_log_mask(LOG_UNIMP,
                          "%s: UART_CTRL_PARITY_ODD is not supported\n",
                          __func__);
        }
        if (value & R_CTRL_RXBLVL_MASK) {
            qemu_log_mask(LOG_UNIMP,
                          "%s: UART_CTRL_RXBLVL is not supported\n", __func__);
        }
        if (value & R_CTRL_NCO_MASK) {
            uint64_t baud = ibex_uart_get_baud(s);

            s->char_tx_time = (NANOSECONDS_PER_SECOND / baud) * 10;
        }
        break;
    case R_STATUS:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: status is read only\n", __func__);
        break;

    case R_RDATA:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: rdata is read only\n", __func__);
        break;
    case R_WDATA:
        uart_write_tx_fifo(s, (uint8_t *) &value, 1);
        break;

    case R_FIFO_CTRL:
        s->uart_fifo_ctrl = value;

        if (value & R_FIFO_CTRL_RXRST_MASK) {
            qemu_log_mask(LOG_UNIMP,
                          "%s: RX fifos are not supported\n", __func__);
        }
        if (value & R_FIFO_CTRL_TXRST_MASK) {
            s->tx_level = 0;
        }
        break;
    case R_FIFO_STATUS:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: fifo_status is read only\n", __func__);
        break;

    case R_OVRD:
        s->uart_ovrd = value;
        qemu_log_mask(LOG_UNIMP,
                      "%s: ovrd is not supported\n", __func__);
        break;
    case R_VAL:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: val is read only\n", __func__);
        break;
    case R_TIMEOUT_CTRL:
        s->uart_timeout_ctrl = value;
        qemu_log_mask(LOG_UNIMP,
                      "%s: timeout_ctrl is not supported\n", __func__);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, addr);
    }
}

static void ibex_uart_clk_update(void *opaque)
{
    IbexUartState *s = opaque;

    /* recompute uart's speed on clock change */
    uint64_t baud = ibex_uart_get_baud(s);

    s->char_tx_time = (NANOSECONDS_PER_SECOND / baud) * 10;
}

static void fifo_trigger_update(void *opaque)
{
    IbexUartState *s = opaque;

    if (s->uart_ctrl & R_CTRL_TX_ENABLE_MASK) {
        ibex_uart_xmit(NULL, G_IO_OUT, s);
    }
}

static const MemoryRegionOps ibex_uart_ops = {
    .read = ibex_uart_read,
    .write = ibex_uart_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

static int ibex_uart_post_load(void *opaque, int version_id)
{
    IbexUartState *s = opaque;

    ibex_uart_update_irqs(s);
    return 0;
}

static const VMStateDescription vmstate_ibex_uart = {
    .name = TYPE_IBEX_UART,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = ibex_uart_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8_ARRAY(tx_fifo, IbexUartState,
                            IBEX_UART_TX_FIFO_SIZE),
        VMSTATE_UINT32(tx_level, IbexUartState),
        VMSTATE_UINT64(char_tx_time, IbexUartState),
        VMSTATE_TIMER_PTR(fifo_trigger_handle, IbexUartState),
        VMSTATE_UINT32(uart_intr_state, IbexUartState),
        VMSTATE_UINT32(uart_intr_enable, IbexUartState),
        VMSTATE_UINT32(uart_ctrl, IbexUartState),
        VMSTATE_UINT32(uart_status, IbexUartState),
        VMSTATE_UINT32(uart_rdata, IbexUartState),
        VMSTATE_UINT32(uart_fifo_ctrl, IbexUartState),
        VMSTATE_UINT32(uart_fifo_status, IbexUartState),
        VMSTATE_UINT32(uart_ovrd, IbexUartState),
        VMSTATE_UINT32(uart_val, IbexUartState),
        VMSTATE_UINT32(uart_timeout_ctrl, IbexUartState),
        VMSTATE_END_OF_LIST()
    }
};

static Property ibex_uart_properties[] = {
    DEFINE_PROP_CHR("chardev", IbexUartState, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void ibex_uart_init(Object *obj)
{
    IbexUartState *s = IBEX_UART(obj);

    s->f_clk = qdev_init_clock_in(DEVICE(obj), "f_clock",
                                  ibex_uart_clk_update, s);
    clock_set_hz(s->f_clk, IBEX_UART_CLOCK);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->tx_watermark);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->rx_watermark);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->tx_empty);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->rx_overflow);

    memory_region_init_io(&s->mmio, obj, &ibex_uart_ops, s,
                          TYPE_IBEX_UART, 0x400);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static void ibex_uart_realize(DeviceState *dev, Error **errp)
{
    IbexUartState *s = IBEX_UART(dev);

    s->fifo_trigger_handle = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                          fifo_trigger_update, s);

    qemu_chr_fe_set_handlers(&s->chr, ibex_uart_can_receive,
                             ibex_uart_receive, NULL, NULL,
                             s, NULL, true);
}

static void ibex_uart_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = ibex_uart_reset;
    dc->realize = ibex_uart_realize;
    dc->vmsd = &vmstate_ibex_uart;
    device_class_set_props(dc, ibex_uart_properties);
}

static const TypeInfo ibex_uart_info = {
    .name          = TYPE_IBEX_UART,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IbexUartState),
    .instance_init = ibex_uart_init,
    .class_init    = ibex_uart_class_init,
};

static void ibex_uart_register_types(void)
{
    type_register_static(&ibex_uart_info);
}

type_init(ibex_uart_register_types)
