/*
 * Helpers for using D-Bus
 *
 * Copyright (C) 2019 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/dbus.h"
#include "qemu/error-report.h"
#include "qapi/error.h"

/*
 * qemu_dbus_get_queued_owners() - return the list of queued unique names
 * @connection: A GDBusConnection
 * @name: a service name
 *
 * Return: a GStrv of unique names, or NULL on failure.
 */
GStrv
qemu_dbus_get_queued_owners(GDBusConnection *connection, const char *name,
                            Error **errp)
{
    g_autoptr(GDBusProxy) proxy = NULL;
    g_autoptr(GVariant) result = NULL;
    g_autoptr(GVariant) child = NULL;
    g_autoptr(GError) err = NULL;

    proxy = g_dbus_proxy_new_sync(connection, G_DBUS_PROXY_FLAGS_NONE, NULL,
                                  "org.freedesktop.DBus",
                                  "/org/freedesktop/DBus",
                                  "org.freedesktop.DBus",
                                  NULL, &err);
    if (!proxy) {
        error_setg(errp, "Failed to create DBus proxy: %s", err->message);
        return NULL;
    }

    result = g_dbus_proxy_call_sync(proxy, "ListQueuedOwners",
                                    g_variant_new("(s)", name),
                                    G_DBUS_CALL_FLAGS_NO_AUTO_START,
                                    -1, NULL, &err);
    if (!result) {
        if (g_error_matches(err,
                            G_DBUS_ERROR,
                            G_DBUS_ERROR_NAME_HAS_NO_OWNER)) {
            return g_new0(char *, 1);
        }
        error_setg(errp, "Failed to call ListQueuedOwners: %s", err->message);
        return NULL;
    }

    child = g_variant_get_child_value(result, 0);
    return g_variant_dup_strv(child, NULL);
}
