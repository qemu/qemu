/*
 * QEMU I/O channels memory buffer driver
 *
 * Copyright (c) 2015 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
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
#include "io/channel-buffer.h"
#include "io/channel-watch.h"
#include "qemu/module.h"
#include "qemu/sockets.h"
#include "trace.h"

QIOChannelBuffer *
qio_channel_buffer_new(size_t capacity)
{
    QIOChannelBuffer *ioc;

    ioc = QIO_CHANNEL_BUFFER(object_new(TYPE_QIO_CHANNEL_BUFFER));

    if (capacity) {
        ioc->data = g_new0(uint8_t, capacity);
        ioc->capacity = capacity;
    }

    return ioc;
}


static void qio_channel_buffer_finalize(Object *obj)
{
    QIOChannelBuffer *ioc = QIO_CHANNEL_BUFFER(obj);
    g_free(ioc->data);
    ioc->capacity = ioc->usage = ioc->offset = 0;
}


static ssize_t qio_channel_buffer_readv(QIOChannel *ioc,
                                        const struct iovec *iov,
                                        size_t niov,
                                        int **fds,
                                        size_t *nfds,
                                        Error **errp)
{
    QIOChannelBuffer *bioc = QIO_CHANNEL_BUFFER(ioc);
    ssize_t ret = 0;
    size_t i;

    for (i = 0; i < niov; i++) {
        size_t want = iov[i].iov_len;
        if (bioc->offset >= bioc->usage) {
            break;
        }
        if ((bioc->offset + want) > bioc->usage)  {
            want = bioc->usage - bioc->offset;
        }
        memcpy(iov[i].iov_base, bioc->data + bioc->offset, want);
        ret += want;
        bioc->offset += want;
    }

    return ret;
}

static ssize_t qio_channel_buffer_writev(QIOChannel *ioc,
                                         const struct iovec *iov,
                                         size_t niov,
                                         int *fds,
                                         size_t nfds,
                                         Error **errp)
{
    QIOChannelBuffer *bioc = QIO_CHANNEL_BUFFER(ioc);
    ssize_t ret = 0;
    size_t i;
    size_t towrite = 0;

    for (i = 0; i < niov; i++) {
        towrite += iov[i].iov_len;
    }

    if ((bioc->offset + towrite) > bioc->capacity) {
        bioc->capacity = bioc->offset + towrite;
        bioc->data = g_realloc(bioc->data, bioc->capacity);
    }

    if (bioc->offset > bioc->usage) {
        memset(bioc->data, 0, bioc->offset - bioc->usage);
        bioc->usage = bioc->offset;
    }

    for (i = 0; i < niov; i++) {
        memcpy(bioc->data + bioc->usage,
               iov[i].iov_base,
               iov[i].iov_len);
        bioc->usage += iov[i].iov_len;
        bioc->offset += iov[i].iov_len;
        ret += iov[i].iov_len;
    }

    return ret;
}

static int qio_channel_buffer_set_blocking(QIOChannel *ioc G_GNUC_UNUSED,
                                           bool enabled G_GNUC_UNUSED,
                                           Error **errp G_GNUC_UNUSED)
{
    return 0;
}


static off_t qio_channel_buffer_seek(QIOChannel *ioc,
                                     off_t offset,
                                     int whence,
                                     Error **errp)
{
    QIOChannelBuffer *bioc = QIO_CHANNEL_BUFFER(ioc);

    bioc->offset = offset;

    return offset;
}


static int qio_channel_buffer_close(QIOChannel *ioc,
                                    Error **errp)
{
    QIOChannelBuffer *bioc = QIO_CHANNEL_BUFFER(ioc);

    g_free(bioc->data);
    bioc->data = NULL;
    bioc->capacity = bioc->usage = bioc->offset = 0;

    return 0;
}


typedef struct QIOChannelBufferSource QIOChannelBufferSource;
struct QIOChannelBufferSource {
    GSource parent;
    QIOChannelBuffer *bioc;
    GIOCondition condition;
};

static gboolean
qio_channel_buffer_source_prepare(GSource *source,
                                  gint *timeout)
{
    QIOChannelBufferSource *bsource = (QIOChannelBufferSource *)source;

    *timeout = -1;

    return (G_IO_IN | G_IO_OUT) & bsource->condition;
}

static gboolean
qio_channel_buffer_source_check(GSource *source)
{
    QIOChannelBufferSource *bsource = (QIOChannelBufferSource *)source;

    return (G_IO_IN | G_IO_OUT) & bsource->condition;
}

static gboolean
qio_channel_buffer_source_dispatch(GSource *source,
                                   GSourceFunc callback,
                                   gpointer user_data)
{
    QIOChannelFunc func = (QIOChannelFunc)callback;
    QIOChannelBufferSource *bsource = (QIOChannelBufferSource *)source;

    return (*func)(QIO_CHANNEL(bsource->bioc),
                   ((G_IO_IN | G_IO_OUT) & bsource->condition),
                   user_data);
}

static void
qio_channel_buffer_source_finalize(GSource *source)
{
    QIOChannelBufferSource *ssource = (QIOChannelBufferSource *)source;

    object_unref(OBJECT(ssource->bioc));
}

GSourceFuncs qio_channel_buffer_source_funcs = {
    qio_channel_buffer_source_prepare,
    qio_channel_buffer_source_check,
    qio_channel_buffer_source_dispatch,
    qio_channel_buffer_source_finalize
};

static GSource *qio_channel_buffer_create_watch(QIOChannel *ioc,
                                                GIOCondition condition)
{
    QIOChannelBuffer *bioc = QIO_CHANNEL_BUFFER(ioc);
    QIOChannelBufferSource *ssource;
    GSource *source;

    source = g_source_new(&qio_channel_buffer_source_funcs,
                          sizeof(QIOChannelBufferSource));
    ssource = (QIOChannelBufferSource *)source;

    ssource->bioc = bioc;
    object_ref(OBJECT(bioc));

    ssource->condition = condition;

    return source;
}


static void qio_channel_buffer_class_init(ObjectClass *klass,
                                          void *class_data G_GNUC_UNUSED)
{
    QIOChannelClass *ioc_klass = QIO_CHANNEL_CLASS(klass);

    ioc_klass->io_writev = qio_channel_buffer_writev;
    ioc_klass->io_readv = qio_channel_buffer_readv;
    ioc_klass->io_set_blocking = qio_channel_buffer_set_blocking;
    ioc_klass->io_seek = qio_channel_buffer_seek;
    ioc_klass->io_close = qio_channel_buffer_close;
    ioc_klass->io_create_watch = qio_channel_buffer_create_watch;
}

static const TypeInfo qio_channel_buffer_info = {
    .parent = TYPE_QIO_CHANNEL,
    .name = TYPE_QIO_CHANNEL_BUFFER,
    .instance_size = sizeof(QIOChannelBuffer),
    .instance_finalize = qio_channel_buffer_finalize,
    .class_init = qio_channel_buffer_class_init,
};

static void qio_channel_buffer_register_types(void)
{
    type_register_static(&qio_channel_buffer_info);
}

type_init(qio_channel_buffer_register_types);
