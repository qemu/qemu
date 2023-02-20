/*
 * Arm PrimeCell PL011 UART
 *
 * Copyright (c) 2006 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

/*
 * QEMU interface:
 *  + sysbus MMIO region 0: device registers
 *  + sysbus IRQ 0: UARTINTR (combined interrupt line)
 *  + sysbus IRQ 1: UARTRXINTR (receive FIFO interrupt line)
 *  + sysbus IRQ 2: UARTTXINTR (transmit FIFO interrupt line)
 *  + sysbus IRQ 3: UARTRTINTR (receive timeout interrupt line)
 *  + sysbus IRQ 4: UARTMSINTR (momem status interrupt line)
 *  + sysbus IRQ 5: UARTEINTR (error interrupt line)
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/char/pl011.h"
#include "hw/irq.h"
#include "hw/sysbus.h"
#include "hw/qdev-clock.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "migration/vmstate.h"
#include "chardev/char-fe.h"
#include "chardev/char-serial.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "trace.h"

DeviceState *pl011_create(hwaddr addr, qemu_irq irq, Chardev *chr)
{
    DeviceState *dev;
    SysBusDevice *s;

    dev = qdev_new("pl011");
    s = SYS_BUS_DEVICE(dev);
    qdev_prop_set_chr(dev, "chardev", chr);
    sysbus_realize_and_unref(s, &error_fatal);
    sysbus_mmio_map(s, 0, addr);
    sysbus_connect_irq(s, 0, irq);

    return dev;
}

#define PL011_INT_TX 0x20
#define PL011_INT_RX 0x10

#define PL011_FLAG_TXFE 0x80
#define PL011_FLAG_RXFF 0x40
#define PL011_FLAG_TXFF 0x20
#define PL011_FLAG_RXFE 0x10

/* Interrupt status bits in UARTRIS, UARTMIS, UARTIMSC */
#define INT_OE (1 << 10)
#define INT_BE (1 << 9)
#define INT_PE (1 << 8)
#define INT_FE (1 << 7)
#define INT_RT (1 << 6)
#define INT_TX (1 << 5)
#define INT_RX (1 << 4)
#define INT_DSR (1 << 3)
#define INT_DCD (1 << 2)
#define INT_CTS (1 << 1)
#define INT_RI (1 << 0)
#define INT_E (INT_OE | INT_BE | INT_PE | INT_FE)
#define INT_MS (INT_RI | INT_DSR | INT_DCD | INT_CTS)

static const unsigned char pl011_id_arm[8] =
  { 0x11, 0x10, 0x14, 0x00, 0x0d, 0xf0, 0x05, 0xb1 };
static const unsigned char pl011_id_luminary[8] =
  { 0x11, 0x00, 0x18, 0x01, 0x0d, 0xf0, 0x05, 0xb1 };

/* Which bits in the interrupt status matter for each outbound IRQ line ? */
static const uint32_t irqmask[] = {
    INT_E | INT_MS | INT_RT | INT_TX | INT_RX, /* combined IRQ */
    INT_RX,
    INT_TX,
    INT_RT,
    INT_MS,
    INT_E,
};

static void pl011_update(PL011State *s)
{
    uint32_t flags;
    int i;

    flags = s->int_level & s->int_enabled;
    trace_pl011_irq_state(flags != 0);
    for (i = 0; i < ARRAY_SIZE(s->irq); i++) {
        qemu_set_irq(s->irq[i], (flags & irqmask[i]) != 0);
    }
}

static bool pl011_is_fifo_enabled(PL011State *s)
{
    return (s->lcr & 0x10) != 0;
}

static inline unsigned pl011_get_fifo_depth(PL011State *s)
{
    /* Note: FIFO depth is expected to be power-of-2 */
    return pl011_is_fifo_enabled(s) ? PL011_FIFO_DEPTH : 1;
}

static inline void pl011_reset_fifo(PL011State *s)
{
    s->read_count = 0;
    s->read_pos = 0;

    /* Reset FIFO flags */
    s->flags &= ~(PL011_FLAG_RXFF | PL011_FLAG_TXFF);
    s->flags |= PL011_FLAG_RXFE | PL011_FLAG_TXFE;
}

static uint64_t pl011_read(void *opaque, hwaddr offset,
                           unsigned size)
{
    PL011State *s = (PL011State *)opaque;
    uint32_t c;
    uint64_t r;

    switch (offset >> 2) {
    case 0: /* UARTDR */
        s->flags &= ~PL011_FLAG_RXFF;
        c = s->read_fifo[s->read_pos];
        if (s->read_count > 0) {
            s->read_count--;
            s->read_pos = (s->read_pos + 1) & (pl011_get_fifo_depth(s) - 1);
        }
        if (s->read_count == 0) {
            s->flags |= PL011_FLAG_RXFE;
        }
        if (s->read_count == s->read_trigger - 1)
            s->int_level &= ~ PL011_INT_RX;
        trace_pl011_read_fifo(s->read_count);
        s->rsr = c >> 8;
        pl011_update(s);
        qemu_chr_fe_accept_input(&s->chr);
        r = c;
        break;
    case 1: /* UARTRSR */
        r = s->rsr;
        break;
    case 6: /* UARTFR */
        r = s->flags;
        break;
    case 8: /* UARTILPR */
        r = s->ilpr;
        break;
    case 9: /* UARTIBRD */
        r = s->ibrd;
        break;
    case 10: /* UARTFBRD */
        r = s->fbrd;
        break;
    case 11: /* UARTLCR_H */
        r = s->lcr;
        break;
    case 12: /* UARTCR */
        r = s->cr;
        break;
    case 13: /* UARTIFLS */
        r = s->ifl;
        break;
    case 14: /* UARTIMSC */
        r = s->int_enabled;
        break;
    case 15: /* UARTRIS */
        r = s->int_level;
        break;
    case 16: /* UARTMIS */
        r = s->int_level & s->int_enabled;
        break;
    case 18: /* UARTDMACR */
        r = s->dmacr;
        break;
    case 0x3f8 ... 0x400:
        r = s->id[(offset - 0xfe0) >> 2];
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "pl011_read: Bad offset 0x%x\n", (int)offset);
        r = 0;
        break;
    }

    trace_pl011_read(offset, r);
    return r;
}

static void pl011_set_read_trigger(PL011State *s)
{
#if 0
    /* The docs say the RX interrupt is triggered when the FIFO exceeds
       the threshold.  However linux only reads the FIFO in response to an
       interrupt.  Triggering the interrupt when the FIFO is non-empty seems
       to make things work.  */
    if (s->lcr & 0x10)
        s->read_trigger = (s->ifl >> 1) & 0x1c;
    else
#endif
        s->read_trigger = 1;
}

static unsigned int pl011_get_baudrate(const PL011State *s)
{
    uint64_t clk;

    if (s->ibrd == 0) {
        return 0;
    }

    clk = clock_get_hz(s->clk);
    return (clk / ((s->ibrd << 6) + s->fbrd)) << 2;
}

static void pl011_trace_baudrate_change(const PL011State *s)
{
    trace_pl011_baudrate_change(pl011_get_baudrate(s),
                                clock_get_hz(s->clk),
                                s->ibrd, s->fbrd);
}

static void pl011_write(void *opaque, hwaddr offset,
                        uint64_t value, unsigned size)
{
    PL011State *s = (PL011State *)opaque;
    unsigned char ch;

    trace_pl011_write(offset, value);

    switch (offset >> 2) {
    case 0: /* UARTDR */
        /* ??? Check if transmitter is enabled.  */
        ch = value;
        /* XXX this blocks entire thread. Rewrite to use
         * qemu_chr_fe_write and background I/O callbacks */
        qemu_chr_fe_write_all(&s->chr, &ch, 1);
        s->int_level |= PL011_INT_TX;
        pl011_update(s);
        break;
    case 1: /* UARTRSR/UARTECR */
        s->rsr = 0;
        break;
    case 6: /* UARTFR */
        /* Writes to Flag register are ignored.  */
        break;
    case 8: /* UARTUARTILPR */
        s->ilpr = value;
        break;
    case 9: /* UARTIBRD */
        s->ibrd = value;
        pl011_trace_baudrate_change(s);
        break;
    case 10: /* UARTFBRD */
        s->fbrd = value;
        pl011_trace_baudrate_change(s);
        break;
    case 11: /* UARTLCR_H */
        /* Reset the FIFO state on FIFO enable or disable */
        if ((s->lcr ^ value) & 0x10) {
            pl011_reset_fifo(s);
        }
        if ((s->lcr ^ value) & 0x1) {
            int break_enable = value & 0x1;
            qemu_chr_fe_ioctl(&s->chr, CHR_IOCTL_SERIAL_SET_BREAK,
                              &break_enable);
        }
        s->lcr = value;
        pl011_set_read_trigger(s);
        break;
    case 12: /* UARTCR */
        /* ??? Need to implement the enable and loopback bits.  */
        s->cr = value;
        break;
    case 13: /* UARTIFS */
        s->ifl = value;
        pl011_set_read_trigger(s);
        break;
    case 14: /* UARTIMSC */
        s->int_enabled = value;
        pl011_update(s);
        break;
    case 17: /* UARTICR */
        s->int_level &= ~value;
        pl011_update(s);
        break;
    case 18: /* UARTDMACR */
        s->dmacr = value;
        if (value & 3) {
            qemu_log_mask(LOG_UNIMP, "pl011: DMA not implemented\n");
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "pl011_write: Bad offset 0x%x\n", (int)offset);
    }
}

static int pl011_can_receive(void *opaque)
{
    PL011State *s = (PL011State *)opaque;
    int r;

    r = s->read_count < pl011_get_fifo_depth(s);
    trace_pl011_can_receive(s->lcr, s->read_count, r);
    return r;
}

static void pl011_put_fifo(void *opaque, uint32_t value)
{
    PL011State *s = (PL011State *)opaque;
    int slot;
    unsigned pipe_depth;

    pipe_depth = pl011_get_fifo_depth(s);
    slot = (s->read_pos + s->read_count) & (pipe_depth - 1);
    s->read_fifo[slot] = value;
    s->read_count++;
    s->flags &= ~PL011_FLAG_RXFE;
    trace_pl011_put_fifo(value, s->read_count);
    if (s->read_count == pipe_depth) {
        trace_pl011_put_fifo_full();
        s->flags |= PL011_FLAG_RXFF;
    }
    if (s->read_count == s->read_trigger) {
        s->int_level |= PL011_INT_RX;
        pl011_update(s);
    }
}

static void pl011_receive(void *opaque, const uint8_t *buf, int size)
{
    pl011_put_fifo(opaque, *buf);
}

static void pl011_event(void *opaque, QEMUChrEvent event)
{
    if (event == CHR_EVENT_BREAK)
        pl011_put_fifo(opaque, 0x400);
}

static void pl011_clock_update(void *opaque, ClockEvent event)
{
    PL011State *s = PL011(opaque);

    pl011_trace_baudrate_change(s);
}

static const MemoryRegionOps pl011_ops = {
    .read = pl011_read,
    .write = pl011_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static bool pl011_clock_needed(void *opaque)
{
    PL011State *s = PL011(opaque);

    return s->migrate_clk;
}

static const VMStateDescription vmstate_pl011_clock = {
    .name = "pl011/clock",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = pl011_clock_needed,
    .fields = (VMStateField[]) {
        VMSTATE_CLOCK(clk, PL011State),
        VMSTATE_END_OF_LIST()
    }
};

static int pl011_post_load(void *opaque, int version_id)
{
    PL011State* s = opaque;

    /* Sanity-check input state */
    if (s->read_pos >= ARRAY_SIZE(s->read_fifo) ||
        s->read_count > ARRAY_SIZE(s->read_fifo)) {
        return -1;
    }

    if (!pl011_is_fifo_enabled(s) && s->read_count > 0 && s->read_pos > 0) {
        /*
         * Older versions of PL011 didn't ensure that the single
         * character in the FIFO in FIFO-disabled mode is in
         * element 0 of the array; convert to follow the current
         * code's assumptions.
         */
        s->read_fifo[0] = s->read_fifo[s->read_pos];
        s->read_pos = 0;
    }

    return 0;
}

static const VMStateDescription vmstate_pl011 = {
    .name = "pl011",
    .version_id = 2,
    .minimum_version_id = 2,
    .post_load = pl011_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(readbuff, PL011State),
        VMSTATE_UINT32(flags, PL011State),
        VMSTATE_UINT32(lcr, PL011State),
        VMSTATE_UINT32(rsr, PL011State),
        VMSTATE_UINT32(cr, PL011State),
        VMSTATE_UINT32(dmacr, PL011State),
        VMSTATE_UINT32(int_enabled, PL011State),
        VMSTATE_UINT32(int_level, PL011State),
        VMSTATE_UINT32_ARRAY(read_fifo, PL011State, PL011_FIFO_DEPTH),
        VMSTATE_UINT32(ilpr, PL011State),
        VMSTATE_UINT32(ibrd, PL011State),
        VMSTATE_UINT32(fbrd, PL011State),
        VMSTATE_UINT32(ifl, PL011State),
        VMSTATE_INT32(read_pos, PL011State),
        VMSTATE_INT32(read_count, PL011State),
        VMSTATE_INT32(read_trigger, PL011State),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * []) {
        &vmstate_pl011_clock,
        NULL
    }
};

static Property pl011_properties[] = {
    DEFINE_PROP_CHR("chardev", PL011State, chr),
    DEFINE_PROP_BOOL("migrate-clk", PL011State, migrate_clk, true),
    DEFINE_PROP_END_OF_LIST(),
};

static void pl011_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    PL011State *s = PL011(obj);
    int i;

    memory_region_init_io(&s->iomem, OBJECT(s), &pl011_ops, s, "pl011", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    for (i = 0; i < ARRAY_SIZE(s->irq); i++) {
        sysbus_init_irq(sbd, &s->irq[i]);
    }

    s->clk = qdev_init_clock_in(DEVICE(obj), "clk", pl011_clock_update, s,
                                ClockUpdate);

    s->id = pl011_id_arm;
}

static void pl011_realize(DeviceState *dev, Error **errp)
{
    PL011State *s = PL011(dev);

    qemu_chr_fe_set_handlers(&s->chr, pl011_can_receive, pl011_receive,
                             pl011_event, NULL, s, NULL, true);
}

static void pl011_reset(DeviceState *dev)
{
    PL011State *s = PL011(dev);

    s->lcr = 0;
    s->rsr = 0;
    s->dmacr = 0;
    s->int_enabled = 0;
    s->int_level = 0;
    s->ilpr = 0;
    s->ibrd = 0;
    s->fbrd = 0;
    s->read_trigger = 1;
    s->ifl = 0x12;
    s->cr = 0x300;
    s->flags = 0;
    pl011_reset_fifo(s);
}

static void pl011_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = pl011_realize;
    dc->reset = pl011_reset;
    dc->vmsd = &vmstate_pl011;
    device_class_set_props(dc, pl011_properties);
}

static const TypeInfo pl011_arm_info = {
    .name          = TYPE_PL011,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PL011State),
    .instance_init = pl011_init,
    .class_init    = pl011_class_init,
};

static void pl011_luminary_init(Object *obj)
{
    PL011State *s = PL011(obj);

    s->id = pl011_id_luminary;
}

static const TypeInfo pl011_luminary_info = {
    .name          = TYPE_PL011_LUMINARY,
    .parent        = TYPE_PL011,
    .instance_init = pl011_luminary_init,
};

static void pl011_register_types(void)
{
    type_register_static(&pl011_arm_info);
    type_register_static(&pl011_luminary_info);
}

type_init(pl011_register_types)
