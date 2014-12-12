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

bool qemu_file_is_writable(QEMUFile *f)
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

/*
 * Attempt to fill the buffer from the underlying file
 * Returns the number of bytes read, or negative value for an error.
 *
 * Note that it can return a partially full buffer even in a not error/not EOF
 * case if the underlying file descriptor gives a short read, and that can
 * happen even on a blocking fd.
 */
static ssize_t qemu_fill_buffer(QEMUFile *f)
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

    return len;
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

/*
 * Read 'size' bytes from file (at 'offset') into buf without moving the
 * pointer.
 *
 * It will return size bytes unless there was an error, in which case it will
 * return as many as it managed to read (assuming blocking fd's which
 * all current QEMUFile are)
 */
int qemu_peek_buffer(QEMUFile *f, uint8_t *buf, int size, size_t offset)
{
    int pending;
    int index;

    assert(!qemu_file_is_writable(f));
    assert(offset < IO_BUF_SIZE);
    assert(size <= IO_BUF_SIZE - offset);

    /* The 1st byte to read from */
    index = f->buf_index + offset;
    /* The number of available bytes starting at index */
    pending = f->buf_size - index;

    /*
     * qemu_fill_buffer might return just a few bytes, even when there isn't
     * an error, so loop collecting them until we get enough.
     */
    while (pending < size) {
        int received = qemu_fill_buffer(f);

        if (received <= 0) {
            break;
        }

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

/*
 * Read 'size' bytes of data from the file into buf.
 * 'size' can be larger than the internal buffer.
 *
 * It will return size bytes unless there was an error, in which case it will
 * return as many as it managed to read (assuming blocking fd's which
 * all current QEMUFile are)
 */
int qemu_get_buffer(QEMUFile *f, uint8_t *buf, int size)
{
    int pending = size;
    int done = 0;

    while (pending > 0) {
        int res;

        res = qemu_peek_buffer(f, buf, MIN(pending, IO_BUF_SIZE), 0);
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

/*
 * Peeks a single byte from the buffer; this isn't guaranteed to work if
 * offset leaves a gap after the previous read/peeked data.
 */
int qemu_peek_byte(QEMUFile *f, int offset)
{
    int index = f->buf_index + offset;

    assert(!qemu_file_is_writable(f));
    assert(offset < IO_BUF_SIZE);

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

#define QSB_CHUNK_SIZE      (1 << 10)
#define QSB_MAX_CHUNK_SIZE  (16 * QSB_CHUNK_SIZE)

/**
 * Create a QEMUSizedBuffer
 * This type of buffer uses scatter-gather lists internally and
 * can grow to any size. Any data array in the scatter-gather list
 * can hold different amount of bytes.
 *
 * @buffer: Optional buffer to copy into the QSB
 * @len: size of initial buffer; if @buffer is given, buffer must
 *       hold at least len bytes
 *
 * Returns a pointer to a QEMUSizedBuffer or NULL on allocation failure
 */
QEMUSizedBuffer *qsb_create(const uint8_t *buffer, size_t len)
{
    QEMUSizedBuffer *qsb;
    size_t alloc_len, num_chunks, i, to_copy;
    size_t chunk_size = (len > QSB_MAX_CHUNK_SIZE)
                        ? QSB_MAX_CHUNK_SIZE
                        : QSB_CHUNK_SIZE;

    num_chunks = DIV_ROUND_UP(len ? len : QSB_CHUNK_SIZE, chunk_size);
    alloc_len = num_chunks * chunk_size;

    qsb = g_try_new0(QEMUSizedBuffer, 1);
    if (!qsb) {
        return NULL;
    }

    qsb->iov = g_try_new0(struct iovec, num_chunks);
    if (!qsb->iov) {
        g_free(qsb);
        return NULL;
    }

    qsb->n_iov = num_chunks;

    for (i = 0; i < num_chunks; i++) {
        qsb->iov[i].iov_base = g_try_malloc0(chunk_size);
        if (!qsb->iov[i].iov_base) {
            /* qsb_free is safe since g_free can cope with NULL */
            qsb_free(qsb);
            return NULL;
        }

        qsb->iov[i].iov_len = chunk_size;
        if (buffer) {
            to_copy = (len - qsb->used) > chunk_size
                      ? chunk_size : (len - qsb->used);
            memcpy(qsb->iov[i].iov_base, &buffer[qsb->used], to_copy);
            qsb->used += to_copy;
        }
    }

    qsb->size = alloc_len;

    return qsb;
}

/**
 * Free the QEMUSizedBuffer
 *
 * @qsb: The QEMUSizedBuffer to free
 */
void qsb_free(QEMUSizedBuffer *qsb)
{
    size_t i;

    if (!qsb) {
        return;
    }

    for (i = 0; i < qsb->n_iov; i++) {
        g_free(qsb->iov[i].iov_base);
    }
    g_free(qsb->iov);
    g_free(qsb);
}

/**
 * Get the number of used bytes in the QEMUSizedBuffer
 *
 * @qsb: A QEMUSizedBuffer
 *
 * Returns the number of bytes currently used in this buffer
 */
size_t qsb_get_length(const QEMUSizedBuffer *qsb)
{
    return qsb->used;
}

/**
 * Set the length of the buffer; the primary usage of this
 * function is to truncate the number of used bytes in the buffer.
 * The size will not be extended beyond the current number of
 * allocated bytes in the QEMUSizedBuffer.
 *
 * @qsb: A QEMUSizedBuffer
 * @new_len: The new length of bytes in the buffer
 *
 * Returns the number of bytes the buffer was truncated or extended
 * to.
 */
size_t qsb_set_length(QEMUSizedBuffer *qsb, size_t new_len)
{
    if (new_len <= qsb->size) {
        qsb->used = new_len;
    } else {
        qsb->used = qsb->size;
    }
    return qsb->used;
}

/**
 * Get the iovec that holds the data for a given position @pos.
 *
 * @qsb: A QEMUSizedBuffer
 * @pos: The index of a byte in the buffer
 * @d_off: Pointer to an offset that this function will indicate
 *         at what position within the returned iovec the byte
 *         is to be found
 *
 * Returns the index of the iovec that holds the byte at the given
 * index @pos in the byte stream; a negative number if the iovec
 * for the given position @pos does not exist.
 */
static ssize_t qsb_get_iovec(const QEMUSizedBuffer *qsb,
                             off_t pos, off_t *d_off)
{
    ssize_t i;
    off_t curr = 0;

    if (pos > qsb->used) {
        return -1;
    }

    for (i = 0; i < qsb->n_iov; i++) {
        if (curr + qsb->iov[i].iov_len > pos) {
            *d_off = pos - curr;
            return i;
        }
        curr += qsb->iov[i].iov_len;
    }
    return -1;
}

/*
 * Convert the QEMUSizedBuffer into a flat buffer.
 *
 * Note: If at all possible, try to avoid this function since it
 *       may unnecessarily copy memory around.
 *
 * @qsb: pointer to QEMUSizedBuffer
 * @start: offset to start at
 * @count: number of bytes to copy
 * @buf: a pointer to a buffer to write into (at least @count bytes)
 *
 * Returns the number of bytes copied into the output buffer
 */
ssize_t qsb_get_buffer(const QEMUSizedBuffer *qsb, off_t start,
                       size_t count, uint8_t *buffer)
{
    const struct iovec *iov;
    size_t to_copy, all_copy;
    ssize_t index;
    off_t s_off;
    off_t d_off = 0;
    char *s;

    if (start > qsb->used) {
        return 0;
    }

    all_copy = qsb->used - start;
    if (all_copy > count) {
        all_copy = count;
    } else {
        count = all_copy;
    }

    index = qsb_get_iovec(qsb, start, &s_off);
    if (index < 0) {
        return 0;
    }

    while (all_copy > 0) {
        iov = &qsb->iov[index];

        s = iov->iov_base;

        to_copy = iov->iov_len - s_off;
        if (to_copy > all_copy) {
            to_copy = all_copy;
        }
        memcpy(&buffer[d_off], &s[s_off], to_copy);

        d_off += to_copy;
        all_copy -= to_copy;

        s_off = 0;
        index++;
    }

    return count;
}

/**
 * Grow the QEMUSizedBuffer to the given size and allocate
 * memory for it.
 *
 * @qsb: A QEMUSizedBuffer
 * @new_size: The new size of the buffer
 *
 * Return:
 *    a negative error code in case of memory allocation failure
 * or
 *    the new size of the buffer. The returned size may be greater or equal
 *    to @new_size.
 */
static ssize_t qsb_grow(QEMUSizedBuffer *qsb, size_t new_size)
{
    size_t needed_chunks, i;

    if (qsb->size < new_size) {
        struct iovec *new_iov;
        size_t size_diff = new_size - qsb->size;
        size_t chunk_size = (size_diff > QSB_MAX_CHUNK_SIZE)
                             ? QSB_MAX_CHUNK_SIZE : QSB_CHUNK_SIZE;

        needed_chunks = DIV_ROUND_UP(size_diff, chunk_size);

        new_iov = g_try_new(struct iovec, qsb->n_iov + needed_chunks);
        if (new_iov == NULL) {
            return -ENOMEM;
        }

        /* Allocate new chunks as needed into new_iov */
        for (i = qsb->n_iov; i < qsb->n_iov + needed_chunks; i++) {
            new_iov[i].iov_base = g_try_malloc0(chunk_size);
            new_iov[i].iov_len = chunk_size;
            if (!new_iov[i].iov_base) {
                size_t j;

                /* Free previously allocated new chunks */
                for (j = qsb->n_iov; j < i; j++) {
                    g_free(new_iov[j].iov_base);
                }
                g_free(new_iov);

                return -ENOMEM;
            }
        }

        /*
         * Now we can't get any allocation errors, copy over to new iov
         * and switch.
         */
        for (i = 0; i < qsb->n_iov; i++) {
            new_iov[i] = qsb->iov[i];
        }

        qsb->n_iov += needed_chunks;
        g_free(qsb->iov);
        qsb->iov = new_iov;
        qsb->size += (needed_chunks * chunk_size);
    }

    return qsb->size;
}

/**
 * Write into the QEMUSizedBuffer at a given position and a given
 * number of bytes. This function will automatically grow the
 * QEMUSizedBuffer.
 *
 * @qsb: A QEMUSizedBuffer
 * @source: A byte array to copy data from
 * @pos: The position within the @qsb to write data to
 * @size: The number of bytes to copy into the @qsb
 *
 * Returns @size or a negative error code in case of memory allocation failure,
 *           or with an invalid 'pos'
 */
ssize_t qsb_write_at(QEMUSizedBuffer *qsb, const uint8_t *source,
                     off_t pos, size_t count)
{
    ssize_t rc = qsb_grow(qsb, pos + count);
    size_t to_copy;
    size_t all_copy = count;
    const struct iovec *iov;
    ssize_t index;
    char *dest;
    off_t d_off, s_off = 0;

    if (rc < 0) {
        return rc;
    }

    if (pos + count > qsb->used) {
        qsb->used = pos + count;
    }

    index = qsb_get_iovec(qsb, pos, &d_off);
    if (index < 0) {
        return -EINVAL;
    }

    while (all_copy > 0) {
        iov = &qsb->iov[index];

        dest = iov->iov_base;

        to_copy = iov->iov_len - d_off;
        if (to_copy > all_copy) {
            to_copy = all_copy;
        }

        memcpy(&dest[d_off], &source[s_off], to_copy);

        s_off += to_copy;
        all_copy -= to_copy;

        d_off = 0;
        index++;
    }

    return count;
}

/**
 * Create a deep copy of the given QEMUSizedBuffer.
 *
 * @qsb: A QEMUSizedBuffer
 *
 * Returns a clone of @qsb or NULL on allocation failure
 */
QEMUSizedBuffer *qsb_clone(const QEMUSizedBuffer *qsb)
{
    QEMUSizedBuffer *out = qsb_create(NULL, qsb_get_length(qsb));
    size_t i;
    ssize_t res;
    off_t pos = 0;

    if (!out) {
        return NULL;
    }

    for (i = 0; i < qsb->n_iov; i++) {
        res =  qsb_write_at(out, qsb->iov[i].iov_base,
                            pos, qsb->iov[i].iov_len);
        if (res < 0) {
            qsb_free(out);
            return NULL;
        }
        pos += res;
    }

    return out;
}

typedef struct QEMUBuffer {
    QEMUSizedBuffer *qsb;
    QEMUFile *file;
} QEMUBuffer;

static int buf_get_buffer(void *opaque, uint8_t *buf, int64_t pos, int size)
{
    QEMUBuffer *s = opaque;
    ssize_t len = qsb_get_length(s->qsb) - pos;

    if (len <= 0) {
        return 0;
    }

    if (len > size) {
        len = size;
    }
    return qsb_get_buffer(s->qsb, pos, len, buf);
}

static int buf_put_buffer(void *opaque, const uint8_t *buf,
                          int64_t pos, int size)
{
    QEMUBuffer *s = opaque;

    return qsb_write_at(s->qsb, buf, pos, size);
}

static int buf_close(void *opaque)
{
    QEMUBuffer *s = opaque;

    qsb_free(s->qsb);

    g_free(s);

    return 0;
}

const QEMUSizedBuffer *qemu_buf_get(QEMUFile *f)
{
    QEMUBuffer *p;

    qemu_fflush(f);

    p = f->opaque;

    return p->qsb;
}

static const QEMUFileOps buf_read_ops = {
    .get_buffer = buf_get_buffer,
    .close =      buf_close,
};

static const QEMUFileOps buf_write_ops = {
    .put_buffer = buf_put_buffer,
    .close =      buf_close,
};

QEMUFile *qemu_bufopen(const char *mode, QEMUSizedBuffer *input)
{
    QEMUBuffer *s;

    if (mode == NULL || (mode[0] != 'r' && mode[0] != 'w') ||
        mode[1] != '\0') {
        error_report("qemu_bufopen: Argument validity check failed");
        return NULL;
    }

    s = g_malloc0(sizeof(QEMUBuffer));
    if (mode[0] == 'r') {
        s->qsb = input;
    }

    if (s->qsb == NULL) {
        s->qsb = qsb_create(NULL, 0);
    }
    if (!s->qsb) {
        g_free(s);
        error_report("qemu_bufopen: qsb_create failed");
        return NULL;
    }


    if (mode[0] == 'r') {
        s->file = qemu_fopen_ops(s, &buf_read_ops);
    } else {
        s->file = qemu_fopen_ops(s, &buf_write_ops);
    }
    return s->file;
}
