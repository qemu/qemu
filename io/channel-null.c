/*
 * QEMU I/O channels null driver
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
#include "io/channel-null.h"
#include "io/channel-watch.h"
#include "qapi/error.h"
#include "trace.h"
#include "qemu/iov.h"

typedef struct QIOChannelNullSource QIOChannelNullSource;
struct QIOChannelNullSource {
    GSource parent;
    QIOChannel *ioc;
    GIOCondition condition;
};


QIOChannelNull *
qio_channel_null_new(void)
{
    QIOChannelNull *ioc;

    ioc = QIO_CHANNEL_NULL(object_new(TYPE_QIO_CHANNEL_NULL));

    trace_qio_channel_null_new(ioc);

    return ioc;
}


static void
qio_channel_null_init(Object *obj)
{
    QIOChannelNull *ioc = QIO_CHANNEL_NULL(obj);
    ioc->closed = false;
}


static ssize_t
qio_channel_null_readv(QIOChannel *ioc,
                       const struct iovec *iov,
                       size_t niov,
                       int **fds G_GNUC_UNUSED,
                       size_t *nfds G_GNUC_UNUSED,
                       int flags,
                       Error **errp)
{
    QIOChannelNull *nioc = QIO_CHANNEL_NULL(ioc);

    if (nioc->closed) {
        error_setg_errno(errp, EINVAL,
                         "Channel is closed");
        return -1;
    }

    return 0;
}


static ssize_t
qio_channel_null_writev(QIOChannel *ioc,
                        const struct iovec *iov,
                        size_t niov,
                        int *fds G_GNUC_UNUSED,
                        size_t nfds G_GNUC_UNUSED,
                        int flags G_GNUC_UNUSED,
                        Error **errp)
{
    QIOChannelNull *nioc = QIO_CHANNEL_NULL(ioc);

    if (nioc->closed) {
        error_setg_errno(errp, EINVAL,
                         "Channel is closed");
        return -1;
    }

    return iov_size(iov, niov);
}


static int
qio_channel_null_set_blocking(QIOChannel *ioc G_GNUC_UNUSED,
                              bool enabled G_GNUC_UNUSED,
                              Error **errp G_GNUC_UNUSED)
{
    return 0;
}


static off_t
qio_channel_null_seek(QIOChannel *ioc G_GNUC_UNUSED,
                      off_t offset G_GNUC_UNUSED,
                      int whence G_GNUC_UNUSED,
                      Error **errp G_GNUC_UNUSED)
{
    return 0;
}


static int
qio_channel_null_close(QIOChannel *ioc,
                       Error **errp G_GNUC_UNUSED)
{
    QIOChannelNull *nioc = QIO_CHANNEL_NULL(ioc);

    nioc->closed = true;
    return 0;
}


static void
qio_channel_null_set_aio_fd_handler(QIOChannel *ioc G_GNUC_UNUSED,
                                    AioContext *ctx G_GNUC_UNUSED,
                                    IOHandler *io_read G_GNUC_UNUSED,
                                    IOHandler *io_write G_GNUC_UNUSED,
                                    void *opaque G_GNUC_UNUSED)
{
}


static gboolean
qio_channel_null_source_prepare(GSource *source G_GNUC_UNUSED,
                                gint *timeout)
{
    *timeout = -1;

    return TRUE;
}


static gboolean
qio_channel_null_source_check(GSource *source G_GNUC_UNUSED)
{
    return TRUE;
}


static gboolean
qio_channel_null_source_dispatch(GSource *source,
                                 GSourceFunc callback,
                                 gpointer user_data)
{
    QIOChannelFunc func = (QIOChannelFunc)callback;
    QIOChannelNullSource *ssource = (QIOChannelNullSource *)source;

    return (*func)(ssource->ioc,
                   ssource->condition,
                   user_data);
}


static void
qio_channel_null_source_finalize(GSource *source)
{
    QIOChannelNullSource *ssource = (QIOChannelNullSource *)source;

    object_unref(OBJECT(ssource->ioc));
}


GSourceFuncs qio_channel_null_source_funcs = {
    qio_channel_null_source_prepare,
    qio_channel_null_source_check,
    qio_channel_null_source_dispatch,
    qio_channel_null_source_finalize
};


static GSource *
qio_channel_null_create_watch(QIOChannel *ioc,
                              GIOCondition condition)
{
    GSource *source;
    QIOChannelNullSource *ssource;

    source = g_source_new(&qio_channel_null_source_funcs,
                          sizeof(QIOChannelNullSource));
    ssource = (QIOChannelNullSource *)source;

    ssource->ioc = ioc;
    object_ref(OBJECT(ioc));

    ssource->condition = condition;

    return source;
}


static void
qio_channel_null_class_init(ObjectClass *klass,
                            void *class_data G_GNUC_UNUSED)
{
    QIOChannelClass *ioc_klass = QIO_CHANNEL_CLASS(klass);

    ioc_klass->io_writev = qio_channel_null_writev;
    ioc_klass->io_readv = qio_channel_null_readv;
    ioc_klass->io_set_blocking = qio_channel_null_set_blocking;
    ioc_klass->io_seek = qio_channel_null_seek;
    ioc_klass->io_close = qio_channel_null_close;
    ioc_klass->io_create_watch = qio_channel_null_create_watch;
    ioc_klass->io_set_aio_fd_handler = qio_channel_null_set_aio_fd_handler;
}


static const TypeInfo qio_channel_null_info = {
    .parent = TYPE_QIO_CHANNEL,
    .name = TYPE_QIO_CHANNEL_NULL,
    .instance_size = sizeof(QIOChannelNull),
    .instance_init = qio_channel_null_init,
    .class_init = qio_channel_null_class_init,
};


static void
qio_channel_null_register_types(void)
{
    type_register_static(&qio_channel_null_info);
}

type_init(qio_channel_null_register_types);
