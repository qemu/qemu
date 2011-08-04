/*
 * FTDI FT232BM Device emulation
 *
 * Copyright (c) 2006 CodeSourcery.
 * Copyright (c) 2008 Samuel Thibault <samuel.thibault@ens-lyon.org>
 * Written by Paul Brook, reused for FTDI by Samuel Thibault
 *
 * This code is licensed under the LGPL.
 */

#include "qemu-common.h"
#include "qemu-error.h"
#include "usb.h"
#include "usb-desc.h"
#include "qemu-char.h"

//#define DEBUG_Serial

#ifdef DEBUG_Serial
#define DPRINTF(fmt, ...) \
do { printf("usb-serial: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) do {} while(0)
#endif

#define RECV_BUF 384

/* Commands */
#define FTDI_RESET		0
#define FTDI_SET_MDM_CTRL	1
#define FTDI_SET_FLOW_CTRL	2
#define FTDI_SET_BAUD		3
#define FTDI_SET_DATA		4
#define FTDI_GET_MDM_ST		5
#define FTDI_SET_EVENT_CHR	6
#define FTDI_SET_ERROR_CHR	7
#define FTDI_SET_LATENCY	9
#define FTDI_GET_LATENCY	10

#define DeviceOutVendor	((USB_DIR_OUT|USB_TYPE_VENDOR|USB_RECIP_DEVICE)<<8)
#define DeviceInVendor	((USB_DIR_IN |USB_TYPE_VENDOR|USB_RECIP_DEVICE)<<8)

/* RESET */

#define FTDI_RESET_SIO	0
#define FTDI_RESET_RX	1
#define FTDI_RESET_TX	2

/* SET_MDM_CTRL */

#define FTDI_DTR	1
#define FTDI_SET_DTR	(FTDI_DTR << 8)
#define FTDI_RTS	2
#define FTDI_SET_RTS	(FTDI_RTS << 8)

/* SET_FLOW_CTRL */

#define FTDI_RTS_CTS_HS		1
#define FTDI_DTR_DSR_HS		2
#define FTDI_XON_XOFF_HS	4

/* SET_DATA */

#define FTDI_PARITY	(0x7 << 8)
#define FTDI_ODD	(0x1 << 8)
#define FTDI_EVEN	(0x2 << 8)
#define FTDI_MARK	(0x3 << 8)
#define FTDI_SPACE	(0x4 << 8)

#define FTDI_STOP	(0x3 << 11)
#define FTDI_STOP1	(0x0 << 11)
#define FTDI_STOP15	(0x1 << 11)
#define FTDI_STOP2	(0x2 << 11)

/* GET_MDM_ST */
/* TODO: should be sent every 40ms */
#define FTDI_CTS  (1<<4)        // CTS line status
#define FTDI_DSR  (1<<5)        // DSR line status
#define FTDI_RI   (1<<6)        // RI line status
#define FTDI_RLSD (1<<7)        // Receive Line Signal Detect

/* Status */

#define FTDI_DR   (1<<0)        // Data Ready
#define FTDI_OE   (1<<1)        // Overrun Err
#define FTDI_PE   (1<<2)        // Parity Err
#define FTDI_FE   (1<<3)        // Framing Err
#define FTDI_BI   (1<<4)        // Break Interrupt
#define FTDI_THRE (1<<5)        // Transmitter Holding Register
#define FTDI_TEMT (1<<6)        // Transmitter Empty
#define FTDI_FIFO (1<<7)        // Error in FIFO

typedef struct {
    USBDevice dev;
    uint8_t recv_buf[RECV_BUF];
    uint16_t recv_ptr;
    uint16_t recv_used;
    uint8_t event_chr;
    uint8_t error_chr;
    uint8_t event_trigger;
    QEMUSerialSetParams params;
    int latency;        /* ms */
    CharDriverState *cs;
} USBSerialState;

enum {
    STR_MANUFACTURER = 1,
    STR_PRODUCT_SERIAL,
    STR_PRODUCT_BRAILLE,
    STR_SERIALNUMBER,
};

static const USBDescStrings desc_strings = {
    [STR_MANUFACTURER]    = "QEMU " QEMU_VERSION,
    [STR_PRODUCT_SERIAL]  = "QEMU USB SERIAL",
    [STR_PRODUCT_BRAILLE] = "QEMU USB BRAILLE",
    [STR_SERIALNUMBER]    = "1",
};

static const USBDescIface desc_iface0 = {
    .bInterfaceNumber              = 0,
    .bNumEndpoints                 = 2,
    .bInterfaceClass               = 0xff,
    .bInterfaceSubClass            = 0xff,
    .bInterfaceProtocol            = 0xff,
    .eps = (USBDescEndpoint[]) {
        {
            .bEndpointAddress      = USB_DIR_IN | 0x01,
            .bmAttributes          = USB_ENDPOINT_XFER_BULK,
            .wMaxPacketSize        = 64,
        },{
            .bEndpointAddress      = USB_DIR_OUT | 0x02,
            .bmAttributes          = USB_ENDPOINT_XFER_BULK,
            .wMaxPacketSize        = 64,
        },
    }
};

static const USBDescDevice desc_device = {
    .bcdUSB                        = 0x0200,
    .bMaxPacketSize0               = 8,
    .bNumConfigurations            = 1,
    .confs = (USBDescConfig[]) {
        {
            .bNumInterfaces        = 1,
            .bConfigurationValue   = 1,
            .bmAttributes          = 0x80,
            .bMaxPower             = 50,
            .nif = 1,
            .ifs = &desc_iface0,
        },
    },
};

static const USBDesc desc_serial = {
    .id = {
        .idVendor          = 0x0403,
        .idProduct         = 0x6001,
        .bcdDevice         = 0x0400,
        .iManufacturer     = STR_MANUFACTURER,
        .iProduct          = STR_PRODUCT_SERIAL,
        .iSerialNumber     = STR_SERIALNUMBER,
    },
    .full = &desc_device,
    .str  = desc_strings,
};

static const USBDesc desc_braille = {
    .id = {
        .idVendor          = 0x0403,
        .idProduct         = 0xfe72,
        .bcdDevice         = 0x0400,
        .iManufacturer     = STR_MANUFACTURER,
        .iProduct          = STR_PRODUCT_BRAILLE,
        .iSerialNumber     = STR_SERIALNUMBER,
    },
    .full = &desc_device,
    .str  = desc_strings,
};

static void usb_serial_reset(USBSerialState *s)
{
    /* TODO: Set flow control to none */
    s->event_chr = 0x0d;
    s->event_trigger = 0;
    s->recv_ptr = 0;
    s->recv_used = 0;
    /* TODO: purge in char driver */
}

static void usb_serial_handle_reset(USBDevice *dev)
{
    USBSerialState *s = (USBSerialState *)dev;

    DPRINTF("Reset\n");

    usb_serial_reset(s);
    /* TODO: Reset char device, send BREAK? */
}

static uint8_t usb_get_modem_lines(USBSerialState *s)
{
    int flags;
    uint8_t ret;

    if (qemu_chr_ioctl(s->cs, CHR_IOCTL_SERIAL_GET_TIOCM, &flags) == -ENOTSUP)
        return FTDI_CTS|FTDI_DSR|FTDI_RLSD;

    ret = 0;
    if (flags & CHR_TIOCM_CTS)
        ret |= FTDI_CTS;
    if (flags & CHR_TIOCM_DSR)
        ret |= FTDI_DSR;
    if (flags & CHR_TIOCM_RI)
        ret |= FTDI_RI;
    if (flags & CHR_TIOCM_CAR)
        ret |= FTDI_RLSD;

    return ret;
}

static int usb_serial_handle_control(USBDevice *dev, USBPacket *p,
               int request, int value, int index, int length, uint8_t *data)
{
    USBSerialState *s = (USBSerialState *)dev;
    int ret;

    DPRINTF("got control %x, value %x\n",request, value);
    ret = usb_desc_handle_control(dev, p, request, value, index, length, data);
    if (ret >= 0) {
        return ret;
    }

    ret = 0;
    switch (request) {
    case DeviceRequest | USB_REQ_GET_INTERFACE:
        data[0] = 0;
        ret = 1;
        break;
    case InterfaceOutRequest | USB_REQ_SET_INTERFACE:
        ret = 0;
        break;
    case EndpointOutRequest | USB_REQ_CLEAR_FEATURE:
        ret = 0;
        break;

        /* Class specific requests.  */
    case DeviceOutVendor | FTDI_RESET:
        switch (value) {
        case FTDI_RESET_SIO:
            usb_serial_reset(s);
            break;
        case FTDI_RESET_RX:
            s->recv_ptr = 0;
            s->recv_used = 0;
            /* TODO: purge from char device */
            break;
        case FTDI_RESET_TX:
            /* TODO: purge from char device */
            break;
        }
        break;
    case DeviceOutVendor | FTDI_SET_MDM_CTRL:
    {
        static int flags;
        qemu_chr_ioctl(s->cs,CHR_IOCTL_SERIAL_GET_TIOCM, &flags);
        if (value & FTDI_SET_RTS) {
            if (value & FTDI_RTS)
                flags |= CHR_TIOCM_RTS;
            else
                flags &= ~CHR_TIOCM_RTS;
        }
        if (value & FTDI_SET_DTR) {
            if (value & FTDI_DTR)
                flags |= CHR_TIOCM_DTR;
            else
                flags &= ~CHR_TIOCM_DTR;
        }
        qemu_chr_ioctl(s->cs,CHR_IOCTL_SERIAL_SET_TIOCM, &flags);
        break;
    }
    case DeviceOutVendor | FTDI_SET_FLOW_CTRL:
        /* TODO: ioctl */
        break;
    case DeviceOutVendor | FTDI_SET_BAUD: {
        static const int subdivisors8[8] = { 0, 4, 2, 1, 3, 5, 6, 7 };
        int subdivisor8 = subdivisors8[((value & 0xc000) >> 14)
                                     | ((index & 1) << 2)];
        int divisor = value & 0x3fff;

        /* chip special cases */
        if (divisor == 1 && subdivisor8 == 0)
            subdivisor8 = 4;
        if (divisor == 0 && subdivisor8 == 0)
            divisor = 1;

        s->params.speed = (48000000 / 2) / (8 * divisor + subdivisor8);
        qemu_chr_ioctl(s->cs, CHR_IOCTL_SERIAL_SET_PARAMS, &s->params);
        break;
    }
    case DeviceOutVendor | FTDI_SET_DATA:
        switch (value & FTDI_PARITY) {
            case 0:
                s->params.parity = 'N';
                break;
            case FTDI_ODD:
                s->params.parity = 'O';
                break;
            case FTDI_EVEN:
                s->params.parity = 'E';
                break;
            default:
                DPRINTF("unsupported parity %d\n", value & FTDI_PARITY);
                goto fail;
        }
        switch (value & FTDI_STOP) {
            case FTDI_STOP1:
                s->params.stop_bits = 1;
                break;
            case FTDI_STOP2:
                s->params.stop_bits = 2;
                break;
            default:
                DPRINTF("unsupported stop bits %d\n", value & FTDI_STOP);
                goto fail;
        }
        qemu_chr_ioctl(s->cs, CHR_IOCTL_SERIAL_SET_PARAMS, &s->params);
        /* TODO: TX ON/OFF */
        break;
    case DeviceInVendor | FTDI_GET_MDM_ST:
        data[0] = usb_get_modem_lines(s) | 1;
        data[1] = 0;
        ret = 2;
        break;
    case DeviceOutVendor | FTDI_SET_EVENT_CHR:
        /* TODO: handle it */
        s->event_chr = value;
        break;
    case DeviceOutVendor | FTDI_SET_ERROR_CHR:
        /* TODO: handle it */
        s->error_chr = value;
        break;
    case DeviceOutVendor | FTDI_SET_LATENCY:
        s->latency = value;
        break;
    case DeviceInVendor | FTDI_GET_LATENCY:
        data[0] = s->latency;
        ret = 1;
        break;
    default:
    fail:
        DPRINTF("got unsupported/bogus control %x, value %x\n", request, value);
        ret = USB_RET_STALL;
        break;
    }
    return ret;
}

static int usb_serial_handle_data(USBDevice *dev, USBPacket *p)
{
    USBSerialState *s = (USBSerialState *)dev;
    int i, ret = 0;
    uint8_t devep = p->devep;
    struct iovec *iov;
    uint8_t header[2];
    int first_len, len;

    switch (p->pid) {
    case USB_TOKEN_OUT:
        if (devep != 2)
            goto fail;
        for (i = 0; i < p->iov.niov; i++) {
            iov = p->iov.iov + i;
            qemu_chr_write(s->cs, iov->iov_base, iov->iov_len);
        }
        break;

    case USB_TOKEN_IN:
        if (devep != 1)
            goto fail;
        first_len = RECV_BUF - s->recv_ptr;
        len = p->iov.size;
        if (len <= 2) {
            ret = USB_RET_NAK;
            break;
        }
        header[0] = usb_get_modem_lines(s) | 1;
        /* We do not have the uart details */
        /* handle serial break */
        if (s->event_trigger && s->event_trigger & FTDI_BI) {
            s->event_trigger &= ~FTDI_BI;
            header[1] = FTDI_BI;
            usb_packet_copy(p, header, 2);
            ret = 2;
            break;
        } else {
            header[1] = 0;
        }
        len -= 2;
        if (len > s->recv_used)
            len = s->recv_used;
        if (!len) {
            ret = USB_RET_NAK;
            break;
        }
        if (first_len > len)
            first_len = len;
        usb_packet_copy(p, header, 2);
        usb_packet_copy(p, s->recv_buf + s->recv_ptr, first_len);
        if (len > first_len)
            usb_packet_copy(p, s->recv_buf, len - first_len);
        s->recv_used -= len;
        s->recv_ptr = (s->recv_ptr + len) % RECV_BUF;
        ret = len + 2;
        break;

    default:
        DPRINTF("Bad token\n");
    fail:
        ret = USB_RET_STALL;
        break;
    }

    return ret;
}

static void usb_serial_handle_destroy(USBDevice *dev)
{
    USBSerialState *s = (USBSerialState *)dev;

    qemu_chr_close(s->cs);
}

static int usb_serial_can_read(void *opaque)
{
    USBSerialState *s = opaque;
    return RECV_BUF - s->recv_used;
}

static void usb_serial_read(void *opaque, const uint8_t *buf, int size)
{
    USBSerialState *s = opaque;
    int first_size, start;

    /* room in the buffer? */
    if (size > (RECV_BUF - s->recv_used))
        size = RECV_BUF - s->recv_used;

    start = s->recv_ptr + s->recv_used;
    if (start < RECV_BUF) {
        /* copy data to end of buffer */
        first_size = RECV_BUF - start;
        if (first_size > size)
            first_size = size;

        memcpy(s->recv_buf + start, buf, first_size);

        /* wrap around to front if needed */
        if (size > first_size)
            memcpy(s->recv_buf, buf + first_size, size - first_size);
    } else {
        start -= RECV_BUF;
        memcpy(s->recv_buf + start, buf, size);
    }
    s->recv_used += size;
}

static void usb_serial_event(void *opaque, int event)
{
    USBSerialState *s = opaque;

    switch (event) {
        case CHR_EVENT_BREAK:
            s->event_trigger |= FTDI_BI;
            break;
        case CHR_EVENT_FOCUS:
            break;
        case CHR_EVENT_OPENED:
            usb_serial_reset(s);
            /* TODO: Reset USB port */
            break;
    }
}

static int usb_serial_initfn(USBDevice *dev)
{
    USBSerialState *s = DO_UPCAST(USBSerialState, dev, dev);

    usb_desc_init(dev);

    if (!s->cs) {
        error_report("Property chardev is required");
        return -1;
    }

    qemu_chr_add_handlers(s->cs, usb_serial_can_read, usb_serial_read,
                          usb_serial_event, s);
    usb_serial_handle_reset(dev);
    return 0;
}

static USBDevice *usb_serial_init(const char *filename)
{
    USBDevice *dev;
    CharDriverState *cdrv;
    uint32_t vendorid = 0, productid = 0;
    char label[32];
    static int index;

    while (*filename && *filename != ':') {
        const char *p;
        char *e;
        if (strstart(filename, "vendorid=", &p)) {
            vendorid = strtol(p, &e, 16);
            if (e == p || (*e && *e != ',' && *e != ':')) {
                error_report("bogus vendor ID %s", p);
                return NULL;
            }
            filename = e;
        } else if (strstart(filename, "productid=", &p)) {
            productid = strtol(p, &e, 16);
            if (e == p || (*e && *e != ',' && *e != ':')) {
                error_report("bogus product ID %s", p);
                return NULL;
            }
            filename = e;
        } else {
            error_report("unrecognized serial USB option %s", filename);
            return NULL;
        }
        while(*filename == ',')
            filename++;
    }
    if (!*filename) {
        error_report("character device specification needed");
        return NULL;
    }
    filename++;

    snprintf(label, sizeof(label), "usbserial%d", index++);
    cdrv = qemu_chr_open(label, filename, NULL);
    if (!cdrv)
        return NULL;

    dev = usb_create(NULL /* FIXME */, "usb-serial");
    if (!dev) {
        return NULL;
    }
    qdev_prop_set_chr(&dev->qdev, "chardev", cdrv);
    if (vendorid)
        qdev_prop_set_uint16(&dev->qdev, "vendorid", vendorid);
    if (productid)
        qdev_prop_set_uint16(&dev->qdev, "productid", productid);
    qdev_init_nofail(&dev->qdev);

    return dev;
}

static USBDevice *usb_braille_init(const char *unused)
{
    USBDevice *dev;
    CharDriverState *cdrv;

    cdrv = qemu_chr_open("braille", "braille", NULL);
    if (!cdrv)
        return NULL;

    dev = usb_create(NULL /* FIXME */, "usb-braille");
    qdev_prop_set_chr(&dev->qdev, "chardev", cdrv);
    qdev_init_nofail(&dev->qdev);

    return dev;
}

static const VMStateDescription vmstate_usb_serial = {
    .name = "usb-serial",
    .unmigratable = 1,
};

static struct USBDeviceInfo serial_info = {
    .product_desc   = "QEMU USB Serial",
    .qdev.name      = "usb-serial",
    .qdev.size      = sizeof(USBSerialState),
    .qdev.vmsd      = &vmstate_usb_serial,
    .usb_desc       = &desc_serial,
    .init           = usb_serial_initfn,
    .handle_packet  = usb_generic_handle_packet,
    .handle_reset   = usb_serial_handle_reset,
    .handle_control = usb_serial_handle_control,
    .handle_data    = usb_serial_handle_data,
    .handle_destroy = usb_serial_handle_destroy,
    .usbdevice_name = "serial",
    .usbdevice_init = usb_serial_init,
    .qdev.props     = (Property[]) {
        DEFINE_PROP_CHR("chardev", USBSerialState, cs),
        DEFINE_PROP_END_OF_LIST(),
    },
};

static struct USBDeviceInfo braille_info = {
    .product_desc   = "QEMU USB Braille",
    .qdev.name      = "usb-braille",
    .qdev.size      = sizeof(USBSerialState),
    .qdev.vmsd      = &vmstate_usb_serial,
    .usb_desc       = &desc_braille,
    .init           = usb_serial_initfn,
    .handle_packet  = usb_generic_handle_packet,
    .handle_reset   = usb_serial_handle_reset,
    .handle_control = usb_serial_handle_control,
    .handle_data    = usb_serial_handle_data,
    .handle_destroy = usb_serial_handle_destroy,
    .usbdevice_name = "braille",
    .usbdevice_init = usb_braille_init,
    .qdev.props     = (Property[]) {
        DEFINE_PROP_CHR("chardev", USBSerialState, cs),
        DEFINE_PROP_END_OF_LIST(),
    },
};

static void usb_serial_register_devices(void)
{
    usb_qdev_register(&serial_info);
    usb_qdev_register(&braille_info);
}
device_init(usb_serial_register_devices)
