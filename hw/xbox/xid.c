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
#include "ui/console.h"
#include "hw/usb.h"
#include "hw/usb/desc.h"

//#define DEBUG_XID
#ifdef DEBUG_XID
#define DPRINTF printf
#else
#define DPRINTF(...)
#endif

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



typedef struct XIDDesc {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t bcdXid;
    uint8_t bType;
    uint8_t bSubType;
    uint8_t bMaxInputReportSize;
    uint8_t bMaxOutputReportSize;
    uint16_t wAlternateProductIds[4];
} QEMU_PACKED XIDDesc;

typedef struct XIDGamepadReport {
    uint8_t bReportId;
    uint8_t bLength;
    uint16_t wButtons;
    uint8_t bAnalogButtons[8];
    int16_t sThumbLX;
    int16_t sThumbLY;
    int16_t sThumbRX;
    int16_t sThumbRY;
} QEMU_PACKED XIDGamepadReport;



typedef struct USBXIDState {
    USBDevice dev;
    USBEndpoint *intr;

    const XIDDesc *xid_desc;

    QEMUPutKbdEntry *kbd_entry;
    XIDGamepadReport in_state;
} USBXIDState;

static const USBDescIface desc_iface_xbox_gamepad = {
    .bInterfaceNumber              = 0,
    .bNumEndpoints                 = 2,
    .bInterfaceClass               = USB_CLASS_XID,
    .bInterfaceSubClass            = 0x42,
    .bInterfaceProtocol            = 0x00,
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

static const XIDDesc desc_xid_xbox_gamepad = {
    .bLength = 0x10,
    .bDescriptorType = USB_DT_XID,
    .bcdXid = 1,
    .bType = 1,
    .bSubType = 1,
    .bMaxInputReportSize = 0x20,
    .bMaxOutputReportSize = 0x6,
    .wAlternateProductIds = {-1, -1, -1, -1},
};


#define GAMEPAD_A                0
#define GAMEPAD_B                1
#define GAMEPAD_X                2
#define GAMEPAD_Y                3
#define GAMEPAD_BLACK            4
#define GAMEPAD_WHITE            5
#define GAMEPAD_LEFT_TRIGGER     6
#define GAMEPAD_RIGHT_TRIGGER    7

#define GAMEPAD_DPAD_UP          8
#define GAMEPAD_DPAD_DOWN        9
#define GAMEPAD_DPAD_LEFT        10
#define GAMEPAD_DPAD_RIGHT       11
#define GAMEPAD_START            12
#define GAMEPAD_BACK             13
#define GAMEPAD_LEFT_THUMB       14
#define GAMEPAD_RIGHT_THUMB      15

static const int gamepad_mapping[] = {
    [0 ... Q_KEY_CODE_MAX] = -1,

    [Q_KEY_CODE_UP]    = GAMEPAD_DPAD_UP,
    [Q_KEY_CODE_KP_8]  = GAMEPAD_DPAD_UP,
    [Q_KEY_CODE_DOWN]  = GAMEPAD_DPAD_DOWN,
    [Q_KEY_CODE_KP_2]  = GAMEPAD_DPAD_DOWN,
    [Q_KEY_CODE_LEFT]  = GAMEPAD_DPAD_LEFT,
    [Q_KEY_CODE_KP_4]  = GAMEPAD_DPAD_LEFT,
    [Q_KEY_CODE_RIGHT] = GAMEPAD_DPAD_RIGHT,
    [Q_KEY_CODE_KP_6]  = GAMEPAD_DPAD_RIGHT,

    [Q_KEY_CODE_RET]   = GAMEPAD_START,
    [Q_KEY_CODE_BACKSPACE] = GAMEPAD_BACK,

    [Q_KEY_CODE_Z]     = GAMEPAD_A,
    [Q_KEY_CODE_X]     = GAMEPAD_B,
    [Q_KEY_CODE_A]     = GAMEPAD_X,
    [Q_KEY_CODE_S]     = GAMEPAD_Y,
};

static void xbox_gamepad_keyboard_event(void *opaque, int keycode)
{
    USBXIDState *s = opaque;

    bool up = keycode & 0x80;
    QKeyCode code = index_from_keycode(keycode & 0x7f);
    if (code >= Q_KEY_CODE_MAX) return;

    int button = gamepad_mapping[code];

    DPRINTF("xid keyboard_event %x - %d %d %d\n", keycode, code, button, up);

    uint16_t mask;
    switch (button) {
    case GAMEPAD_A ... GAMEPAD_RIGHT_TRIGGER:
        s->in_state.bAnalogButtons[button] = up?0:0xff;
        break;
    case GAMEPAD_DPAD_UP ... GAMEPAD_RIGHT_THUMB:
        mask = (1 << (button-GAMEPAD_DPAD_UP));
        s->in_state.wButtons &= ~mask;
        if (!up) s->in_state.wButtons |= mask;
        break;
    default:
        break;
    }
}


static void usb_xid_handle_reset(USBDevice *dev)
{
    DPRINTF("xid reset\n");
}

static void usb_xid_handle_control(USBDevice *dev, USBPacket *p,
               int request, int value, int index, int length, uint8_t *data)
{
    USBXIDState *s = DO_UPCAST(USBXIDState, dev, dev);

    DPRINTF("xid handle_control %x %x\n", request, value);

    int ret = usb_desc_handle_control(dev, p, request, value, index, length, data);
    if (ret >= 0) {
        return;
    }

    switch (request) {
    /* HID requests */
    case ClassInterfaceRequest | HID_GET_REPORT:
        DPRINTF("xid GET_REPORT %x\n", value);
        if (value == 0x100) { /* input */
            assert(s->in_state.bLength <= length);
            memcpy(data, &s->in_state, s->in_state.bLength);
            p->actual_length = s->in_state.bLength;
        } else {
            assert(false);
        }
        break;
    case ClassInterfaceOutRequest | HID_SET_REPORT:
        DPRINTF("xid SET_REPORT %x\n", value);
        assert(false);
        break;
    /* XID requests */
    case InterfaceRequestVendor | USB_REQ_GET_DESCRIPTOR:
        DPRINTF("xid GET_DESCRIPTOR %x\n", value);
        if (value == 0x4200) {
            assert(s->xid_desc->bLength <= length);
            memcpy(data, s->xid_desc, s->xid_desc->bLength);
            p->actual_length = s->xid_desc->bLength;
        } else {
            assert(false);
        }
        break;
    case InterfaceRequestVendor | XID_GET_CAPABILITIES:
        DPRINTF("xid XID_GET_CAPABILITIES %x\n", value);
        assert(false);
        break;
    default:
        p->status = USB_RET_STALL;
        assert(false);
        break;
    }
}

static void usb_xid_handle_data(USBDevice *dev, USBPacket *p)
{
    USBXIDState *s = DO_UPCAST(USBXIDState, dev, dev);

    DPRINTF("xid handle_data %x %d %zx\n", p->pid, p->ep->nr, p->iov.size);

    switch (p->pid) {
    case USB_TOKEN_IN:
        if (p->ep->nr == 2) {
            usb_packet_copy(p, &s->in_state, s->in_state.bLength);
        } else {
            assert(false);
        }
        break;
    default:
        assert(false);
        break;
    }
}

static void usb_xid_handle_destroy(USBDevice *dev)
{
    DPRINTF("xid handle_destroy\n");
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

    s->in_state.bLength = sizeof(s->in_state);
    s->kbd_entry = qemu_add_kbd_event_handler(xbox_gamepad_keyboard_event, s);
    s->xid_desc = &desc_xid_xbox_gamepad;

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
