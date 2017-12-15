/*
 * QEMU live migration channel operations
 *
 * Copyright Red Hat, Inc. 2016
 *
 * Authors:
 *  Daniel P. Berrange <berrange@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#ifndef QEMU_MIGRATION_CHANNEL_H
#define QEMU_MIGRATION_CHANNEL_H

#include "io/channel.h"

void migration_channel_process_incoming(QIOChannel *ioc);

void migration_channel_connect(MigrationState *s,
                               QIOChannel *ioc,
                               const char *hostname,
                               Error *error_in);
#endif
