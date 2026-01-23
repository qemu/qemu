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
#include "qapi/qapi-types-migration.h"

/* Migration channel types */
typedef enum {
    CH_NONE,
    CH_MAIN,
    CH_MULTIFD,
    CH_POSTCOPY
} MigChannelType;

void migration_channel_process_incoming(QIOChannel *ioc);

void migration_channel_connect_outgoing(MigrationState *s, QIOChannel *ioc);

int migration_channel_read_peek(QIOChannel *ioc,
                                const char *buf,
                                const size_t buflen,
                                Error **errp);

bool migration_has_main_and_multifd_channels(void);
bool migration_has_all_channels(void);

void migration_connect_outgoing(MigrationState *s, MigrationAddress *addr,
                                Error **errp);
void migration_connect_incoming(MigrationAddress *addr, Error **errp);

bool migration_channel_parse_input(const char *uri,
                                   MigrationChannelList *channels,
                                   MigrationChannel **main_channelp,
                                   MigrationChannel **cpr_channelp,
                                   Error **errp);
#endif
