/*
 * Copyright (c) 2021-2023 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "exec/ramblock.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "channel.h"
#include "fd.h"
#include "file.h"
#include "migration.h"
#include "io/channel-file.h"
#include "io/channel-util.h"
#include "options.h"
#include "trace.h"

#define OFFSET_OPTION ",offset="

static struct FileOutgoingArgs {
    char *fname;
} outgoing_args;

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

void file_cleanup_outgoing_migration(void)
{
    g_free(outgoing_args.fname);
    outgoing_args.fname = NULL;
}

bool file_send_channel_create(gpointer opaque, Error **errp)
{
    QIOChannelFile *ioc;
    int flags = O_WRONLY;
    bool ret = false;
    int fd = fd_args_get_fd();

    if (fd && fd != -1) {
        ioc = qio_channel_file_new_fd(dup(fd));
    } else {
        ioc = qio_channel_file_new_path(outgoing_args.fname, flags, 0, errp);
        if (!ioc) {
            goto out;
        }
    }

    multifd_channel_connect(opaque, QIO_CHANNEL(ioc));
    ret = true;

out:
    /*
     * File channel creation is synchronous. However posting this
     * semaphore here is simpler than adding a special case.
     */
    multifd_send_channel_created();

    return ret;
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

    outgoing_args.fname = g_strdup(filename);

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
    int channels = 1;
    int i = 0;

    trace_migration_file_incoming(filename);

    fioc = qio_channel_file_new_path(filename, O_RDONLY, 0, errp);
    if (!fioc) {
        return;
    }

    if (offset &&
        qio_channel_io_seek(QIO_CHANNEL(fioc), offset, SEEK_SET, errp) < 0) {
        return;
    }

    if (migrate_multifd()) {
        channels += migrate_multifd_channels();
    }

    do {
        QIOChannel *ioc = QIO_CHANNEL(fioc);

        qio_channel_set_name(ioc, "migration-file-incoming");
        qio_channel_add_watch_full(ioc, G_IO_IN,
                                   file_accept_incoming_migration,
                                   NULL, NULL,
                                   g_main_context_get_thread_default());

        fioc = qio_channel_file_new_fd(dup(fioc->fd));

        if (!fioc || fioc->fd == -1) {
            error_setg(errp, "Error creating migration incoming channel");
            break;
        }
    } while (++i < channels);
}

int file_write_ramblock_iov(QIOChannel *ioc, const struct iovec *iov,
                            int niov, RAMBlock *block, Error **errp)
{
    ssize_t ret = -1;
    int i, slice_idx, slice_num;
    uintptr_t base, next, offset;
    size_t len;

    slice_idx = 0;
    slice_num = 1;

    /*
     * If the iov array doesn't have contiguous elements, we need to
     * split it in slices because we only have one file offset for the
     * whole iov. Do this here so callers don't need to break the iov
     * array themselves.
     */
    for (i = 0; i < niov; i++, slice_num++) {
        base = (uintptr_t) iov[i].iov_base;

        if (i != niov - 1) {
            len = iov[i].iov_len;
            next = (uintptr_t) iov[i + 1].iov_base;

            if (base + len == next) {
                continue;
            }
        }

        /*
         * Use the offset of the first element of the segment that
         * we're sending.
         */
        offset = (uintptr_t) iov[slice_idx].iov_base - (uintptr_t) block->host;
        if (offset >= block->used_length) {
            error_setg(errp, "offset " RAM_ADDR_FMT
                       "outside of ramblock %s range", offset, block->idstr);
            ret = -1;
            break;
        }

        ret = qio_channel_pwritev(ioc, &iov[slice_idx], slice_num,
                                  block->pages_offset + offset, errp);
        if (ret < 0) {
            break;
        }

        slice_idx += slice_num;
        slice_num = 0;
    }

    return (ret < 0) ? ret : 0;
}

int multifd_file_recv_data(MultiFDRecvParams *p, Error **errp)
{
    MultiFDRecvData *data = p->data;
    size_t ret;

    ret = qio_channel_pread(p->c, (char *) data->opaque,
                            data->size, data->file_offset, errp);
    if (ret != data->size) {
        error_prepend(errp,
                      "multifd recv (%u): read 0x%zx, expected 0x%zx",
                      p->id, ret, data->size);
        return -1;
    }

    return 0;
}
