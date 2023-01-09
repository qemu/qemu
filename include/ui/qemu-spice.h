/*
 * Copyright (C) 2010 Red Hat, Inc.
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

#ifndef QEMU_SPICE_H
#define QEMU_SPICE_H

#include "qapi/error.h"
#include "ui/qemu-spice-module.h"

#ifdef CONFIG_SPICE

#include <spice.h>
#include "qemu/config-file.h"

void qemu_spice_input_init(void);
void qemu_spice_display_init(void);
void qemu_spice_display_init_done(void);
bool qemu_spice_have_display_interface(QemuConsole *con);
int qemu_spice_add_display_interface(QXLInstance *qxlin, QemuConsole *con);
int qemu_spice_migrate_info(const char *hostname, int port, int tls_port,
                            const char *subject);

#if SPICE_SERVER_VERSION >= 0x000f00 /* release 0.15.0 */
#define SPICE_HAS_ATTACHED_WORKER 1
#else
#define SPICE_HAS_ATTACHED_WORKER 0
#endif

#else  /* CONFIG_SPICE */

#include "qemu/error-report.h"

#define spice_displays 0

#endif /* CONFIG_SPICE */

static inline bool qemu_using_spice(Error **errp)
{
    if (!using_spice) {
        error_set(errp, ERROR_CLASS_DEVICE_NOT_ACTIVE,
                  "SPICE is not in use");
        return false;
    }
    return true;
}

#endif /* QEMU_SPICE_H */
