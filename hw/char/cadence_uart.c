/*
 * Device model for Cadence UART
 *
 * Reference: Xilinx Zynq 7000 reference manual
 *   - http://www.xilinx.com/support/documentation/user_guides/ug585-Zynq-7000-TRM.pdf
 *   - Chapter 19 UART Controller
 *   - Appendix B for Register details
 *
 * Copyright (c) 2010 Xilinx Inc.
 * Copyright (c) 2012 Peter A.G. Crosthwaite (peter.crosthwaite@petalogix.com)
 * Copyright (c) 2012 PetaLogix Pty Ltd.
 * Written by Haibing Ma
 *            M.Habib
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "chardev/char-fe.h"
#include "chardev/char-serial.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/char/cadence_uart.h"
#include "hw/irq.h"
#include "hw/qdev-clock.h"
#include "hw/qdev-properties-system.h"
#include "trace.h"

#ifdef CADENCE_UART_ERR_DEBUG
#define DB_PRINT(...) do { \
    fprintf(stderr,  ": %s: ", __func__); \
    fprintf(stderr, ## __VA_ARGS__); \
    } while (0)
#else
    #define DB_PRINT(...)
#endif

#define UART_SR_INTR_RTRIG     0x00000001
#define UART_SR_INTR_REMPTY    0x00000002
#define UART_SR_INTR_RFUL      0x00000004
#define UART_SR_INTR_TEMPTY    0x00000008
#define UART_SR_INTR_TFUL      0x00000010
/* somewhat awkwardly, TTRIG is misaligned between SR and ISR */
#define UART_SR_TTRIG          0x00002000
#define UART_INTR_TTRIG        0x00000400
/* bits fields in CSR that correlate to CISR. If any of these bits are set in
 * SR, then the same bit in CISR is set high too */
#define UART_SR_TO_CISR_MASK   0x0000001F

#define UART_INTR_ROVR         0x00000020
#define UART_INTR_FRAME        0x00000040
#define UART_INTR_PARE         0x00000080
#define UART_INTR_TIMEOUT      0x00000100
#define UART_INTR_DMSI         0x00000200
#define UART_INTR_TOVR         0x00001000

#define UART_SR_RACTIVE    0x00000400
#define UART_SR_TACTIVE    0x00000800
#define UART_SR_FDELT      0x00001000

#define UART_CR_RXRST       0x00000001
#define UART_CR_TXRST       0x00000002
#define UART_CR_RX_EN       0x00000004
#define UART_CR_RX_DIS      0x00000008
#define UART_CR_TX_EN       0x00000010
#define UART_CR_TX_DIS      0x00000020
#define UART_CR_RST_TO      0x00000040
#define UART_CR_STARTBRK    0x00000080
#define UART_CR_STOPBRK     0x00000100

#define UART_MR_CLKS            0x00000001
#define UART_MR_CHRL            0x00000006
#define UART_MR_CHRL_SH         1
#define UART_MR_PAR             0x00000038
#define UART_MR_PAR_SH          3
#define UART_MR_NBSTOP          0x000000C0
#define UART_MR_NBSTOP_SH       6
#define UART_MR_CHMODE          0x00000300
#define UART_MR_CHMODE_SH       8
#define UART_MR_UCLKEN          0x00000400
#define UART_MR_IRMODE          0x00000800

#define UART_DATA_BITS_6       (0x3 << UART_MR_CHRL_SH)
#define UART_DATA_BITS_7       (0x2 << UART_MR_CHRL_SH)
#define UART_PARITY_ODD        (0x1 << UART_MR_PAR_SH)
#define UART_PARITY_EVEN       (0x0 << UART_MR_PAR_SH)
#define UART_STOP_BITS_1       (0x3 << UART_MR_NBSTOP_SH)
#define UART_STOP_BITS_2       (0x2 << UART_MR_NBSTOP_SH)
#define NORMAL_MODE            (0x0 << UART_MR_CHMODE_SH)
#define ECHO_MODE              (0x1 << UART_MR_CHMODE_SH)
#define LOCAL_LOOPBACK         (0x2 << UART_MR_CHMODE_SH)
#define REMOTE_LOOPBACK        (0x3 << UART_MR_CHMODE_SH)

#define UART_DEFAULT_REF_CLK (50 * 1000 * 1000)

#define R_CR       (0x00/4)
#define R_MR       (0x04/4)
#define R_IER      (0x08/4)
#define R_IDR      (0x0C/4)
#define R_IMR      (0x10/4)
#define R_CISR     (0x14/4)
#define R_BRGR     (0x18/4)
#define R_RTOR     (0x1C/4)
#define R_RTRIG    (0x20/4)
#define R_MCR      (0x24/4)
#define R_MSR      (0x28/4)
#define R_SR       (0x2C/4)
#define R_TX_RX    (0x30/4)
#define R_BDIV     (0x34/4)
#define R_FDEL     (0x38/4)
#define R_PMIN     (0x3C/4)
#define R_PWID     (0x40/4)
#define R_TTRIG    (0x44/4)


static void uart_update_status(CadenceUARTState *s)
{
    s->r[R_SR] = 0;

    s->r[R_SR] |= s->rx_count == CADENCE_UART_RX_FIFO_SIZE ? UART_SR_INTR_RFUL
                                                           : 0;
    s->r[R_SR] |= !s->rx_count ? UART_SR_INTR_REMPTY : 0;
    s->r[R_SR] |= s->rx_count >= s->r[R_RTRIG] ? UART_SR_INTR_RTRIG : 0;

    s->r[R_SR] |= s->tx_count == CADENCE_UART_TX_FIFO_SIZE ? UART_SR_INTR_TFUL
                                                           : 0;
    s->r[R_SR] |= !s->tx_count ? UART_SR_INTR_TEMPTY : 0;
    s->r[R_SR] |= s->tx_count >= s->r[R_TTRIG] ? UART_SR_TTRIG : 0;

    s->r[R_CISR] |= s->r[R_SR] & UART_SR_TO_CISR_MASK;
    s->r[R_CISR] |= s->r[R_SR] & UART_SR_TTRIG ? UART_INTR_TTRIG : 0;
    qemu_set_irq(s->irq, !!(s->r[R_IMR] & s->r[R_CISR]));
}

static void fifo_trigger_update(void *opaque)
{
    CadenceUARTState *s = opaque;

    if (s->r[R_RTOR]) {
        s->r[R_CISR] |= UART_INTR_TIMEOUT;
        uart_update_status(s);
    }
}

static void uart_rx_reset(CadenceUARTState *s)
{
    s->rx_wpos = 0;
    s->rx_count = 0;
    qemu_chr_fe_accept_input(&s->chr);
}

static void uart_tx_reset(CadenceUARTState *s)
{
    s->tx_count = 0;
}

static void uart_send_breaks(CadenceUARTState *s)
{
    int break_enabled = 1;

    qemu_chr_fe_ioctl(&s->chr, CHR_IOCTL_SERIAL_SET_BREAK,
                      &break_enabled);
}

static void uart_parameters_setup(CadenceUARTState *s)
{
    QEMUSerialSetParams ssp;
    unsigned int baud_rate, packet_size, input_clk;
    input_clk = clock_get_hz(s->refclk);

    baud_rate = (s->r[R_MR] & UART_MR_CLKS) ? input_clk / 8 : input_clk;
    baud_rate /= (s->r[R_BRGR] * (s->r[R_BDIV] + 1));
    trace_cadence_uart_baudrate(baud_rate);

    ssp.speed = baud_rate;

    packet_size = 1;

    switch (s->r[R_MR] & UART_MR_PAR) {
    case UART_PARITY_EVEN:
        ssp.parity = 'E';
        packet_size++;
        break;
    case UART_PARITY_ODD:
        ssp.parity = 'O';
        packet_size++;
        break;
    default:
        ssp.parity = 'N';
        break;
    }

    switch (s->r[R_MR] & UART_MR_CHRL) {
    case UART_DATA_BITS_6:
        ssp.data_bits = 6;
        break;
    case UART_DATA_BITS_7:
        ssp.data_bits = 7;
        break;
    default:
        ssp.data_bits = 8;
        break;
    }

    switch (s->r[R_MR] & UART_MR_NBSTOP) {
    case UART_STOP_BITS_1:
        ssp.stop_bits = 1;
        break;
    default:
        ssp.stop_bits = 2;
        break;
    }

    packet_size += ssp.data_bits + ssp.stop_bits;
    if (ssp.speed == 0) {
        /*
         * Avoid division-by-zero below.
         * TODO: find something better
         */
        ssp.speed = 1;
    }
    s->char_tx_time = (NANOSECONDS_PER_SECOND / ssp.speed) * packet_size;
    qemu_chr_fe_ioctl(&s->chr, CHR_IOCTL_SERIAL_SET_PARAMS, &ssp);
}

static int uart_can_receive(void *opaque)
{
    CadenceUARTState *s = opaque;
    int ret;
    uint32_t ch_mode;

    /* ignore characters when unclocked or in reset */
    if (!clock_is_enabled(s->refclk) || device_is_in_reset(DEVICE(s))) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: uart is unclocked or in reset\n",
                      __func__);
        return 0;
    }

    ret = MAX(CADENCE_UART_RX_FIFO_SIZE, CADENCE_UART_TX_FIFO_SIZE);
    ch_mode = s->r[R_MR] & UART_MR_CHMODE;

    if (ch_mode == NORMAL_MODE || ch_mode == ECHO_MODE) {
        ret = MIN(ret, CADENCE_UART_RX_FIFO_SIZE - s->rx_count);
    }
    if (ch_mode == REMOTE_LOOPBACK || ch_mode == ECHO_MODE) {
        ret = MIN(ret, CADENCE_UART_TX_FIFO_SIZE - s->tx_count);
    }
    return ret;
}

static void uart_ctrl_update(CadenceUARTState *s)
{
    if (s->r[R_CR] & UART_CR_TXRST) {
        uart_tx_reset(s);
    }

    if (s->r[R_CR] & UART_CR_RXRST) {
        uart_rx_reset(s);
    }

    s->r[R_CR] &= ~(UART_CR_TXRST | UART_CR_RXRST);

    if (s->r[R_CR] & UART_CR_STARTBRK && !(s->r[R_CR] & UART_CR_STOPBRK)) {
        uart_send_breaks(s);
    }
}

static void uart_write_rx_fifo(void *opaque, const uint8_t *buf, int size)
{
    CadenceUARTState *s = opaque;
    uint64_t new_rx_time = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    int i;

    if ((s->r[R_CR] & UART_CR_RX_DIS) || !(s->r[R_CR] & UART_CR_RX_EN)) {
        return;
    }

    if (s->rx_count == CADENCE_UART_RX_FIFO_SIZE) {
        s->r[R_CISR] |= UART_INTR_ROVR;
    } else {
        for (i = 0; i < size; i++) {
            s->rx_fifo[s->rx_wpos] = buf[i];
            s->rx_wpos = (s->rx_wpos + 1) % CADENCE_UART_RX_FIFO_SIZE;
            s->rx_count++;
        }
        timer_mod(s->fifo_trigger_handle, new_rx_time +
                                                (s->char_tx_time * 4));
    }
    uart_update_status(s);
}

static gboolean cadence_uart_xmit(void *do_not_use, GIOCondition cond,
                                  void *opaque)
{
    CadenceUARTState *s = opaque;
    int ret;

    /* instant drain the fifo when there's no back-end */
    if (!qemu_chr_fe_backend_connected(&s->chr)) {
        s->tx_count = 0;
        return FALSE;
    }

    if (!s->tx_count) {
        return FALSE;
    }

    ret = qemu_chr_fe_write(&s->chr, s->tx_fifo, s->tx_count);

    if (ret >= 0) {
        s->tx_count -= ret;
        memmove(s->tx_fifo, s->tx_fifo + ret, s->tx_count);
    }

    if (s->tx_count) {
        guint r = qemu_chr_fe_add_watch(&s->chr, G_IO_OUT | G_IO_HUP,
                                        cadence_uart_xmit, s);
        if (!r) {
            s->tx_count = 0;
            return FALSE;
        }
    }

    uart_update_status(s);
    return FALSE;
}

static void uart_write_tx_fifo(CadenceUARTState *s, const uint8_t *buf,
                               int size)
{
    if ((s->r[R_CR] & UART_CR_TX_DIS) || !(s->r[R_CR] & UART_CR_TX_EN)) {
        return;
    }

    if (size > CADENCE_UART_TX_FIFO_SIZE - s->tx_count) {
        size = CADENCE_UART_TX_FIFO_SIZE - s->tx_count;
        /*
         * This can only be a guest error via a bad tx fifo register push,
         * as can_receive() should stop remote loop and echo modes ever getting
         * us to here.
         */
        qemu_log_mask(LOG_GUEST_ERROR, "cadence_uart: TxFIFO overflow");
        s->r[R_CISR] |= UART_INTR_ROVR;
    }

    memcpy(s->tx_fifo + s->tx_count, buf, size);
    s->tx_count += size;

    cadence_uart_xmit(NULL, G_IO_OUT, s);
}

static void uart_receive(void *opaque, const uint8_t *buf, int size)
{
    CadenceUARTState *s = opaque;
    uint32_t ch_mode = s->r[R_MR] & UART_MR_CHMODE;

    if (ch_mode == NORMAL_MODE || ch_mode == ECHO_MODE) {
        uart_write_rx_fifo(opaque, buf, size);
    }
    if (ch_mode == REMOTE_LOOPBACK || ch_mode == ECHO_MODE) {
        uart_write_tx_fifo(s, buf, size);
    }
}

static void uart_event(void *opaque, QEMUChrEvent event)
{
    CadenceUARTState *s = opaque;
    uint8_t buf = '\0';

    /* ignore characters when unclocked or in reset */
    if (!clock_is_enabled(s->refclk) || device_is_in_reset(DEVICE(s))) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: uart is unclocked or in reset\n",
                      __func__);
        return;
    }

    if (event == CHR_EVENT_BREAK) {
        uart_write_rx_fifo(opaque, &buf, 1);
    }

    uart_update_status(s);
}

static void uart_read_rx_fifo(CadenceUARTState *s, uint32_t *c)
{
    if ((s->r[R_CR] & UART_CR_RX_DIS) || !(s->r[R_CR] & UART_CR_RX_EN)) {
        return;
    }

    if (s->rx_count) {
        uint32_t rx_rpos = (CADENCE_UART_RX_FIFO_SIZE + s->rx_wpos -
                            s->rx_count) % CADENCE_UART_RX_FIFO_SIZE;
        *c = s->rx_fifo[rx_rpos];
        s->rx_count--;

        qemu_chr_fe_accept_input(&s->chr);
    } else {
        *c = 0;
    }

    uart_update_status(s);
}

static MemTxResult uart_write(void *opaque, hwaddr offset,
                              uint64_t value, unsigned size, MemTxAttrs attrs)
{
    CadenceUARTState *s = opaque;

    /* ignore access when unclocked or in reset */
    if (!clock_is_enabled(s->refclk) || device_is_in_reset(DEVICE(s))) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: uart is unclocked or in reset\n",
                      __func__);
        return MEMTX_ERROR;
    }

    DB_PRINT(" offset:%x data:%08x\n", (unsigned)offset, (unsigned)value);
    offset >>= 2;
    if (offset >= CADENCE_UART_R_MAX) {
        return MEMTX_DECODE_ERROR;
    }
    switch (offset) {
    case R_IER: /* ier (wts imr) */
        s->r[R_IMR] |= value;
        break;
    case R_IDR: /* idr (wtc imr) */
        s->r[R_IMR] &= ~value;
        break;
    case R_IMR: /* imr (read only) */
        break;
    case R_CISR: /* cisr (wtc) */
        s->r[R_CISR] &= ~value;
        break;
    case R_TX_RX: /* UARTDR */
        switch (s->r[R_MR] & UART_MR_CHMODE) {
        case NORMAL_MODE:
            uart_write_tx_fifo(s, (uint8_t *) &value, 1);
            break;
        case LOCAL_LOOPBACK:
            uart_write_rx_fifo(opaque, (uint8_t *) &value, 1);
            break;
        }
        break;
    case R_BRGR: /* Baud rate generator */
        value &= 0xffff;
        if (value >= 0x01) {
            s->r[offset] = value;
        }
        break;
    case R_BDIV:    /* Baud rate divider */
        value &= 0xff;
        if (value >= 0x04) {
            s->r[offset] = value;
        }
        break;
    default:
        s->r[offset] = value;
    }

    switch (offset) {
    case R_CR:
        uart_ctrl_update(s);
        break;
    case R_MR:
        uart_parameters_setup(s);
        break;
    }
    uart_update_status(s);

    return MEMTX_OK;
}

static MemTxResult uart_read(void *opaque, hwaddr offset,
                             uint64_t *value, unsigned size, MemTxAttrs attrs)
{
    CadenceUARTState *s = opaque;
    uint32_t c = 0;

    /* ignore access when unclocked or in reset */
    if (!clock_is_enabled(s->refclk) || device_is_in_reset(DEVICE(s))) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: uart is unclocked or in reset\n",
                      __func__);
        return MEMTX_ERROR;
    }

    offset >>= 2;
    if (offset >= CADENCE_UART_R_MAX) {
        return MEMTX_DECODE_ERROR;
    }
    if (offset == R_TX_RX) {
        uart_read_rx_fifo(s, &c);
    } else {
        c = s->r[offset];
    }

    DB_PRINT(" offset:%x data:%08x\n", (unsigned)(offset << 2), (unsigned)c);
    *value = c;
    return MEMTX_OK;
}

static const MemoryRegionOps uart_ops = {
    .read_with_attrs = uart_read,
    .write_with_attrs = uart_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void cadence_uart_reset_init(Object *obj, ResetType type)
{
    CadenceUARTState *s = CADENCE_UART(obj);

    s->r[R_CR] = 0x00000128;
    s->r[R_IMR] = 0;
    s->r[R_CISR] = 0;
    s->r[R_RTRIG] = 0x00000020;
    s->r[R_BRGR] = 0x0000028B;
    s->r[R_BDIV] = 0x0000000F;
    s->r[R_TTRIG] = 0x00000020;
}

static void cadence_uart_reset_hold(Object *obj)
{
    CadenceUARTState *s = CADENCE_UART(obj);

    uart_rx_reset(s);
    uart_tx_reset(s);

    uart_update_status(s);
}

static void cadence_uart_realize(DeviceState *dev, Error **errp)
{
    CadenceUARTState *s = CADENCE_UART(dev);

    s->fifo_trigger_handle = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                          fifo_trigger_update, s);

    qemu_chr_fe_set_handlers(&s->chr, uart_can_receive, uart_receive,
                             uart_event, NULL, s, NULL, true);
}

static void cadence_uart_refclk_update(void *opaque, ClockEvent event)
{
    CadenceUARTState *s = opaque;

    /* recompute uart's speed on clock change */
    uart_parameters_setup(s);
}

static void cadence_uart_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    CadenceUARTState *s = CADENCE_UART(obj);

    memory_region_init_io(&s->iomem, obj, &uart_ops, s, "uart", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    s->refclk = qdev_init_clock_in(DEVICE(obj), "refclk",
                                   cadence_uart_refclk_update, s, ClockUpdate);
    /* initialize the frequency in case the clock remains unconnected */
    clock_set_hz(s->refclk, UART_DEFAULT_REF_CLK);

    s->char_tx_time = (NANOSECONDS_PER_SECOND / 9600) * 10;
}

static int cadence_uart_pre_load(void *opaque)
{
    CadenceUARTState *s = opaque;

    /* the frequency will be overriden if the refclk field is present */
    clock_set_hz(s->refclk, UART_DEFAULT_REF_CLK);
    return 0;
}

static int cadence_uart_post_load(void *opaque, int version_id)
{
    CadenceUARTState *s = opaque;

    /* Ensure these two aren't invalid numbers */
    if (s->r[R_BRGR] < 1 || s->r[R_BRGR] & ~0xFFFF ||
        s->r[R_BDIV] <= 3 || s->r[R_BDIV] & ~0xFF) {
        /* Value is invalid, abort */
        return 1;
    }

    uart_parameters_setup(s);
    uart_update_status(s);
    return 0;
}

static const VMStateDescription vmstate_cadence_uart = {
    .name = "cadence_uart",
    .version_id = 3,
    .minimum_version_id = 2,
    .pre_load = cadence_uart_pre_load,
    .post_load = cadence_uart_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(r, CadenceUARTState, CADENCE_UART_R_MAX),
        VMSTATE_UINT8_ARRAY(rx_fifo, CadenceUARTState,
                            CADENCE_UART_RX_FIFO_SIZE),
        VMSTATE_UINT8_ARRAY(tx_fifo, CadenceUARTState,
                            CADENCE_UART_TX_FIFO_SIZE),
        VMSTATE_UINT32(rx_count, CadenceUARTState),
        VMSTATE_UINT32(tx_count, CadenceUARTState),
        VMSTATE_UINT32(rx_wpos, CadenceUARTState),
        VMSTATE_TIMER_PTR(fifo_trigger_handle, CadenceUARTState),
        VMSTATE_CLOCK_V(refclk, CadenceUARTState, 3),
        VMSTATE_END_OF_LIST()
    },
};

static Property cadence_uart_properties[] = {
    DEFINE_PROP_CHR("chardev", CadenceUARTState, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void cadence_uart_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = cadence_uart_realize;
    dc->vmsd = &vmstate_cadence_uart;
    rc->phases.enter = cadence_uart_reset_init;
    rc->phases.hold  = cadence_uart_reset_hold;
    device_class_set_props(dc, cadence_uart_properties);
  }

static const TypeInfo cadence_uart_info = {
    .name          = TYPE_CADENCE_UART,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(CadenceUARTState),
    .instance_init = cadence_uart_init,
    .class_init    = cadence_uart_class_init,
};

static void cadence_uart_register_types(void)
{
    type_register_static(&cadence_uart_info);
}

type_init(cadence_uart_register_types)
