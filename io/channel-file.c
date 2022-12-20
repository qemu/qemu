/*
 * QEMU I/O channels files driver
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

#include "qemu/osdep.h"
#include "io/channel-file.h"
#include "io/channel-watch.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qemu/sockets.h"
#include "trace.h"

QIOChannelFile *
qio_channel_file_new_fd(int fd)
{
    QIOChannelFile *ioc;

    ioc = QIO_CHANNEL_FILE(object_new(TYPE_QIO_CHANNEL_FILE));

    ioc->fd = fd;

    trace_qio_channel_file_new_fd(ioc, fd);

    return ioc;
}


QIOChannelFile *
qio_channel_file_new_path(const char *path,
                          int flags,
                          mode_t mode,
                          Error **errp)
{
    QIOChannelFile *ioc;

    ioc = QIO_CHANNEL_FILE(object_new(TYPE_QIO_CHANNEL_FILE));

    ioc->fd = qemu_open_old(path, flags, mode);
    if (ioc->fd < 0) {
        object_unref(OBJECT(ioc));
        error_setg_errno(errp, errno,
                         "Unable to open %s", path);
        return NULL;
    }

    trace_qio_channel_file_new_path(ioc, path, flags, mode, ioc->fd);

    return ioc;
}


static void qio_channel_file_init(Object *obj)
{
    QIOChannelFile *ioc = QIO_CHANNEL_FILE(obj);
    ioc->fd = -1;
}

static void qio_channel_file_finalize(Object *obj)
{
    QIOChannelFile *ioc = QIO_CHANNEL_FILE(obj);
    if (ioc->fd != -1) {
        qemu_close(ioc->fd);
        ioc->fd = -1;
    }
}


static ssize_t qio_channel_file_readv(QIOChannel *ioc,
                                      const struct iovec *iov,
                                      size_t niov,
                                      int **fds,
                                      size_t *nfds,
                                      int flags,
                                      Error **errp)
{
    QIOChannelFile *fioc = QIO_CHANNEL_FILE(ioc);
    ssize_t ret;

 retry:
    ret = readv(fioc->fd, iov, niov);
    if (ret < 0) {
        if (errno == EAGAIN) {
            return QIO_CHANNEL_ERR_BLOCK;
        }
        if (errno == EINTR) {
            goto retry;
        }

        error_setg_errno(errp, errno,
                         "Unable to read from file");
        return -1;
    }

    return ret;
}

static ssize_t qio_channel_file_writev(QIOChannel *ioc,
                                       const struct iovec *iov,
                                       size_t niov,
                                       int *fds,
                                       size_t nfds,
                                       int flags,
                                       Error **errp)
{
    QIOChannelFile *fioc = QIO_CHANNEL_FILE(ioc);
    ssize_t ret;

 retry:
    ret = writev(fioc->fd, iov, niov);
    if (ret <= 0) {
        if (errno == EAGAIN) {
            return QIO_CHANNEL_ERR_BLOCK;
        }
        if (errno == EINTR) {
            goto retry;
        }
        error_setg_errno(errp, errno,
                         "Unable to write to file");
        return -1;
    }
    return ret;
}

static int qio_channel_file_set_blocking(QIOChannel *ioc,
                                         bool enabled,
                                         Error **errp)
{
#ifdef WIN32
    /* not implemented */
    error_setg_errno(errp, errno, "Failed to set FD nonblocking");
    return -1;
#else
    QIOChannelFile *fioc = QIO_CHANNEL_FILE(ioc);

    if (!g_unix_set_fd_nonblocking(fioc->fd, !enabled, NULL)) {
        error_setg_errno(errp, errno, "Failed to set FD nonblocking");
        return -1;
    }
    return 0;
#endif
}


static off_t qio_channel_file_seek(QIOChannel *ioc,
                                   off_t offset,
                                   int whence,
                                   Error **errp)
{
    QIOChannelFile *fioc = QIO_CHANNEL_FILE(ioc);
    off_t ret;

    ret = lseek(fioc->fd, offset, whence);
    if (ret == (off_t)-1) {
        error_setg_errno(errp, errno,
                         "Unable to seek to offset %lld whence %d in file",
                         (long long int)offset, whence);
        return -1;
    }
    return ret;
}


static int qio_channel_file_close(QIOChannel *ioc,
                                  Error **errp)
{
    QIOChannelFile *fioc = QIO_CHANNEL_FILE(ioc);

    if (qemu_close(fioc->fd) < 0) {
        error_setg_errno(errp, errno,
                         "Unable to close file");
        return -1;
    }
    fioc->fd = -1;
    return 0;
}


static void qio_channel_file_set_aio_fd_handler(QIOChannel *ioc,
                                                AioContext *ctx,
                                                IOHandler *io_read,
                                                IOHandler *io_write,
                                                void *opaque)
{
    QIOChannelFile *fioc = QIO_CHANNEL_FILE(ioc);
    aio_set_fd_handler(ctx, fioc->fd, false, io_read, io_write,
                       NULL, NULL, opaque);
}

static GSource *qio_channel_file_create_watch(QIOChannel *ioc,
                                              GIOCondition condition)
{
    QIOChannelFile *fioc = QIO_CHANNEL_FILE(ioc);
    return qio_channel_create_fd_watch(ioc,
                                       fioc->fd,
                                       condition);
}

static void qio_channel_file_class_init(ObjectClass *klass,
                                        void *class_data G_GNUC_UNUSED)
{
    QIOChannelClass *ioc_klass = QIO_CHANNEL_CLASS(klass);

    ioc_klass->io_writev = qio_channel_file_writev;
    ioc_klass->io_readv = qio_channel_file_readv;
    ioc_klass->io_set_blocking = qio_channel_file_set_blocking;
    ioc_klass->io_seek = qio_channel_file_seek;
    ioc_klass->io_close = qio_channel_file_close;
    ioc_klass->io_create_watch = qio_channel_file_create_watch;
    ioc_klass->io_set_aio_fd_handler = qio_channel_file_set_aio_fd_handler;
}

static const TypeInfo qio_channel_file_info = {
    .parent = TYPE_QIO_CHANNEL,
    .name = TYPE_QIO_CHANNEL_FILE,
    .instance_size = sizeof(QIOChannelFile),
    .instance_init = qio_channel_file_init,
    .instance_finalize = qio_channel_file_finalize,
    .class_init = qio_channel_file_class_init,
};

static void qio_channel_file_register_types(void)
{
    type_register_static(&qio_channel_file_info);
}

type_init(qio_channel_file_register_types);
