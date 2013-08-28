/*
 * QEMU USB XID Devices
 *
 * Copyright (c) 2013 espes
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "hw/hw.h"
#include "hw/usb.h"
#include "hw/usb/desc.h"

/*
 * http://xbox-linux.cvs.sourceforge.net/viewvc/xbox-linux/kernel-2.6/drivers/usb/input/xpad.c
 * http://euc.jp/periphs/xbox-controller.en.html
 * http://euc.jp/periphs/xbox-pad-desc.txt
 */

#define USB_CLASS_XID  0x58
#define USB_DT_XID     0x42


#define HID_GET_REPORT       0x01
#define HID_SET_REPORT       0x09
#define XID_GET_CAPABILITIES 0x01

typedef struct USBXIDState {
    USBDevice dev;
    USBEndpoint *intr;
} USBXIDState;

static const USBDescIface desc_iface_xbox_gamepad = {
    .bInterfaceNumber              = 0,
    .bNumEndpoints                 = 2,
    .bInterfaceClass               = USB_CLASS_XID,
    .bInterfaceSubClass            = 0x42,
    .bInterfaceProtocol            = 0x00,
    .ndesc                         = 1,
    .descs = (USBDescOther[]) {
        {
            /* XID descriptor */
            .data = (uint8_t[]) {
                0x09,          /*  u8  bLength */
                USB_DT_XID,    /*  u8  bDescriptorType */
                0x00, 0x01,    /*  u16 bcdXid */
                0x01,          /*  u8  bType */
                0x01,          /*  u8  bSubType */
                0x20,          /*  u8  bMaxInputReportSize */
                0x06,          /*  u8  bMaxOutputReportSize */
                0xff, 0xff,    /*  u16 wAlternateProductIds[4] */
                0xff, 0xff,
                0xff, 0xff,
                0xff, 0xff,
            },
        },
    },
    .eps = (USBDescEndpoint[]) {
        {
            .bEndpointAddress      = USB_DIR_IN | 0x02,
            .bmAttributes          = USB_ENDPOINT_XFER_INT,
            .wMaxPacketSize        = 0x20,
            .bInterval             = 4,
        },
        {
            .bEndpointAddress      = USB_DIR_OUT | 0x02,
            .bmAttributes          = USB_ENDPOINT_XFER_INT,
            .wMaxPacketSize        = 0x20,
            .bInterval             = 4,
        },
    },
};

static const USBDescDevice desc_device_xbox_gamepad = {
    .bcdUSB                        = 0x0110,
    .bMaxPacketSize0               = 0x40,
    .bNumConfigurations            = 1,
    .confs = (USBDescConfig[]) {
        {
            .bNumInterfaces        = 1,
            .bConfigurationValue   = 1,
            .bmAttributes          = 0x80,
            .bMaxPower             = 50,
            .nif = 1,
            .ifs = &desc_iface_xbox_gamepad,
        },
    },
};

static const USBDesc desc_xbox_gamepad = {
    .id = {
        .idVendor          = 0x045e,
        .idProduct         = 0x0202,
        .bcdDevice         = 0x0100,
    },
    .full = &desc_device_xbox_gamepad,
};


static void usb_xid_handle_reset(USBDevice *dev)
{
    USBXIDState *s = DO_UPCAST(USBXIDState, dev, dev);
}

static void usb_xid_handle_control(USBDevice *dev, USBPacket *p,
               int request, int value, int index, int length, uint8_t *data)
{
    USBXIDState *s = DO_UPCAST(USBXIDState, dev, dev);
    int ret;

    ret = usb_desc_handle_control(dev, p, request, value, index, length, data);
    if (ret >= 0) {
        return;
    }

    switch (request) {
    /* HID requests */
    case ClassInterfaceRequest | HID_GET_REPORT:
        printf("xid GET_REPORT\n");
        break;
    case ClassInterfaceOutRequest | HID_SET_REPORT:
        printf("xid SET_REPORT\n");
        break;
    /* XID requests */
    case InterfaceRequestVendor | USB_REQ_GET_DESCRIPTOR:
        printf("xid GET_DESCRIPTOR %x\n", value);
        break;
    case InterfaceRequestVendor | XID_GET_CAPABILITIES:
        printf("xid XID_GET_CAPABILITIES %x\n", value);
        break;
    default:
        p->status = USB_RET_STALL;
        assert(false);
        break;
    }
}

static void usb_xid_handle_data(USBDevice *dev, USBPacket *p)
{
    printf("xid handle_data\n");
}

static void usb_xid_handle_destroy(USBDevice *dev)
{
    printf("xid handle_destroy\n");
}

static void usb_xid_class_initfn(ObjectClass *klass, void *data)
{
    USBDeviceClass *uc = USB_DEVICE_CLASS(klass);

    uc->handle_reset   = usb_xid_handle_reset;
    uc->handle_control = usb_xid_handle_control;
    uc->handle_data    = usb_xid_handle_data;
    uc->handle_destroy = usb_xid_handle_destroy;
    uc->handle_attach  = usb_desc_attach;
}

static int usb_xbox_gamepad_initfn(USBDevice *dev)
{
    USBXIDState *s = DO_UPCAST(USBXIDState, dev, dev);
    usb_desc_init(dev);
    s->intr = usb_ep_get(dev, USB_TOKEN_IN, 2);
    printf("xid usb_xbox_gamepad_initfn\n");

    return 0;
}

static void usb_xbox_gamepad_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    USBDeviceClass *uc = USB_DEVICE_CLASS(klass);

    usb_xid_class_initfn(klass, data);
    uc->init           = usb_xbox_gamepad_initfn;
    uc->product_desc   = "Microsoft Xbox Controller";
    uc->usb_desc       = &desc_xbox_gamepad;
    //dc->vmsd = &vmstate_usb_kbd;
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
}

static const TypeInfo usb_xbox_gamepad_info = {
    .name          = "usb-xbox-gamepad",
    .parent        = TYPE_USB_DEVICE,
    .instance_size = sizeof(USBXIDState),
    .class_init    = usb_xbox_gamepad_class_initfn,
};

static void usb_xid_register_types(void)
{
    type_register_static(&usb_xbox_gamepad_info);
}

type_init(usb_xid_register_types)
