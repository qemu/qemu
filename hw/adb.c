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
#include "hw.h"
#include "ppc_mac.h"
#include "console.h"

/* debug ADB */
//#define DEBUG_ADB

#ifdef DEBUG_ADB
#define ADB_DPRINTF(fmt, ...) \
do { printf("ADB: " fmt , ## __VA_ARGS__); } while (0)
#else
#define ADB_DPRINTF(fmt, ...)
#endif

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

/* error codes */
#define ADB_RET_NOTPRESENT (-2)

int adb_request(ADBBusState *s, uint8_t *obuf, const uint8_t *buf, int len)
{
    ADBDevice *d;
    int devaddr, cmd, i;

    cmd = buf[0] & 0xf;
    if (cmd == ADB_BUSRESET) {
        for(i = 0; i < s->nb_devices; i++) {
            d = &s->devices[i];
            if (d->devreset) {
                d->devreset(d);
            }
        }
        return 0;
    }
    devaddr = buf[0] >> 4;
    for(i = 0; i < s->nb_devices; i++) {
        d = &s->devices[i];
        if (d->devaddr == devaddr) {
            return d->devreq(d, obuf, buf, len);
        }
    }
    return ADB_RET_NOTPRESENT;
}

/* XXX: move that to cuda ? */
int adb_poll(ADBBusState *s, uint8_t *obuf)
{
    ADBDevice *d;
    int olen, i;
    uint8_t buf[1];

    olen = 0;
    for(i = 0; i < s->nb_devices; i++) {
        if (s->poll_index >= s->nb_devices)
            s->poll_index = 0;
        d = &s->devices[s->poll_index];
        buf[0] = ADB_READREG | (d->devaddr << 4);
        olen = adb_request(s, obuf + 1, buf, 1);
        /* if there is data, we poll again the same device */
        if (olen > 0) {
            obuf[0] = buf[0];
            olen++;
            break;
        }
        s->poll_index++;
    }
    return olen;
}

ADBDevice *adb_register_device(ADBBusState *s, int devaddr,
                               ADBDeviceRequest *devreq,
                               ADBDeviceReset *devreset,
                               void *opaque)
{
    ADBDevice *d;
    if (s->nb_devices >= MAX_ADB_DEVICES)
        return NULL;
    d = &s->devices[s->nb_devices++];
    d->bus = s;
    d->devaddr = devaddr;
    d->devreq = devreq;
    d->devreset = devreset;
    d->opaque = opaque;
    qemu_register_reset((QEMUResetHandler *)devreset, d);
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
            obuf[0] = adb_keycode | (keycode & 0x80);
            /* NOTE: could put a second keycode if needed */
            obuf[1] = 0xff;
            olen = 2;
            ext_keycode = 0;
            break;
        }
    }
    return olen;
}

static int adb_kbd_request(ADBDevice *d, uint8_t *obuf,
                           const uint8_t *buf, int len)
{
    KBDState *s = d->opaque;
    int cmd, reg, olen;

    if ((buf[0] & 0x0f) == ADB_FLUSH) {
        /* flush keyboard fifo */
        s->wptr = s->rptr = s->count = 0;
        return 0;
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
            obuf[0] = d->handler;
            obuf[1] = d->devaddr;
            olen = 2;
            break;
        }
        break;
    }
    return olen;
}

static const VMStateDescription vmstate_adb_kbd = {
    .name = "adb_kbd",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_BUFFER(data, KBDState),
        VMSTATE_INT32(rptr, KBDState),
        VMSTATE_INT32(wptr, KBDState),
        VMSTATE_INT32(count, KBDState),
        VMSTATE_END_OF_LIST()
    }
};

static int adb_kbd_reset(ADBDevice *d)
{
    KBDState *s = d->opaque;

    d->handler = 1;
    d->devaddr = ADB_KEYBOARD;
    memset(s, 0, sizeof(KBDState));

    return 0;
}

void adb_kbd_init(ADBBusState *bus)
{
    ADBDevice *d;
    KBDState *s;
    s = g_malloc0(sizeof(KBDState));
    d = adb_register_device(bus, ADB_KEYBOARD, adb_kbd_request,
                            adb_kbd_reset, s);
    qemu_add_kbd_event_handler(adb_kbd_put_keycode, d);
    vmstate_register(NULL, -1, &vmstate_adb_kbd, s);
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

    if (!(s->buttons_state & MOUSE_EVENT_LBUTTON))
        dy |= 0x80;
    if (!(s->buttons_state & MOUSE_EVENT_RBUTTON))
        dx |= 0x80;

    obuf[0] = dy;
    obuf[1] = dx;
    return 2;
}

static int adb_mouse_request(ADBDevice *d, uint8_t *obuf,
                             const uint8_t *buf, int len)
{
    MouseState *s = d->opaque;
    int cmd, reg, olen;

    if ((buf[0] & 0x0f) == ADB_FLUSH) {
        /* flush mouse fifo */
        s->buttons_state = s->last_buttons_state;
        s->dx = 0;
        s->dy = 0;
        s->dz = 0;
        return 0;
    }

    cmd = buf[0] & 0xc;
    reg = buf[0] & 0x3;
    olen = 0;
    switch(cmd) {
    case ADB_WRITEREG:
        ADB_DPRINTF("write reg %d val 0x%2.2x\n", reg, buf[1]);
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
        case 0:
            olen = adb_mouse_poll(d, obuf);
            break;
        case 1:
            break;
        case 3:
            obuf[0] = d->handler;
            obuf[1] = d->devaddr;
            olen = 2;
            break;
        }
        ADB_DPRINTF("read reg %d obuf[0] 0x%2.2x obuf[1] 0x%2.2x\n", reg,
                    obuf[0], obuf[1]);
        break;
    }
    return olen;
}

static int adb_mouse_reset(ADBDevice *d)
{
    MouseState *s = d->opaque;

    d->handler = 2;
    d->devaddr = ADB_MOUSE;
    memset(s, 0, sizeof(MouseState));

    return 0;
}

static const VMStateDescription vmstate_adb_mouse = {
    .name = "adb_mouse",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_INT32(buttons_state, MouseState),
        VMSTATE_INT32(last_buttons_state, MouseState),
        VMSTATE_INT32(dx, MouseState),
        VMSTATE_INT32(dy, MouseState),
        VMSTATE_INT32(dz, MouseState),
        VMSTATE_END_OF_LIST()
    }
};

void adb_mouse_init(ADBBusState *bus)
{
    ADBDevice *d;
    MouseState *s;

    s = g_malloc0(sizeof(MouseState));
    d = adb_register_device(bus, ADB_MOUSE, adb_mouse_request,
                            adb_mouse_reset, s);
    qemu_add_mouse_event_handler(adb_mouse_event, d, 0, "QEMU ADB Mouse");
    vmstate_register(NULL, -1, &vmstate_adb_mouse, s);
}
