/*
 * Arm PrimeCell PL011 UART
 *
 * Copyright (c) 2006 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "sysemu/char.h"

#define TYPE_PL011 "pl011"
#define PL011(obj) OBJECT_CHECK(PL011State, (obj), TYPE_PL011)

typedef struct PL011State {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t readbuff;
    uint32_t flags;
    uint32_t lcr;
    uint32_t rsr;
    uint32_t cr;
    uint32_t dmacr;
    uint32_t int_enabled;
    uint32_t int_level;
    uint32_t read_fifo[16];
    uint32_t ilpr;
    uint32_t ibrd;
    uint32_t fbrd;
    uint32_t ifl;
    int read_pos;
    int read_count;
    int read_trigger;
    CharDriverState *chr;
    qemu_irq irq;
    const unsigned char *id;
} PL011State;

#define PL011_INT_TX 0x20
#define PL011_INT_RX 0x10

#define PL011_FLAG_TXFE 0x80
#define PL011_FLAG_RXFF 0x40
#define PL011_FLAG_TXFF 0x20
#define PL011_FLAG_RXFE 0x10

static const unsigned char pl011_id_arm[8] =
  { 0x11, 0x10, 0x14, 0x00, 0x0d, 0xf0, 0x05, 0xb1 };
static const unsigned char pl011_id_luminary[8] =
  { 0x11, 0x00, 0x18, 0x01, 0x0d, 0xf0, 0x05, 0xb1 };

static void pl011_update(PL011State *s)
{
    uint32_t flags;

    flags = s->int_level & s->int_enabled;
    qemu_set_irq(s->irq, flags != 0);
}

static uint64_t pl011_read(void *opaque, hwaddr offset,
                           unsigned size)
{
    PL011State *s = (PL011State *)opaque;
    uint32_t c;

    if (offset >= 0xfe0 && offset < 0x1000) {
        return s->id[(offset - 0xfe0) >> 2];
    }
    switch (offset >> 2) {
    case 0: /* UARTDR */
        s->flags &= ~PL011_FLAG_RXFF;
        c = s->read_fifo[s->read_pos];
        if (s->read_count > 0) {
            s->read_count--;
            if (++s->read_pos == 16)
                s->read_pos = 0;
        }
        if (s->read_count == 0) {
            s->flags |= PL011_FLAG_RXFE;
        }
        if (s->read_count == s->read_trigger - 1)
            s->int_level &= ~ PL011_INT_RX;
        s->rsr = c >> 8;
        pl011_update(s);
        if (s->chr) {
            qemu_chr_accept_input(s->chr);
        }
        return c;
    case 1: /* UARTRSR */
        return s->rsr;
    case 6: /* UARTFR */
        return s->flags;
    case 8: /* UARTILPR */
        return s->ilpr;
    case 9: /* UARTIBRD */
        return s->ibrd;
    case 10: /* UARTFBRD */
        return s->fbrd;
    case 11: /* UARTLCR_H */
        return s->lcr;
    case 12: /* UARTCR */
        return s->cr;
    case 13: /* UARTIFLS */
        return s->ifl;
    case 14: /* UARTIMSC */
        return s->int_enabled;
    case 15: /* UARTRIS */
        return s->int_level;
    case 16: /* UARTMIS */
        return s->int_level & s->int_enabled;
    case 18: /* UARTDMACR */
        return s->dmacr;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "pl011_read: Bad offset %x\n", (int)offset);
        return 0;
    }
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

static void pl011_write(void *opaque, hwaddr offset,
                        uint64_t value, unsigned size)
{
    PL011State *s = (PL011State *)opaque;
    unsigned char ch;

    switch (offset >> 2) {
    case 0: /* UARTDR */
        /* ??? Check if transmitter is enabled.  */
        ch = value;
        if (s->chr)
            qemu_chr_fe_write(s->chr, &ch, 1);
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
        break;
    case 10: /* UARTFBRD */
        s->fbrd = value;
        break;
    case 11: /* UARTLCR_H */
        /* Reset the FIFO state on FIFO enable or disable */
        if ((s->lcr ^ value) & 0x10) {
            s->read_count = 0;
            s->read_pos = 0;
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
                      "pl011_write: Bad offset %x\n", (int)offset);
    }
}

static int pl011_can_receive(void *opaque)
{
    PL011State *s = (PL011State *)opaque;

    if (s->lcr & 0x10)
        return s->read_count < 16;
    else
        return s->read_count < 1;
}

static void pl011_put_fifo(void *opaque, uint32_t value)
{
    PL011State *s = (PL011State *)opaque;
    int slot;

    slot = s->read_pos + s->read_count;
    if (slot >= 16)
        slot -= 16;
    s->read_fifo[slot] = value;
    s->read_count++;
    s->flags &= ~PL011_FLAG_RXFE;
    if (!(s->lcr & 0x10) || s->read_count == 16) {
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

static void pl011_event(void *opaque, int event)
{
    if (event == CHR_EVENT_BREAK)
        pl011_put_fifo(opaque, 0x400);
}

static const MemoryRegionOps pl011_ops = {
    .read = pl011_read,
    .write = pl011_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_pl011 = {
    .name = "pl011",
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(readbuff, PL011State),
        VMSTATE_UINT32(flags, PL011State),
        VMSTATE_UINT32(lcr, PL011State),
        VMSTATE_UINT32(rsr, PL011State),
        VMSTATE_UINT32(cr, PL011State),
        VMSTATE_UINT32(dmacr, PL011State),
        VMSTATE_UINT32(int_enabled, PL011State),
        VMSTATE_UINT32(int_level, PL011State),
        VMSTATE_UINT32_ARRAY(read_fifo, PL011State, 16),
        VMSTATE_UINT32(ilpr, PL011State),
        VMSTATE_UINT32(ibrd, PL011State),
        VMSTATE_UINT32(fbrd, PL011State),
        VMSTATE_UINT32(ifl, PL011State),
        VMSTATE_INT32(read_pos, PL011State),
        VMSTATE_INT32(read_count, PL011State),
        VMSTATE_INT32(read_trigger, PL011State),
        VMSTATE_END_OF_LIST()
    }
};

static void pl011_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    PL011State *s = PL011(obj);

    memory_region_init_io(&s->iomem, OBJECT(s), &pl011_ops, s, "pl011", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    s->read_trigger = 1;
    s->ifl = 0x12;
    s->cr = 0x300;
    s->flags = 0x90;

    s->id = pl011_id_arm;
}

static void pl011_realize(DeviceState *dev, Error **errp)
{
    PL011State *s = PL011(dev);

    /* FIXME use a qdev chardev prop instead of qemu_char_get_next_serial() */
    s->chr = qemu_char_get_next_serial();

    if (s->chr) {
        qemu_chr_add_handlers(s->chr, pl011_can_receive, pl011_receive,
                              pl011_event, s);
    }
}

static void pl011_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = pl011_realize;
    dc->vmsd = &vmstate_pl011;
    /* Reason: realize() method uses qemu_char_get_next_serial() */
    dc->cannot_instantiate_with_device_add_yet = true;
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
    .name          = "pl011_luminary",
    .parent        = TYPE_PL011,
    .instance_init = pl011_luminary_init,
};

static void pl011_register_types(void)
{
    type_register_static(&pl011_arm_info);
    type_register_static(&pl011_luminary_info);
}

type_init(pl011_register_types)
