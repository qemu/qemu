/*
 * QEMU ADB keyboard support
 *
 * Copyright (c) 2004 Fabrice Bellard
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
#include "hw/input/adb.h"
#include "migration/vmstate.h"
#include "qemu/module.h"
#include "standard-headers/linux/input-event-codes.h"
#include "ui/input.h"
#include "hw/input/adb-keys.h"
#include "adb-internal.h"
#include "trace.h"
#include "qom/object.h"

OBJECT_DECLARE_TYPE(KBDState, ADBKeyboardClass, ADB_KEYBOARD)

struct KBDState {
    /*< private >*/
    ADBDevice parent_obj;
    /*< public >*/

    uint8_t data[128];
    int rptr, wptr, count;
};


struct ADBKeyboardClass {
    /*< private >*/
    ADBDeviceClass parent_class;
    /*< public >*/

    DeviceRealize parent_realize;
};

/* The adb keyboard doesn't have every key imaginable */
#define NO_KEY 0xff

int linux_to_adb_keycode[] = {
     /* Make sure future additions are automatically set to NO_KEY */
    [0 ... KEY_MAX]  = NO_KEY,

    [KEY_LEFTSHIFT]  = ADB_KEY_LEFT_SHIFT,
    [KEY_RIGHTSHIFT] = ADB_KEY_RIGHT_SHIFT,
    [KEY_LEFTALT]    = ADB_KEY_LEFT_OPTION,
    [KEY_RIGHTALT]   = ADB_KEY_RIGHT_OPTION,
    [KEY_LEFTCTRL]   = ADB_KEY_LEFT_CONTROL,
    [KEY_RIGHTCTRL]  = ADB_KEY_RIGHT_CONTROL,
    [KEY_LEFTMETA]   = ADB_KEY_COMMAND,
    [KEY_RIGHTMETA]  = ADB_KEY_COMMAND,
    [KEY_SPACE]      = ADB_KEY_SPACEBAR,

    [KEY_ESC]        = ADB_KEY_ESC,
    [KEY_1]          = ADB_KEY_1,
    [KEY_2]          = ADB_KEY_2,
    [KEY_3]          = ADB_KEY_3,
    [KEY_4]          = ADB_KEY_4,
    [KEY_5]          = ADB_KEY_5,
    [KEY_6]          = ADB_KEY_6,
    [KEY_7]          = ADB_KEY_7,
    [KEY_8]          = ADB_KEY_8,
    [KEY_9]          = ADB_KEY_9,
    [KEY_0]          = ADB_KEY_0,
    [KEY_MINUS]      = ADB_KEY_MINUS,
    [KEY_EQUAL]      = ADB_KEY_EQUAL,
    [KEY_BACKSPACE]  = ADB_KEY_DELETE,
    [KEY_TAB]        = ADB_KEY_TAB,
    [KEY_Q]          = ADB_KEY_Q,
    [KEY_W]          = ADB_KEY_W,
    [KEY_E]          = ADB_KEY_E,
    [KEY_R]          = ADB_KEY_R,
    [KEY_T]          = ADB_KEY_T,
    [KEY_Y]          = ADB_KEY_Y,
    [KEY_U]          = ADB_KEY_U,
    [KEY_I]          = ADB_KEY_I,
    [KEY_O]          = ADB_KEY_O,
    [KEY_P]          = ADB_KEY_P,
    [KEY_LEFTBRACE]  = ADB_KEY_LEFT_BRACKET,
    [KEY_RIGHTBRACE] = ADB_KEY_RIGHT_BRACKET,
    [KEY_ENTER]      = ADB_KEY_RETURN,
    [KEY_A]          = ADB_KEY_A,
    [KEY_S]          = ADB_KEY_S,
    [KEY_D]          = ADB_KEY_D,
    [KEY_F]          = ADB_KEY_F,
    [KEY_G]          = ADB_KEY_G,
    [KEY_H]          = ADB_KEY_H,
    [KEY_J]          = ADB_KEY_J,
    [KEY_K]          = ADB_KEY_K,
    [KEY_L]          = ADB_KEY_L,
    [KEY_SEMICOLON]  = ADB_KEY_SEMICOLON,
    [KEY_APOSTROPHE] = ADB_KEY_APOSTROPHE,
    [KEY_GRAVE]      = ADB_KEY_GRAVE_ACCENT,
    [KEY_BACKSLASH]  = ADB_KEY_BACKSLASH,
    [KEY_Z]          = ADB_KEY_Z,
    [KEY_X]          = ADB_KEY_X,
    [KEY_C]          = ADB_KEY_C,
    [KEY_V]          = ADB_KEY_V,
    [KEY_B]          = ADB_KEY_B,
    [KEY_N]          = ADB_KEY_N,
    [KEY_M]          = ADB_KEY_M,
    [KEY_COMMA]      = ADB_KEY_COMMA,
    [KEY_DOT]        = ADB_KEY_PERIOD,
    [KEY_SLASH]      = ADB_KEY_FORWARD_SLASH,
    [KEY_CAPSLOCK]   = ADB_KEY_CAPS_LOCK,

    [KEY_F1]         = ADB_KEY_F1,
    [KEY_F2]         = ADB_KEY_F2,
    [KEY_F3]         = ADB_KEY_F3,
    [KEY_F4]         = ADB_KEY_F4,
    [KEY_F5]         = ADB_KEY_F5,
    [KEY_F6]         = ADB_KEY_F6,
    [KEY_F7]         = ADB_KEY_F7,
    [KEY_F8]         = ADB_KEY_F8,
    [KEY_F9]         = ADB_KEY_F9,
    [KEY_F10]        = ADB_KEY_F10,
    [KEY_F11]        = ADB_KEY_F11,
    [KEY_F12]        = ADB_KEY_F12,
    [KEY_SYSRQ]      = ADB_KEY_F13,
    [KEY_SCROLLLOCK] = ADB_KEY_F14,
    [KEY_PAUSE]      = ADB_KEY_F15,

    [KEY_NUMLOCK]    = ADB_KEY_KP_CLEAR,
    [KEY_KPEQUAL]    = ADB_KEY_KP_EQUAL,
    [KEY_KPSLASH]    = ADB_KEY_KP_DIVIDE,
    [KEY_KPASTERISK] = ADB_KEY_KP_MULTIPLY,
    [KEY_KPMINUS]    = ADB_KEY_KP_SUBTRACT,
    [KEY_KPPLUS]     = ADB_KEY_KP_PLUS,
    [KEY_KPENTER]    = ADB_KEY_KP_ENTER,
    [KEY_KPDOT]      = ADB_KEY_KP_PERIOD,
    [KEY_KP0]        = ADB_KEY_KP_0,
    [KEY_KP1]        = ADB_KEY_KP_1,
    [KEY_KP2]        = ADB_KEY_KP_2,
    [KEY_KP3]        = ADB_KEY_KP_3,
    [KEY_KP4]        = ADB_KEY_KP_4,
    [KEY_KP5]        = ADB_KEY_KP_5,
    [KEY_KP6]        = ADB_KEY_KP_6,
    [KEY_KP7]        = ADB_KEY_KP_7,
    [KEY_KP8]        = ADB_KEY_KP_8,
    [KEY_KP9]        = ADB_KEY_KP_9,

    [KEY_UP]         = ADB_KEY_UP,
    [KEY_DOWN]       = ADB_KEY_DOWN,
    [KEY_LEFT]       = ADB_KEY_LEFT,
    [KEY_RIGHT]      = ADB_KEY_RIGHT,

    [KEY_HELP]       = ADB_KEY_HELP,
    [KEY_INSERT]     = ADB_KEY_HELP,
    [KEY_DELETE]     = ADB_KEY_FORWARD_DELETE,
    [KEY_HOME]       = ADB_KEY_HOME,
    [KEY_END]        = ADB_KEY_END,
    [KEY_PAGEUP]     = ADB_KEY_PAGE_UP,
    [KEY_PAGEDOWN]   = ADB_KEY_PAGE_DOWN,

    [KEY_POWER]      = ADB_KEY_POWER
};

static void adb_kbd_put_keycode(void *opaque, int keycode)
{
    KBDState *s = opaque;

    if (s->count < sizeof(s->data)) {
        s->data[s->wptr] = keycode;
        if (++s->wptr == sizeof(s->data)) {
            s->wptr = 0;
        }
        s->count++;
    }
}

static int adb_kbd_poll(ADBDevice *d, uint8_t *obuf)
{
    KBDState *s = ADB_KEYBOARD(d);
    int keycode;

    if (s->count == 0) {
        return 0;
    }
    keycode = s->data[s->rptr];
    s->rptr++;
    if (s->rptr == sizeof(s->data)) {
        s->rptr = 0;
    }
    s->count--;
    /*
     * The power key is the only two byte value key, so it is a special case.
     * Since 0x7f is not a used keycode for ADB we overload it to indicate the
     * power button when we're storing keycodes in our internal buffer, and
     * expand it out to two bytes when we send to the guest.
     */
    if (keycode == 0x7f) {
        obuf[0] = 0x7f;
        obuf[1] = 0x7f;
    } else {
        obuf[0] = keycode;
        /* NOTE: the power key key-up is the two byte sequence 0xff 0xff;
         * otherwise we could in theory send a second keycode in the second
         * byte, but choose not to bother.
         */
        obuf[1] = 0xff;
    }

    return 2;
}

static int adb_kbd_request(ADBDevice *d, uint8_t *obuf,
                           const uint8_t *buf, int len)
{
    KBDState *s = ADB_KEYBOARD(d);
    int cmd, reg, olen;

    if ((buf[0] & 0x0f) == ADB_FLUSH) {
        /* flush keyboard fifo */
        s->wptr = s->rptr = s->count = 0;
        return 0;
    }

    cmd = buf[0] & 0xc;
    reg = buf[0] & 0x3;
    olen = 0;
    switch (cmd) {
    case ADB_WRITEREG:
        trace_adb_device_kbd_writereg(reg, buf[1]);
        switch (reg) {
        case 2:
            /* LED status */
            break;
        case 3:
            switch (buf[2]) {
            case ADB_CMD_SELF_TEST:
                break;
            case ADB_CMD_CHANGE_ID:
            case ADB_CMD_CHANGE_ID_AND_ACT:
            case ADB_CMD_CHANGE_ID_AND_ENABLE:
                d->devaddr = buf[1] & 0xf;
                trace_adb_device_kbd_request_change_addr(d->devaddr);
                break;
            default:
                d->devaddr = buf[1] & 0xf;
                /*
                 * we support handlers:
                 * 1: Apple Standard Keyboard
                 * 2: Apple Extended Keyboard (LShift = RShift)
                 * 3: Apple Extended Keyboard (LShift != RShift)
                 */
                if (buf[2] == 1 || buf[2] == 2 || buf[2] == 3) {
                    d->handler = buf[2];
                }

                trace_adb_device_kbd_request_change_addr_and_handler(
                    d->devaddr, d->handler);
                break;
            }
        }
        break;
    case ADB_READREG:
        switch (reg) {
        case 0:
            olen = adb_kbd_poll(d, obuf);
            break;
        case 1:
            break;
        case 2:
            obuf[0] = 0x00; /* XXX: check this */
            obuf[1] = 0x07; /* led status */
            olen = 2;
            break;
        case 3:
            obuf[0] = d->devaddr;
            obuf[1] = d->handler;
            olen = 2;
            break;
        }
        trace_adb_device_kbd_readreg(reg, obuf[0], obuf[1]);
        break;
    }
    return olen;
}

static bool adb_kbd_has_data(ADBDevice *d)
{
    KBDState *s = ADB_KEYBOARD(d);

    return s->count > 0;
}

/* This is where keyboard events enter this file */
static void adb_keyboard_event(DeviceState *dev, QemuConsole *src,
                               QemuInputEvent *evt)
{
    KBDState *s = (KBDState *)dev;
    int keycode;

    if (evt->key.key >= ARRAY_SIZE(linux_to_adb_keycode)) {
        return;
    }
    /* FIXME: take handler into account when translating evt->key.key */
    keycode = linux_to_adb_keycode[evt->key.key];
    if (keycode == NO_KEY) {  /* We don't want to send this to the guest */
        trace_adb_device_kbd_no_key();
        return;
    }
    if (evt->key.down == false) { /* if key release event */
        keycode = keycode | 0x80;   /* create keyboard break code */
    }

    adb_kbd_put_keycode(s, keycode);
}

static const VMStateDescription vmstate_adb_kbd = {
    .name = "adb_kbd",
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT(parent_obj, KBDState, 0, vmstate_adb_device, ADBDevice),
        VMSTATE_BUFFER(data, KBDState),
        VMSTATE_INT32(rptr, KBDState),
        VMSTATE_INT32(wptr, KBDState),
        VMSTATE_INT32(count, KBDState),
        VMSTATE_END_OF_LIST()
    }
};

static void adb_kbd_reset(DeviceState *dev)
{
    ADBDevice *d = ADB_DEVICE(dev);
    KBDState *s = ADB_KEYBOARD(dev);

    d->handler = 1;
    d->devaddr = ADB_DEVID_KEYBOARD;
    memset(s->data, 0, sizeof(s->data));
    s->rptr = 0;
    s->wptr = 0;
    s->count = 0;
}

static const QemuInputHandler adb_keyboard_handler = {
    .name  = "QEMU ADB Keyboard",
    .mask  = INPUT_EVENT_MASK_KEY,
    .event = adb_keyboard_event,
};

static void adb_kbd_realizefn(DeviceState *dev, Error **errp)
{
    ADBKeyboardClass *akc = ADB_KEYBOARD_GET_CLASS(dev);
    akc->parent_realize(dev, errp);
    qemu_input_handler_register(dev, &adb_keyboard_handler);
}

static void adb_kbd_initfn(Object *obj)
{
    ADBDevice *d = ADB_DEVICE(obj);

    d->devaddr = ADB_DEVID_KEYBOARD;
}

static void adb_kbd_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    ADBDeviceClass *adc = ADB_DEVICE_CLASS(oc);
    ADBKeyboardClass *akc = ADB_KEYBOARD_CLASS(oc);

    device_class_set_parent_realize(dc, adb_kbd_realizefn,
                                    &akc->parent_realize);
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);

    adc->devreq = adb_kbd_request;
    adc->devhasdata = adb_kbd_has_data;
    device_class_set_legacy_reset(dc, adb_kbd_reset);
    dc->vmsd = &vmstate_adb_kbd;
}

static const TypeInfo adb_kbd_type_info = {
    .name = TYPE_ADB_KEYBOARD,
    .parent = TYPE_ADB_DEVICE,
    .instance_size = sizeof(KBDState),
    .instance_init = adb_kbd_initfn,
    .class_init = adb_kbd_class_init,
    .class_size = sizeof(ADBKeyboardClass),
};

static void adb_kbd_register_types(void)
{
    type_register_static(&adb_kbd_type_info);
}

type_init(adb_kbd_register_types)
