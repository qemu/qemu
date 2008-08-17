/*
 * FTDI FT232BM Device emulation
 *
 * Copyright (c) 2006 CodeSourcery.
 * Copyright (c) 2008 Samuel Thibault <samuel.thibault@ens-lyon.org>
 * Written by Paul Brook, reused for FTDI by Samuel Thibault
 *
 * This code is licenced under the LGPL.
 */

#include "qemu-common.h"
#include "usb.h"
#include "qemu-char.h"

//#define DEBUG_Serial

#ifdef DEBUG_Serial
#define DPRINTF(fmt, args...) \
do { printf("usb-serial: " fmt , ##args); } while (0)
#else
#define DPRINTF(fmt, args...) do {} while(0)
#endif

#define RECV_BUF 384
#define SEND_BUF 128        // Not used for now

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
    uint16_t vendorid;
    uint16_t productid;
    uint8_t recv_buf[RECV_BUF];
    uint8_t recv_ptr;
    uint8_t recv_used;
    uint8_t send_buf[SEND_BUF];
    uint8_t event_chr;
    uint8_t error_chr;
    uint8_t event_trigger;
    QEMUSerialSetParams params;
    int latency;        /* ms */
    CharDriverState *cs;
} USBSerialState;

static const uint8_t qemu_serial_dev_descriptor[] = {
        0x12,       /*  u8 bLength; */
        0x01,       /*  u8 bDescriptorType; Device */
        0x00, 0x02, /*  u16 bcdUSB; v2.0 */

        0x00,       /*  u8  bDeviceClass; */
        0x00,       /*  u8  bDeviceSubClass; */
        0x00,       /*  u8  bDeviceProtocol; [ low/full speeds only ] */
        0x08,       /*  u8  bMaxPacketSize0; 8 Bytes */

        /* Vendor and product id are arbitrary.  */
        0x03, 0x04, /*  u16 idVendor; */
        0x00, 0xFF, /*  u16 idProduct; */
        0x00, 0x04, /*  u16 bcdDevice */

        0x01,       /*  u8  iManufacturer; */
        0x02,       /*  u8  iProduct; */
        0x03,       /*  u8  iSerialNumber; */
        0x01        /*  u8  bNumConfigurations; */
};

static const uint8_t qemu_serial_config_descriptor[] = {

        /* one configuration */
        0x09,       /*  u8  bLength; */
        0x02,       /*  u8  bDescriptorType; Configuration */
        0x20, 0x00, /*  u16 wTotalLength; */
        0x01,       /*  u8  bNumInterfaces; (1) */
        0x01,       /*  u8  bConfigurationValue; */
        0x00,       /*  u8  iConfiguration; */
        0x80,       /*  u8  bmAttributes;
                                 Bit 7: must be set,
                                     6: Self-powered,
                                     5: Remote wakeup,
                                     4..0: resvd */
        100/2,       /*  u8  MaxPower; */

        /* one interface */
        0x09,       /*  u8  if_bLength; */
        0x04,       /*  u8  if_bDescriptorType; Interface */
        0x00,       /*  u8  if_bInterfaceNumber; */
        0x00,       /*  u8  if_bAlternateSetting; */
        0x02,       /*  u8  if_bNumEndpoints; */
        0xff,       /*  u8  if_bInterfaceClass; Vendor Specific */
        0xff,       /*  u8  if_bInterfaceSubClass; Vendor Specific */
        0xff,       /*  u8  if_bInterfaceProtocol; Vendor Specific */
        0x02,       /*  u8  if_iInterface; */

        /* Bulk-In endpoint */
        0x07,       /*  u8  ep_bLength; */
        0x05,       /*  u8  ep_bDescriptorType; Endpoint */
        0x81,       /*  u8  ep_bEndpointAddress; IN Endpoint 1 */
        0x02,       /*  u8  ep_bmAttributes; Bulk */
        0x40, 0x00, /*  u16 ep_wMaxPacketSize; */
        0x00,       /*  u8  ep_bInterval; */

        /* Bulk-Out endpoint */
        0x07,       /*  u8  ep_bLength; */
        0x05,       /*  u8  ep_bDescriptorType; Endpoint */
        0x02,       /*  u8  ep_bEndpointAddress; OUT Endpoint 2 */
        0x02,       /*  u8  ep_bmAttributes; Bulk */
        0x40, 0x00, /*  u16 ep_wMaxPacketSize; */
        0x00        /*  u8  ep_bInterval; */
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

static int usb_serial_handle_control(USBDevice *dev, int request, int value,
                                  int index, int length, uint8_t *data)
{
    USBSerialState *s = (USBSerialState *)dev;
    int ret = 0;

    //DPRINTF("got control %x, value %x\n",request, value);
    switch (request) {
    case DeviceRequest | USB_REQ_GET_STATUS:
        data[0] = (0 << USB_DEVICE_SELF_POWERED) |
            (dev->remote_wakeup << USB_DEVICE_REMOTE_WAKEUP);
        data[1] = 0x00;
        ret = 2;
        break;
    case DeviceOutRequest | USB_REQ_CLEAR_FEATURE:
        if (value == USB_DEVICE_REMOTE_WAKEUP) {
            dev->remote_wakeup = 0;
        } else {
            goto fail;
        }
        ret = 0;
        break;
    case DeviceOutRequest | USB_REQ_SET_FEATURE:
        if (value == USB_DEVICE_REMOTE_WAKEUP) {
            dev->remote_wakeup = 1;
        } else {
            goto fail;
        }
        ret = 0;
        break;
    case DeviceOutRequest | USB_REQ_SET_ADDRESS:
        dev->addr = value;
        ret = 0;
        break;
    case DeviceRequest | USB_REQ_GET_DESCRIPTOR:
        switch(value >> 8) {
        case USB_DT_DEVICE:
            memcpy(data, qemu_serial_dev_descriptor,
                   sizeof(qemu_serial_dev_descriptor));
            data[8] = s->vendorid & 0xff;
            data[9] = ((s->vendorid) >> 8) & 0xff;
            data[10] = s->productid & 0xff;
            data[11] = ((s->productid) >> 8) & 0xff;
            ret = sizeof(qemu_serial_dev_descriptor);
            break;
        case USB_DT_CONFIG:
            memcpy(data, qemu_serial_config_descriptor,
                   sizeof(qemu_serial_config_descriptor));
            ret = sizeof(qemu_serial_config_descriptor);
            break;
        case USB_DT_STRING:
            switch(value & 0xff) {
            case 0:
                /* language ids */
                data[0] = 4;
                data[1] = 3;
                data[2] = 0x09;
                data[3] = 0x04;
                ret = 4;
                break;
            case 1:
                /* vendor description */
                ret = set_usb_string(data, "QEMU " QEMU_VERSION);
                break;
            case 2:
                /* product description */
                ret = set_usb_string(data, "QEMU USB SERIAL");
                break;
            case 3:
                /* serial number */
                ret = set_usb_string(data, "1");
                break;
            default:
                goto fail;
            }
            break;
        default:
            goto fail;
        }
        break;
    case DeviceRequest | USB_REQ_GET_CONFIGURATION:
        data[0] = 1;
        ret = 1;
        break;
    case DeviceOutRequest | USB_REQ_SET_CONFIGURATION:
        ret = 0;
        break;
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
    int ret = 0;
    uint8_t devep = p->devep;
    uint8_t *data = p->data;
    int len = p->len;
    int first_len;

    switch (p->pid) {
    case USB_TOKEN_OUT:
        if (devep != 2)
            goto fail;
        qemu_chr_write(s->cs, data, len);
        break;

    case USB_TOKEN_IN:
        if (devep != 1)
            goto fail;
        first_len = RECV_BUF - s->recv_ptr;
        if (len <= 2) {
            ret = USB_RET_NAK;
            break;
        }
        *data++ = usb_get_modem_lines(s) | 1;
        /* We do not have the uart details */
        *data++ = 0;
        len -= 2;
        if (len > s->recv_used)
            len = s->recv_used;
        if (!len) {
            ret = USB_RET_NAK;
            break;
        }
        if (first_len > len)
            first_len = len;
        memcpy(data, s->recv_buf + s->recv_ptr, first_len);
        if (len > first_len)
            memcpy(data + first_len, s->recv_buf, len - first_len);
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
    qemu_free(s);
}

static int usb_serial_can_read(void *opaque)
{
    USBSerialState *s = opaque;
    return RECV_BUF - s->recv_used;
}

static void usb_serial_read(void *opaque, const uint8_t *buf, int size)
{
    USBSerialState *s = opaque;
    int first_size = RECV_BUF - s->recv_ptr;
    if (first_size > size)
        first_size = size;
    memcpy(s->recv_buf + s->recv_ptr + s->recv_used, buf, first_size);
    if (size > first_size)
        memcpy(s->recv_buf, buf + first_size, size - first_size);
    s->recv_used += size;
}

static void usb_serial_event(void *opaque, int event)
{
    USBSerialState *s = opaque;

    switch (event) {
        case CHR_EVENT_BREAK:
            /* TODO: Send Break to USB */
            break;
        case CHR_EVENT_FOCUS:
            break;
        case CHR_EVENT_RESET:
            usb_serial_reset(s);
            /* TODO: Reset USB port */
            break;
    }
}

USBDevice *usb_serial_init(const char *filename)
{
    USBSerialState *s;
    CharDriverState *cdrv;
    unsigned short vendorid = 0x0403, productid = 0x6001;

    while (*filename && *filename != ':') {
        const char *p;
        char *e;
        if (strstart(filename, "vendorid=", &p)) {
            vendorid = strtol(p, &e, 16);
            if (e == p || (*e && *e != ',' && *e != ':')) {
                printf("bogus vendor ID %s\n", p);
                return NULL;
            }
            filename = e;
        } else if (strstart(filename, "productid=", &p)) {
            productid = strtol(p, &e, 16);
            if (e == p || (*e && *e != ',' && *e != ':')) {
                printf("bogus product ID %s\n", p);
                return NULL;
            }
            filename = e;
        } else {
            printf("unrecognized serial USB option %s\n", filename);
            return NULL;
        }
        while(*filename == ',')
            filename++;
    }
    if (!*filename) {
        printf("character device specification needed\n");
        return NULL;
    }
    filename++;
    s = qemu_mallocz(sizeof(USBSerialState));
    if (!s)
        return NULL;

    cdrv = qemu_chr_open(filename);
    if (!cdrv)
        goto fail;
    s->cs = cdrv;
    qemu_chr_add_handlers(cdrv, usb_serial_can_read, usb_serial_read, usb_serial_event, s);

    s->dev.speed = USB_SPEED_FULL;
    s->dev.handle_packet = usb_generic_handle_packet;

    s->dev.handle_reset = usb_serial_handle_reset;
    s->dev.handle_control = usb_serial_handle_control;
    s->dev.handle_data = usb_serial_handle_data;
    s->dev.handle_destroy = usb_serial_handle_destroy;

    s->vendorid = vendorid;
    s->productid = productid;

    snprintf(s->dev.devname, sizeof(s->dev.devname), "QEMU USB Serial(%.16s)",
             filename);

    usb_serial_handle_reset((USBDevice *)s);
    return (USBDevice *)s;
 fail:
    qemu_free(s);
    return NULL;
}
