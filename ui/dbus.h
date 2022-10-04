/*
 * QEMU DBus display
 *
 * Copyright (c) 2021 Marc-André Lureau <marcandre.lureau@redhat.com>
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

#ifndef UI_DBUS_H
#define UI_DBUS_H

#include "chardev/char-socket.h"
#include "qemu/dbus.h"
#include "qom/object.h"
#include "ui/console.h"
#include "ui/clipboard.h"

#include "ui/dbus-display1.h"

typedef struct DBusClipboardRequest {
    GDBusMethodInvocation *invocation;
    QemuClipboardType type;
    guint timeout_id;
} DBusClipboardRequest;

struct DBusDisplay {
    Object parent;

    DisplayGLMode gl_mode;
    bool p2p;
    char *dbus_addr;
    char *audiodev;
    DisplayGLCtx glctx;

    GDBusConnection *bus;
    GDBusObjectManagerServer *server;
    QemuDBusDisplay1VM *iface;
    GPtrArray *consoles;
    GCancellable *add_client_cancellable;

    QemuClipboardPeer clipboard_peer;
    QemuDBusDisplay1Clipboard *clipboard;
    QemuDBusDisplay1Clipboard *clipboard_proxy;
    DBusClipboardRequest clipboard_request[QEMU_CLIPBOARD_SELECTION__COUNT];

    Notifier notifier;
};

#define TYPE_DBUS_DISPLAY "dbus-display"
OBJECT_DECLARE_SIMPLE_TYPE(DBusDisplay, DBUS_DISPLAY)

void dbus_display_notifier_add(Notifier *notifier);

#define DBUS_DISPLAY_TYPE_CONSOLE dbus_display_console_get_type()
G_DECLARE_FINAL_TYPE(DBusDisplayConsole,
                     dbus_display_console,
                     DBUS_DISPLAY,
                     CONSOLE,
                     GDBusObjectSkeleton)

DBusDisplayConsole *
dbus_display_console_new(DBusDisplay *display, QemuConsole *con);

int
dbus_display_console_get_index(DBusDisplayConsole *ddc);


extern const DisplayChangeListenerOps dbus_console_dcl_ops;

#define DBUS_DISPLAY_TYPE_LISTENER dbus_display_listener_get_type()
G_DECLARE_FINAL_TYPE(DBusDisplayListener,
                     dbus_display_listener,
                     DBUS_DISPLAY,
                     LISTENER,
                     GObject)

DBusDisplayListener *
dbus_display_listener_new(const char *bus_name,
                          GDBusConnection *conn,
                          DBusDisplayConsole *console);

DBusDisplayConsole *
dbus_display_listener_get_console(DBusDisplayListener *ddl);

const char *
dbus_display_listener_get_bus_name(DBusDisplayListener *ddl);

extern const DisplayChangeListenerOps dbus_gl_dcl_ops;
extern const DisplayChangeListenerOps dbus_dcl_ops;

#define TYPE_CHARDEV_DBUS "chardev-dbus"

typedef struct DBusChardevClass {
    SocketChardevClass parent_class;

    void (*parent_chr_be_event)(Chardev *s, QEMUChrEvent event);
} DBusChardevClass;

DECLARE_CLASS_CHECKERS(DBusChardevClass, DBUS_CHARDEV,
                       TYPE_CHARDEV_DBUS)

typedef struct DBusChardev {
    SocketChardev parent;

    bool exported;
    QemuDBusDisplay1Chardev *iface;
} DBusChardev;

DECLARE_INSTANCE_CHECKER(DBusChardev, DBUS_CHARDEV, TYPE_CHARDEV_DBUS)

#define CHARDEV_IS_DBUS(chr) \
    object_dynamic_cast(OBJECT(chr), TYPE_CHARDEV_DBUS)

typedef enum {
    DBUS_DISPLAY_CHARDEV_OPEN,
    DBUS_DISPLAY_CHARDEV_CLOSE,
} DBusDisplayEventType;

typedef struct DBusDisplayEvent {
    DBusDisplayEventType type;
    union {
        DBusChardev *chardev;
    };
} DBusDisplayEvent;

void dbus_display_notify(DBusDisplayEvent *event);

void dbus_chardev_init(DBusDisplay *dpy);

void dbus_clipboard_init(DBusDisplay *dpy);

#endif /* UI_DBUS_H */
