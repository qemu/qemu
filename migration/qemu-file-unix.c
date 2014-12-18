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
#include "qemu-common.h"
#include "qemu/iov.h"
#include "qemu/sockets.h"
#include "block/coroutine.h"
#include "migration/qemu-file.h"

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

    len = iov_send(s->fd, iov, iovcnt, 0, size);
    if (len < size) {
        len = -socket_error();
    }
    return len;
}

static int socket_get_fd(void *opaque)
{
    QEMUFileSocket *s = opaque;

    return s->fd;
}

static int socket_get_buffer(void *opaque, uint8_t *buf, int64_t pos, int size)
{
    QEMUFileSocket *s = opaque;
    ssize_t len;

    for (;;) {
        len = qemu_recv(s->fd, buf, size, 0);
        if (len != -1) {
            break;
        }
        if (socket_error() == EAGAIN) {
            yield_until_fd_readable(s->fd);
        } else if (socket_error() != EINTR) {
            break;
        }
    }

    if (len == -1) {
        len = -socket_error();
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

static int unix_get_buffer(void *opaque, uint8_t *buf, int64_t pos, int size)
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

    s = g_malloc0(sizeof(QEMUFileSocket));
    s->fd = fd;

    if (mode[0] == 'r') {
        s->file = qemu_fopen_ops(s, &unix_read_ops);
    } else {
        s->file = qemu_fopen_ops(s, &unix_write_ops);
    }
    return s->file;
}

static const QEMUFileOps socket_read_ops = {
    .get_fd =     socket_get_fd,
    .get_buffer = socket_get_buffer,
    .close =      socket_close
};

static const QEMUFileOps socket_write_ops = {
    .get_fd =     socket_get_fd,
    .writev_buffer = socket_writev_buffer,
    .close =      socket_close
};

QEMUFile *qemu_fopen_socket(int fd, const char *mode)
{
    QEMUFileSocket *s;

    if (qemu_file_mode_is_not_valid(mode)) {
        return NULL;
    }

    s = g_malloc0(sizeof(QEMUFileSocket));
    s->fd = fd;
    if (mode[0] == 'w') {
        qemu_set_block(s->fd);
        s->file = qemu_fopen_ops(s, &socket_write_ops);
    } else {
        s->file = qemu_fopen_ops(s, &socket_read_ops);
    }
    return s->file;
}
