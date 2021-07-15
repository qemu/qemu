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
#ifndef UI_DBUS_H_
#define UI_DBUS_H_

#include "qemu/dbus.h"
#include "qom/object.h"
#include "ui/console.h"

#include "dbus-display1.h"

struct DBusDisplay {
    Object parent;

    DisplayGLMode gl_mode;
    char *dbus_addr;
    DisplayGLCtx glctx;

    GDBusConnection *bus;
    GDBusObjectManagerServer *server;
    QemuDBusDisplay1VM *iface;
    GPtrArray *consoles;
};

#define TYPE_DBUS_DISPLAY "dbus-display"
OBJECT_DECLARE_SIMPLE_TYPE(DBusDisplay, DBUS_DISPLAY)

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

#endif /* UI_DBUS_H_ */
