/*
 * Copyright (c) 2021-2023 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "system/ramblock.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "channel.h"
#include "file.h"
#include "migration.h"
#include "io/channel-file.h"
#include "io/channel-socket.h"
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

static void file_enable_direct_io(int *flags)
{
#ifdef O_DIRECT
    *flags |= O_DIRECT;
#else
    /* it should have been rejected when setting the parameter */
    g_assert_not_reached();
#endif
}

bool file_send_channel_create(gpointer opaque, Error **errp)
{
    QIOChannelFile *ioc;
    int flags = O_WRONLY;
    bool ret = true;

    if (migrate_direct_io()) {
        /*
         * Enable O_DIRECT for the secondary channels. These are used
         * for sending ram pages and writes should be guaranteed to be
         * aligned to at least page size.
         */
        file_enable_direct_io(&flags);
    }

    ioc = qio_channel_file_new_path(outgoing_args.fname, flags, 0, errp);
    if (!ioc) {
        ret = false;
        goto out;
    }

    multifd_channel_connect(opaque, QIO_CHANNEL(ioc));

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

    fioc = qio_channel_file_new_path(filename, O_CREAT | O_WRONLY, 0600, errp);
    if (!fioc) {
        return;
    }

    if (ftruncate(fioc->fd, offset)) {
        error_setg_errno(errp, errno,
                         "failed to truncate migration file to offset %" PRIx64,
                         offset);
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

static void file_create_incoming_channels(QIOChannel *ioc, char *filename,
                                          Error **errp)
{
    int i, channels = 1;
    g_autofree QIOChannel **iocs = NULL;
    int flags = O_RDONLY;

    if (migrate_multifd()) {
        channels += migrate_multifd_channels();
        if (migrate_direct_io()) {
            file_enable_direct_io(&flags);
        }
    }

    iocs = g_new0(QIOChannel *, channels);
    iocs[0] = ioc;

    for (i = 1; i < channels; i++) {
        QIOChannelFile *fioc = qio_channel_file_new_path(filename, flags, 0, errp);

        if (!fioc) {
            while (i) {
                object_unref(iocs[--i]);
            }
            return;
        }

        iocs[i] = QIO_CHANNEL(fioc);
    }

    for (i = 0; i < channels; i++) {
        qio_channel_set_name(iocs[i], "migration-file-incoming");
        qio_channel_add_watch_full(iocs[i], G_IO_IN,
                                   file_accept_incoming_migration,
                                   NULL, NULL,
                                   g_main_context_get_thread_default());
    }
}

void file_start_incoming_migration(FileMigrationArgs *file_args, Error **errp)
{
    g_autofree char *filename = g_strdup(file_args->filename);
    QIOChannelFile *fioc = NULL;
    uint64_t offset = file_args->offset;

    trace_migration_file_incoming(filename);

    fioc = qio_channel_file_new_path(filename, O_RDONLY, 0, errp);
    if (!fioc) {
        return;
    }

    if (offset &&
        qio_channel_io_seek(QIO_CHANNEL(fioc), offset, SEEK_SET, errp) < 0) {
        object_unref(OBJECT(fioc));
        return;
    }

    file_create_incoming_channels(QIO_CHANNEL(fioc), filename, errp);
}

int file_write_ramblock_iov(QIOChannel *ioc, const struct iovec *iov,
                            int niov, MultiFDPages_t *pages, Error **errp)
{
    ssize_t ret = 0;
    int i, slice_idx, slice_num;
    uintptr_t base, next, offset;
    size_t len;
    RAMBlock *block = pages->block;

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
            error_setg(errp, "offset %" PRIxPTR
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
