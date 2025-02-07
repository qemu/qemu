/*
 * QEMU I/O channels
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
#include "block/aio-wait.h"
#include "io/channel.h"
#include "qapi/error.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qemu/iov.h"

bool qio_channel_has_feature(QIOChannel *ioc,
                             QIOChannelFeature feature)
{
    return ioc->features & (1 << feature);
}


void qio_channel_set_feature(QIOChannel *ioc,
                             QIOChannelFeature feature)
{
    ioc->features |= (1 << feature);
}


void qio_channel_set_name(QIOChannel *ioc,
                          const char *name)
{
    g_free(ioc->name);
    ioc->name = g_strdup(name);
}


ssize_t qio_channel_readv_full(QIOChannel *ioc,
                               const struct iovec *iov,
                               size_t niov,
                               int **fds,
                               size_t *nfds,
                               int flags,
                               Error **errp)
{
    QIOChannelClass *klass = QIO_CHANNEL_GET_CLASS(ioc);

    if ((fds || nfds) &&
        !qio_channel_has_feature(ioc, QIO_CHANNEL_FEATURE_FD_PASS)) {
        error_setg_errno(errp, EINVAL,
                         "Channel does not support file descriptor passing");
        return -1;
    }

    if ((flags & QIO_CHANNEL_READ_FLAG_MSG_PEEK) &&
        !qio_channel_has_feature(ioc, QIO_CHANNEL_FEATURE_READ_MSG_PEEK)) {
        error_setg_errno(errp, EINVAL,
                         "Channel does not support peek read");
        return -1;
    }

    return klass->io_readv(ioc, iov, niov, fds, nfds, flags, errp);
}


ssize_t qio_channel_writev_full(QIOChannel *ioc,
                                const struct iovec *iov,
                                size_t niov,
                                int *fds,
                                size_t nfds,
                                int flags,
                                Error **errp)
{
    QIOChannelClass *klass = QIO_CHANNEL_GET_CLASS(ioc);

    if (fds || nfds) {
        if (!qio_channel_has_feature(ioc, QIO_CHANNEL_FEATURE_FD_PASS)) {
            error_setg_errno(errp, EINVAL,
                             "Channel does not support file descriptor passing");
            return -1;
        }
        if (flags & QIO_CHANNEL_WRITE_FLAG_ZERO_COPY) {
            error_setg_errno(errp, EINVAL,
                             "Zero Copy does not support file descriptor passing");
            return -1;
        }
    }

    if ((flags & QIO_CHANNEL_WRITE_FLAG_ZERO_COPY) &&
        !qio_channel_has_feature(ioc, QIO_CHANNEL_FEATURE_WRITE_ZERO_COPY)) {
        error_setg_errno(errp, EINVAL,
                         "Requested Zero Copy feature is not available");
        return -1;
    }

    return klass->io_writev(ioc, iov, niov, fds, nfds, flags, errp);
}


int coroutine_mixed_fn qio_channel_readv_all_eof(QIOChannel *ioc,
                                                 const struct iovec *iov,
                                                 size_t niov,
                                                 Error **errp)
{
    return qio_channel_readv_full_all_eof(ioc, iov, niov, NULL, NULL, 0,
                                          errp);
}

int coroutine_mixed_fn qio_channel_readv_all(QIOChannel *ioc,
                                             const struct iovec *iov,
                                             size_t niov,
                                             Error **errp)
{
    return qio_channel_readv_full_all(ioc, iov, niov, NULL, NULL, errp);
}

int coroutine_mixed_fn qio_channel_readv_full_all_eof(QIOChannel *ioc,
                                                      const struct iovec *iov,
                                                      size_t niov,
                                                      int **fds, size_t *nfds,
                                                      int flags,
                                                      Error **errp)
{
    int ret = -1;
    struct iovec *local_iov = g_new(struct iovec, niov);
    struct iovec *local_iov_head = local_iov;
    unsigned int nlocal_iov = niov;
    int **local_fds = fds;
    size_t *local_nfds = nfds;
    bool partial = false;

    if (nfds) {
        *nfds = 0;
    }

    if (fds) {
        *fds = NULL;
    }

    nlocal_iov = iov_copy(local_iov, nlocal_iov,
                          iov, niov,
                          0, iov_size(iov, niov));

    while ((nlocal_iov > 0) || local_fds) {
        ssize_t len;
        len = qio_channel_readv_full(ioc, local_iov, nlocal_iov, local_fds,
                                     local_nfds, flags, errp);
        if (len == QIO_CHANNEL_ERR_BLOCK) {
            if (qemu_in_coroutine()) {
                qio_channel_yield(ioc, G_IO_IN);
            } else {
                qio_channel_wait(ioc, G_IO_IN);
            }
            continue;
        }

        if (len == 0) {
            if (local_nfds && *local_nfds) {
                /*
                 * Got some FDs, but no data yet. This isn't an EOF
                 * scenario (yet), so carry on to try to read data
                 * on next loop iteration
                 */
                goto next_iter;
            } else if (!partial) {
                /* No fds and no data - EOF before any data read */
                ret = 0;
                goto cleanup;
            } else {
                len = -1;
                error_setg(errp,
                           "Unexpected end-of-file before all data were read");
                /* Fallthrough into len < 0 handling */
            }
        }

        if (len < 0) {
            /* Close any FDs we previously received */
            if (nfds && fds) {
                size_t i;
                for (i = 0; i < (*nfds); i++) {
                    close((*fds)[i]);
                }
                g_free(*fds);
                *fds = NULL;
                *nfds = 0;
            }
            goto cleanup;
        }

        if (nlocal_iov) {
            iov_discard_front(&local_iov, &nlocal_iov, len);
        }

next_iter:
        partial = true;
        local_fds = NULL;
        local_nfds = NULL;
    }

    ret = 1;

 cleanup:
    g_free(local_iov_head);
    return ret;
}

int coroutine_mixed_fn qio_channel_readv_full_all(QIOChannel *ioc,
                                                  const struct iovec *iov,
                                                  size_t niov,
                                                  int **fds, size_t *nfds,
                                                  Error **errp)
{
    int ret = qio_channel_readv_full_all_eof(ioc, iov, niov, fds, nfds, 0,
                                             errp);

    if (ret == 0) {
        error_setg(errp, "Unexpected end-of-file before all data were read");
        return -1;
    }
    if (ret == 1) {
        return 0;
    }

    return ret;
}

int coroutine_mixed_fn qio_channel_writev_all(QIOChannel *ioc,
                                              const struct iovec *iov,
                                              size_t niov,
                                              Error **errp)
{
    return qio_channel_writev_full_all(ioc, iov, niov, NULL, 0, 0, errp);
}

int coroutine_mixed_fn qio_channel_writev_full_all(QIOChannel *ioc,
                                                   const struct iovec *iov,
                                                   size_t niov,
                                                   int *fds, size_t nfds,
                                                   int flags, Error **errp)
{
    int ret = -1;
    struct iovec *local_iov = g_new(struct iovec, niov);
    struct iovec *local_iov_head = local_iov;
    unsigned int nlocal_iov = niov;

    nlocal_iov = iov_copy(local_iov, nlocal_iov,
                          iov, niov,
                          0, iov_size(iov, niov));

    while (nlocal_iov > 0) {
        ssize_t len;

        len = qio_channel_writev_full(ioc, local_iov, nlocal_iov, fds,
                                            nfds, flags, errp);

        if (len == QIO_CHANNEL_ERR_BLOCK) {
            if (qemu_in_coroutine()) {
                qio_channel_yield(ioc, G_IO_OUT);
            } else {
                qio_channel_wait(ioc, G_IO_OUT);
            }
            continue;
        }
        if (len < 0) {
            goto cleanup;
        }

        iov_discard_front(&local_iov, &nlocal_iov, len);

        fds = NULL;
        nfds = 0;
    }

    ret = 0;
 cleanup:
    g_free(local_iov_head);
    return ret;
}

ssize_t qio_channel_readv(QIOChannel *ioc,
                          const struct iovec *iov,
                          size_t niov,
                          Error **errp)
{
    return qio_channel_readv_full(ioc, iov, niov, NULL, NULL, 0, errp);
}


ssize_t qio_channel_writev(QIOChannel *ioc,
                           const struct iovec *iov,
                           size_t niov,
                           Error **errp)
{
    return qio_channel_writev_full(ioc, iov, niov, NULL, 0, 0, errp);
}


ssize_t qio_channel_read(QIOChannel *ioc,
                         char *buf,
                         size_t buflen,
                         Error **errp)
{
    struct iovec iov = { .iov_base = buf, .iov_len = buflen };
    return qio_channel_readv_full(ioc, &iov, 1, NULL, NULL, 0, errp);
}


ssize_t qio_channel_write(QIOChannel *ioc,
                          const char *buf,
                          size_t buflen,
                          Error **errp)
{
    struct iovec iov = { .iov_base = (char *)buf, .iov_len = buflen };
    return qio_channel_writev_full(ioc, &iov, 1, NULL, 0, 0, errp);
}


int coroutine_mixed_fn qio_channel_read_all_eof(QIOChannel *ioc,
                                                char *buf,
                                                size_t buflen,
                                                Error **errp)
{
    struct iovec iov = { .iov_base = buf, .iov_len = buflen };
    return qio_channel_readv_all_eof(ioc, &iov, 1, errp);
}


int coroutine_mixed_fn qio_channel_read_all(QIOChannel *ioc,
                                            char *buf,
                                            size_t buflen,
                                            Error **errp)
{
    struct iovec iov = { .iov_base = buf, .iov_len = buflen };
    return qio_channel_readv_all(ioc, &iov, 1, errp);
}


int coroutine_mixed_fn qio_channel_write_all(QIOChannel *ioc,
                                             const char *buf,
                                             size_t buflen,
                                             Error **errp)
{
    struct iovec iov = { .iov_base = (char *)buf, .iov_len = buflen };
    return qio_channel_writev_all(ioc, &iov, 1, errp);
}


int qio_channel_set_blocking(QIOChannel *ioc,
                              bool enabled,
                              Error **errp)
{
    QIOChannelClass *klass = QIO_CHANNEL_GET_CLASS(ioc);
    return klass->io_set_blocking(ioc, enabled, errp);
}


void qio_channel_set_follow_coroutine_ctx(QIOChannel *ioc, bool enabled)
{
    ioc->follow_coroutine_ctx = enabled;
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
    GSource *ret = klass->io_create_watch(ioc, condition);

    if (ioc->name) {
        g_source_set_name(ret, ioc->name);
    }

    return ret;
}


void qio_channel_set_aio_fd_handler(QIOChannel *ioc,
                                    AioContext *read_ctx,
                                    IOHandler *io_read,
                                    AioContext *write_ctx,
                                    IOHandler *io_write,
                                    void *opaque)
{
    QIOChannelClass *klass = QIO_CHANNEL_GET_CLASS(ioc);

    klass->io_set_aio_fd_handler(ioc, read_ctx, io_read, write_ctx, io_write,
            opaque);
}

guint qio_channel_add_watch_full(QIOChannel *ioc,
                                 GIOCondition condition,
                                 QIOChannelFunc func,
                                 gpointer user_data,
                                 GDestroyNotify notify,
                                 GMainContext *context)
{
    GSource *source;
    guint id;

    source = qio_channel_create_watch(ioc, condition);

    g_source_set_callback(source, (GSourceFunc)func, user_data, notify);

    id = g_source_attach(source, context);
    g_source_unref(source);

    return id;
}

guint qio_channel_add_watch(QIOChannel *ioc,
                            GIOCondition condition,
                            QIOChannelFunc func,
                            gpointer user_data,
                            GDestroyNotify notify)
{
    return qio_channel_add_watch_full(ioc, condition, func,
                                      user_data, notify, NULL);
}

GSource *qio_channel_add_watch_source(QIOChannel *ioc,
                                      GIOCondition condition,
                                      QIOChannelFunc func,
                                      gpointer user_data,
                                      GDestroyNotify notify,
                                      GMainContext *context)
{
    GSource *source;
    guint id;

    id = qio_channel_add_watch_full(ioc, condition, func,
                                    user_data, notify, context);
    source = g_main_context_find_source_by_id(context, id);
    g_source_ref(source);
    return source;
}


ssize_t qio_channel_pwritev(QIOChannel *ioc, const struct iovec *iov,
                            size_t niov, off_t offset, Error **errp)
{
    QIOChannelClass *klass = QIO_CHANNEL_GET_CLASS(ioc);

    if (!klass->io_pwritev) {
        error_setg(errp, "Channel does not support pwritev");
        return -1;
    }

    if (!qio_channel_has_feature(ioc, QIO_CHANNEL_FEATURE_SEEKABLE)) {
        error_setg_errno(errp, EINVAL, "Requested channel is not seekable");
        return -1;
    }

    return klass->io_pwritev(ioc, iov, niov, offset, errp);
}

ssize_t qio_channel_pwrite(QIOChannel *ioc, char *buf, size_t buflen,
                           off_t offset, Error **errp)
{
    struct iovec iov = {
        .iov_base = buf,
        .iov_len = buflen
    };

    return qio_channel_pwritev(ioc, &iov, 1, offset, errp);
}

ssize_t qio_channel_preadv(QIOChannel *ioc, const struct iovec *iov,
                           size_t niov, off_t offset, Error **errp)
{
    QIOChannelClass *klass = QIO_CHANNEL_GET_CLASS(ioc);

    if (!klass->io_preadv) {
        error_setg(errp, "Channel does not support preadv");
        return -1;
    }

    if (!qio_channel_has_feature(ioc, QIO_CHANNEL_FEATURE_SEEKABLE)) {
        error_setg_errno(errp, EINVAL, "Requested channel is not seekable");
        return -1;
    }

    return klass->io_preadv(ioc, iov, niov, offset, errp);
}

ssize_t qio_channel_pread(QIOChannel *ioc, char *buf, size_t buflen,
                          off_t offset, Error **errp)
{
    struct iovec iov = {
        .iov_base = buf,
        .iov_len = buflen
    };

    return qio_channel_preadv(ioc, &iov, 1, offset, errp);
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

int qio_channel_get_peerpid(QIOChannel *ioc,
                             unsigned int *pid,
                             Error **errp)
{
    QIOChannelClass *klass = QIO_CHANNEL_GET_CLASS(ioc);

    if (!klass->io_peerpid) {
        error_setg(errp, "Channel does not support peer pid");
        return -1;
    }
    klass->io_peerpid(ioc, pid, errp);
    return 0;
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

int qio_channel_flush(QIOChannel *ioc,
                                Error **errp)
{
    QIOChannelClass *klass = QIO_CHANNEL_GET_CLASS(ioc);

    if (!klass->io_flush ||
        !qio_channel_has_feature(ioc, QIO_CHANNEL_FEATURE_WRITE_ZERO_COPY)) {
        return 0;
    }

    return klass->io_flush(ioc, errp);
}


static void qio_channel_restart_read(void *opaque)
{
    QIOChannel *ioc = opaque;
    Coroutine *co = qatomic_xchg(&ioc->read_coroutine, NULL);

    if (!co) {
        return;
    }

    /* Assert that aio_co_wake() reenters the coroutine directly */
    assert(qemu_get_current_aio_context() ==
           qemu_coroutine_get_aio_context(co));
    aio_co_wake(co);
}

static void qio_channel_restart_write(void *opaque)
{
    QIOChannel *ioc = opaque;
    Coroutine *co = qatomic_xchg(&ioc->write_coroutine, NULL);

    if (!co) {
        return;
    }

    /* Assert that aio_co_wake() reenters the coroutine directly */
    assert(qemu_get_current_aio_context() ==
           qemu_coroutine_get_aio_context(co));
    aio_co_wake(co);
}

static void coroutine_fn
qio_channel_set_fd_handlers(QIOChannel *ioc, GIOCondition condition)
{
    AioContext *ctx = ioc->follow_coroutine_ctx ?
        qemu_coroutine_get_aio_context(qemu_coroutine_self()) :
        iohandler_get_aio_context();
    AioContext *read_ctx = NULL;
    IOHandler *io_read = NULL;
    AioContext *write_ctx = NULL;
    IOHandler *io_write = NULL;

    if (condition == G_IO_IN) {
        ioc->read_coroutine = qemu_coroutine_self();
        ioc->read_ctx = ctx;
        read_ctx = ctx;
        io_read = qio_channel_restart_read;

        /*
         * Thread safety: if the other coroutine is set and its AioContext
         * matches ours, then there is mutual exclusion between read and write
         * because they share a single thread and it's safe to set both read
         * and write fd handlers here. If the AioContext does not match ours,
         * then both threads may run in parallel but there is no shared state
         * to worry about.
         */
        if (ioc->write_coroutine && ioc->write_ctx == ctx) {
            write_ctx = ctx;
            io_write = qio_channel_restart_write;
        }
    } else if (condition == G_IO_OUT) {
        ioc->write_coroutine = qemu_coroutine_self();
        ioc->write_ctx = ctx;
        write_ctx = ctx;
        io_write = qio_channel_restart_write;
        if (ioc->read_coroutine && ioc->read_ctx == ctx) {
            read_ctx = ctx;
            io_read = qio_channel_restart_read;
        }
    } else {
        abort();
    }

    qio_channel_set_aio_fd_handler(ioc, read_ctx, io_read,
            write_ctx, io_write, ioc);
}

static void coroutine_fn
qio_channel_clear_fd_handlers(QIOChannel *ioc, GIOCondition condition)
{
    AioContext *read_ctx = NULL;
    IOHandler *io_read = NULL;
    AioContext *write_ctx = NULL;
    IOHandler *io_write = NULL;
    AioContext *ctx;

    if (condition == G_IO_IN) {
        ctx = ioc->read_ctx;
        read_ctx = ctx;
        io_read = NULL;
        if (ioc->write_coroutine && ioc->write_ctx == ctx) {
            write_ctx = ctx;
            io_write = qio_channel_restart_write;
        }
    } else if (condition == G_IO_OUT) {
        ctx = ioc->write_ctx;
        write_ctx = ctx;
        io_write = NULL;
        if (ioc->read_coroutine && ioc->read_ctx == ctx) {
            read_ctx = ctx;
            io_read = qio_channel_restart_read;
        }
    } else {
        abort();
    }

    qio_channel_set_aio_fd_handler(ioc, read_ctx, io_read,
            write_ctx, io_write, ioc);
}

void coroutine_fn qio_channel_yield(QIOChannel *ioc,
                                    GIOCondition condition)
{
    AioContext *ioc_ctx;

    assert(qemu_in_coroutine());
    ioc_ctx = qemu_coroutine_get_aio_context(qemu_coroutine_self());

    if (condition == G_IO_IN) {
        assert(!ioc->read_coroutine);
    } else if (condition == G_IO_OUT) {
        assert(!ioc->write_coroutine);
    } else {
        abort();
    }
    qio_channel_set_fd_handlers(ioc, condition);
    qemu_coroutine_yield();
    assert(in_aio_context_home_thread(ioc_ctx));

    /* Allow interrupting the operation by reentering the coroutine other than
     * through the aio_fd_handlers. */
    if (condition == G_IO_IN) {
        assert(ioc->read_coroutine == NULL);
    } else if (condition == G_IO_OUT) {
        assert(ioc->write_coroutine == NULL);
    }
    qio_channel_clear_fd_handlers(ioc, condition);
}

void qio_channel_wake_read(QIOChannel *ioc)
{
    Coroutine *co = qatomic_xchg(&ioc->read_coroutine, NULL);
    if (co) {
        aio_co_wake(co);
    }
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


static void qio_channel_finalize(Object *obj)
{
    QIOChannel *ioc = QIO_CHANNEL(obj);

    /* Must not have coroutines in qio_channel_yield() */
    assert(!ioc->read_coroutine);
    assert(!ioc->write_coroutine);

    g_free(ioc->name);

#ifdef _WIN32
    if (ioc->event) {
        CloseHandle(ioc->event);
    }
#endif
}

static const TypeInfo qio_channel_info = {
    .parent = TYPE_OBJECT,
    .name = TYPE_QIO_CHANNEL,
    .instance_size = sizeof(QIOChannel),
    .instance_finalize = qio_channel_finalize,
    .abstract = true,
    .class_size = sizeof(QIOChannelClass),
};


static void qio_channel_register_types(void)
{
    type_register_static(&qio_channel_info);
}


type_init(qio_channel_register_types);
