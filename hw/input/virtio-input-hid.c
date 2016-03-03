/*
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/iov.h"

#include "hw/qdev.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-input.h"

#undef CONFIG_CURSES
#include "ui/console.h"

#include "standard-headers/linux/input.h"

#define VIRTIO_ID_NAME_KEYBOARD "QEMU Virtio Keyboard"
#define VIRTIO_ID_NAME_MOUSE    "QEMU Virtio Mouse"
#define VIRTIO_ID_NAME_TABLET   "QEMU Virtio Tablet"

/* ----------------------------------------------------------------- */

static const unsigned int keymap_qcode[Q_KEY_CODE__MAX] = {
    [Q_KEY_CODE_ESC]                 = KEY_ESC,
    [Q_KEY_CODE_1]                   = KEY_1,
    [Q_KEY_CODE_2]                   = KEY_2,
    [Q_KEY_CODE_3]                   = KEY_3,
    [Q_KEY_CODE_4]                   = KEY_4,
    [Q_KEY_CODE_5]                   = KEY_5,
    [Q_KEY_CODE_6]                   = KEY_6,
    [Q_KEY_CODE_7]                   = KEY_7,
    [Q_KEY_CODE_8]                   = KEY_8,
    [Q_KEY_CODE_9]                   = KEY_9,
    [Q_KEY_CODE_0]                   = KEY_0,
    [Q_KEY_CODE_MINUS]               = KEY_MINUS,
    [Q_KEY_CODE_EQUAL]               = KEY_EQUAL,
    [Q_KEY_CODE_BACKSPACE]           = KEY_BACKSPACE,

    [Q_KEY_CODE_TAB]                 = KEY_TAB,
    [Q_KEY_CODE_Q]                   = KEY_Q,
    [Q_KEY_CODE_W]                   = KEY_W,
    [Q_KEY_CODE_E]                   = KEY_E,
    [Q_KEY_CODE_R]                   = KEY_R,
    [Q_KEY_CODE_T]                   = KEY_T,
    [Q_KEY_CODE_Y]                   = KEY_Y,
    [Q_KEY_CODE_U]                   = KEY_U,
    [Q_KEY_CODE_I]                   = KEY_I,
    [Q_KEY_CODE_O]                   = KEY_O,
    [Q_KEY_CODE_P]                   = KEY_P,
    [Q_KEY_CODE_BRACKET_LEFT]        = KEY_LEFTBRACE,
    [Q_KEY_CODE_BRACKET_RIGHT]       = KEY_RIGHTBRACE,
    [Q_KEY_CODE_RET]                 = KEY_ENTER,

    [Q_KEY_CODE_CTRL]                = KEY_LEFTCTRL,
    [Q_KEY_CODE_A]                   = KEY_A,
    [Q_KEY_CODE_S]                   = KEY_S,
    [Q_KEY_CODE_D]                   = KEY_D,
    [Q_KEY_CODE_F]                   = KEY_F,
    [Q_KEY_CODE_G]                   = KEY_G,
    [Q_KEY_CODE_H]                   = KEY_H,
    [Q_KEY_CODE_J]                   = KEY_J,
    [Q_KEY_CODE_K]                   = KEY_K,
    [Q_KEY_CODE_L]                   = KEY_L,
    [Q_KEY_CODE_SEMICOLON]           = KEY_SEMICOLON,
    [Q_KEY_CODE_APOSTROPHE]          = KEY_APOSTROPHE,
    [Q_KEY_CODE_GRAVE_ACCENT]        = KEY_GRAVE,

    [Q_KEY_CODE_SHIFT]               = KEY_LEFTSHIFT,
    [Q_KEY_CODE_BACKSLASH]           = KEY_BACKSLASH,
    [Q_KEY_CODE_LESS]                = KEY_102ND,
    [Q_KEY_CODE_Z]                   = KEY_Z,
    [Q_KEY_CODE_X]                   = KEY_X,
    [Q_KEY_CODE_C]                   = KEY_C,
    [Q_KEY_CODE_V]                   = KEY_V,
    [Q_KEY_CODE_B]                   = KEY_B,
    [Q_KEY_CODE_N]                   = KEY_N,
    [Q_KEY_CODE_M]                   = KEY_M,
    [Q_KEY_CODE_COMMA]               = KEY_COMMA,
    [Q_KEY_CODE_DOT]                 = KEY_DOT,
    [Q_KEY_CODE_SLASH]               = KEY_SLASH,
    [Q_KEY_CODE_SHIFT_R]             = KEY_RIGHTSHIFT,

    [Q_KEY_CODE_ALT]                 = KEY_LEFTALT,
    [Q_KEY_CODE_SPC]                 = KEY_SPACE,
    [Q_KEY_CODE_CAPS_LOCK]           = KEY_CAPSLOCK,

    [Q_KEY_CODE_F1]                  = KEY_F1,
    [Q_KEY_CODE_F2]                  = KEY_F2,
    [Q_KEY_CODE_F3]                  = KEY_F3,
    [Q_KEY_CODE_F4]                  = KEY_F4,
    [Q_KEY_CODE_F5]                  = KEY_F5,
    [Q_KEY_CODE_F6]                  = KEY_F6,
    [Q_KEY_CODE_F7]                  = KEY_F7,
    [Q_KEY_CODE_F8]                  = KEY_F8,
    [Q_KEY_CODE_F9]                  = KEY_F9,
    [Q_KEY_CODE_F10]                 = KEY_F10,
    [Q_KEY_CODE_NUM_LOCK]            = KEY_NUMLOCK,
    [Q_KEY_CODE_SCROLL_LOCK]         = KEY_SCROLLLOCK,

    [Q_KEY_CODE_KP_0]                = KEY_KP0,
    [Q_KEY_CODE_KP_1]                = KEY_KP1,
    [Q_KEY_CODE_KP_2]                = KEY_KP2,
    [Q_KEY_CODE_KP_3]                = KEY_KP3,
    [Q_KEY_CODE_KP_4]                = KEY_KP4,
    [Q_KEY_CODE_KP_5]                = KEY_KP5,
    [Q_KEY_CODE_KP_6]                = KEY_KP6,
    [Q_KEY_CODE_KP_7]                = KEY_KP7,
    [Q_KEY_CODE_KP_8]                = KEY_KP8,
    [Q_KEY_CODE_KP_9]                = KEY_KP9,
    [Q_KEY_CODE_KP_SUBTRACT]         = KEY_KPMINUS,
    [Q_KEY_CODE_KP_ADD]              = KEY_KPPLUS,
    [Q_KEY_CODE_KP_DECIMAL]          = KEY_KPDOT,
    [Q_KEY_CODE_KP_ENTER]            = KEY_KPENTER,
    [Q_KEY_CODE_KP_DIVIDE]           = KEY_KPSLASH,
    [Q_KEY_CODE_KP_MULTIPLY]         = KEY_KPASTERISK,

    [Q_KEY_CODE_F11]                 = KEY_F11,
    [Q_KEY_CODE_F12]                 = KEY_F12,

    [Q_KEY_CODE_CTRL_R]              = KEY_RIGHTCTRL,
    [Q_KEY_CODE_SYSRQ]               = KEY_SYSRQ,
    [Q_KEY_CODE_ALT_R]               = KEY_RIGHTALT,

    [Q_KEY_CODE_HOME]                = KEY_HOME,
    [Q_KEY_CODE_UP]                  = KEY_UP,
    [Q_KEY_CODE_PGUP]                = KEY_PAGEUP,
    [Q_KEY_CODE_LEFT]                = KEY_LEFT,
    [Q_KEY_CODE_RIGHT]               = KEY_RIGHT,
    [Q_KEY_CODE_END]                 = KEY_END,
    [Q_KEY_CODE_DOWN]                = KEY_DOWN,
    [Q_KEY_CODE_PGDN]                = KEY_PAGEDOWN,
    [Q_KEY_CODE_INSERT]              = KEY_INSERT,
    [Q_KEY_CODE_DELETE]              = KEY_DELETE,

    [Q_KEY_CODE_META_L]              = KEY_LEFTMETA,
    [Q_KEY_CODE_META_R]              = KEY_RIGHTMETA,
    [Q_KEY_CODE_MENU]                = KEY_MENU,
};

static const unsigned int keymap_button[INPUT_BUTTON__MAX] = {
    [INPUT_BUTTON_LEFT]              = BTN_LEFT,
    [INPUT_BUTTON_RIGHT]             = BTN_RIGHT,
    [INPUT_BUTTON_MIDDLE]            = BTN_MIDDLE,
    [INPUT_BUTTON_WHEEL_UP]          = BTN_GEAR_UP,
    [INPUT_BUTTON_WHEEL_DOWN]        = BTN_GEAR_DOWN,
};

static const unsigned int axismap_rel[INPUT_AXIS__MAX] = {
    [INPUT_AXIS_X]                   = REL_X,
    [INPUT_AXIS_Y]                   = REL_Y,
};

static const unsigned int axismap_abs[INPUT_AXIS__MAX] = {
    [INPUT_AXIS_X]                   = ABS_X,
    [INPUT_AXIS_Y]                   = ABS_Y,
};

/* ----------------------------------------------------------------- */

static void virtio_input_key_config(VirtIOInput *vinput,
                                    const unsigned int *keymap,
                                    size_t mapsize)
{
    virtio_input_config keys;
    int i, bit, byte, bmax = 0;

    memset(&keys, 0, sizeof(keys));
    for (i = 0; i < mapsize; i++) {
        bit = keymap[i];
        if (!bit) {
            continue;
        }
        byte = bit / 8;
        bit  = bit % 8;
        keys.u.bitmap[byte] |= (1 << bit);
        if (bmax < byte+1) {
            bmax = byte+1;
        }
    }
    keys.select = VIRTIO_INPUT_CFG_EV_BITS;
    keys.subsel = EV_KEY;
    keys.size   = bmax;
    virtio_input_add_config(vinput, &keys);
}

static void virtio_input_handle_event(DeviceState *dev, QemuConsole *src,
                                      InputEvent *evt)
{
    VirtIOInput *vinput = VIRTIO_INPUT(dev);
    virtio_input_event event;
    int qcode;
    InputKeyEvent *key;
    InputMoveEvent *move;
    InputBtnEvent *btn;

    switch (evt->type) {
    case INPUT_EVENT_KIND_KEY:
        key = evt->u.key;
        qcode = qemu_input_key_value_to_qcode(key->key);
        if (qcode && keymap_qcode[qcode]) {
            event.type  = cpu_to_le16(EV_KEY);
            event.code  = cpu_to_le16(keymap_qcode[qcode]);
            event.value = cpu_to_le32(key->down ? 1 : 0);
            virtio_input_send(vinput, &event);
        } else {
            if (key->down) {
                fprintf(stderr, "%s: unmapped key: %d [%s]\n", __func__,
                        qcode, QKeyCode_lookup[qcode]);
            }
        }
        break;
    case INPUT_EVENT_KIND_BTN:
        btn = evt->u.btn;
        if (keymap_button[btn->button]) {
            event.type  = cpu_to_le16(EV_KEY);
            event.code  = cpu_to_le16(keymap_button[btn->button]);
            event.value = cpu_to_le32(btn->down ? 1 : 0);
            virtio_input_send(vinput, &event);
        } else {
            if (btn->down) {
                fprintf(stderr, "%s: unmapped button: %d [%s]\n", __func__,
                        btn->button,
                        InputButton_lookup[btn->button]);
            }
        }
        break;
    case INPUT_EVENT_KIND_REL:
        move = evt->u.rel;
        event.type  = cpu_to_le16(EV_REL);
        event.code  = cpu_to_le16(axismap_rel[move->axis]);
        event.value = cpu_to_le32(move->value);
        virtio_input_send(vinput, &event);
        break;
    case INPUT_EVENT_KIND_ABS:
        move = evt->u.abs;
        event.type  = cpu_to_le16(EV_ABS);
        event.code  = cpu_to_le16(axismap_abs[move->axis]);
        event.value = cpu_to_le32(move->value);
        virtio_input_send(vinput, &event);
        break;
    default:
        /* keep gcc happy */
        break;
    }
}

static void virtio_input_handle_sync(DeviceState *dev)
{
    VirtIOInput *vinput = VIRTIO_INPUT(dev);
    virtio_input_event event = {
        .type  = cpu_to_le16(EV_SYN),
        .code  = cpu_to_le16(SYN_REPORT),
        .value = 0,
    };

    virtio_input_send(vinput, &event);
}

static void virtio_input_hid_realize(DeviceState *dev, Error **errp)
{
    VirtIOInputHID *vhid = VIRTIO_INPUT_HID(dev);

    vhid->hs = qemu_input_handler_register(dev, vhid->handler);
    if (vhid->display && vhid->hs) {
        qemu_input_handler_bind(vhid->hs, vhid->display, vhid->head, NULL);
    }
}

static void virtio_input_hid_unrealize(DeviceState *dev, Error **errp)
{
    VirtIOInputHID *vhid = VIRTIO_INPUT_HID(dev);
    qemu_input_handler_unregister(vhid->hs);
}

static void virtio_input_hid_change_active(VirtIOInput *vinput)
{
    VirtIOInputHID *vhid = VIRTIO_INPUT_HID(vinput);

    if (vinput->active) {
        qemu_input_handler_activate(vhid->hs);
    } else {
        qemu_input_handler_deactivate(vhid->hs);
    }
}

static void virtio_input_hid_handle_status(VirtIOInput *vinput,
                                           virtio_input_event *event)
{
    VirtIOInputHID *vhid = VIRTIO_INPUT_HID(vinput);
    int ledbit = 0;

    switch (le16_to_cpu(event->type)) {
    case EV_LED:
        if (event->code == LED_NUML) {
            ledbit = QEMU_NUM_LOCK_LED;
        } else if (event->code == LED_CAPSL) {
            ledbit = QEMU_CAPS_LOCK_LED;
        } else if (event->code == LED_SCROLLL) {
            ledbit = QEMU_SCROLL_LOCK_LED;
        }
        if (event->value) {
            vhid->ledstate |= ledbit;
        } else {
            vhid->ledstate &= ~ledbit;
        }
        kbd_put_ledstate(vhid->ledstate);
        break;
    default:
        fprintf(stderr, "%s: unknown type %d\n", __func__,
                le16_to_cpu(event->type));
        break;
    }
}

static Property virtio_input_hid_properties[] = {
    DEFINE_PROP_STRING("display", VirtIOInputHID, display),
    DEFINE_PROP_UINT32("head", VirtIOInputHID, head, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_input_hid_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtIOInputClass *vic = VIRTIO_INPUT_CLASS(klass);

    dc->props          = virtio_input_hid_properties;
    vic->realize       = virtio_input_hid_realize;
    vic->unrealize     = virtio_input_hid_unrealize;
    vic->change_active = virtio_input_hid_change_active;
    vic->handle_status = virtio_input_hid_handle_status;
}

static const TypeInfo virtio_input_hid_info = {
    .name          = TYPE_VIRTIO_INPUT_HID,
    .parent        = TYPE_VIRTIO_INPUT,
    .instance_size = sizeof(VirtIOInputHID),
    .class_init    = virtio_input_hid_class_init,
    .abstract      = true,
};

/* ----------------------------------------------------------------- */

static QemuInputHandler virtio_keyboard_handler = {
    .name  = VIRTIO_ID_NAME_KEYBOARD,
    .mask  = INPUT_EVENT_MASK_KEY,
    .event = virtio_input_handle_event,
    .sync  = virtio_input_handle_sync,
};

static struct virtio_input_config virtio_keyboard_config[] = {
    {
        .select    = VIRTIO_INPUT_CFG_ID_NAME,
        .size      = sizeof(VIRTIO_ID_NAME_KEYBOARD),
        .u.string  = VIRTIO_ID_NAME_KEYBOARD,
    },{
        .select    = VIRTIO_INPUT_CFG_ID_DEVIDS,
        .size      = sizeof(struct virtio_input_devids),
        .u.ids     = {
            .bustype = const_le16(BUS_VIRTUAL),
            .vendor  = const_le16(0x0627), /* same we use for usb hid devices */
            .product = const_le16(0x0001),
            .version = const_le16(0x0001),
        },
    },{
        .select    = VIRTIO_INPUT_CFG_EV_BITS,
        .subsel    = EV_REP,
        .size      = 1,
    },{
        .select    = VIRTIO_INPUT_CFG_EV_BITS,
        .subsel    = EV_LED,
        .size      = 1,
        .u.bitmap  = {
            (1 << LED_NUML) | (1 << LED_CAPSL) | (1 << LED_SCROLLL),
        },
    },
    { /* end of list */ },
};

static void virtio_keyboard_init(Object *obj)
{
    VirtIOInputHID *vhid = VIRTIO_INPUT_HID(obj);
    VirtIOInput *vinput = VIRTIO_INPUT(obj);

    vhid->handler = &virtio_keyboard_handler;
    virtio_input_init_config(vinput, virtio_keyboard_config);
    virtio_input_key_config(vinput, keymap_qcode,
                            ARRAY_SIZE(keymap_qcode));
}

static const TypeInfo virtio_keyboard_info = {
    .name          = TYPE_VIRTIO_KEYBOARD,
    .parent        = TYPE_VIRTIO_INPUT_HID,
    .instance_size = sizeof(VirtIOInputHID),
    .instance_init = virtio_keyboard_init,
};

/* ----------------------------------------------------------------- */

static QemuInputHandler virtio_mouse_handler = {
    .name  = VIRTIO_ID_NAME_MOUSE,
    .mask  = INPUT_EVENT_MASK_BTN | INPUT_EVENT_MASK_REL,
    .event = virtio_input_handle_event,
    .sync  = virtio_input_handle_sync,
};

static struct virtio_input_config virtio_mouse_config[] = {
    {
        .select    = VIRTIO_INPUT_CFG_ID_NAME,
        .size      = sizeof(VIRTIO_ID_NAME_MOUSE),
        .u.string  = VIRTIO_ID_NAME_MOUSE,
    },{
        .select    = VIRTIO_INPUT_CFG_ID_DEVIDS,
        .size      = sizeof(struct virtio_input_devids),
        .u.ids     = {
            .bustype = const_le16(BUS_VIRTUAL),
            .vendor  = const_le16(0x0627), /* same we use for usb hid devices */
            .product = const_le16(0x0002),
            .version = const_le16(0x0001),
        },
    },{
        .select    = VIRTIO_INPUT_CFG_EV_BITS,
        .subsel    = EV_REL,
        .size      = 1,
        .u.bitmap  = {
            (1 << REL_X) | (1 << REL_Y),
        },
    },
    { /* end of list */ },
};

static void virtio_mouse_init(Object *obj)
{
    VirtIOInputHID *vhid = VIRTIO_INPUT_HID(obj);
    VirtIOInput *vinput = VIRTIO_INPUT(obj);

    vhid->handler = &virtio_mouse_handler;
    virtio_input_init_config(vinput, virtio_mouse_config);
    virtio_input_key_config(vinput, keymap_button,
                            ARRAY_SIZE(keymap_button));
}

static const TypeInfo virtio_mouse_info = {
    .name          = TYPE_VIRTIO_MOUSE,
    .parent        = TYPE_VIRTIO_INPUT_HID,
    .instance_size = sizeof(VirtIOInputHID),
    .instance_init = virtio_mouse_init,
};

/* ----------------------------------------------------------------- */

static QemuInputHandler virtio_tablet_handler = {
    .name  = VIRTIO_ID_NAME_TABLET,
    .mask  = INPUT_EVENT_MASK_BTN | INPUT_EVENT_MASK_ABS,
    .event = virtio_input_handle_event,
    .sync  = virtio_input_handle_sync,
};

static struct virtio_input_config virtio_tablet_config[] = {
    {
        .select    = VIRTIO_INPUT_CFG_ID_NAME,
        .size      = sizeof(VIRTIO_ID_NAME_TABLET),
        .u.string  = VIRTIO_ID_NAME_TABLET,
    },{
        .select    = VIRTIO_INPUT_CFG_ID_DEVIDS,
        .size      = sizeof(struct virtio_input_devids),
        .u.ids     = {
            .bustype = const_le16(BUS_VIRTUAL),
            .vendor  = const_le16(0x0627), /* same we use for usb hid devices */
            .product = const_le16(0x0003),
            .version = const_le16(0x0001),
        },
    },{
        .select    = VIRTIO_INPUT_CFG_EV_BITS,
        .subsel    = EV_ABS,
        .size      = 1,
        .u.bitmap  = {
            (1 << ABS_X) | (1 << ABS_Y),
        },
    },{
        .select    = VIRTIO_INPUT_CFG_ABS_INFO,
        .subsel    = ABS_X,
        .size      = sizeof(virtio_input_absinfo),
        .u.abs.max = const_le32(INPUT_EVENT_ABS_SIZE),
    },{
        .select    = VIRTIO_INPUT_CFG_ABS_INFO,
        .subsel    = ABS_Y,
        .size      = sizeof(virtio_input_absinfo),
        .u.abs.max = const_le32(INPUT_EVENT_ABS_SIZE),
    },
    { /* end of list */ },
};

static void virtio_tablet_init(Object *obj)
{
    VirtIOInputHID *vhid = VIRTIO_INPUT_HID(obj);
    VirtIOInput *vinput = VIRTIO_INPUT(obj);

    vhid->handler = &virtio_tablet_handler;
    virtio_input_init_config(vinput, virtio_tablet_config);
    virtio_input_key_config(vinput, keymap_button,
                            ARRAY_SIZE(keymap_button));
}

static const TypeInfo virtio_tablet_info = {
    .name          = TYPE_VIRTIO_TABLET,
    .parent        = TYPE_VIRTIO_INPUT_HID,
    .instance_size = sizeof(VirtIOInputHID),
    .instance_init = virtio_tablet_init,
};

/* ----------------------------------------------------------------- */

static void virtio_register_types(void)
{
    type_register_static(&virtio_input_hid_info);
    type_register_static(&virtio_keyboard_info);
    type_register_static(&virtio_mouse_info);
    type_register_static(&virtio_tablet_info);
}

type_init(virtio_register_types)
