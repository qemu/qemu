/*
 * QEMU I/O channels
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
#include "io/channel.h"
#include "qemu/coroutine.h"

bool qio_channel_has_feature(QIOChannel *ioc,
                             QIOChannelFeature feature)
{
    return ioc->features & (1 << feature);
}


ssize_t qio_channel_readv_full(QIOChannel *ioc,
                               const struct iovec *iov,
                               size_t niov,
                               int **fds,
                               size_t *nfds,
                               Error **errp)
{
    QIOChannelClass *klass = QIO_CHANNEL_GET_CLASS(ioc);

    if ((fds || nfds) &&
        !(ioc->features & (1 << QIO_CHANNEL_FEATURE_FD_PASS))) {
        error_setg_errno(errp, EINVAL,
                         "Channel does not support file descriptor passing");
        return -1;
    }

    return klass->io_readv(ioc, iov, niov, fds, nfds, errp);
}


ssize_t qio_channel_writev_full(QIOChannel *ioc,
                                const struct iovec *iov,
                                size_t niov,
                                int *fds,
                                size_t nfds,
                                Error **errp)
{
    QIOChannelClass *klass = QIO_CHANNEL_GET_CLASS(ioc);

    if ((fds || nfds) &&
        !(ioc->features & (1 << QIO_CHANNEL_FEATURE_FD_PASS))) {
        error_setg_errno(errp, EINVAL,
                         "Channel does not support file descriptor passing");
        return -1;
    }

    return klass->io_writev(ioc, iov, niov, fds, nfds, errp);
}


ssize_t qio_channel_readv(QIOChannel *ioc,
                          const struct iovec *iov,
                          size_t niov,
                          Error **errp)
{
    return qio_channel_readv_full(ioc, iov, niov, NULL, NULL, errp);
}


ssize_t qio_channel_writev(QIOChannel *ioc,
                           const struct iovec *iov,
                           size_t niov,
                           Error **errp)
{
    return qio_channel_writev_full(ioc, iov, niov, NULL, 0, errp);
}


ssize_t qio_channel_read(QIOChannel *ioc,
                         char *buf,
                         size_t buflen,
                         Error **errp)
{
    struct iovec iov = { .iov_base = buf, .iov_len = buflen };
    return qio_channel_readv_full(ioc, &iov, 1, NULL, NULL, errp);
}


ssize_t qio_channel_write(QIOChannel *ioc,
                          const char *buf,
                          size_t buflen,
                          Error **errp)
{
    struct iovec iov = { .iov_base = (char *)buf, .iov_len = buflen };
    return qio_channel_writev_full(ioc, &iov, 1, NULL, 0, errp);
}


int qio_channel_set_blocking(QIOChannel *ioc,
                              bool enabled,
                              Error **errp)
{
    QIOChannelClass *klass = QIO_CHANNEL_GET_CLASS(ioc);
    return klass->io_set_blocking(ioc, enabled, errp);
}


int qio_channel_close(QIOChannel *ioc,
                      Error **errp)
{
    QIOChannelClass *klass = QIO_CHANNEL_GET_CLASS(ioc);
    return klass->io_close(ioc, errp);
}


GSource *qio_channel_create_watch(QIOChannel *ioc,
                                  GIOCondition condition)
{
    QIOChannelClass *klass = QIO_CHANNEL_GET_CLASS(ioc);
    return klass->io_create_watch(ioc, condition);
}


guint qio_channel_add_watch(QIOChannel *ioc,
                            GIOCondition condition,
                            QIOChannelFunc func,
                            gpointer user_data,
                            GDestroyNotify notify)
{
    GSource *source;
    guint id;

    source = qio_channel_create_watch(ioc, condition);

    g_source_set_callback(source, (GSourceFunc)func, user_data, notify);

    id = g_source_attach(source, NULL);
    g_source_unref(source);

    return id;
}


int qio_channel_shutdown(QIOChannel *ioc,
                         QIOChannelShutdown how,
                         Error **errp)
{
    QIOChannelClass *klass = QIO_CHANNEL_GET_CLASS(ioc);

    if (!klass->io_shutdown) {
        error_setg(errp, "Data path shutdown not supported");
        return -1;
    }

    return klass->io_shutdown(ioc, how, errp);
}


void qio_channel_set_delay(QIOChannel *ioc,
                           bool enabled)
{
    QIOChannelClass *klass = QIO_CHANNEL_GET_CLASS(ioc);

    if (klass->io_set_delay) {
        klass->io_set_delay(ioc, enabled);
    }
}


void qio_channel_set_cork(QIOChannel *ioc,
                          bool enabled)
{
    QIOChannelClass *klass = QIO_CHANNEL_GET_CLASS(ioc);

    if (klass->io_set_cork) {
        klass->io_set_cork(ioc, enabled);
    }
}


off_t qio_channel_io_seek(QIOChannel *ioc,
                          off_t offset,
                          int whence,
                          Error **errp)
{
    QIOChannelClass *klass = QIO_CHANNEL_GET_CLASS(ioc);

    if (!klass->io_seek) {
        error_setg(errp, "Channel does not support random access");
        return -1;
    }

    return klass->io_seek(ioc, offset, whence, errp);
}


typedef struct QIOChannelYieldData QIOChannelYieldData;
struct QIOChannelYieldData {
    QIOChannel *ioc;
    Coroutine *co;
};


static gboolean qio_channel_yield_enter(QIOChannel *ioc,
                                        GIOCondition condition,
                                        gpointer opaque)
{
    QIOChannelYieldData *data = opaque;
    qemu_coroutine_enter(data->co, NULL);
    return FALSE;
}


void coroutine_fn qio_channel_yield(QIOChannel *ioc,
                                    GIOCondition condition)
{
    QIOChannelYieldData data;

    assert(qemu_in_coroutine());
    data.ioc = ioc;
    data.co = qemu_coroutine_self();
    qio_channel_add_watch(ioc,
                          condition,
                          qio_channel_yield_enter,
                          &data,
                          NULL);
    qemu_coroutine_yield();
}


static gboolean qio_channel_wait_complete(QIOChannel *ioc,
                                          GIOCondition condition,
                                          gpointer opaque)
{
    GMainLoop *loop = opaque;

    g_main_loop_quit(loop);
    return FALSE;
}


void qio_channel_wait(QIOChannel *ioc,
                      GIOCondition condition)
{
    GMainContext *ctxt = g_main_context_new();
    GMainLoop *loop = g_main_loop_new(ctxt, TRUE);
    GSource *source;

    source = qio_channel_create_watch(ioc, condition);

    g_source_set_callback(source,
                          (GSourceFunc)qio_channel_wait_complete,
                          loop,
                          NULL);

    g_source_attach(source, ctxt);

    g_main_loop_run(loop);

    g_source_unref(source);
    g_main_loop_unref(loop);
    g_main_context_unref(ctxt);
}


#ifdef _WIN32
static void qio_channel_finalize(Object *obj)
{
    QIOChannel *ioc = QIO_CHANNEL(obj);

    if (ioc->event) {
        CloseHandle(ioc->event);
    }
}
#endif

static const TypeInfo qio_channel_info = {
    .parent = TYPE_OBJECT,
    .name = TYPE_QIO_CHANNEL,
    .instance_size = sizeof(QIOChannel),
#ifdef _WIN32
    .instance_finalize = qio_channel_finalize,
#endif
    .abstract = true,
    .class_size = sizeof(QIOChannelClass),
};


static void qio_channel_register_types(void)
{
    type_register_static(&qio_channel_info);
}


type_init(qio_channel_register_types);
