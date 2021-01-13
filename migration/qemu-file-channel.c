/*
 * QEMUFile backend for QIOChannel objects
 *
 * Copyright (c) 2015-2016 Red Hat, Inc
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu-file-channel.h"
#include "qemu-file.h"
#include "io/channel-socket.h"
#include "qemu/iov.h"
#include "qemu/yank.h"


static ssize_t channel_writev_buffer(void *opaque,
                                     struct iovec *iov,
                                     int iovcnt,
                                     int64_t pos,
                                     Error **errp)
{
    QIOChannel *ioc = QIO_CHANNEL(opaque);
    ssize_t done = 0;
    struct iovec *local_iov = g_new(struct iovec, iovcnt);
    struct iovec *local_iov_head = local_iov;
    unsigned int nlocal_iov = iovcnt;

    nlocal_iov = iov_copy(local_iov, nlocal_iov,
                          iov, iovcnt,
                          0, iov_size(iov, iovcnt));

    while (nlocal_iov > 0) {
        ssize_t len;
        len = qio_channel_writev(ioc, local_iov, nlocal_iov, errp);
        if (len == QIO_CHANNEL_ERR_BLOCK) {
            if (qemu_in_coroutine()) {
                qio_channel_yield(ioc, G_IO_OUT);
            } else {
                qio_channel_wait(ioc, G_IO_OUT);
            }
            continue;
        }
        if (len < 0) {
            done = -EIO;
            goto cleanup;
        }

        iov_discard_front(&local_iov, &nlocal_iov, len);
        done += len;
    }

 cleanup:
    g_free(local_iov_head);
    return done;
}


static ssize_t channel_get_buffer(void *opaque,
                                  uint8_t *buf,
                                  int64_t pos,
                                  size_t size,
                                  Error **errp)
{
    QIOChannel *ioc = QIO_CHANNEL(opaque);
    ssize_t ret;

    do {
        ret = qio_channel_read(ioc, (char *)buf, size, errp);
        if (ret < 0) {
            if (ret == QIO_CHANNEL_ERR_BLOCK) {
                if (qemu_in_coroutine()) {
                    qio_channel_yield(ioc, G_IO_IN);
                } else {
                    qio_channel_wait(ioc, G_IO_IN);
                }
            } else {
                return -EIO;
            }
        }
    } while (ret == QIO_CHANNEL_ERR_BLOCK);

    return ret;
}


static int channel_close(void *opaque, Error **errp)
{
    int ret;
    QIOChannel *ioc = QIO_CHANNEL(opaque);
    ret = qio_channel_close(ioc, errp);
    if (object_dynamic_cast(OBJECT(ioc), TYPE_QIO_CHANNEL_SOCKET)
        && OBJECT(ioc)->ref == 1) {
        yank_unregister_function(MIGRATION_YANK_INSTANCE,
                                 yank_generic_iochannel,
                                 QIO_CHANNEL(ioc));
    }
    object_unref(OBJECT(ioc));
    return ret;
}


static int channel_shutdown(void *opaque,
                            bool rd,
                            bool wr,
                            Error **errp)
{
    QIOChannel *ioc = QIO_CHANNEL(opaque);

    if (qio_channel_has_feature(ioc,
                                QIO_CHANNEL_FEATURE_SHUTDOWN)) {
        QIOChannelShutdown mode;
        if (rd && wr) {
            mode = QIO_CHANNEL_SHUTDOWN_BOTH;
        } else if (rd) {
            mode = QIO_CHANNEL_SHUTDOWN_READ;
        } else {
            mode = QIO_CHANNEL_SHUTDOWN_WRITE;
        }
        if (qio_channel_shutdown(ioc, mode, errp) < 0) {
            return -EIO;
        }
    }
    return 0;
}


static int channel_set_blocking(void *opaque,
                                bool enabled,
                                Error **errp)
{
    QIOChannel *ioc = QIO_CHANNEL(opaque);

    if (qio_channel_set_blocking(ioc, enabled, errp) < 0) {
        return -1;
    }
    return 0;
}

static QEMUFile *channel_get_input_return_path(void *opaque)
{
    QIOChannel *ioc = QIO_CHANNEL(opaque);

    return qemu_fopen_channel_output(ioc);
}

static QEMUFile *channel_get_output_return_path(void *opaque)
{
    QIOChannel *ioc = QIO_CHANNEL(opaque);

    return qemu_fopen_channel_input(ioc);
}

static const QEMUFileOps channel_input_ops = {
    .get_buffer = channel_get_buffer,
    .close = channel_close,
    .shut_down = channel_shutdown,
    .set_blocking = channel_set_blocking,
    .get_return_path = channel_get_input_return_path,
};


static const QEMUFileOps channel_output_ops = {
    .writev_buffer = channel_writev_buffer,
    .close = channel_close,
    .shut_down = channel_shutdown,
    .set_blocking = channel_set_blocking,
    .get_return_path = channel_get_output_return_path,
};


QEMUFile *qemu_fopen_channel_input(QIOChannel *ioc)
{
    object_ref(OBJECT(ioc));
    return qemu_fopen_ops(ioc, &channel_input_ops);
}

QEMUFile *qemu_fopen_channel_output(QIOChannel *ioc)
{
    object_ref(OBJECT(ioc));
    return qemu_fopen_ops(ioc, &channel_output_ops);
}
