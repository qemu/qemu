/*
 * Copyright (c) 2021-2023 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qapi/error.h"
#include "channel.h"
#include "file.h"
#include "migration.h"
#include "io/channel-file.h"
#include "io/channel-util.h"
#include "trace.h"

#define OFFSET_OPTION ",offset="

/* Remove the offset option from @filespec and return it in @offsetp. */

int file_parse_offset(char *filespec, uint64_t *offsetp, Error **errp)
{
    char *option = strstr(filespec, OFFSET_OPTION);
    int ret;

    if (option) {
        *option = 0;
        option += sizeof(OFFSET_OPTION) - 1;
        ret = qemu_strtosz(option, NULL, offsetp);
        if (ret) {
            error_setg_errno(errp, -ret, "file URI has bad offset %s", option);
            return -1;
        }
    }
    return 0;
}

void file_start_outgoing_migration(MigrationState *s,
                                   FileMigrationArgs *file_args, Error **errp)
{
    g_autoptr(QIOChannelFile) fioc = NULL;
    g_autofree char *filename = g_strdup(file_args->filename);
    uint64_t offset = file_args->offset;
    QIOChannel *ioc;

    trace_migration_file_outgoing(filename);

    fioc = qio_channel_file_new_path(filename, O_CREAT | O_WRONLY | O_TRUNC,
                                     0600, errp);
    if (!fioc) {
        return;
    }

    ioc = QIO_CHANNEL(fioc);
    if (offset && qio_channel_io_seek(ioc, offset, SEEK_SET, errp) < 0) {
        return;
    }
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

void file_start_incoming_migration(FileMigrationArgs *file_args, Error **errp)
{
    g_autofree char *filename = g_strdup(file_args->filename);
    QIOChannelFile *fioc = NULL;
    uint64_t offset = file_args->offset;
    QIOChannel *ioc;

    trace_migration_file_incoming(filename);

    fioc = qio_channel_file_new_path(filename, O_RDONLY, 0, errp);
    if (!fioc) {
        return;
    }

    ioc = QIO_CHANNEL(fioc);
    if (offset && qio_channel_io_seek(ioc, offset, SEEK_SET, errp) < 0) {
        return;
    }
    qio_channel_set_name(QIO_CHANNEL(ioc), "migration-file-incoming");
    qio_channel_add_watch_full(ioc, G_IO_IN,
                               file_accept_incoming_migration,
                               NULL, NULL,
                               g_main_context_get_thread_default());
}
