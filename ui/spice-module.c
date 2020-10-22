/*
 * spice module support, also spice stubs.
 *
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

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qapi/qapi-types-ui.h"
#include "qapi/qapi-commands-ui.h"
#include "ui/qemu-spice-module.h"

int using_spice;

static void qemu_spice_init_stub(void)
{
}

static void qemu_spice_display_init_stub(void)
{
    /* This must never be called if CONFIG_SPICE is disabled */
    error_report("spice support is disabled");
    abort();
}

static int qemu_spice_migrate_info_stub(const char *h, int p, int t,
                                        const char *s)
{
    return -1;
}

static int qemu_spice_set_passwd_stub(const char *passwd,
                                      bool fail_if_connected,
                                      bool disconnect_if_connected)
{
    return -1;
}

static int qemu_spice_set_pw_expire_stub(time_t expires)
{
    return -1;
}

static int qemu_spice_display_add_client_stub(int csock, int skipauth,
                                              int tls)
{
    return -1;
}

struct QemuSpiceOps qemu_spice = {
    .init         = qemu_spice_init_stub,
    .display_init = qemu_spice_display_init_stub,
    .migrate_info = qemu_spice_migrate_info_stub,
    .set_passwd   = qemu_spice_set_passwd_stub,
    .set_pw_expire = qemu_spice_set_pw_expire_stub,
    .display_add_client = qemu_spice_display_add_client_stub,
};

#ifdef CONFIG_SPICE

SpiceInfo *qmp_query_spice(Error **errp)
{
    if (!qemu_spice.qmp_query) {
        SpiceInfo *info = g_new0(SpiceInfo, 1);
        info->enabled = false;
        return info;
    }
    return qemu_spice.qmp_query(errp);
}

#endif
