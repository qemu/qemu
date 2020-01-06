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

GStrv qemu_dbus_get_queued_owners(GDBusConnection *connection,
                                  const char *name,
                                  Error **errp);

#endif /* DBUS_H */
