/*
 * QEMU Wacom Penpartner serial tablet emulation
 *
 * some protocol details:
 *   http://linuxwacom.sourceforge.net/wiki/index.php/Serial_Protocol_IV
 *
 * Copyright (c) 2016 Anatoli Huseu1
 * Copyright (c) 2016,17 Gerd Hoffmann
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "chardev/char-serial.h"
#include "ui/console.h"
#include "ui/input.h"
#include "trace.h"
#include "qom/object.h"


#define WC_OUTPUT_BUF_MAX_LEN 512
#define WC_COMMAND_MAX_LEN 60

#define WC_L7(n) ((n) & 127)
#define WC_M7(n) (((n) >> 7) & 127)
#define WC_H2(n) ((n) >> 14)

#define WC_L4(n) ((n) & 15)
#define WC_H4(n) (((n) >> 4) & 15)

/* Model string and config string */
#define WC_MODEL_STRING_LENGTH 18
uint8_t WC_MODEL_STRING[WC_MODEL_STRING_LENGTH + 1] = "~#CT-0045R,V1.3-5,";

#define WC_CONFIG_STRING_LENGTH 8
uint8_t WC_CONFIG_STRING[WC_CONFIG_STRING_LENGTH + 1] = "96,N,8,0";

#define WC_FULL_CONFIG_STRING_LENGTH 61
uint8_t WC_FULL_CONFIG_STRING[WC_FULL_CONFIG_STRING_LENGTH + 1] = {
    0x5c, 0x39, 0x36, 0x2c, 0x4e, 0x2c, 0x38, 0x2c,
    0x31, 0x28, 0x01, 0x24, 0x57, 0x41, 0x43, 0x30,
    0x30, 0x34, 0x35, 0x5c, 0x5c, 0x50, 0x45, 0x4e, 0x5c,
    0x57, 0x41, 0x43, 0x30, 0x30, 0x30, 0x30, 0x5c,
    0x54, 0x61, 0x62, 0x6c, 0x65, 0x74, 0x0d, 0x0a,
    0x43, 0x54, 0x2d, 0x30, 0x30, 0x34, 0x35, 0x52,
    0x2c, 0x56, 0x31, 0x2e, 0x33, 0x2d, 0x35, 0x0d,
    0x0a, 0x45, 0x37, 0x29
};

/* This structure is used to save private info for Wacom Tablet. */
struct TabletChardev {
    Chardev parent;
    QemuInputHandlerState *hs;

    /* Query string from serial */
    uint8_t query[100];
    int query_index;

    /* Command to be sent to serial port */
    uint8_t outbuf[WC_OUTPUT_BUF_MAX_LEN];
    int outlen;

    int line_speed;
    bool send_events;
    int axis[INPUT_AXIS__MAX];
    bool btns[INPUT_BUTTON__MAX];

};
typedef struct TabletChardev TabletChardev;

#define TYPE_CHARDEV_WCTABLET "chardev-wctablet"
DECLARE_INSTANCE_CHECKER(TabletChardev, WCTABLET_CHARDEV,
                         TYPE_CHARDEV_WCTABLET)


static void wctablet_chr_accept_input(Chardev *chr);

static void wctablet_shift_input(TabletChardev *tablet, int count)
{
    tablet->query_index -= count;
    memmove(tablet->query, tablet->query + count, tablet->query_index);
    tablet->query[tablet->query_index] = 0;
}

static void wctablet_queue_output(TabletChardev *tablet, uint8_t *buf, int count)
{
    if (tablet->outlen + count > sizeof(tablet->outbuf)) {
        return;
    }

    memcpy(tablet->outbuf + tablet->outlen, buf, count);
    tablet->outlen += count;
    wctablet_chr_accept_input(CHARDEV(tablet));
}

static void wctablet_reset(TabletChardev *tablet)
{
    /* clear buffers */
    tablet->query_index = 0;
    tablet->outlen = 0;
    /* reset state */
    tablet->send_events = false;
}

static void wctablet_queue_event(TabletChardev *tablet)
{
    uint8_t codes[8] = { 0xe0, 0, 0, 0, 0, 0, 0 };

    if (tablet->line_speed != 9600) {
        return;
    }

    int newX = tablet->axis[INPUT_AXIS_X] * 0.1537;
    int nexY = tablet->axis[INPUT_AXIS_Y] * 0.1152;

    codes[0] = codes[0] | WC_H2(newX);
    codes[1] = codes[1] | WC_M7(newX);
    codes[2] = codes[2] | WC_L7(newX);

    codes[3] = codes[3] | WC_H2(nexY);
    codes[4] = codes[4] | WC_M7(nexY);
    codes[5] = codes[5] | WC_L7(nexY);

    if (tablet->btns[INPUT_BUTTON_LEFT]) {
        codes[0] = 0xa0;
    }

    wctablet_queue_output(tablet, codes, 7);
}

static void wctablet_input_event(DeviceState *dev, QemuConsole *src,
                                InputEvent *evt)
{
    TabletChardev *tablet = (TabletChardev *)dev;
    InputMoveEvent *move;
    InputBtnEvent *btn;

    switch (evt->type) {
    case INPUT_EVENT_KIND_ABS:
        move = evt->u.abs.data;
        tablet->axis[move->axis] = move->value;
        break;

    case INPUT_EVENT_KIND_BTN:
        btn = evt->u.btn.data;
        tablet->btns[btn->button] = btn->down;
        break;

    default:
        /* keep gcc happy */
        break;
    }
}

static void wctablet_input_sync(DeviceState *dev)
{
    TabletChardev *tablet = (TabletChardev *)dev;

    if (tablet->send_events) {
        wctablet_queue_event(tablet);
    }
}

static QemuInputHandler wctablet_handler = {
    .name  = "QEMU Wacom Pen Tablet",
    .mask  = INPUT_EVENT_MASK_BTN | INPUT_EVENT_MASK_ABS,
    .event = wctablet_input_event,
    .sync  = wctablet_input_sync,
};

static void wctablet_chr_accept_input(Chardev *chr)
{
    TabletChardev *tablet = WCTABLET_CHARDEV(chr);
    int len, canWrite;

    canWrite = qemu_chr_be_can_write(chr);
    len = canWrite;
    if (len > tablet->outlen) {
        len = tablet->outlen;
    }

    if (len) {
        qemu_chr_be_write(chr, tablet->outbuf, len);
        tablet->outlen -= len;
        if (tablet->outlen) {
            memmove(tablet->outbuf, tablet->outbuf + len, tablet->outlen);
        }
    }
}

static int wctablet_chr_write(struct Chardev *chr,
                              const uint8_t *buf, int len)
{
    TabletChardev *tablet = WCTABLET_CHARDEV(chr);
    unsigned int i, clen;
    char *pos;

    if (tablet->line_speed != 9600) {
        return len;
    }
    for (i = 0; i < len && tablet->query_index < sizeof(tablet->query) - 1; i++) {
        tablet->query[tablet->query_index++] = buf[i];
    }
    tablet->query[tablet->query_index] = 0;

    while (tablet->query_index > 0 && (tablet->query[0] == '@'  ||
                                       tablet->query[0] == '\r' ||
                                       tablet->query[0] == '\n')) {
        wctablet_shift_input(tablet, 1);
    }
    if (!tablet->query_index) {
        return len;
    }

    if (strncmp((char *)tablet->query, "~#", 2) == 0) {
        /* init / detect sequence */
        trace_wct_init();
        wctablet_shift_input(tablet, 2);
        wctablet_queue_output(tablet, WC_MODEL_STRING,
                              WC_MODEL_STRING_LENGTH);
        return len;
    }

    /* detect line */
    pos = strchr((char *)tablet->query, '\r');
    if (!pos) {
        pos = strchr((char *)tablet->query, '\n');
    }
    if (!pos) {
        return len;
    }
    clen = pos - (char *)tablet->query;

    /* process commands */
    if (strncmp((char *)tablet->query, "RE", 2) == 0 &&
        clen == 2) {
        trace_wct_cmd_re();
        wctablet_shift_input(tablet, 3);
        wctablet_queue_output(tablet, WC_CONFIG_STRING,
                              WC_CONFIG_STRING_LENGTH);

    } else if (strncmp((char *)tablet->query, "ST", 2) == 0 &&
               clen == 2) {
        trace_wct_cmd_st();
        wctablet_shift_input(tablet, 3);
        tablet->send_events = true;
        wctablet_queue_event(tablet);

    } else if (strncmp((char *)tablet->query, "SP", 2) == 0 &&
               clen == 2) {
        trace_wct_cmd_sp();
        wctablet_shift_input(tablet, 3);
        tablet->send_events = false;

    } else if (strncmp((char *)tablet->query, "TS", 2) == 0 &&
               clen == 3) {
        unsigned int input = tablet->query[2];
        uint8_t codes[7] = {
            0xa3,
            ((input & 0x80) == 0) ? 0x7e : 0x7f,
            (((WC_H4(input) & 0x7) ^ 0x5) << 4) | (WC_L4(input) ^ 0x7),
            0x03,
            0x7f,
            0x7f,
            0x00,
        };
        trace_wct_cmd_ts(input);
        wctablet_shift_input(tablet, 4);
        wctablet_queue_output(tablet, codes, 7);

    } else {
        tablet->query[clen] = 0; /* terminate line for printing */
        trace_wct_cmd_other((char *)tablet->query);
        wctablet_shift_input(tablet, clen + 1);

    }

    return len;
}

static int wctablet_chr_ioctl(Chardev *chr, int cmd, void *arg)
{
    TabletChardev *tablet = WCTABLET_CHARDEV(chr);
    QEMUSerialSetParams *ssp;

    switch (cmd) {
    case CHR_IOCTL_SERIAL_SET_PARAMS:
        ssp = arg;
        if (tablet->line_speed != ssp->speed) {
            trace_wct_speed(ssp->speed);
            wctablet_reset(tablet);
            tablet->line_speed = ssp->speed;
        }
        break;
    default:
        return -ENOTSUP;
    }
    return 0;
}

static void wctablet_chr_finalize(Object *obj)
{
    TabletChardev *tablet = WCTABLET_CHARDEV(obj);

    qemu_input_handler_unregister(tablet->hs);
    g_free(tablet);
}

static void wctablet_chr_open(Chardev *chr,
                              ChardevBackend *backend,
                              bool *be_opened,
                              Error **errp)
{
    TabletChardev *tablet = WCTABLET_CHARDEV(chr);

    *be_opened = true;

    /* init state machine */
    memcpy(tablet->outbuf, WC_FULL_CONFIG_STRING, WC_FULL_CONFIG_STRING_LENGTH);
    tablet->outlen = WC_FULL_CONFIG_STRING_LENGTH;
    tablet->query_index = 0;

    tablet->hs = qemu_input_handler_register((DeviceState *)tablet,
                                             &wctablet_handler);
}

static void wctablet_chr_class_init(ObjectClass *oc, void *data)
{
    ChardevClass *cc = CHARDEV_CLASS(oc);

    cc->open = wctablet_chr_open;
    cc->chr_write = wctablet_chr_write;
    cc->chr_ioctl = wctablet_chr_ioctl;
    cc->chr_accept_input = wctablet_chr_accept_input;
}

static const TypeInfo wctablet_type_info = {
    .name = TYPE_CHARDEV_WCTABLET,
    .parent = TYPE_CHARDEV,
    .instance_size = sizeof(TabletChardev),
    .instance_finalize = wctablet_chr_finalize,
    .class_init = wctablet_chr_class_init,
};

static void register_types(void)
{
     type_register_static(&wctablet_type_info);
}

type_init(register_types);
