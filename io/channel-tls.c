/*
 * QEMU I/O channels TLS driver
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
#include "qapi/error.h"
#include "qemu/module.h"
#include "io/channel-tls.h"
#include "trace.h"
#include "qemu/atomic.h"


static ssize_t qio_channel_tls_write_handler(const char *buf,
                                             size_t len,
                                             void *opaque)
{
    QIOChannelTLS *tioc = QIO_CHANNEL_TLS(opaque);
    ssize_t ret;

    ret = qio_channel_write(tioc->master, buf, len, NULL);
    if (ret == QIO_CHANNEL_ERR_BLOCK) {
        errno = EAGAIN;
        return -1;
    } else if (ret < 0) {
        errno = EIO;
        return -1;
    }
    return ret;
}

static ssize_t qio_channel_tls_read_handler(char *buf,
                                            size_t len,
                                            void *opaque)
{
    QIOChannelTLS *tioc = QIO_CHANNEL_TLS(opaque);
    ssize_t ret;

    ret = qio_channel_read(tioc->master, buf, len, NULL);
    if (ret == QIO_CHANNEL_ERR_BLOCK) {
        errno = EAGAIN;
        return -1;
    } else if (ret < 0) {
        errno = EIO;
        return -1;
    }
    return ret;
}


QIOChannelTLS *
qio_channel_tls_new_server(QIOChannel *master,
                           QCryptoTLSCreds *creds,
                           const char *aclname,
                           Error **errp)
{
    QIOChannelTLS *ioc;

    ioc = QIO_CHANNEL_TLS(object_new(TYPE_QIO_CHANNEL_TLS));

    ioc->master = master;
    if (qio_channel_has_feature(master, QIO_CHANNEL_FEATURE_SHUTDOWN)) {
        qio_channel_set_feature(QIO_CHANNEL(ioc), QIO_CHANNEL_FEATURE_SHUTDOWN);
    }
    object_ref(OBJECT(master));

    ioc->session = qcrypto_tls_session_new(
        creds,
        NULL,
        aclname,
        QCRYPTO_TLS_CREDS_ENDPOINT_SERVER,
        errp);
    if (!ioc->session) {
        goto error;
    }

    qcrypto_tls_session_set_callbacks(
        ioc->session,
        qio_channel_tls_write_handler,
        qio_channel_tls_read_handler,
        ioc);

    trace_qio_channel_tls_new_server(ioc, master, creds, aclname);
    return ioc;

 error:
    object_unref(OBJECT(ioc));
    return NULL;
}

QIOChannelTLS *
qio_channel_tls_new_client(QIOChannel *master,
                           QCryptoTLSCreds *creds,
                           const char *hostname,
                           Error **errp)
{
    QIOChannelTLS *tioc;
    QIOChannel *ioc;

    tioc = QIO_CHANNEL_TLS(object_new(TYPE_QIO_CHANNEL_TLS));
    ioc = QIO_CHANNEL(tioc);

    tioc->master = master;
    if (qio_channel_has_feature(master, QIO_CHANNEL_FEATURE_SHUTDOWN)) {
        qio_channel_set_feature(ioc, QIO_CHANNEL_FEATURE_SHUTDOWN);
    }
    object_ref(OBJECT(master));

    tioc->session = qcrypto_tls_session_new(
        creds,
        hostname,
        NULL,
        QCRYPTO_TLS_CREDS_ENDPOINT_CLIENT,
        errp);
    if (!tioc->session) {
        goto error;
    }

    qcrypto_tls_session_set_callbacks(
        tioc->session,
        qio_channel_tls_write_handler,
        qio_channel_tls_read_handler,
        tioc);

    trace_qio_channel_tls_new_client(tioc, master, creds, hostname);
    return tioc;

 error:
    object_unref(OBJECT(tioc));
    return NULL;
}

struct QIOChannelTLSData {
    QIOTask *task;
    GMainContext *context;
};
typedef struct QIOChannelTLSData QIOChannelTLSData;

static gboolean qio_channel_tls_handshake_io(QIOChannel *ioc,
                                             GIOCondition condition,
                                             gpointer user_data);

static void qio_channel_tls_handshake_task(QIOChannelTLS *ioc,
                                           QIOTask *task,
                                           GMainContext *context)
{
    Error *err = NULL;
    QCryptoTLSSessionHandshakeStatus status;

    if (qcrypto_tls_session_handshake(ioc->session, &err) < 0) {
        trace_qio_channel_tls_handshake_fail(ioc);
        qio_task_set_error(task, err);
        qio_task_complete(task);
        return;
    }

    status = qcrypto_tls_session_get_handshake_status(ioc->session);
    if (status == QCRYPTO_TLS_HANDSHAKE_COMPLETE) {
        trace_qio_channel_tls_handshake_complete(ioc);
        if (qcrypto_tls_session_check_credentials(ioc->session,
                                                  &err) < 0) {
            trace_qio_channel_tls_credentials_deny(ioc);
            qio_task_set_error(task, err);
        } else {
            trace_qio_channel_tls_credentials_allow(ioc);
        }
        qio_task_complete(task);
    } else {
        GIOCondition condition;
        QIOChannelTLSData *data = g_new0(typeof(*data), 1);

        data->task = task;
        data->context = context;

        if (context) {
            g_main_context_ref(context);
        }

        if (status == QCRYPTO_TLS_HANDSHAKE_SENDING) {
            condition = G_IO_OUT;
        } else {
            condition = G_IO_IN;
        }

        trace_qio_channel_tls_handshake_pending(ioc, status);
        qio_channel_add_watch_full(ioc->master,
                                   condition,
                                   qio_channel_tls_handshake_io,
                                   data,
                                   NULL,
                                   context);
    }
}


static gboolean qio_channel_tls_handshake_io(QIOChannel *ioc,
                                             GIOCondition condition,
                                             gpointer user_data)
{
    QIOChannelTLSData *data = user_data;
    QIOTask *task = data->task;
    GMainContext *context = data->context;
    QIOChannelTLS *tioc = QIO_CHANNEL_TLS(
        qio_task_get_source(task));

    g_free(data);
    qio_channel_tls_handshake_task(tioc, task, context);

    if (context) {
        g_main_context_unref(context);
    }

    return FALSE;
}

void qio_channel_tls_handshake(QIOChannelTLS *ioc,
                               QIOTaskFunc func,
                               gpointer opaque,
                               GDestroyNotify destroy,
                               GMainContext *context)
{
    QIOTask *task;

    task = qio_task_new(OBJECT(ioc),
                        func, opaque, destroy);

    trace_qio_channel_tls_handshake_start(ioc);
    qio_channel_tls_handshake_task(ioc, task, context);
}


static void qio_channel_tls_init(Object *obj G_GNUC_UNUSED)
{
}


static void qio_channel_tls_finalize(Object *obj)
{
    QIOChannelTLS *ioc = QIO_CHANNEL_TLS(obj);

    object_unref(OBJECT(ioc->master));
    qcrypto_tls_session_free(ioc->session);
}


static ssize_t qio_channel_tls_readv(QIOChannel *ioc,
                                     const struct iovec *iov,
                                     size_t niov,
                                     int **fds,
                                     size_t *nfds,
                                     int flags,
                                     Error **errp)
{
    QIOChannelTLS *tioc = QIO_CHANNEL_TLS(ioc);
    size_t i;
    ssize_t got = 0;

    for (i = 0 ; i < niov ; i++) {
        ssize_t ret = qcrypto_tls_session_read(tioc->session,
                                               iov[i].iov_base,
                                               iov[i].iov_len);
        if (ret < 0) {
            if (errno == EAGAIN) {
                if (got) {
                    return got;
                } else {
                    return QIO_CHANNEL_ERR_BLOCK;
                }
            } else if (errno == ECONNABORTED &&
                       (qatomic_load_acquire(&tioc->shutdown) &
                        QIO_CHANNEL_SHUTDOWN_READ)) {
                return 0;
            }

            error_setg_errno(errp, errno,
                             "Cannot read from TLS channel");
            return -1;
        }
        got += ret;
        if (ret < iov[i].iov_len) {
            break;
        }
    }
    return got;
}


static ssize_t qio_channel_tls_writev(QIOChannel *ioc,
                                      const struct iovec *iov,
                                      size_t niov,
                                      int *fds,
                                      size_t nfds,
                                      int flags,
                                      Error **errp)
{
    QIOChannelTLS *tioc = QIO_CHANNEL_TLS(ioc);
    size_t i;
    ssize_t done = 0;

    for (i = 0 ; i < niov ; i++) {
        ssize_t ret = qcrypto_tls_session_write(tioc->session,
                                                iov[i].iov_base,
                                                iov[i].iov_len);
        if (ret <= 0) {
            if (errno == EAGAIN) {
                if (done) {
                    return done;
                } else {
                    return QIO_CHANNEL_ERR_BLOCK;
                }
            }

            error_setg_errno(errp, errno,
                             "Cannot write to TLS channel");
            return -1;
        }
        done += ret;
        if (ret < iov[i].iov_len) {
            break;
        }
    }
    return done;
}

static int qio_channel_tls_set_blocking(QIOChannel *ioc,
                                        bool enabled,
                                        Error **errp)
{
    QIOChannelTLS *tioc = QIO_CHANNEL_TLS(ioc);

    return qio_channel_set_blocking(tioc->master, enabled, errp);
}

static void qio_channel_tls_set_delay(QIOChannel *ioc,
                                      bool enabled)
{
    QIOChannelTLS *tioc = QIO_CHANNEL_TLS(ioc);

    qio_channel_set_delay(tioc->master, enabled);
}

static void qio_channel_tls_set_cork(QIOChannel *ioc,
                                     bool enabled)
{
    QIOChannelTLS *tioc = QIO_CHANNEL_TLS(ioc);

    qio_channel_set_cork(tioc->master, enabled);
}

static int qio_channel_tls_shutdown(QIOChannel *ioc,
                                    QIOChannelShutdown how,
                                    Error **errp)
{
    QIOChannelTLS *tioc = QIO_CHANNEL_TLS(ioc);

    qatomic_or(&tioc->shutdown, how);

    return qio_channel_shutdown(tioc->master, how, errp);
}

static int qio_channel_tls_close(QIOChannel *ioc,
                                 Error **errp)
{
    QIOChannelTLS *tioc = QIO_CHANNEL_TLS(ioc);

    return qio_channel_close(tioc->master, errp);
}

static void qio_channel_tls_set_aio_fd_handler(QIOChannel *ioc,
                                               AioContext *ctx,
                                               IOHandler *io_read,
                                               IOHandler *io_write,
                                               void *opaque)
{
    QIOChannelTLS *tioc = QIO_CHANNEL_TLS(ioc);

    qio_channel_set_aio_fd_handler(tioc->master, ctx, io_read, io_write, opaque);
}

typedef struct QIOChannelTLSSource QIOChannelTLSSource;
struct QIOChannelTLSSource {
    GSource parent;
    QIOChannelTLS *tioc;
};

static gboolean
qio_channel_tls_source_check(GSource *source)
{
    QIOChannelTLSSource *tsource = (QIOChannelTLSSource *)source;

    return qcrypto_tls_session_check_pending(tsource->tioc->session) > 0;
}

static gboolean
qio_channel_tls_source_prepare(GSource *source, gint *timeout)
{
    *timeout = -1;
    return qio_channel_tls_source_check(source);
}

static gboolean
qio_channel_tls_source_dispatch(GSource *source, GSourceFunc callback,
                                gpointer user_data)
{
    return G_SOURCE_CONTINUE;
}

static void
qio_channel_tls_source_finalize(GSource *source)
{
    QIOChannelTLSSource *tsource = (QIOChannelTLSSource *)source;

    object_unref(OBJECT(tsource->tioc));
}

static GSourceFuncs qio_channel_tls_source_funcs = {
    qio_channel_tls_source_prepare,
    qio_channel_tls_source_check,
    qio_channel_tls_source_dispatch,
    qio_channel_tls_source_finalize
};

static void
qio_channel_tls_read_watch(QIOChannelTLS *tioc, GSource *source)
{
    GSource *child;
    QIOChannelTLSSource *tlssource;

    child = g_source_new(&qio_channel_tls_source_funcs,
                          sizeof(QIOChannelTLSSource));
    tlssource = (QIOChannelTLSSource *)child;

    tlssource->tioc = tioc;
    object_ref(OBJECT(tioc));

    g_source_add_child_source(source, child);
    g_source_unref(child);
}

static GSource *qio_channel_tls_create_watch(QIOChannel *ioc,
                                             GIOCondition condition)
{
    QIOChannelTLS *tioc = QIO_CHANNEL_TLS(ioc);
    GSource *source = qio_channel_create_watch(tioc->master, condition);

    if (condition & G_IO_IN) {
        qio_channel_tls_read_watch(tioc, source);
    }

    return source;
}

QCryptoTLSSession *
qio_channel_tls_get_session(QIOChannelTLS *ioc)
{
    return ioc->session;
}

static void qio_channel_tls_class_init(ObjectClass *klass,
                                       void *class_data G_GNUC_UNUSED)
{
    QIOChannelClass *ioc_klass = QIO_CHANNEL_CLASS(klass);

    ioc_klass->io_writev = qio_channel_tls_writev;
    ioc_klass->io_readv = qio_channel_tls_readv;
    ioc_klass->io_set_blocking = qio_channel_tls_set_blocking;
    ioc_klass->io_set_delay = qio_channel_tls_set_delay;
    ioc_klass->io_set_cork = qio_channel_tls_set_cork;
    ioc_klass->io_close = qio_channel_tls_close;
    ioc_klass->io_shutdown = qio_channel_tls_shutdown;
    ioc_klass->io_create_watch = qio_channel_tls_create_watch;
    ioc_klass->io_set_aio_fd_handler = qio_channel_tls_set_aio_fd_handler;
}

static const TypeInfo qio_channel_tls_info = {
    .parent = TYPE_QIO_CHANNEL,
    .name = TYPE_QIO_CHANNEL_TLS,
    .instance_size = sizeof(QIOChannelTLS),
    .instance_init = qio_channel_tls_init,
    .instance_finalize = qio_channel_tls_finalize,
    .class_init = qio_channel_tls_class_init,
};

static void qio_channel_tls_register_types(void)
{
    type_register_static(&qio_channel_tls_info);
}

type_init(qio_channel_tls_register_types);
