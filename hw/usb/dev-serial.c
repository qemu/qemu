/*
 * FTDI FT232BM Device emulation
 *
 * Copyright (c) 2006 CodeSourcery.
 * Copyright (c) 2008 Samuel Thibault <samuel.thibault@ens-lyon.org>
 * Written by Paul Brook, reused for FTDI by Samuel Thibault
 *
 * This code is licensed under the LGPL.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "hw/usb.h"
#include "migration/vmstate.h"
#include "desc.h"
#include "chardev/char-serial.h"
#include "chardev/char-fe.h"
#include "qom/object.h"
#include "trace.h"


#define RECV_BUF (512 - (2 * 8))

/* Commands */
#define FTDI_RESET             0
#define FTDI_SET_MDM_CTRL      1
#define FTDI_SET_FLOW_CTRL     2
#define FTDI_SET_BAUD          3
#define FTDI_SET_DATA          4
#define FTDI_GET_MDM_ST        5
#define FTDI_SET_EVENT_CHR     6
#define FTDI_SET_ERROR_CHR     7
#define FTDI_SET_LATENCY       9
#define FTDI_GET_LATENCY       10

/* RESET */

#define FTDI_RESET_SIO 0
#define FTDI_RESET_RX  1
#define FTDI_RESET_TX  2

/* SET_MDM_CTRL */

#define FTDI_DTR       1
#define FTDI_SET_DTR   (FTDI_DTR << 8)
#define FTDI_RTS       2
#define FTDI_SET_RTS   (FTDI_RTS << 8)

/* SET_FLOW_CTRL */

#define FTDI_NO_HS         0
#define FTDI_RTS_CTS_HS    1
#define FTDI_DTR_DSR_HS    2
#define FTDI_XON_XOFF_HS   4

/* SET_DATA */

#define FTDI_PARITY    (0x7 << 8)
#define FTDI_ODD       (0x1 << 8)
#define FTDI_EVEN      (0x2 << 8)
#define FTDI_MARK      (0x3 << 8)
#define FTDI_SPACE     (0x4 << 8)

#define FTDI_STOP      (0x3 << 11)
#define FTDI_STOP1     (0x0 << 11)
#define FTDI_STOP15    (0x1 << 11)
#define FTDI_STOP2     (0x2 << 11)

/* GET_MDM_ST */
/* TODO: should be sent every 40ms */
#define FTDI_CTS   (1 << 4)    /* CTS line status */
#define FTDI_DSR   (1 << 5)    /* DSR line status */
#define FTDI_RI    (1 << 6)    /* RI line status */
#define FTDI_RLSD  (1 << 7)    /* Receive Line Signal Detect */

/* Status */

#define FTDI_DR    (1 << 0)    /* Data Ready */
#define FTDI_OE    (1 << 1)    /* Overrun Err */
#define FTDI_PE    (1 << 2)    /* Parity Err */
#define FTDI_FE    (1 << 3)    /* Framing Err */
#define FTDI_BI    (1 << 4)    /* Break Interrupt */
#define FTDI_THRE  (1 << 5)    /* Transmitter Holding Register */
#define FTDI_TEMT  (1 << 6)    /* Transmitter Empty */
#define FTDI_FIFO  (1 << 7)    /* Error in FIFO */

struct USBSerialState {
    USBDevice dev;

    USBEndpoint *intr;
    uint8_t recv_buf[RECV_BUF];
    uint16_t recv_ptr;
    uint16_t recv_used;
    uint8_t event_chr;
    uint8_t error_chr;
    uint8_t event_trigger;
    bool always_plugged;
    uint8_t flow_control;
    uint8_t xon;
    uint8_t xoff;
    QEMUSerialSetParams params;
    int latency;        /* ms */
    CharFrontend cs;
};

#define TYPE_USB_SERIAL "usb-serial-dev"
OBJECT_DECLARE_SIMPLE_TYPE(USBSerialState, USB_SERIAL)

enum {
    STR_MANUFACTURER = 1,
    STR_PRODUCT_SERIAL,
    STR_PRODUCT_BRAILLE,
    STR_SERIALNUMBER,
};

static const USBDescStrings desc_strings = {
    [STR_MANUFACTURER]    = "QEMU",
    [STR_PRODUCT_SERIAL]  = "QEMU USB SERIAL",
    [STR_PRODUCT_BRAILLE] = "QEMU USB BAUM BRAILLE",
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
            .bmAttributes          = USB_CFG_ATT_ONE | USB_CFG_ATT_WAKEUP,
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

static void usb_serial_set_flow_control(USBSerialState *s,
                                        uint8_t flow_control)
{
    USBDevice *dev = USB_DEVICE(s);
    USBBus *bus = usb_bus_from_device(dev);

    /* TODO: ioctl */
    s->flow_control = flow_control;
    trace_usb_serial_set_flow_control(bus->busnr, dev->addr, flow_control);
}

static void usb_serial_set_xonxoff(USBSerialState *s, int xonxoff)
{
    USBDevice *dev = USB_DEVICE(s);
    USBBus *bus = usb_bus_from_device(dev);

    s->xon = xonxoff & 0xff;
    s->xoff = (xonxoff >> 8) & 0xff;

    trace_usb_serial_set_xonxoff(bus->busnr, dev->addr, s->xon, s->xoff);
}

static void usb_serial_reset(USBSerialState *s)
{
    s->event_chr = 0x0d;
    s->event_trigger = 0;
    s->recv_ptr = 0;
    s->recv_used = 0;
    /* TODO: purge in char driver */
    usb_serial_set_flow_control(s, FTDI_NO_HS);
}

static void usb_serial_handle_reset(USBDevice *dev)
{
    USBSerialState *s = USB_SERIAL(dev);
    USBBus *bus = usb_bus_from_device(dev);

    trace_usb_serial_reset(bus->busnr, dev->addr);

    usb_serial_reset(s);
    /* TODO: Reset char device, send BREAK? */
}

static uint8_t usb_get_modem_lines(USBSerialState *s)
{
    int flags;
    uint8_t ret;

    if (qemu_chr_fe_ioctl(&s->cs,
                          CHR_IOCTL_SERIAL_GET_TIOCM, &flags) == -ENOTSUP) {
        return FTDI_CTS | FTDI_DSR | FTDI_RLSD;
    }

    ret = 0;
    if (flags & CHR_TIOCM_CTS) {
        ret |= FTDI_CTS;
    }
    if (flags & CHR_TIOCM_DSR) {
        ret |= FTDI_DSR;
    }
    if (flags & CHR_TIOCM_RI) {
        ret |= FTDI_RI;
    }
    if (flags & CHR_TIOCM_CAR) {
        ret |= FTDI_RLSD;
    }

    return ret;
}

static void usb_serial_handle_control(USBDevice *dev, USBPacket *p,
                                      int request, int value, int index,
                                      int length, uint8_t *data)
{
    USBSerialState *s = USB_SERIAL(dev);
    USBBus *bus = usb_bus_from_device(dev);
    int ret;

    trace_usb_serial_handle_control(bus->busnr, dev->addr, request, value);

    ret = usb_desc_handle_control(dev, p, request, value, index, length, data);
    if (ret >= 0) {
        return;
    }

    switch (request) {
    case EndpointOutRequest | USB_REQ_CLEAR_FEATURE:
        break;

    /* Class specific requests.  */
    case VendorDeviceOutRequest | FTDI_RESET:
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
    case VendorDeviceOutRequest | FTDI_SET_MDM_CTRL:
    {
        static int flags;
        qemu_chr_fe_ioctl(&s->cs, CHR_IOCTL_SERIAL_GET_TIOCM, &flags);
        if (value & FTDI_SET_RTS) {
            if (value & FTDI_RTS) {
                flags |= CHR_TIOCM_RTS;
            } else {
                flags &= ~CHR_TIOCM_RTS;
            }
        }
        if (value & FTDI_SET_DTR) {
            if (value & FTDI_DTR) {
                flags |= CHR_TIOCM_DTR;
            } else {
                flags &= ~CHR_TIOCM_DTR;
            }
        }
        qemu_chr_fe_ioctl(&s->cs, CHR_IOCTL_SERIAL_SET_TIOCM, &flags);
        break;
    }
    case VendorDeviceOutRequest | FTDI_SET_FLOW_CTRL: {
        uint8_t flow_control = index >> 8;

        usb_serial_set_flow_control(s, flow_control);
        if (flow_control & FTDI_XON_XOFF_HS) {
            usb_serial_set_xonxoff(s, value);
        }
        break;
    }
    case VendorDeviceOutRequest | FTDI_SET_BAUD: {
        static const int subdivisors8[8] = { 0, 4, 2, 1, 3, 5, 6, 7 };
        int subdivisor8 = subdivisors8[((value & 0xc000) >> 14)
                                     | ((index & 1) << 2)];
        int divisor = value & 0x3fff;

        /* chip special cases */
        if (divisor == 1 && subdivisor8 == 0) {
            subdivisor8 = 4;
        }
        if (divisor == 0 && subdivisor8 == 0) {
            divisor = 1;
        }

        s->params.speed = (48000000 / 2) / (8 * divisor + subdivisor8);
        trace_usb_serial_set_baud(bus->busnr, dev->addr, s->params.speed);
        qemu_chr_fe_ioctl(&s->cs, CHR_IOCTL_SERIAL_SET_PARAMS, &s->params);
        break;
    }
    case VendorDeviceOutRequest | FTDI_SET_DATA:
        switch (value & 0xff) {
        case 7:
            s->params.data_bits = 7;
            break;
        case 8:
            s->params.data_bits = 8;
            break;
        default:
            /*
             * According to a comment in Linux's ftdi_sio.c original FTDI
             * chips fall back to 8 data bits for unsupported data_bits
             */
            trace_usb_serial_unsupported_data_bits(bus->busnr, dev->addr,
                                                   value & 0xff);
            s->params.data_bits = 8;
        }

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
            trace_usb_serial_unsupported_parity(bus->busnr, dev->addr,
                                                value & FTDI_PARITY);
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
            trace_usb_serial_unsupported_stopbits(bus->busnr, dev->addr,
                                                  value & FTDI_STOP);
            goto fail;
        }

        trace_usb_serial_set_data(bus->busnr, dev->addr, s->params.parity,
                                  s->params.data_bits, s->params.stop_bits);
        qemu_chr_fe_ioctl(&s->cs, CHR_IOCTL_SERIAL_SET_PARAMS, &s->params);
        /* TODO: TX ON/OFF */
        break;
    case VendorDeviceRequest | FTDI_GET_MDM_ST:
        data[0] = usb_get_modem_lines(s) | 1;
        data[1] = FTDI_THRE | FTDI_TEMT;
        p->actual_length = 2;
        break;
    case VendorDeviceOutRequest | FTDI_SET_EVENT_CHR:
        /* TODO: handle it */
        s->event_chr = value;
        break;
    case VendorDeviceOutRequest | FTDI_SET_ERROR_CHR:
        /* TODO: handle it */
        s->error_chr = value;
        break;
    case VendorDeviceOutRequest | FTDI_SET_LATENCY:
        s->latency = value;
        break;
    case VendorDeviceRequest | FTDI_GET_LATENCY:
        data[0] = s->latency;
        p->actual_length = 1;
        break;
    default:
    fail:
        trace_usb_serial_unsupported_control(bus->busnr, dev->addr, request,
                                             value);
        p->status = USB_RET_STALL;
        break;
    }
}

static void usb_serial_token_in(USBSerialState *s, USBPacket *p)
{
    const int max_packet_size = desc_iface0.eps[0].wMaxPacketSize;
    int packet_len;
    uint8_t header[2];

    packet_len = p->iov.size;
    if (packet_len <= 2) {
        p->status = USB_RET_NAK;
        return;
    }

    header[0] = usb_get_modem_lines(s) | 1;
    /* We do not have the uart details */
    /* handle serial break */
    if (s->event_trigger && s->event_trigger & FTDI_BI) {
        s->event_trigger &= ~FTDI_BI;
        header[1] = FTDI_BI;
        usb_packet_copy(p, header, 2);
        return;
    } else {
        header[1] = 0;
    }

    if (!s->recv_used) {
        p->status = USB_RET_NAK;
        return;
    }

    while (s->recv_used && packet_len > 2) {
        int first_len, len;

        len = MIN(packet_len, max_packet_size);
        len -= 2;
        if (len > s->recv_used) {
            len = s->recv_used;
        }

        first_len = RECV_BUF - s->recv_ptr;
        if (first_len > len) {
            first_len = len;
        }
        usb_packet_copy(p, header, 2);
        usb_packet_copy(p, s->recv_buf + s->recv_ptr, first_len);
        if (len > first_len) {
            usb_packet_copy(p, s->recv_buf, len - first_len);
        }
        s->recv_used -= len;
        s->recv_ptr = (s->recv_ptr + len) % RECV_BUF;
        packet_len -= len + 2;
    }
}

static void usb_serial_handle_data(USBDevice *dev, USBPacket *p)
{
    USBSerialState *s = USB_SERIAL(dev);
    USBBus *bus = usb_bus_from_device(dev);
    uint8_t devep = p->ep->nr;
    struct iovec *iov;
    int i;

    switch (p->pid) {
    case USB_TOKEN_OUT:
        if (devep != 2) {
            goto fail;
        }
        for (i = 0; i < p->iov.niov; i++) {
            iov = p->iov.iov + i;
            /*
             * XXX this blocks entire thread. Rewrite to use
             * qemu_chr_fe_write and background I/O callbacks
             */
            qemu_chr_fe_write_all(&s->cs, iov->iov_base, iov->iov_len);
        }
        p->actual_length = p->iov.size;
        break;

    case USB_TOKEN_IN:
        if (devep != 1) {
            goto fail;
        }
        usb_serial_token_in(s, p);
        break;

    default:
        trace_usb_serial_bad_token(bus->busnr, dev->addr);
    fail:
        p->status = USB_RET_STALL;
        break;
    }
}

static int usb_serial_can_read(void *opaque)
{
    USBSerialState *s = opaque;

    if (!s->dev.attached) {
        return 0;
    }
    return RECV_BUF - s->recv_used;
}

static void usb_serial_read(void *opaque, const uint8_t *buf, int size)
{
    USBSerialState *s = opaque;
    int first_size, start;

    /* room in the buffer? */
    if (size > (RECV_BUF - s->recv_used)) {
        size = RECV_BUF - s->recv_used;
    }

    start = s->recv_ptr + s->recv_used;
    if (start < RECV_BUF) {
        /* copy data to end of buffer */
        first_size = RECV_BUF - start;
        if (first_size > size) {
            first_size = size;
        }

        memcpy(s->recv_buf + start, buf, first_size);

        /* wrap around to front if needed */
        if (size > first_size) {
            memcpy(s->recv_buf, buf + first_size, size - first_size);
        }
    } else {
        start -= RECV_BUF;
        memcpy(s->recv_buf + start, buf, size);
    }
    s->recv_used += size;

    usb_wakeup(s->intr, 0);
}

static void usb_serial_event(void *opaque, QEMUChrEvent event)
{
    USBSerialState *s = opaque;

    switch (event) {
    case CHR_EVENT_BREAK:
        s->event_trigger |= FTDI_BI;
        break;
    case CHR_EVENT_OPENED:
        if (!s->always_plugged && !s->dev.attached) {
            usb_device_attach(&s->dev, &error_abort);
        }
        break;
    case CHR_EVENT_CLOSED:
        if (!s->always_plugged && s->dev.attached) {
            usb_device_detach(&s->dev);
        }
        break;
    case CHR_EVENT_MUX_IN:
    case CHR_EVENT_MUX_OUT:
        /* Ignore */
        break;
    }
}

static void usb_serial_realize(USBDevice *dev, Error **errp)
{
    USBSerialState *s = USB_SERIAL(dev);
    Error *local_err = NULL;

    usb_desc_create_serial(dev);
    usb_desc_init(dev);
    dev->auto_attach = 0;

    if (!qemu_chr_fe_backend_connected(&s->cs)) {
        error_setg(errp, "Property chardev is required");
        return;
    }

    usb_check_attach(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    qemu_chr_fe_set_handlers(&s->cs, usb_serial_can_read, usb_serial_read,
                             usb_serial_event, NULL, s, NULL, true);
    usb_serial_handle_reset(dev);

    if ((s->always_plugged || qemu_chr_fe_backend_open(&s->cs)) &&
        !dev->attached) {
        usb_device_attach(dev, &error_abort);
    }
    s->intr = usb_ep_get(dev, USB_TOKEN_IN, 1);
}

static USBDevice *usb_braille_init(void)
{
    USBDevice *dev;
    Chardev *cdrv;

    cdrv = qemu_chr_new("braille", "braille", NULL);
    if (!cdrv) {
        return NULL;
    }

    dev = USB_DEVICE(qdev_new("usb-braille"));
    qdev_prop_set_chr(&dev->qdev, "chardev", cdrv);
    return dev;
}

static const VMStateDescription vmstate_usb_serial = {
    .name = "usb-serial",
    .unmigratable = 1,
};

static const Property serial_properties[] = {
    DEFINE_PROP_CHR("chardev", USBSerialState, cs),
    DEFINE_PROP_BOOL("always-plugged", USBSerialState, always_plugged, false),
};

static void usb_serial_dev_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    USBDeviceClass *uc = USB_DEVICE_CLASS(klass);

    uc->realize        = usb_serial_realize;
    uc->handle_reset   = usb_serial_handle_reset;
    uc->handle_control = usb_serial_handle_control;
    uc->handle_data    = usb_serial_handle_data;
    dc->vmsd = &vmstate_usb_serial;
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
}

static const TypeInfo usb_serial_dev_type_info = {
    .name = TYPE_USB_SERIAL,
    .parent = TYPE_USB_DEVICE,
    .instance_size = sizeof(USBSerialState),
    .abstract = true,
    .class_init = usb_serial_dev_class_init,
};

static void usb_serial_class_initfn(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    USBDeviceClass *uc = USB_DEVICE_CLASS(klass);

    uc->product_desc   = "QEMU USB Serial";
    uc->usb_desc       = &desc_serial;
    device_class_set_props(dc, serial_properties);
}

static const TypeInfo serial_info = {
    .name          = "usb-serial",
    .parent        = TYPE_USB_SERIAL,
    .class_init    = usb_serial_class_initfn,
};

static const Property braille_properties[] = {
    DEFINE_PROP_CHR("chardev", USBSerialState, cs),
};

static void usb_braille_class_initfn(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    USBDeviceClass *uc = USB_DEVICE_CLASS(klass);

    uc->product_desc   = "QEMU USB Braille";
    uc->usb_desc       = &desc_braille;
    device_class_set_props(dc, braille_properties);
}

static const TypeInfo braille_info = {
    .name          = "usb-braille",
    .parent        = TYPE_USB_SERIAL,
    .class_init    = usb_braille_class_initfn,
};

static void usb_serial_register_types(void)
{
    type_register_static(&usb_serial_dev_type_info);
    type_register_static(&serial_info);
    type_register_static(&braille_info);
    usb_legacy_register("usb-braille", "braille", usb_braille_init);
}

type_init(usb_serial_register_types)
