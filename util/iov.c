/*
 * Helpers for getting linearized buffers from iov / filling buffers into iovs
 *
 * Copyright IBM, Corp. 2007, 2008
 * Copyright (C) 2010 Red Hat, Inc.
 *
 * Author(s):
 *  Anthony Liguori <aliguori@us.ibm.com>
 *  Amit Shah <amit.shah@redhat.com>
 *  Michael Tokarev <mjt@tls.msk.ru>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/iov.h"
#include "qemu/sockets.h"
#include "qemu/cutils.h"

size_t iov_from_buf_full(const struct iovec *iov, unsigned int iov_cnt,
                         size_t offset, const void *buf, size_t bytes)
{
    size_t done;
    unsigned int i;
    for (i = 0, done = 0; (offset || done < bytes) && i < iov_cnt; i++) {
        if (offset < iov[i].iov_len) {
            size_t len = MIN(iov[i].iov_len - offset, bytes - done);
            memcpy(iov[i].iov_base + offset, buf + done, len);
            done += len;
            offset = 0;
        } else {
            offset -= iov[i].iov_len;
        }
    }
    assert(offset == 0);
    return done;
}

size_t iov_to_buf_full(const struct iovec *iov, const unsigned int iov_cnt,
                       size_t offset, void *buf, size_t bytes)
{
    size_t done;
    unsigned int i;
    for (i = 0, done = 0; (offset || done < bytes) && i < iov_cnt; i++) {
        if (offset < iov[i].iov_len) {
            size_t len = MIN(iov[i].iov_len - offset, bytes - done);
            memcpy(buf + done, iov[i].iov_base + offset, len);
            done += len;
            offset = 0;
        } else {
            offset -= iov[i].iov_len;
        }
    }
    assert(offset == 0);
    return done;
}

size_t iov_memset(const struct iovec *iov, const unsigned int iov_cnt,
                  size_t offset, int fillc, size_t bytes)
{
    size_t done;
    unsigned int i;
    for (i = 0, done = 0; (offset || done < bytes) && i < iov_cnt; i++) {
        if (offset < iov[i].iov_len) {
            size_t len = MIN(iov[i].iov_len - offset, bytes - done);
            memset(iov[i].iov_base + offset, fillc, len);
            done += len;
            offset = 0;
        } else {
            offset -= iov[i].iov_len;
        }
    }
    assert(offset == 0);
    return done;
}

size_t iov_size(const struct iovec *iov, const unsigned int iov_cnt)
{
    size_t len;
    unsigned int i;

    len = 0;
    for (i = 0; i < iov_cnt; i++) {
        len += iov[i].iov_len;
    }
    return len;
}

/* helper function for iov_send_recv() */
static ssize_t
do_send_recv(int sockfd, struct iovec *iov, unsigned iov_cnt, bool do_send)
{
#ifdef CONFIG_POSIX
    ssize_t ret;
    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = iov;
    msg.msg_iovlen = iov_cnt;
    do {
        ret = do_send
            ? sendmsg(sockfd, &msg, 0)
            : recvmsg(sockfd, &msg, 0);
    } while (ret < 0 && errno == EINTR);
    return ret;
#else
    /* else send piece-by-piece */
    /*XXX Note: windows has WSASend() and WSARecv() */
    unsigned i = 0;
    ssize_t ret = 0;
    while (i < iov_cnt) {
        ssize_t r = do_send
            ? send(sockfd, iov[i].iov_base, iov[i].iov_len, 0)
            : recv(sockfd, iov[i].iov_base, iov[i].iov_len, 0);
        if (r > 0) {
            ret += r;
        } else if (!r) {
            break;
        } else if (errno == EINTR) {
            continue;
        } else {
            /* else it is some "other" error,
             * only return if there was no data processed. */
            if (ret == 0) {
                ret = -1;
            }
            break;
        }
        i++;
    }
    return ret;
#endif
}

ssize_t iov_send_recv(int sockfd, const struct iovec *_iov, unsigned iov_cnt,
                      size_t offset, size_t bytes,
                      bool do_send)
{
    ssize_t total = 0;
    ssize_t ret;
    size_t orig_len, tail;
    unsigned niov;
    struct iovec *local_iov, *iov;

    if (bytes <= 0) {
        return 0;
    }

    local_iov = g_new0(struct iovec, iov_cnt);
    iov_copy(local_iov, iov_cnt, _iov, iov_cnt, offset, bytes);
    offset = 0;
    iov = local_iov;

    while (bytes > 0) {
        /* Find the start position, skipping `offset' bytes:
         * first, skip all full-sized vector elements, */
        for (niov = 0; niov < iov_cnt && offset >= iov[niov].iov_len; ++niov) {
            offset -= iov[niov].iov_len;
        }

        /* niov == iov_cnt would only be valid if bytes == 0, which
         * we already ruled out in the loop condition.  */
        assert(niov < iov_cnt);
        iov += niov;
        iov_cnt -= niov;

        if (offset) {
            /* second, skip `offset' bytes from the (now) first element,
             * undo it on exit */
            iov[0].iov_base += offset;
            iov[0].iov_len -= offset;
        }
        /* Find the end position skipping `bytes' bytes: */
        /* first, skip all full-sized elements */
        tail = bytes;
        for (niov = 0; niov < iov_cnt && iov[niov].iov_len <= tail; ++niov) {
            tail -= iov[niov].iov_len;
        }
        if (tail) {
            /* second, fixup the last element, and remember the original
             * length */
            assert(niov < iov_cnt);
            assert(iov[niov].iov_len > tail);
            orig_len = iov[niov].iov_len;
            iov[niov++].iov_len = tail;
            ret = do_send_recv(sockfd, iov, niov, do_send);
            /* Undo the changes above before checking for errors */
            iov[niov-1].iov_len = orig_len;
        } else {
            ret = do_send_recv(sockfd, iov, niov, do_send);
        }
        if (offset) {
            iov[0].iov_base -= offset;
            iov[0].iov_len += offset;
        }

        if (ret < 0) {
            assert(errno != EINTR);
            g_free(local_iov);
            if (errno == EAGAIN && total > 0) {
                return total;
            }
            return -1;
        }

        if (ret == 0 && !do_send) {
            /* recv returns 0 when the peer has performed an orderly
             * shutdown. */
            break;
        }

        /* Prepare for the next iteration */
        offset += ret;
        total += ret;
        bytes -= ret;
    }

    g_free(local_iov);
    return total;
}


void iov_hexdump(const struct iovec *iov, const unsigned int iov_cnt,
                 FILE *fp, const char *prefix, size_t limit)
{
    int v;
    size_t size = 0;
    char *buf;

    for (v = 0; v < iov_cnt; v++) {
        size += iov[v].iov_len;
    }
    size = size > limit ? limit : size;
    buf = g_malloc(size);
    iov_to_buf(iov, iov_cnt, 0, buf, size);
    qemu_hexdump(buf, fp, prefix, size);
    g_free(buf);
}

unsigned iov_copy(struct iovec *dst_iov, unsigned int dst_iov_cnt,
                 const struct iovec *iov, unsigned int iov_cnt,
                 size_t offset, size_t bytes)
{
    size_t len;
    unsigned int i, j;
    for (i = 0, j = 0;
         i < iov_cnt && j < dst_iov_cnt && (offset || bytes); i++) {
        if (offset >= iov[i].iov_len) {
            offset -= iov[i].iov_len;
            continue;
        }
        len = MIN(bytes, iov[i].iov_len - offset);

        dst_iov[j].iov_base = iov[i].iov_base + offset;
        dst_iov[j].iov_len = len;
        j++;
        bytes -= len;
        offset = 0;
    }
    assert(offset == 0);
    return j;
}

/* io vectors */

void qemu_iovec_init(QEMUIOVector *qiov, int alloc_hint)
{
    qiov->iov = g_new(struct iovec, alloc_hint);
    qiov->niov = 0;
    qiov->nalloc = alloc_hint;
    qiov->size = 0;
}

void qemu_iovec_init_external(QEMUIOVector *qiov, struct iovec *iov, int niov)
{
    int i;

    qiov->iov = iov;
    qiov->niov = niov;
    qiov->nalloc = -1;
    qiov->size = 0;
    for (i = 0; i < niov; i++)
        qiov->size += iov[i].iov_len;
}

void qemu_iovec_add(QEMUIOVector *qiov, void *base, size_t len)
{
    assert(qiov->nalloc != -1);

    if (qiov->niov == qiov->nalloc) {
        qiov->nalloc = 2 * qiov->nalloc + 1;
        qiov->iov = g_renew(struct iovec, qiov->iov, qiov->nalloc);
    }
    qiov->iov[qiov->niov].iov_base = base;
    qiov->iov[qiov->niov].iov_len = len;
    qiov->size += len;
    ++qiov->niov;
}

/*
 * Concatenates (partial) iovecs from src_iov to the end of dst.
 * It starts copying after skipping `soffset' bytes at the
 * beginning of src and adds individual vectors from src to
 * dst copies up to `sbytes' bytes total, or up to the end
 * of src_iov if it comes first.  This way, it is okay to specify
 * very large value for `sbytes' to indicate "up to the end
 * of src".
 * Only vector pointers are processed, not the actual data buffers.
 */
size_t qemu_iovec_concat_iov(QEMUIOVector *dst,
                             struct iovec *src_iov, unsigned int src_cnt,
                             size_t soffset, size_t sbytes)
{
    int i;
    size_t done;

    if (!sbytes) {
        return 0;
    }
    assert(dst->nalloc != -1);
    for (i = 0, done = 0; done < sbytes && i < src_cnt; i++) {
        if (soffset < src_iov[i].iov_len) {
            size_t len = MIN(src_iov[i].iov_len - soffset, sbytes - done);
            qemu_iovec_add(dst, src_iov[i].iov_base + soffset, len);
            done += len;
            soffset = 0;
        } else {
            soffset -= src_iov[i].iov_len;
        }
    }
    assert(soffset == 0); /* offset beyond end of src */

    return done;
}

/*
 * Concatenates (partial) iovecs from src to the end of dst.
 * It starts copying after skipping `soffset' bytes at the
 * beginning of src and adds individual vectors from src to
 * dst copies up to `sbytes' bytes total, or up to the end
 * of src if it comes first.  This way, it is okay to specify
 * very large value for `sbytes' to indicate "up to the end
 * of src".
 * Only vector pointers are processed, not the actual data buffers.
 */
void qemu_iovec_concat(QEMUIOVector *dst,
                       QEMUIOVector *src, size_t soffset, size_t sbytes)
{
    qemu_iovec_concat_iov(dst, src->iov, src->niov, soffset, sbytes);
}

/*
 * qiov_find_iov
 *
 * Return pointer to iovec structure, where byte at @offset in original vector
 * @iov exactly is.
 * Set @remaining_offset to be offset inside that iovec to the same byte.
 */
static struct iovec *iov_skip_offset(struct iovec *iov, size_t offset,
                                     size_t *remaining_offset)
{
    while (offset > 0 && offset >= iov->iov_len) {
        offset -= iov->iov_len;
        iov++;
    }
    *remaining_offset = offset;

    return iov;
}

/*
 * qiov_slice
 *
 * Find subarray of iovec's, containing requested range. @head would
 * be offset in first iov (returned by the function), @tail would be
 * count of extra bytes in last iovec (returned iov + @niov - 1).
 */
static struct iovec *qiov_slice(QEMUIOVector *qiov,
                                size_t offset, size_t len,
                                size_t *head, size_t *tail, int *niov)
{
    struct iovec *iov, *end_iov;

    assert(offset + len <= qiov->size);

    iov = iov_skip_offset(qiov->iov, offset, head);
    end_iov = iov_skip_offset(iov, *head + len, tail);

    if (*tail > 0) {
        assert(*tail < end_iov->iov_len);
        *tail = end_iov->iov_len - *tail;
        end_iov++;
    }

    *niov = end_iov - iov;

    return iov;
}

int qemu_iovec_subvec_niov(QEMUIOVector *qiov, size_t offset, size_t len)
{
    size_t head, tail;
    int niov;

    qiov_slice(qiov, offset, len, &head, &tail, &niov);

    return niov;
}

/*
 * Compile new iovec, combining @head_buf buffer, sub-qiov of @mid_qiov,
 * and @tail_buf buffer into new qiov.
 */
void qemu_iovec_init_extended(
        QEMUIOVector *qiov,
        void *head_buf, size_t head_len,
        QEMUIOVector *mid_qiov, size_t mid_offset, size_t mid_len,
        void *tail_buf, size_t tail_len)
{
    size_t mid_head, mid_tail;
    int total_niov, mid_niov = 0;
    struct iovec *p, *mid_iov = NULL;

    if (mid_len) {
        mid_iov = qiov_slice(mid_qiov, mid_offset, mid_len,
                             &mid_head, &mid_tail, &mid_niov);
    }

    total_niov = !!head_len + mid_niov + !!tail_len;
    if (total_niov == 1) {
        qemu_iovec_init_buf(qiov, NULL, 0);
        p = &qiov->local_iov;
    } else {
        qiov->niov = qiov->nalloc = total_niov;
        qiov->size = head_len + mid_len + tail_len;
        p = qiov->iov = g_new(struct iovec, qiov->niov);
    }

    if (head_len) {
        p->iov_base = head_buf;
        p->iov_len = head_len;
        p++;
    }

    assert(!mid_niov == !mid_len);
    if (mid_niov) {
        memcpy(p, mid_iov, mid_niov * sizeof(*p));
        p[0].iov_base = (uint8_t *)p[0].iov_base + mid_head;
        p[0].iov_len -= mid_head;
        p[mid_niov - 1].iov_len -= mid_tail;
        p += mid_niov;
    }

    if (tail_len) {
        p->iov_base = tail_buf;
        p->iov_len = tail_len;
    }
}

/*
 * Check if the contents of subrange of qiov data is all zeroes.
 */
bool qemu_iovec_is_zero(QEMUIOVector *qiov, size_t offset, size_t bytes)
{
    struct iovec *iov;
    size_t current_offset;

    assert(offset + bytes <= qiov->size);

    iov = iov_skip_offset(qiov->iov, offset, &current_offset);

    while (bytes) {
        uint8_t *base = (uint8_t *)iov->iov_base + current_offset;
        size_t len = MIN(iov->iov_len - current_offset, bytes);

        if (!buffer_is_zero(base, len)) {
            return false;
        }

        current_offset = 0;
        bytes -= len;
        iov++;
    }

    return true;
}

void qemu_iovec_init_slice(QEMUIOVector *qiov, QEMUIOVector *source,
                           size_t offset, size_t len)
{
    qemu_iovec_init_extended(qiov, NULL, 0, source, offset, len, NULL, 0);
}

void qemu_iovec_destroy(QEMUIOVector *qiov)
{
    if (qiov->nalloc != -1) {
        g_free(qiov->iov);
    }

    memset(qiov, 0, sizeof(*qiov));
}

void qemu_iovec_reset(QEMUIOVector *qiov)
{
    assert(qiov->nalloc != -1);

    qiov->niov = 0;
    qiov->size = 0;
}

size_t qemu_iovec_to_buf(QEMUIOVector *qiov, size_t offset,
                         void *buf, size_t bytes)
{
    return iov_to_buf(qiov->iov, qiov->niov, offset, buf, bytes);
}

size_t qemu_iovec_from_buf(QEMUIOVector *qiov, size_t offset,
                           const void *buf, size_t bytes)
{
    return iov_from_buf(qiov->iov, qiov->niov, offset, buf, bytes);
}

size_t qemu_iovec_memset(QEMUIOVector *qiov, size_t offset,
                         int fillc, size_t bytes)
{
    return iov_memset(qiov->iov, qiov->niov, offset, fillc, bytes);
}

/**
 * Check that I/O vector contents are identical
 *
 * The IO vectors must have the same structure (same length of all parts).
 * A typical usage is to compare vectors created with qemu_iovec_clone().
 *
 * @a:          I/O vector
 * @b:          I/O vector
 * @ret:        Offset to first mismatching byte or -1 if match
 */
ssize_t qemu_iovec_compare(QEMUIOVector *a, QEMUIOVector *b)
{
    int i;
    ssize_t offset = 0;

    assert(a->niov == b->niov);
    for (i = 0; i < a->niov; i++) {
        size_t len = 0;
        uint8_t *p = (uint8_t *)a->iov[i].iov_base;
        uint8_t *q = (uint8_t *)b->iov[i].iov_base;

        assert(a->iov[i].iov_len == b->iov[i].iov_len);
        while (len < a->iov[i].iov_len && *p++ == *q++) {
            len++;
        }

        offset += len;

        if (len != a->iov[i].iov_len) {
            return offset;
        }
    }
    return -1;
}

typedef struct {
    int src_index;
    struct iovec *src_iov;
    void *dest_base;
} IOVectorSortElem;

static int sortelem_cmp_src_base(const void *a, const void *b)
{
    const IOVectorSortElem *elem_a = a;
    const IOVectorSortElem *elem_b = b;

    /* Don't overflow */
    if (elem_a->src_iov->iov_base < elem_b->src_iov->iov_base) {
        return -1;
    } else if (elem_a->src_iov->iov_base > elem_b->src_iov->iov_base) {
        return 1;
    } else {
        return 0;
    }
}

static int sortelem_cmp_src_index(const void *a, const void *b)
{
    const IOVectorSortElem *elem_a = a;
    const IOVectorSortElem *elem_b = b;

    return elem_a->src_index - elem_b->src_index;
}

/**
 * Copy contents of I/O vector
 *
 * The relative relationships of overlapping iovecs are preserved.  This is
 * necessary to ensure identical semantics in the cloned I/O vector.
 */
void qemu_iovec_clone(QEMUIOVector *dest, const QEMUIOVector *src, void *buf)
{
    IOVectorSortElem sortelems[src->niov];
    void *last_end;
    int i;

    /* Sort by source iovecs by base address */
    for (i = 0; i < src->niov; i++) {
        sortelems[i].src_index = i;
        sortelems[i].src_iov = &src->iov[i];
    }
    qsort(sortelems, src->niov, sizeof(sortelems[0]), sortelem_cmp_src_base);

    /* Allocate buffer space taking into account overlapping iovecs */
    last_end = NULL;
    for (i = 0; i < src->niov; i++) {
        struct iovec *cur = sortelems[i].src_iov;
        ptrdiff_t rewind = 0;

        /* Detect overlap */
        if (last_end && last_end > cur->iov_base) {
            rewind = last_end - cur->iov_base;
        }

        sortelems[i].dest_base = buf - rewind;
        buf += cur->iov_len - MIN(rewind, cur->iov_len);
        last_end = MAX(cur->iov_base + cur->iov_len, last_end);
    }

    /* Sort by source iovec index and build destination iovec */
    qsort(sortelems, src->niov, sizeof(sortelems[0]), sortelem_cmp_src_index);
    for (i = 0; i < src->niov; i++) {
        qemu_iovec_add(dest, sortelems[i].dest_base, src->iov[i].iov_len);
    }
}

size_t iov_discard_front(struct iovec **iov, unsigned int *iov_cnt,
                         size_t bytes)
{
    size_t total = 0;
    struct iovec *cur;

    for (cur = *iov; *iov_cnt > 0; cur++) {
        if (cur->iov_len > bytes) {
            cur->iov_base += bytes;
            cur->iov_len -= bytes;
            total += bytes;
            break;
        }

        bytes -= cur->iov_len;
        total += cur->iov_len;
        *iov_cnt -= 1;
    }

    *iov = cur;
    return total;
}

size_t iov_discard_back(struct iovec *iov, unsigned int *iov_cnt,
                        size_t bytes)
{
    size_t total = 0;
    struct iovec *cur;

    if (*iov_cnt == 0) {
        return 0;
    }

    cur = iov + (*iov_cnt - 1);

    while (*iov_cnt > 0) {
        if (cur->iov_len > bytes) {
            cur->iov_len -= bytes;
            total += bytes;
            break;
        }

        bytes -= cur->iov_len;
        total += cur->iov_len;
        cur--;
        *iov_cnt -= 1;
    }

    return total;
}

void qemu_iovec_discard_back(QEMUIOVector *qiov, size_t bytes)
{
    size_t total;
    unsigned int niov = qiov->niov;

    assert(qiov->size >= bytes);
    total = iov_discard_back(qiov->iov, &niov, bytes);
    assert(total == bytes);

    qiov->niov = niov;
    qiov->size -= bytes;
}
