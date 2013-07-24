/*
 * IMX31 UARTS
 *
 * Copyright (c) 2008 OKL
 * Originally Written by Hans Jiang
 * Copyright (c) 2011 NICTA Pty Ltd.
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

#include "hw/hw.h"
#include "hw/sysbus.h"
#include "sysemu/sysemu.h"
#include "sysemu/char.h"
#include "hw/arm/imx.h"

//#define DEBUG_SERIAL 1
#ifdef DEBUG_SERIAL
#define DPRINTF(fmt, args...) \
do { printf("imx_serial: " fmt , ##args); } while (0)
#else
#define DPRINTF(fmt, args...) do {} while (0)
#endif

/*
 * Define to 1 for messages about attempts to
 * access unimplemented registers or similar.
 */
//#define DEBUG_IMPLEMENTATION 1
#ifdef DEBUG_IMPLEMENTATION
#  define IPRINTF(fmt, args...) \
    do  { fprintf(stderr, "imx_serial: " fmt, ##args); } while (0)
#else
#  define IPRINTF(fmt, args...) do {} while (0)
#endif

#define TYPE_IMX_SERIAL "imx-serial"
#define IMX_SERIAL(obj) OBJECT_CHECK(IMXSerialState, (obj), TYPE_IMX_SERIAL)

typedef struct IMXSerialState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    int32_t readbuff;

    uint32_t usr1;
    uint32_t usr2;
    uint32_t ucr1;
    uint32_t ucr2;
    uint32_t uts1;

    /*
     * The registers below are implemented just so that the
     * guest OS sees what it has written
     */
    uint32_t onems;
    uint32_t ufcr;
    uint32_t ubmr;
    uint32_t ubrc;
    uint32_t ucr3;

    qemu_irq irq;
    CharDriverState *chr;
} IMXSerialState;

static const VMStateDescription vmstate_imx_serial = {
    .name = "imx-serial",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
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
        VMSTATE_END_OF_LIST()
    },
};


#define URXD_CHARRDY    (1<<15)   /* character read is valid */
#define URXD_ERR        (1<<14)   /* Character has error */
#define URXD_BRK        (1<<11)   /* Break received */

#define USR1_PARTYER    (1<<15)   /* Parity Error */
#define USR1_RTSS       (1<<14)   /* RTS pin status */
#define USR1_TRDY       (1<<13)   /* Tx ready */
#define USR1_RTSD       (1<<12)   /* RTS delta: pin changed state */
#define USR1_ESCF       (1<<11)   /* Escape sequence interrupt */
#define USR1_FRAMERR    (1<<10)   /* Framing error  */
#define USR1_RRDY       (1<<9)    /* receiver ready */
#define USR1_AGTIM      (1<<8)    /* Aging timer interrupt */
#define USR1_DTRD       (1<<7)    /* DTR changed */
#define USR1_RXDS       (1<<6)    /* Receiver is idle */
#define USR1_AIRINT     (1<<5)    /* Aysnch IR interrupt */
#define USR1_AWAKE      (1<<4)    /* Falling edge detected on RXd pin */

#define USR2_ADET       (1<<15)   /* Autobaud complete */
#define USR2_TXFE       (1<<14)   /* Transmit FIFO empty */
#define USR2_DTRF       (1<<13)   /* DTR/DSR transition */
#define USR2_IDLE       (1<<12)   /* UART has been idle for too long */
#define USR2_ACST       (1<<11)   /* Autobaud counter stopped */
#define USR2_RIDELT     (1<<10)   /* Ring Indicator delta */
#define USR2_RIIN       (1<<9)    /* Ring Indicator Input */
#define USR2_IRINT      (1<<8)    /* Serial Infrared Interrupt */
#define USR2_WAKE       (1<<7)    /* Start bit detected */
#define USR2_DCDDELT    (1<<6)    /* Data Carrier Detect delta */
#define USR2_DCDIN      (1<<5)    /* Data Carrier Detect Input */
#define USR2_RTSF       (1<<4)    /* RTS transition */
#define USR2_TXDC       (1<<3)    /* Transmission complete */
#define USR2_BRCD       (1<<2)    /* Break condition detected */
#define USR2_ORE        (1<<1)    /* Overrun error */
#define USR2_RDR        (1<<0)    /* Receive data ready */

#define UCR1_TRDYEN     (1<<13)   /* Tx Ready Interrupt Enable */
#define UCR1_RRDYEN     (1<<9)    /* Rx Ready Interrupt Enable */
#define UCR1_TXMPTYEN   (1<<6)    /* Tx Empty Interrupt Enable */
#define UCR1_UARTEN     (1<<0)    /* UART Enable */

#define UCR2_TXEN       (1<<2)    /* Transmitter enable */
#define UCR2_RXEN       (1<<1)    /* Receiver enable */
#define UCR2_SRST       (1<<0)    /* Reset complete */

#define UTS1_TXEMPTY    (1<<6)
#define UTS1_RXEMPTY    (1<<5)
#define UTS1_TXFULL     (1<<4)
#define UTS1_RXFULL     (1<<3)

static void imx_update(IMXSerialState *s)
{
    uint32_t flags;

    flags = (s->usr1 & s->ucr1) & (USR1_TRDY|USR1_RRDY);
    if (!(s->ucr1 & UCR1_TXMPTYEN)) {
        flags &= ~USR1_TRDY;
    }

    qemu_set_irq(s->irq, !!flags);
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

    DPRINTF("read(offset=%x)\n", offset >> 2);
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
            qemu_chr_accept_input(s->chr);
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
    case 0x29: /* BRM Incremental */
        return 0x0; /* TODO */

    default:
        IPRINTF("imx_serial_read: bad offset: 0x%x\n", (int)offset);
        return 0;
    }
}

static void imx_serial_write(void *opaque, hwaddr offset,
                      uint64_t value, unsigned size)
{
    IMXSerialState *s = (IMXSerialState *)opaque;
    unsigned char ch;

    DPRINTF("write(offset=%x, value = %x) to %s\n",
            offset >> 2,
            (unsigned int)value, s->chr ? s->chr->label : "NODEV");

    switch (offset >> 2) {
    case 0x10: /* UTXD */
        ch = value;
        if (s->ucr2 & UCR2_TXEN) {
            if (s->chr) {
                qemu_chr_fe_write(s->chr, &ch, 1);
            }
            s->usr1 &= ~USR1_TRDY;
            imx_update(s);
            s->usr1 |= USR1_TRDY;
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
                qemu_chr_accept_input(s->chr);
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

    case 0x2d: /* UTS1 */
    case 0x23: /* UCR4 */
        IPRINTF("Unimplemented Register %x written to\n", offset >> 2);
        /* TODO */
        break;

    default:
        IPRINTF("imx_serial_write: Bad offset 0x%x\n", (int)offset);
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
    imx_update(s);
}

static void imx_receive(void *opaque, const uint8_t *buf, int size)
{
    imx_put_data(opaque, *buf);
}

static void imx_event(void *opaque, int event)
{
    if (event == CHR_EVENT_BREAK) {
        imx_put_data(opaque, URXD_BRK);
    }
}


static const struct MemoryRegionOps imx_serial_ops = {
    .read = imx_serial_read,
    .write = imx_serial_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static int imx_serial_init(SysBusDevice *dev)
{
    IMXSerialState *s = IMX_SERIAL(dev);


    memory_region_init_io(&s->iomem, OBJECT(s), &imx_serial_ops, s,
                          "imx-serial", 0x1000);
    sysbus_init_mmio(dev, &s->iomem);
    sysbus_init_irq(dev, &s->irq);

    if (s->chr) {
        qemu_chr_add_handlers(s->chr, imx_can_receive, imx_receive,
                              imx_event, s);
    } else {
        DPRINTF("No char dev for uart at 0x%lx\n",
                (unsigned long)s->iomem.ram_addr);
    }

    return 0;
}

void imx_serial_create(int uart, const hwaddr addr, qemu_irq irq)
{
    DeviceState *dev;
    SysBusDevice *bus;
    CharDriverState *chr;
    const char chr_name[] = "serial";
    char label[ARRAY_SIZE(chr_name) + 1];

    dev = qdev_create(NULL, TYPE_IMX_SERIAL);

    if (uart >= MAX_SERIAL_PORTS) {
        hw_error("Cannot assign uart %d: QEMU supports only %d ports\n",
                 uart, MAX_SERIAL_PORTS);
    }
    chr = serial_hds[uart];
    if (!chr) {
        snprintf(label, ARRAY_SIZE(label), "%s%d", chr_name, uart);
        chr = qemu_chr_new(label, "null", NULL);
        if (!(chr)) {
            hw_error("Can't assign serial port to imx-uart%d.\n", uart);
        }
    }

    qdev_prop_set_chr(dev, "chardev", chr);
    bus = SYS_BUS_DEVICE(dev);
    qdev_init_nofail(dev);
    if (addr != (hwaddr)-1) {
        sysbus_mmio_map(bus, 0, addr);
    }
    sysbus_connect_irq(bus, 0, irq);

}


static Property imx32_serial_properties[] = {
    DEFINE_PROP_CHR("chardev", IMXSerialState, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void imx_serial_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = imx_serial_init;
    dc->vmsd = &vmstate_imx_serial;
    dc->reset = imx_serial_reset_at_boot;
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
    dc->desc = "i.MX series UART";
    dc->props = imx32_serial_properties;
}

static const TypeInfo imx_serial_info = {
    .name = TYPE_IMX_SERIAL,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IMXSerialState),
    .class_init = imx_serial_class_init,
};

static void imx_serial_register_types(void)
{
    type_register_static(&imx_serial_info);
}

type_init(imx_serial_register_types)
