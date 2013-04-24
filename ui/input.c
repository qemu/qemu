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

struct QEMUPutMouseEntry {
    QEMUPutMouseEvent *qemu_put_mouse_event;
    void *qemu_put_mouse_event_opaque;
    int qemu_put_mouse_event_absolute;
    char *qemu_put_mouse_event_name;

    int index;

    /* used internally by qemu for handling mice */
    QTAILQ_ENTRY(QEMUPutMouseEntry) node;
};

struct QEMUPutKbdEntry {
    QEMUPutKBDEvent *put_kbd;
    void *opaque;
    QTAILQ_ENTRY(QEMUPutKbdEntry) next;
};

struct QEMUPutLEDEntry {
    QEMUPutLEDEvent *put_led;
    void *opaque;
    QTAILQ_ENTRY(QEMUPutLEDEntry) next;
};

static QTAILQ_HEAD(, QEMUPutLEDEntry) led_handlers =
    QTAILQ_HEAD_INITIALIZER(led_handlers);
static QTAILQ_HEAD(, QEMUPutKbdEntry) kbd_handlers =
    QTAILQ_HEAD_INITIALIZER(kbd_handlers);
static QTAILQ_HEAD(, QEMUPutMouseEntry) mouse_handlers =
    QTAILQ_HEAD_INITIALIZER(mouse_handlers);
static NotifierList mouse_mode_notifiers =
    NOTIFIER_LIST_INITIALIZER(mouse_mode_notifiers);

static const int key_defs[] = {
    [Q_KEY_CODE_SHIFT] = 0x2a,
    [Q_KEY_CODE_SHIFT_R] = 0x36,

    [Q_KEY_CODE_ALT] = 0x38,
    [Q_KEY_CODE_ALT_R] = 0xb8,
    [Q_KEY_CODE_ALTGR] = 0x64,
    [Q_KEY_CODE_ALTGR_R] = 0xe4,
    [Q_KEY_CODE_CTRL] = 0x1d,
    [Q_KEY_CODE_CTRL_R] = 0x9d,

    [Q_KEY_CODE_MENU] = 0xdd,

    [Q_KEY_CODE_ESC] = 0x01,

    [Q_KEY_CODE_1] = 0x02,
    [Q_KEY_CODE_2] = 0x03,
    [Q_KEY_CODE_3] = 0x04,
    [Q_KEY_CODE_4] = 0x05,
    [Q_KEY_CODE_5] = 0x06,
    [Q_KEY_CODE_6] = 0x07,
    [Q_KEY_CODE_7] = 0x08,
    [Q_KEY_CODE_8] = 0x09,
    [Q_KEY_CODE_9] = 0x0a,
    [Q_KEY_CODE_0] = 0x0b,
    [Q_KEY_CODE_MINUS] = 0x0c,
    [Q_KEY_CODE_EQUAL] = 0x0d,
    [Q_KEY_CODE_BACKSPACE] = 0x0e,

    [Q_KEY_CODE_TAB] = 0x0f,
    [Q_KEY_CODE_Q] = 0x10,
    [Q_KEY_CODE_W] = 0x11,
    [Q_KEY_CODE_E] = 0x12,
    [Q_KEY_CODE_R] = 0x13,
    [Q_KEY_CODE_T] = 0x14,
    [Q_KEY_CODE_Y] = 0x15,
    [Q_KEY_CODE_U] = 0x16,
    [Q_KEY_CODE_I] = 0x17,
    [Q_KEY_CODE_O] = 0x18,
    [Q_KEY_CODE_P] = 0x19,
    [Q_KEY_CODE_BRACKET_LEFT] = 0x1a,
    [Q_KEY_CODE_BRACKET_RIGHT] = 0x1b,
    [Q_KEY_CODE_RET] = 0x1c,

    [Q_KEY_CODE_A] = 0x1e,
    [Q_KEY_CODE_S] = 0x1f,
    [Q_KEY_CODE_D] = 0x20,
    [Q_KEY_CODE_F] = 0x21,
    [Q_KEY_CODE_G] = 0x22,
    [Q_KEY_CODE_H] = 0x23,
    [Q_KEY_CODE_J] = 0x24,
    [Q_KEY_CODE_K] = 0x25,
    [Q_KEY_CODE_L] = 0x26,
    [Q_KEY_CODE_SEMICOLON] = 0x27,
    [Q_KEY_CODE_APOSTROPHE] = 0x28,
    [Q_KEY_CODE_GRAVE_ACCENT] = 0x29,

    [Q_KEY_CODE_BACKSLASH] = 0x2b,
    [Q_KEY_CODE_Z] = 0x2c,
    [Q_KEY_CODE_X] = 0x2d,
    [Q_KEY_CODE_C] = 0x2e,
    [Q_KEY_CODE_V] = 0x2f,
    [Q_KEY_CODE_B] = 0x30,
    [Q_KEY_CODE_N] = 0x31,
    [Q_KEY_CODE_M] = 0x32,
    [Q_KEY_CODE_COMMA] = 0x33,
    [Q_KEY_CODE_DOT] = 0x34,
    [Q_KEY_CODE_SLASH] = 0x35,

    [Q_KEY_CODE_ASTERISK] = 0x37,

    [Q_KEY_CODE_SPC] = 0x39,
    [Q_KEY_CODE_CAPS_LOCK] = 0x3a,
    [Q_KEY_CODE_F1] = 0x3b,
    [Q_KEY_CODE_F2] = 0x3c,
    [Q_KEY_CODE_F3] = 0x3d,
    [Q_KEY_CODE_F4] = 0x3e,
    [Q_KEY_CODE_F5] = 0x3f,
    [Q_KEY_CODE_F6] = 0x40,
    [Q_KEY_CODE_F7] = 0x41,
    [Q_KEY_CODE_F8] = 0x42,
    [Q_KEY_CODE_F9] = 0x43,
    [Q_KEY_CODE_F10] = 0x44,
    [Q_KEY_CODE_NUM_LOCK] = 0x45,
    [Q_KEY_CODE_SCROLL_LOCK] = 0x46,

    [Q_KEY_CODE_KP_DIVIDE] = 0xb5,
    [Q_KEY_CODE_KP_MULTIPLY] = 0x37,
    [Q_KEY_CODE_KP_SUBTRACT] = 0x4a,
    [Q_KEY_CODE_KP_ADD] = 0x4e,
    [Q_KEY_CODE_KP_ENTER] = 0x9c,
    [Q_KEY_CODE_KP_DECIMAL] = 0x53,
    [Q_KEY_CODE_SYSRQ] = 0x54,

    [Q_KEY_CODE_KP_0] = 0x52,
    [Q_KEY_CODE_KP_1] = 0x4f,
    [Q_KEY_CODE_KP_2] = 0x50,
    [Q_KEY_CODE_KP_3] = 0x51,
    [Q_KEY_CODE_KP_4] = 0x4b,
    [Q_KEY_CODE_KP_5] = 0x4c,
    [Q_KEY_CODE_KP_6] = 0x4d,
    [Q_KEY_CODE_KP_7] = 0x47,
    [Q_KEY_CODE_KP_8] = 0x48,
    [Q_KEY_CODE_KP_9] = 0x49,

    [Q_KEY_CODE_LESS] = 0x56,

    [Q_KEY_CODE_F11] = 0x57,
    [Q_KEY_CODE_F12] = 0x58,

    [Q_KEY_CODE_PRINT] = 0xb7,

    [Q_KEY_CODE_HOME] = 0xc7,
    [Q_KEY_CODE_PGUP] = 0xc9,
    [Q_KEY_CODE_PGDN] = 0xd1,
    [Q_KEY_CODE_END] = 0xcf,

    [Q_KEY_CODE_LEFT] = 0xcb,
    [Q_KEY_CODE_UP] = 0xc8,
    [Q_KEY_CODE_DOWN] = 0xd0,
    [Q_KEY_CODE_RIGHT] = 0xcd,

    [Q_KEY_CODE_INSERT] = 0xd2,
    [Q_KEY_CODE_DELETE] = 0xd3,
#ifdef NEED_CPU_H
#if defined(TARGET_SPARC) && !defined(TARGET_SPARC64)
    [Q_KEY_CODE_STOP] = 0xf0,
    [Q_KEY_CODE_AGAIN] = 0xf1,
    [Q_KEY_CODE_PROPS] = 0xf2,
    [Q_KEY_CODE_UNDO] = 0xf3,
    [Q_KEY_CODE_FRONT] = 0xf4,
    [Q_KEY_CODE_COPY] = 0xf5,
    [Q_KEY_CODE_OPEN] = 0xf6,
    [Q_KEY_CODE_PASTE] = 0xf7,
    [Q_KEY_CODE_FIND] = 0xf8,
    [Q_KEY_CODE_CUT] = 0xf9,
    [Q_KEY_CODE_LF] = 0xfa,
    [Q_KEY_CODE_HELP] = 0xfb,
    [Q_KEY_CODE_META_L] = 0xfc,
    [Q_KEY_CODE_META_R] = 0xfd,
    [Q_KEY_CODE_COMPOSE] = 0xfe,
#endif
#endif
    [Q_KEY_CODE_MAX] = 0,
};

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

int index_from_keycode(int code)
{
    int i;

    for (i = 0; i < Q_KEY_CODE_MAX; i++) {
        if (key_defs[i] == code) {
            break;
        }
    }

    /* Return Q_KEY_CODE_MAX if the code is invalid */
    return i;
}

static int *keycodes;
static int keycodes_size;
static QEMUTimer *key_timer;

static int keycode_from_keyvalue(const KeyValue *value)
{
    if (value->kind == KEY_VALUE_KIND_QCODE) {
        return key_defs[value->qcode];
    } else {
        assert(value->kind == KEY_VALUE_KIND_NUMBER);
        return value->number;
    }
}

static void free_keycodes(void)
{
    g_free(keycodes);
    keycodes = NULL;
    keycodes_size = 0;
}

static void release_keys(void *opaque)
{
    while (keycodes_size > 0) {
        if (keycodes[--keycodes_size] & 0x80) {
            kbd_put_keycode(0xe0);
        }
        kbd_put_keycode(keycodes[keycodes_size] | 0x80);
    }

    free_keycodes();
}

void qmp_send_key(KeyValueList *keys, bool has_hold_time, int64_t hold_time,
                  Error **errp)
{
    int keycode;
    KeyValueList *p;

    if (!key_timer) {
        key_timer = qemu_new_timer_ns(vm_clock, release_keys, NULL);
    }

    if (keycodes != NULL) {
        qemu_del_timer(key_timer);
        release_keys(NULL);
    }

    if (!has_hold_time) {
        hold_time = 100;
    }

    for (p = keys; p != NULL; p = p->next) {
        /* key down events */
        keycode = keycode_from_keyvalue(p->value);
        if (keycode < 0x01 || keycode > 0xff) {
            error_setg(errp, "invalid hex keycode 0x%x", keycode);
            free_keycodes();
            return;
        }

        if (keycode & 0x80) {
            kbd_put_keycode(0xe0);
        }
        kbd_put_keycode(keycode & 0x7f);

        keycodes = g_realloc(keycodes, sizeof(int) * (keycodes_size + 1));
        keycodes[keycodes_size++] = keycode;
    }

    /* delayed key up events */
    qemu_mod_timer(key_timer, qemu_get_clock_ns(vm_clock) +
                   muldiv64(get_ticks_per_sec(), hold_time, 1000));
}

QEMUPutKbdEntry *qemu_add_kbd_event_handler(QEMUPutKBDEvent *func, void *opaque)
{
    QEMUPutKbdEntry *entry;

    entry = g_malloc0(sizeof(QEMUPutKbdEntry));
    entry->put_kbd = func;
    entry->opaque = opaque;
    QTAILQ_INSERT_HEAD(&kbd_handlers, entry, next);
    return entry;
}

void qemu_remove_kbd_event_handler(QEMUPutKbdEntry *entry)
{
    QTAILQ_REMOVE(&kbd_handlers, entry, next);
}

static void check_mode_change(void)
{
    static int current_is_absolute, current_has_absolute;
    int is_absolute;
    int has_absolute;

    is_absolute = kbd_mouse_is_absolute();
    has_absolute = kbd_mouse_has_absolute();

    if (is_absolute != current_is_absolute ||
        has_absolute != current_has_absolute) {
        notifier_list_notify(&mouse_mode_notifiers, NULL);
    }

    current_is_absolute = is_absolute;
    current_has_absolute = has_absolute;
}

QEMUPutMouseEntry *qemu_add_mouse_event_handler(QEMUPutMouseEvent *func,
                                                void *opaque, int absolute,
                                                const char *name)
{
    QEMUPutMouseEntry *s;
    static int mouse_index = 0;

    s = g_malloc0(sizeof(QEMUPutMouseEntry));

    s->qemu_put_mouse_event = func;
    s->qemu_put_mouse_event_opaque = opaque;
    s->qemu_put_mouse_event_absolute = absolute;
    s->qemu_put_mouse_event_name = g_strdup(name);
    s->index = mouse_index++;

    QTAILQ_INSERT_TAIL(&mouse_handlers, s, node);

    check_mode_change();

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

    g_free(entry->qemu_put_mouse_event_name);
    g_free(entry);

    check_mode_change();
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

void kbd_put_keycode(int keycode)
{
    QEMUPutKbdEntry *entry = QTAILQ_FIRST(&kbd_handlers);

    if (!runstate_is_running() && !runstate_check(RUN_STATE_SUSPENDED)) {
        return;
    }
    if (entry) {
        entry->put_kbd(entry->opaque, keycode);
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
    int width, height;

    if (!runstate_is_running() && !runstate_check(RUN_STATE_SUSPENDED)) {
        return;
    }
    if (QTAILQ_EMPTY(&mouse_handlers)) {
        return;
    }

    entry = QTAILQ_FIRST(&mouse_handlers);

    mouse_event = entry->qemu_put_mouse_event;
    mouse_event_opaque = entry->qemu_put_mouse_event_opaque;

    if (mouse_event) {
        if (entry->qemu_put_mouse_event_absolute) {
            width = 0x7fff;
            height = 0x7fff;
        } else {
            width = graphic_width - 1;
            height = graphic_height - 1;
        }

        switch (graphic_rotate) {
        case 0:
            mouse_event(mouse_event_opaque,
                        dx, dy, dz, buttons_state);
            break;
        case 90:
            mouse_event(mouse_event_opaque,
                        width - dy, dx, dz, buttons_state);
            break;
        case 180:
            mouse_event(mouse_event_opaque,
                        width - dx, height - dy, dz, buttons_state);
            break;
        case 270:
            mouse_event(mouse_event_opaque,
                        dy, height - dx, dz, buttons_state);
            break;
        }
    }
}

int kbd_mouse_is_absolute(void)
{
    if (QTAILQ_EMPTY(&mouse_handlers)) {
        return 0;
    }

    return QTAILQ_FIRST(&mouse_handlers)->qemu_put_mouse_event_absolute;
}

int kbd_mouse_has_absolute(void)
{
    QEMUPutMouseEntry *entry;

    QTAILQ_FOREACH(entry, &mouse_handlers, node) {
        if (entry->qemu_put_mouse_event_absolute) {
            return 1;
        }
    }

    return 0;
}

MouseInfoList *qmp_query_mice(Error **errp)
{
    MouseInfoList *mice_list = NULL;
    QEMUPutMouseEntry *cursor;
    bool current = true;

    QTAILQ_FOREACH(cursor, &mouse_handlers, node) {
        MouseInfoList *info = g_malloc0(sizeof(*info));
        info->value = g_malloc0(sizeof(*info->value));
        info->value->name = g_strdup(cursor->qemu_put_mouse_event_name);
        info->value->index = cursor->index;
        info->value->absolute = !!cursor->qemu_put_mouse_event_absolute;
        info->value->current = current;

        current = false;

        info->next = mice_list;
        mice_list = info;
    }

    return mice_list;
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

    check_mode_change();
}

void qemu_add_mouse_mode_change_notifier(Notifier *notify)
{
    notifier_list_add(&mouse_mode_notifiers, notify);
}

void qemu_remove_mouse_mode_change_notifier(Notifier *notify)
{
    notifier_remove(notify);
}
