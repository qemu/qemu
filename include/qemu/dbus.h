/*
 * Helpers for using D-Bus
 *
 * Copyright (C) 2019 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#ifndef DBUS_H
#define DBUS_H

#include <gio/gio.h>

#include "qom/object.h"
#include "chardev/char.h"
#include "qemu/notify.h"

/* glib/gio 2.68 */
#define DBUS_METHOD_INVOCATION_HANDLED TRUE
#define DBUS_METHOD_INVOCATION_UNHANDLED FALSE

/* in msec */
#define DBUS_DEFAULT_TIMEOUT 1000

#define DBUS_DISPLAY1_ROOT "/org/qemu/Display1"

#define DBUS_DISPLAY_ERROR (dbus_display_error_quark())
GQuark dbus_display_error_quark(void);

typedef enum {
    DBUS_DISPLAY_ERROR_FAILED,
    DBUS_DISPLAY_ERROR_INVALID,
    DBUS_DISPLAY_ERROR_UNSUPPORTED,
    DBUS_DISPLAY_N_ERRORS,
} DBusDisplayError;

GStrv qemu_dbus_get_queued_owners(GDBusConnection *connection,
                                  const char *name,
                                  Error **errp);

#endif /* DBUS_H */
