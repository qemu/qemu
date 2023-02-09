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
#include <zlib.h>
#include "qemu/madvise.h"
#include "qemu/error-report.h"
#include "qemu/iov.h"
#include "migration.h"
#include "qemu-file.h"
#include "trace.h"
#include "qapi/error.h"

#define IO_BUF_SIZE 32768
#define MAX_IOV_SIZE MIN_CONST(IOV_MAX, 64)

struct QEMUFile {
    const QEMUFileHooks *hooks;
    QIOChannel *ioc;
    bool is_writable;

    /*
     * Maximum amount of data in bytes to transfer during one
     * rate limiting time window
     */
    int64_t rate_limit_max;
    /*
     * Total amount of data in bytes queued for transfer
     * during this rate limiting time window
     */
    int64_t rate_limit_used;

    /* The sum of bytes transferred on the wire */
    int64_t total_transferred;

    int buf_index;
    int buf_size; /* 0 when writing */
    uint8_t buf[IO_BUF_SIZE];

    DECLARE_BITMAP(may_free, MAX_IOV_SIZE);
    struct iovec iov[MAX_IOV_SIZE];
    unsigned int iovcnt;

    int last_error;
    Error *last_error_obj;
    /* has the file has been shutdown */
    bool shutdown;
};

/*
 * Stop a file from being read/written - not all backing files can do this
 * typically only sockets can.
 *
 * TODO: convert to propagate Error objects instead of squashing
 * to a fixed errno value
 */
int qemu_file_shutdown(QEMUFile *f)
{
    int ret = 0;

    f->shutdown = true;

    /*
     * We must set qemufile error before the real shutdown(), otherwise
     * there can be a race window where we thought IO all went though
     * (because last_error==NULL) but actually IO has already stopped.
     *
     * If without correct ordering, the race can happen like this:
     *
     *      page receiver                     other thread
     *      -------------                     ------------
     *      qemu_get_buffer()
     *                                        do shutdown()
     *        returns 0 (buffer all zero)
     *        (we didn't check this retcode)
     *      try to detect IO error
     *        last_error==NULL, IO okay
     *      install ALL-ZERO page
     *                                        set last_error
     *      --> guest crash!
     */
    if (!f->last_error) {
        qemu_file_set_error(f, -EIO);
    }

    if (!qio_channel_has_feature(f->ioc,
                                 QIO_CHANNEL_FEATURE_SHUTDOWN)) {
        return -ENOSYS;
    }

    if (qio_channel_shutdown(f->ioc, QIO_CHANNEL_SHUTDOWN_BOTH, NULL) < 0) {
        ret = -EIO;
    }

    return ret;
}

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

static QEMUFile *qemu_file_new_impl(QIOChannel *ioc, bool is_writable)
{
    QEMUFile *f;

    f = g_new0(QEMUFile, 1);

    object_ref(ioc);
    f->ioc = ioc;
    f->is_writable = is_writable;

    return f;
}

/*
 * Result: QEMUFile* for a 'return path' for comms in the opposite direction
 *         NULL if not available
 */
QEMUFile *qemu_file_get_return_path(QEMUFile *f)
{
    return qemu_file_new_impl(f->ioc, !f->is_writable);
}

QEMUFile *qemu_file_new_output(QIOChannel *ioc)
{
    return qemu_file_new_impl(ioc, true);
}

QEMUFile *qemu_file_new_input(QIOChannel *ioc)
{
    return qemu_file_new_impl(ioc, false);
}

void qemu_file_set_hooks(QEMUFile *f, const QEMUFileHooks *hooks)
{
    f->hooks = hooks;
}

/*
 * Get last error for stream f with optional Error*
 *
 * Return negative error value if there has been an error on previous
 * operations, return 0 if no error happened.
 * Optional, it returns Error* in errp, but it may be NULL even if return value
 * is not 0.
 *
 */
int qemu_file_get_error_obj(QEMUFile *f, Error **errp)
{
    if (errp) {
        *errp = f->last_error_obj ? error_copy(f->last_error_obj) : NULL;
    }
    return f->last_error;
}

/*
 * Get last error for either stream f1 or f2 with optional Error*.
 * The error returned (non-zero) can be either from f1 or f2.
 *
 * If any of the qemufile* is NULL, then skip the check on that file.
 *
 * When there is no error on both qemufile, zero is returned.
 */
int qemu_file_get_error_obj_any(QEMUFile *f1, QEMUFile *f2, Error **errp)
{
    int ret = 0;

    if (f1) {
        ret = qemu_file_get_error_obj(f1, errp);
        /* If there's already error detected, return */
        if (ret) {
            return ret;
        }
    }

    if (f2) {
        ret = qemu_file_get_error_obj(f2, errp);
    }

    return ret;
}

/*
 * Set the last error for stream f with optional Error*
 */
void qemu_file_set_error_obj(QEMUFile *f, int ret, Error *err)
{
    if (f->last_error == 0 && ret) {
        f->last_error = ret;
        error_propagate(&f->last_error_obj, err);
    } else if (err) {
        error_report_err(err);
    }
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
    return qemu_file_get_error_obj(f, NULL);
}

/*
 * Set the last error for stream f
 */
void qemu_file_set_error(QEMUFile *f, int ret)
{
    qemu_file_set_error_obj(f, ret, NULL);
}

bool qemu_file_is_writable(QEMUFile *f)
{
    return f->is_writable;
}

static void qemu_iovec_release_ram(QEMUFile *f)
{
    struct iovec iov;
    unsigned long idx;

    /* Find and release all the contiguous memory ranges marked as may_free. */
    idx = find_next_bit(f->may_free, f->iovcnt, 0);
    if (idx >= f->iovcnt) {
        return;
    }
    iov = f->iov[idx];

    /* The madvise() in the loop is called for iov within a continuous range and
     * then reinitialize the iov. And in the end, madvise() is called for the
     * last iov.
     */
    while ((idx = find_next_bit(f->may_free, f->iovcnt, idx + 1)) < f->iovcnt) {
        /* check for adjacent buffer and coalesce them */
        if (iov.iov_base + iov.iov_len == f->iov[idx].iov_base) {
            iov.iov_len += f->iov[idx].iov_len;
            continue;
        }
        if (qemu_madvise(iov.iov_base, iov.iov_len, QEMU_MADV_DONTNEED) < 0) {
            error_report("migrate: madvise DONTNEED failed %p %zd: %s",
                         iov.iov_base, iov.iov_len, strerror(errno));
        }
        iov = f->iov[idx];
    }
    if (qemu_madvise(iov.iov_base, iov.iov_len, QEMU_MADV_DONTNEED) < 0) {
            error_report("migrate: madvise DONTNEED failed %p %zd: %s",
                         iov.iov_base, iov.iov_len, strerror(errno));
    }
    memset(f->may_free, 0, sizeof(f->may_free));
}


/**
 * Flushes QEMUFile buffer
 *
 * This will flush all pending data. If data was only partially flushed, it
 * will set an error state.
 */
void qemu_fflush(QEMUFile *f)
{
    if (!qemu_file_is_writable(f)) {
        return;
    }

    if (f->shutdown) {
        return;
    }
    if (f->iovcnt > 0) {
        Error *local_error = NULL;
        if (qio_channel_writev_all(f->ioc,
                                   f->iov, f->iovcnt,
                                   &local_error) < 0) {
            qemu_file_set_error_obj(f, -EIO, local_error);
        } else {
            f->total_transferred += iov_size(f->iov, f->iovcnt);
        }

        qemu_iovec_release_ram(f);
    }

    f->buf_index = 0;
    f->iovcnt = 0;
}

void ram_control_before_iterate(QEMUFile *f, uint64_t flags)
{
    int ret = 0;

    if (f->hooks && f->hooks->before_ram_iterate) {
        ret = f->hooks->before_ram_iterate(f, flags, NULL);
        if (ret < 0) {
            qemu_file_set_error(f, ret);
        }
    }
}

void ram_control_after_iterate(QEMUFile *f, uint64_t flags)
{
    int ret = 0;

    if (f->hooks && f->hooks->after_ram_iterate) {
        ret = f->hooks->after_ram_iterate(f, flags, NULL);
        if (ret < 0) {
            qemu_file_set_error(f, ret);
        }
    }
}

void ram_control_load_hook(QEMUFile *f, uint64_t flags, void *data)
{
    int ret = -EINVAL;

    if (f->hooks && f->hooks->hook_ram_load) {
        ret = f->hooks->hook_ram_load(f, flags, data);
        if (ret < 0) {
            qemu_file_set_error(f, ret);
        }
    } else {
        /*
         * Hook is a hook specifically requested by the source sending a flag
         * that expects there to be a hook on the destination.
         */
        if (flags == RAM_CONTROL_HOOK) {
            qemu_file_set_error(f, ret);
        }
    }
}

size_t ram_control_save_page(QEMUFile *f, ram_addr_t block_offset,
                             ram_addr_t offset, size_t size,
                             uint64_t *bytes_sent)
{
    if (f->hooks && f->hooks->save_page) {
        int ret = f->hooks->save_page(f, block_offset,
                                      offset, size, bytes_sent);
        if (ret != RAM_SAVE_CONTROL_NOT_SUPP) {
            f->rate_limit_used += size;
        }

        if (ret != RAM_SAVE_CONTROL_DELAYED &&
            ret != RAM_SAVE_CONTROL_NOT_SUPP) {
            if (bytes_sent && *bytes_sent > 0) {
                qemu_file_credit_transfer(f, *bytes_sent);
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
    Error *local_error = NULL;

    assert(!qemu_file_is_writable(f));

    pending = f->buf_size - f->buf_index;
    if (pending > 0) {
        memmove(f->buf, f->buf + f->buf_index, pending);
    }
    f->buf_index = 0;
    f->buf_size = pending;

    if (f->shutdown) {
        return 0;
    }

    do {
        len = qio_channel_read(f->ioc,
                               (char *)f->buf + pending,
                               IO_BUF_SIZE - pending,
                               &local_error);
        if (len == QIO_CHANNEL_ERR_BLOCK) {
            if (qemu_in_coroutine()) {
                qio_channel_yield(f->ioc, G_IO_IN);
            } else {
                qio_channel_wait(f->ioc, G_IO_IN);
            }
        } else if (len < 0) {
            len = -EIO;
        }
    } while (len == QIO_CHANNEL_ERR_BLOCK);

    if (len > 0) {
        f->buf_size += len;
        f->total_transferred += len;
    } else if (len == 0) {
        qemu_file_set_error_obj(f, -EIO, local_error);
    } else {
        qemu_file_set_error_obj(f, len, local_error);
    }

    return len;
}

void qemu_file_credit_transfer(QEMUFile *f, size_t size)
{
    f->total_transferred += size;
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
    int ret, ret2;
    qemu_fflush(f);
    ret = qemu_file_get_error(f);

    ret2 = qio_channel_close(f->ioc, NULL);
    if (ret >= 0) {
        ret = ret2;
    }
    g_clear_pointer(&f->ioc, object_unref);

    /* If any error was spotted before closing, we should report it
     * instead of the close() return value.
     */
    if (f->last_error) {
        ret = f->last_error;
    }
    error_free(f->last_error_obj);
    g_free(f);
    trace_qemu_file_fclose();
    return ret;
}

/*
 * Add buf to iovec. Do flush if iovec is full.
 *
 * Return values:
 * 1 iovec is full and flushed
 * 0 iovec is not flushed
 *
 */
static int add_to_iovec(QEMUFile *f, const uint8_t *buf, size_t size,
                        bool may_free)
{
    /* check for adjacent buffer and coalesce them */
    if (f->iovcnt > 0 && buf == f->iov[f->iovcnt - 1].iov_base +
        f->iov[f->iovcnt - 1].iov_len &&
        may_free == test_bit(f->iovcnt - 1, f->may_free))
    {
        f->iov[f->iovcnt - 1].iov_len += size;
    } else {
        if (f->iovcnt >= MAX_IOV_SIZE) {
            /* Should only happen if a previous fflush failed */
            assert(f->shutdown || !qemu_file_is_writable(f));
            return 1;
        }
        if (may_free) {
            set_bit(f->iovcnt, f->may_free);
        }
        f->iov[f->iovcnt].iov_base = (uint8_t *)buf;
        f->iov[f->iovcnt++].iov_len = size;
    }

    if (f->iovcnt >= MAX_IOV_SIZE) {
        qemu_fflush(f);
        return 1;
    }

    return 0;
}

static void add_buf_to_iovec(QEMUFile *f, size_t len)
{
    if (!add_to_iovec(f, f->buf + f->buf_index, len, false)) {
        f->buf_index += len;
        if (f->buf_index == IO_BUF_SIZE) {
            qemu_fflush(f);
        }
    }
}

void qemu_put_buffer_async(QEMUFile *f, const uint8_t *buf, size_t size,
                           bool may_free)
{
    if (f->last_error) {
        return;
    }

    f->rate_limit_used += size;
    add_to_iovec(f, buf, size, may_free);
}

void qemu_put_buffer(QEMUFile *f, const uint8_t *buf, size_t size)
{
    size_t l;

    if (f->last_error) {
        return;
    }

    while (size > 0) {
        l = IO_BUF_SIZE - f->buf_index;
        if (l > size) {
            l = size;
        }
        memcpy(f->buf + f->buf_index, buf, l);
        f->rate_limit_used += l;
        add_buf_to_iovec(f, l);
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
    f->rate_limit_used++;
    add_buf_to_iovec(f, 1);
}

void qemu_file_skip(QEMUFile *f, int size)
{
    if (f->buf_index + size <= f->buf_size) {
        f->buf_index += size;
    }
}

/*
 * Read 'size' bytes from file (at 'offset') without moving the
 * pointer and set 'buf' to point to that data.
 *
 * It will return size bytes unless there was an error, in which case it will
 * return as many as it managed to read (assuming blocking fd's which
 * all current QEMUFile are)
 */
size_t qemu_peek_buffer(QEMUFile *f, uint8_t **buf, size_t size, size_t offset)
{
    ssize_t pending;
    size_t index;

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

    *buf = f->buf + index;
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
size_t qemu_get_buffer(QEMUFile *f, uint8_t *buf, size_t size)
{
    size_t pending = size;
    size_t done = 0;

    while (pending > 0) {
        size_t res;
        uint8_t *src;

        res = qemu_peek_buffer(f, &src, MIN(pending, IO_BUF_SIZE), 0);
        if (res == 0) {
            return done;
        }
        memcpy(buf, src, res);
        qemu_file_skip(f, res);
        buf += res;
        pending -= res;
        done += res;
    }
    return done;
}

/*
 * Read 'size' bytes of data from the file.
 * 'size' can be larger than the internal buffer.
 *
 * The data:
 *   may be held on an internal buffer (in which case *buf is updated
 *     to point to it) that is valid until the next qemu_file operation.
 * OR
 *   will be copied to the *buf that was passed in.
 *
 * The code tries to avoid the copy if possible.
 *
 * It will return size bytes unless there was an error, in which case it will
 * return as many as it managed to read (assuming blocking fd's which
 * all current QEMUFile are)
 *
 * Note: Since **buf may get changed, the caller should take care to
 *       keep a pointer to the original buffer if it needs to deallocate it.
 */
size_t qemu_get_buffer_in_place(QEMUFile *f, uint8_t **buf, size_t size)
{
    if (size < IO_BUF_SIZE) {
        size_t res;
        uint8_t *src = NULL;

        res = qemu_peek_buffer(f, &src, size, 0);

        if (res == size) {
            qemu_file_skip(f, res);
            *buf = src;
            return res;
        }
    }

    return qemu_get_buffer(f, *buf, size);
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

int64_t qemu_file_total_transferred_fast(QEMUFile *f)
{
    int64_t ret = f->total_transferred;
    int i;

    for (i = 0; i < f->iovcnt; i++) {
        ret += f->iov[i].iov_len;
    }

    return ret;
}

int64_t qemu_file_total_transferred(QEMUFile *f)
{
    qemu_fflush(f);
    return f->total_transferred;
}

int qemu_file_rate_limit(QEMUFile *f)
{
    if (f->shutdown) {
        return 1;
    }
    if (qemu_file_get_error(f)) {
        return 1;
    }
    if (f->rate_limit_max > 0 && f->rate_limit_used > f->rate_limit_max) {
        return 1;
    }
    return 0;
}

int64_t qemu_file_get_rate_limit(QEMUFile *f)
{
    return f->rate_limit_max;
}

void qemu_file_set_rate_limit(QEMUFile *f, int64_t limit)
{
    f->rate_limit_max = limit;
}

void qemu_file_reset_rate_limit(QEMUFile *f)
{
    f->rate_limit_used = 0;
}

void qemu_file_acct_rate_limit(QEMUFile *f, int64_t len)
{
    f->rate_limit_used += len;
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
    v = (unsigned int)qemu_get_byte(f) << 24;
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

/* return the size after compression, or negative value on error */
static int qemu_compress_data(z_stream *stream, uint8_t *dest, size_t dest_len,
                              const uint8_t *source, size_t source_len)
{
    int err;

    err = deflateReset(stream);
    if (err != Z_OK) {
        return -1;
    }

    stream->avail_in = source_len;
    stream->next_in = (uint8_t *)source;
    stream->avail_out = dest_len;
    stream->next_out = dest;

    err = deflate(stream, Z_FINISH);
    if (err != Z_STREAM_END) {
        return -1;
    }

    return stream->next_out - dest;
}

/* Compress size bytes of data start at p and store the compressed
 * data to the buffer of f.
 *
 * Since the file is dummy file with empty_ops, return -1 if f has no space to
 * save the compressed data.
 */
ssize_t qemu_put_compression_data(QEMUFile *f, z_stream *stream,
                                  const uint8_t *p, size_t size)
{
    ssize_t blen = IO_BUF_SIZE - f->buf_index - sizeof(int32_t);

    if (blen < compressBound(size)) {
        return -1;
    }

    blen = qemu_compress_data(stream, f->buf + f->buf_index + sizeof(int32_t),
                              blen, p, size);
    if (blen < 0) {
        return -1;
    }

    qemu_put_be32(f, blen);
    add_buf_to_iovec(f, blen);
    return blen + sizeof(int32_t);
}

/* Put the data in the buffer of f_src to the buffer of f_des, and
 * then reset the buf_index of f_src to 0.
 */

int qemu_put_qemu_file(QEMUFile *f_des, QEMUFile *f_src)
{
    int len = 0;

    if (f_src->buf_index > 0) {
        len = f_src->buf_index;
        qemu_put_buffer(f_des, f_src->buf, f_src->buf_index);
        f_src->buf_index = 0;
        f_src->iovcnt = 0;
    }
    return len;
}

/*
 * Get a string whose length is determined by a single preceding byte
 * A preallocated 256 byte buffer must be passed in.
 * Returns: len on success and a 0 terminated string in the buffer
 *          else 0
 *          (Note a 0 length string will return 0 either way)
 */
size_t qemu_get_counted_string(QEMUFile *f, char buf[256])
{
    size_t len = qemu_get_byte(f);
    size_t res = qemu_get_buffer(f, (uint8_t *)buf, len);

    buf[res] = 0;

    return res == len ? res : 0;
}

/*
 * Put a string with one preceding byte containing its length. The length of
 * the string should be less than 256.
 */
void qemu_put_counted_string(QEMUFile *f, const char *str)
{
    size_t len = strlen(str);

    assert(len < 256);
    qemu_put_byte(f, len);
    qemu_put_buffer(f, (const uint8_t *)str, len);
}

/*
 * Set the blocking state of the QEMUFile.
 * Note: On some transports the OS only keeps a single blocking state for
 *       both directions, and thus changing the blocking on the main
 *       QEMUFile can also affect the return path.
 */
void qemu_file_set_blocking(QEMUFile *f, bool block)
{
    qio_channel_set_blocking(f->ioc, block, NULL);
}

/*
 * qemu_file_get_ioc:
 *
 * Get the ioc object for the file, without incrementing
 * the reference count.
 *
 * Returns: the ioc object
 */
QIOChannel *qemu_file_get_ioc(QEMUFile *file)
{
    return file->ioc;
}

/*
 * Read size bytes from QEMUFile f and write them to fd.
 */
int qemu_file_get_to_fd(QEMUFile *f, int fd, size_t size)
{
    while (size) {
        size_t pending = f->buf_size - f->buf_index;
        ssize_t rc;

        if (!pending) {
            rc = qemu_fill_buffer(f);
            if (rc < 0) {
                return rc;
            }
            if (rc == 0) {
                return -EIO;
            }
            continue;
        }

        rc = write(fd, f->buf + f->buf_index, MIN(pending, size));
        if (rc < 0) {
            return -errno;
        }
        if (rc == 0) {
            return -EIO;
        }
        f->buf_index += rc;
        size -= rc;
    }

    return 0;
}
