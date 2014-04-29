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

#include "sysemu/sysemu.h"
#include "monitor/monitor.h"
#include "ui/console.h"
#include "qapi/error.h"
#include "qmp-commands.h"
#include "qapi-types.h"
#include "ui/keymaps.h"
#include "ui/input.h"

struct QEMUPutMouseEntry {
    QEMUPutMouseEvent *qemu_put_mouse_event;
    void *qemu_put_mouse_event_opaque;
    int qemu_put_mouse_event_absolute;

    /* new input core */
    QemuInputHandler h;
    QemuInputHandlerState *s;
    int axis[INPUT_AXIS_MAX];
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
static QTAILQ_HEAD(, QEMUPutMouseEntry) mouse_handlers =
    QTAILQ_HEAD_INITIALIZER(mouse_handlers);

int index_from_key(const char *key)
{
    int i;

    for (i = 0; QKeyCode_lookup[i] != NULL; i++) {
        if (!strcmp(key, QKeyCode_lookup[i])) {
            break;
        }
    }

    /* Return Q_KEY_CODE_MAX if the key is invalid */
    return i;
}

static KeyValue **keyvalues;
static int keyvalues_size;
static QEMUTimer *key_timer;

static void free_keyvalues(void)
{
    g_free(keyvalues);
    keyvalues = NULL;
    keyvalues_size = 0;
}

static void release_keys(void *opaque)
{
    while (keyvalues_size > 0) {
        qemu_input_event_send_key(NULL, keyvalues[--keyvalues_size],
                                  false);
    }

    free_keyvalues();
}

static KeyValue *copy_key_value(KeyValue *src)
{
    KeyValue *dst = g_new(KeyValue, 1);
    memcpy(dst, src, sizeof(*src));
    return dst;
}

void qmp_send_key(KeyValueList *keys, bool has_hold_time, int64_t hold_time,
                  Error **errp)
{
    KeyValueList *p;

    if (!key_timer) {
        key_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, release_keys, NULL);
    }

    if (keyvalues != NULL) {
        timer_del(key_timer);
        release_keys(NULL);
    }

    if (!has_hold_time) {
        hold_time = 100;
    }

    for (p = keys; p != NULL; p = p->next) {
        qemu_input_event_send_key(NULL, copy_key_value(p->value), true);

        keyvalues = g_realloc(keyvalues, sizeof(KeyValue *) *
                              (keyvalues_size + 1));
        keyvalues[keyvalues_size++] = copy_key_value(p->value);
    }

    /* delayed key up events */
    timer_mod(key_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
              muldiv64(get_ticks_per_sec(), hold_time, 1000));
}

static void legacy_kbd_event(DeviceState *dev, QemuConsole *src,
                             InputEvent *evt)
{
    QEMUPutKbdEntry *entry = (QEMUPutKbdEntry *)dev;
    int scancodes[3], i, count;

    if (!entry || !entry->put_kbd) {
        return;
    }
    count = qemu_input_key_value_to_scancode(evt->key->key,
                                             evt->key->down,
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

void qemu_remove_kbd_event_handler(QEMUPutKbdEntry *entry)
{
    qemu_input_handler_unregister(entry->s);
    g_free(entry);
}

static void legacy_mouse_event(DeviceState *dev, QemuConsole *src,
                               InputEvent *evt)
{
    static const int bmap[INPUT_BUTTON_MAX] = {
        [INPUT_BUTTON_LEFT]   = MOUSE_EVENT_LBUTTON,
        [INPUT_BUTTON_MIDDLE] = MOUSE_EVENT_MBUTTON,
        [INPUT_BUTTON_RIGHT]  = MOUSE_EVENT_RBUTTON,
    };
    QEMUPutMouseEntry *s = (QEMUPutMouseEntry *)dev;

    switch (evt->kind) {
    case INPUT_EVENT_KIND_BTN:
        if (evt->btn->down) {
            s->buttons |= bmap[evt->btn->button];
        } else {
            s->buttons &= ~bmap[evt->btn->button];
        }
        if (evt->btn->down && evt->btn->button == INPUT_BUTTON_WHEEL_UP) {
            s->qemu_put_mouse_event(s->qemu_put_mouse_event_opaque,
                                    s->axis[INPUT_AXIS_X],
                                    s->axis[INPUT_AXIS_Y],
                                    -1,
                                    s->buttons);
        }
        if (evt->btn->down && evt->btn->button == INPUT_BUTTON_WHEEL_DOWN) {
            s->qemu_put_mouse_event(s->qemu_put_mouse_event_opaque,
                                    s->axis[INPUT_AXIS_X],
                                    s->axis[INPUT_AXIS_Y],
                                    1,
                                    s->buttons);
        }
        break;
    case INPUT_EVENT_KIND_ABS:
        s->axis[evt->abs->axis] = evt->abs->value;
        break;
    case INPUT_EVENT_KIND_REL:
        s->axis[evt->rel->axis] += evt->rel->value;
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

    s = g_malloc0(sizeof(QEMUPutMouseEntry));

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

    s = g_malloc0(sizeof(QEMUPutLEDEntry));

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
