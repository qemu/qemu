/*
 * IMX31 UARTS
 *
 * Copyright (c) 2008 OKL
 * Originally Written by Hans Jiang
 * Copyright (c) 2011 NICTA Pty Ltd.
 * Updated by Jean-Christophe Dubois <jcd@tribudubois.net>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * This is a `bare-bones' implementation of the IMX series serial ports.
 * TODO:
 *  -- implement FIFOs.  The real hardware has 32 word transmit
 *                       and receive FIFOs; we currently use a 1-char buffer
 *  -- implement DMA
 *  -- implement BAUD-rate and modem lines, for when the backend
 *     is a real serial device.
 */

#include "qemu/osdep.h"
#include "hw/char/imx_serial.h"
#include "sysemu/sysemu.h"
#include "qemu/log.h"
#include "qemu/module.h"

#ifndef DEBUG_IMX_UART
#define DEBUG_IMX_UART 0
#endif

#define DPRINTF(fmt, args...) \
    do { \
        if (DEBUG_IMX_UART) { \
            fprintf(stderr, "[%s]%s: " fmt , TYPE_IMX_SERIAL, \
                                             __func__, ##args); \
        } \
    } while (0)

static const VMStateDescription vmstate_imx_serial = {
    .name = TYPE_IMX_SERIAL,
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (VMStateField[]) {
        VMSTATE_INT32(readbuff, IMXSerialState),
        VMSTATE_UINT32(usr1, IMXSerialState),
        VMSTATE_UINT32(usr2, IMXSerialState),
        VMSTATE_UINT32(ucr1, IMXSerialState),
        VMSTATE_UINT32(uts1, IMXSerialState),
        VMSTATE_UINT32(onems, IMXSerialState),
        VMSTATE_UINT32(ufcr, IMXSerialState),
        VMSTATE_UINT32(ubmr, IMXSerialState),
        VMSTATE_UINT32(ubrc, IMXSerialState),
        VMSTATE_UINT32(ucr3, IMXSerialState),
        VMSTATE_UINT32(ucr4, IMXSerialState),
        VMSTATE_END_OF_LIST()
    },
};

static void imx_update(IMXSerialState *s)
{
    uint32_t usr1;
    uint32_t usr2;
    uint32_t mask;

    /*
     * Lucky for us TRDY and RRDY has the same offset in both USR1 and
     * UCR1, so we can get away with something as simple as the
     * following:
     */
    usr1 = s->usr1 & s->ucr1 & (USR1_TRDY | USR1_RRDY);
    /*
     * Bits that we want in USR2 are not as conveniently laid out,
     * unfortunately.
     */
    mask = (s->ucr1 & UCR1_TXMPTYEN) ? USR2_TXFE : 0;
    /*
     * TCEN and TXDC are both bit 3
     * RDR and DREN are both bit 0
     */
    mask |= s->ucr4 & (UCR4_TCEN | UCR4_DREN);

    usr2 = s->usr2 & mask;

    qemu_set_irq(s->irq, usr1 || usr2);
}

static void imx_serial_reset(IMXSerialState *s)
{

    s->usr1 = USR1_TRDY | USR1_RXDS;
    /*
     * Fake attachment of a terminal: assert RTS.
     */
    s->usr1 |= USR1_RTSS;
    s->usr2 = USR2_TXFE | USR2_TXDC | USR2_DCDIN;
    s->uts1 = UTS1_RXEMPTY | UTS1_TXEMPTY;
    s->ucr1 = 0;
    s->ucr2 = UCR2_SRST;
    s->ucr3 = 0x700;
    s->ubmr = 0;
    s->ubrc = 4;
    s->readbuff = URXD_ERR;
}

static void imx_serial_reset_at_boot(DeviceState *dev)
{
    IMXSerialState *s = IMX_SERIAL(dev);

    imx_serial_reset(s);

    /*
     * enable the uart on boot, so messages from the linux decompresser
     * are visible.  On real hardware this is done by the boot rom
     * before anything else is loaded.
     */
    s->ucr1 = UCR1_UARTEN;
    s->ucr2 = UCR2_TXEN;

}

static uint64_t imx_serial_read(void *opaque, hwaddr offset,
                                unsigned size)
{
    IMXSerialState *s = (IMXSerialState *)opaque;
    uint32_t c;

    DPRINTF("read(offset=0x%" HWADDR_PRIx ")\n", offset);

    switch (offset >> 2) {
    case 0x0: /* URXD */
        c = s->readbuff;
        if (!(s->uts1 & UTS1_RXEMPTY)) {
            /* Character is valid */
            c |= URXD_CHARRDY;
            s->usr1 &= ~USR1_RRDY;
            s->usr2 &= ~USR2_RDR;
            s->uts1 |= UTS1_RXEMPTY;
            imx_update(s);
            qemu_chr_fe_accept_input(&s->chr);
        }
        return c;

    case 0x20: /* UCR1 */
        return s->ucr1;

    case 0x21: /* UCR2 */
        return s->ucr2;

    case 0x25: /* USR1 */
        return s->usr1;

    case 0x26: /* USR2 */
        return s->usr2;

    case 0x2A: /* BRM Modulator */
        return s->ubmr;

    case 0x2B: /* Baud Rate Count */
        return s->ubrc;

    case 0x2d: /* Test register */
        return s->uts1;

    case 0x24: /* UFCR */
        return s->ufcr;

    case 0x2c:
        return s->onems;

    case 0x22: /* UCR3 */
        return s->ucr3;

    case 0x23: /* UCR4 */
        return s->ucr4;

    case 0x29: /* BRM Incremental */
        return 0x0; /* TODO */

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad register at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_IMX_SERIAL, __func__, offset);
        return 0;
    }
}

static void imx_serial_write(void *opaque, hwaddr offset,
                             uint64_t value, unsigned size)
{
    IMXSerialState *s = (IMXSerialState *)opaque;
    Chardev *chr = qemu_chr_fe_get_driver(&s->chr);
    unsigned char ch;

    DPRINTF("write(offset=0x%" HWADDR_PRIx ", value = 0x%x) to %s\n",
            offset, (unsigned int)value, chr ? chr->label : "NODEV");

    switch (offset >> 2) {
    case 0x10: /* UTXD */
        ch = value;
        if (s->ucr2 & UCR2_TXEN) {
            /* XXX this blocks entire thread. Rewrite to use
             * qemu_chr_fe_write and background I/O callbacks */
            qemu_chr_fe_write_all(&s->chr, &ch, 1);
            s->usr1 &= ~USR1_TRDY;
            s->usr2 &= ~USR2_TXDC;
            imx_update(s);
            s->usr1 |= USR1_TRDY;
            s->usr2 |= USR2_TXDC;
            imx_update(s);
        }
        break;

    case 0x20: /* UCR1 */
        s->ucr1 = value & 0xffff;

        DPRINTF("write(ucr1=%x)\n", (unsigned int)value);

        imx_update(s);
        break;

    case 0x21: /* UCR2 */
        /*
         * Only a few bits in control register 2 are implemented as yet.
         * If it's intended to use a real serial device as a back-end, this
         * register will have to be implemented more fully.
         */
        if (!(value & UCR2_SRST)) {
            imx_serial_reset(s);
            imx_update(s);
            value |= UCR2_SRST;
        }
        if (value & UCR2_RXEN) {
            if (!(s->ucr2 & UCR2_RXEN)) {
                qemu_chr_fe_accept_input(&s->chr);
            }
        }
        s->ucr2 = value & 0xffff;
        break;

    case 0x25: /* USR1 */
        value &= USR1_AWAKE | USR1_AIRINT | USR1_DTRD | USR1_AGTIM |
                 USR1_FRAMERR | USR1_ESCF | USR1_RTSD | USR1_PARTYER;
        s->usr1 &= ~value;
        break;

    case 0x26: /* USR2 */
        /*
         * Writing 1 to some bits clears them; all other
         * values are ignored
         */
        value &= USR2_ADET | USR2_DTRF | USR2_IDLE | USR2_ACST |
                 USR2_RIDELT | USR2_IRINT | USR2_WAKE |
                 USR2_DCDDELT | USR2_RTSF | USR2_BRCD | USR2_ORE;
        s->usr2 &= ~value;
        break;

    /*
     * Linux expects to see what it writes to these registers
     * We don't currently alter the baud rate
     */
    case 0x29: /* UBIR */
        s->ubrc = value & 0xffff;
        break;

    case 0x2a: /* UBMR */
        s->ubmr = value & 0xffff;
        break;

    case 0x2c: /* One ms reg */
        s->onems = value & 0xffff;
        break;

    case 0x24: /* FIFO control register */
        s->ufcr = value & 0xffff;
        break;

    case 0x22: /* UCR3 */
        s->ucr3 = value & 0xffff;
        break;

    case 0x23: /* UCR4 */
        s->ucr4 = value & 0xffff;
        imx_update(s);
        break;

    case 0x2d: /* UTS1 */
        qemu_log_mask(LOG_UNIMP, "[%s]%s: Unimplemented reg 0x%"
                      HWADDR_PRIx "\n", TYPE_IMX_SERIAL, __func__, offset);
        /* TODO */
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad register at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_IMX_SERIAL, __func__, offset);
    }
}

static int imx_can_receive(void *opaque)
{
    IMXSerialState *s = (IMXSerialState *)opaque;
    return !(s->usr1 & USR1_RRDY);
}

static void imx_put_data(void *opaque, uint32_t value)
{
    IMXSerialState *s = (IMXSerialState *)opaque;

    DPRINTF("received char\n");

    s->usr1 |= USR1_RRDY;
    s->usr2 |= USR2_RDR;
    s->uts1 &= ~UTS1_RXEMPTY;
    s->readbuff = value;
    if (value & URXD_BRK) {
        s->usr2 |= USR2_BRCD;
    }
    imx_update(s);
}

static void imx_receive(void *opaque, const uint8_t *buf, int size)
{
    imx_put_data(opaque, *buf);
}

static void imx_event(void *opaque, int event)
{
    if (event == CHR_EVENT_BREAK) {
        imx_put_data(opaque, URXD_BRK | URXD_FRMERR | URXD_ERR);
    }
}


static const struct MemoryRegionOps imx_serial_ops = {
    .read = imx_serial_read,
    .write = imx_serial_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void imx_serial_realize(DeviceState *dev, Error **errp)
{
    IMXSerialState *s = IMX_SERIAL(dev);

    DPRINTF("char dev for uart: %p\n", qemu_chr_fe_get_driver(&s->chr));

    qemu_chr_fe_set_handlers(&s->chr, imx_can_receive, imx_receive,
                             imx_event, NULL, s, NULL, true);
}

static void imx_serial_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    IMXSerialState *s = IMX_SERIAL(obj);

    memory_region_init_io(&s->iomem, obj, &imx_serial_ops, s,
                          TYPE_IMX_SERIAL, 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static Property imx_serial_properties[] = {
    DEFINE_PROP_CHR("chardev", IMXSerialState, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void imx_serial_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = imx_serial_realize;
    dc->vmsd = &vmstate_imx_serial;
    dc->reset = imx_serial_reset_at_boot;
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
    dc->desc = "i.MX series UART";
    dc->props = imx_serial_properties;
}

static const TypeInfo imx_serial_info = {
    .name           = TYPE_IMX_SERIAL,
    .parent         = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(IMXSerialState),
    .instance_init  = imx_serial_init,
    .class_init     = imx_serial_class_init,
};

static void imx_serial_register_types(void)
{
    type_register_static(&imx_serial_info);
}

type_init(imx_serial_register_types)
