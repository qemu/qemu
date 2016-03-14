/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
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
#include "qemu-common.h"
#include "qemu/error-report.h"
#include "qemu/iov.h"
#include "qemu/sockets.h"
#include "qemu/coroutine.h"
#include "migration/qemu-file.h"
#include "migration/qemu-file-internal.h"

typedef struct QEMUFileSocket {
    int fd;
    QEMUFile *file;
} QEMUFileSocket;

static ssize_t socket_writev_buffer(void *opaque, struct iovec *iov, int iovcnt,
                                    int64_t pos)
{
    QEMUFileSocket *s = opaque;
    ssize_t len;
    ssize_t size = iov_size(iov, iovcnt);
    ssize_t offset = 0;
    int     err;

    while (size > 0) {
        len = iov_send(s->fd, iov, iovcnt, offset, size);

        if (len > 0) {
            size -= len;
            offset += len;
        }

        if (size > 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                error_report("socket_writev_buffer: Got err=%d for (%zu/%zu)",
                             errno, (size_t)size, (size_t)len);
                /*
                 * If I've already sent some but only just got the error, I
                 * could return the amount validly sent so far and wait for the
                 * next call to report the error, but I'd rather flag the error
                 * immediately.
                 */
                return -errno;
            }

            /* Emulate blocking */
            GPollFD pfd;

            pfd.fd = s->fd;
            pfd.events = G_IO_OUT | G_IO_ERR;
            pfd.revents = 0;
            TFR(err = g_poll(&pfd, 1, -1 /* no timeout */));
            /* Errors other than EINTR intentionally ignored */
        }
     }

    return offset;
}

static int socket_get_fd(void *opaque)
{
    QEMUFileSocket *s = opaque;

    return s->fd;
}

static ssize_t socket_get_buffer(void *opaque, uint8_t *buf, int64_t pos,
                                 size_t size)
{
    QEMUFileSocket *s = opaque;
    ssize_t len;

    for (;;) {
        len = qemu_recv(s->fd, buf, size, 0);
        if (len != -1) {
            break;
        }
        if (errno == EAGAIN) {
            yield_until_fd_readable(s->fd);
        } else if (errno != EINTR) {
            break;
        }
    }

    if (len == -1) {
        len = -errno;
    }
    return len;
}

static int socket_close(void *opaque)
{
    QEMUFileSocket *s = opaque;
    closesocket(s->fd);
    g_free(s);
    return 0;
}

static int socket_shutdown(void *opaque, bool rd, bool wr)
{
    QEMUFileSocket *s = opaque;

    if (shutdown(s->fd, rd ? (wr ? SHUT_RDWR : SHUT_RD) : SHUT_WR)) {
        return -errno;
    } else {
        return 0;
    }
}

static int socket_return_close(void *opaque)
{
    QEMUFileSocket *s = opaque;
    /*
     * Note: We don't close the socket, that should be done by the forward
     * path.
     */
    g_free(s);
    return 0;
}

static const QEMUFileOps socket_return_read_ops = {
    .get_fd          = socket_get_fd,
    .get_buffer      = socket_get_buffer,
    .close           = socket_return_close,
    .shut_down       = socket_shutdown,
};

static const QEMUFileOps socket_return_write_ops = {
    .get_fd          = socket_get_fd,
    .writev_buffer   = socket_writev_buffer,
    .close           = socket_return_close,
    .shut_down       = socket_shutdown,
};

/*
 * Give a QEMUFile* off the same socket but data in the opposite
 * direction.
 */
static QEMUFile *socket_get_return_path(void *opaque)
{
    QEMUFileSocket *forward = opaque;
    QEMUFileSocket *reverse;

    if (qemu_file_get_error(forward->file)) {
        /* If the forward file is in error, don't try and open a return */
        return NULL;
    }

    reverse = g_malloc0(sizeof(QEMUFileSocket));
    reverse->fd = forward->fd;
    /* I don't think there's a better way to tell which direction 'this' is */
    if (forward->file->ops->get_buffer != NULL) {
        /* being called from the read side, so we need to be able to write */
        return qemu_fopen_ops(reverse, &socket_return_write_ops);
    } else {
        return qemu_fopen_ops(reverse, &socket_return_read_ops);
    }
}

static ssize_t unix_writev_buffer(void *opaque, struct iovec *iov, int iovcnt,
                                  int64_t pos)
{
    QEMUFileSocket *s = opaque;
    ssize_t len, offset;
    ssize_t size = iov_size(iov, iovcnt);
    ssize_t total = 0;

    assert(iovcnt > 0);
    offset = 0;
    while (size > 0) {
        /* Find the next start position; skip all full-sized vector elements  */
        while (offset >= iov[0].iov_len) {
            offset -= iov[0].iov_len;
            iov++, iovcnt--;
        }

        /* skip `offset' bytes from the (now) first element, undo it on exit */
        assert(iovcnt > 0);
        iov[0].iov_base += offset;
        iov[0].iov_len -= offset;

        do {
            len = writev(s->fd, iov, iovcnt);
        } while (len == -1 && errno == EINTR);
        if (len == -1) {
            return -errno;
        }

        /* Undo the changes above */
        iov[0].iov_base -= offset;
        iov[0].iov_len += offset;

        /* Prepare for the next iteration */
        offset += len;
        total += len;
        size -= len;
    }

    return total;
}

static ssize_t unix_get_buffer(void *opaque, uint8_t *buf, int64_t pos,
                              size_t size)
{
    QEMUFileSocket *s = opaque;
    ssize_t len;

    for (;;) {
        len = read(s->fd, buf, size);
        if (len != -1) {
            break;
        }
        if (errno == EAGAIN) {
            yield_until_fd_readable(s->fd);
        } else if (errno != EINTR) {
            break;
        }
    }

    if (len == -1) {
        len = -errno;
    }
    return len;
}

static int unix_close(void *opaque)
{
    QEMUFileSocket *s = opaque;
    close(s->fd);
    g_free(s);
    return 0;
}

static const QEMUFileOps unix_read_ops = {
    .get_fd =     socket_get_fd,
    .get_buffer = unix_get_buffer,
    .close =      unix_close
};

static const QEMUFileOps unix_write_ops = {
    .get_fd =     socket_get_fd,
    .writev_buffer = unix_writev_buffer,
    .close =      unix_close
};

QEMUFile *qemu_fdopen(int fd, const char *mode)
{
    QEMUFileSocket *s;

    if (mode == NULL ||
        (mode[0] != 'r' && mode[0] != 'w') ||
        mode[1] != 'b' || mode[2] != 0) {
        fprintf(stderr, "qemu_fdopen: Argument validity check failed\n");
        return NULL;
    }

    s = g_new0(QEMUFileSocket, 1);
    s->fd = fd;

    if (mode[0] == 'r') {
        s->file = qemu_fopen_ops(s, &unix_read_ops);
    } else {
        s->file = qemu_fopen_ops(s, &unix_write_ops);
    }
    return s->file;
}

static const QEMUFileOps socket_read_ops = {
    .get_fd          = socket_get_fd,
    .get_buffer      = socket_get_buffer,
    .close           = socket_close,
    .shut_down       = socket_shutdown,
    .get_return_path = socket_get_return_path
};

static const QEMUFileOps socket_write_ops = {
    .get_fd          = socket_get_fd,
    .writev_buffer   = socket_writev_buffer,
    .close           = socket_close,
    .shut_down       = socket_shutdown,
    .get_return_path = socket_get_return_path
};

QEMUFile *qemu_fopen_socket(int fd, const char *mode)
{
    QEMUFileSocket *s;

    if (qemu_file_mode_is_not_valid(mode)) {
        return NULL;
    }

    s = g_new0(QEMUFileSocket, 1);
    s->fd = fd;
    if (mode[0] == 'w') {
        qemu_set_block(s->fd);
        s->file = qemu_fopen_ops(s, &socket_write_ops);
    } else {
        s->file = qemu_fopen_ops(s, &socket_read_ops);
    }
    return s->file;
}
