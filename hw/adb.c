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

int adb_request(ADBBusState *s, uint8_t *obuf, const uint8_t *buf, int len)
{
    ADBDevice *d;
    int devaddr, cmd, i;

    cmd = buf[0] & 0xf;
    devaddr = buf[0] >> 4;
    if (buf[1] == ADB_BUSRESET) {
        obuf[0] = 0x00;
        obuf[1] = 0x00;
        return 2;
    }
    if (cmd == ADB_FLUSH) {
        obuf[0] = 0x00;
        obuf[1] = 0x00;
        return 2;
    }

    for(i = 0; i < s->nb_devices; i++) {
        d = &s->devices[i];
        if (d->devaddr == devaddr) {
            return d->devreq(d, obuf, buf, len);
        }
    }
    return 0;
}

int adb_poll(ADBBusState *s, uint8_t *obuf)
{
    ADBDevice *d;
    int olen, i;

    olen = 0;
    for(i = 0; i < s->nb_devices; i++) {
        if (s->poll_index >= s->nb_devices)
            s->poll_index = 0;
        d = &s->devices[s->poll_index];
        olen = d->devreq(d, obuf, NULL, 0);
        s->poll_index++;
        if (olen)
            break;
    }
    return olen;
}

ADBDevice *adb_register_device(ADBBusState *s, int devaddr, 
                               ADBDeviceRequest *devreq, 
                               void *opaque)
{
    ADBDevice *d;
    if (s->nb_devices >= MAX_ADB_DEVICES)
        return NULL;
    d = &s->devices[s->nb_devices++];
    d->bus = s;
    d->devaddr = devaddr;
    d->devreq = devreq;
    d->opaque = opaque;
    return d;
}

/***************************************************************/
/* Keyboard ADB device */

typedef struct KBDState {
    uint8_t data[128];
    int rptr, wptr, count;
} KBDState;

static const uint8_t pc_to_adb_keycode[256] = {
  0, 53, 18, 19, 20, 21, 23, 22, 26, 28, 25, 29, 27, 24, 51, 48,
 12, 13, 14, 15, 17, 16, 32, 34, 31, 35, 33, 30, 36, 54,  0,  1,
  2,  3,  5,  4, 38, 40, 37, 41, 39, 50, 56, 42,  6,  7,  8,  9,
 11, 45, 46, 43, 47, 44,123, 67, 58, 49, 57,122,120, 99,118, 96,
 97, 98,100,101,109, 71,107, 89, 91, 92, 78, 86, 87, 88, 69, 83,
 84, 85, 82, 65,  0,  0, 10,103,111,  0,  0,110, 81,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0, 94,  0, 93,  0,  0,  0,  0,  0,  0,104,102,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 76,125,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,105,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0, 75,  0,  0,124,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,115, 62,116,  0, 59,  0, 60,  0,119,
 61,121,114,117,  0,  0,  0,  0,  0,  0,  0, 55,126,  0,127,  0,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0, 95,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
};

static void adb_kbd_put_keycode(void *opaque, int keycode)
{
    ADBDevice *d = opaque;
    KBDState *s = d->opaque;

    if (s->count < sizeof(s->data)) {
        s->data[s->wptr] = keycode;
        if (++s->wptr == sizeof(s->data))
            s->wptr = 0;
        s->count++;
    }
}

static int adb_kbd_poll(ADBDevice *d, uint8_t *obuf)
{
    static int ext_keycode;
    KBDState *s = d->opaque;
    int adb_keycode, keycode;
    int olen;

    olen = 0;
    for(;;) {
        if (s->count == 0)
            break;
        keycode = s->data[s->rptr];
        if (++s->rptr == sizeof(s->data))
            s->rptr = 0;
        s->count--;

        if (keycode == 0xe0) {
            ext_keycode = 1;
        } else {
            if (ext_keycode)
                adb_keycode =  pc_to_adb_keycode[keycode | 0x80];
            else
                adb_keycode =  pc_to_adb_keycode[keycode & 0x7f];
            obuf[0] = (d->devaddr << 4) | 0x0c;
            obuf[1] = adb_keycode | (keycode & 0x80);
            obuf[2] = 0xff;
            olen = 3;
            ext_keycode = 0;
            break;
        }
    }
    return olen;
}

static int adb_kbd_request(ADBDevice *d, uint8_t *obuf,
                           const uint8_t *buf, int len)
{
    int cmd, reg, olen;

    if (!buf) {
        return adb_kbd_poll(d, obuf);
    }

    cmd = buf[0] & 0xc;
    reg = buf[0] & 0x3;
    olen = 0;
    switch(cmd) {
    case ADB_WRITEREG:
        switch(reg) {
        case 2:
            /* LED status */
            break;
        case 3:
            switch(buf[2]) {
            case ADB_CMD_SELF_TEST:
                break;
            case ADB_CMD_CHANGE_ID:
            case ADB_CMD_CHANGE_ID_AND_ACT:
            case ADB_CMD_CHANGE_ID_AND_ENABLE:
                d->devaddr = buf[1] & 0xf;
                break;
            default:
                /* XXX: check this */
                d->devaddr = buf[1] & 0xf;
                d->handler = buf[2];
                break;
            }
        }
        break;
    case ADB_READREG:
        switch(reg) {
        case 1:
            break;
        case 2:
            obuf[0] = 0x00; /* XXX: check this */
            obuf[1] = 0x07; /* led status */
            olen = 2;
            break;
        case 3:
            obuf[0] = d->handler;
            obuf[1] = d->devaddr;
            olen = 2;
            break;
        }
        break;
    }
    return olen;
}

void adb_kbd_init(ADBBusState *bus)
{
    ADBDevice *d;
    KBDState *s;
    s = qemu_mallocz(sizeof(KBDState));
    d = adb_register_device(bus, ADB_KEYBOARD, adb_kbd_request, s);
    d->handler = 1;
    qemu_add_kbd_event_handler(adb_kbd_put_keycode, d);
}

/***************************************************************/
/* Mouse ADB device */

typedef struct MouseState {
    int buttons_state, last_buttons_state;
    int dx, dy, dz;
} MouseState;

static void adb_mouse_event(void *opaque,
                            int dx1, int dy1, int dz1, int buttons_state)
{
    ADBDevice *d = opaque;
    MouseState *s = d->opaque;

    s->dx += dx1;
    s->dy += dy1;
    s->dz += dz1;
    s->buttons_state = buttons_state;
}


static int adb_mouse_poll(ADBDevice *d, uint8_t *obuf)
{
    MouseState *s = d->opaque;
    int dx, dy;

    if (s->last_buttons_state == s->buttons_state &&
        s->dx == 0 && s->dy == 0)
        return 0;
        
    dx = s->dx;
    if (dx < -63)
        dx = -63;
    else if (dx > 63)
        dx = 63;
    
    dy = s->dy;
    if (dy < -63)
        dy = -63;
    else if (dy > 63)
        dy = 63;
    
    s->dx -= dx;
    s->dy -= dy;
    s->last_buttons_state = s->buttons_state;
    
    dx &= 0x7f;
    dy &= 0x7f;
    
    if (s->buttons_state & MOUSE_EVENT_LBUTTON)
        dy |= 0x80;
    if (s->buttons_state & MOUSE_EVENT_RBUTTON)
        dx |= 0x80;
    
    obuf[0] = (d->devaddr << 4) | 0x0c;
    obuf[1] = dy;
    obuf[2] = dx;
    return 3;
}

static int adb_mouse_request(ADBDevice *d, uint8_t *obuf,
                             const uint8_t *buf, int len)
{
    int cmd, reg, olen;
    
    if (!buf) {
        return adb_mouse_poll(d, obuf);
    }

    cmd = buf[0] & 0xc;
    reg = buf[0] & 0x3;
    olen = 0;
    switch(cmd) {
    case ADB_WRITEREG:
        switch(reg) {
        case 2:
            break;
        case 3:
            switch(buf[2]) {
            case ADB_CMD_SELF_TEST:
                break;
            case ADB_CMD_CHANGE_ID:
            case ADB_CMD_CHANGE_ID_AND_ACT:
            case ADB_CMD_CHANGE_ID_AND_ENABLE:
                d->devaddr = buf[1] & 0xf;
                break;
            default:
                /* XXX: check this */
                d->devaddr = buf[1] & 0xf;
                break;
            }
        }
        break;
    case ADB_READREG:
        switch(reg) {
        case 1:
            break;
        case 3:
            obuf[0] = d->handler;
            obuf[1] = d->devaddr;
            olen = 2;
            break;
        }
        break;
    }
    return olen;
}

void adb_mouse_init(ADBBusState *bus)
{
    ADBDevice *d;
    MouseState *s;

    s = qemu_mallocz(sizeof(MouseState));
    d = adb_register_device(bus, ADB_MOUSE, adb_mouse_request, s);
    d->handler = 2;
    qemu_add_mouse_event_handler(adb_mouse_event, d);
}
