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

#ifdef CONFIG_SPICE

#include <spice.h>
#include "qemu/option.h"
#include "qemu/config-file.h"

extern int using_spice;

void qemu_spice_init(void);
void qemu_spice_input_init(void);
void qemu_spice_audio_init(void);
void qemu_spice_display_init(void);
int qemu_spice_display_add_client(int csock, int skipauth, int tls);
int qemu_spice_add_interface(SpiceBaseInstance *sin);
bool qemu_spice_have_display_interface(QemuConsole *con);
int qemu_spice_add_display_interface(QXLInstance *qxlin, QemuConsole *con);
int qemu_spice_set_passwd(const char *passwd,
                          bool fail_if_connected, bool disconnect_if_connected);
int qemu_spice_set_pw_expire(time_t expires);
int qemu_spice_migrate_info(const char *hostname, int port, int tls_port,
                            const char *subject);

#if !defined(SPICE_SERVER_VERSION) || (SPICE_SERVER_VERSION < 0xc06)
#define SPICE_NEEDS_SET_MM_TIME 1
#else
#define SPICE_NEEDS_SET_MM_TIME 0
#endif

#if SPICE_SERVER_VERSION >= 0x000c02
void qemu_spice_register_ports(void);
#else
static inline Chardev *qemu_chr_open_spice_port(const char *name)
{ return NULL; }
#endif

#else  /* CONFIG_SPICE */

#include "qemu/error-report.h"

#define using_spice 0
#define spice_displays 0
static inline int qemu_spice_set_passwd(const char *passwd,
                                        bool fail_if_connected,
                                        bool disconnect_if_connected)
{
    return -1;
}
static inline int qemu_spice_set_pw_expire(time_t expires)
{
    return -1;
}
static inline int qemu_spice_migrate_info(const char *h, int p, int t,
                                          const char *s)
{
    return -1;
}

static inline int qemu_spice_display_add_client(int csock, int skipauth,
                                                int tls)
{
    return -1;
}

static inline void qemu_spice_display_init(void)
{
    /* This must never be called if CONFIG_SPICE is disabled */
    error_report("spice support is disabled");
    abort();
}

static inline void qemu_spice_init(void)
{
}

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
