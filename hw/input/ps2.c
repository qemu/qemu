/*
 * QEMU PS/2 keyboard/mouse emulation
 *
 * Copyright (c) 2003 Fabrice Bellard
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
#include "qemu/log.h"
#include "hw/irq.h"
#include "hw/sysbus.h"
#include "hw/input/ps2.h"
#include "migration/vmstate.h"
#include "ui/console.h"
#include "ui/input.h"
#include "sysemu/reset.h"
#include "sysemu/runstate.h"
#include "qapi/error.h"

#include "trace.h"

/* Keyboard Commands */
#define KBD_CMD_SET_LEDS        0xED    /* Set keyboard leds */
#define KBD_CMD_ECHO            0xEE
#define KBD_CMD_SCANCODE        0xF0    /* Get/set scancode set */
#define KBD_CMD_GET_ID          0xF2    /* get keyboard ID */
#define KBD_CMD_SET_RATE        0xF3    /* Set typematic rate */
#define KBD_CMD_ENABLE          0xF4    /* Enable scanning */
#define KBD_CMD_RESET_DISABLE   0xF5    /* reset and disable scanning */
#define KBD_CMD_RESET_ENABLE    0xF6    /* reset and enable scanning */
#define KBD_CMD_RESET           0xFF    /* Reset */
#define KBD_CMD_SET_MAKE_BREAK  0xFC    /* Set Make and Break mode */
#define KBD_CMD_SET_TYPEMATIC   0xFA    /* Set Typematic Make and Break mode */

/* Keyboard Replies */
#define KBD_REPLY_POR       0xAA    /* Power on reset */
#define KBD_REPLY_ID        0xAB    /* Keyboard ID */
#define KBD_REPLY_ACK       0xFA    /* Command ACK */
#define KBD_REPLY_RESEND    0xFE    /* Command NACK, send the cmd again */

/* Mouse Commands */
#define AUX_SET_SCALE11     0xE6    /* Set 1:1 scaling */
#define AUX_SET_SCALE21     0xE7    /* Set 2:1 scaling */
#define AUX_SET_RES         0xE8    /* Set resolution */
#define AUX_GET_SCALE       0xE9    /* Get scaling factor */
#define AUX_SET_STREAM      0xEA    /* Set stream mode */
#define AUX_POLL            0xEB    /* Poll */
#define AUX_RESET_WRAP      0xEC    /* Reset wrap mode */
#define AUX_SET_WRAP        0xEE    /* Set wrap mode */
#define AUX_SET_REMOTE      0xF0    /* Set remote mode */
#define AUX_GET_TYPE        0xF2    /* Get type */
#define AUX_SET_SAMPLE      0xF3    /* Set sample rate */
#define AUX_ENABLE_DEV      0xF4    /* Enable aux device */
#define AUX_DISABLE_DEV     0xF5    /* Disable aux device */
#define AUX_SET_DEFAULT     0xF6
#define AUX_RESET           0xFF    /* Reset aux device */
#define AUX_ACK             0xFA    /* Command byte ACK. */

#define MOUSE_STATUS_REMOTE     0x40
#define MOUSE_STATUS_ENABLED    0x20
#define MOUSE_STATUS_SCALE21    0x10

#define PS2_QUEUE_SIZE      16  /* Queue size required by PS/2 protocol */
#define PS2_QUEUE_HEADROOM  8   /* Queue size for keyboard command replies */

/* Bits for 'modifiers' field in PS2KbdState */
#define MOD_CTRL_L  (1 << 0)
#define MOD_SHIFT_L (1 << 1)
#define MOD_ALT_L   (1 << 2)
#define MOD_CTRL_R  (1 << 3)
#define MOD_SHIFT_R (1 << 4)
#define MOD_ALT_R   (1 << 5)

static uint8_t translate_table[256] = {
    0xff, 0x43, 0x41, 0x3f, 0x3d, 0x3b, 0x3c, 0x58,
    0x64, 0x44, 0x42, 0x40, 0x3e, 0x0f, 0x29, 0x59,
    0x65, 0x38, 0x2a, 0x70, 0x1d, 0x10, 0x02, 0x5a,
    0x66, 0x71, 0x2c, 0x1f, 0x1e, 0x11, 0x03, 0x5b,
    0x67, 0x2e, 0x2d, 0x20, 0x12, 0x05, 0x04, 0x5c,
    0x68, 0x39, 0x2f, 0x21, 0x14, 0x13, 0x06, 0x5d,
    0x69, 0x31, 0x30, 0x23, 0x22, 0x15, 0x07, 0x5e,
    0x6a, 0x72, 0x32, 0x24, 0x16, 0x08, 0x09, 0x5f,
    0x6b, 0x33, 0x25, 0x17, 0x18, 0x0b, 0x0a, 0x60,
    0x6c, 0x34, 0x35, 0x26, 0x27, 0x19, 0x0c, 0x61,
    0x6d, 0x73, 0x28, 0x74, 0x1a, 0x0d, 0x62, 0x6e,
    0x3a, 0x36, 0x1c, 0x1b, 0x75, 0x2b, 0x63, 0x76,
    0x55, 0x56, 0x77, 0x78, 0x79, 0x7a, 0x0e, 0x7b,
    0x7c, 0x4f, 0x7d, 0x4b, 0x47, 0x7e, 0x7f, 0x6f,
    0x52, 0x53, 0x50, 0x4c, 0x4d, 0x48, 0x01, 0x45,
    0x57, 0x4e, 0x51, 0x4a, 0x37, 0x49, 0x46, 0x54,
    0x80, 0x81, 0x82, 0x41, 0x54, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
    0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
    0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,
    0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
    0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7,
    0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
    0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
    0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf,
    0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7,
    0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf,
    0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7,
    0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef,
    0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
    0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff,
};

static unsigned int ps2_modifier_bit(QKeyCode key)
{
    switch (key) {
    case Q_KEY_CODE_CTRL:
        return MOD_CTRL_L;
    case Q_KEY_CODE_CTRL_R:
        return MOD_CTRL_R;
    case Q_KEY_CODE_SHIFT:
        return MOD_SHIFT_L;
    case Q_KEY_CODE_SHIFT_R:
        return MOD_SHIFT_R;
    case Q_KEY_CODE_ALT:
        return MOD_ALT_L;
    case Q_KEY_CODE_ALT_R:
        return MOD_ALT_R;
    default:
        return 0;
    }
}

static void ps2_reset_queue(PS2State *s)
{
    PS2Queue *q = &s->queue;

    q->rptr = 0;
    q->wptr = 0;
    q->cwptr = -1;
    q->count = 0;
}

int ps2_queue_empty(PS2State *s)
{
    return s->queue.count == 0;
}

void ps2_queue_noirq(PS2State *s, int b)
{
    PS2Queue *q = &s->queue;

    if (q->count >= PS2_QUEUE_SIZE) {
        return;
    }

    q->data[q->wptr] = b;
    if (++q->wptr == PS2_BUFFER_SIZE) {
        q->wptr = 0;
    }
    q->count++;
}

static void ps2_raise_irq(PS2State *s)
{
    qemu_set_irq(s->irq, 1);
}

static void ps2_lower_irq(PS2State *s)
{
    qemu_set_irq(s->irq, 0);
}

void ps2_queue(PS2State *s, int b)
{
    if (PS2_QUEUE_SIZE - s->queue.count < 1) {
        return;
    }

    ps2_queue_noirq(s, b);
    ps2_raise_irq(s);
}

void ps2_queue_2(PS2State *s, int b1, int b2)
{
    if (PS2_QUEUE_SIZE - s->queue.count < 2) {
        return;
    }

    ps2_queue_noirq(s, b1);
    ps2_queue_noirq(s, b2);
    ps2_raise_irq(s);
}

void ps2_queue_3(PS2State *s, int b1, int b2, int b3)
{
    if (PS2_QUEUE_SIZE - s->queue.count < 3) {
        return;
    }

    ps2_queue_noirq(s, b1);
    ps2_queue_noirq(s, b2);
    ps2_queue_noirq(s, b3);
    ps2_raise_irq(s);
}

void ps2_queue_4(PS2State *s, int b1, int b2, int b3, int b4)
{
    if (PS2_QUEUE_SIZE - s->queue.count < 4) {
        return;
    }

    ps2_queue_noirq(s, b1);
    ps2_queue_noirq(s, b2);
    ps2_queue_noirq(s, b3);
    ps2_queue_noirq(s, b4);
    ps2_raise_irq(s);
}

static void ps2_cqueue_data(PS2Queue *q, int b)
{
    q->data[q->cwptr] = b;
    if (++q->cwptr >= PS2_BUFFER_SIZE) {
        q->cwptr = 0;
    }
    q->count++;
}

static void ps2_cqueue_1(PS2State *s, int b1)
{
    PS2Queue *q = &s->queue;

    q->rptr = (q->rptr - 1) & (PS2_BUFFER_SIZE - 1);
    q->cwptr = q->rptr;
    ps2_cqueue_data(q, b1);
    ps2_raise_irq(s);
}

static void ps2_cqueue_2(PS2State *s, int b1, int b2)
{
    PS2Queue *q = &s->queue;

    q->rptr = (q->rptr - 2) & (PS2_BUFFER_SIZE - 1);
    q->cwptr = q->rptr;
    ps2_cqueue_data(q, b1);
    ps2_cqueue_data(q, b2);
    ps2_raise_irq(s);
}

static void ps2_cqueue_3(PS2State *s, int b1, int b2, int b3)
{
    PS2Queue *q = &s->queue;

    q->rptr = (q->rptr - 3) & (PS2_BUFFER_SIZE - 1);
    q->cwptr = q->rptr;
    ps2_cqueue_data(q, b1);
    ps2_cqueue_data(q, b2);
    ps2_cqueue_data(q, b3);
    ps2_raise_irq(s);
}

static void ps2_cqueue_reset(PS2State *s)
{
    PS2Queue *q = &s->queue;
    int ccount;

    if (q->cwptr == -1) {
        return;
    }

    ccount = (q->cwptr - q->rptr) & (PS2_BUFFER_SIZE - 1);
    q->count -= ccount;
    q->rptr = q->cwptr;
    q->cwptr = -1;
}

/* keycode is the untranslated scancode in the current scancode set. */
static void ps2_put_keycode(void *opaque, int keycode)
{
    PS2KbdState *s = opaque;
    PS2State *ps = PS2_DEVICE(s);

    trace_ps2_put_keycode(opaque, keycode);
    qemu_system_wakeup_request(QEMU_WAKEUP_REASON_OTHER, NULL);

    if (s->translate) {
        if (keycode == 0xf0) {
            s->need_high_bit = true;
        } else if (s->need_high_bit) {
            ps2_queue(ps, translate_table[keycode] | 0x80);
            s->need_high_bit = false;
        } else {
            ps2_queue(ps, translate_table[keycode]);
        }
    } else {
        ps2_queue(ps, keycode);
    }
}

static void ps2_keyboard_event(DeviceState *dev, QemuConsole *src,
                               InputEvent *evt)
{
    PS2KbdState *s = (PS2KbdState *)dev;
    InputKeyEvent *key = evt->u.key.data;
    int qcode;
    uint16_t keycode = 0;
    int mod;

    /* do not process events while disabled to prevent stream corruption */
    if (!s->scan_enabled) {
        return;
    }

    qemu_system_wakeup_request(QEMU_WAKEUP_REASON_OTHER, NULL);
    assert(evt->type == INPUT_EVENT_KIND_KEY);
    qcode = qemu_input_key_value_to_qcode(key->key);

    mod = ps2_modifier_bit(qcode);
    trace_ps2_keyboard_event(s, qcode, key->down, mod,
                             s->modifiers, s->scancode_set, s->translate);
    if (key->down) {
        s->modifiers |= mod;
    } else {
        s->modifiers &= ~mod;
    }

    if (s->scancode_set == 1) {
        if (qcode == Q_KEY_CODE_PAUSE) {
            if (s->modifiers & (MOD_CTRL_L | MOD_CTRL_R)) {
                if (key->down) {
                    ps2_put_keycode(s, 0xe0);
                    ps2_put_keycode(s, 0x46);
                    ps2_put_keycode(s, 0xe0);
                    ps2_put_keycode(s, 0xc6);
                }
            } else {
                if (key->down) {
                    ps2_put_keycode(s, 0xe1);
                    ps2_put_keycode(s, 0x1d);
                    ps2_put_keycode(s, 0x45);
                    ps2_put_keycode(s, 0xe1);
                    ps2_put_keycode(s, 0x9d);
                    ps2_put_keycode(s, 0xc5);
                }
            }
        } else if (qcode == Q_KEY_CODE_PRINT) {
            if (s->modifiers & MOD_ALT_L) {
                if (key->down) {
                    ps2_put_keycode(s, 0xb8);
                    ps2_put_keycode(s, 0x38);
                    ps2_put_keycode(s, 0x54);
                } else {
                    ps2_put_keycode(s, 0xd4);
                    ps2_put_keycode(s, 0xb8);
                    ps2_put_keycode(s, 0x38);
                }
            } else if (s->modifiers & MOD_ALT_R) {
                if (key->down) {
                    ps2_put_keycode(s, 0xe0);
                    ps2_put_keycode(s, 0xb8);
                    ps2_put_keycode(s, 0xe0);
                    ps2_put_keycode(s, 0x38);
                    ps2_put_keycode(s, 0x54);
                } else {
                    ps2_put_keycode(s, 0xd4);
                    ps2_put_keycode(s, 0xe0);
                    ps2_put_keycode(s, 0xb8);
                    ps2_put_keycode(s, 0xe0);
                    ps2_put_keycode(s, 0x38);
                }
            } else if (s->modifiers & (MOD_SHIFT_L | MOD_CTRL_L |
                                       MOD_SHIFT_R | MOD_CTRL_R)) {
                if (key->down) {
                    ps2_put_keycode(s, 0xe0);
                    ps2_put_keycode(s, 0x37);
                } else {
                    ps2_put_keycode(s, 0xe0);
                    ps2_put_keycode(s, 0xb7);
                }
            } else {
                if (key->down) {
                    ps2_put_keycode(s, 0xe0);
                    ps2_put_keycode(s, 0x2a);
                    ps2_put_keycode(s, 0xe0);
                    ps2_put_keycode(s, 0x37);
                } else {
                    ps2_put_keycode(s, 0xe0);
                    ps2_put_keycode(s, 0xb7);
                    ps2_put_keycode(s, 0xe0);
                    ps2_put_keycode(s, 0xaa);
                }
            }
        } else {
            if (qcode < qemu_input_map_qcode_to_atset1_len) {
                keycode = qemu_input_map_qcode_to_atset1[qcode];
            }
            if (keycode) {
                if (keycode & 0xff00) {
                    ps2_put_keycode(s, keycode >> 8);
                }
                if (!key->down) {
                    keycode |= 0x80;
                }
                ps2_put_keycode(s, keycode & 0xff);
            } else {
                qemu_log_mask(LOG_UNIMP,
                              "ps2: ignoring key with qcode %d\n", qcode);
            }
        }
    } else if (s->scancode_set == 2) {
        if (qcode == Q_KEY_CODE_PAUSE) {
            if (s->modifiers & (MOD_CTRL_L | MOD_CTRL_R)) {
                if (key->down) {
                    ps2_put_keycode(s, 0xe0);
                    ps2_put_keycode(s, 0x7e);
                    ps2_put_keycode(s, 0xe0);
                    ps2_put_keycode(s, 0xf0);
                    ps2_put_keycode(s, 0x7e);
                }
            } else {
                if (key->down) {
                    ps2_put_keycode(s, 0xe1);
                    ps2_put_keycode(s, 0x14);
                    ps2_put_keycode(s, 0x77);
                    ps2_put_keycode(s, 0xe1);
                    ps2_put_keycode(s, 0xf0);
                    ps2_put_keycode(s, 0x14);
                    ps2_put_keycode(s, 0xf0);
                    ps2_put_keycode(s, 0x77);
                }
            }
        } else if (qcode == Q_KEY_CODE_PRINT) {
            if (s->modifiers & MOD_ALT_L) {
                if (key->down) {
                    ps2_put_keycode(s, 0xf0);
                    ps2_put_keycode(s, 0x11);
                    ps2_put_keycode(s, 0x11);
                    ps2_put_keycode(s, 0x84);
                } else {
                    ps2_put_keycode(s, 0xf0);
                    ps2_put_keycode(s, 0x84);
                    ps2_put_keycode(s, 0xf0);
                    ps2_put_keycode(s, 0x11);
                    ps2_put_keycode(s, 0x11);
                }
            } else if (s->modifiers & MOD_ALT_R) {
                if (key->down) {
                    ps2_put_keycode(s, 0xe0);
                    ps2_put_keycode(s, 0xf0);
                    ps2_put_keycode(s, 0x11);
                    ps2_put_keycode(s, 0xe0);
                    ps2_put_keycode(s, 0x11);
                    ps2_put_keycode(s, 0x84);
                } else {
                    ps2_put_keycode(s, 0xf0);
                    ps2_put_keycode(s, 0x84);
                    ps2_put_keycode(s, 0xe0);
                    ps2_put_keycode(s, 0xf0);
                    ps2_put_keycode(s, 0x11);
                    ps2_put_keycode(s, 0xe0);
                    ps2_put_keycode(s, 0x11);
                }
            } else if (s->modifiers & (MOD_SHIFT_L | MOD_CTRL_L |
                                       MOD_SHIFT_R | MOD_CTRL_R)) {
                if (key->down) {
                    ps2_put_keycode(s, 0xe0);
                    ps2_put_keycode(s, 0x7c);
                } else {
                    ps2_put_keycode(s, 0xe0);
                    ps2_put_keycode(s, 0xf0);
                    ps2_put_keycode(s, 0x7c);
                }
            } else {
                if (key->down) {
                    ps2_put_keycode(s, 0xe0);
                    ps2_put_keycode(s, 0x12);
                    ps2_put_keycode(s, 0xe0);
                    ps2_put_keycode(s, 0x7c);
                } else {
                    ps2_put_keycode(s, 0xe0);
                    ps2_put_keycode(s, 0xf0);
                    ps2_put_keycode(s, 0x7c);
                    ps2_put_keycode(s, 0xe0);
                    ps2_put_keycode(s, 0xf0);
                    ps2_put_keycode(s, 0x12);
                }
            }
        } else {
            if (qcode < qemu_input_map_qcode_to_atset2_len) {
                keycode = qemu_input_map_qcode_to_atset2[qcode];
            }
            if (keycode) {
                if (keycode & 0xff00) {
                    ps2_put_keycode(s, keycode >> 8);
                }
                if (!key->down) {
                    ps2_put_keycode(s, 0xf0);
                }
                ps2_put_keycode(s, keycode & 0xff);
            } else {
                qemu_log_mask(LOG_UNIMP,
                              "ps2: ignoring key with qcode %d\n", qcode);
            }
        }
    } else if (s->scancode_set == 3) {
        if (qcode < qemu_input_map_qcode_to_atset3_len) {
            keycode = qemu_input_map_qcode_to_atset3[qcode];
        }
        if (keycode) {
            /* FIXME: break code should be configured on a key by key basis */
            if (!key->down) {
                ps2_put_keycode(s, 0xf0);
            }
            ps2_put_keycode(s, keycode);
        } else {
            qemu_log_mask(LOG_UNIMP,
                          "ps2: ignoring key with qcode %d\n", qcode);
        }
    }
}

uint32_t ps2_read_data(PS2State *s)
{
    PS2Queue *q;
    int val, index;

    trace_ps2_read_data(s);
    q = &s->queue;
    if (q->count == 0) {
        /*
         * NOTE: if no data left, we return the last keyboard one
         * (needed for EMM386)
         */
        /* XXX: need a timer to do things correctly */
        index = q->rptr - 1;
        if (index < 0) {
            index = PS2_BUFFER_SIZE - 1;
        }
        val = q->data[index];
    } else {
        val = q->data[q->rptr];
        if (++q->rptr == PS2_BUFFER_SIZE) {
            q->rptr = 0;
        }
        q->count--;
        if (q->rptr == q->cwptr) {
            /* command reply queue is empty */
            q->cwptr = -1;
        }
        /* reading deasserts IRQ */
        ps2_lower_irq(s);
        /* reassert IRQs if data left */
        if (q->count) {
            ps2_raise_irq(s);
        }
    }
    return val;
}

static void ps2_set_ledstate(PS2KbdState *s, int ledstate)
{
    trace_ps2_set_ledstate(s, ledstate);
    s->ledstate = ledstate;
    kbd_put_ledstate(ledstate);
}

static void ps2_reset_keyboard(PS2KbdState *s)
{
    PS2State *ps2 = PS2_DEVICE(s);

    trace_ps2_reset_keyboard(s);
    s->scan_enabled = 1;
    s->scancode_set = 2;
    ps2_reset_queue(ps2);
    ps2_set_ledstate(s, 0);
}

void ps2_write_keyboard(PS2KbdState *s, int val)
{
    PS2State *ps2 = PS2_DEVICE(s);

    trace_ps2_write_keyboard(s, val);
    ps2_cqueue_reset(ps2);
    switch (ps2->write_cmd) {
    default:
    case -1:
        switch (val) {
        case 0x00:
            ps2_cqueue_1(ps2, KBD_REPLY_ACK);
            break;
        case 0x05:
            ps2_cqueue_1(ps2, KBD_REPLY_RESEND);
            break;
        case KBD_CMD_GET_ID:
            /* We emulate a MF2 AT keyboard here */
            ps2_cqueue_3(ps2, KBD_REPLY_ACK, KBD_REPLY_ID,
                         s->translate ? 0x41 : 0x83);
            break;
        case KBD_CMD_ECHO:
            ps2_cqueue_1(ps2, KBD_CMD_ECHO);
            break;
        case KBD_CMD_ENABLE:
            s->scan_enabled = 1;
            ps2_cqueue_1(ps2, KBD_REPLY_ACK);
            break;
        case KBD_CMD_SCANCODE:
        case KBD_CMD_SET_LEDS:
        case KBD_CMD_SET_RATE:
        case KBD_CMD_SET_MAKE_BREAK:
            ps2->write_cmd = val;
            ps2_cqueue_1(ps2, KBD_REPLY_ACK);
            break;
        case KBD_CMD_RESET_DISABLE:
            ps2_reset_keyboard(s);
            s->scan_enabled = 0;
            ps2_cqueue_1(ps2, KBD_REPLY_ACK);
            break;
        case KBD_CMD_RESET_ENABLE:
            ps2_reset_keyboard(s);
            s->scan_enabled = 1;
            ps2_cqueue_1(ps2, KBD_REPLY_ACK);
            break;
        case KBD_CMD_RESET:
            ps2_reset_keyboard(s);
            ps2_cqueue_2(ps2,
                         KBD_REPLY_ACK,
                         KBD_REPLY_POR);
            break;
        case KBD_CMD_SET_TYPEMATIC:
            ps2_cqueue_1(ps2, KBD_REPLY_ACK);
            break;
        default:
            ps2_cqueue_1(ps2, KBD_REPLY_RESEND);
            break;
        }
        break;
    case KBD_CMD_SET_MAKE_BREAK:
        ps2_cqueue_1(ps2, KBD_REPLY_ACK);
        ps2->write_cmd = -1;
        break;
    case KBD_CMD_SCANCODE:
        if (val == 0) {
            ps2_cqueue_2(ps2, KBD_REPLY_ACK, s->translate ?
                translate_table[s->scancode_set] : s->scancode_set);
        } else if (val >= 1 && val <= 3) {
            s->scancode_set = val;
            ps2_cqueue_1(ps2, KBD_REPLY_ACK);
        } else {
            ps2_cqueue_1(ps2, KBD_REPLY_RESEND);
        }
        ps2->write_cmd = -1;
        break;
    case KBD_CMD_SET_LEDS:
        ps2_set_ledstate(s, val);
        ps2_cqueue_1(ps2, KBD_REPLY_ACK);
        ps2->write_cmd = -1;
        break;
    case KBD_CMD_SET_RATE:
        ps2_cqueue_1(ps2, KBD_REPLY_ACK);
        ps2->write_cmd = -1;
        break;
    }
}

/*
 * Set the scancode translation mode.
 * 0 = raw scancodes.
 * 1 = translated scancodes (used by qemu internally).
 */

void ps2_keyboard_set_translation(PS2KbdState *s, int mode)
{
    trace_ps2_keyboard_set_translation(s, mode);
    s->translate = mode;
}

static int ps2_mouse_send_packet(PS2MouseState *s)
{
    PS2State *ps2 = PS2_DEVICE(s);
    /* IMPS/2 and IMEX send 4 bytes, PS2 sends 3 bytes */
    const int needed = s->mouse_type ? 4 : 3;
    unsigned int b;
    int dx1, dy1, dz1, dw1;

    if (PS2_QUEUE_SIZE - ps2->queue.count < needed) {
        return 0;
    }

    dx1 = s->mouse_dx;
    dy1 = s->mouse_dy;
    dz1 = s->mouse_dz;
    dw1 = s->mouse_dw;
    /* XXX: increase range to 8 bits ? */
    if (dx1 > 127) {
        dx1 = 127;
    } else if (dx1 < -127) {
        dx1 = -127;
    }
    if (dy1 > 127) {
        dy1 = 127;
    } else if (dy1 < -127) {
        dy1 = -127;
    }
    b = 0x08 | ((dx1 < 0) << 4) | ((dy1 < 0) << 5) | (s->mouse_buttons & 0x07);
    ps2_queue_noirq(ps2, b);
    ps2_queue_noirq(ps2, dx1 & 0xff);
    ps2_queue_noirq(ps2, dy1 & 0xff);
    /* extra byte for IMPS/2 or IMEX */
    switch (s->mouse_type) {
    default:
        /* Just ignore the wheels if not supported */
        s->mouse_dz = 0;
        s->mouse_dw = 0;
        break;
    case 3:
        if (dz1 > 127) {
            dz1 = 127;
        } else if (dz1 < -127) {
            dz1 = -127;
        }
        ps2_queue_noirq(ps2, dz1 & 0xff);
        s->mouse_dz -= dz1;
        s->mouse_dw = 0;
        break;
    case 4:
        /*
         * This matches what the Linux kernel expects for exps/2 in
         * drivers/input/mouse/psmouse-base.c. Note, if you happen to
         * press/release the 4th or 5th buttons at the same moment as a
         * horizontal wheel scroll, those button presses will get lost. I'm not
         * sure what to do about that, since by this point we don't know
         * whether those buttons actually changed state.
         */
        if (dw1 != 0) {
            if (dw1 > 31) {
                dw1 = 31;
            } else if (dw1 < -31) {
                dw1 = -31;
            }

            /*
             * linux kernel expects first 6 bits to represent the value
             * for horizontal scroll
             */
            b = (dw1 & 0x3f) | 0x40;
            s->mouse_dw -= dw1;
        } else {
            if (dz1 > 7) {
                dz1 = 7;
            } else if (dz1 < -7) {
                dz1 = -7;
            }

            b = (dz1 & 0x0f) | ((s->mouse_buttons & 0x18) << 1);
            s->mouse_dz -= dz1;
        }
        ps2_queue_noirq(ps2, b);
        break;
    }

    ps2_raise_irq(ps2);

    trace_ps2_mouse_send_packet(s, dx1, dy1, dz1, b);
    /* update deltas */
    s->mouse_dx -= dx1;
    s->mouse_dy -= dy1;

    return 1;
}

static void ps2_mouse_event(DeviceState *dev, QemuConsole *src,
                            InputEvent *evt)
{
    static const int bmap[INPUT_BUTTON__MAX] = {
        [INPUT_BUTTON_LEFT]   = PS2_MOUSE_BUTTON_LEFT,
        [INPUT_BUTTON_MIDDLE] = PS2_MOUSE_BUTTON_MIDDLE,
        [INPUT_BUTTON_RIGHT]  = PS2_MOUSE_BUTTON_RIGHT,
        [INPUT_BUTTON_SIDE]   = PS2_MOUSE_BUTTON_SIDE,
        [INPUT_BUTTON_EXTRA]  = PS2_MOUSE_BUTTON_EXTRA,
    };
    PS2MouseState *s = (PS2MouseState *)dev;
    InputMoveEvent *move;
    InputBtnEvent *btn;

    /* check if deltas are recorded when disabled */
    if (!(s->mouse_status & MOUSE_STATUS_ENABLED)) {
        return;
    }

    switch (evt->type) {
    case INPUT_EVENT_KIND_REL:
        move = evt->u.rel.data;
        if (move->axis == INPUT_AXIS_X) {
            s->mouse_dx += move->value;
        } else if (move->axis == INPUT_AXIS_Y) {
            s->mouse_dy -= move->value;
        }
        break;

    case INPUT_EVENT_KIND_BTN:
        btn = evt->u.btn.data;
        if (btn->down) {
            s->mouse_buttons |= bmap[btn->button];
            if (btn->button == INPUT_BUTTON_WHEEL_UP) {
                s->mouse_dz--;
            } else if (btn->button == INPUT_BUTTON_WHEEL_DOWN) {
                s->mouse_dz++;
            }

            if (btn->button == INPUT_BUTTON_WHEEL_RIGHT) {
                s->mouse_dw--;
            } else if (btn->button == INPUT_BUTTON_WHEEL_LEFT) {
                s->mouse_dw++;
            }
        } else {
            s->mouse_buttons &= ~bmap[btn->button];
        }
        break;

    default:
        /* keep gcc happy */
        break;
    }
}

static void ps2_mouse_sync(DeviceState *dev)
{
    PS2MouseState *s = (PS2MouseState *)dev;

    /* do not sync while disabled to prevent stream corruption */
    if (!(s->mouse_status & MOUSE_STATUS_ENABLED)) {
        return;
    }

    if (s->mouse_buttons) {
        qemu_system_wakeup_request(QEMU_WAKEUP_REASON_OTHER, NULL);
    }
    if (!(s->mouse_status & MOUSE_STATUS_REMOTE)) {
        /*
         * if not remote, send event. Multiple events are sent if
         * too big deltas
         */
        while (ps2_mouse_send_packet(s)) {
            if (s->mouse_dx == 0 && s->mouse_dy == 0
                    && s->mouse_dz == 0 && s->mouse_dw == 0) {
                break;
            }
        }
    }
}

void ps2_mouse_fake_event(PS2MouseState *s)
{
    trace_ps2_mouse_fake_event(s);
    s->mouse_dx++;
    ps2_mouse_sync(DEVICE(s));
}

void ps2_write_mouse(PS2MouseState *s, int val)
{
    PS2State *ps2 = PS2_DEVICE(s);

    trace_ps2_write_mouse(s, val);
    switch (ps2->write_cmd) {
    default:
    case -1:
        /* mouse command */
        if (s->mouse_wrap) {
            if (val == AUX_RESET_WRAP) {
                s->mouse_wrap = 0;
                ps2_queue(ps2, AUX_ACK);
                return;
            } else if (val != AUX_RESET) {
                ps2_queue(ps2, val);
                return;
            }
        }
        switch (val) {
        case AUX_SET_SCALE11:
            s->mouse_status &= ~MOUSE_STATUS_SCALE21;
            ps2_queue(ps2, AUX_ACK);
            break;
        case AUX_SET_SCALE21:
            s->mouse_status |= MOUSE_STATUS_SCALE21;
            ps2_queue(ps2, AUX_ACK);
            break;
        case AUX_SET_STREAM:
            s->mouse_status &= ~MOUSE_STATUS_REMOTE;
            ps2_queue(ps2, AUX_ACK);
            break;
        case AUX_SET_WRAP:
            s->mouse_wrap = 1;
            ps2_queue(ps2, AUX_ACK);
            break;
        case AUX_SET_REMOTE:
            s->mouse_status |= MOUSE_STATUS_REMOTE;
            ps2_queue(ps2, AUX_ACK);
            break;
        case AUX_GET_TYPE:
            ps2_queue_2(ps2,
                AUX_ACK,
                s->mouse_type);
            break;
        case AUX_SET_RES:
        case AUX_SET_SAMPLE:
            ps2->write_cmd = val;
            ps2_queue(ps2, AUX_ACK);
            break;
        case AUX_GET_SCALE:
            ps2_queue_4(ps2,
                AUX_ACK,
                s->mouse_status,
                s->mouse_resolution,
                s->mouse_sample_rate);
            break;
        case AUX_POLL:
            ps2_queue(ps2, AUX_ACK);
            ps2_mouse_send_packet(s);
            break;
        case AUX_ENABLE_DEV:
            s->mouse_status |= MOUSE_STATUS_ENABLED;
            ps2_queue(ps2, AUX_ACK);
            break;
        case AUX_DISABLE_DEV:
            s->mouse_status &= ~MOUSE_STATUS_ENABLED;
            ps2_queue(ps2, AUX_ACK);
            break;
        case AUX_SET_DEFAULT:
            s->mouse_sample_rate = 100;
            s->mouse_resolution = 2;
            s->mouse_status = 0;
            ps2_queue(ps2, AUX_ACK);
            break;
        case AUX_RESET:
            s->mouse_sample_rate = 100;
            s->mouse_resolution = 2;
            s->mouse_status = 0;
            s->mouse_type = 0;
            ps2_reset_queue(ps2);
            ps2_queue_3(ps2,
                AUX_ACK,
                0xaa,
                s->mouse_type);
            break;
        default:
            break;
        }
        break;
    case AUX_SET_SAMPLE:
        s->mouse_sample_rate = val;
        /* detect IMPS/2 or IMEX */
        switch (s->mouse_detect_state) {
        default:
        case 0:
            if (val == 200) {
                s->mouse_detect_state = 1;
            }
            break;
        case 1:
            if (val == 100) {
                s->mouse_detect_state = 2;
            } else if (val == 200) {
                s->mouse_detect_state = 3;
            } else {
                s->mouse_detect_state = 0;
            }
            break;
        case 2:
            if (val == 80) {
                s->mouse_type = 3; /* IMPS/2 */
            }
            s->mouse_detect_state = 0;
            break;
        case 3:
            if (val == 80) {
                s->mouse_type = 4; /* IMEX */
            }
            s->mouse_detect_state = 0;
            break;
        }
        ps2_queue(ps2, AUX_ACK);
        ps2->write_cmd = -1;
        break;
    case AUX_SET_RES:
        s->mouse_resolution = val;
        ps2_queue(ps2, AUX_ACK);
        ps2->write_cmd = -1;
        break;
    }
}

static void ps2_reset(DeviceState *dev)
{
    PS2State *s = PS2_DEVICE(dev);

    s->write_cmd = -1;
    ps2_reset_queue(s);
    ps2_lower_irq(s);
}

static void ps2_common_post_load(PS2State *s)
{
    PS2Queue *q = &s->queue;
    int ccount = 0;

    /* limit the number of queued command replies to PS2_QUEUE_HEADROOM */
    if (q->cwptr != -1) {
        ccount = (q->cwptr - q->rptr) & (PS2_BUFFER_SIZE - 1);
        if (ccount > PS2_QUEUE_HEADROOM) {
            ccount = PS2_QUEUE_HEADROOM;
        }
    }

    /* limit the scancode queue size to PS2_QUEUE_SIZE */
    if (q->count < ccount) {
        q->count = ccount;
    } else if (q->count > ccount + PS2_QUEUE_SIZE) {
        q->count = ccount + PS2_QUEUE_SIZE;
    }

    /* sanitize rptr and recalculate wptr and cwptr */
    q->rptr = q->rptr & (PS2_BUFFER_SIZE - 1);
    q->wptr = (q->rptr + q->count) & (PS2_BUFFER_SIZE - 1);
    q->cwptr = ccount ? (q->rptr + ccount) & (PS2_BUFFER_SIZE - 1) : -1;
}

static void ps2_kbd_reset(DeviceState *dev)
{
    PS2DeviceClass *ps2dc = PS2_DEVICE_GET_CLASS(dev);
    PS2KbdState *s = PS2_KBD_DEVICE(dev);

    trace_ps2_kbd_reset(s);
    ps2dc->parent_reset(dev);

    s->scan_enabled = 1;
    s->translate = 0;
    s->scancode_set = 2;
    s->modifiers = 0;
}

static void ps2_mouse_reset(DeviceState *dev)
{
    PS2DeviceClass *ps2dc = PS2_DEVICE_GET_CLASS(dev);
    PS2MouseState *s = PS2_MOUSE_DEVICE(dev);

    trace_ps2_mouse_reset(s);
    ps2dc->parent_reset(dev);

    s->mouse_status = 0;
    s->mouse_resolution = 0;
    s->mouse_sample_rate = 0;
    s->mouse_wrap = 0;
    s->mouse_type = 0;
    s->mouse_detect_state = 0;
    s->mouse_dx = 0;
    s->mouse_dy = 0;
    s->mouse_dz = 0;
    s->mouse_dw = 0;
    s->mouse_buttons = 0;
}

static const VMStateDescription vmstate_ps2_common = {
    .name = "PS2 Common State",
    .version_id = 3,
    .minimum_version_id = 2,
    .fields = (VMStateField[]) {
        VMSTATE_INT32(write_cmd, PS2State),
        VMSTATE_INT32(queue.rptr, PS2State),
        VMSTATE_INT32(queue.wptr, PS2State),
        VMSTATE_INT32(queue.count, PS2State),
        VMSTATE_BUFFER(queue.data, PS2State),
        VMSTATE_END_OF_LIST()
    }
};

static bool ps2_keyboard_ledstate_needed(void *opaque)
{
    PS2KbdState *s = opaque;

    return s->ledstate != 0; /* 0 is default state */
}

static int ps2_kbd_ledstate_post_load(void *opaque, int version_id)
{
    PS2KbdState *s = opaque;

    kbd_put_ledstate(s->ledstate);
    return 0;
}

static const VMStateDescription vmstate_ps2_keyboard_ledstate = {
    .name = "ps2kbd/ledstate",
    .version_id = 3,
    .minimum_version_id = 2,
    .post_load = ps2_kbd_ledstate_post_load,
    .needed = ps2_keyboard_ledstate_needed,
    .fields = (VMStateField[]) {
        VMSTATE_INT32(ledstate, PS2KbdState),
        VMSTATE_END_OF_LIST()
    }
};

static bool ps2_keyboard_need_high_bit_needed(void *opaque)
{
    PS2KbdState *s = opaque;
    return s->need_high_bit != 0; /* 0 is the usual state */
}

static const VMStateDescription vmstate_ps2_keyboard_need_high_bit = {
    .name = "ps2kbd/need_high_bit",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = ps2_keyboard_need_high_bit_needed,
    .fields = (VMStateField[]) {
        VMSTATE_BOOL(need_high_bit, PS2KbdState),
        VMSTATE_END_OF_LIST()
    }
};

static bool ps2_keyboard_cqueue_needed(void *opaque)
{
    PS2KbdState *s = opaque;
    PS2State *ps2 = PS2_DEVICE(s);

    return ps2->queue.cwptr != -1; /* the queue is mostly empty */
}

static const VMStateDescription vmstate_ps2_keyboard_cqueue = {
    .name = "ps2kbd/command_reply_queue",
    .needed = ps2_keyboard_cqueue_needed,
    .fields = (VMStateField[]) {
        VMSTATE_INT32(parent_obj.queue.cwptr, PS2KbdState),
        VMSTATE_END_OF_LIST()
    }
};

static int ps2_kbd_post_load(void *opaque, int version_id)
{
    PS2KbdState *s = (PS2KbdState *)opaque;
    PS2State *ps2 = PS2_DEVICE(s);

    if (version_id == 2) {
        s->scancode_set = 2;
    }

    ps2_common_post_load(ps2);

    return 0;
}

static const VMStateDescription vmstate_ps2_keyboard = {
    .name = "ps2kbd",
    .version_id = 3,
    .minimum_version_id = 2,
    .post_load = ps2_kbd_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT(parent_obj, PS2KbdState, 0, vmstate_ps2_common,
                       PS2State),
        VMSTATE_INT32(scan_enabled, PS2KbdState),
        VMSTATE_INT32(translate, PS2KbdState),
        VMSTATE_INT32_V(scancode_set, PS2KbdState, 3),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * []) {
        &vmstate_ps2_keyboard_ledstate,
        &vmstate_ps2_keyboard_need_high_bit,
        &vmstate_ps2_keyboard_cqueue,
        NULL
    }
};

static int ps2_mouse_post_load(void *opaque, int version_id)
{
    PS2MouseState *s = (PS2MouseState *)opaque;
    PS2State *ps2 = PS2_DEVICE(s);

    ps2_common_post_load(ps2);

    return 0;
}

static const VMStateDescription vmstate_ps2_mouse = {
    .name = "ps2mouse",
    .version_id = 2,
    .minimum_version_id = 2,
    .post_load = ps2_mouse_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT(parent_obj, PS2MouseState, 0, vmstate_ps2_common,
                       PS2State),
        VMSTATE_UINT8(mouse_status, PS2MouseState),
        VMSTATE_UINT8(mouse_resolution, PS2MouseState),
        VMSTATE_UINT8(mouse_sample_rate, PS2MouseState),
        VMSTATE_UINT8(mouse_wrap, PS2MouseState),
        VMSTATE_UINT8(mouse_type, PS2MouseState),
        VMSTATE_UINT8(mouse_detect_state, PS2MouseState),
        VMSTATE_INT32(mouse_dx, PS2MouseState),
        VMSTATE_INT32(mouse_dy, PS2MouseState),
        VMSTATE_INT32(mouse_dz, PS2MouseState),
        VMSTATE_UINT8(mouse_buttons, PS2MouseState),
        VMSTATE_END_OF_LIST()
    }
};

static QemuInputHandler ps2_keyboard_handler = {
    .name  = "QEMU PS/2 Keyboard",
    .mask  = INPUT_EVENT_MASK_KEY,
    .event = ps2_keyboard_event,
};

static void ps2_kbd_realize(DeviceState *dev, Error **errp)
{
    qemu_input_handler_register(dev, &ps2_keyboard_handler);
}

static QemuInputHandler ps2_mouse_handler = {
    .name  = "QEMU PS/2 Mouse",
    .mask  = INPUT_EVENT_MASK_BTN | INPUT_EVENT_MASK_REL,
    .event = ps2_mouse_event,
    .sync  = ps2_mouse_sync,
};

static void ps2_mouse_realize(DeviceState *dev, Error **errp)
{
    qemu_input_handler_register(dev, &ps2_mouse_handler);
}

static void ps2_kbd_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PS2DeviceClass *ps2dc = PS2_DEVICE_CLASS(klass);

    dc->realize = ps2_kbd_realize;
    device_class_set_parent_reset(dc, ps2_kbd_reset, &ps2dc->parent_reset);
    dc->vmsd = &vmstate_ps2_keyboard;
}

static const TypeInfo ps2_kbd_info = {
    .name          = TYPE_PS2_KBD_DEVICE,
    .parent        = TYPE_PS2_DEVICE,
    .instance_size = sizeof(PS2KbdState),
    .class_init    = ps2_kbd_class_init
};

static void ps2_mouse_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PS2DeviceClass *ps2dc = PS2_DEVICE_CLASS(klass);

    dc->realize = ps2_mouse_realize;
    device_class_set_parent_reset(dc, ps2_mouse_reset,
                                  &ps2dc->parent_reset);
    dc->vmsd = &vmstate_ps2_mouse;
}

static const TypeInfo ps2_mouse_info = {
    .name          = TYPE_PS2_MOUSE_DEVICE,
    .parent        = TYPE_PS2_DEVICE,
    .instance_size = sizeof(PS2MouseState),
    .class_init    = ps2_mouse_class_init
};

static void ps2_init(Object *obj)
{
    PS2State *s = PS2_DEVICE(obj);

    qdev_init_gpio_out(DEVICE(obj), &s->irq, 1);
}

static void ps2_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = ps2_reset;
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
}

static const TypeInfo ps2_info = {
    .name          = TYPE_PS2_DEVICE,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_init = ps2_init,
    .instance_size = sizeof(PS2State),
    .class_init    = ps2_class_init,
    .class_size    = sizeof(PS2DeviceClass),
    .abstract      = true
};

static void ps2_register_types(void)
{
    type_register_static(&ps2_info);
    type_register_static(&ps2_kbd_info);
    type_register_static(&ps2_mouse_info);
}

type_init(ps2_register_types)
