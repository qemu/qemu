/*
 * QEMU HID devices
 *
 * Copyright (c) 2005 Fabrice Bellard
 * Copyright (c) 2007 OpenMoko, Inc.  (andrew@openedhand.com)
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
#include "console.h"
#include "qemu-timer.h"
#include "hid.h"

#define HID_USAGE_ERROR_ROLLOVER        0x01
#define HID_USAGE_POSTFAIL              0x02
#define HID_USAGE_ERROR_UNDEFINED       0x03

/* Indices are QEMU keycodes, values are from HID Usage Table.  Indices
 * above 0x80 are for keys that come after 0xe0 or 0xe1+0x1d or 0xe1+0x9d.  */
static const uint8_t hid_usage_keys[0x100] = {
    0x00, 0x29, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23,
    0x24, 0x25, 0x26, 0x27, 0x2d, 0x2e, 0x2a, 0x2b,
    0x14, 0x1a, 0x08, 0x15, 0x17, 0x1c, 0x18, 0x0c,
    0x12, 0x13, 0x2f, 0x30, 0x28, 0xe0, 0x04, 0x16,
    0x07, 0x09, 0x0a, 0x0b, 0x0d, 0x0e, 0x0f, 0x33,
    0x34, 0x35, 0xe1, 0x31, 0x1d, 0x1b, 0x06, 0x19,
    0x05, 0x11, 0x10, 0x36, 0x37, 0x38, 0xe5, 0x55,
    0xe2, 0x2c, 0x32, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e,
    0x3f, 0x40, 0x41, 0x42, 0x43, 0x53, 0x47, 0x5f,
    0x60, 0x61, 0x56, 0x5c, 0x5d, 0x5e, 0x57, 0x59,
    0x5a, 0x5b, 0x62, 0x63, 0x00, 0x00, 0x00, 0x44,
    0x45, 0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e,
    0xe8, 0xe9, 0x71, 0x72, 0x73, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x85, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0xe3, 0xe7, 0x65,

    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x58, 0xe4, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x54, 0x00, 0x46,
    0xe6, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x48, 0x00, 0x4a,
    0x52, 0x4b, 0x00, 0x50, 0x00, 0x4f, 0x00, 0x4d,
    0x51, 0x4e, 0x49, 0x4c, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0xe3, 0xe7, 0x65, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

bool hid_has_events(HIDState *hs)
{
    return hs->n > 0;
}

void hid_set_next_idle(HIDState *hs, int64_t curtime)
{
    hs->next_idle_clock = curtime + (get_ticks_per_sec() * hs->idle * 4) / 1000;
}

static void hid_pointer_event_clear(HIDPointerEvent *e, int buttons)
{
    e->xdx = e->ydy = e->dz = 0;
    e->buttons_state = buttons;
}

static void hid_pointer_event_combine(HIDPointerEvent *e, int xyrel,
                                      int x1, int y1, int z1) {
    if (xyrel) {
        e->xdx += x1;
        e->ydy += y1;
    } else {
        e->xdx = x1;
        e->ydy = y1;
        /* Windows drivers do not like the 0/0 position and ignore such
         * events. */
        if (!(x1 | y1)) {
            x1 = 1;
        }
    }
    e->dz += z1;
}

static void hid_pointer_event(void *opaque,
                              int x1, int y1, int z1, int buttons_state)
{
    HIDState *hs = opaque;
    unsigned use_slot = (hs->head + hs->n - 1) & QUEUE_MASK;
    unsigned previous_slot = (use_slot - 1) & QUEUE_MASK;

    /* We combine events where feasible to keep the queue small.  We shouldn't
     * combine anything with the first event of a particular button state, as
     * that would change the location of the button state change.  When the
     * queue is empty, a second event is needed because we don't know if
     * the first event changed the button state.  */
    if (hs->n == QUEUE_LENGTH) {
        /* Queue full.  Discard old button state, combine motion normally.  */
        hs->ptr.queue[use_slot].buttons_state = buttons_state;
    } else if (hs->n < 2 ||
               hs->ptr.queue[use_slot].buttons_state != buttons_state ||
               hs->ptr.queue[previous_slot].buttons_state !=
               hs->ptr.queue[use_slot].buttons_state) {
        /* Cannot or should not combine, so add an empty item to the queue.  */
        QUEUE_INCR(use_slot);
        hs->n++;
        hid_pointer_event_clear(&hs->ptr.queue[use_slot], buttons_state);
    }
    hid_pointer_event_combine(&hs->ptr.queue[use_slot],
                              hs->kind == HID_MOUSE,
                              x1, y1, z1);
    hs->event(hs);
}

static void hid_keyboard_event(void *opaque, int keycode)
{
    HIDState *hs = opaque;
    int slot;

    if (hs->n == QUEUE_LENGTH) {
        fprintf(stderr, "usb-kbd: warning: key event queue full\n");
        return;
    }
    slot = (hs->head + hs->n) & QUEUE_MASK; hs->n++;
    hs->kbd.keycodes[slot] = keycode;
    hs->event(hs);
}

static void hid_keyboard_process_keycode(HIDState *hs)
{
    uint8_t hid_code, key;
    int i, keycode, slot;

    if (hs->n == 0) {
        return;
    }
    slot = hs->head & QUEUE_MASK; QUEUE_INCR(hs->head); hs->n--;
    keycode = hs->kbd.keycodes[slot];

    key = keycode & 0x7f;
    hid_code = hid_usage_keys[key | ((hs->kbd.modifiers >> 1) & (1 << 7))];
    hs->kbd.modifiers &= ~(1 << 8);

    switch (hid_code) {
    case 0x00:
        return;

    case 0xe0:
        if (hs->kbd.modifiers & (1 << 9)) {
            hs->kbd.modifiers ^= 3 << 8;
            return;
        }
    case 0xe1 ... 0xe7:
        if (keycode & (1 << 7)) {
            hs->kbd.modifiers &= ~(1 << (hid_code & 0x0f));
            return;
        }
    case 0xe8 ... 0xef:
        hs->kbd.modifiers |= 1 << (hid_code & 0x0f);
        return;
    }

    if (keycode & (1 << 7)) {
        for (i = hs->kbd.keys - 1; i >= 0; i--) {
            if (hs->kbd.key[i] == hid_code) {
                hs->kbd.key[i] = hs->kbd.key[-- hs->kbd.keys];
                hs->kbd.key[hs->kbd.keys] = 0x00;
                break;
            }
        }
        if (i < 0) {
            return;
        }
    } else {
        for (i = hs->kbd.keys - 1; i >= 0; i--) {
            if (hs->kbd.key[i] == hid_code) {
                break;
            }
        }
        if (i < 0) {
            if (hs->kbd.keys < sizeof(hs->kbd.key)) {
                hs->kbd.key[hs->kbd.keys++] = hid_code;
            }
        } else {
            return;
        }
    }
}

static inline int int_clamp(int val, int vmin, int vmax)
{
    if (val < vmin) {
        return vmin;
    } else if (val > vmax) {
        return vmax;
    } else {
        return val;
    }
}

void hid_pointer_activate(HIDState *hs)
{
    if (!hs->ptr.mouse_grabbed) {
        qemu_activate_mouse_event_handler(hs->ptr.eh_entry);
        hs->ptr.mouse_grabbed = 1;
    }
}

int hid_pointer_poll(HIDState *hs, uint8_t *buf, int len)
{
    int dx, dy, dz, b, l;
    int index;
    HIDPointerEvent *e;

    hid_pointer_activate(hs);

    /* When the buffer is empty, return the last event.  Relative
       movements will all be zero.  */
    index = (hs->n ? hs->head : hs->head - 1);
    e = &hs->ptr.queue[index & QUEUE_MASK];

    if (hs->kind == HID_MOUSE) {
        dx = int_clamp(e->xdx, -127, 127);
        dy = int_clamp(e->ydy, -127, 127);
        e->xdx -= dx;
        e->ydy -= dy;
    } else {
        dx = e->xdx;
        dy = e->ydy;
    }
    dz = int_clamp(e->dz, -127, 127);
    e->dz -= dz;

    b = 0;
    if (e->buttons_state & MOUSE_EVENT_LBUTTON) {
        b |= 0x01;
    }
    if (e->buttons_state & MOUSE_EVENT_RBUTTON) {
        b |= 0x02;
    }
    if (e->buttons_state & MOUSE_EVENT_MBUTTON) {
        b |= 0x04;
    }

    if (hs->n &&
        !e->dz &&
        (hs->kind == HID_TABLET || (!e->xdx && !e->ydy))) {
        /* that deals with this event */
        QUEUE_INCR(hs->head);
        hs->n--;
    }

    /* Appears we have to invert the wheel direction */
    dz = 0 - dz;
    l = 0;
    switch (hs->kind) {
    case HID_MOUSE:
        if (len > l) {
            buf[l++] = b;
        }
        if (len > l) {
            buf[l++] = dx;
        }
        if (len > l) {
            buf[l++] = dy;
        }
        if (len > l) {
            buf[l++] = dz;
        }
        break;

    case HID_TABLET:
        if (len > l) {
            buf[l++] = b;
        }
        if (len > l) {
            buf[l++] = dx & 0xff;
        }
        if (len > l) {
            buf[l++] = dx >> 8;
        }
        if (len > l) {
            buf[l++] = dy & 0xff;
        }
        if (len > l) {
            buf[l++] = dy >> 8;
        }
        if (len > l) {
            buf[l++] = dz;
        }
        break;

    default:
        abort();
    }

    return l;
}

int hid_keyboard_poll(HIDState *hs, uint8_t *buf, int len)
{
    if (len < 2) {
        return 0;
    }

    hid_keyboard_process_keycode(hs);

    buf[0] = hs->kbd.modifiers & 0xff;
    buf[1] = 0;
    if (hs->kbd.keys > 6) {
        memset(buf + 2, HID_USAGE_ERROR_ROLLOVER, MIN(8, len) - 2);
    } else {
        memcpy(buf + 2, hs->kbd.key, MIN(8, len) - 2);
    }

    return MIN(8, len);
}

int hid_keyboard_write(HIDState *hs, uint8_t *buf, int len)
{
    if (len > 0) {
        int ledstate = 0;
        /* 0x01: Num Lock LED
         * 0x02: Caps Lock LED
         * 0x04: Scroll Lock LED
         * 0x08: Compose LED
         * 0x10: Kana LED */
        hs->kbd.leds = buf[0];
        if (hs->kbd.leds & 0x04) {
            ledstate |= QEMU_SCROLL_LOCK_LED;
        }
        if (hs->kbd.leds & 0x01) {
            ledstate |= QEMU_NUM_LOCK_LED;
        }
        if (hs->kbd.leds & 0x02) {
            ledstate |= QEMU_CAPS_LOCK_LED;
        }
        kbd_put_ledstate(ledstate);
    }
    return 0;
}

void hid_reset(HIDState *hs)
{
    switch (hs->kind) {
    case HID_KEYBOARD:
        memset(hs->kbd.keycodes, 0, sizeof(hs->kbd.keycodes));
        memset(hs->kbd.key, 0, sizeof(hs->kbd.key));
        hs->kbd.keys = 0;
        break;
    case HID_MOUSE:
    case HID_TABLET:
        memset(hs->ptr.queue, 0, sizeof(hs->ptr.queue));
        break;
    }
    hs->head = 0;
    hs->n = 0;
    hs->protocol = 1;
    hs->idle = 0;
}

void hid_free(HIDState *hs)
{
    switch (hs->kind) {
    case HID_KEYBOARD:
        qemu_remove_kbd_event_handler();
        break;
    case HID_MOUSE:
    case HID_TABLET:
        qemu_remove_mouse_event_handler(hs->ptr.eh_entry);
        break;
    }
}

void hid_init(HIDState *hs, int kind, HIDEventFunc event)
{
    hs->kind = kind;
    hs->event = event;

    if (hs->kind == HID_KEYBOARD) {
        qemu_add_kbd_event_handler(hid_keyboard_event, hs);
    } else if (hs->kind == HID_MOUSE) {
        hs->ptr.eh_entry = qemu_add_mouse_event_handler(hid_pointer_event, hs,
                                                        0, "QEMU HID Mouse");
    } else if (hs->kind == HID_TABLET) {
        hs->ptr.eh_entry = qemu_add_mouse_event_handler(hid_pointer_event, hs,
                                                        1, "QEMU HID Tablet");
    }
}
