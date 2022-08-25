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
#include "qemu/module.h"
#include "qemu/fifo8.h"
#include "chardev/char.h"
#include "chardev/char-serial.h"
#include "ui/console.h"
#include "ui/input.h"
#include "qom/object.h"

#define MSMOUSE_LO6(n)  ((n) & 0x3f)
#define MSMOUSE_HI2(n)  (((n) & 0xc0) >> 6)
#define MSMOUSE_PWR(cm) (cm & (CHR_TIOCM_RTS | CHR_TIOCM_DTR))

/* Serial PnP for 6 bit devices/mice sends all ASCII chars - 0x20 */
#define M(c) (c - 0x20)
/* Serial fifo size. */
#define MSMOUSE_BUF_SZ 64

/* Mouse ID: Send "M3" cause we behave like a 3 button logitech mouse. */
const uint8_t mouse_id[] = {'M', '3'};
/*
 * PnP start "(", PnP version (1.0), vendor ID, product ID, '\\',
 * serial ID (omitted), '\\', MS class name, '\\', driver ID (omitted), '\\',
 * product description, checksum, ")"
 * Missing parts are inserted later.
 */
const uint8_t pnp_data[] = {M('('), 1, '$', M('Q'), M('M'), M('U'),
                         M('0'), M('0'), M('0'), M('1'),
                         M('\\'), M('\\'),
                         M('M'), M('O'), M('U'), M('S'), M('E'),
                         M('\\'), M('\\')};

struct MouseChardev {
    Chardev parent;

    QemuInputHandlerState *hs;
    int tiocm;
    int axis[INPUT_AXIS__MAX];
    bool btns[INPUT_BUTTON__MAX];
    bool btnc[INPUT_BUTTON__MAX];
    Fifo8 outbuf;
};
typedef struct MouseChardev MouseChardev;

#define TYPE_CHARDEV_MSMOUSE "chardev-msmouse"
DECLARE_INSTANCE_CHECKER(MouseChardev, MOUSE_CHARDEV,
                         TYPE_CHARDEV_MSMOUSE)

static void msmouse_chr_accept_input(Chardev *chr)
{
    MouseChardev *mouse = MOUSE_CHARDEV(chr);
    uint32_t len, avail;

    len = qemu_chr_be_can_write(chr);
    avail = fifo8_num_used(&mouse->outbuf);
    while (len > 0 && avail > 0) {
        const uint8_t *buf;
        uint32_t size;

        buf = fifo8_pop_buf(&mouse->outbuf, MIN(len, avail), &size);
        qemu_chr_be_write(chr, buf, size);
        len = qemu_chr_be_can_write(chr);
        avail -= size;
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
        count++;
    }

    if (fifo8_num_free(&mouse->outbuf) >= count) {
        fifo8_push_all(&mouse->outbuf, bytes, count);
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

    /* Ignore events if serial mouse powered down. */
    if (!MSMOUSE_PWR(mouse->tiocm)) {
        return;
    }

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

    /* Ignore events if serial mouse powered down. */
    if (!MSMOUSE_PWR(mouse->tiocm)) {
        return;
    }

    msmouse_queue_event(mouse);
    msmouse_chr_accept_input(chr);
}

static int msmouse_chr_write(struct Chardev *s, const uint8_t *buf, int len)
{
    /* Ignore writes to mouse port */
    return len;
}

static QemuInputHandler msmouse_handler = {
    .name  = "QEMU Microsoft Mouse",
    .mask  = INPUT_EVENT_MASK_BTN | INPUT_EVENT_MASK_REL,
    .event = msmouse_input_event,
    .sync  = msmouse_input_sync,
};

static int msmouse_ioctl(Chardev *chr, int cmd, void *arg)
{
    MouseChardev *mouse = MOUSE_CHARDEV(chr);
    int c, i, j;
    uint8_t bytes[MSMOUSE_BUF_SZ / 2];
    int *targ = (int *)arg;
    const uint8_t hexchr[16] = {M('0'), M('1'), M('2'), M('3'), M('4'), M('5'),
                             M('6'), M('7'), M('8'), M('9'), M('A'), M('B'),
                             M('C'), M('D'), M('E'), M('F')};

    switch (cmd) {
    case CHR_IOCTL_SERIAL_SET_TIOCM:
        c = mouse->tiocm;
        mouse->tiocm = *(int *)arg;
        if (MSMOUSE_PWR(mouse->tiocm)) {
            if (!MSMOUSE_PWR(c)) {
                /*
                 * Power on after reset: Send ID and PnP data
                 * No need to check fifo space as it is empty at this point.
                 */
                fifo8_push_all(&mouse->outbuf, mouse_id, sizeof(mouse_id));
                /* Add PnP data: */
                fifo8_push_all(&mouse->outbuf, pnp_data, sizeof(pnp_data));
                /*
                 * Add device description from qemu handler name.
                 * Make sure this all fits into the queue beforehand!
                 */
                c = M(')');
                for (i = 0; msmouse_handler.name[i]; i++) {
                    bytes[i] = M(msmouse_handler.name[i]);
                    c += bytes[i];
                }
                /* Calc more of checksum */
                for (j = 0; j < sizeof(pnp_data); j++) {
                    c += pnp_data[j];
                }
                c &= 0xff;
                bytes[i++] = hexchr[c >> 4];
                bytes[i++] = hexchr[c & 0x0f];
                bytes[i++] = M(')');
                fifo8_push_all(&mouse->outbuf, bytes, i);
                /* Start sending data to serial. */
                msmouse_chr_accept_input(chr);
            }
            break;
        }
        /*
         * Reset mouse buffers on power down.
         * Mouse won't send anything without power.
         */
        fifo8_reset(&mouse->outbuf);
        memset(mouse->axis, 0, sizeof(mouse->axis));
        memset(mouse->btns, false, sizeof(mouse->btns));
        memset(mouse->btnc, false, sizeof(mouse->btns));
        break;
    case CHR_IOCTL_SERIAL_GET_TIOCM:
        /* Remember line control status. */
        *targ = mouse->tiocm;
        break;
    default:
        return -ENOTSUP;
    }
    return 0;
}

static void char_msmouse_finalize(Object *obj)
{
    MouseChardev *mouse = MOUSE_CHARDEV(obj);

    if (mouse->hs) {
        qemu_input_handler_unregister(mouse->hs);
    }
    fifo8_destroy(&mouse->outbuf);
}

static void msmouse_chr_open(Chardev *chr,
                             ChardevBackend *backend,
                             bool *be_opened,
                             Error **errp)
{
    MouseChardev *mouse = MOUSE_CHARDEV(chr);

    *be_opened = false;
    mouse->hs = qemu_input_handler_register((DeviceState *)mouse,
                                            &msmouse_handler);
    mouse->tiocm = 0;
    fifo8_create(&mouse->outbuf, MSMOUSE_BUF_SZ);
}

static void char_msmouse_class_init(ObjectClass *oc, void *data)
{
    ChardevClass *cc = CHARDEV_CLASS(oc);

    cc->open = msmouse_chr_open;
    cc->chr_write = msmouse_chr_write;
    cc->chr_accept_input = msmouse_chr_accept_input;
    cc->chr_ioctl = msmouse_ioctl;
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
