/*
 * Copyright (c) 2021-2023 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "channel.h"
#include "file.h"
#include "migration.h"
#include "io/channel-file.h"
#include "io/channel-util.h"
#include "trace.h"

void file_start_outgoing_migration(MigrationState *s, const char *filename,
                                   Error **errp)
{
    g_autoptr(QIOChannelFile) fioc = NULL;
    QIOChannel *ioc;

    trace_migration_file_outgoing(filename);

    fioc = qio_channel_file_new_path(filename, O_CREAT | O_WRONLY | O_TRUNC,
                                     0600, errp);
    if (!fioc) {
        return;
    }

    ioc = QIO_CHANNEL(fioc);
    qio_channel_set_name(ioc, "migration-file-outgoing");
    migration_channel_connect(s, ioc, NULL, NULL);
}

static gboolean file_accept_incoming_migration(QIOChannel *ioc,
                                               GIOCondition condition,
                                               gpointer opaque)
{
    migration_channel_process_incoming(ioc);
    object_unref(OBJECT(ioc));
    return G_SOURCE_REMOVE;
}

void file_start_incoming_migration(const char *filename, Error **errp)
{
    QIOChannelFile *fioc = NULL;
    QIOChannel *ioc;

    trace_migration_file_incoming(filename);

    fioc = qio_channel_file_new_path(filename, O_RDONLY, 0, errp);
    if (!fioc) {
        return;
    }

    ioc = QIO_CHANNEL(fioc);
    qio_channel_set_name(QIO_CHANNEL(ioc), "migration-file-incoming");
    qio_channel_add_watch_full(ioc, G_IO_IN,
                               file_accept_incoming_migration,
                               NULL, NULL,
                               g_main_context_get_thread_default());
}
