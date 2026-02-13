/*
 * calypso_uart.c — Calypso UART with CharBackend
 *
 * TI Calypso 16550-like UART with 64-byte FIFO, DLAB routing,
 * enhanced mode (LCR=0xBF → EFR access), level-sensitive IRQ.
 *
 * BUG FIXES vs previous inline implementation:
 *   1. RHR now pops from FIFO (was hardcoded 0x41)
 *   2. IIR computed dynamically (was hardcoded RDI|FIFO_EN)
 *   3. LSR DR bit reflects actual FIFO state (was always set)
 *
 * Register map (8-bit, offsets from base):
 *   Offset  DLAB=0/R     DLAB=0/W     DLAB=1       LCR=0xBF
 *   0x00    RHR          THR          DLL          DLL
 *   0x01    IER          IER          DLH          IER
 *   0x02    IIR          FCR          IIR/FCR      EFR
 *   0x03    LCR          LCR          LCR          LCR
 *   0x04    MCR          MCR          MCR          XON1
 *   0x05    LSR          —            LSR          XON2
 *   0x06    MSR          MSR          MSR          XOFF1
 *   0x07    SPR          SPR          SPR          XOFF2
 *   0x08    MDR1         MDR1
 *   0x10    SCR/SSR      SCR/SSR
 *   0x18    TXFLL        —
 *   0x1A    RXFLL        —
 *   0x80    DLL alias (Calypso-specific)
 *   0x81    DLH alias (Calypso-specific)
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "chardev/char-fe.h"
#include "qemu/log.h"
#include "calypso_uart.h"

/* ---- LSR bits ---- */
#define UART_LSR_DR   0x01  /* Data Ready (FIFO has data)     */
#define UART_LSR_OE   0x02  /* Overrun Error                  */
#define UART_LSR_THRE 0x20  /* THR Empty                      */
#define UART_LSR_TEMT 0x40  /* Transmitter completely Empty    */

/* ---- IER bits ---- */
#define UART_IER_RDI  0x01  /* RX Data Available interrupt    */
#define UART_IER_THRI 0x02  /* THR Empty interrupt            */
#define UART_IER_RLSI 0x04  /* RX Line Status interrupt       */
#define UART_IER_MSI  0x08  /* Modem Status interrupt         */

/* ---- IIR values ---- */
#define UART_IIR_NO_INT    0x01  /* No interrupt pending       */
#define UART_IIR_RDI       0x04  /* RX Data Available          */
#define UART_IIR_THRI      0x02  /* THR Empty                  */
#define UART_IIR_RLSI      0x06  /* RX Line Status             */
#define UART_IIR_FIFO_EN   0xC0  /* FIFOs enabled indicator    */

/* ---- LCR bits ---- */
#define UART_LCR_DLAB     0x80  /* Divisor Latch Access Bit   */
#define UART_LCR_ENHANCED 0xBF  /* Magic value for EFR access */

/* ================================================================
 * FIFO helpers
 * ================================================================ */

static inline bool uart_rx_empty(CalypsoUARTState *s)
{
    return s->rx_count == 0;
}

static inline bool uart_rx_full(CalypsoUARTState *s)
{
    return s->rx_count >= CALYPSO_UART_FIFO_SIZE;
}

static void uart_rx_push(CalypsoUARTState *s, uint8_t byte)
{
    if (uart_rx_full(s)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "calypso-uart-%s: RX FIFO overrun (dropped 0x%02x)\n",
                      s->name ? s->name : "?", byte);
        return;
    }
    s->rx_fifo[s->rx_head] = byte;
    s->rx_head = (s->rx_head + 1) % CALYPSO_UART_FIFO_SIZE;
    s->rx_count++;
}

static uint8_t uart_rx_pop(CalypsoUARTState *s)
{
    uint8_t byte;
    if (uart_rx_empty(s)) {
        return 0x00;
    }
    byte = s->rx_fifo[s->rx_tail];
    s->rx_tail = (s->rx_tail + 1) % CALYPSO_UART_FIFO_SIZE;
    s->rx_count--;
    return byte;
}

static void uart_rx_reset(CalypsoUARTState *s)
{
    s->rx_head = 0;
    s->rx_tail = 0;
    s->rx_count = 0;
}

/* ================================================================
 * IRQ management (level-sensitive, 16550-style priorities)
 *
 * Priority order (highest first):
 *   1. RX Line Status (IIR=0x06) — not implemented yet
 *   2. RX Data Available (IIR=0x04) — FIFO non-empty + IER.RDI
 *   3. THR Empty (IIR=0x02) — IER.THRI + one-shot after TX/IIR read
 *   4. Modem Status (IIR=0x00) — not implemented yet
 * ================================================================ */

static void calypso_uart_update_irq(CalypsoUARTState *s)
{
    bool should_raise = false;

    /* RX data available */
    if ((s->ier & UART_IER_RDI) && !uart_rx_empty(s)) {
        should_raise = true;
    }
    /* THR empty (one-shot: cleared on IIR read when THRI is source) */
    if ((s->ier & UART_IER_THRI) && s->thri_pending) {
        should_raise = true;
    }

    if (should_raise && !s->irq_raised) {
        qemu_irq_raise(s->irq);
        s->irq_raised = true;
    } else if (!should_raise && s->irq_raised) {
        qemu_irq_lower(s->irq);
        s->irq_raised = false;
    }
}

/*
 * FIX #2: Compute IIR dynamically
 *
 * Returns the highest-priority pending interrupt identification.
 * Reading IIR clears the THRI condition (standard 16550 behavior).
 */
static uint8_t calypso_uart_get_iir(CalypsoUARTState *s)
{
    uint8_t iir = UART_IIR_NO_INT | UART_IIR_FIFO_EN;

    /* Priority 1: RX data available */
    if ((s->ier & UART_IER_RDI) && !uart_rx_empty(s)) {
        iir = UART_IIR_RDI | UART_IIR_FIFO_EN;
        return iir;
    }
    /* Priority 2: THR empty (one-shot) */
    if ((s->ier & UART_IER_THRI) && s->thri_pending) {
        iir = UART_IIR_THRI | UART_IIR_FIFO_EN;
        /* Reading IIR when THRI is the source clears THRI pending */
        s->thri_pending = false;
        calypso_uart_update_irq(s);
        return iir;
    }

    return iir;  /* No interrupt pending (bit 0 = 1) */
}

/* ================================================================
 * CharBackend callbacks
 * ================================================================ */

static void calypso_uart_rx_callback(void *opaque, const uint8_t *buf, int size)
{
    CalypsoUARTState *s = CALYPSO_UART(opaque);

    for (int i = 0; i < size; i++) {
        uart_rx_push(s, buf[i]);
    }
    calypso_uart_update_irq(s);
}

static int calypso_uart_can_receive(void *opaque)
{
    CalypsoUARTState *s = CALYPSO_UART(opaque);
    int avail = CALYPSO_UART_FIFO_SIZE - s->rx_count;
    return avail > 0 ? avail : 0;
}

static void calypso_uart_event(void *opaque, QEMUChrEvent event)
{
    /* Nothing needed */
}

/* ================================================================
 * Register access
 * ================================================================ */

static uint64_t calypso_uart_read(void *opaque, hwaddr offset, unsigned size)
{
    CalypsoUARTState *s = CALYPSO_UART(opaque);
    uint64_t ret = 0;
    bool dlab = (s->lcr & UART_LCR_DLAB);
    bool enhanced = (s->lcr == UART_LCR_ENHANCED);

    switch (offset) {
    case 0x00: /* RHR / DLL */
        if (dlab || enhanced) {
            ret = s->dll;
        } else {
            /*
             * FIX #1: Pop from FIFO instead of returning hardcoded 0x41.
             * This is the critical fix that makes osmocon communication work.
             */
            ret = uart_rx_pop(s);
            calypso_uart_update_irq(s);
            qemu_chr_fe_accept_input(&s->chr);
        }
        break;

    case 0x01: /* IER / DLH */
        if (dlab && !enhanced) {
            ret = s->dlh;
        } else {
            ret = s->ier;
        }
        break;

    case 0x02: /* IIR / EFR */
        if (enhanced) {
            ret = s->efr;
        } else {
            /* FIX #2: Compute IIR dynamically */
            ret = calypso_uart_get_iir(s);
        }
        break;

    case 0x03: /* LCR */
        ret = s->lcr;
        break;

    case 0x04: /* MCR / XON1 */
        ret = enhanced ? s->xon1 : s->mcr;
        break;

    case 0x05: /* LSR / XON2 */
        if (enhanced) {
            ret = s->xon2;
        } else {
            /*
             * FIX #3: LSR DR bit reflects actual FIFO state.
             * TX side is always ready (we transmit instantly).
             */
            ret = UART_LSR_THRE | UART_LSR_TEMT;
            if (!uart_rx_empty(s)) {
                ret |= UART_LSR_DR;
            }
        }
        break;

    case 0x06: /* MSR / XOFF1 */
        ret = enhanced ? s->xoff1 : s->msr;
        break;

    case 0x07: /* SPR / XOFF2 */
        ret = enhanced ? s->xoff2 : s->scr;
        break;

    case 0x08: /* MDR1 */
        ret = s->mdr1;
        break;

    case 0x10: /* SCR - Supplementary Control Register */
        ret = 0x00;
        break;

    case 0x11: /* SSR - Supplementary Status Register */
        ret = 0x00; /* TX FIFO not full */
        break;

    case 0x12: /* ACREG */
        ret = 0x00;
        break;

    case 0x18: /* TXFLL */
        ret = 0x00;
        break;

    case 0x19: /* TXFLH */
        ret = 0x00;
        break;

    case 0x1A: /* RXFLL */
        ret = s->rx_count & 0xFF;
        break;

    case 0x1B: /* RXFLH */
        ret = 0x00;
        break;

    case 0x80: /* DLL alias (Calypso-specific) */
        ret = s->dll;
        break;

    case 0x81: /* DLH alias (Calypso-specific) */
        ret = s->dlh;
        break;

    default:
        ret = 0;
        break;
    }

    return ret;
}

static void calypso_uart_write(void *opaque, hwaddr offset, uint64_t value,
                                unsigned size)
{
    CalypsoUARTState *s = CALYPSO_UART(opaque);
    uint8_t val = value & 0xFF;
    bool dlab = (s->lcr & UART_LCR_DLAB);
    bool enhanced = (s->lcr == UART_LCR_ENHANCED);

    switch (offset) {
    case 0x00: /* THR / DLL */
        if (dlab || enhanced) {
            s->dll = val;
        } else {
            /* Transmit byte via chardev */
            qemu_chr_fe_write_all(&s->chr, &val, 1);
            /*
             * After TX, raise THRI one-shot so firmware gets a
             * THR-empty interrupt on next IRQ evaluation.
             */
            s->thri_pending = true;
            calypso_uart_update_irq(s);
        }
        break;

    case 0x01: /* IER / DLH */
        if (dlab && !enhanced) {
            s->dlh = val;
        } else {
            uint8_t old_ier = s->ier;
            s->ier = val & 0x0F;
            /*
             * 16550 behavior: when THRI is enabled while THR is empty,
             * immediately assert THRI (one-shot).
             */
            if (!(old_ier & UART_IER_THRI) && (s->ier & UART_IER_THRI)) {
                s->thri_pending = true;
            }
            calypso_uart_update_irq(s);
        }
        break;

    case 0x02: /* FCR / EFR */
        if (enhanced) {
            s->efr = val;
        } else {
            s->fcr = val;
            if (val & 0x02) { /* Reset RX FIFO */
                uart_rx_reset(s);
                calypso_uart_update_irq(s);
            }
            /* Bit 2: reset TX FIFO (no-op) */
        }
        break;

    case 0x03: /* LCR */
        s->lcr = val;
        break;

    case 0x04: /* MCR / XON1 */
        if (enhanced) {
            s->xon1 = val;
        } else {
            s->mcr = val;
        }
        break;

    case 0x05: /* XON2 (enhanced only; LSR is read-only) */
        if (enhanced) {
            s->xon2 = val;
        }
        break;

    case 0x06: /* XOFF1 (enhanced) */
        if (enhanced) {
            s->xoff1 = val;
        }
        break;

    case 0x07: /* SPR / XOFF2 */
        if (enhanced) {
            s->xoff2 = val;
        } else {
            s->scr = val;
        }
        break;

    case 0x08: /* MDR1 */
        s->mdr1 = val;
        break;

    case 0x10: /* SCR */
    case 0x11: /* SSR — read-only */
    case 0x12: /* ACREG */
        break;

    case 0x80: /* DLL alias */
        s->dll = val;
        break;

    case 0x81: /* DLH alias */
        s->dlh = val;
        break;

    default:
        qemu_log_mask(LOG_UNIMP, "calypso-uart-%s: unhandled write 0x%02x "
                      "← 0x%02x\n", s->name ? s->name : "?",
                      (unsigned)offset, val);
        break;
    }
}

static const MemoryRegionOps calypso_uart_ops = {
    .read = calypso_uart_read,
    .write = calypso_uart_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = { .min_access_size = 1, .max_access_size = 4 },
};

/* ================================================================
 * QOM lifecycle
 * ================================================================ */

static void calypso_uart_realize(DeviceState *dev, Error **errp)
{
    CalypsoUARTState *s = CALYPSO_UART(dev);

    memory_region_init_io(&s->iomem, OBJECT(dev), &calypso_uart_ops, s,
                          "calypso-uart", 0x100);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq);

    /* Connect CharBackend handlers if a chardev was attached */
    if (qemu_chr_fe_backend_connected(&s->chr)) {
        qemu_chr_fe_set_handlers(&s->chr,
                                 calypso_uart_can_receive,
                                 calypso_uart_rx_callback,
                                 calypso_uart_event,
                                 NULL, s, NULL, true);
    }
}

static void calypso_uart_reset(DeviceState *dev)
{
    CalypsoUARTState *s = CALYPSO_UART(dev);

    uart_rx_reset(s);
    s->ier = 0;
    s->lcr = 0;
    s->mcr = 0;
    s->msr = 0;
    s->scr = 0;
    s->mdr1 = 0;
    s->dll = 0;
    s->dlh = 0;
    s->efr = 0;
    s->fcr = 0;
    s->xon1 = s->xon2 = 0;
    s->xoff1 = s->xoff2 = 0;
    s->thri_pending = false;
    s->irq_raised = false;
}

static Property calypso_uart_properties[] = {
    DEFINE_PROP_CHR("chardev", CalypsoUARTState, chr),
    DEFINE_PROP_STRING("label", CalypsoUARTState, name),
    DEFINE_PROP_END_OF_LIST(),
};

static void calypso_uart_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = calypso_uart_realize;
    device_class_set_legacy_reset(dc, calypso_uart_reset);
    dc->desc = "Calypso UART with 64-byte FIFO";
    device_class_set_props(dc, calypso_uart_properties);
}

static const TypeInfo calypso_uart_info = {
    .name          = TYPE_CALYPSO_UART,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(CalypsoUARTState),
    .class_init    = calypso_uart_class_init,
};

static void calypso_uart_register_types(void)
{
    type_register_static(&calypso_uart_info);
}

type_init(calypso_uart_register_types)
