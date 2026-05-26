/*
 * Keyboard and mouse input dispatch via D-Bus.
 *
 * Copyright (C) 2026 Red Hat, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "ui/dbus-display1.h"
#include "ui/input.h"
#include "trace.h"
#include "qemu-vnc.h"

struct QEMUPutLEDEntry {
    QEMUPutLEDEvent *put_led;
    void *opaque;
    QTAILQ_ENTRY(QEMUPutLEDEntry) next;
};

static NotifierList mouse_mode_notifiers =
    NOTIFIER_LIST_INITIALIZER(mouse_mode_notifiers);
static QTAILQ_HEAD(, QEMUPutLEDEntry) led_handlers =
    QTAILQ_HEAD_INITIALIZER(led_handlers);

/* Track the target console for pending mouse events (used by sync) */
static QemuConsole *mouse_target;

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
    if (!entry) {
        return;
    }
    QTAILQ_REMOVE(&led_handlers, entry, next);
    g_free(entry);
}

static void
on_keyboard_modifiers_changed(GObject *gobject, GParamSpec *pspec,
                              gpointer user_data)
{
    guint modifiers;
    QEMUPutLEDEntry *cursor;

    modifiers = qemu_dbus_display1_keyboard_get_modifiers(
        QEMU_DBUS_DISPLAY1_KEYBOARD(gobject));

    /*
     * The D-Bus Keyboard.Modifiers property uses the same
     * bit layout as QEMU's LED constants.
     */
    QTAILQ_FOREACH(cursor, &led_handlers, next) {
        cursor->put_led(cursor->opaque, modifiers);
    }
}

void qemu_add_mouse_mode_change_notifier(Notifier *notify)
{
    notifier_list_add(&mouse_mode_notifiers, notify);
}

void qemu_remove_mouse_mode_change_notifier(Notifier *notify)
{
    notifier_remove(notify);
}

void qemu_input_event_send_key_delay(uint32_t delay_ms)
{
}

void qemu_input_event_send_key_linux(QemuConsole *src, unsigned int lnx,
                                     bool down)
{
    QemuDBusDisplay1Keyboard *kbd;
    guint qnum;

    trace_qemu_vnc_key_event(lnx, down);

    if (!src) {
        return;
    }
    kbd = console_get_keyboard(src);
    if (!kbd) {
        return;
    }

    if (lnx >= qemu_input_map_linux_to_qnum_len) {
        return;
    }
    qnum = qemu_input_map_linux_to_qnum[lnx];

    if (down) {
        qemu_dbus_display1_keyboard_call_press(
            kbd, qnum,
            G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
    } else {
        qemu_dbus_display1_keyboard_call_release(
            kbd, qnum,
            G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
    }
}

static guint abs_x, abs_y;
static bool abs_pending;
static gint rel_dx, rel_dy;
static bool rel_pending;

void qemu_input_queue_abs(QemuConsole *src, InputAxis axis,
                          int value, int min_in, int max_in)
{
    if (axis == INPUT_AXIS_X) {
        abs_x = value;
    } else if (axis == INPUT_AXIS_Y) {
        abs_y = value;
    }
    abs_pending = true;
    mouse_target = src;
}

void qemu_input_queue_rel(QemuConsole *src, InputAxis axis, int value)
{
    if (axis == INPUT_AXIS_X) {
        rel_dx += value;
    } else if (axis == INPUT_AXIS_Y) {
        rel_dy += value;
    }
    rel_pending = true;
    mouse_target = src;
}

void qemu_input_event_sync(void)
{
    QemuDBusDisplay1Mouse *mouse;

    if (!mouse_target) {
        return;
    }

    mouse = console_get_mouse(mouse_target);
    if (!mouse) {
        abs_pending = false;
        rel_pending = false;
        return;
    }

    if (abs_pending) {
        trace_qemu_vnc_input_abs(abs_x, abs_y);
        abs_pending = false;
        qemu_dbus_display1_mouse_call_set_abs_position(
            mouse, abs_x, abs_y,
            G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
    }

    if (rel_pending) {
        trace_qemu_vnc_input_rel(rel_dx, rel_dy);
        rel_pending = false;
        qemu_dbus_display1_mouse_call_rel_motion(
            mouse, rel_dx, rel_dy,
            G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
        rel_dx = 0;
        rel_dy = 0;
    }
}

bool qemu_input_is_absolute(QemuConsole *con)
{
    QemuDBusDisplay1Mouse *mouse;

    if (!con) {
        return false;
    }
    mouse = console_get_mouse(con);

    if (!mouse) {
        return false;
    }
    return qemu_dbus_display1_mouse_get_is_absolute(mouse);
}

static void
on_mouse_is_absolute_changed(GObject *gobject, GParamSpec *pspec,
                              gpointer user_data)
{
    notifier_list_notify(&mouse_mode_notifiers, NULL);
}

void qemu_input_update_buttons(QemuConsole *src, uint32_t *button_map,
                               uint32_t button_old, uint32_t button_new)
{
    QemuDBusDisplay1Mouse *mouse;
    uint32_t changed;
    int i;

    if (!src) {
        return;
    }
    mouse = console_get_mouse(src);
    if (!mouse) {
        return;
    }

    changed = button_old ^ button_new;
    for (i = 0; i < 32; i++) {
        if (!(changed & (1u << i))) {
            continue;
        }
        trace_qemu_vnc_input_btn(i, !!(button_new & (1u << i)));
        if (button_new & (1u << i)) {
            qemu_dbus_display1_mouse_call_press(
                mouse, i,
                G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
        } else {
            qemu_dbus_display1_mouse_call_release(
                mouse, i,
                G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
        }
    }
}

void input_setup(QemuDBusDisplay1Keyboard *kbd,
                 QemuDBusDisplay1Mouse *mouse)
{
    g_signal_connect(kbd, "notify::modifiers",
                     G_CALLBACK(on_keyboard_modifiers_changed), NULL);
    g_signal_connect(mouse, "notify::is-absolute",
                     G_CALLBACK(on_mouse_is_absolute_changed), NULL);
}
