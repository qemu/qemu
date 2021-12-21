/*
 * D-Bus module support.
 *
 * Copyright (C) 2021 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "ui/dbus-module.h"

int using_dbus_display;

static bool
qemu_dbus_display_add_client(int csock, Error **errp)
{
    error_setg(errp, "D-Bus display isn't enabled");
    return false;
}

struct QemuDBusDisplayOps qemu_dbus_display = {
    .add_client = qemu_dbus_display_add_client,
};
