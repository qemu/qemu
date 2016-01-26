/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (c) 2014 IBM Corp.
 *
 * Authors:
 *  Stefan Berger <stefanb@linux.vnet.ibm.com>
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
#include "migration/migration.h"
#include "migration/qemu-file.h"
#include "migration/qemu-file-internal.h"
#include "trace.h"

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

typedef struct QEMUBuffer {
    QEMUSizedBuffer *qsb;
    QEMUFile *file;
    bool qsb_allocated;
} QEMUBuffer;

static ssize_t buf_get_buffer(void *opaque, uint8_t *buf, int64_t pos,
                              size_t size)
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

static ssize_t buf_put_buffer(void *opaque, const uint8_t *buf,
                              int64_t pos, size_t size)
{
    QEMUBuffer *s = opaque;

    return qsb_write_at(s->qsb, buf, pos, size);
}

static int buf_close(void *opaque)
{
    QEMUBuffer *s = opaque;

    if (s->qsb_allocated) {
        qsb_free(s->qsb);
    }

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

    s = g_new0(QEMUBuffer, 1);
    s->qsb = input;

    if (s->qsb == NULL) {
        s->qsb = qsb_create(NULL, 0);
        s->qsb_allocated = true;
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
