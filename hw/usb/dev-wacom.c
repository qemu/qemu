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

#include "qemu/osdep.h"
#include "ui/console.h"
#include "hw/usb/usb.h"
#include "hw/usb/hid.h"
#include "migration/vmstate.h"
#include "qemu/module.h"
#include "desc.h"
#include "qom/object.h"

/* Interface requests */
#define WACOM_GET_REPORT    0x2101
#define WACOM_SET_REPORT    0x2109

struct USBWacomState {
    USBDevice dev;
    USBEndpoint *intr;
    QemuInputHandlerState *hs;
    int axis[INPUT_AXIS__MAX];
    int dz;
    bool btns[INPUT_BUTTON__MAX];
    enum {
        WACOM_MODE_HID = 1,
        WACOM_MODE_WACOM = 2,
    } mode;
    uint8_t idle;
    int changed;
};

#define TYPE_USB_WACOM "usb-wacom-tablet"
OBJECT_DECLARE_SIMPLE_TYPE(USBWacomState, USB_WACOM)

enum {
    STR_MANUFACTURER = 1,
    STR_PRODUCT,
    STR_SERIALNUMBER,
};

static const USBDescStrings desc_strings = {
    [STR_MANUFACTURER]     = "QEMU",
    [STR_PRODUCT]          = "Wacom PenPartner",
    [STR_SERIALNUMBER]     = "1",
};

static const uint8_t qemu_wacom_hid_report_descriptor[] = {
    0x05, 0x01,      /* Usage Page (Desktop) */
    0x09, 0x02,      /* Usage (Mouse) */
    0xa1, 0x01,      /* Collection (Application) */
    0x85, 0x01,      /*    Report ID (1) */
    0x09, 0x01,      /*    Usage (Pointer) */
    0xa1, 0x00,      /*    Collection (Physical) */
    0x05, 0x09,      /*       Usage Page (Button) */
    0x19, 0x01,      /*       Usage Minimum (01h) */
    0x29, 0x03,      /*       Usage Maximum (03h) */
    0x15, 0x00,      /*       Logical Minimum (0) */
    0x25, 0x01,      /*       Logical Maximum (1) */
    0x95, 0x03,      /*       Report Count (3) */
    0x75, 0x01,      /*       Report Size (1) */
    0x81, 0x02,      /*       Input (Data, Variable, Absolute) */
    0x95, 0x01,      /*       Report Count (1) */
    0x75, 0x05,      /*       Report Size (5) */
    0x81, 0x01,      /*       Input (Constant) */
    0x05, 0x01,      /*       Usage Page (Desktop) */
    0x09, 0x30,      /*       Usage (X) */
    0x09, 0x31,      /*       Usage (Y) */
    0x09, 0x38,      /*       Usage (Wheel) */
    0x15, 0x81,      /*       Logical Minimum (-127) */
    0x25, 0x7f,      /*       Logical Maximum (127) */
    0x75, 0x08,      /*       Report Size (8) */
    0x95, 0x03,      /*       Report Count (3) */
    0x81, 0x06,      /*       Input (Data, Variable, Relative) */
    0x95, 0x03,      /*       Report Count (3) */
    0x81, 0x01,      /*       Input (Constant) */
    0xc0,            /*    End Collection */
    0xc0,            /* End Collection */
    0x05, 0x0d,      /* Usage Page (Digitizer) */
    0x09, 0x01,      /* Usage (Digitizer) */
    0xa1, 0x01,      /* Collection (Application) */
    0x85, 0x02,      /*    Report ID (2) */
    0xa1, 0x00,      /*    Collection (Physical) */
    0x06, 0x00, 0xff,/*       Usage Page (ff00h), vendor-defined */
    0x09, 0x01,      /*       Usage (01h) */
    0x15, 0x00,      /*       Logical Minimum (0) */
    0x26, 0xff, 0x00,/*       Logical Maximum (255) */
    0x75, 0x08,      /*       Report Size (8) */
    0x95, 0x07,      /*       Report Count (7) */
    0x81, 0x02,      /*       Input (Data, Variable, Absolute) */
    0xc0,            /*    End Collection */
    0x09, 0x01,      /*    Usage (01h) */
    0x85, 0x63,      /*    Report ID (99) */
    0x95, 0x07,      /*    Report Count (7) */
    0x81, 0x02,      /*    Input (Data, Variable, Absolute) */
    0x09, 0x01,      /*    Usage (01h) */
    0x85, 0x02,      /*    Report ID (2) */
    0x95, 0x01,      /*    Report Count (1) */
    0xb1, 0x02,      /*    Feature (Variable) */
    0x09, 0x01,      /*    Usage (01h) */
    0x85, 0x03,      /*    Report ID (3) */
    0x95, 0x01,      /*    Report Count (1) */
    0xb1, 0x02,      /*    Feature (Variable) */
    0xc0             /* End Collection */
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
                USB_DT_HID,    /*  u8  bDescriptorType */
                0x01, 0x10,    /*  u16 HID_class */
                0x00,          /*  u8  country_code */
                0x01,          /*  u8  num_descriptors */
                USB_DT_REPORT, /*  u8  type: Report */
                sizeof(qemu_wacom_hid_report_descriptor), 0, /*  u16 len */
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
            .bmAttributes          = USB_CFG_ATT_ONE,
            .bMaxPower             = 40,
            .nif = 1,
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

static void usb_wacom_input_event(DeviceState *dev, QemuConsole *src,
                                  QemuInputEvent *evt)
{
    USBWacomState *s = USB_WACOM(dev);

    switch (evt->type) {
    case INPUT_EVENT_KIND_BTN:
        if (evt->btn.down) {
            if (evt->btn.button == INPUT_BUTTON_WHEEL_UP) {
                s->dz--;
            } else if (evt->btn.button == INPUT_BUTTON_WHEEL_DOWN) {
                s->dz++;
            }
        }
        s->btns[evt->btn.button] = evt->btn.down;
        break;
    case INPUT_EVENT_KIND_ABS:
        s->axis[evt->abs.axis] = evt->abs.value;
        break;
    case INPUT_EVENT_KIND_REL:
        s->axis[evt->rel.axis] += evt->rel.value;
        break;
    default:
        break;
    }
}

static void usb_wacom_input_sync(DeviceState *dev)
{
    USBWacomState *s = USB_WACOM(dev);

    s->changed = 1;
    usb_wakeup(s->intr, 0);
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

static void usb_wacom_register_input_handler(USBWacomState *s, bool absolute)
{
    static const QemuInputHandler usb_wacom_abs_handler = {
        .name  = "QEMU PenPartner tablet",
        .mask  = INPUT_EVENT_MASK_BTN | INPUT_EVENT_MASK_ABS,
        .event = usb_wacom_input_event,
        .sync  = usb_wacom_input_sync,
    };

    static const QemuInputHandler usb_wacom_rel_handler = {
        .name  = "QEMU PenPartner tablet",
        .mask  = INPUT_EVENT_MASK_BTN | INPUT_EVENT_MASK_REL,
        .event = usb_wacom_input_event,
        .sync  = usb_wacom_input_sync,
    };

    const QemuInputHandler *h = absolute ?
        &usb_wacom_abs_handler : &usb_wacom_rel_handler;

    g_clear_pointer(&s->hs, qemu_input_handler_unregister);

    s->hs = qemu_input_handler_register(DEVICE(s), h);
    qemu_input_handler_activate(s->hs);
}

static int usb_mouse_poll(USBWacomState *s, uint8_t *buf, int len)
{
    int dx, dy, dz, b, l;

    if (!s->hs) {
        usb_wacom_register_input_handler(s, false);
    }

    dx = int_clamp(s->axis[INPUT_AXIS_X], -128, 127);
    dy = int_clamp(s->axis[INPUT_AXIS_Y], -128, 127);
    dz = int_clamp(s->dz, -128, 127);

    s->axis[INPUT_AXIS_X] -= dx;
    s->axis[INPUT_AXIS_Y] -= dy;
    s->dz -= dz;

    b = 0;
    if (s->btns[INPUT_BUTTON_LEFT]) {
        b |= 0x01;
    }
    if (s->btns[INPUT_BUTTON_RIGHT]) {
        b |= 0x02;
    }
    if (s->btns[INPUT_BUTTON_MIDDLE]) {
        b |= 0x04;
    }

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
    int b, x, y;

    if (!s->hs) {
        usb_wacom_register_input_handler(s, true);
    }

    b = 0;
    if (s->btns[INPUT_BUTTON_LEFT]) {
        b |= 0x01;
    }
    if (s->btns[INPUT_BUTTON_RIGHT]) {
        b |= 0x40;
    }
    if (s->btns[INPUT_BUTTON_MIDDLE]) {
        b |= 0x20; /* eraser */
    }

    if (len < 7) {
        return 0;
    }

    x = qemu_input_scale_axis(s->axis[INPUT_AXIS_X],
                              INPUT_EVENT_ABS_MIN, INPUT_EVENT_ABS_MAX,
                              0, 5040);
    y = qemu_input_scale_axis(s->axis[INPUT_AXIS_Y],
                              INPUT_EVENT_ABS_MIN, INPUT_EVENT_ABS_MAX,
                              0, 3780);

    buf[0] = s->mode;
    buf[5] = 0x00 | (b & 0xf0);
    buf[1] = x & 0xff;
    buf[2] = x >> 8;
    buf[3] = y & 0xff;
    buf[4] = y >> 8;
    if (b & 0x3f) {
        buf[6] = 0;
    } else {
        buf[6] = (unsigned char) -127;
    }

    return 7;
}

static void usb_wacom_handle_reset(USBDevice *dev)
{
    USBWacomState *s = USB_WACOM(dev);

    memset(s->axis, 0, sizeof(s->axis));
    memset(s->btns, 0, sizeof(s->btns));
    s->dz = 0;
    s->mode = WACOM_MODE_HID;
    g_clear_pointer(&s->hs, qemu_input_handler_unregister);
}

static void usb_wacom_handle_control(USBDevice *dev, USBPacket *p,
               int request, int value, int index, int length, uint8_t *data)
{
    USBWacomState *s = (USBWacomState *) dev;
    int ret;

    ret = usb_desc_handle_control(dev, p, request, value, index, length, data);
    if (ret >= 0) {
        return;
    }

    switch (request) {
    case InterfaceRequest | USB_REQ_GET_DESCRIPTOR:
        switch (value >> 8) {
        case 0x22:
                memcpy(data, qemu_wacom_hid_report_descriptor,
                       sizeof(qemu_wacom_hid_report_descriptor));
                p->actual_length = sizeof(qemu_wacom_hid_report_descriptor);
            break;
        default:
            return;
        }
        break;
    case WACOM_SET_REPORT:
        g_clear_pointer(&s->hs, qemu_input_handler_unregister);
        s->mode = data[0];
        break;
    case WACOM_GET_REPORT:
        data[0] = 0;
        data[1] = s->mode;
        p->actual_length = 2;
        break;
    /* USB HID requests */
    case HID_GET_REPORT:
        if (s->mode == WACOM_MODE_HID)
            p->actual_length = usb_mouse_poll(s, data, length);
        else if (s->mode == WACOM_MODE_WACOM)
            p->actual_length = usb_wacom_poll(s, data, length);
        break;
    case HID_GET_IDLE:
        data[0] = s->idle;
        p->actual_length = 1;
        break;
    case HID_SET_IDLE:
        s->idle = (uint8_t) (value >> 8);
        break;
    default:
        p->status = USB_RET_STALL;
        break;
    }
}

static void usb_wacom_handle_data(USBDevice *dev, USBPacket *p)
{
    USBWacomState *s = (USBWacomState *) dev;
    g_autofree uint8_t *buf = g_malloc(p->iov.size);
    int len = 0;

    switch (p->pid) {
    case USB_TOKEN_IN:
        if (p->ep->nr == 1) {
            if (!(s->changed || s->idle)) {
                p->status = USB_RET_NAK;
                return;
            }
            s->changed = 0;
            if (s->mode == WACOM_MODE_HID)
                len = usb_mouse_poll(s, buf, p->iov.size);
            else if (s->mode == WACOM_MODE_WACOM)
                len = usb_wacom_poll(s, buf, p->iov.size);
            usb_packet_copy(p, buf, len);
            break;
        }
        /* Fall through.  */
    case USB_TOKEN_OUT:
    default:
        p->status = USB_RET_STALL;
    }
}

static void usb_wacom_unrealize(USBDevice *dev)
{
    USBWacomState *s = (USBWacomState *) dev;

    g_clear_pointer(&s->hs, qemu_input_handler_unregister);
}

static void usb_wacom_realize(USBDevice *dev, Error **errp)
{
    USBWacomState *s = USB_WACOM(dev);
    usb_desc_create_serial(dev);
    usb_desc_init(dev);
    s->intr = usb_ep_get(dev, USB_TOKEN_IN, 1);
    s->changed = 1;
}

static const VMStateDescription vmstate_usb_wacom = {
    .name = "usb-wacom",
    .unmigratable = 1,
};

static void usb_wacom_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    USBDeviceClass *uc = USB_DEVICE_CLASS(klass);

    uc->product_desc   = "QEMU PenPartner Tablet";
    uc->usb_desc       = &desc_wacom;
    uc->realize        = usb_wacom_realize;
    uc->handle_reset   = usb_wacom_handle_reset;
    uc->handle_control = usb_wacom_handle_control;
    uc->handle_data    = usb_wacom_handle_data;
    uc->unrealize      = usb_wacom_unrealize;
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
    dc->desc = "QEMU PenPartner Tablet";
    dc->vmsd = &vmstate_usb_wacom;
}

static const TypeInfo wacom_info = {
    .name          = TYPE_USB_WACOM,
    .parent        = TYPE_USB_DEVICE,
    .instance_size = sizeof(USBWacomState),
    .class_init    = usb_wacom_class_init,
};

static void usb_wacom_register_types(void)
{
    type_register_static(&wacom_info);
    usb_legacy_register(TYPE_USB_WACOM, "wacom-tablet", NULL);
}

type_init(usb_wacom_register_types)
