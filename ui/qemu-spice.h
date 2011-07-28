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

#ifdef CONFIG_SPICE

#include <spice.h>

#include "qemu-option.h"
#include "qemu-config.h"
#include "qemu-char.h"

extern int using_spice;

void qemu_spice_init(void);
void qemu_spice_input_init(void);
void qemu_spice_audio_init(void);
void qemu_spice_display_init(DisplayState *ds);
int qemu_spice_add_interface(SpiceBaseInstance *sin);
int qemu_spice_set_passwd(const char *passwd,
                          bool fail_if_connected, bool disconnect_if_connected);
int qemu_spice_set_pw_expire(time_t expires);
int qemu_spice_migrate_info(const char *hostname, int port, int tls_port,
                            const char *subject);

void do_info_spice_print(Monitor *mon, const QObject *data);
void do_info_spice(Monitor *mon, QObject **ret_data);

int qemu_chr_open_spice(QemuOpts *opts, CharDriverState **_chr);

#else  /* CONFIG_SPICE */

#define using_spice 0
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
static inline int qemu_spice_migrate_info(const char *h, int p, int t, const char *s)
{ return -1; }

#endif /* CONFIG_SPICE */

#endif /* QEMU_SPICE_H */
