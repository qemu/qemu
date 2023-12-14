/*
 * Helpers for using (partial) iovecs.
 *
 * Copyright (C) 2010 Red Hat, Inc.
 *
 * Author(s):
 *  Amit Shah <amit.shah@redhat.com>
 *  Michael Tokarev <mjt@tls.msk.ru>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#ifndef IOV_H
#define IOV_H

/**
 * count and return data size, in bytes, of an iovec
 * starting at `iov' of `iov_cnt' number of elements.
 */
size_t iov_size(const struct iovec *iov, const unsigned int iov_cnt);

/**
 * Copy from single continuous buffer to scatter-gather vector of buffers
 * (iovec) and back like memcpy() between two continuous memory regions.
 * Data in single continuous buffer starting at address `buf' and
 * `bytes' bytes long will be copied to/from an iovec `iov' with
 * `iov_cnt' number of elements, starting at byte position `offset'
 * within the iovec.  If the iovec does not contain enough space,
 * only part of data will be copied, up to the end of the iovec.
 * Number of bytes actually copied will be returned, which is
 *  min(bytes, iov_size(iov)-offset)
 * `Offset' must point to the inside of iovec.
 */
size_t iov_from_buf_full(const struct iovec *iov, unsigned int iov_cnt,
                         size_t offset, const void *buf, size_t bytes);
size_t iov_to_buf_full(const struct iovec *iov, const unsigned int iov_cnt,
                       size_t offset, void *buf, size_t bytes);

static inline size_t
iov_from_buf(const struct iovec *iov, unsigned int iov_cnt,
             size_t offset, const void *buf, size_t bytes)
{
    if (__builtin_constant_p(bytes) && iov_cnt &&
        offset <= iov[0].iov_len && bytes <= iov[0].iov_len - offset) {
        memcpy(iov[0].iov_base + offset, buf, bytes);
        return bytes;
    } else {
        return iov_from_buf_full(iov, iov_cnt, offset, buf, bytes);
    }
}

static inline size_t
iov_to_buf(const struct iovec *iov, const unsigned int iov_cnt,
           size_t offset, void *buf, size_t bytes)
{
    if (__builtin_constant_p(bytes) && iov_cnt &&
        offset <= iov[0].iov_len && bytes <= iov[0].iov_len - offset) {
        memcpy(buf, iov[0].iov_base + offset, bytes);
        return bytes;
    } else {
        return iov_to_buf_full(iov, iov_cnt, offset, buf, bytes);
    }
}

/**
 * Set data bytes pointed out by iovec `iov' of size `iov_cnt' elements,
 * starting at byte offset `start', to value `fillc', repeating it
 * `bytes' number of times.  `Offset' must point to the inside of iovec.
 * If `bytes' is large enough, only last bytes portion of iovec,
 * up to the end of it, will be filled with the specified value.
 * Function return actual number of bytes processed, which is
 * min(size, iov_size(iov) - offset).
 */
size_t iov_memset(const struct iovec *iov, const unsigned int iov_cnt,
                  size_t offset, int fillc, size_t bytes);

/*
 * Send/recv data from/to iovec buffers directly
 *
 * `offset' bytes in the beginning of iovec buffer are skipped and
 * next `bytes' bytes are used, which must be within data of iovec.
 *
 *   r = iov_send_recv(sockfd, iov, iovcnt, offset, bytes, true);
 *
 * is logically equivalent to
 *
 *   char *buf = malloc(bytes);
 *   iov_to_buf(iov, iovcnt, offset, buf, bytes);
 *   r = send(sockfd, buf, bytes, 0);
 *   free(buf);
 *
 * For iov_send_recv() _whole_ area being sent or received
 * should be within the iovec, not only beginning of it.
 */
ssize_t iov_send_recv(int sockfd, const struct iovec *iov, unsigned iov_cnt,
                      size_t offset, size_t bytes, bool do_send);
#define iov_recv(sockfd, iov, iov_cnt, offset, bytes) \
  iov_send_recv(sockfd, iov, iov_cnt, offset, bytes, false)
#define iov_send(sockfd, iov, iov_cnt, offset, bytes) \
  iov_send_recv(sockfd, iov, iov_cnt, offset, bytes, true)

/**
 * Produce a text hexdump of iovec `iov' with `iov_cnt' number of elements
 * in file `fp', prefixing each line with `prefix' and processing not more
 * than `limit' data bytes.
 */
void iov_hexdump(const struct iovec *iov, const unsigned int iov_cnt,
                 FILE *fp, const char *prefix, size_t limit);

/*
 * Partial copy of vector from iov to dst_iov (data is not copied).
 * dst_iov overlaps iov at a specified offset.
 * size of dst_iov is at most bytes. dst vector count is returned.
 */
unsigned iov_copy(struct iovec *dst_iov, unsigned int dst_iov_cnt,
                 const struct iovec *iov, unsigned int iov_cnt,
                 size_t offset, size_t bytes);

/*
 * Remove a given number of bytes from the front or back of a vector.
 * This may update iov and/or iov_cnt to exclude iovec elements that are
 * no longer required.
 *
 * The number of bytes actually discarded is returned.  This number may be
 * smaller than requested if the vector is too small.
 */
size_t iov_discard_front(struct iovec **iov, unsigned int *iov_cnt,
                         size_t bytes);
size_t iov_discard_back(struct iovec *iov, unsigned int *iov_cnt,
                        size_t bytes);

/* Information needed to undo an iov_discard_*() operation */
typedef struct {
    struct iovec *modified_iov;
    struct iovec orig;
} IOVDiscardUndo;

/*
 * Undo an iov_discard_front_undoable() or iov_discard_back_undoable()
 * operation. If multiple operations are made then each one needs a separate
 * IOVDiscardUndo and iov_discard_undo() must be called in the reverse order
 * that the operations were made.
 */
void iov_discard_undo(IOVDiscardUndo *undo);

/*
 * Undoable versions of iov_discard_front() and iov_discard_back(). Use
 * iov_discard_undo() to reset to the state before the discard operations.
 */
size_t iov_discard_front_undoable(struct iovec **iov, unsigned int *iov_cnt,
                                  size_t bytes, IOVDiscardUndo *undo);
size_t iov_discard_back_undoable(struct iovec *iov, unsigned int *iov_cnt,
                                 size_t bytes, IOVDiscardUndo *undo);

typedef struct QEMUIOVector {
    struct iovec *iov;
    int niov;

    /*
     * For external @iov (qemu_iovec_init_external()) or allocated @iov
     * (qemu_iovec_init()), @size is the cumulative size of iovecs and
     * @local_iov is invalid and unused.
     *
     * For embedded @iov (QEMU_IOVEC_INIT_BUF() or qemu_iovec_init_buf()),
     * @iov is equal to &@local_iov, and @size is valid, as it has same
     * offset and type as @local_iov.iov_len, which is guaranteed by
     * static assertion below.
     *
     * @nalloc is always valid and is -1 both for embedded and external
     * cases. It is included in the union only to ensure the padding prior
     * to the @size field will not result in a 0-length array.
     */
    union {
        struct {
            int nalloc;
            struct iovec local_iov;
        };
        struct {
            char __pad[sizeof(int) + offsetof(struct iovec, iov_len)];
            size_t size;
        };
    };
} QEMUIOVector;

QEMU_BUILD_BUG_ON(offsetof(QEMUIOVector, size) !=
                  offsetof(QEMUIOVector, local_iov.iov_len));

#define QEMU_IOVEC_INIT_BUF(self, buf, len)              \
{                                                        \
    .iov = &(self).local_iov,                            \
    .niov = 1,                                           \
    .nalloc = -1,                                        \
    .local_iov = {                                       \
        .iov_base = (void *)(buf), /* cast away const */ \
        .iov_len = (len),                                \
    },                                                   \
}

/*
 * qemu_iovec_init_buf
 *
 * Initialize embedded QEMUIOVector.
 *
 * Note: "const" is used over @buf pointer to make it simple to pass
 * const pointers, appearing in read functions. Then this "const" is
 * cast away by QEMU_IOVEC_INIT_BUF().
 */
static inline void qemu_iovec_init_buf(QEMUIOVector *qiov,
                                       const void *buf, size_t len)
{
    *qiov = (QEMUIOVector) QEMU_IOVEC_INIT_BUF(*qiov, buf, len);
}

static inline void *qemu_iovec_buf(QEMUIOVector *qiov)
{
    /* Only supports embedded iov */
    assert(qiov->nalloc == -1 && qiov->iov == &qiov->local_iov);

    return qiov->local_iov.iov_base;
}

void qemu_iovec_init(QEMUIOVector *qiov, int alloc_hint);
void qemu_iovec_init_external(QEMUIOVector *qiov, struct iovec *iov, int niov);
void qemu_iovec_init_slice(QEMUIOVector *qiov, QEMUIOVector *source,
                           size_t offset, size_t len);
struct iovec *qemu_iovec_slice(QEMUIOVector *qiov,
                               size_t offset, size_t len,
                               size_t *head, size_t *tail, int *niov);
int qemu_iovec_subvec_niov(QEMUIOVector *qiov, size_t offset, size_t len);
void qemu_iovec_add(QEMUIOVector *qiov, void *base, size_t len);
void qemu_iovec_concat(QEMUIOVector *dst,
                       QEMUIOVector *src, size_t soffset, size_t sbytes);
size_t qemu_iovec_concat_iov(QEMUIOVector *dst,
                             struct iovec *src_iov, unsigned int src_cnt,
                             size_t soffset, size_t sbytes);
bool qemu_iovec_is_zero(QEMUIOVector *qiov, size_t qiov_offeset, size_t bytes);
void qemu_iovec_destroy(QEMUIOVector *qiov);
void qemu_iovec_reset(QEMUIOVector *qiov);
size_t qemu_iovec_to_buf(QEMUIOVector *qiov, size_t offset,
                         void *buf, size_t bytes);
size_t qemu_iovec_from_buf(QEMUIOVector *qiov, size_t offset,
                           const void *buf, size_t bytes);
size_t qemu_iovec_memset(QEMUIOVector *qiov, size_t offset,
                         int fillc, size_t bytes);
ssize_t qemu_iovec_compare(QEMUIOVector *a, QEMUIOVector *b);
void qemu_iovec_clone(QEMUIOVector *dest, const QEMUIOVector *src, void *buf);
void qemu_iovec_discard_back(QEMUIOVector *qiov, size_t bytes);

#endif
