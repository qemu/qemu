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
#include "qemu/madvise.h"
#include "qemu/error-report.h"
#include "qemu/iov.h"
#include "migration.h"
#include "migration-stats.h"
#include "qemu-file.h"
#include "trace.h"
#include "options.h"
#include "qapi/error.h"
#include "rdma.h"
#include "io/channel-file.h"

#define IO_BUF_SIZE 32768
#define MAX_IOV_SIZE MIN_CONST(IOV_MAX, 64)

typedef struct FdEntry {
    QTAILQ_ENTRY(FdEntry) entry;
    int fd;
} FdEntry;

struct QEMUFile {
    QIOChannel *ioc;
    bool is_writable;

    int buf_index;
    int buf_size; /* 0 when writing */
    uint8_t buf[IO_BUF_SIZE];

    DECLARE_BITMAP(may_free, MAX_IOV_SIZE);
    struct iovec iov[MAX_IOV_SIZE];
    unsigned int iovcnt;

    int last_error;
    Error *last_error_obj;

    bool can_pass_fd;
    QTAILQ_HEAD(, FdEntry) fds;
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
    Error *err = NULL;

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

    if (qio_channel_shutdown(f->ioc, QIO_CHANNEL_SHUTDOWN_BOTH, &err) < 0) {
        error_report_err(err);
        return -EIO;
    }

    return 0;
}

static QEMUFile *qemu_file_new_impl(QIOChannel *ioc, bool is_writable)
{
    QEMUFile *f;

    f = g_new0(QEMUFile, 1);

    object_ref(ioc);
    f->ioc = ioc;
    f->is_writable = is_writable;
    f->can_pass_fd = qio_channel_has_feature(ioc, QIO_CHANNEL_FEATURE_FD_PASS);
    QTAILQ_INIT(&f->fds);

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

/*
 * Get last error for stream f with optional Error*
 *
 * Return negative error value if there has been an error on previous
 * operations, return 0 if no error happened.
 *
 * If errp is specified, a verbose error message will be copied over.
 */
int qemu_file_get_error_obj(QEMUFile *f, Error **errp)
{
    if (!f->last_error) {
        return 0;
    }

    /* There is an error */
    if (errp) {
        if (f->last_error_obj) {
            *errp = error_copy(f->last_error_obj);
        } else {
            error_setg_errno(errp, -f->last_error, "Channel error");
        }
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
    return f->last_error;
}

/*
 * Set the last error for stream f
 */
void qemu_file_set_error(QEMUFile *f, int ret)
{
    qemu_file_set_error_obj(f, ret, NULL);
}

static bool qemu_file_is_writable(QEMUFile *f)
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

bool qemu_file_is_seekable(QEMUFile *f)
{
    return qio_channel_has_feature(f->ioc, QIO_CHANNEL_FEATURE_SEEKABLE);
}

/**
 * Flushes QEMUFile buffer
 *
 * This will flush all pending data. If data was only partially flushed, it
 * will set an error state.
 */
int qemu_fflush(QEMUFile *f)
{
    if (!qemu_file_is_writable(f)) {
        return f->last_error;
    }

    if (f->last_error) {
        return f->last_error;
    }
    if (f->iovcnt > 0) {
        Error *local_error = NULL;
        if (qio_channel_writev_all(f->ioc,
                                   f->iov, f->iovcnt,
                                   &local_error) < 0) {
            qemu_file_set_error_obj(f, -EIO, local_error);
        } else {
            uint64_t size = iov_size(f->iov, f->iovcnt);
            stat64_add(&mig_stats.qemu_file_transferred, size);
        }

        qemu_iovec_release_ram(f);
    }

    f->buf_index = 0;
    f->iovcnt = 0;
    return f->last_error;
}

/*
 * Attempt to fill the buffer from the underlying file
 * Returns the number of bytes read, or negative value for an error.
 *
 * Note that it can return a partially full buffer even in a not error/not EOF
 * case if the underlying file descriptor gives a short read, and that can
 * happen even on a blocking fd.
 */
static ssize_t coroutine_mixed_fn qemu_fill_buffer(QEMUFile *f)
{
    int len;
    int pending;
    Error *local_error = NULL;
    g_autofree int *fds = NULL;
    size_t nfd = 0;
    int **pfds = f->can_pass_fd ? &fds : NULL;
    size_t *pnfd = f->can_pass_fd ? &nfd : NULL;

    assert(!qemu_file_is_writable(f));

    pending = f->buf_size - f->buf_index;
    if (pending > 0) {
        memmove(f->buf, f->buf + f->buf_index, pending);
    }
    f->buf_index = 0;
    f->buf_size = pending;

    if (qemu_file_get_error(f)) {
        return 0;
    }

    do {
        struct iovec iov = { f->buf + pending, IO_BUF_SIZE - pending };
        len = qio_channel_readv_full(f->ioc, &iov, 1, pfds, pnfd, 0,
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
    } else if (len == 0) {
        qemu_file_set_error_obj(f, -EIO, local_error);
    } else {
        qemu_file_set_error_obj(f, len, local_error);
    }

    for (int i = 0; i < nfd; i++) {
        FdEntry *fde = g_new0(FdEntry, 1);
        fde->fd = fds[i];
        QTAILQ_INSERT_TAIL(&f->fds, fde, entry);
    }

    return len;
}

int qemu_file_put_fd(QEMUFile *f, int fd)
{
    int ret = 0;
    QIOChannel *ioc = qemu_file_get_ioc(f);
    Error *err = NULL;
    struct iovec iov = { (void *)" ", 1 };

    /*
     * Send a dummy byte so qemu_fill_buffer on the receiving side does not
     * fail with a len=0 error.  Flush first to maintain ordering wrt other
     * data.
     */

    qemu_fflush(f);
    if (qio_channel_writev_full(ioc, &iov, 1, &fd, 1, 0, &err) < 1) {
        error_report_err(error_copy(err));
        qemu_file_set_error_obj(f, -EIO, err);
        ret = -1;
    }
    trace_qemu_file_put_fd(f->ioc->name, fd, ret);
    return ret;
}

int qemu_file_get_fd(QEMUFile *f)
{
    int fd = -1;
    FdEntry *fde;

    if (!f->can_pass_fd) {
        Error *err = NULL;
        error_setg(&err, "%s does not support fd passing", f->ioc->name);
        error_report_err(error_copy(err));
        qemu_file_set_error_obj(f, -EIO, err);
        goto out;
    }

    /* Force the dummy byte and its fd passenger to appear. */
    qemu_peek_byte(f, 0);

    fde = QTAILQ_FIRST(&f->fds);
    if (fde) {
        qemu_get_byte(f);       /* Drop the dummy byte */
        fd = fde->fd;
        QTAILQ_REMOVE(&f->fds, fde, entry);
        g_free(fde);
    }
out:
    trace_qemu_file_get_fd(f->ioc->name, fd);
    return fd;
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
    FdEntry *fde, *next;
    int ret = qemu_fflush(f);
    int ret2 = qio_channel_close(f->ioc, NULL);
    if (ret >= 0) {
        ret = ret2;
    }
    QTAILQ_FOREACH_SAFE(fde, &f->fds, entry, next) {
        warn_report("qemu_fclose: received fd %d was never claimed", fde->fd);
        close(fde->fd);
        g_free(fde);
    }
    g_clear_pointer(&f->ioc, object_unref);
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
            assert(qemu_file_get_error(f) || !qemu_file_is_writable(f));
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
        add_buf_to_iovec(f, l);
        if (qemu_file_get_error(f)) {
            break;
        }
        buf += l;
        size -= l;
    }
}

void qemu_put_buffer_at(QEMUFile *f, const uint8_t *buf, size_t buflen,
                        off_t pos)
{
    Error *err = NULL;
    size_t ret;

    if (f->last_error) {
        return;
    }

    qemu_fflush(f);
    ret = qio_channel_pwrite(f->ioc, (char *)buf, buflen, pos, &err);

    if (err) {
        qemu_file_set_error_obj(f, -EIO, err);
        return;
    }

    if ((ssize_t)ret == QIO_CHANNEL_ERR_BLOCK) {
        qemu_file_set_error_obj(f, -EAGAIN, NULL);
        return;
    }

    if (ret != buflen) {
        error_setg(&err, "Partial write of size %zu, expected %zu", ret,
                   buflen);
        qemu_file_set_error_obj(f, -EIO, err);
        return;
    }

    stat64_add(&mig_stats.qemu_file_transferred, buflen);
}


size_t qemu_get_buffer_at(QEMUFile *f, const uint8_t *buf, size_t buflen,
                          off_t pos)
{
    Error *err = NULL;
    size_t ret;

    if (f->last_error) {
        return 0;
    }

    ret = qio_channel_pread(f->ioc, (char *)buf, buflen, pos, &err);

    if ((ssize_t)ret == -1 || err) {
        qemu_file_set_error_obj(f, -EIO, err);
        return 0;
    }

    if ((ssize_t)ret == QIO_CHANNEL_ERR_BLOCK) {
        qemu_file_set_error_obj(f, -EAGAIN, NULL);
        return 0;
    }

    if (ret != buflen) {
        error_setg(&err, "Partial read of size %zu, expected %zu", ret, buflen);
        qemu_file_set_error_obj(f, -EIO, err);
        return 0;
    }

    return ret;
}

void qemu_set_offset(QEMUFile *f, off_t off, int whence)
{
    Error *err = NULL;
    off_t ret;

    if (qemu_file_is_writable(f)) {
        qemu_fflush(f);
    } else {
        /* Drop all cached buffers if existed; will trigger a re-fill later */
        f->buf_index = 0;
        f->buf_size = 0;
    }

    ret = qio_channel_io_seek(f->ioc, off, whence, &err);
    if (ret == (off_t)-1) {
        qemu_file_set_error_obj(f, -EIO, err);
    }
}

off_t qemu_get_offset(QEMUFile *f)
{
    Error *err = NULL;
    off_t ret;

    qemu_fflush(f);

    ret = qio_channel_io_seek(f->ioc, 0, SEEK_CUR, &err);
    if (ret == (off_t)-1) {
        qemu_file_set_error_obj(f, -EIO, err);
    }
    return ret;
}


void qemu_put_byte(QEMUFile *f, int v)
{
    if (f->last_error) {
        return;
    }

    f->buf[f->buf_index] = v;
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
size_t coroutine_mixed_fn qemu_peek_buffer(QEMUFile *f, uint8_t **buf, size_t size, size_t offset)
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
size_t coroutine_mixed_fn qemu_get_buffer(QEMUFile *f, uint8_t *buf, size_t size)
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
size_t coroutine_mixed_fn qemu_get_buffer_in_place(QEMUFile *f, uint8_t **buf, size_t size)
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
int coroutine_mixed_fn qemu_peek_byte(QEMUFile *f, int offset)
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

int coroutine_mixed_fn qemu_get_byte(QEMUFile *f)
{
    int result;

    result = qemu_peek_byte(f, 0);
    qemu_file_skip(f, 1);
    return result;
}

uint64_t qemu_file_transferred(QEMUFile *f)
{
    uint64_t ret = stat64_get(&mig_stats.qemu_file_transferred);
    int i;

    g_assert(qemu_file_is_writable(f));

    for (i = 0; i < f->iovcnt; i++) {
        ret += f->iov[i].iov_len;
    }

    return ret;
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

/*
 * Get a string whose length is determined by a single preceding byte
 * A preallocated 256 byte buffer must be passed in.
 * Returns: len on success and a 0 terminated string in the buffer
 *          else 0
 *          (Note a 0 length string will return 0 either way)
 */
size_t coroutine_fn qemu_get_counted_string(QEMUFile *f, char buf[256])
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
