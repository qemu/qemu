/*
 * QEMU model of the UART on the SiFive E300 and U500 series SOCs.
 *
 * Copyright (c) 2016 Stefan O'Rear
 *
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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "migration/vmstate.h"
#include "chardev/char.h"
#include "chardev/char-fe.h"
#include "hw/irq.h"
#include "hw/char/sifive_uart.h"
#include "hw/qdev-properties-system.h"

#define TX_INTERRUPT_TRIGGER_DELAY_NS 100

/*
 * Not yet implemented:
 *
 * Transmit FIFO using "qemu/fifo8.h"
 */

/* Returns the state of the IP (interrupt pending) register */
static uint64_t sifive_uart_ip(SiFiveUARTState *s)
{
    uint64_t ret = 0;

    uint64_t txcnt = SIFIVE_UART_GET_TXCNT(s->txctrl);
    uint64_t rxcnt = SIFIVE_UART_GET_RXCNT(s->rxctrl);

    if (txcnt != 0) {
        ret |= SIFIVE_UART_IP_TXWM;
    }
    if (s->rx_fifo_len > rxcnt) {
        ret |= SIFIVE_UART_IP_RXWM;
    }

    return ret;
}

static void sifive_uart_update_irq(SiFiveUARTState *s)
{
    int cond = 0;
    if ((s->ie & SIFIVE_UART_IE_TXWM) ||
        ((s->ie & SIFIVE_UART_IE_RXWM) && s->rx_fifo_len)) {
        cond = 1;
    }
    if (cond) {
        qemu_irq_raise(s->irq);
    } else {
        qemu_irq_lower(s->irq);
    }
}

static gboolean sifive_uart_xmit(void *do_not_use, GIOCondition cond,
                                 void *opaque)
{
    SiFiveUARTState *s = opaque;
    int ret;
    const uint8_t *characters;
    uint32_t numptr = 0;

    /* instant drain the fifo when there's no back-end */
    if (!qemu_chr_fe_backend_connected(&s->chr)) {
        fifo8_reset(&s->tx_fifo);
        return G_SOURCE_REMOVE;
    }

    if (fifo8_is_empty(&s->tx_fifo)) {
        return G_SOURCE_REMOVE;
    }

    /* Don't pop the FIFO in case the write fails */
    characters = fifo8_peek_bufptr(&s->tx_fifo,
                                   fifo8_num_used(&s->tx_fifo), &numptr);
    ret = qemu_chr_fe_write(&s->chr, characters, numptr);

    if (ret >= 0) {
        /* We wrote the data, actually pop the fifo */
        fifo8_pop_bufptr(&s->tx_fifo, ret, NULL);
    }

    if (!fifo8_is_empty(&s->tx_fifo)) {
        guint r = qemu_chr_fe_add_watch(&s->chr, G_IO_OUT | G_IO_HUP,
                                        sifive_uart_xmit, s);
        if (!r) {
            fifo8_reset(&s->tx_fifo);
            return G_SOURCE_REMOVE;
        }
    }

    /* Clear the TX Full bit */
    if (!fifo8_is_full(&s->tx_fifo)) {
        s->txfifo &= ~SIFIVE_UART_TXFIFO_FULL;
    }

    sifive_uart_update_irq(s);
    return G_SOURCE_REMOVE;
}

static void sifive_uart_write_tx_fifo(SiFiveUARTState *s, const uint8_t *buf,
                                      int size)
{
    uint64_t current_time = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    if (size > fifo8_num_free(&s->tx_fifo)) {
        size = fifo8_num_free(&s->tx_fifo);
        qemu_log_mask(LOG_GUEST_ERROR, "sifive_uart: TX FIFO overflow");
    }

    fifo8_push_all(&s->tx_fifo, buf, size);

    if (fifo8_is_full(&s->tx_fifo)) {
        s->txfifo |= SIFIVE_UART_TXFIFO_FULL;
    }

    timer_mod(s->fifo_trigger_handle, current_time +
                  TX_INTERRUPT_TRIGGER_DELAY_NS);
}

static uint64_t
sifive_uart_read(void *opaque, hwaddr addr, unsigned int size)
{
    SiFiveUARTState *s = opaque;
    unsigned char r;
    switch (addr) {
    case SIFIVE_UART_RXFIFO:
        if (s->rx_fifo_len) {
            r = s->rx_fifo[0];
            memmove(s->rx_fifo, s->rx_fifo + 1, s->rx_fifo_len - 1);
            s->rx_fifo_len--;
            qemu_chr_fe_accept_input(&s->chr);
            sifive_uart_update_irq(s);
            return r;
        }
        return 0x80000000;

    case SIFIVE_UART_TXFIFO:
        return s->txfifo;
    case SIFIVE_UART_IE:
        return s->ie;
    case SIFIVE_UART_IP:
        return sifive_uart_ip(s);
    case SIFIVE_UART_TXCTRL:
        return s->txctrl;
    case SIFIVE_UART_RXCTRL:
        return s->rxctrl;
    case SIFIVE_UART_DIV:
        return s->div;
    }

    qemu_log_mask(LOG_GUEST_ERROR, "%s: bad read: addr=0x%x\n",
                  __func__, (int)addr);
    return 0;
}

static void
sifive_uart_write(void *opaque, hwaddr addr,
                  uint64_t val64, unsigned int size)
{
    SiFiveUARTState *s = opaque;
    uint32_t value = val64;
    uint8_t ch = value;

    switch (addr) {
    case SIFIVE_UART_TXFIFO:
        sifive_uart_write_tx_fifo(s, &ch, 1);
        return;
    case SIFIVE_UART_IE:
        s->ie = val64;
        sifive_uart_update_irq(s);
        return;
    case SIFIVE_UART_TXCTRL:
        s->txctrl = val64;
        return;
    case SIFIVE_UART_RXCTRL:
        s->rxctrl = val64;
        return;
    case SIFIVE_UART_DIV:
        s->div = val64;
        return;
    }
    qemu_log_mask(LOG_GUEST_ERROR, "%s: bad write: addr=0x%x v=0x%x\n",
                  __func__, (int)addr, (int)value);
}

static void fifo_trigger_update(void *opaque)
{
    SiFiveUARTState *s = opaque;

    sifive_uart_xmit(NULL, G_IO_OUT, s);
}

static const MemoryRegionOps sifive_uart_ops = {
    .read = sifive_uart_read,
    .write = sifive_uart_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

static void sifive_uart_rx(void *opaque, const uint8_t *buf, int size)
{
    SiFiveUARTState *s = opaque;

    /* Got a byte.  */
    if (s->rx_fifo_len >= sizeof(s->rx_fifo)) {
        printf("WARNING: UART dropped char.\n");
        return;
    }
    s->rx_fifo[s->rx_fifo_len++] = *buf;

    sifive_uart_update_irq(s);
}

static int sifive_uart_can_rx(void *opaque)
{
    SiFiveUARTState *s = opaque;

    return s->rx_fifo_len < sizeof(s->rx_fifo);
}

static void sifive_uart_event(void *opaque, QEMUChrEvent event)
{
}

static int sifive_uart_be_change(void *opaque)
{
    SiFiveUARTState *s = opaque;

    qemu_chr_fe_set_handlers(&s->chr, sifive_uart_can_rx, sifive_uart_rx,
                             sifive_uart_event, sifive_uart_be_change, s,
                             NULL, true);

    return 0;
}

static const Property sifive_uart_properties[] = {
    DEFINE_PROP_CHR("chardev", SiFiveUARTState, chr),
};

static void sifive_uart_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    SiFiveUARTState *s = SIFIVE_UART(obj);

    memory_region_init_io(&s->mmio, OBJECT(s), &sifive_uart_ops, s,
                          TYPE_SIFIVE_UART, SIFIVE_UART_MAX);
    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_init_irq(sbd, &s->irq);
}

static void sifive_uart_realize(DeviceState *dev, Error **errp)
{
    SiFiveUARTState *s = SIFIVE_UART(dev);

    s->fifo_trigger_handle = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                          fifo_trigger_update, s);

    qemu_chr_fe_set_handlers(&s->chr, sifive_uart_can_rx, sifive_uart_rx,
                             sifive_uart_event, sifive_uart_be_change, s,
                             NULL, true);

}

static void sifive_uart_reset_enter(Object *obj, ResetType type)
{
    SiFiveUARTState *s = SIFIVE_UART(obj);

    s->txfifo = 0;
    s->ie = 0;
    s->ip = 0;
    s->txctrl = 0;
    s->rxctrl = 0;
    s->div = 0;

    s->rx_fifo_len = 0;

    memset(s->rx_fifo, 0, SIFIVE_UART_RX_FIFO_SIZE);
    fifo8_create(&s->tx_fifo, SIFIVE_UART_TX_FIFO_SIZE);
}

static void sifive_uart_reset_hold(Object *obj, ResetType type)
{
    SiFiveUARTState *s = SIFIVE_UART(obj);
    qemu_irq_lower(s->irq);
}

static const VMStateDescription vmstate_sifive_uart = {
    .name = TYPE_SIFIVE_UART,
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8_ARRAY(rx_fifo, SiFiveUARTState,
                            SIFIVE_UART_RX_FIFO_SIZE),
        VMSTATE_UINT8(rx_fifo_len, SiFiveUARTState),
        VMSTATE_UINT32(ie, SiFiveUARTState),
        VMSTATE_UINT32(ip, SiFiveUARTState),
        VMSTATE_UINT32(txctrl, SiFiveUARTState),
        VMSTATE_UINT32(rxctrl, SiFiveUARTState),
        VMSTATE_UINT32(div, SiFiveUARTState),
        VMSTATE_UINT32(txfifo, SiFiveUARTState),
        VMSTATE_FIFO8(tx_fifo, SiFiveUARTState),
        VMSTATE_TIMER_PTR(fifo_trigger_handle, SiFiveUARTState),
        VMSTATE_END_OF_LIST()
    },
};


static void sifive_uart_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    dc->realize = sifive_uart_realize;
    dc->vmsd = &vmstate_sifive_uart;
    rc->phases.enter = sifive_uart_reset_enter;
    rc->phases.hold  = sifive_uart_reset_hold;
    device_class_set_props(dc, sifive_uart_properties);
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
}

static const TypeInfo sifive_uart_info = {
    .name          = TYPE_SIFIVE_UART,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SiFiveUARTState),
    .instance_init = sifive_uart_init,
    .class_init    = sifive_uart_class_init,
};

static void sifive_uart_register_types(void)
{
    type_register_static(&sifive_uart_info);
}

type_init(sifive_uart_register_types)

/*
 * Create UART device.
 */
SiFiveUARTState *sifive_uart_create(MemoryRegion *address_space, hwaddr base,
    Chardev *chr, qemu_irq irq)
{
    DeviceState *dev;
    SysBusDevice *s;

    dev = qdev_new("riscv.sifive.uart");
    s = SYS_BUS_DEVICE(dev);
    qdev_prop_set_chr(dev, "chardev", chr);
    sysbus_realize_and_unref(s, &error_fatal);
    memory_region_add_subregion(address_space, base,
                                sysbus_mmio_get_region(s, 0));
    sysbus_connect_irq(s, 0, irq);

    return SIFIVE_UART(dev);
}
