/*
 * QEMU migration TLS support
 *
 * Copyright (c) 2015 Red Hat, Inc.
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

#ifndef QEMU_MIGRATION_TLS_H
#define QEMU_MIGRATION_TLS_H

#include "io/channel.h"
#include "io/channel-tls.h"

void migration_tls_channel_process_incoming(MigrationState *s,
                                            QIOChannel *ioc,
                                            Error **errp);

QIOChannelTLS *migration_tls_client_create(MigrationState *s,
                                           QIOChannel *ioc,
                                           const char *hostname,
                                           Error **errp);

void migration_tls_channel_connect(MigrationState *s,
                                   QIOChannel *ioc,
                                   const char *hostname,
                                   Error **errp);
#endif
