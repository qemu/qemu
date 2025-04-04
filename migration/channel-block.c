/*
 * QEMU I/O channels block driver
 *
 * Copyright (c) 2022 Red Hat, Inc.
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

#include "qemu/osdep.h"
#include "migration/channel-block.h"
#include "qapi/error.h"
#include "block/block.h"
#include "trace.h"

QIOChannelBlock *
qio_channel_block_new(BlockDriverState *bs)
{
    QIOChannelBlock *ioc;

    ioc = QIO_CHANNEL_BLOCK(object_new(TYPE_QIO_CHANNEL_BLOCK));

    bdrv_ref(bs);
    ioc->bs = bs;

    return ioc;
}


static void
qio_channel_block_finalize(Object *obj)
{
    QIOChannelBlock *ioc = QIO_CHANNEL_BLOCK(obj);

    g_clear_pointer(&ioc->bs, bdrv_unref);
}


static ssize_t
qio_channel_block_readv(QIOChannel *ioc,
                        const struct iovec *iov,
                        size_t niov,
                        int **fds,
                        size_t *nfds,
                        int flags,
                        Error **errp)
{
    QIOChannelBlock *bioc = QIO_CHANNEL_BLOCK(ioc);
    QEMUIOVector qiov;
    int ret;

    qemu_iovec_init_external(&qiov, (struct iovec *)iov, niov);
    ret = bdrv_readv_vmstate(bioc->bs, &qiov, bioc->offset);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "bdrv_readv_vmstate failed");
        return -1;
    }

    bioc->offset += qiov.size;
    return qiov.size;
}


static ssize_t
qio_channel_block_writev(QIOChannel *ioc,
                         const struct iovec *iov,
                         size_t niov,
                         int *fds,
                         size_t nfds,
                         int flags,
                         Error **errp)
{
    QIOChannelBlock *bioc = QIO_CHANNEL_BLOCK(ioc);
    QEMUIOVector qiov;
    int ret;

    qemu_iovec_init_external(&qiov, (struct iovec *)iov, niov);
    ret = bdrv_writev_vmstate(bioc->bs, &qiov, bioc->offset);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "bdrv_writev_vmstate failed");
        return -1;
    }

    bioc->offset += qiov.size;
    return qiov.size;
}


static int
qio_channel_block_set_blocking(QIOChannel *ioc,
                               bool enabled,
                               Error **errp)
{
    if (!enabled) {
        error_setg(errp, "Non-blocking mode not supported for block devices");
        return -1;
    }
    return 0;
}


static off_t
qio_channel_block_seek(QIOChannel *ioc,
                       off_t offset,
                       int whence,
                       Error **errp)
{
    QIOChannelBlock *bioc = QIO_CHANNEL_BLOCK(ioc);

    switch (whence) {
    case SEEK_SET:
        bioc->offset = offset;
        break;
    case SEEK_CUR:
        bioc->offset += offset;
        break;
    case SEEK_END:
        error_setg(errp, "Size of VMstate region is unknown");
        return (off_t)-1;
    default:
        g_assert_not_reached();
    }

    return bioc->offset;
}


static int
qio_channel_block_close(QIOChannel *ioc,
                        Error **errp)
{
    QIOChannelBlock *bioc = QIO_CHANNEL_BLOCK(ioc);
    int rv = bdrv_flush(bioc->bs);

    if (rv < 0) {
        error_setg_errno(errp, -rv,
                         "Unable to flush VMState");
        return -1;
    }

    g_clear_pointer(&bioc->bs, bdrv_unref);
    bioc->offset = 0;

    return 0;
}


static void
qio_channel_block_set_aio_fd_handler(QIOChannel *ioc,
                                     AioContext *read_ctx,
                                     IOHandler *io_read,
                                     AioContext *write_ctx,
                                     IOHandler *io_write,
                                     void *opaque)
{
    /* XXX anything we can do here ? */
}


static void
qio_channel_block_class_init(ObjectClass *klass,
                             void *class_data G_GNUC_UNUSED)
{
    QIOChannelClass *ioc_klass = QIO_CHANNEL_CLASS(klass);

    ioc_klass->io_writev = qio_channel_block_writev;
    ioc_klass->io_readv = qio_channel_block_readv;
    ioc_klass->io_set_blocking = qio_channel_block_set_blocking;
    ioc_klass->io_seek = qio_channel_block_seek;
    ioc_klass->io_close = qio_channel_block_close;
    ioc_klass->io_set_aio_fd_handler = qio_channel_block_set_aio_fd_handler;
}

static const TypeInfo qio_channel_block_info = {
    .parent = TYPE_QIO_CHANNEL,
    .name = TYPE_QIO_CHANNEL_BLOCK,
    .instance_size = sizeof(QIOChannelBlock),
    .instance_finalize = qio_channel_block_finalize,
    .class_init = qio_channel_block_class_init,
};

static void
qio_channel_block_register_types(void)
{
    type_register_static(&qio_channel_block_info);
}

type_init(qio_channel_block_register_types);
