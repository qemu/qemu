/*
 * QEMU Microsoft serial mouse emulation
 *
 * Copyright (c) 2008 Lubomir Rintel
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
#include "qemu-common.h"
#include "sysemu/char.h"
#include "ui/console.h"
#include "ui/input.h"

#define MSMOUSE_LO6(n) ((n) & 0x3f)
#define MSMOUSE_HI2(n) (((n) & 0xc0) >> 6)

typedef struct {
    Chardev parent;

    QemuInputHandlerState *hs;
    int axis[INPUT_AXIS__MAX];
    bool btns[INPUT_BUTTON__MAX];
    bool btnc[INPUT_BUTTON__MAX];
    uint8_t outbuf[32];
    int outlen;
} MouseChardev;

#define TYPE_CHARDEV_MSMOUSE "chardev-msmouse"
#define MOUSE_CHARDEV(obj)                                      \
    OBJECT_CHECK(MouseChardev, (obj), TYPE_CHARDEV_MSMOUSE)

static void msmouse_chr_accept_input(Chardev *chr)
{
    MouseChardev *mouse = MOUSE_CHARDEV(chr);
    int len;

    len = qemu_chr_be_can_write(chr);
    if (len > mouse->outlen) {
        len = mouse->outlen;
    }
    if (!len) {
        return;
    }

    qemu_chr_be_write(chr, mouse->outbuf, len);
    mouse->outlen -= len;
    if (mouse->outlen) {
        memmove(mouse->outbuf, mouse->outbuf + len, mouse->outlen);
    }
}

static void msmouse_queue_event(MouseChardev *mouse)
{
    unsigned char bytes[4] = { 0x40, 0x00, 0x00, 0x00 };
    int dx, dy, count = 3;

    dx = mouse->axis[INPUT_AXIS_X];
    mouse->axis[INPUT_AXIS_X] = 0;

    dy = mouse->axis[INPUT_AXIS_Y];
    mouse->axis[INPUT_AXIS_Y] = 0;

    /* Movement deltas */
    bytes[0] |= (MSMOUSE_HI2(dy) << 2) | MSMOUSE_HI2(dx);
    bytes[1] |= MSMOUSE_LO6(dx);
    bytes[2] |= MSMOUSE_LO6(dy);

    /* Buttons */
    bytes[0] |= (mouse->btns[INPUT_BUTTON_LEFT]   ? 0x20 : 0x00);
    bytes[0] |= (mouse->btns[INPUT_BUTTON_RIGHT]  ? 0x10 : 0x00);
    if (mouse->btns[INPUT_BUTTON_MIDDLE] ||
        mouse->btnc[INPUT_BUTTON_MIDDLE]) {
        bytes[3] |= (mouse->btns[INPUT_BUTTON_MIDDLE] ? 0x20 : 0x00);
        mouse->btnc[INPUT_BUTTON_MIDDLE] = false;
        count = 4;
    }

    if (mouse->outlen <= sizeof(mouse->outbuf) - count) {
        memcpy(mouse->outbuf + mouse->outlen, bytes, count);
        mouse->outlen += count;
    } else {
        /* queue full -> drop event */
    }
}

static void msmouse_input_event(DeviceState *dev, QemuConsole *src,
                                InputEvent *evt)
{
    MouseChardev *mouse = MOUSE_CHARDEV(dev);
    InputMoveEvent *move;
    InputBtnEvent *btn;

    switch (evt->type) {
    case INPUT_EVENT_KIND_REL:
        move = evt->u.rel.data;
        mouse->axis[move->axis] += move->value;
        break;

    case INPUT_EVENT_KIND_BTN:
        btn = evt->u.btn.data;
        mouse->btns[btn->button] = btn->down;
        mouse->btnc[btn->button] = true;
        break;

    default:
        /* keep gcc happy */
        break;
    }
}

static void msmouse_input_sync(DeviceState *dev)
{
    MouseChardev *mouse = MOUSE_CHARDEV(dev);
    Chardev *chr = CHARDEV(dev);

    msmouse_queue_event(mouse);
    msmouse_chr_accept_input(chr);
}

static int msmouse_chr_write(struct Chardev *s, const uint8_t *buf, int len)
{
    /* Ignore writes to mouse port */
    return len;
}

static void char_msmouse_finalize(Object *obj)
{
    MouseChardev *mouse = MOUSE_CHARDEV(obj);

    qemu_input_handler_unregister(mouse->hs);
}

static QemuInputHandler msmouse_handler = {
    .name  = "QEMU Microsoft Mouse",
    .mask  = INPUT_EVENT_MASK_BTN | INPUT_EVENT_MASK_REL,
    .event = msmouse_input_event,
    .sync  = msmouse_input_sync,
};

static void msmouse_chr_open(Chardev *chr,
                             ChardevBackend *backend,
                             bool *be_opened,
                             Error **errp)
{
    MouseChardev *mouse = MOUSE_CHARDEV(chr);

    *be_opened = false;
    mouse->hs = qemu_input_handler_register((DeviceState *)mouse,
                                            &msmouse_handler);
}

static void char_msmouse_class_init(ObjectClass *oc, void *data)
{
    ChardevClass *cc = CHARDEV_CLASS(oc);

    cc->open = msmouse_chr_open;
    cc->chr_write = msmouse_chr_write;
    cc->chr_accept_input = msmouse_chr_accept_input;
}

static const TypeInfo char_msmouse_type_info = {
    .name = TYPE_CHARDEV_MSMOUSE,
    .parent = TYPE_CHARDEV,
    .instance_size = sizeof(MouseChardev),
    .instance_finalize = char_msmouse_finalize,
    .class_init = char_msmouse_class_init,
};

static void register_types(void)
{
    type_register_static(&char_msmouse_type_info);
}

type_init(register_types);
