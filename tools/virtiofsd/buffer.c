/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2010  Miklos Szeredi <miklos@szeredi.hu>

  Functions for dealing with `struct fuse_buf` and `struct
  fuse_bufvec`.

  This program can be distributed under the terms of the GNU LGPLv2.
  See the file COPYING.LIB
*/

#define _GNU_SOURCE

#include "config.h"
#include "fuse_i.h"
#include "fuse_lowlevel.h"
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

size_t fuse_buf_size(const struct fuse_bufvec *bufv)
{
	size_t i;
	size_t size = 0;

	for (i = 0; i < bufv->count; i++) {
		if (bufv->buf[i].size == SIZE_MAX)
			size = SIZE_MAX;
		else
			size += bufv->buf[i].size;
	}

	return size;
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
			if (!copied)
				return -errno;
			break;
		}
		if (res == 0)
			break;

		copied += res;
		if (!(dst->flags & FUSE_BUF_FD_RETRY))
			break;

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
			if (!copied)
				return -errno;
			break;
		}
		if (res == 0)
			break;

		copied += res;
		if (!(src->flags & FUSE_BUF_FD_RETRY))
			break;

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
			if (!copied)
				return res;
			break;
		}
		if (res == 0)
			break;

		read_len = res;
		res = fuse_buf_write(dst, dst_off, &tmp, 0, read_len);
		if (res < 0) {
			if (!copied)
				return res;
			break;
		}
		if (res == 0)
			break;

		copied += res;

		if (res < this_len)
			break;

		dst_off += res;
		src_off += res;
		len -= res;
	}

	return copied;
}

#ifdef HAVE_SPLICE
static ssize_t fuse_buf_splice(const struct fuse_buf *dst, size_t dst_off,
			       const struct fuse_buf *src, size_t src_off,
			       size_t len, enum fuse_buf_copy_flags flags)
{
	int splice_flags = 0;
	off_t *srcpos = NULL;
	off_t *dstpos = NULL;
	off_t srcpos_val;
	off_t dstpos_val;
	ssize_t res;
	size_t copied = 0;

	if (flags & FUSE_BUF_SPLICE_MOVE)
		splice_flags |= SPLICE_F_MOVE;
	if (flags & FUSE_BUF_SPLICE_NONBLOCK)
		splice_flags |= SPLICE_F_NONBLOCK;

	if (src->flags & FUSE_BUF_FD_SEEK) {
		srcpos_val = src->pos + src_off;
		srcpos = &srcpos_val;
	}
	if (dst->flags & FUSE_BUF_FD_SEEK) {
		dstpos_val = dst->pos + dst_off;
		dstpos = &dstpos_val;
	}

	while (len) {
		res = splice(src->fd, srcpos, dst->fd, dstpos, len,
			     splice_flags);
		if (res == -1) {
			if (copied)
				break;

			if (errno != EINVAL || (flags & FUSE_BUF_FORCE_SPLICE))
				return -errno;

			/* Maybe splice is not supported for this combination */
			return fuse_buf_fd_to_fd(dst, dst_off, src, src_off,
						 len);
		}
		if (res == 0)
			break;

		copied += res;
		if (!(src->flags & FUSE_BUF_FD_RETRY) &&
		    !(dst->flags & FUSE_BUF_FD_RETRY)) {
			break;
		}

		len -= res;
	}

	return copied;
}
#else
static ssize_t fuse_buf_splice(const struct fuse_buf *dst, size_t dst_off,
			       const struct fuse_buf *src, size_t src_off,
			       size_t len, enum fuse_buf_copy_flags flags)
{
	(void) flags;

	return fuse_buf_fd_to_fd(dst, dst_off, src, src_off, len);
}
#endif


static ssize_t fuse_buf_copy_one(const struct fuse_buf *dst, size_t dst_off,
				 const struct fuse_buf *src, size_t src_off,
				 size_t len, enum fuse_buf_copy_flags flags)
{
	int src_is_fd = src->flags & FUSE_BUF_IS_FD;
	int dst_is_fd = dst->flags & FUSE_BUF_IS_FD;

	if (!src_is_fd && !dst_is_fd) {
		char *dstmem = (char *)dst->mem + dst_off;
		char *srcmem = (char *)src->mem + src_off;

		if (dstmem != srcmem) {
			if (dstmem + len <= srcmem || srcmem + len <= dstmem)
				memcpy(dstmem, srcmem, len);
			else
				memmove(dstmem, srcmem, len);
		}

		return len;
	} else if (!src_is_fd) {
		return fuse_buf_write(dst, dst_off, src, src_off, len);
	} else if (!dst_is_fd) {
		return fuse_buf_read(dst, dst_off, src, src_off, len);
	} else if (flags & FUSE_BUF_NO_SPLICE) {
		return fuse_buf_fd_to_fd(dst, dst_off, src, src_off, len);
	} else {
		return fuse_buf_splice(dst, dst_off, src, src_off, len, flags);
	}
}

static const struct fuse_buf *fuse_bufvec_current(struct fuse_bufvec *bufv)
{
	if (bufv->idx < bufv->count)
		return &bufv->buf[bufv->idx];
	else
		return NULL;
}

static int fuse_bufvec_advance(struct fuse_bufvec *bufv, size_t len)
{
	const struct fuse_buf *buf = fuse_bufvec_current(bufv);

	bufv->off += len;
	assert(bufv->off <= buf->size);
	if (bufv->off == buf->size) {
		assert(bufv->idx < bufv->count);
		bufv->idx++;
		if (bufv->idx == bufv->count)
			return 0;
		bufv->off = 0;
	}
	return 1;
}

ssize_t fuse_buf_copy(struct fuse_bufvec *dstv, struct fuse_bufvec *srcv,
		      enum fuse_buf_copy_flags flags)
{
	size_t copied = 0;

	if (dstv == srcv)
		return fuse_buf_size(dstv);

	for (;;) {
		const struct fuse_buf *src = fuse_bufvec_current(srcv);
		const struct fuse_buf *dst = fuse_bufvec_current(dstv);
		size_t src_len;
		size_t dst_len;
		size_t len;
		ssize_t res;

		if (src == NULL || dst == NULL)
			break;

		src_len = src->size - srcv->off;
		dst_len = dst->size - dstv->off;
		len = min_size(src_len, dst_len);

		res = fuse_buf_copy_one(dst, dstv->off, src, srcv->off, len, flags);
		if (res < 0) {
			if (!copied)
				return res;
			break;
		}
		copied += res;

		if (!fuse_bufvec_advance(srcv, res) ||
		    !fuse_bufvec_advance(dstv, res))
			break;

		if (res < len)
			break;
	}

	return copied;
}
