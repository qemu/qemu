#include "qemu-common.h"
#include "qemu/iov.h"
#include "qemu/sockets.h"
#include "block/coroutine.h"
#include "migration/migration.h"
#include "migration/qemu-file.h"
#include "trace.h"

#define IO_BUF_SIZE 32768
#define MAX_IOV_SIZE MIN(IOV_MAX, 64)

struct QEMUFile {
    const QEMUFileOps *ops;
    void *opaque;

    int64_t bytes_xfer;
    int64_t xfer_limit;

    int64_t pos; /* start of buffer when writing, end of buffer
                    when reading */
    int buf_index;
    int buf_size; /* 0 when writing */
    uint8_t buf[IO_BUF_SIZE];

    struct iovec iov[MAX_IOV_SIZE];
    unsigned int iovcnt;

    int last_error;
};

typedef struct QEMUFileStdio {
    FILE *stdio_file;
    QEMUFile *file;
} QEMUFileStdio;

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

static int stdio_get_fd(void *opaque)
{
    QEMUFileStdio *s = opaque;

    return fileno(s->stdio_file);
}

static int stdio_put_buffer(void *opaque, const uint8_t *buf, int64_t pos,
                            int size)
{
    QEMUFileStdio *s = opaque;
    int res;

    res = fwrite(buf, 1, size, s->stdio_file);

    if (res != size) {
        return -errno;
    }
    return res;
}

static int stdio_get_buffer(void *opaque, uint8_t *buf, int64_t pos, int size)
{
    QEMUFileStdio *s = opaque;
    FILE *fp = s->stdio_file;
    int bytes;

    for (;;) {
        clearerr(fp);
        bytes = fread(buf, 1, size, fp);
        if (bytes != 0 || !ferror(fp)) {
            break;
        }
        if (errno == EAGAIN) {
            yield_until_fd_readable(fileno(fp));
        } else if (errno != EINTR) {
            break;
        }
    }
    return bytes;
}

static int stdio_pclose(void *opaque)
{
    QEMUFileStdio *s = opaque;
    int ret;
    ret = pclose(s->stdio_file);
    if (ret == -1) {
        ret = -errno;
    } else if (!WIFEXITED(ret) || WEXITSTATUS(ret) != 0) {
        /* close succeeded, but non-zero exit code: */
        ret = -EIO; /* fake errno value */
    }
    g_free(s);
    return ret;
}

static int stdio_fclose(void *opaque)
{
    QEMUFileStdio *s = opaque;
    int ret = 0;

    if (s->file->ops->put_buffer || s->file->ops->writev_buffer) {
        int fd = fileno(s->stdio_file);
        struct stat st;

        ret = fstat(fd, &st);
        if (ret == 0 && S_ISREG(st.st_mode)) {
            /*
             * If the file handle is a regular file make sure the
             * data is flushed to disk before signaling success.
             */
            ret = fsync(fd);
            if (ret != 0) {
                ret = -errno;
                return ret;
            }
        }
    }
    if (fclose(s->stdio_file) == EOF) {
        ret = -errno;
    }
    g_free(s);
    return ret;
}

static const QEMUFileOps stdio_pipe_read_ops = {
    .get_fd =     stdio_get_fd,
    .get_buffer = stdio_get_buffer,
    .close =      stdio_pclose
};

static const QEMUFileOps stdio_pipe_write_ops = {
    .get_fd =     stdio_get_fd,
    .put_buffer = stdio_put_buffer,
    .close =      stdio_pclose
};

QEMUFile *qemu_popen_cmd(const char *command, const char *mode)
{
    FILE *stdio_file;
    QEMUFileStdio *s;

    if (mode == NULL || (mode[0] != 'r' && mode[0] != 'w') || mode[1] != 0) {
        fprintf(stderr, "qemu_popen: Argument validity check failed\n");
        return NULL;
    }

    stdio_file = popen(command, mode);
    if (stdio_file == NULL) {
        return NULL;
    }

    s = g_malloc0(sizeof(QEMUFileStdio));

    s->stdio_file = stdio_file;

    if (mode[0] == 'r') {
        s->file = qemu_fopen_ops(s, &stdio_pipe_read_ops);
    } else {
        s->file = qemu_fopen_ops(s, &stdio_pipe_write_ops);
    }
    return s->file;
}

static const QEMUFileOps stdio_file_read_ops = {
    .get_fd =     stdio_get_fd,
    .get_buffer = stdio_get_buffer,
    .close =      stdio_fclose
};

static const QEMUFileOps stdio_file_write_ops = {
    .get_fd =     stdio_get_fd,
    .put_buffer = stdio_put_buffer,
    .close =      stdio_fclose
};

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

bool qemu_file_mode_is_not_valid(const char *mode)
{
    if (mode == NULL ||
        (mode[0] != 'r' && mode[0] != 'w') ||
        mode[1] != 'b' || mode[2] != 0) {
        fprintf(stderr, "qemu_fopen: Argument validity check failed\n");
        return true;
    }

    return false;
}

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

QEMUFile *qemu_fopen(const char *filename, const char *mode)
{
    QEMUFileStdio *s;

    if (qemu_file_mode_is_not_valid(mode)) {
        return NULL;
    }

    s = g_malloc0(sizeof(QEMUFileStdio));

    s->stdio_file = fopen(filename, mode);
    if (!s->stdio_file) {
        goto fail;
    }

    if (mode[0] == 'w') {
        s->file = qemu_fopen_ops(s, &stdio_file_write_ops);
    } else {
        s->file = qemu_fopen_ops(s, &stdio_file_read_ops);
    }
    return s->file;
fail:
    g_free(s);
    return NULL;
}

QEMUFile *qemu_fopen_ops(void *opaque, const QEMUFileOps *ops)
{
    QEMUFile *f;

    f = g_malloc0(sizeof(QEMUFile));

    f->opaque = opaque;
    f->ops = ops;
    return f;
}

/*
 * Get last error for stream f
 *
 * Return negative error value if there has been an error on previous
 * operations, return 0 if no error happened.
 *
 */
int qemu_file_get_error(QEMUFile *f)
{
    return f->last_error;
}

void qemu_file_set_error(QEMUFile *f, int ret)
{
    if (f->last_error == 0) {
        f->last_error = ret;
    }
}

static inline bool qemu_file_is_writable(QEMUFile *f)
{
    return f->ops->writev_buffer || f->ops->put_buffer;
}

/**
 * Flushes QEMUFile buffer
 *
 * If there is writev_buffer QEMUFileOps it uses it otherwise uses
 * put_buffer ops.
 */
void qemu_fflush(QEMUFile *f)
{
    ssize_t ret = 0;

    if (!qemu_file_is_writable(f)) {
        return;
    }

    if (f->ops->writev_buffer) {
        if (f->iovcnt > 0) {
            ret = f->ops->writev_buffer(f->opaque, f->iov, f->iovcnt, f->pos);
        }
    } else {
        if (f->buf_index > 0) {
            ret = f->ops->put_buffer(f->opaque, f->buf, f->pos, f->buf_index);
        }
    }
    if (ret >= 0) {
        f->pos += ret;
    }
    f->buf_index = 0;
    f->iovcnt = 0;
    if (ret < 0) {
        qemu_file_set_error(f, ret);
    }
}

void ram_control_before_iterate(QEMUFile *f, uint64_t flags)
{
    int ret = 0;

    if (f->ops->before_ram_iterate) {
        ret = f->ops->before_ram_iterate(f, f->opaque, flags);
        if (ret < 0) {
            qemu_file_set_error(f, ret);
        }
    }
}

void ram_control_after_iterate(QEMUFile *f, uint64_t flags)
{
    int ret = 0;

    if (f->ops->after_ram_iterate) {
        ret = f->ops->after_ram_iterate(f, f->opaque, flags);
        if (ret < 0) {
            qemu_file_set_error(f, ret);
        }
    }
}

void ram_control_load_hook(QEMUFile *f, uint64_t flags)
{
    int ret = -EINVAL;

    if (f->ops->hook_ram_load) {
        ret = f->ops->hook_ram_load(f, f->opaque, flags);
        if (ret < 0) {
            qemu_file_set_error(f, ret);
        }
    } else {
        qemu_file_set_error(f, ret);
    }
}

size_t ram_control_save_page(QEMUFile *f, ram_addr_t block_offset,
                         ram_addr_t offset, size_t size, int *bytes_sent)
{
    if (f->ops->save_page) {
        int ret = f->ops->save_page(f, f->opaque, block_offset,
                                    offset, size, bytes_sent);

        if (ret != RAM_SAVE_CONTROL_DELAYED) {
            if (bytes_sent && *bytes_sent > 0) {
                qemu_update_position(f, *bytes_sent);
            } else if (ret < 0) {
                qemu_file_set_error(f, ret);
            }
        }

        return ret;
    }

    return RAM_SAVE_CONTROL_NOT_SUPP;
}

static void qemu_fill_buffer(QEMUFile *f)
{
    int len;
    int pending;

    assert(!qemu_file_is_writable(f));

    pending = f->buf_size - f->buf_index;
    if (pending > 0) {
        memmove(f->buf, f->buf + f->buf_index, pending);
    }
    f->buf_index = 0;
    f->buf_size = pending;

    len = f->ops->get_buffer(f->opaque, f->buf + pending, f->pos,
                        IO_BUF_SIZE - pending);
    if (len > 0) {
        f->buf_size += len;
        f->pos += len;
    } else if (len == 0) {
        qemu_file_set_error(f, -EIO);
    } else if (len != -EAGAIN) {
        qemu_file_set_error(f, len);
    }
}

int qemu_get_fd(QEMUFile *f)
{
    if (f->ops->get_fd) {
        return f->ops->get_fd(f->opaque);
    }
    return -1;
}

void qemu_update_position(QEMUFile *f, size_t size)
{
    f->pos += size;
}

/** Closes the file
 *
 * Returns negative error value if any error happened on previous operations or
 * while closing the file. Returns 0 or positive number on success.
 *
 * The meaning of return value on success depends on the specific backend
 * being used.
 */
int qemu_fclose(QEMUFile *f)
{
    int ret;
    qemu_fflush(f);
    ret = qemu_file_get_error(f);

    if (f->ops->close) {
        int ret2 = f->ops->close(f->opaque);
        if (ret >= 0) {
            ret = ret2;
        }
    }
    /* If any error was spotted before closing, we should report it
     * instead of the close() return value.
     */
    if (f->last_error) {
        ret = f->last_error;
    }
    g_free(f);
    trace_qemu_file_fclose();
    return ret;
}

static void add_to_iovec(QEMUFile *f, const uint8_t *buf, int size)
{
    /* check for adjacent buffer and coalesce them */
    if (f->iovcnt > 0 && buf == f->iov[f->iovcnt - 1].iov_base +
        f->iov[f->iovcnt - 1].iov_len) {
        f->iov[f->iovcnt - 1].iov_len += size;
    } else {
        f->iov[f->iovcnt].iov_base = (uint8_t *)buf;
        f->iov[f->iovcnt++].iov_len = size;
    }

    if (f->iovcnt >= MAX_IOV_SIZE) {
        qemu_fflush(f);
    }
}

void qemu_put_buffer_async(QEMUFile *f, const uint8_t *buf, int size)
{
    if (!f->ops->writev_buffer) {
        qemu_put_buffer(f, buf, size);
        return;
    }

    if (f->last_error) {
        return;
    }

    f->bytes_xfer += size;
    add_to_iovec(f, buf, size);
}

void qemu_put_buffer(QEMUFile *f, const uint8_t *buf, int size)
{
    int l;

    if (f->last_error) {
        return;
    }

    while (size > 0) {
        l = IO_BUF_SIZE - f->buf_index;
        if (l > size) {
            l = size;
        }
        memcpy(f->buf + f->buf_index, buf, l);
        f->bytes_xfer += l;
        if (f->ops->writev_buffer) {
            add_to_iovec(f, f->buf + f->buf_index, l);
        }
        f->buf_index += l;
        if (f->buf_index == IO_BUF_SIZE) {
            qemu_fflush(f);
        }
        if (qemu_file_get_error(f)) {
            break;
        }
        buf += l;
        size -= l;
    }
}

void qemu_put_byte(QEMUFile *f, int v)
{
    if (f->last_error) {
        return;
    }

    f->buf[f->buf_index] = v;
    f->bytes_xfer++;
    if (f->ops->writev_buffer) {
        add_to_iovec(f, f->buf + f->buf_index, 1);
    }
    f->buf_index++;
    if (f->buf_index == IO_BUF_SIZE) {
        qemu_fflush(f);
    }
}

void qemu_file_skip(QEMUFile *f, int size)
{
    if (f->buf_index + size <= f->buf_size) {
        f->buf_index += size;
    }
}

int qemu_peek_buffer(QEMUFile *f, uint8_t *buf, int size, size_t offset)
{
    int pending;
    int index;

    assert(!qemu_file_is_writable(f));

    index = f->buf_index + offset;
    pending = f->buf_size - index;
    if (pending < size) {
        qemu_fill_buffer(f);
        index = f->buf_index + offset;
        pending = f->buf_size - index;
    }

    if (pending <= 0) {
        return 0;
    }
    if (size > pending) {
        size = pending;
    }

    memcpy(buf, f->buf + index, size);
    return size;
}

int qemu_get_buffer(QEMUFile *f, uint8_t *buf, int size)
{
    int pending = size;
    int done = 0;

    while (pending > 0) {
        int res;

        res = qemu_peek_buffer(f, buf, pending, 0);
        if (res == 0) {
            return done;
        }
        qemu_file_skip(f, res);
        buf += res;
        pending -= res;
        done += res;
    }
    return done;
}

int qemu_peek_byte(QEMUFile *f, int offset)
{
    int index = f->buf_index + offset;

    assert(!qemu_file_is_writable(f));

    if (index >= f->buf_size) {
        qemu_fill_buffer(f);
        index = f->buf_index + offset;
        if (index >= f->buf_size) {
            return 0;
        }
    }
    return f->buf[index];
}

int qemu_get_byte(QEMUFile *f)
{
    int result;

    result = qemu_peek_byte(f, 0);
    qemu_file_skip(f, 1);
    return result;
}

int64_t qemu_ftell(QEMUFile *f)
{
    qemu_fflush(f);
    return f->pos;
}

int qemu_file_rate_limit(QEMUFile *f)
{
    if (qemu_file_get_error(f)) {
        return 1;
    }
    if (f->xfer_limit > 0 && f->bytes_xfer > f->xfer_limit) {
        return 1;
    }
    return 0;
}

int64_t qemu_file_get_rate_limit(QEMUFile *f)
{
    return f->xfer_limit;
}

void qemu_file_set_rate_limit(QEMUFile *f, int64_t limit)
{
    f->xfer_limit = limit;
}

void qemu_file_reset_rate_limit(QEMUFile *f)
{
    f->bytes_xfer = 0;
}

void qemu_put_be16(QEMUFile *f, unsigned int v)
{
    qemu_put_byte(f, v >> 8);
    qemu_put_byte(f, v);
}

void qemu_put_be32(QEMUFile *f, unsigned int v)
{
    qemu_put_byte(f, v >> 24);
    qemu_put_byte(f, v >> 16);
    qemu_put_byte(f, v >> 8);
    qemu_put_byte(f, v);
}

void qemu_put_be64(QEMUFile *f, uint64_t v)
{
    qemu_put_be32(f, v >> 32);
    qemu_put_be32(f, v);
}

unsigned int qemu_get_be16(QEMUFile *f)
{
    unsigned int v;
    v = qemu_get_byte(f) << 8;
    v |= qemu_get_byte(f);
    return v;
}

unsigned int qemu_get_be32(QEMUFile *f)
{
    unsigned int v;
    v = qemu_get_byte(f) << 24;
    v |= qemu_get_byte(f) << 16;
    v |= qemu_get_byte(f) << 8;
    v |= qemu_get_byte(f);
    return v;
}

uint64_t qemu_get_be64(QEMUFile *f)
{
    uint64_t v;
    v = (uint64_t)qemu_get_be32(f) << 32;
    v |= qemu_get_be32(f);
    return v;
}
