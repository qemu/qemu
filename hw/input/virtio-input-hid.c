/*
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/iov.h"
#include "qemu/module.h"

#include "hw/virtio/virtio.h"
#include "hw/qdev-properties.h"
#include "hw/virtio/virtio-input.h"

#include "ui/console.h"

#include "standard-headers/linux/input.h"

#define VIRTIO_ID_NAME_KEYBOARD     "QEMU Virtio Keyboard"
#define VIRTIO_ID_NAME_MOUSE        "QEMU Virtio Mouse"
#define VIRTIO_ID_NAME_TABLET       "QEMU Virtio Tablet"
#define VIRTIO_ID_NAME_MULTITOUCH   "QEMU Virtio MultiTouch"

/* ----------------------------------------------------------------- */

static const unsigned short keymap_button[INPUT_BUTTON__MAX] = {
    [INPUT_BUTTON_LEFT]              = BTN_LEFT,
    [INPUT_BUTTON_RIGHT]             = BTN_RIGHT,
    [INPUT_BUTTON_MIDDLE]            = BTN_MIDDLE,
    [INPUT_BUTTON_WHEEL_UP]          = BTN_GEAR_UP,
    [INPUT_BUTTON_WHEEL_DOWN]        = BTN_GEAR_DOWN,
    [INPUT_BUTTON_SIDE]              = BTN_SIDE,
    [INPUT_BUTTON_EXTRA]             = BTN_EXTRA,
    [INPUT_BUTTON_TOUCH]             = BTN_TOUCH,
};

static const unsigned short axismap_rel[INPUT_AXIS__MAX] = {
    [INPUT_AXIS_X]                   = REL_X,
    [INPUT_AXIS_Y]                   = REL_Y,
};

static const unsigned short axismap_abs[INPUT_AXIS__MAX] = {
    [INPUT_AXIS_X]                   = ABS_X,
    [INPUT_AXIS_Y]                   = ABS_Y,
};

static const unsigned short axismap_tch[INPUT_AXIS__MAX] = {
    [INPUT_AXIS_X]                   = ABS_MT_POSITION_X,
    [INPUT_AXIS_Y]                   = ABS_MT_POSITION_Y,
};

/* ----------------------------------------------------------------- */

static void virtio_input_extend_config(VirtIOInput *vinput,
                                       const unsigned short *map,
                                       size_t mapsize,
                                       uint8_t select, uint8_t subsel)
{
    virtio_input_config ext;
    int i, bit, byte, bmax = 0;

    memset(&ext, 0, sizeof(ext));
    for (i = 0; i < mapsize; i++) {
        bit = map[i];
        if (!bit) {
            continue;
        }
        byte = bit / 8;
        bit  = bit % 8;
        ext.u.bitmap[byte] |= (1 << bit);
        if (bmax < byte+1) {
            bmax = byte+1;
        }
    }
    ext.select = select;
    ext.subsel = subsel;
    ext.size   = bmax;
    virtio_input_add_config(vinput, &ext);
}

static void virtio_input_handle_event(DeviceState *dev, QemuConsole *src,
                                      InputEvent *evt)
{
    VirtIOInputHID *vhid = VIRTIO_INPUT_HID(dev);
    VirtIOInput *vinput = VIRTIO_INPUT(dev);
    virtio_input_event event;
    int qcode;
    InputKeyEvent *key;
    InputMoveEvent *move;
    InputBtnEvent *btn;
    InputMultiTouchEvent *mtt;

    switch (evt->type) {
    case INPUT_EVENT_KIND_KEY:
        key = evt->u.key.data;
        qcode = qemu_input_key_value_to_qcode(key->key);
        if (qcode < qemu_input_map_qcode_to_linux_len &&
            qemu_input_map_qcode_to_linux[qcode]) {
            event.type  = cpu_to_le16(EV_KEY);
            event.code  = cpu_to_le16(qemu_input_map_qcode_to_linux[qcode]);
            event.value = cpu_to_le32(key->down ? 1 : 0);
            virtio_input_send(vinput, &event);
        } else {
            if (key->down) {
                fprintf(stderr, "%s: unmapped key: %d [%s]\n", __func__,
                        qcode, QKeyCode_str(qcode));
            }
        }
        break;
    case INPUT_EVENT_KIND_BTN:
        btn = evt->u.btn.data;
        if (vhid->wheel_axis &&
            (btn->button == INPUT_BUTTON_WHEEL_UP ||
             btn->button == INPUT_BUTTON_WHEEL_DOWN) &&
            btn->down) {
            event.type  = cpu_to_le16(EV_REL);
            event.code  = cpu_to_le16(REL_WHEEL);
            event.value = cpu_to_le32(btn->button == INPUT_BUTTON_WHEEL_UP
                                      ? 1 : -1);
            virtio_input_send(vinput, &event);
        } else if (keymap_button[btn->button]) {
            event.type  = cpu_to_le16(EV_KEY);
            event.code  = cpu_to_le16(keymap_button[btn->button]);
            event.value = cpu_to_le32(btn->down ? 1 : 0);
            virtio_input_send(vinput, &event);
        } else {
            if (btn->down) {
                fprintf(stderr, "%s: unmapped button: %d [%s]\n", __func__,
                        btn->button,
                        InputButton_str(btn->button));
            }
        }
        break;
    case INPUT_EVENT_KIND_REL:
        move = evt->u.rel.data;
        event.type  = cpu_to_le16(EV_REL);
        event.code  = cpu_to_le16(axismap_rel[move->axis]);
        event.value = cpu_to_le32(move->value);
        virtio_input_send(vinput, &event);
        break;
    case INPUT_EVENT_KIND_ABS:
        move = evt->u.abs.data;
        event.type  = cpu_to_le16(EV_ABS);
        event.code  = cpu_to_le16(axismap_abs[move->axis]);
        event.value = cpu_to_le32(move->value);
        virtio_input_send(vinput, &event);
        break;
    case INPUT_EVENT_KIND_MTT:
        mtt = evt->u.mtt.data;
        if (mtt->type == INPUT_MULTI_TOUCH_TYPE_DATA) {
            event.type  = cpu_to_le16(EV_ABS);
            event.code  = cpu_to_le16(axismap_tch[mtt->axis]);
            event.value = cpu_to_le32(mtt->value);
            virtio_input_send(vinput, &event);
        } else {
            event.type  = cpu_to_le16(EV_ABS);
            event.code  = cpu_to_le16(ABS_MT_SLOT);
            event.value = cpu_to_le32(mtt->slot);
            virtio_input_send(vinput, &event);
            event.type  = cpu_to_le16(EV_ABS);
            event.code  = cpu_to_le16(ABS_MT_TRACKING_ID);
            event.value = cpu_to_le32(mtt->tracking_id);
            virtio_input_send(vinput, &event);
        }
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

static void virtio_input_hid_unrealize(DeviceState *dev)
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

    device_class_set_props(dc, virtio_input_hid_properties);
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

static const QemuInputHandler virtio_keyboard_handler = {
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
    virtio_input_extend_config(vinput, qemu_input_map_qcode_to_linux,
                               qemu_input_map_qcode_to_linux_len,
                               VIRTIO_INPUT_CFG_EV_BITS, EV_KEY);
}

static const TypeInfo virtio_keyboard_info = {
    .name          = TYPE_VIRTIO_KEYBOARD,
    .parent        = TYPE_VIRTIO_INPUT_HID,
    .instance_size = sizeof(VirtIOInputHID),
    .instance_init = virtio_keyboard_init,
};

/* ----------------------------------------------------------------- */

static const QemuInputHandler virtio_mouse_handler = {
    .name  = VIRTIO_ID_NAME_MOUSE,
    .mask  = INPUT_EVENT_MASK_BTN | INPUT_EVENT_MASK_REL,
    .event = virtio_input_handle_event,
    .sync  = virtio_input_handle_sync,
};

static struct virtio_input_config virtio_mouse_config_v1[] = {
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

static struct virtio_input_config virtio_mouse_config_v2[] = {
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
            .version = const_le16(0x0002),
        },
    },{
        .select    = VIRTIO_INPUT_CFG_EV_BITS,
        .subsel    = EV_REL,
        .size      = 2,
        .u.bitmap  = {
            (1 << REL_X) | (1 << REL_Y),
            (1 << (REL_WHEEL - 8))
        },
    },
    { /* end of list */ },
};

static Property virtio_mouse_properties[] = {
    DEFINE_PROP_BOOL("wheel-axis", VirtIOInputHID, wheel_axis, true),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_mouse_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, virtio_mouse_properties);
}

static void virtio_mouse_init(Object *obj)
{
    VirtIOInputHID *vhid = VIRTIO_INPUT_HID(obj);
    VirtIOInput *vinput = VIRTIO_INPUT(obj);

    vhid->handler = &virtio_mouse_handler;
    virtio_input_init_config(vinput, vhid->wheel_axis
                             ? virtio_mouse_config_v2
                             : virtio_mouse_config_v1);
    virtio_input_extend_config(vinput, keymap_button,
                               ARRAY_SIZE(keymap_button),
                               VIRTIO_INPUT_CFG_EV_BITS, EV_KEY);
}

static const TypeInfo virtio_mouse_info = {
    .name          = TYPE_VIRTIO_MOUSE,
    .parent        = TYPE_VIRTIO_INPUT_HID,
    .instance_size = sizeof(VirtIOInputHID),
    .instance_init = virtio_mouse_init,
    .class_init    = virtio_mouse_class_init,
};

/* ----------------------------------------------------------------- */

static const QemuInputHandler virtio_tablet_handler = {
    .name  = VIRTIO_ID_NAME_TABLET,
    .mask  = INPUT_EVENT_MASK_BTN | INPUT_EVENT_MASK_ABS,
    .event = virtio_input_handle_event,
    .sync  = virtio_input_handle_sync,
};

static struct virtio_input_config virtio_tablet_config_v1[] = {
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
        .u.abs.min = const_le32(INPUT_EVENT_ABS_MIN),
        .u.abs.max = const_le32(INPUT_EVENT_ABS_MAX),
    },{
        .select    = VIRTIO_INPUT_CFG_ABS_INFO,
        .subsel    = ABS_Y,
        .size      = sizeof(virtio_input_absinfo),
        .u.abs.min = const_le32(INPUT_EVENT_ABS_MIN),
        .u.abs.max = const_le32(INPUT_EVENT_ABS_MAX),
    },
    { /* end of list */ },
};

static struct virtio_input_config virtio_tablet_config_v2[] = {
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
            .version = const_le16(0x0002),
        },
    },{
        .select    = VIRTIO_INPUT_CFG_EV_BITS,
        .subsel    = EV_ABS,
        .size      = 1,
        .u.bitmap  = {
            (1 << ABS_X) | (1 << ABS_Y),
        },
    },{
        .select    = VIRTIO_INPUT_CFG_EV_BITS,
        .subsel    = EV_REL,
        .size      = 2,
        .u.bitmap  = {
            0,
            (1 << (REL_WHEEL - 8))
        },
    },{
        .select    = VIRTIO_INPUT_CFG_ABS_INFO,
        .subsel    = ABS_X,
        .size      = sizeof(virtio_input_absinfo),
        .u.abs.min = const_le32(INPUT_EVENT_ABS_MIN),
        .u.abs.max = const_le32(INPUT_EVENT_ABS_MAX),
    },{
        .select    = VIRTIO_INPUT_CFG_ABS_INFO,
        .subsel    = ABS_Y,
        .size      = sizeof(virtio_input_absinfo),
        .u.abs.min = const_le32(INPUT_EVENT_ABS_MIN),
        .u.abs.max = const_le32(INPUT_EVENT_ABS_MAX),
    },
    { /* end of list */ },
};

static Property virtio_tablet_properties[] = {
    DEFINE_PROP_BOOL("wheel-axis", VirtIOInputHID, wheel_axis, true),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_tablet_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, virtio_tablet_properties);
}

static void virtio_tablet_init(Object *obj)
{
    VirtIOInputHID *vhid = VIRTIO_INPUT_HID(obj);
    VirtIOInput *vinput = VIRTIO_INPUT(obj);

    vhid->handler = &virtio_tablet_handler;
    virtio_input_init_config(vinput, vhid->wheel_axis
                             ? virtio_tablet_config_v2
                             : virtio_tablet_config_v1);
    virtio_input_extend_config(vinput, keymap_button,
                               ARRAY_SIZE(keymap_button),
                               VIRTIO_INPUT_CFG_EV_BITS, EV_KEY);
}

static const TypeInfo virtio_tablet_info = {
    .name          = TYPE_VIRTIO_TABLET,
    .parent        = TYPE_VIRTIO_INPUT_HID,
    .instance_size = sizeof(VirtIOInputHID),
    .instance_init = virtio_tablet_init,
    .class_init    = virtio_tablet_class_init,
};

/* ----------------------------------------------------------------- */

static const QemuInputHandler virtio_multitouch_handler = {
    .name  = VIRTIO_ID_NAME_MULTITOUCH,
    .mask  = INPUT_EVENT_MASK_BTN | INPUT_EVENT_MASK_MTT,
    .event = virtio_input_handle_event,
    .sync  = virtio_input_handle_sync,
};

static struct virtio_input_config virtio_multitouch_config[] = {
    {
        .select    = VIRTIO_INPUT_CFG_ID_NAME,
        .size      = sizeof(VIRTIO_ID_NAME_MULTITOUCH),
        .u.string  = VIRTIO_ID_NAME_MULTITOUCH,
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
        .select    = VIRTIO_INPUT_CFG_ABS_INFO,
        .subsel    = ABS_MT_SLOT,
        .size      = sizeof(virtio_input_absinfo),
        .u.abs.min = const_le32(INPUT_EVENT_SLOTS_MIN),
        .u.abs.max = const_le32(INPUT_EVENT_SLOTS_MAX),
    },{
        .select    = VIRTIO_INPUT_CFG_ABS_INFO,
        .subsel    = ABS_MT_TRACKING_ID,
        .size      = sizeof(virtio_input_absinfo),
        .u.abs.min = const_le32(INPUT_EVENT_SLOTS_MIN),
        .u.abs.max = const_le32(INPUT_EVENT_SLOTS_MAX),
    },{
        .select    = VIRTIO_INPUT_CFG_ABS_INFO,
        .subsel    = ABS_MT_POSITION_X,
        .size      = sizeof(virtio_input_absinfo),
        .u.abs.min = const_le32(INPUT_EVENT_ABS_MIN),
        .u.abs.max = const_le32(INPUT_EVENT_ABS_MAX),
    },{
        .select    = VIRTIO_INPUT_CFG_ABS_INFO,
        .subsel    = ABS_MT_POSITION_Y,
        .size      = sizeof(virtio_input_absinfo),
        .u.abs.min = const_le32(INPUT_EVENT_ABS_MIN),
        .u.abs.max = const_le32(INPUT_EVENT_ABS_MAX),
    },
    { /* end of list */ },
};

static void virtio_multitouch_init(Object *obj)
{
    VirtIOInputHID *vhid = VIRTIO_INPUT_HID(obj);
    VirtIOInput *vinput = VIRTIO_INPUT(obj);
    unsigned short abs_props[] = {
        INPUT_PROP_DIRECT,
    };
    unsigned short abs_bits[] = {
        ABS_MT_SLOT,
        ABS_MT_TRACKING_ID,
        ABS_MT_POSITION_X,
        ABS_MT_POSITION_Y,
    };

    vhid->handler = &virtio_multitouch_handler;
    virtio_input_init_config(vinput, virtio_multitouch_config);
    virtio_input_extend_config(vinput, keymap_button,
                               ARRAY_SIZE(keymap_button),
                               VIRTIO_INPUT_CFG_EV_BITS, EV_KEY);
    virtio_input_extend_config(vinput, abs_props,
                               ARRAY_SIZE(abs_props),
                               VIRTIO_INPUT_CFG_PROP_BITS, 0);
    virtio_input_extend_config(vinput, abs_bits,
                               ARRAY_SIZE(abs_bits),
                               VIRTIO_INPUT_CFG_EV_BITS, EV_ABS);
}

static const TypeInfo virtio_multitouch_info = {
    .name          = TYPE_VIRTIO_MULTITOUCH,
    .parent        = TYPE_VIRTIO_INPUT_HID,
    .instance_size = sizeof(VirtIOInputHID),
    .instance_init = virtio_multitouch_init,
};

/* ----------------------------------------------------------------- */

static void virtio_register_types(void)
{
    type_register_static(&virtio_input_hid_info);
    type_register_static(&virtio_keyboard_info);
    type_register_static(&virtio_mouse_info);
    type_register_static(&virtio_tablet_info);
    type_register_static(&virtio_multitouch_info);
}

type_init(virtio_register_types)
