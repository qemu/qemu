/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
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
#include "qapi/qapi-commands-ui.h"
#include "sysemu/sysemu.h"
#include "ui/console.h"
#include "keymaps.h"
#include "ui/input.h"

struct QEMUPutMouseEntry {
    QEMUPutMouseEvent *qemu_put_mouse_event;
    void *qemu_put_mouse_event_opaque;
    int qemu_put_mouse_event_absolute;

    /* new input core */
    QemuInputHandler h;
    QemuInputHandlerState *s;
    int axis[INPUT_AXIS__MAX];
    int buttons;
};

struct QEMUPutKbdEntry {
    QEMUPutKBDEvent *put_kbd;
    void *opaque;
    QemuInputHandlerState *s;
};

struct QEMUPutLEDEntry {
    QEMUPutLEDEvent *put_led;
    void *opaque;
    QTAILQ_ENTRY(QEMUPutLEDEntry) next;
};

static QTAILQ_HEAD(, QEMUPutLEDEntry) led_handlers =
    QTAILQ_HEAD_INITIALIZER(led_handlers);

int index_from_key(const char *key, size_t key_length)
{
    int i;

    for (i = 0; i < Q_KEY_CODE__MAX; i++) {
        if (!strncmp(key, QKeyCode_str(i), key_length) &&
            !QKeyCode_str(i)[key_length]) {
            break;
        }
    }

    /* Return Q_KEY_CODE__MAX if the key is invalid */
    return i;
}

static KeyValue *copy_key_value(KeyValue *src)
{
    KeyValue *dst = g_new(KeyValue, 1);
    memcpy(dst, src, sizeof(*src));
    if (dst->type == KEY_VALUE_KIND_NUMBER) {
        QKeyCode code = qemu_input_key_number_to_qcode(dst->u.number.data);
        dst->type = KEY_VALUE_KIND_QCODE;
        dst->u.qcode.data = code;
    }
    return dst;
}

void qmp_send_key(KeyValueList *keys, bool has_hold_time, int64_t hold_time,
                  Error **errp)
{
    KeyValueList *p;
    KeyValue **up = NULL;
    int count = 0;

    if (!has_hold_time) {
        hold_time = 0; /* use default */
    }

    for (p = keys; p != NULL; p = p->next) {
        qemu_input_event_send_key(NULL, copy_key_value(p->value), true);
        qemu_input_event_send_key_delay(hold_time);
        up = g_realloc(up, sizeof(*up) * (count+1));
        up[count] = copy_key_value(p->value);
        count++;
    }
    while (count) {
        count--;
        qemu_input_event_send_key(NULL, up[count], false);
        qemu_input_event_send_key_delay(hold_time);
    }
    g_free(up);
}

static void legacy_kbd_event(DeviceState *dev, QemuConsole *src,
                             InputEvent *evt)
{
    QEMUPutKbdEntry *entry = (QEMUPutKbdEntry *)dev;
    int scancodes[3], i, count;
    InputKeyEvent *key = evt->u.key.data;

    if (!entry || !entry->put_kbd) {
        return;
    }
    count = qemu_input_key_value_to_scancode(key->key,
                                             key->down,
                                             scancodes);
    for (i = 0; i < count; i++) {
        entry->put_kbd(entry->opaque, scancodes[i]);
    }
}

static QemuInputHandler legacy_kbd_handler = {
    .name  = "legacy-kbd",
    .mask  = INPUT_EVENT_MASK_KEY,
    .event = legacy_kbd_event,
};

QEMUPutKbdEntry *qemu_add_kbd_event_handler(QEMUPutKBDEvent *func, void *opaque)
{
    QEMUPutKbdEntry *entry;

    entry = g_new0(QEMUPutKbdEntry, 1);
    entry->put_kbd = func;
    entry->opaque = opaque;
    entry->s = qemu_input_handler_register((DeviceState *)entry,
                                           &legacy_kbd_handler);
    qemu_input_handler_activate(entry->s);
    return entry;
}

static void legacy_mouse_event(DeviceState *dev, QemuConsole *src,
                               InputEvent *evt)
{
    static const int bmap[INPUT_BUTTON__MAX] = {
        [INPUT_BUTTON_LEFT]   = MOUSE_EVENT_LBUTTON,
        [INPUT_BUTTON_MIDDLE] = MOUSE_EVENT_MBUTTON,
        [INPUT_BUTTON_RIGHT]  = MOUSE_EVENT_RBUTTON,
    };
    QEMUPutMouseEntry *s = (QEMUPutMouseEntry *)dev;
    InputBtnEvent *btn;
    InputMoveEvent *move;

    switch (evt->type) {
    case INPUT_EVENT_KIND_BTN:
        btn = evt->u.btn.data;
        if (btn->down) {
            s->buttons |= bmap[btn->button];
        } else {
            s->buttons &= ~bmap[btn->button];
        }
        if (btn->down && btn->button == INPUT_BUTTON_WHEEL_UP) {
            s->qemu_put_mouse_event(s->qemu_put_mouse_event_opaque,
                                    s->axis[INPUT_AXIS_X],
                                    s->axis[INPUT_AXIS_Y],
                                    -1,
                                    s->buttons);
        }
        if (btn->down && btn->button == INPUT_BUTTON_WHEEL_DOWN) {
            s->qemu_put_mouse_event(s->qemu_put_mouse_event_opaque,
                                    s->axis[INPUT_AXIS_X],
                                    s->axis[INPUT_AXIS_Y],
                                    1,
                                    s->buttons);
        }
        break;
    case INPUT_EVENT_KIND_ABS:
        move = evt->u.abs.data;
        s->axis[move->axis] = move->value;
        break;
    case INPUT_EVENT_KIND_REL:
        move = evt->u.rel.data;
        s->axis[move->axis] += move->value;
        break;
    default:
        break;
    }
}

static void legacy_mouse_sync(DeviceState *dev)
{
    QEMUPutMouseEntry *s = (QEMUPutMouseEntry *)dev;

    s->qemu_put_mouse_event(s->qemu_put_mouse_event_opaque,
                            s->axis[INPUT_AXIS_X],
                            s->axis[INPUT_AXIS_Y],
                            0,
                            s->buttons);

    if (!s->qemu_put_mouse_event_absolute) {
        s->axis[INPUT_AXIS_X] = 0;
        s->axis[INPUT_AXIS_Y] = 0;
    }
}

QEMUPutMouseEntry *qemu_add_mouse_event_handler(QEMUPutMouseEvent *func,
                                                void *opaque, int absolute,
                                                const char *name)
{
    QEMUPutMouseEntry *s;

    s = g_new0(QEMUPutMouseEntry, 1);

    s->qemu_put_mouse_event = func;
    s->qemu_put_mouse_event_opaque = opaque;
    s->qemu_put_mouse_event_absolute = absolute;

    s->h.name = name;
    s->h.mask = INPUT_EVENT_MASK_BTN |
        (absolute ? INPUT_EVENT_MASK_ABS : INPUT_EVENT_MASK_REL);
    s->h.event = legacy_mouse_event;
    s->h.sync = legacy_mouse_sync;
    s->s = qemu_input_handler_register((DeviceState *)s,
                                       &s->h);

    return s;
}

void qemu_activate_mouse_event_handler(QEMUPutMouseEntry *entry)
{
    qemu_input_handler_activate(entry->s);
}

void qemu_remove_mouse_event_handler(QEMUPutMouseEntry *entry)
{
    qemu_input_handler_unregister(entry->s);

    g_free(entry);
}

QEMUPutLEDEntry *qemu_add_led_event_handler(QEMUPutLEDEvent *func,
                                            void *opaque)
{
    QEMUPutLEDEntry *s;

    s = g_new0(QEMUPutLEDEntry, 1);

    s->put_led = func;
    s->opaque = opaque;
    QTAILQ_INSERT_TAIL(&led_handlers, s, next);
    return s;
}

void qemu_remove_led_event_handler(QEMUPutLEDEntry *entry)
{
    if (entry == NULL)
        return;
    QTAILQ_REMOVE(&led_handlers, entry, next);
    g_free(entry);
}

void kbd_put_ledstate(int ledstate)
{
    QEMUPutLEDEntry *cursor;

    QTAILQ_FOREACH(cursor, &led_handlers, next) {
        cursor->put_led(cursor->opaque, ledstate);
    }
}
