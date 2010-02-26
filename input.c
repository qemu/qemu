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
static QEMUPutMouseEntry *qemu_put_mouse_event_head;
static QEMUPutMouseEntry *qemu_put_mouse_event_current;
static QTAILQ_HEAD(, QEMUPutLEDEntry) led_handlers = QTAILQ_HEAD_INITIALIZER(led_handlers);

void qemu_add_kbd_event_handler(QEMUPutKBDEvent *func, void *opaque)
{
    qemu_put_kbd_event_opaque = opaque;
    qemu_put_kbd_event = func;
}

QEMUPutMouseEntry *qemu_add_mouse_event_handler(QEMUPutMouseEvent *func,
                                                void *opaque, int absolute,
                                                const char *name)
{
    QEMUPutMouseEntry *s, *cursor;

    s = qemu_mallocz(sizeof(QEMUPutMouseEntry));

    s->qemu_put_mouse_event = func;
    s->qemu_put_mouse_event_opaque = opaque;
    s->qemu_put_mouse_event_absolute = absolute;
    s->qemu_put_mouse_event_name = qemu_strdup(name);
    s->next = NULL;

    if (!qemu_put_mouse_event_head) {
        qemu_put_mouse_event_head = qemu_put_mouse_event_current = s;
        return s;
    }

    cursor = qemu_put_mouse_event_head;
    while (cursor->next != NULL)
        cursor = cursor->next;

    cursor->next = s;
    qemu_put_mouse_event_current = s;

    return s;
}

void qemu_remove_mouse_event_handler(QEMUPutMouseEntry *entry)
{
    QEMUPutMouseEntry *prev = NULL, *cursor;

    if (!qemu_put_mouse_event_head || entry == NULL)
        return;

    cursor = qemu_put_mouse_event_head;
    while (cursor != NULL && cursor != entry) {
        prev = cursor;
        cursor = cursor->next;
    }

    if (cursor == NULL) // does not exist or list empty
        return;
    else if (prev == NULL) { // entry is head
        qemu_put_mouse_event_head = cursor->next;
        if (qemu_put_mouse_event_current == entry)
            qemu_put_mouse_event_current = cursor->next;
        qemu_free(entry->qemu_put_mouse_event_name);
        qemu_free(entry);
        return;
    }

    prev->next = entry->next;

    if (qemu_put_mouse_event_current == entry)
        qemu_put_mouse_event_current = prev;

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
    QEMUPutMouseEvent *mouse_event;
    void *mouse_event_opaque;
    int width;

    if (!qemu_put_mouse_event_current) {
        return;
    }

    mouse_event =
        qemu_put_mouse_event_current->qemu_put_mouse_event;
    mouse_event_opaque =
        qemu_put_mouse_event_current->qemu_put_mouse_event_opaque;

    if (mouse_event) {
        if (graphic_rotate) {
            if (qemu_put_mouse_event_current->qemu_put_mouse_event_absolute)
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
    if (!qemu_put_mouse_event_current)
        return 0;

    return qemu_put_mouse_event_current->qemu_put_mouse_event_absolute;
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
    int index = 0;

    mice_list = qlist_new();

    if (!qemu_put_mouse_event_head) {
        goto out;
    }

    cursor = qemu_put_mouse_event_head;
    while (cursor != NULL) {
        QObject *obj;
        obj = qobject_from_jsonf("{ 'name': %s, 'index': %d, 'current': %i }",
                                 cursor->qemu_put_mouse_event_name,
                                 index, cursor == qemu_put_mouse_event_current);
        qlist_append_obj(mice_list, obj);
        index++;
        cursor = cursor->next;
    }

out:
    *ret_data = QOBJECT(mice_list);
}

void do_mouse_set(Monitor *mon, const QDict *qdict)
{
    QEMUPutMouseEntry *cursor;
    int i = 0;
    int index = qdict_get_int(qdict, "index");

    if (!qemu_put_mouse_event_head) {
        monitor_printf(mon, "No mouse devices connected\n");
        return;
    }

    cursor = qemu_put_mouse_event_head;
    while (cursor != NULL && index != i) {
        i++;
        cursor = cursor->next;
    }

    if (cursor != NULL)
        qemu_put_mouse_event_current = cursor;
    else
        monitor_printf(mon, "Mouse at given index not found\n");
}
