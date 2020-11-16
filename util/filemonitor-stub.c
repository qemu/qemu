/*
 * QEMU file monitor stub impl
 *
 * Copyright (c) 2018 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "qemu/filemonitor.h"
#include "qemu/error-report.h"
#include "qapi/error.h"


QFileMonitor *
qemu_file_monitor_new(Error **errp)
{
    error_setg(errp, "File monitoring not available on this platform");
    return NULL;
}


void
qemu_file_monitor_free(QFileMonitor *mon G_GNUC_UNUSED)
{
}


int64_t
qemu_file_monitor_add_watch(QFileMonitor *mon G_GNUC_UNUSED,
                            const char *dirpath G_GNUC_UNUSED,
                            const char *filename G_GNUC_UNUSED,
                            QFileMonitorHandler cb G_GNUC_UNUSED,
                            void *opaque G_GNUC_UNUSED,
                            Error **errp)
{
    error_setg(errp, "File monitoring not available on this platform");
    return -1;
}


void
qemu_file_monitor_remove_watch(QFileMonitor *mon G_GNUC_UNUSED,
                               const char *dirpath G_GNUC_UNUSED,
                               int64_t id G_GNUC_UNUSED)
{
}
