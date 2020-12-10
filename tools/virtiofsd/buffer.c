/*
 * FUSE: Filesystem in Userspace
 * Copyright (C) 2010  Miklos Szeredi <miklos@szeredi.hu>
 *
 * Functions for dealing with `struct fuse_buf` and `struct
 * fuse_bufvec`.
 *
 * This program can be distributed under the terms of the GNU LGPLv2.
 * See the file COPYING.LIB
 */

#include "qemu/osdep.h"
#include "fuse_i.h"
#include "fuse_lowlevel.h"

size_t fuse_buf_size(const struct fuse_bufvec *bufv)
{
    size_t i;
    size_t size = 0;

    for (i = 0; i < bufv->count; i++) {
        if (bufv->buf[i].size == SIZE_MAX) {
            size = SIZE_MAX;
        } else {
            size += bufv->buf[i].size;
        }
    }

    return size;
}

static ssize_t fuse_buf_writev(struct fuse_buf *out_buf,
                               struct fuse_bufvec *in_buf)
{
    ssize_t res, i, j;
    size_t iovcnt = in_buf->count;
    struct iovec *iov;
    int fd = out_buf->fd;

    iov = calloc(iovcnt, sizeof(struct iovec));
    if (!iov) {
        return -ENOMEM;
    }

    for (i = 0, j = 0; i < iovcnt; i++) {
        /* Skip the buf with 0 size */
        if (in_buf->buf[i].size) {
            iov[j].iov_base = in_buf->buf[i].mem;
            iov[j].iov_len = in_buf->buf[i].size;
            j++;
        }
    }

    if (out_buf->flags & FUSE_BUF_FD_SEEK) {
        res = pwritev(fd, iov, iovcnt, out_buf->pos);
    } else {
        res = writev(fd, iov, iovcnt);
    }

    if (res == -1) {
        res = -errno;
    }

    free(iov);
    return res;
}

static size_t min_size(size_t s1, size_t s2)
{
    return s1 < s2 ? s1 : s2;
}

static ssize_t fuse_buf_write(const struct fuse_buf *dst, size_t dst_off,
                              const struct fuse_buf *src, size_t src_off,
                              size_t len)
{
    ssize_t res = 0;
    size_t copied = 0;

    while (len) {
        if (dst->flags & FUSE_BUF_FD_SEEK) {
            res = pwrite(dst->fd, (char *)src->mem + src_off, len,
                         dst->pos + dst_off);
        } else {
            res = write(dst->fd, (char *)src->mem + src_off, len);
        }
        if (res == -1) {
            if (!copied) {
                return -errno;
            }
            break;
        }
        if (res == 0) {
            break;
        }

        copied += res;
        if (!(dst->flags & FUSE_BUF_FD_RETRY)) {
            break;
        }

        src_off += res;
        dst_off += res;
        len -= res;
    }

    return copied;
}

static ssize_t fuse_buf_read(const struct fuse_buf *dst, size_t dst_off,
                             const struct fuse_buf *src, size_t src_off,
                             size_t len)
{
    ssize_t res = 0;
    size_t copied = 0;

    while (len) {
        if (src->flags & FUSE_BUF_FD_SEEK) {
            res = pread(src->fd, (char *)dst->mem + dst_off, len,
                        src->pos + src_off);
        } else {
            res = read(src->fd, (char *)dst->mem + dst_off, len);
        }
        if (res == -1) {
            if (!copied) {
                return -errno;
            }
            break;
        }
        if (res == 0) {
            break;
        }

        copied += res;
        if (!(src->flags & FUSE_BUF_FD_RETRY)) {
            break;
        }

        dst_off += res;
        src_off += res;
        len -= res;
    }

    return copied;
}

static ssize_t fuse_buf_fd_to_fd(const struct fuse_buf *dst, size_t dst_off,
                                 const struct fuse_buf *src, size_t src_off,
                                 size_t len)
{
    char buf[4096];
    struct fuse_buf tmp = {
        .size = sizeof(buf),
        .flags = 0,
    };
    ssize_t res;
    size_t copied = 0;

    tmp.mem = buf;

    while (len) {
        size_t this_len = min_size(tmp.size, len);
        size_t read_len;

        res = fuse_buf_read(&tmp, 0, src, src_off, this_len);
        if (res < 0) {
            if (!copied) {
                return res;
            }
            break;
        }
        if (res == 0) {
            break;
        }

        read_len = res;
        res = fuse_buf_write(dst, dst_off, &tmp, 0, read_len);
        if (res < 0) {
            if (!copied) {
                return res;
            }
            break;
        }
        if (res == 0) {
            break;
        }

        copied += res;

        if (res < this_len) {
            break;
        }

        dst_off += res;
        src_off += res;
        len -= res;
    }

    return copied;
}

static ssize_t fuse_buf_copy_one(const struct fuse_buf *dst, size_t dst_off,
                                 const struct fuse_buf *src, size_t src_off,
                                 size_t len)
{
    int src_is_fd = src->flags & FUSE_BUF_IS_FD;
    int dst_is_fd = dst->flags & FUSE_BUF_IS_FD;

    if (!src_is_fd && !dst_is_fd) {
        char *dstmem = (char *)dst->mem + dst_off;
        char *srcmem = (char *)src->mem + src_off;

        if (dstmem != srcmem) {
            if (dstmem + len <= srcmem || srcmem + len <= dstmem) {
                memcpy(dstmem, srcmem, len);
            } else {
                memmove(dstmem, srcmem, len);
            }
        }

        return len;
    } else if (!src_is_fd) {
        return fuse_buf_write(dst, dst_off, src, src_off, len);
    } else if (!dst_is_fd) {
        return fuse_buf_read(dst, dst_off, src, src_off, len);
    } else {
        return fuse_buf_fd_to_fd(dst, dst_off, src, src_off, len);
    }
}

static const struct fuse_buf *fuse_bufvec_current(struct fuse_bufvec *bufv)
{
    if (bufv->idx < bufv->count) {
        return &bufv->buf[bufv->idx];
    } else {
        return NULL;
    }
}

static int fuse_bufvec_advance(struct fuse_bufvec *bufv, size_t len)
{
    const struct fuse_buf *buf = fuse_bufvec_current(bufv);

    if (!buf) {
        return 0;
    }

    bufv->off += len;
    assert(bufv->off <= buf->size);
    if (bufv->off == buf->size) {
        assert(bufv->idx < bufv->count);
        bufv->idx++;
        if (bufv->idx == bufv->count) {
            return 0;
        }
        bufv->off = 0;
    }
    return 1;
}

ssize_t fuse_buf_copy(struct fuse_bufvec *dstv, struct fuse_bufvec *srcv)
{
    size_t copied = 0, i;

    if (dstv == srcv) {
        return fuse_buf_size(dstv);
    }

    /*
     * use writev to improve bandwidth when all the
     * src buffers already mapped by the daemon
     * process
     */
    for (i = 0; i < srcv->count; i++) {
        if (srcv->buf[i].flags & FUSE_BUF_IS_FD) {
            break;
        }
    }
    if ((i == srcv->count) && (dstv->count == 1) &&
        (dstv->idx == 0) &&
        (dstv->buf[0].flags & FUSE_BUF_IS_FD)) {
        dstv->buf[0].pos += dstv->off;
        return fuse_buf_writev(&dstv->buf[0], srcv);
    }

    for (;;) {
        const struct fuse_buf *src = fuse_bufvec_current(srcv);
        const struct fuse_buf *dst = fuse_bufvec_current(dstv);
        size_t src_len;
        size_t dst_len;
        size_t len;
        ssize_t res;

        if (src == NULL || dst == NULL) {
            break;
        }

        src_len = src->size - srcv->off;
        dst_len = dst->size - dstv->off;
        len = min_size(src_len, dst_len);

        res = fuse_buf_copy_one(dst, dstv->off, src, srcv->off, len);
        if (res < 0) {
            if (!copied) {
                return res;
            }
            break;
        }
        copied += res;

        if (!fuse_bufvec_advance(srcv, res) ||
            !fuse_bufvec_advance(dstv, res)) {
            break;
        }

        if (res < len) {
            break;
        }
    }

    return copied;
}

void *fuse_mbuf_iter_advance(struct fuse_mbuf_iter *iter, size_t len)
{
    void *ptr;

    if (len > iter->size - iter->pos) {
        return NULL;
    }

    ptr = iter->mem + iter->pos;
    iter->pos += len;
    return ptr;
}

const char *fuse_mbuf_iter_advance_str(struct fuse_mbuf_iter *iter)
{
    const char *str = iter->mem + iter->pos;
    size_t remaining = iter->size - iter->pos;
    size_t i;

    for (i = 0; i < remaining; i++) {
        if (str[i] == '\0') {
            iter->pos += i + 1;
            return str;
        }
    }
    return NULL;
}
