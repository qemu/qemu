/*
 * QEMU ADB support
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
#include "vl.h"

/* ADB commands */
#define ADB_BUSRESET		0x00
#define ADB_FLUSH               0x01
#define ADB_WRITEREG		0x08
#define ADB_READREG		0x0c

/* ADB device commands */
#define ADB_CMD_SELF_TEST		0xff
#define ADB_CMD_CHANGE_ID		0xfe
#define ADB_CMD_CHANGE_ID_AND_ACT	0xfd
#define ADB_CMD_CHANGE_ID_AND_ENABLE	0x00

/* ADB default device IDs (upper 4 bits of ADB command byte) */
#define ADB_DONGLE	1
#define ADB_KEYBOARD	2
#define ADB_MOUSE	3
#define ADB_TABLET	4
#define ADB_MODEM	5
#define ADB_MISC	7

#define ADB_RET_OK			0
#define ADB_RET_INUSE			1
#define ADB_RET_NOTPRESENT		2
#define ADB_RET_TIMEOUT			3
#define ADB_RET_UNEXPECTED_RESULT	4
#define ADB_RET_REQUEST_ERROR		5
#define ADB_RET_BUS_ERROR		6


static void adb_send_packet1(ADBBusState *s, uint8_t reply)
{
    adb_send_packet(s, &reply, 1);
}

void adb_receive_packet(ADBBusState *s, const uint8_t *buf, int len)
{
    ADBDevice *d;
    int devaddr, cmd, i;
    uint8_t obuf[4];

    cmd = buf[0] & 0xf;
    devaddr = buf[0] >> 4;
    if (buf[1] == ADB_BUSRESET) {
        obuf[0] = 0x00;
        obuf[1] = 0x00;
        adb_send_packet(s, obuf, 2);
        return;
    }
    if (cmd == ADB_FLUSH) {
        obuf[0] = 0x00;
        obuf[1] = 0x00;
        adb_send_packet(s, obuf, 2);
        return;
    }

    for(i = 0; i < s->nb_devices; i++) {
        d = &s->devices[i];
        if (d->devaddr == devaddr) {
            d->receive_packet(d, buf, len);
            return;
        }
    }
    adb_send_packet1(s, ADB_RET_NOTPRESENT);
}

ADBDevice *adb_register_device(ADBBusState *s, int devaddr, 
                               ADBDeviceReceivePacket *receive_packet, 
                               void *opaque)
{
    ADBDevice *d;
    if (s->nb_devices >= MAX_ADB_DEVICES)
        return NULL;
    d = &s->devices[s->nb_devices++];
    d->bus = s;
    d->devaddr = devaddr;
    d->receive_packet = receive_packet;
    d->opaque = opaque;
    return d;
}

/***************************************************************/
/* Keyboard ADB device */

static const uint8_t pc_to_adb_keycode[256] = {
  0, 53, 18, 19, 20, 21, 23, 22, 26, 28, 25, 29, 27, 24, 51, 48,
 12, 13, 14, 15, 17, 16, 32, 34, 31, 35, 33, 30, 36, 54,  0,  1,
  2,  3,  5,  4, 38, 40, 37, 41, 39, 50, 56, 42,  6,  7,  8,  9,
 11, 45, 46, 43, 47, 44,123, 67, 58, 49, 57,122,120, 99,118, 96,
 97, 98,100,101,109, 71,107, 89, 91, 92, 78, 86, 87, 88, 69, 83,
 84, 85, 82, 65,  0,  0, 10,103,111,  0,  0,  0,  0,  0,  0,  0,
 76,125, 75,105,124,110,115, 62,116, 59, 60,119, 61,121,114,117,
  0,  0,  0,  0,127, 81,  0,113,  0,  0,  0,  0, 95, 55,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0, 94,  0, 93,  0,  0,  0,  0,  0,  0,104,102,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
};

static void adb_kbd_put_keycode(void *opaque, int keycode)
{
    static int ext_keycode;
    ADBDevice *d = opaque;
    uint8_t buf[4];
    int adb_keycode;
    
    if (keycode == 0xe0) {
        ext_keycode = 1;
    } else {
        
        if (ext_keycode)
            adb_keycode =  pc_to_adb_keycode[keycode | 0x80];
        else
            adb_keycode =  pc_to_adb_keycode[keycode & 0x7f];
            
        buf[0] = 0x40;
        buf[1] = (d->devaddr << 4) | 0x0c;
        buf[2] = adb_keycode | (keycode & 0x80);
        buf[3] = 0xff;
        adb_send_packet(d->bus, buf, 4);
        ext_keycode = 0;
    }
}

static void adb_kbd_receive_packet(ADBDevice *d, const uint8_t *buf, int len)
{
    int cmd, reg;
    uint8_t obuf[4];

    cmd = buf[0] & 0xc;
    reg = buf[0] & 0x3;
    switch(cmd) {
    case ADB_WRITEREG:
        switch(reg) {
        case 2:
            /* LED status */
            adb_send_packet1(d->bus, ADB_RET_OK);
            break;
        case 3:
            switch(buf[2]) {
            case ADB_CMD_SELF_TEST:
                adb_send_packet1(d->bus, ADB_RET_OK);
                break;
            case ADB_CMD_CHANGE_ID:
            case ADB_CMD_CHANGE_ID_AND_ACT:
            case ADB_CMD_CHANGE_ID_AND_ENABLE:
                d->devaddr = buf[1] & 0xf;
                adb_send_packet1(d->bus, ADB_RET_OK);
                break;
            default:
                /* XXX: check this */
                d->devaddr = buf[1] & 0xf;
                d->handler = buf[2];
                adb_send_packet1(d->bus, ADB_RET_OK);
                break;
            }
        }
        break;
    case ADB_READREG:
        switch(reg) {
        case 1:
            adb_send_packet1(d->bus, ADB_RET_OK);
            break;
        case 2:
            obuf[0] = ADB_RET_OK;
            obuf[1] = 0x00; /* XXX: check this */
            obuf[2] = 0x07; /* led status */
            adb_send_packet(d->bus, obuf, 3);
            break;
        case 3:
            obuf[0] = ADB_RET_OK;
            obuf[1] = d->handler;
            obuf[2] = d->devaddr;
            adb_send_packet(d->bus, obuf, 3);
            break;
        }
        break;
    }
}

void adb_kbd_init(ADBBusState *bus)
{
    ADBDevice *d;

    d = adb_register_device(bus, ADB_KEYBOARD, adb_kbd_receive_packet, NULL);
    qemu_add_kbd_event_handler(adb_kbd_put_keycode, d);
}

/***************************************************************/
/* Mouse ADB device */

static void adb_mouse_event(void *opaque,
                            int dx1, int dy1, int dz1, int buttons_state)
{
    ADBDevice *d = opaque;
    uint8_t buf[4];
    int dx, dy;

    dx = dx1;
    if (dx < -63)
        dx = -63;
    else if (dx > 63)
        dx = 63;

    dy = dy1;
    if (dy < -63)
        dy = -63;
    else if (dy > 63)
        dy = 63;

    dx &= 0x7f;
    dy &= 0x7f;

    if (buttons_state & MOUSE_EVENT_LBUTTON)
        dy |= 0x80;
    if (buttons_state & MOUSE_EVENT_RBUTTON)
        dx |= 0x80;

    buf[0] = 0x40;
    buf[1] = (d->devaddr << 4) | 0x0c;
    buf[2] = dy;
    buf[3] = dx;
    adb_send_packet(d->bus, buf, 4);
}

static void adb_mouse_receive_packet(ADBDevice *d, const uint8_t *buf, int len)
{
    int cmd, reg;
    uint8_t obuf[4];

    cmd = buf[0] & 0xc;
    reg = buf[0] & 0x3;
    switch(cmd) {
    case ADB_WRITEREG:
        switch(reg) {
        case 2:
            adb_send_packet1(d->bus, ADB_RET_OK);
            break;
        case 3:
            switch(buf[2]) {
            case ADB_CMD_SELF_TEST:
                adb_send_packet1(d->bus, ADB_RET_OK);
                break;
            case ADB_CMD_CHANGE_ID:
            case ADB_CMD_CHANGE_ID_AND_ACT:
            case ADB_CMD_CHANGE_ID_AND_ENABLE:
                d->devaddr = buf[1] & 0xf;
                adb_send_packet1(d->bus, ADB_RET_OK);
                break;
            default:
                /* XXX: check this */
                d->devaddr = buf[1] & 0xf;
                adb_send_packet1(d->bus, ADB_RET_OK);
                break;
            }
        }
        break;
    case ADB_READREG:
        switch(reg) {
        case 1:
            adb_send_packet1(d->bus, ADB_RET_OK);
            break;
        case 3:
            obuf[0] = ADB_RET_OK;
            obuf[1] = d->handler;
            obuf[2] = d->devaddr;
            adb_send_packet(d->bus, obuf, 3);
            break;
        }
        break;
    }
}

void adb_mouse_init(ADBBusState *bus)
{
    ADBDevice *d;

    d = adb_register_device(bus, ADB_MOUSE, adb_mouse_receive_packet, NULL);
    qemu_add_mouse_event_handler(adb_mouse_event, d);
}
