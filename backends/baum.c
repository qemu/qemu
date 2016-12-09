/*
 * QEMU Baum Braille Device
 *
 * Copyright (c) 2008, 2010-2011, 2016 Samuel Thibault
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
#include "qapi/error.h"
#include "qemu-common.h"
#include "sysemu/char.h"
#include "qemu/timer.h"
#include "hw/usb.h"
#include "ui/console.h"
#include <brlapi.h>
#include <brlapi_constants.h>
#include <brlapi_keycodes.h>

#if 0
#define DPRINTF(fmt, ...) \
        printf(fmt, ## __VA_ARGS__)
#else
#define DPRINTF(fmt, ...)
#endif

#define ESC 0x1B

#define BAUM_REQ_DisplayData		0x01
#define BAUM_REQ_GetVersionNumber	0x05
#define BAUM_REQ_GetKeys		0x08
#define BAUM_REQ_SetMode		0x12
#define BAUM_REQ_SetProtocol		0x15
#define BAUM_REQ_GetDeviceIdentity	0x84
#define BAUM_REQ_GetSerialNumber	0x8A

#define BAUM_RSP_CellCount		0x01
#define BAUM_RSP_VersionNumber		0x05
#define BAUM_RSP_ModeSetting		0x11
#define BAUM_RSP_CommunicationChannel	0x16
#define BAUM_RSP_PowerdownSignal	0x17
#define BAUM_RSP_HorizontalSensors	0x20
#define BAUM_RSP_VerticalSensors	0x21
#define BAUM_RSP_RoutingKeys		0x22
#define BAUM_RSP_Switches		0x23
#define BAUM_RSP_TopKeys		0x24
#define BAUM_RSP_HorizontalSensor	0x25
#define BAUM_RSP_VerticalSensor		0x26
#define BAUM_RSP_RoutingKey		0x27
#define BAUM_RSP_FrontKeys6		0x28
#define BAUM_RSP_BackKeys6		0x29
#define BAUM_RSP_CommandKeys		0x2B
#define BAUM_RSP_FrontKeys10		0x2C
#define BAUM_RSP_BackKeys10		0x2D
#define BAUM_RSP_EntryKeys		0x33
#define BAUM_RSP_JoyStick		0x34
#define BAUM_RSP_ErrorCode		0x40
#define BAUM_RSP_InfoBlock		0x42
#define BAUM_RSP_DeviceIdentity		0x84
#define BAUM_RSP_SerialNumber		0x8A
#define BAUM_RSP_BluetoothName		0x8C

#define BAUM_TL1 0x01
#define BAUM_TL2 0x02
#define BAUM_TL3 0x04
#define BAUM_TR1 0x08
#define BAUM_TR2 0x10
#define BAUM_TR3 0x20

#define BUF_SIZE 256

typedef struct {
    Chardev parent;

    brlapi_handle_t *brlapi;
    int brlapi_fd;
    unsigned int x, y;
    bool deferred_init;

    uint8_t in_buf[BUF_SIZE];
    uint8_t in_buf_used;
    uint8_t out_buf[BUF_SIZE];
    uint8_t out_buf_used, out_buf_ptr;

    QEMUTimer *cellCount_timer;
} BaumChardev;

#define TYPE_CHARDEV_BRAILLE "chardev-braille"
#define BAUM_CHARDEV(obj) OBJECT_CHECK(BaumChardev, (obj), TYPE_CHARDEV_BRAILLE)

/* Let's assume NABCC by default */
enum way {
    DOTS2ASCII,
    ASCII2DOTS
};
static const uint8_t nabcc_translation[2][256] = {
#ifndef BRLAPI_DOTS
#define BRLAPI_DOTS(d1,d2,d3,d4,d5,d6,d7,d8) \
    ((d1?BRLAPI_DOT1:0)|\
     (d2?BRLAPI_DOT2:0)|\
     (d3?BRLAPI_DOT3:0)|\
     (d4?BRLAPI_DOT4:0)|\
     (d5?BRLAPI_DOT5:0)|\
     (d6?BRLAPI_DOT6:0)|\
     (d7?BRLAPI_DOT7:0)|\
     (d8?BRLAPI_DOT8:0))
#endif
#define DO(dots, ascii) \
    [DOTS2ASCII][dots] = ascii, \
    [ASCII2DOTS][ascii] = dots
    DO(0, ' '),
    DO(BRLAPI_DOTS(1, 0, 0, 0, 0, 0, 0, 0), 'a'),
    DO(BRLAPI_DOTS(1, 1, 0, 0, 0, 0, 0, 0), 'b'),
    DO(BRLAPI_DOTS(1, 0, 0, 1, 0, 0, 0, 0), 'c'),
    DO(BRLAPI_DOTS(1, 0, 0, 1, 1, 0, 0, 0), 'd'),
    DO(BRLAPI_DOTS(1, 0, 0, 0, 1, 0, 0, 0), 'e'),
    DO(BRLAPI_DOTS(1, 1, 0, 1, 0, 0, 0, 0), 'f'),
    DO(BRLAPI_DOTS(1, 1, 0, 1, 1, 0, 0, 0), 'g'),
    DO(BRLAPI_DOTS(1, 1, 0, 0, 1, 0, 0, 0), 'h'),
    DO(BRLAPI_DOTS(0, 1, 0, 1, 0, 0, 0, 0), 'i'),
    DO(BRLAPI_DOTS(0, 1, 0, 1, 1, 0, 0, 0), 'j'),
    DO(BRLAPI_DOTS(1, 0, 1, 0, 0, 0, 0, 0), 'k'),
    DO(BRLAPI_DOTS(1, 1, 1, 0, 0, 0, 0, 0), 'l'),
    DO(BRLAPI_DOTS(1, 0, 1, 1, 0, 0, 0, 0), 'm'),
    DO(BRLAPI_DOTS(1, 0, 1, 1, 1, 0, 0, 0), 'n'),
    DO(BRLAPI_DOTS(1, 0, 1, 0, 1, 0, 0, 0), 'o'),
    DO(BRLAPI_DOTS(1, 1, 1, 1, 0, 0, 0, 0), 'p'),
    DO(BRLAPI_DOTS(1, 1, 1, 1, 1, 0, 0, 0), 'q'),
    DO(BRLAPI_DOTS(1, 1, 1, 0, 1, 0, 0, 0), 'r'),
    DO(BRLAPI_DOTS(0, 1, 1, 1, 0, 0, 0, 0), 's'),
    DO(BRLAPI_DOTS(0, 1, 1, 1, 1, 0, 0, 0), 't'),
    DO(BRLAPI_DOTS(1, 0, 1, 0, 0, 1, 0, 0), 'u'),
    DO(BRLAPI_DOTS(1, 1, 1, 0, 0, 1, 0, 0), 'v'),
    DO(BRLAPI_DOTS(0, 1, 0, 1, 1, 1, 0, 0), 'w'),
    DO(BRLAPI_DOTS(1, 0, 1, 1, 0, 1, 0, 0), 'x'),
    DO(BRLAPI_DOTS(1, 0, 1, 1, 1, 1, 0, 0), 'y'),
    DO(BRLAPI_DOTS(1, 0, 1, 0, 1, 1, 0, 0), 'z'),

    DO(BRLAPI_DOTS(1, 0, 0, 0, 0, 0, 1, 0), 'A'),
    DO(BRLAPI_DOTS(1, 1, 0, 0, 0, 0, 1, 0), 'B'),
    DO(BRLAPI_DOTS(1, 0, 0, 1, 0, 0, 1, 0), 'C'),
    DO(BRLAPI_DOTS(1, 0, 0, 1, 1, 0, 1, 0), 'D'),
    DO(BRLAPI_DOTS(1, 0, 0, 0, 1, 0, 1, 0), 'E'),
    DO(BRLAPI_DOTS(1, 1, 0, 1, 0, 0, 1, 0), 'F'),
    DO(BRLAPI_DOTS(1, 1, 0, 1, 1, 0, 1, 0), 'G'),
    DO(BRLAPI_DOTS(1, 1, 0, 0, 1, 0, 1, 0), 'H'),
    DO(BRLAPI_DOTS(0, 1, 0, 1, 0, 0, 1, 0), 'I'),
    DO(BRLAPI_DOTS(0, 1, 0, 1, 1, 0, 1, 0), 'J'),
    DO(BRLAPI_DOTS(1, 0, 1, 0, 0, 0, 1, 0), 'K'),
    DO(BRLAPI_DOTS(1, 1, 1, 0, 0, 0, 1, 0), 'L'),
    DO(BRLAPI_DOTS(1, 0, 1, 1, 0, 0, 1, 0), 'M'),
    DO(BRLAPI_DOTS(1, 0, 1, 1, 1, 0, 1, 0), 'N'),
    DO(BRLAPI_DOTS(1, 0, 1, 0, 1, 0, 1, 0), 'O'),
    DO(BRLAPI_DOTS(1, 1, 1, 1, 0, 0, 1, 0), 'P'),
    DO(BRLAPI_DOTS(1, 1, 1, 1, 1, 0, 1, 0), 'Q'),
    DO(BRLAPI_DOTS(1, 1, 1, 0, 1, 0, 1, 0), 'R'),
    DO(BRLAPI_DOTS(0, 1, 1, 1, 0, 0, 1, 0), 'S'),
    DO(BRLAPI_DOTS(0, 1, 1, 1, 1, 0, 1, 0), 'T'),
    DO(BRLAPI_DOTS(1, 0, 1, 0, 0, 1, 1, 0), 'U'),
    DO(BRLAPI_DOTS(1, 1, 1, 0, 0, 1, 1, 0), 'V'),
    DO(BRLAPI_DOTS(0, 1, 0, 1, 1, 1, 1, 0), 'W'),
    DO(BRLAPI_DOTS(1, 0, 1, 1, 0, 1, 1, 0), 'X'),
    DO(BRLAPI_DOTS(1, 0, 1, 1, 1, 1, 1, 0), 'Y'),
    DO(BRLAPI_DOTS(1, 0, 1, 0, 1, 1, 1, 0), 'Z'),

    DO(BRLAPI_DOTS(0, 0, 1, 0, 1, 1, 0, 0), '0'),
    DO(BRLAPI_DOTS(0, 1, 0, 0, 0, 0, 0, 0), '1'),
    DO(BRLAPI_DOTS(0, 1, 1, 0, 0, 0, 0, 0), '2'),
    DO(BRLAPI_DOTS(0, 1, 0, 0, 1, 0, 0, 0), '3'),
    DO(BRLAPI_DOTS(0, 1, 0, 0, 1, 1, 0, 0), '4'),
    DO(BRLAPI_DOTS(0, 1, 0, 0, 0, 1, 0, 0), '5'),
    DO(BRLAPI_DOTS(0, 1, 1, 0, 1, 0, 0, 0), '6'),
    DO(BRLAPI_DOTS(0, 1, 1, 0, 1, 1, 0, 0), '7'),
    DO(BRLAPI_DOTS(0, 1, 1, 0, 0, 1, 0, 0), '8'),
    DO(BRLAPI_DOTS(0, 0, 1, 0, 1, 0, 0, 0), '9'),

    DO(BRLAPI_DOTS(0, 0, 0, 1, 0, 1, 0, 0), '.'),
    DO(BRLAPI_DOTS(0, 0, 1, 1, 0, 1, 0, 0), '+'),
    DO(BRLAPI_DOTS(0, 0, 1, 0, 0, 1, 0, 0), '-'),
    DO(BRLAPI_DOTS(1, 0, 0, 0, 0, 1, 0, 0), '*'),
    DO(BRLAPI_DOTS(0, 0, 1, 1, 0, 0, 0, 0), '/'),
    DO(BRLAPI_DOTS(1, 1, 1, 0, 1, 1, 0, 0), '('),
    DO(BRLAPI_DOTS(0, 1, 1, 1, 1, 1, 0, 0), ')'),

    DO(BRLAPI_DOTS(1, 1, 1, 1, 0, 1, 0, 0), '&'),
    DO(BRLAPI_DOTS(0, 0, 1, 1, 1, 1, 0, 0), '#'),

    DO(BRLAPI_DOTS(0, 0, 0, 0, 0, 1, 0, 0), ','),
    DO(BRLAPI_DOTS(0, 0, 0, 0, 1, 1, 0, 0), ';'),
    DO(BRLAPI_DOTS(1, 0, 0, 0, 1, 1, 0, 0), ':'),
    DO(BRLAPI_DOTS(0, 1, 1, 1, 0, 1, 0, 0), '!'),
    DO(BRLAPI_DOTS(1, 0, 0, 1, 1, 1, 0, 0), '?'),
    DO(BRLAPI_DOTS(0, 0, 0, 0, 1, 0, 0, 0), '"'),
    DO(BRLAPI_DOTS(0, 0, 1, 0, 0, 0, 0, 0), '\''),
    DO(BRLAPI_DOTS(0, 0, 0, 1, 0, 0, 0, 0), '`'),
    DO(BRLAPI_DOTS(0, 0, 0, 1, 1, 0, 1, 0), '^'),
    DO(BRLAPI_DOTS(0, 0, 0, 1, 1, 0, 0, 0), '~'),
    DO(BRLAPI_DOTS(0, 1, 0, 1, 0, 1, 1, 0), '['),
    DO(BRLAPI_DOTS(1, 1, 0, 1, 1, 1, 1, 0), ']'),
    DO(BRLAPI_DOTS(0, 1, 0, 1, 0, 1, 0, 0), '{'),
    DO(BRLAPI_DOTS(1, 1, 0, 1, 1, 1, 0, 0), '}'),
    DO(BRLAPI_DOTS(1, 1, 1, 1, 1, 1, 0, 0), '='),
    DO(BRLAPI_DOTS(1, 1, 0, 0, 0, 1, 0, 0), '<'),
    DO(BRLAPI_DOTS(0, 0, 1, 1, 1, 0, 0, 0), '>'),
    DO(BRLAPI_DOTS(1, 1, 0, 1, 0, 1, 0, 0), '$'),
    DO(BRLAPI_DOTS(1, 0, 0, 1, 0, 1, 0, 0), '%'),
    DO(BRLAPI_DOTS(0, 0, 0, 1, 0, 0, 1, 0), '@'),
    DO(BRLAPI_DOTS(1, 1, 0, 0, 1, 1, 0, 0), '|'),
    DO(BRLAPI_DOTS(1, 1, 0, 0, 1, 1, 1, 0), '\\'),
    DO(BRLAPI_DOTS(0, 0, 0, 1, 1, 1, 0, 0), '_'),
};

/* The guest OS has started discussing with us, finish initializing BrlAPI */
static int baum_deferred_init(BaumChardev *baum)
{
    int tty = BRLAPI_TTY_DEFAULT;
    QemuConsole *con;

    if (baum->deferred_init) {
        return 1;
    }

    if (brlapi__getDisplaySize(baum->brlapi, &baum->x, &baum->y) == -1) {
        brlapi_perror("baum: brlapi__getDisplaySize");
        return 0;
    }

    con = qemu_console_lookup_by_index(0);
    if (con && qemu_console_is_graphic(con)) {
        tty = qemu_console_get_window_id(con);
        if (tty == -1)
            tty = BRLAPI_TTY_DEFAULT;
    }

    if (brlapi__enterTtyMode(baum->brlapi, tty, NULL) == -1) {
        brlapi_perror("baum: brlapi__enterTtyMode");
        return 0;
    }
    baum->deferred_init = 1;
    return 1;
}

/* The serial port can receive more of our data */
static void baum_chr_accept_input(struct Chardev *chr)
{
    BaumChardev *baum = BAUM_CHARDEV(chr);
    int room, first;

    if (!baum->out_buf_used)
        return;
    room = qemu_chr_be_can_write(chr);
    if (!room)
        return;
    if (room > baum->out_buf_used)
        room = baum->out_buf_used;

    first = BUF_SIZE - baum->out_buf_ptr;
    if (room > first) {
        qemu_chr_be_write(chr, baum->out_buf + baum->out_buf_ptr, first);
        baum->out_buf_ptr = 0;
        baum->out_buf_used -= first;
        room -= first;
    }
    qemu_chr_be_write(chr, baum->out_buf + baum->out_buf_ptr, room);
    baum->out_buf_ptr += room;
    baum->out_buf_used -= room;
}

/* We want to send a packet */
static void baum_write_packet(BaumChardev *baum, const uint8_t *buf, int len)
{
    Chardev *chr = CHARDEV(baum);
    uint8_t io_buf[1 + 2 * len], *cur = io_buf;
    int room;
    *cur++ = ESC;
    while (len--)
        if ((*cur++ = *buf++) == ESC)
            *cur++ = ESC;
    room = qemu_chr_be_can_write(chr);
    len = cur - io_buf;
    if (len <= room) {
        /* Fits */
        qemu_chr_be_write(chr, io_buf, len);
    } else {
        int first;
        uint8_t out;
        /* Can't fit all, send what can be, and store the rest. */
        qemu_chr_be_write(chr, io_buf, room);
        len -= room;
        cur = io_buf + room;
        if (len > BUF_SIZE - baum->out_buf_used) {
            /* Can't even store it, drop the previous data... */
            assert(len <= BUF_SIZE);
            baum->out_buf_used = 0;
            baum->out_buf_ptr = 0;
        }
        out = baum->out_buf_ptr;
        baum->out_buf_used += len;
        first = BUF_SIZE - baum->out_buf_ptr;
        if (len > first) {
            memcpy(baum->out_buf + out, cur, first);
            out = 0;
            len -= first;
            cur += first;
        }
        memcpy(baum->out_buf + out, cur, len);
    }
}

/* Called when the other end seems to have a wrong idea of our display size */
static void baum_cellCount_timer_cb(void *opaque)
{
    BaumChardev *baum = BAUM_CHARDEV(opaque);
    uint8_t cell_count[] = { BAUM_RSP_CellCount, baum->x * baum->y };
    DPRINTF("Timeout waiting for DisplayData, sending cell count\n");
    baum_write_packet(baum, cell_count, sizeof(cell_count));
}

/* Try to interpret a whole incoming packet */
static int baum_eat_packet(BaumChardev *baum, const uint8_t *buf, int len)
{
    const uint8_t *cur = buf;
    uint8_t req = 0;

    if (!len--)
        return 0;
    if (*cur++ != ESC) {
        while (*cur != ESC) {
            if (!len--)
                return 0;
            cur++;
        }
        DPRINTF("Dropped %td bytes!\n", cur - buf);
    }

#define EAT(c) do {\
    if (!len--) \
        return 0; \
    if ((c = *cur++) == ESC) { \
        if (!len--) \
            return 0; \
        if (*cur++ != ESC) { \
            DPRINTF("Broken packet %#2x, tossing\n", req); \
            if (timer_pending(baum->cellCount_timer)) {    \
                timer_del(baum->cellCount_timer);     \
                baum_cellCount_timer_cb(baum);             \
            } \
            return (cur - 2 - buf); \
        } \
    } \
} while (0)

    EAT(req);
    switch (req) {
    case BAUM_REQ_DisplayData:
    {
        uint8_t cells[baum->x * baum->y], c;
        uint8_t text[baum->x * baum->y];
        uint8_t zero[baum->x * baum->y];
        int cursor = BRLAPI_CURSOR_OFF;
        int i;

        /* Allow 100ms to complete the DisplayData packet */
        timer_mod(baum->cellCount_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                       NANOSECONDS_PER_SECOND / 10);
        for (i = 0; i < baum->x * baum->y ; i++) {
            EAT(c);
            cells[i] = c;
            if ((c & (BRLAPI_DOT7|BRLAPI_DOT8))
                    == (BRLAPI_DOT7|BRLAPI_DOT8)) {
                cursor = i + 1;
                c &= ~(BRLAPI_DOT7|BRLAPI_DOT8);
            }
            c = nabcc_translation[DOTS2ASCII][c];
            if (!c) {
                c = '?';
            }
            text[i] = c;
        }
        timer_del(baum->cellCount_timer);

        memset(zero, 0, sizeof(zero));

        brlapi_writeArguments_t wa = {
            .displayNumber = BRLAPI_DISPLAY_DEFAULT,
            .regionBegin = 1,
            .regionSize = baum->x * baum->y,
            .text = (char *)text,
            .textSize = baum->x * baum->y,
            .andMask = zero,
            .orMask = cells,
            .cursor = cursor,
            .charset = (char *)"ISO-8859-1",
        };

        if (brlapi__write(baum->brlapi, &wa) == -1)
            brlapi_perror("baum brlapi_write");
        break;
    }
    case BAUM_REQ_SetMode:
    {
        uint8_t mode, setting;
        DPRINTF("SetMode\n");
        EAT(mode);
        EAT(setting);
        /* ignore */
        break;
    }
    case BAUM_REQ_SetProtocol:
    {
        uint8_t protocol;
        DPRINTF("SetProtocol\n");
        EAT(protocol);
        /* ignore */
        break;
    }
    case BAUM_REQ_GetDeviceIdentity:
    {
        uint8_t identity[17] = { BAUM_RSP_DeviceIdentity,
            'B','a','u','m',' ','V','a','r','i','o' };
        DPRINTF("GetDeviceIdentity\n");
        identity[11] = '0' + baum->x / 10;
        identity[12] = '0' + baum->x % 10;
        baum_write_packet(baum, identity, sizeof(identity));
        break;
    }
    case BAUM_REQ_GetVersionNumber:
    {
        uint8_t version[] = { BAUM_RSP_VersionNumber, 1 }; /* ? */
        DPRINTF("GetVersionNumber\n");
        baum_write_packet(baum, version, sizeof(version));
        break;
    }
    case BAUM_REQ_GetSerialNumber:
    {
        uint8_t serial[] = { BAUM_RSP_SerialNumber,
            '0','0','0','0','0','0','0','0' };
        DPRINTF("GetSerialNumber\n");
        baum_write_packet(baum, serial, sizeof(serial));
        break;
    }
    case BAUM_REQ_GetKeys:
    {
        DPRINTF("Get%0#2x\n", req);
        /* ignore */
        break;
    }
    default:
        DPRINTF("unrecognized request %0#2x\n", req);
        do
            if (!len--)
                return 0;
        while (*cur++ != ESC);
        cur--;
        break;
    }
    return cur - buf;
}

/* The other end is writing some data.  Store it and try to interpret */
static int baum_chr_write(Chardev *chr, const uint8_t *buf, int len)
{
    BaumChardev *baum = BAUM_CHARDEV(chr);
    int tocopy, cur, eaten, orig_len = len;

    if (!len)
        return 0;
    if (!baum->brlapi)
        return len;
    if (!baum_deferred_init(baum))
        return len;

    while (len) {
        /* Complete our buffer as much as possible */
        tocopy = len;
        if (tocopy > BUF_SIZE - baum->in_buf_used)
            tocopy = BUF_SIZE - baum->in_buf_used;

        memcpy(baum->in_buf + baum->in_buf_used, buf, tocopy);
        baum->in_buf_used += tocopy;
        buf += tocopy;
        len -= tocopy;

        /* Interpret it as much as possible */
        cur = 0;
        while (cur < baum->in_buf_used &&
                (eaten = baum_eat_packet(baum, baum->in_buf + cur, baum->in_buf_used - cur)))
            cur += eaten;

        /* Shift the remainder */
        if (cur) {
            memmove(baum->in_buf, baum->in_buf + cur, baum->in_buf_used - cur);
            baum->in_buf_used -= cur;
        }

        /* And continue if any data left */
    }
    return orig_len;
}

/* Send the key code to the other end */
static void baum_send_key(BaumChardev *baum, uint8_t type, uint8_t value)
{
    uint8_t packet[] = { type, value };
    DPRINTF("writing key %x %x\n", type, value);
    baum_write_packet(baum, packet, sizeof(packet));
}

static void baum_send_key2(BaumChardev *baum, uint8_t type, uint8_t value,
                           uint8_t value2)
{
    uint8_t packet[] = { type, value, value2 };
    DPRINTF("writing key %x %x\n", type, value);
    baum_write_packet(baum, packet, sizeof(packet));
}

/* We got some data on the BrlAPI socket */
static void baum_chr_read(void *opaque)
{
    BaumChardev *baum = BAUM_CHARDEV(opaque);
    brlapi_keyCode_t code;
    int ret;
    if (!baum->brlapi)
        return;
    if (!baum_deferred_init(baum))
        return;
    while ((ret = brlapi__readKey(baum->brlapi, 0, &code)) == 1) {
        DPRINTF("got key %"BRLAPI_PRIxKEYCODE"\n", code);
        /* Emulate */
        switch (code & BRLAPI_KEY_TYPE_MASK) {
        case BRLAPI_KEY_TYPE_CMD:
            switch (code & BRLAPI_KEY_CMD_BLK_MASK) {
            case BRLAPI_KEY_CMD_ROUTE:
                baum_send_key(baum, BAUM_RSP_RoutingKey, (code & BRLAPI_KEY_CMD_ARG_MASK)+1);
                baum_send_key(baum, BAUM_RSP_RoutingKey, 0);
                break;
            case 0:
                switch (code & BRLAPI_KEY_CMD_ARG_MASK) {
                case BRLAPI_KEY_CMD_FWINLT:
                    baum_send_key(baum, BAUM_RSP_TopKeys, BAUM_TL2);
                    baum_send_key(baum, BAUM_RSP_TopKeys, 0);
                    break;
                case BRLAPI_KEY_CMD_FWINRT:
                    baum_send_key(baum, BAUM_RSP_TopKeys, BAUM_TR2);
                    baum_send_key(baum, BAUM_RSP_TopKeys, 0);
                    break;
                case BRLAPI_KEY_CMD_LNUP:
                    baum_send_key(baum, BAUM_RSP_TopKeys, BAUM_TR1);
                    baum_send_key(baum, BAUM_RSP_TopKeys, 0);
                    break;
                case BRLAPI_KEY_CMD_LNDN:
                    baum_send_key(baum, BAUM_RSP_TopKeys, BAUM_TR3);
                    baum_send_key(baum, BAUM_RSP_TopKeys, 0);
                    break;
                case BRLAPI_KEY_CMD_TOP:
                    baum_send_key(baum, BAUM_RSP_TopKeys, BAUM_TL1|BAUM_TR1);
                    baum_send_key(baum, BAUM_RSP_TopKeys, 0);
                    break;
                case BRLAPI_KEY_CMD_BOT:
                    baum_send_key(baum, BAUM_RSP_TopKeys, BAUM_TL3|BAUM_TR3);
                    baum_send_key(baum, BAUM_RSP_TopKeys, 0);
                    break;
                case BRLAPI_KEY_CMD_TOP_LEFT:
                    baum_send_key(baum, BAUM_RSP_TopKeys, BAUM_TL2|BAUM_TR1);
                    baum_send_key(baum, BAUM_RSP_TopKeys, 0);
                    break;
                case BRLAPI_KEY_CMD_BOT_LEFT:
                    baum_send_key(baum, BAUM_RSP_TopKeys, BAUM_TL2|BAUM_TR3);
                    baum_send_key(baum, BAUM_RSP_TopKeys, 0);
                    break;
                case BRLAPI_KEY_CMD_HOME:
                    baum_send_key(baum, BAUM_RSP_TopKeys, BAUM_TL2|BAUM_TR1|BAUM_TR3);
                    baum_send_key(baum, BAUM_RSP_TopKeys, 0);
                    break;
                case BRLAPI_KEY_CMD_PREFMENU:
                    baum_send_key(baum, BAUM_RSP_TopKeys, BAUM_TL1|BAUM_TL3|BAUM_TR1);
                    baum_send_key(baum, BAUM_RSP_TopKeys, 0);
                    break;
                }
            }
            break;
        case BRLAPI_KEY_TYPE_SYM:
            {
                brlapi_keyCode_t keysym = code & BRLAPI_KEY_CODE_MASK;
                if (keysym < 0x100) {
                    uint8_t dots = nabcc_translation[ASCII2DOTS][keysym];
                    if (dots) {
                        baum_send_key2(baum, BAUM_RSP_EntryKeys, 0, dots);
                        baum_send_key2(baum, BAUM_RSP_EntryKeys, 0, 0);
                    }
                }
                break;
            }
        }
    }
    if (ret == -1 && (brlapi_errno != BRLAPI_ERROR_LIBCERR || errno != EINTR)) {
        brlapi_perror("baum: brlapi_readKey");
        brlapi__closeConnection(baum->brlapi);
        g_free(baum->brlapi);
        baum->brlapi = NULL;
    }
}

static void char_braille_finalize(Object *obj)
{
    BaumChardev *baum = BAUM_CHARDEV(obj);

    timer_free(baum->cellCount_timer);
    if (baum->brlapi) {
        brlapi__closeConnection(baum->brlapi);
        g_free(baum->brlapi);
    }
}

static void baum_chr_open(Chardev *chr,
                          ChardevBackend *backend,
                          bool *be_opened,
                          Error **errp)
{
    BaumChardev *baum = BAUM_CHARDEV(chr);
    brlapi_handle_t *handle;

    handle = g_malloc0(brlapi_getHandleSize());
    baum->brlapi = handle;

    baum->brlapi_fd = brlapi__openConnection(handle, NULL, NULL);
    if (baum->brlapi_fd == -1) {
        error_setg(errp, "brlapi__openConnection: %s",
                   brlapi_strerror(brlapi_error_location()));
        g_free(handle);
        return;
    }
    baum->deferred_init = 0;

    baum->cellCount_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, baum_cellCount_timer_cb, baum);

    qemu_set_fd_handler(baum->brlapi_fd, baum_chr_read, NULL, baum);
}

static void char_braille_class_init(ObjectClass *oc, void *data)
{
    ChardevClass *cc = CHARDEV_CLASS(oc);

    cc->open = baum_chr_open;
    cc->chr_write = baum_chr_write;
    cc->chr_accept_input = baum_chr_accept_input;
}

static const TypeInfo char_braille_type_info = {
    .name = TYPE_CHARDEV_BRAILLE,
    .parent = TYPE_CHARDEV,
    .instance_size = sizeof(BaumChardev),
    .instance_finalize = char_braille_finalize,
    .class_init = char_braille_class_init,
};

static void register_types(void)
{
    type_register_static(&char_braille_type_info);
}

type_init(register_types);
