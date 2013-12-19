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

#include "config-host.h"

#ifdef CONFIG_SPICE

#include <spice.h>

#include "qemu/option.h"
#include "qemu/config-file.h"
#include "monitor/monitor.h"

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
                            const char *subject,
                            MonitorCompletion cb, void *opaque);

void do_info_spice_print(Monitor *mon, const QObject *data);
void do_info_spice(Monitor *mon, QObject **ret_data);

CharDriverState *qemu_chr_open_spice_vmc(const char *type);
#if SPICE_SERVER_VERSION >= 0x000c02
CharDriverState *qemu_chr_open_spice_port(const char *name);
void qemu_spice_register_ports(void);
#else
static inline CharDriverState *qemu_chr_open_spice_port(const char *name)
{ return NULL; }
#endif

#else  /* CONFIG_SPICE */
#include "monitor/monitor.h"

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
                                          const char *s,
                                          MonitorCompletion cb, void *opaque)
{
    cb(opaque, NULL);
    return -1;
}

static inline int qemu_spice_display_add_client(int csock, int skipauth,
                                                int tls)
{
    return -1;
}

#endif /* CONFIG_SPICE */

#endif /* QEMU_SPICE_H */
