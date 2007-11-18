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
} USBWacomState;

static const uint8_t qemu_wacom_dev_descriptor[] = {
    0x12,	/*  u8 bLength; */
    0x01,	/*  u8 bDescriptorType; Device */
    0x10, 0x10,	/*  u16 bcdUSB; v1.10 */

    0x00,	/*  u8  bDeviceClass; */
    0x00,	/*  u8  bDeviceSubClass; */
    0x00,	/*  u8  bDeviceProtocol; [ low/full speeds only ] */
    0x08,	/*  u8  bMaxPacketSize0; 8 Bytes */

    0x6a, 0x05,	/*  u16 idVendor; */
    0x00, 0x00,	/*  u16 idProduct; */
    0x10, 0x42,	/*  u16 bcdDevice */

    0x01,	/*  u8  iManufacturer; */
    0x02,	/*  u8  iProduct; */
    0x00,	/*  u8  iSerialNumber; */
    0x01,	/*  u8  bNumConfigurations; */
};

static const uint8_t qemu_wacom_config_descriptor[] = {
    /* one configuration */
    0x09,	/*  u8  bLength; */
    0x02,	/*  u8  bDescriptorType; Configuration */
    0x22, 0x00,	/*  u16 wTotalLength; */
    0x01,	/*  u8  bNumInterfaces; (1) */
    0x01,	/*  u8  bConfigurationValue; */
    0x00,	/*  u8  iConfiguration; */
    0x80,	/*  u8  bmAttributes;
				 Bit 7: must be set,
				     6: Self-powered,
				     5: Remote wakeup,
				     4..0: resvd */
    40,		/*  u8  MaxPower; */

    /* one interface */
    0x09,	/*  u8  if_bLength; */
    0x04,	/*  u8  if_bDescriptorType; Interface */
    0x00,	/*  u8  if_bInterfaceNumber; */
    0x00,	/*  u8  if_bAlternateSetting; */
    0x01,	/*  u8  if_bNumEndpoints; */
    0x03,	/*  u8  if_bInterfaceClass; HID */
    0x01,	/*  u8  if_bInterfaceSubClass; Boot */
    0x02,	/*  u8  if_bInterfaceProtocol; [usb1.1 or single tt] */
    0x00,	/*  u8  if_iInterface; */

    /* HID descriptor */
    0x09,	/*  u8  bLength; */
    0x21,	/*  u8  bDescriptorType; */
    0x01, 0x10,	/*  u16 HID_class */
    0x00,	/*  u8  country_code */
    0x01,	/*  u8  num_descriptors */
    0x22,	/*  u8  type; Report */
    0x6e, 0x00,	/*  u16 len */

    /* one endpoint (status change endpoint) */
    0x07,	/*  u8  ep_bLength; */
    0x05,	/*  u8  ep_bDescriptorType; Endpoint */
    0x81,	/*  u8  ep_bEndpointAddress; IN Endpoint 1 */
    0x03,	/*  u8  ep_bmAttributes; Interrupt */
    0x08, 0x00,	/*  u16 ep_wMaxPacketSize; */
    0x0a,	/*  u8  ep_bInterval; */
};

static void usb_mouse_event(void *opaque,
                            int dx1, int dy1, int dz1, int buttons_state)
{
    USBWacomState *s = opaque;

    s->dx += dx1;
    s->dy += dy1;
    s->dz += dz1;
    s->buttons_state = buttons_state;
}

static void usb_wacom_event(void *opaque,
                            int x, int y, int dz, int buttons_state)
{
    USBWacomState *s = opaque;

    s->x = x;
    s->y = y;
    s->dz += dz;
    s->buttons_state = buttons_state;
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
        s->mouse_grabbed = 1;
    }

    b = 0;
    if (s->buttons_state & MOUSE_EVENT_LBUTTON)
        b |= 0x01;
    if (s->buttons_state & MOUSE_EVENT_RBUTTON)
        b |= 0x02;
    if (s->buttons_state & MOUSE_EVENT_MBUTTON)
        b |= 0x04;

    if (len < 7)
        return 0;

    buf[0] = s->mode;
    buf[5] = 0x00;
    if (b) {
        buf[1] = s->x & 0xff;
        buf[2] = s->x >> 8;
        buf[3] = s->y & 0xff;
        buf[4] = s->y >> 8;
        buf[6] = 0;
    } else {
        buf[1] = 0;
        buf[2] = 0;
        buf[3] = 0;
        buf[4] = 0;
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
    int ret = 0;

    switch (request) {
    case DeviceRequest | USB_REQ_GET_STATUS:
        data[0] = (1 << USB_DEVICE_SELF_POWERED) |
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
        switch (value >> 8) {
        case USB_DT_DEVICE:
            memcpy(data, qemu_wacom_dev_descriptor,
                   sizeof(qemu_wacom_dev_descriptor));
            ret = sizeof(qemu_wacom_dev_descriptor);
            break;
        case USB_DT_CONFIG:
       	    memcpy(data, qemu_wacom_config_descriptor,
                   sizeof(qemu_wacom_config_descriptor));
            ret = sizeof(qemu_wacom_config_descriptor);
            break;
        case USB_DT_STRING:
            switch (value & 0xff) {
            case 0:
                /* language ids */
                data[0] = 4;
                data[1] = 3;
                data[2] = 0x09;
                data[3] = 0x04;
                ret = 4;
                break;
            case 1:
                /* serial number */
                ret = set_usb_string(data, "1");
                break;
            case 2:
		ret = set_usb_string(data, "Wacom PenPartner");
                break;
            case 3:
                /* vendor description */
                ret = set_usb_string(data, "QEMU " QEMU_VERSION);
                break;
            case 4:
                ret = set_usb_string(data, "Wacom Tablet");
                break;
            case 5:
                ret = set_usb_string(data, "Endpoint1 Interrupt Pipe");
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
    case DeviceOutRequest | USB_REQ_SET_INTERFACE:
        ret = 0;
        break;
    case WACOM_SET_REPORT:
        qemu_remove_mouse_event_handler(s->eh_entry);
        s->mouse_grabbed = 0;
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
    case HID_SET_IDLE:
        ret = 0;
        break;
    default:
    fail:
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

    qemu_remove_mouse_event_handler(s->eh_entry);
    qemu_free(s);
}

USBDevice *usb_wacom_init(void)
{
    USBWacomState *s;

    s = qemu_mallocz(sizeof(USBWacomState));
    if (!s)
        return NULL;
    s->dev.speed = USB_SPEED_FULL;
    s->dev.handle_packet = usb_generic_handle_packet;

    s->dev.handle_reset = usb_wacom_handle_reset;
    s->dev.handle_control = usb_wacom_handle_control;
    s->dev.handle_data = usb_wacom_handle_data;
    s->dev.handle_destroy = usb_wacom_handle_destroy;

    pstrcpy(s->dev.devname, sizeof(s->dev.devname),
            "QEMU PenPartner Tablet");

    return (USBDevice *) s;
}
