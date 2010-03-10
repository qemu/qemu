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

#include "sysemu.h"
#include "net.h"
#include "monitor.h"
#include "console.h"
#include "qjson.h"


static QEMUPutKBDEvent *qemu_put_kbd_event;
static void *qemu_put_kbd_event_opaque;
static QTAILQ_HEAD(, QEMUPutLEDEntry) led_handlers = QTAILQ_HEAD_INITIALIZER(led_handlers);
static QTAILQ_HEAD(, QEMUPutMouseEntry) mouse_handlers =
    QTAILQ_HEAD_INITIALIZER(mouse_handlers);

void qemu_add_kbd_event_handler(QEMUPutKBDEvent *func, void *opaque)
{
    qemu_put_kbd_event_opaque = opaque;
    qemu_put_kbd_event = func;
}

QEMUPutMouseEntry *qemu_add_mouse_event_handler(QEMUPutMouseEvent *func,
                                                void *opaque, int absolute,
                                                const char *name)
{
    QEMUPutMouseEntry *s;
    static int mouse_index = 0;

    s = qemu_mallocz(sizeof(QEMUPutMouseEntry));

    s->qemu_put_mouse_event = func;
    s->qemu_put_mouse_event_opaque = opaque;
    s->qemu_put_mouse_event_absolute = absolute;
    s->qemu_put_mouse_event_name = qemu_strdup(name);
    s->index = mouse_index++;

    QTAILQ_INSERT_TAIL(&mouse_handlers, s, node);

    return s;
}

void qemu_activate_mouse_event_handler(QEMUPutMouseEntry *entry)
{
    QTAILQ_REMOVE(&mouse_handlers, entry, node);
    QTAILQ_INSERT_HEAD(&mouse_handlers, entry, node);

    check_mode_change();
}

void qemu_remove_mouse_event_handler(QEMUPutMouseEntry *entry)
{
    QTAILQ_REMOVE(&mouse_handlers, entry, node);

    qemu_free(entry->qemu_put_mouse_event_name);
    qemu_free(entry);
}

QEMUPutLEDEntry *qemu_add_led_event_handler(QEMUPutLEDEvent *func,
                                            void *opaque)
{
    QEMUPutLEDEntry *s;

    s = qemu_mallocz(sizeof(QEMUPutLEDEntry));

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
    qemu_free(entry);
}

void kbd_put_keycode(int keycode)
{
    if (qemu_put_kbd_event) {
        qemu_put_kbd_event(qemu_put_kbd_event_opaque, keycode);
    }
}

void kbd_put_ledstate(int ledstate)
{
    QEMUPutLEDEntry *cursor;

    QTAILQ_FOREACH(cursor, &led_handlers, next) {
        cursor->put_led(cursor->opaque, ledstate);
    }
}

void kbd_mouse_event(int dx, int dy, int dz, int buttons_state)
{
    QEMUPutMouseEntry *entry;
    QEMUPutMouseEvent *mouse_event;
    void *mouse_event_opaque;
    int width;

    if (QTAILQ_EMPTY(&mouse_handlers)) {
        return;
    }

    entry = QTAILQ_FIRST(&mouse_handlers);

    mouse_event = entry->qemu_put_mouse_event;
    mouse_event_opaque = entry->qemu_put_mouse_event_opaque;

    if (mouse_event) {
        if (graphic_rotate) {
            if (entry->qemu_put_mouse_event_absolute)
                width = 0x7fff;
            else
                width = graphic_width - 1;
            mouse_event(mouse_event_opaque,
                        width - dy, dx, dz, buttons_state);
        } else
            mouse_event(mouse_event_opaque,
                        dx, dy, dz, buttons_state);
    }
}

int kbd_mouse_is_absolute(void)
{
    if (QTAILQ_EMPTY(&mouse_handlers)) {
        return 0;
    }

    return QTAILQ_FIRST(&mouse_handlers)->qemu_put_mouse_event_absolute;
}

static void info_mice_iter(QObject *data, void *opaque)
{
    QDict *mouse;
    Monitor *mon = opaque;

    mouse = qobject_to_qdict(data);
    monitor_printf(mon, "%c Mouse #%" PRId64 ": %s\n",
                  (qdict_get_bool(mouse, "current") ? '*' : ' '),
                  qdict_get_int(mouse, "index"), qdict_get_str(mouse, "name"));
}

void do_info_mice_print(Monitor *mon, const QObject *data)
{
    QList *mice_list;

    mice_list = qobject_to_qlist(data);
    if (qlist_empty(mice_list)) {
        monitor_printf(mon, "No mouse devices connected\n");
        return;
    }

    qlist_iter(mice_list, info_mice_iter, mon);
}

/**
 * do_info_mice(): Show VM mice information
 *
 * Each mouse is represented by a QDict, the returned QObject is a QList of
 * all mice.
 *
 * The mouse QDict contains the following:
 *
 * - "name": mouse's name
 * - "index": mouse's index
 * - "current": true if this mouse is receiving events, false otherwise
 *
 * Example:
 *
 * [ { "name": "QEMU Microsoft Mouse", "index": 0, "current": false },
 *   { "name": "QEMU PS/2 Mouse", "index": 1, "current": true } ]
 */
void do_info_mice(Monitor *mon, QObject **ret_data)
{
    QEMUPutMouseEntry *cursor;
    QList *mice_list;
    int current;

    mice_list = qlist_new();

    if (QTAILQ_EMPTY(&mouse_handlers)) {
        goto out;
    }

    current = QTAILQ_FIRST(&mouse_handlers)->index;

    QTAILQ_FOREACH(cursor, &mouse_handlers, node) {
        QObject *obj;
        obj = qobject_from_jsonf("{ 'name': %s, 'index': %d, 'current': %i }",
                                 cursor->qemu_put_mouse_event_name,
                                 cursor->index,
                                 cursor->index == current);
        qlist_append_obj(mice_list, obj);
    }

out:
    *ret_data = QOBJECT(mice_list);
}

void do_mouse_set(Monitor *mon, const QDict *qdict)
{
    QEMUPutMouseEntry *cursor;
    int index = qdict_get_int(qdict, "index");
    int found = 0;

    if (QTAILQ_EMPTY(&mouse_handlers)) {
        monitor_printf(mon, "No mouse devices connected\n");
        return;
    }

    QTAILQ_FOREACH(cursor, &mouse_handlers, node) {
        if (cursor->index == index) {
            found = 1;
            qemu_activate_mouse_event_handler(cursor);
            break;
        }
    }

    if (!found) {
        monitor_printf(mon, "Mouse at given index not found\n");
    }
}
