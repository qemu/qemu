/*
 * Wacom PenPartner USB tablet emulation.
 *
 * Copyright (c) 2006 Openedhand Ltd.
 * Author: Andrzej Zaborowski <balrog@zabor.org>
 *
 * Based on hw/usb-hid.c:
 * Copyright (c) 2005 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "hw.h"
#include "console.h"
#include "usb.h"
#include "usb-desc.h"

/* Interface requests */
#define WACOM_GET_REPORT	0x2101
#define WACOM_SET_REPORT	0x2109

/* HID interface requests */
#define HID_GET_REPORT		0xa101
#define HID_GET_IDLE		0xa102
#define HID_GET_PROTOCOL	0xa103
#define HID_SET_IDLE		0x210a
#define HID_SET_PROTOCOL	0x210b

typedef struct USBWacomState {
    USBDevice dev;
    QEMUPutMouseEntry *eh_entry;
    int dx, dy, dz, buttons_state;
    int x, y;
    int mouse_grabbed;
    enum {
        WACOM_MODE_HID = 1,
        WACOM_MODE_WACOM = 2,
    } mode;
    uint8_t idle;
    int changed;
} USBWacomState;

enum {
    STR_MANUFACTURER = 1,
    STR_PRODUCT,
    STR_SERIALNUMBER,
};

static const USBDescStrings desc_strings = {
    [STR_MANUFACTURER]     = "QEMU " QEMU_VERSION,
    [STR_PRODUCT]          = "Wacom PenPartner",
    [STR_SERIALNUMBER]     = "1",
};

static const USBDescIface desc_iface_wacom = {
    .bInterfaceNumber              = 0,
    .bNumEndpoints                 = 1,
    .bInterfaceClass               = USB_CLASS_HID,
    .bInterfaceSubClass            = 0x01, /* boot */
    .bInterfaceProtocol            = 0x02,
    .ndesc                         = 1,
    .descs = (USBDescOther[]) {
        {
            /* HID descriptor */
            .data = (uint8_t[]) {
                0x09,          /*  u8  bLength */
                0x21,          /*  u8  bDescriptorType */
                0x01, 0x10,    /*  u16 HID_class */
                0x00,          /*  u8  country_code */
                0x01,          /*  u8  num_descriptors */
                0x22,          /*  u8  type: Report */
                0x6e, 0,       /*  u16 len */
            },
        },
    },
    .eps = (USBDescEndpoint[]) {
        {
            .bEndpointAddress      = USB_DIR_IN | 0x01,
            .bmAttributes          = USB_ENDPOINT_XFER_INT,
            .wMaxPacketSize        = 8,
            .bInterval             = 0x0a,
        },
    },
};

static const USBDescDevice desc_device_wacom = {
    .bcdUSB                        = 0x0110,
    .bMaxPacketSize0               = 8,
    .bNumConfigurations            = 1,
    .confs = (USBDescConfig[]) {
        {
            .bNumInterfaces        = 1,
            .bConfigurationValue   = 1,
            .bmAttributes          = 0x80,
            .bMaxPower             = 40,
            .ifs = &desc_iface_wacom,
        },
    },
};

static const USBDesc desc_wacom = {
    .id = {
        .idVendor          = 0x056a,
        .idProduct         = 0x0000,
        .bcdDevice         = 0x4210,
        .iManufacturer     = STR_MANUFACTURER,
        .iProduct          = STR_PRODUCT,
        .iSerialNumber     = STR_SERIALNUMBER,
    },
    .full = &desc_device_wacom,
    .str  = desc_strings,
};

static void usb_mouse_event(void *opaque,
                            int dx1, int dy1, int dz1, int buttons_state)
{
    USBWacomState *s = opaque;

    s->dx += dx1;
    s->dy += dy1;
    s->dz += dz1;
    s->buttons_state = buttons_state;
    s->changed = 1;
}

static void usb_wacom_event(void *opaque,
                            int x, int y, int dz, int buttons_state)
{
    USBWacomState *s = opaque;

    /* scale to Penpartner resolution */
    s->x = (x * 5040 / 0x7FFF);
    s->y = (y * 3780 / 0x7FFF);
    s->dz += dz;
    s->buttons_state = buttons_state;
    s->changed = 1;
}

static inline int int_clamp(int val, int vmin, int vmax)
{
    if (val < vmin)
        return vmin;
    else if (val > vmax)
        return vmax;
    else
        return val;
}

static int usb_mouse_poll(USBWacomState *s, uint8_t *buf, int len)
{
    int dx, dy, dz, b, l;

    if (!s->mouse_grabbed) {
        s->eh_entry = qemu_add_mouse_event_handler(usb_mouse_event, s, 0,
                        "QEMU PenPartner tablet");
        qemu_activate_mouse_event_handler(s->eh_entry);
        s->mouse_grabbed = 1;
    }

    dx = int_clamp(s->dx, -128, 127);
    dy = int_clamp(s->dy, -128, 127);
    dz = int_clamp(s->dz, -128, 127);

    s->dx -= dx;
    s->dy -= dy;
    s->dz -= dz;

    b = 0;
    if (s->buttons_state & MOUSE_EVENT_LBUTTON)
        b |= 0x01;
    if (s->buttons_state & MOUSE_EVENT_RBUTTON)
        b |= 0x02;
    if (s->buttons_state & MOUSE_EVENT_MBUTTON)
        b |= 0x04;

    buf[0] = b;
    buf[1] = dx;
    buf[2] = dy;
    l = 3;
    if (len >= 4) {
        buf[3] = dz;
        l = 4;
    }
    return l;
}

static int usb_wacom_poll(USBWacomState *s, uint8_t *buf, int len)
{
    int b;

    if (!s->mouse_grabbed) {
        s->eh_entry = qemu_add_mouse_event_handler(usb_wacom_event, s, 1,
                        "QEMU PenPartner tablet");
        qemu_activate_mouse_event_handler(s->eh_entry);
        s->mouse_grabbed = 1;
    }

    b = 0;
    if (s->buttons_state & MOUSE_EVENT_LBUTTON)
        b |= 0x01;
    if (s->buttons_state & MOUSE_EVENT_RBUTTON)
        b |= 0x40;
    if (s->buttons_state & MOUSE_EVENT_MBUTTON)
        b |= 0x20; /* eraser */

    if (len < 7)
        return 0;

    buf[0] = s->mode;
    buf[5] = 0x00 | (b & 0xf0);
    buf[1] = s->x & 0xff;
    buf[2] = s->x >> 8;
    buf[3] = s->y & 0xff;
    buf[4] = s->y >> 8;
    if (b & 0x3f) {
        buf[6] = 0;
    } else {
        buf[6] = (unsigned char) -127;
    }

    return 7;
}

static void usb_wacom_handle_reset(USBDevice *dev)
{
    USBWacomState *s = (USBWacomState *) dev;

    s->dx = 0;
    s->dy = 0;
    s->dz = 0;
    s->x = 0;
    s->y = 0;
    s->buttons_state = 0;
    s->mode = WACOM_MODE_HID;
}

static int usb_wacom_handle_control(USBDevice *dev, int request, int value,
                                    int index, int length, uint8_t *data)
{
    USBWacomState *s = (USBWacomState *) dev;
    int ret;

    ret = usb_desc_handle_control(dev, request, value, index, length, data);
    if (ret >= 0) {
        return ret;
    }

    ret = 0;
    switch (request) {
    case DeviceRequest | USB_REQ_GET_INTERFACE:
        data[0] = 0;
        ret = 1;
        break;
    case DeviceOutRequest | USB_REQ_SET_INTERFACE:
        ret = 0;
        break;
    case WACOM_SET_REPORT:
        if (s->mouse_grabbed) {
            qemu_remove_mouse_event_handler(s->eh_entry);
            s->mouse_grabbed = 0;
        }
        s->mode = data[0];
        ret = 0;
        break;
    case WACOM_GET_REPORT:
        data[0] = 0;
        data[1] = s->mode;
        ret = 2;
        break;
    /* USB HID requests */
    case HID_GET_REPORT:
        if (s->mode == WACOM_MODE_HID)
            ret = usb_mouse_poll(s, data, length);
        else if (s->mode == WACOM_MODE_WACOM)
            ret = usb_wacom_poll(s, data, length);
        break;
    case HID_GET_IDLE:
        ret = 1;
        data[0] = s->idle;
        break;
    case HID_SET_IDLE:
        s->idle = (uint8_t) (value >> 8);
        ret = 0;
        break;
    default:
        ret = USB_RET_STALL;
        break;
    }
    return ret;
}

static int usb_wacom_handle_data(USBDevice *dev, USBPacket *p)
{
    USBWacomState *s = (USBWacomState *) dev;
    int ret = 0;

    switch (p->pid) {
    case USB_TOKEN_IN:
        if (p->devep == 1) {
            if (!(s->changed || s->idle))
                return USB_RET_NAK;
            s->changed = 0;
            if (s->mode == WACOM_MODE_HID)
                ret = usb_mouse_poll(s, p->data, p->len);
            else if (s->mode == WACOM_MODE_WACOM)
                ret = usb_wacom_poll(s, p->data, p->len);
            break;
        }
        /* Fall through.  */
    case USB_TOKEN_OUT:
    default:
        ret = USB_RET_STALL;
        break;
    }
    return ret;
}

static void usb_wacom_handle_destroy(USBDevice *dev)
{
    USBWacomState *s = (USBWacomState *) dev;

    if (s->mouse_grabbed) {
        qemu_remove_mouse_event_handler(s->eh_entry);
        s->mouse_grabbed = 0;
    }
}

static int usb_wacom_initfn(USBDevice *dev)
{
    USBWacomState *s = DO_UPCAST(USBWacomState, dev, dev);
    usb_desc_init(dev);
    s->changed = 1;
    return 0;
}

static struct USBDeviceInfo wacom_info = {
    .product_desc   = "QEMU PenPartner Tablet",
    .qdev.name      = "usb-wacom-tablet",
    .qdev.desc      = "QEMU PenPartner Tablet",
    .usbdevice_name = "wacom-tablet",
    .usb_desc       = &desc_wacom,
    .qdev.size      = sizeof(USBWacomState),
    .init           = usb_wacom_initfn,
    .handle_packet  = usb_generic_handle_packet,
    .handle_reset   = usb_wacom_handle_reset,
    .handle_control = usb_wacom_handle_control,
    .handle_data    = usb_wacom_handle_data,
    .handle_destroy = usb_wacom_handle_destroy,
};

static void usb_wacom_register_devices(void)
{
    usb_qdev_register(&wacom_info);
}
device_init(usb_wacom_register_devices)
