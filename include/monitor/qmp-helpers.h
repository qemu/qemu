/*
 * QMP command helpers
 *
 * Copyright (c) 2022 Red Hat Inc.
 *
 * Authors:
 *  Markus Armbruster <armbru@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#ifndef MONITOR_QMP_HELPERS_H

bool qmp_add_client_spice(int fd, bool has_skipauth, bool skipauth,
                        bool has_tls, bool tls, Error **errp);
#ifdef CONFIG_VNC
bool qmp_add_client_vnc(int fd, bool has_skipauth, bool skipauth,
                        bool has_tls, bool tls, Error **errp);
#endif
#ifdef CONFIG_DBUS_DISPLAY
bool qmp_add_client_dbus_display(int fd, bool has_skipauth, bool skipauth,
                        bool has_tls, bool tls, Error **errp);
#endif
bool qmp_add_client_char(int fd, bool has_skipauth, bool skipauth,
                         bool has_tls, bool tls, const char *protocol,
                         Error **errp);

#endif
