/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

  Implementation of (most of) the low-level FUSE API. The session loop
  functions are implemented in separate files.

  This program can be distributed under the terms of the GNU LGPLv2.
  See the file COPYING.LIB
*/

#define _GNU_SOURCE

#include "config.h"
#include "fuse_i.h"
#include "fuse_kernel.h"
#include "fuse_opt.h"
#include "fuse_misc.h"
#include "mount_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <assert.h>
#include <sys/file.h>

#ifndef F_LINUX_SPECIFIC_BASE
#define F_LINUX_SPECIFIC_BASE       1024
#endif
#ifndef F_SETPIPE_SZ
#define F_SETPIPE_SZ	(F_LINUX_SPECIFIC_BASE + 7)
#endif


#define PARAM(inarg) (((char *)(inarg)) + sizeof(*(inarg)))
#define OFFSET_MAX 0x7fffffffffffffffLL

#define container_of(ptr, type, member) ({				\
			const typeof( ((type *)0)->member ) *__mptr = (ptr); \
			(type *)( (char *)__mptr - offsetof(type,member) );})

struct fuse_pollhandle {
	uint64_t kh;
	struct fuse_session *se;
};

static size_t pagesize;

static __attribute__((constructor)) void fuse_ll_init_pagesize(void)
{
	pagesize = getpagesize();
}

static void convert_stat(const struct stat *stbuf, struct fuse_attr *attr)
{
	attr->ino	= stbuf->st_ino;
	attr->mode	= stbuf->st_mode;
	attr->nlink	= stbuf->st_nlink;
	attr->uid	= stbuf->st_uid;
	attr->gid	= stbuf->st_gid;
	attr->rdev	= stbuf->st_rdev;
	attr->size	= stbuf->st_size;
	attr->blksize	= stbuf->st_blksize;
	attr->blocks	= stbuf->st_blocks;
	attr->atime	= stbuf->st_atime;
	attr->mtime	= stbuf->st_mtime;
	attr->ctime	= stbuf->st_ctime;
	attr->atimensec = ST_ATIM_NSEC(stbuf);
	attr->mtimensec = ST_MTIM_NSEC(stbuf);
	attr->ctimensec = ST_CTIM_NSEC(stbuf);
}

static void convert_attr(const struct fuse_setattr_in *attr, struct stat *stbuf)
{
	stbuf->st_mode	       = attr->mode;
	stbuf->st_uid	       = attr->uid;
	stbuf->st_gid	       = attr->gid;
	stbuf->st_size	       = attr->size;
	stbuf->st_atime	       = attr->atime;
	stbuf->st_mtime	       = attr->mtime;
	stbuf->st_ctime        = attr->ctime;
	ST_ATIM_NSEC_SET(stbuf, attr->atimensec);
	ST_MTIM_NSEC_SET(stbuf, attr->mtimensec);
	ST_CTIM_NSEC_SET(stbuf, attr->ctimensec);
}

static	size_t iov_length(const struct iovec *iov, size_t count)
{
	size_t seg;
	size_t ret = 0;

	for (seg = 0; seg < count; seg++)
		ret += iov[seg].iov_len;
	return ret;
}

static void list_init_req(struct fuse_req *req)
{
	req->next = req;
	req->prev = req;
}

static void list_del_req(struct fuse_req *req)
{
	struct fuse_req *prev = req->prev;
	struct fuse_req *next = req->next;
	prev->next = next;
	next->prev = prev;
}

static void list_add_req(struct fuse_req *req, struct fuse_req *next)
{
	struct fuse_req *prev = next->prev;
	req->next = next;
	req->prev = prev;
	prev->next = req;
	next->prev = req;
}

static void destroy_req(fuse_req_t req)
{
	pthread_mutex_destroy(&req->lock);
	free(req);
}

void fuse_free_req(fuse_req_t req)
{
	int ctr;
	struct fuse_session *se = req->se;

	pthread_mutex_lock(&se->lock);
	req->u.ni.func = NULL;
	req->u.ni.data = NULL;
	list_del_req(req);
	ctr = --req->ctr;
	fuse_chan_put(req->ch);
	req->ch = NULL;
	pthread_mutex_unlock(&se->lock);
	if (!ctr)
		destroy_req(req);
}

static struct fuse_req *fuse_ll_alloc_req(struct fuse_session *se)
{
	struct fuse_req *req;

	req = (struct fuse_req *) calloc(1, sizeof(struct fuse_req));
	if (req == NULL) {
		fuse_log(FUSE_LOG_ERR, "fuse: failed to allocate request\n");
	} else {
		req->se = se;
		req->ctr = 1;
		list_init_req(req);
		fuse_mutex_init(&req->lock);
	}

	return req;
}

/* Send data. If *ch* is NULL, send via session master fd */
static int fuse_send_msg(struct fuse_session *se, struct fuse_chan *ch,
			 struct iovec *iov, int count)
{
	struct fuse_out_header *out = iov[0].iov_base;

	out->len = iov_length(iov, count);
	if (se->debug) {
		if (out->unique == 0) {
			fuse_log(FUSE_LOG_DEBUG, "NOTIFY: code=%d length=%u\n",
				out->error, out->len);
		} else if (out->error) {
			fuse_log(FUSE_LOG_DEBUG,
				"   unique: %llu, error: %i (%s), outsize: %i\n",
				(unsigned long long) out->unique, out->error,
				strerror(-out->error), out->len);
		} else {
			fuse_log(FUSE_LOG_DEBUG,
				"   unique: %llu, success, outsize: %i\n",
				(unsigned long long) out->unique, out->len);
		}
	}

	ssize_t res = writev(ch ? ch->fd : se->fd,
			     iov, count);
	int err = errno;

	if (res == -1) {
		assert(se != NULL);

		/* ENOENT means the operation was interrupted */
		if (!fuse_session_exited(se) && err != ENOENT)
			perror("fuse: writing device");
		return -err;
	}

	return 0;
}


int fuse_send_reply_iov_nofree(fuse_req_t req, int error, struct iovec *iov,
			       int count)
{
	struct fuse_out_header out;

	if (error <= -1000 || error > 0) {
		fuse_log(FUSE_LOG_ERR, "fuse: bad error value: %i\n",	error);
		error = -ERANGE;
	}

	out.unique = req->unique;
	out.error = error;

	iov[0].iov_base = &out;
	iov[0].iov_len = sizeof(struct fuse_out_header);

	return fuse_send_msg(req->se, req->ch, iov, count);
}

static int send_reply_iov(fuse_req_t req, int error, struct iovec *iov,
			  int count)
{
	int res;

	res = fuse_send_reply_iov_nofree(req, error, iov, count);
	fuse_free_req(req);
	return res;
}

static int send_reply(fuse_req_t req, int error, const void *arg,
		      size_t argsize)
{
	struct iovec iov[2];
	int count = 1;
	if (argsize) {
		iov[1].iov_base = (void *) arg;
		iov[1].iov_len = argsize;
		count++;
	}
	return send_reply_iov(req, error, iov, count);
}

int fuse_reply_iov(fuse_req_t req, const struct iovec *iov, int count)
{
	int res;
	struct iovec *padded_iov;

	padded_iov = malloc((count + 1) * sizeof(struct iovec));
	if (padded_iov == NULL)
		return fuse_reply_err(req, ENOMEM);

	memcpy(padded_iov + 1, iov, count * sizeof(struct iovec));
	count++;

	res = send_reply_iov(req, 0, padded_iov, count);
	free(padded_iov);

	return res;
}


/* `buf` is allowed to be empty so that the proper size may be
   allocated by the caller */
size_t fuse_add_direntry(fuse_req_t req, char *buf, size_t bufsize,
			 const char *name, const struct stat *stbuf, off_t off)
{
	(void)req;
	size_t namelen;
	size_t entlen;
	size_t entlen_padded;
	struct fuse_dirent *dirent;

	namelen = strlen(name);
	entlen = FUSE_NAME_OFFSET + namelen;
	entlen_padded = FUSE_DIRENT_ALIGN(entlen);

	if ((buf == NULL) || (entlen_padded > bufsize))
	  return entlen_padded;

	dirent = (struct fuse_dirent*) buf;
	dirent->ino = stbuf->st_ino;
	dirent->off = off;
	dirent->namelen = namelen;
	dirent->type = (stbuf->st_mode & S_IFMT) >> 12;
	memcpy(dirent->name, name, namelen);
	memset(dirent->name + namelen, 0, entlen_padded - entlen);

	return entlen_padded;
}

static void convert_statfs(const struct statvfs *stbuf,
			   struct fuse_kstatfs *kstatfs)
{
	kstatfs->bsize	 = stbuf->f_bsize;
	kstatfs->frsize	 = stbuf->f_frsize;
	kstatfs->blocks	 = stbuf->f_blocks;
	kstatfs->bfree	 = stbuf->f_bfree;
	kstatfs->bavail	 = stbuf->f_bavail;
	kstatfs->files	 = stbuf->f_files;
	kstatfs->ffree	 = stbuf->f_ffree;
	kstatfs->namelen = stbuf->f_namemax;
}

static int send_reply_ok(fuse_req_t req, const void *arg, size_t argsize)
{
	return send_reply(req, 0, arg, argsize);
}

int fuse_reply_err(fuse_req_t req, int err)
{
	return send_reply(req, -err, NULL, 0);
}

void fuse_reply_none(fuse_req_t req)
{
	fuse_free_req(req);
}

static unsigned long calc_timeout_sec(double t)
{
	if (t > (double) ULONG_MAX)
		return ULONG_MAX;
	else if (t < 0.0)
		return 0;
	else
		return (unsigned long) t;
}

static unsigned int calc_timeout_nsec(double t)
{
	double f = t - (double) calc_timeout_sec(t);
	if (f < 0.0)
		return 0;
	else if (f >= 0.999999999)
		return 999999999;
	else
		return (unsigned int) (f * 1.0e9);
}

static void fill_entry(struct fuse_entry_out *arg,
		       const struct fuse_entry_param *e)
{
	arg->nodeid = e->ino;
	arg->generation = e->generation;
	arg->entry_valid = calc_timeout_sec(e->entry_timeout);
	arg->entry_valid_nsec = calc_timeout_nsec(e->entry_timeout);
	arg->attr_valid = calc_timeout_sec(e->attr_timeout);
	arg->attr_valid_nsec = calc_timeout_nsec(e->attr_timeout);
	convert_stat(&e->attr, &arg->attr);
}

/* `buf` is allowed to be empty so that the proper size may be
   allocated by the caller */
size_t fuse_add_direntry_plus(fuse_req_t req, char *buf, size_t bufsize,
			      const char *name,
			      const struct fuse_entry_param *e, off_t off)
{
	(void)req;
	size_t namelen;
	size_t entlen;
	size_t entlen_padded;

	namelen = strlen(name);
	entlen = FUSE_NAME_OFFSET_DIRENTPLUS + namelen;
	entlen_padded = FUSE_DIRENT_ALIGN(entlen);
	if ((buf == NULL) || (entlen_padded > bufsize))
	  return entlen_padded;

	struct fuse_direntplus *dp = (struct fuse_direntplus *) buf;
	memset(&dp->entry_out, 0, sizeof(dp->entry_out));
	fill_entry(&dp->entry_out, e);

	struct fuse_dirent *dirent = &dp->dirent;
	dirent->ino = e->attr.st_ino;
	dirent->off = off;
	dirent->namelen = namelen;
	dirent->type = (e->attr.st_mode & S_IFMT) >> 12;
	memcpy(dirent->name, name, namelen);
	memset(dirent->name + namelen, 0, entlen_padded - entlen);

	return entlen_padded;
}

static void fill_open(struct fuse_open_out *arg,
		      const struct fuse_file_info *f)
{
	arg->fh = f->fh;
	if (f->direct_io)
		arg->open_flags |= FOPEN_DIRECT_IO;
	if (f->keep_cache)
		arg->open_flags |= FOPEN_KEEP_CACHE;
	if (f->cache_readdir)
		arg->open_flags |= FOPEN_CACHE_DIR;
	if (f->nonseekable)
		arg->open_flags |= FOPEN_NONSEEKABLE;
}

int fuse_reply_entry(fuse_req_t req, const struct fuse_entry_param *e)
{
	struct fuse_entry_out arg;
	size_t size = req->se->conn.proto_minor < 9 ?
		FUSE_COMPAT_ENTRY_OUT_SIZE : sizeof(arg);

	/* before ABI 7.4 e->ino == 0 was invalid, only ENOENT meant
	   negative entry */
	if (!e->ino && req->se->conn.proto_minor < 4)
		return fuse_reply_err(req, ENOENT);

	memset(&arg, 0, sizeof(arg));
	fill_entry(&arg, e);
	return send_reply_ok(req, &arg, size);
}

int fuse_reply_create(fuse_req_t req, const struct fuse_entry_param *e,
		      const struct fuse_file_info *f)
{
	char buf[sizeof(struct fuse_entry_out) + sizeof(struct fuse_open_out)];
	size_t entrysize = req->se->conn.proto_minor < 9 ?
		FUSE_COMPAT_ENTRY_OUT_SIZE : sizeof(struct fuse_entry_out);
	struct fuse_entry_out *earg = (struct fuse_entry_out *) buf;
	struct fuse_open_out *oarg = (struct fuse_open_out *) (buf + entrysize);

	memset(buf, 0, sizeof(buf));
	fill_entry(earg, e);
	fill_open(oarg, f);
	return send_reply_ok(req, buf,
			     entrysize + sizeof(struct fuse_open_out));
}

int fuse_reply_attr(fuse_req_t req, const struct stat *attr,
		    double attr_timeout)
{
	struct fuse_attr_out arg;
	size_t size = req->se->conn.proto_minor < 9 ?
		FUSE_COMPAT_ATTR_OUT_SIZE : sizeof(arg);

	memset(&arg, 0, sizeof(arg));
	arg.attr_valid = calc_timeout_sec(attr_timeout);
	arg.attr_valid_nsec = calc_timeout_nsec(attr_timeout);
	convert_stat(attr, &arg.attr);

	return send_reply_ok(req, &arg, size);
}

int fuse_reply_readlink(fuse_req_t req, const char *linkname)
{
	return send_reply_ok(req, linkname, strlen(linkname));
}

int fuse_reply_open(fuse_req_t req, const struct fuse_file_info *f)
{
	struct fuse_open_out arg;

	memset(&arg, 0, sizeof(arg));
	fill_open(&arg, f);
	return send_reply_ok(req, &arg, sizeof(arg));
}

int fuse_reply_write(fuse_req_t req, size_t count)
{
	struct fuse_write_out arg;

	memset(&arg, 0, sizeof(arg));
	arg.size = count;

	return send_reply_ok(req, &arg, sizeof(arg));
}

int fuse_reply_buf(fuse_req_t req, const char *buf, size_t size)
{
	return send_reply_ok(req, buf, size);
}

static int fuse_send_data_iov_fallback(struct fuse_session *se,
				       struct fuse_chan *ch,
				       struct iovec *iov, int iov_count,
				       struct fuse_bufvec *buf,
				       size_t len)
{
	struct fuse_bufvec mem_buf = FUSE_BUFVEC_INIT(len);
	void *mbuf;
	int res;

	/* Optimize common case */
	if (buf->count == 1 && buf->idx == 0 && buf->off == 0 &&
	    !(buf->buf[0].flags & FUSE_BUF_IS_FD)) {
		/* FIXME: also avoid memory copy if there are multiple buffers
		   but none of them contain an fd */

		iov[iov_count].iov_base = buf->buf[0].mem;
		iov[iov_count].iov_len = len;
		iov_count++;
		return fuse_send_msg(se, ch, iov, iov_count);
	}

	res = posix_memalign(&mbuf, pagesize, len);
	if (res != 0)
		return res;

	mem_buf.buf[0].mem = mbuf;
	res = fuse_buf_copy(&mem_buf, buf, 0);
	if (res < 0) {
		free(mbuf);
		return -res;
	}
	len = res;

	iov[iov_count].iov_base = mbuf;
	iov[iov_count].iov_len = len;
	iov_count++;
	res = fuse_send_msg(se, ch, iov, iov_count);
	free(mbuf);

	return res;
}

struct fuse_ll_pipe {
	size_t size;
	int can_grow;
	int pipe[2];
};

static void fuse_ll_pipe_free(struct fuse_ll_pipe *llp)
{
	close(llp->pipe[0]);
	close(llp->pipe[1]);
	free(llp);
}

#ifdef HAVE_SPLICE
#if !defined(HAVE_PIPE2) || !defined(O_CLOEXEC)
static int fuse_pipe(int fds[2])
{
	int rv = pipe(fds);

	if (rv == -1)
		return rv;

	if (fcntl(fds[0], F_SETFL, O_NONBLOCK) == -1 ||
	    fcntl(fds[1], F_SETFL, O_NONBLOCK) == -1 ||
	    fcntl(fds[0], F_SETFD, FD_CLOEXEC) == -1 ||
	    fcntl(fds[1], F_SETFD, FD_CLOEXEC) == -1) {
		close(fds[0]);
		close(fds[1]);
		rv = -1;
	}
	return rv;
}
#else
static int fuse_pipe(int fds[2])
{
	return pipe2(fds, O_CLOEXEC | O_NONBLOCK);
}
#endif

static struct fuse_ll_pipe *fuse_ll_get_pipe(struct fuse_session *se)
{
	struct fuse_ll_pipe *llp = pthread_getspecific(se->pipe_key);
	if (llp == NULL) {
		int res;

		llp = malloc(sizeof(struct fuse_ll_pipe));
		if (llp == NULL)
			return NULL;

		res = fuse_pipe(llp->pipe);
		if (res == -1) {
			free(llp);
			return NULL;
		}

		/*
		 *the default size is 16 pages on linux
		 */
		llp->size = pagesize * 16;
		llp->can_grow = 1;

		pthread_setspecific(se->pipe_key, llp);
	}

	return llp;
}
#endif

static void fuse_ll_clear_pipe(struct fuse_session *se)
{
	struct fuse_ll_pipe *llp = pthread_getspecific(se->pipe_key);
	if (llp) {
		pthread_setspecific(se->pipe_key, NULL);
		fuse_ll_pipe_free(llp);
	}
}

#if defined(HAVE_SPLICE) && defined(HAVE_VMSPLICE)
static int read_back(int fd, char *buf, size_t len)
{
	int res;

	res = read(fd, buf, len);
	if (res == -1) {
		fuse_log(FUSE_LOG_ERR, "fuse: internal error: failed to read back from pipe: %s\n", strerror(errno));
		return -EIO;
	}
	if (res != len) {
		fuse_log(FUSE_LOG_ERR, "fuse: internal error: short read back from pipe: %i from %zi\n", res, len);
		return -EIO;
	}
	return 0;
}

static int grow_pipe_to_max(int pipefd)
{
	int max;
	int res;
	int maxfd;
	char buf[32];

	maxfd = open("/proc/sys/fs/pipe-max-size", O_RDONLY);
	if (maxfd < 0)
		return -errno;

	res = read(maxfd, buf, sizeof(buf) - 1);
	if (res < 0) {
		int saved_errno;

		saved_errno = errno;
		close(maxfd);
		return -saved_errno;
	}
	close(maxfd);
	buf[res] = '\0';

	max = atoi(buf);
	res = fcntl(pipefd, F_SETPIPE_SZ, max);
	if (res < 0)
		return -errno;
	return max;
}

static int fuse_send_data_iov(struct fuse_session *se, struct fuse_chan *ch,
			       struct iovec *iov, int iov_count,
			       struct fuse_bufvec *buf, unsigned int flags)
{
	int res;
	size_t len = fuse_buf_size(buf);
	struct fuse_out_header *out = iov[0].iov_base;
	struct fuse_ll_pipe *llp;
	int splice_flags;
	size_t pipesize;
	size_t total_fd_size;
	size_t idx;
	size_t headerlen;
	struct fuse_bufvec pipe_buf = FUSE_BUFVEC_INIT(len);

	if (se->broken_splice_nonblock)
		goto fallback;

	if (flags & FUSE_BUF_NO_SPLICE)
		goto fallback;

	total_fd_size = 0;
	for (idx = buf->idx; idx < buf->count; idx++) {
		if (buf->buf[idx].flags & FUSE_BUF_IS_FD) {
			total_fd_size = buf->buf[idx].size;
			if (idx == buf->idx)
				total_fd_size -= buf->off;
		}
	}
	if (total_fd_size < 2 * pagesize)
		goto fallback;

	if (se->conn.proto_minor < 14 ||
	    !(se->conn.want & FUSE_CAP_SPLICE_WRITE))
		goto fallback;

	llp = fuse_ll_get_pipe(se);
	if (llp == NULL)
		goto fallback;


	headerlen = iov_length(iov, iov_count);

	out->len = headerlen + len;

	/*
	 * Heuristic for the required pipe size, does not work if the
	 * source contains less than page size fragments
	 */
	pipesize = pagesize * (iov_count + buf->count + 1) + out->len;

	if (llp->size < pipesize) {
		if (llp->can_grow) {
			res = fcntl(llp->pipe[0], F_SETPIPE_SZ, pipesize);
			if (res == -1) {
				res = grow_pipe_to_max(llp->pipe[0]);
				if (res > 0)
					llp->size = res;
				llp->can_grow = 0;
				goto fallback;
			}
			llp->size = res;
		}
		if (llp->size < pipesize)
			goto fallback;
	}


	res = vmsplice(llp->pipe[1], iov, iov_count, SPLICE_F_NONBLOCK);
	if (res == -1)
		goto fallback;

	if (res != headerlen) {
		res = -EIO;
		fuse_log(FUSE_LOG_ERR, "fuse: short vmsplice to pipe: %u/%zu\n", res,
			headerlen);
		goto clear_pipe;
	}

	pipe_buf.buf[0].flags = FUSE_BUF_IS_FD;
	pipe_buf.buf[0].fd = llp->pipe[1];

	res = fuse_buf_copy(&pipe_buf, buf,
			    FUSE_BUF_FORCE_SPLICE | FUSE_BUF_SPLICE_NONBLOCK);
	if (res < 0) {
		if (res == -EAGAIN || res == -EINVAL) {
			/*
			 * Should only get EAGAIN on kernels with
			 * broken SPLICE_F_NONBLOCK support (<=
			 * 2.6.35) where this error or a short read is
			 * returned even if the pipe itself is not
			 * full
			 *
			 * EINVAL might mean that splice can't handle
			 * this combination of input and output.
			 */
			if (res == -EAGAIN)
				se->broken_splice_nonblock = 1;

			pthread_setspecific(se->pipe_key, NULL);
			fuse_ll_pipe_free(llp);
			goto fallback;
		}
		res = -res;
		goto clear_pipe;
	}

	if (res != 0 && res < len) {
		struct fuse_bufvec mem_buf = FUSE_BUFVEC_INIT(len);
		void *mbuf;
		size_t now_len = res;
		/*
		 * For regular files a short count is either
		 *  1) due to EOF, or
		 *  2) because of broken SPLICE_F_NONBLOCK (see above)
		 *
		 * For other inputs it's possible that we overflowed
		 * the pipe because of small buffer fragments.
		 */

		res = posix_memalign(&mbuf, pagesize, len);
		if (res != 0)
			goto clear_pipe;

		mem_buf.buf[0].mem = mbuf;
		mem_buf.off = now_len;
		res = fuse_buf_copy(&mem_buf, buf, 0);
		if (res > 0) {
			char *tmpbuf;
			size_t extra_len = res;
			/*
			 * Trickiest case: got more data.  Need to get
			 * back the data from the pipe and then fall
			 * back to regular write.
			 */
			tmpbuf = malloc(headerlen);
			if (tmpbuf == NULL) {
				free(mbuf);
				res = ENOMEM;
				goto clear_pipe;
			}
			res = read_back(llp->pipe[0], tmpbuf, headerlen);
			free(tmpbuf);
			if (res != 0) {
				free(mbuf);
				goto clear_pipe;
			}
			res = read_back(llp->pipe[0], mbuf, now_len);
			if (res != 0) {
				free(mbuf);
				goto clear_pipe;
			}
			len = now_len + extra_len;
			iov[iov_count].iov_base = mbuf;
			iov[iov_count].iov_len = len;
			iov_count++;
			res = fuse_send_msg(se, ch, iov, iov_count);
			free(mbuf);
			return res;
		}
		free(mbuf);
		res = now_len;
	}
	len = res;
	out->len = headerlen + len;

	if (se->debug) {
		fuse_log(FUSE_LOG_DEBUG,
			"   unique: %llu, success, outsize: %i (splice)\n",
			(unsigned long long) out->unique, out->len);
	}

	splice_flags = 0;
	if ((flags & FUSE_BUF_SPLICE_MOVE) &&
	    (se->conn.want & FUSE_CAP_SPLICE_MOVE))
		splice_flags |= SPLICE_F_MOVE;

	res = splice(llp->pipe[0], NULL, ch ? ch->fd : se->fd,
		     NULL, out->len, splice_flags);
	if (res == -1) {
		res = -errno;
		perror("fuse: splice from pipe");
		goto clear_pipe;
	}
	if (res != out->len) {
		res = -EIO;
		fuse_log(FUSE_LOG_ERR, "fuse: short splice from pipe: %u/%u\n",
			res, out->len);
		goto clear_pipe;
	}
	return 0;

clear_pipe:
	fuse_ll_clear_pipe(se);
	return res;

fallback:
	return fuse_send_data_iov_fallback(se, ch, iov, iov_count, buf, len);
}
#else
static int fuse_send_data_iov(struct fuse_session *se, struct fuse_chan *ch,
			       struct iovec *iov, int iov_count,
			       struct fuse_bufvec *buf, unsigned int flags)
{
	size_t len = fuse_buf_size(buf);
	(void) flags;

	return fuse_send_data_iov_fallback(se, ch, iov, iov_count, buf, len);
}
#endif

int fuse_reply_data(fuse_req_t req, struct fuse_bufvec *bufv,
		    enum fuse_buf_copy_flags flags)
{
	struct iovec iov[2];
	struct fuse_out_header out;
	int res;

	iov[0].iov_base = &out;
	iov[0].iov_len = sizeof(struct fuse_out_header);

	out.unique = req->unique;
	out.error = 0;

	res = fuse_send_data_iov(req->se, req->ch, iov, 1, bufv, flags);
	if (res <= 0) {
		fuse_free_req(req);
		return res;
	} else {
		return fuse_reply_err(req, res);
	}
}

int fuse_reply_statfs(fuse_req_t req, const struct statvfs *stbuf)
{
	struct fuse_statfs_out arg;
	size_t size = req->se->conn.proto_minor < 4 ?
		FUSE_COMPAT_STATFS_SIZE : sizeof(arg);

	memset(&arg, 0, sizeof(arg));
	convert_statfs(stbuf, &arg.st);

	return send_reply_ok(req, &arg, size);
}

int fuse_reply_xattr(fuse_req_t req, size_t count)
{
	struct fuse_getxattr_out arg;

	memset(&arg, 0, sizeof(arg));
	arg.size = count;

	return send_reply_ok(req, &arg, sizeof(arg));
}

int fuse_reply_lock(fuse_req_t req, const struct flock *lock)
{
	struct fuse_lk_out arg;

	memset(&arg, 0, sizeof(arg));
	arg.lk.type = lock->l_type;
	if (lock->l_type != F_UNLCK) {
		arg.lk.start = lock->l_start;
		if (lock->l_len == 0)
			arg.lk.end = OFFSET_MAX;
		else
			arg.lk.end = lock->l_start + lock->l_len - 1;
	}
	arg.lk.pid = lock->l_pid;
	return send_reply_ok(req, &arg, sizeof(arg));
}

int fuse_reply_bmap(fuse_req_t req, uint64_t idx)
{
	struct fuse_bmap_out arg;

	memset(&arg, 0, sizeof(arg));
	arg.block = idx;

	return send_reply_ok(req, &arg, sizeof(arg));
}

static struct fuse_ioctl_iovec *fuse_ioctl_iovec_copy(const struct iovec *iov,
						      size_t count)
{
	struct fuse_ioctl_iovec *fiov;
	size_t i;

	fiov = malloc(sizeof(fiov[0]) * count);
	if (!fiov)
		return NULL;

	for (i = 0; i < count; i++) {
		fiov[i].base = (uintptr_t) iov[i].iov_base;
		fiov[i].len = iov[i].iov_len;
	}

	return fiov;
}

int fuse_reply_ioctl_retry(fuse_req_t req,
			   const struct iovec *in_iov, size_t in_count,
			   const struct iovec *out_iov, size_t out_count)
{
	struct fuse_ioctl_out arg;
	struct fuse_ioctl_iovec *in_fiov = NULL;
	struct fuse_ioctl_iovec *out_fiov = NULL;
	struct iovec iov[4];
	size_t count = 1;
	int res;

	memset(&arg, 0, sizeof(arg));
	arg.flags |= FUSE_IOCTL_RETRY;
	arg.in_iovs = in_count;
	arg.out_iovs = out_count;
	iov[count].iov_base = &arg;
	iov[count].iov_len = sizeof(arg);
	count++;

	if (req->se->conn.proto_minor < 16) {
		if (in_count) {
			iov[count].iov_base = (void *)in_iov;
			iov[count].iov_len = sizeof(in_iov[0]) * in_count;
			count++;
		}

		if (out_count) {
			iov[count].iov_base = (void *)out_iov;
			iov[count].iov_len = sizeof(out_iov[0]) * out_count;
			count++;
		}
	} else {
		/* Can't handle non-compat 64bit ioctls on 32bit */
		if (sizeof(void *) == 4 && req->ioctl_64bit) {
			res = fuse_reply_err(req, EINVAL);
			goto out;
		}

		if (in_count) {
			in_fiov = fuse_ioctl_iovec_copy(in_iov, in_count);
			if (!in_fiov)
				goto enomem;

			iov[count].iov_base = (void *)in_fiov;
			iov[count].iov_len = sizeof(in_fiov[0]) * in_count;
			count++;
		}
		if (out_count) {
			out_fiov = fuse_ioctl_iovec_copy(out_iov, out_count);
			if (!out_fiov)
				goto enomem;

			iov[count].iov_base = (void *)out_fiov;
			iov[count].iov_len = sizeof(out_fiov[0]) * out_count;
			count++;
		}
	}

	res = send_reply_iov(req, 0, iov, count);
out:
	free(in_fiov);
	free(out_fiov);

	return res;

enomem:
	res = fuse_reply_err(req, ENOMEM);
	goto out;
}

int fuse_reply_ioctl(fuse_req_t req, int result, const void *buf, size_t size)
{
	struct fuse_ioctl_out arg;
	struct iovec iov[3];
	size_t count = 1;

	memset(&arg, 0, sizeof(arg));
	arg.result = result;
	iov[count].iov_base = &arg;
	iov[count].iov_len = sizeof(arg);
	count++;

	if (size) {
		iov[count].iov_base = (char *) buf;
		iov[count].iov_len = size;
		count++;
	}

	return send_reply_iov(req, 0, iov, count);
}

int fuse_reply_ioctl_iov(fuse_req_t req, int result, const struct iovec *iov,
			 int count)
{
	struct iovec *padded_iov;
	struct fuse_ioctl_out arg;
	int res;

	padded_iov = malloc((count + 2) * sizeof(struct iovec));
	if (padded_iov == NULL)
		return fuse_reply_err(req, ENOMEM);

	memset(&arg, 0, sizeof(arg));
	arg.result = result;
	padded_iov[1].iov_base = &arg;
	padded_iov[1].iov_len = sizeof(arg);

	memcpy(&padded_iov[2], iov, count * sizeof(struct iovec));

	res = send_reply_iov(req, 0, padded_iov, count + 2);
	free(padded_iov);

	return res;
}

int fuse_reply_poll(fuse_req_t req, unsigned revents)
{
	struct fuse_poll_out arg;

	memset(&arg, 0, sizeof(arg));
	arg.revents = revents;

	return send_reply_ok(req, &arg, sizeof(arg));
}

int fuse_reply_lseek(fuse_req_t req, off_t off)
{
	struct fuse_lseek_out arg;

	memset(&arg, 0, sizeof(arg));
	arg.offset = off;

	return send_reply_ok(req, &arg, sizeof(arg));
}

static void do_lookup(fuse_req_t req, fuse_ino_t nodeid, const void *inarg)
{
	char *name = (char *) inarg;

	if (req->se->op.lookup)
		req->se->op.lookup(req, nodeid, name);
	else
		fuse_reply_err(req, ENOSYS);
}

static void do_forget(fuse_req_t req, fuse_ino_t nodeid, const void *inarg)
{
	struct fuse_forget_in *arg = (struct fuse_forget_in *) inarg;

	if (req->se->op.forget)
		req->se->op.forget(req, nodeid, arg->nlookup);
	else
		fuse_reply_none(req);
}

static void do_batch_forget(fuse_req_t req, fuse_ino_t nodeid,
			    const void *inarg)
{
	struct fuse_batch_forget_in *arg = (void *) inarg;
	struct fuse_forget_one *param = (void *) PARAM(arg);
	unsigned int i;

	(void) nodeid;

	if (req->se->op.forget_multi) {
		req->se->op.forget_multi(req, arg->count,
				     (struct fuse_forget_data *) param);
	} else if (req->se->op.forget) {
		for (i = 0; i < arg->count; i++) {
			struct fuse_forget_one *forget = &param[i];
			struct fuse_req *dummy_req;

			dummy_req = fuse_ll_alloc_req(req->se);
			if (dummy_req == NULL)
				break;

			dummy_req->unique = req->unique;
			dummy_req->ctx = req->ctx;
			dummy_req->ch = NULL;

			req->se->op.forget(dummy_req, forget->nodeid,
					  forget->nlookup);
		}
		fuse_reply_none(req);
	} else {
		fuse_reply_none(req);
	}
}

static void do_getattr(fuse_req_t req, fuse_ino_t nodeid, const void *inarg)
{
	struct fuse_file_info *fip = NULL;
	struct fuse_file_info fi;

	if (req->se->conn.proto_minor >= 9) {
		struct fuse_getattr_in *arg = (struct fuse_getattr_in *) inarg;

		if (arg->getattr_flags & FUSE_GETATTR_FH) {
			memset(&fi, 0, sizeof(fi));
			fi.fh = arg->fh;
			fip = &fi;
		}
	}

	if (req->se->op.getattr)
		req->se->op.getattr(req, nodeid, fip);
	else
		fuse_reply_err(req, ENOSYS);
}

static void do_setattr(fuse_req_t req, fuse_ino_t nodeid, const void *inarg)
{
	struct fuse_setattr_in *arg = (struct fuse_setattr_in *) inarg;

	if (req->se->op.setattr) {
		struct fuse_file_info *fi = NULL;
		struct fuse_file_info fi_store;
		struct stat stbuf;
		memset(&stbuf, 0, sizeof(stbuf));
		convert_attr(arg, &stbuf);
		if (arg->valid & FATTR_FH) {
			arg->valid &= ~FATTR_FH;
			memset(&fi_store, 0, sizeof(fi_store));
			fi = &fi_store;
			fi->fh = arg->fh;
		}
		arg->valid &=
			FUSE_SET_ATTR_MODE	|
			FUSE_SET_ATTR_UID	|
			FUSE_SET_ATTR_GID	|
			FUSE_SET_ATTR_SIZE	|
			FUSE_SET_ATTR_ATIME	|
			FUSE_SET_ATTR_MTIME	|
			FUSE_SET_ATTR_ATIME_NOW	|
			FUSE_SET_ATTR_MTIME_NOW |
			FUSE_SET_ATTR_CTIME;

		req->se->op.setattr(req, nodeid, &stbuf, arg->valid, fi);
	} else
		fuse_reply_err(req, ENOSYS);
}

static void do_access(fuse_req_t req, fuse_ino_t nodeid, const void *inarg)
{
	struct fuse_access_in *arg = (struct fuse_access_in *) inarg;

	if (req->se->op.access)
		req->se->op.access(req, nodeid, arg->mask);
	else
		fuse_reply_err(req, ENOSYS);
}

static void do_readlink(fuse_req_t req, fuse_ino_t nodeid, const void *inarg)
{
	(void) inarg;

	if (req->se->op.readlink)
		req->se->op.readlink(req, nodeid);
	else
		fuse_reply_err(req, ENOSYS);
}

static void do_mknod(fuse_req_t req, fuse_ino_t nodeid, const void *inarg)
{
	struct fuse_mknod_in *arg = (struct fuse_mknod_in *) inarg;
	char *name = PARAM(arg);

	if (req->se->conn.proto_minor >= 12)
		req->ctx.umask = arg->umask;
	else
		name = (char *) inarg + FUSE_COMPAT_MKNOD_IN_SIZE;

	if (req->se->op.mknod)
		req->se->op.mknod(req, nodeid, name, arg->mode, arg->rdev);
	else
		fuse_reply_err(req, ENOSYS);
}

static void do_mkdir(fuse_req_t req, fuse_ino_t nodeid, const void *inarg)
{
	struct fuse_mkdir_in *arg = (struct fuse_mkdir_in *) inarg;

	if (req->se->conn.proto_minor >= 12)
		req->ctx.umask = arg->umask;

	if (req->se->op.mkdir)
		req->se->op.mkdir(req, nodeid, PARAM(arg), arg->mode);
	else
		fuse_reply_err(req, ENOSYS);
}

static void do_unlink(fuse_req_t req, fuse_ino_t nodeid, const void *inarg)
{
	char *name = (char *) inarg;

	if (req->se->op.unlink)
		req->se->op.unlink(req, nodeid, name);
	else
		fuse_reply_err(req, ENOSYS);
}

static void do_rmdir(fuse_req_t req, fuse_ino_t nodeid, const void *inarg)
{
	char *name = (char *) inarg;

	if (req->se->op.rmdir)
		req->se->op.rmdir(req, nodeid, name);
	else
		fuse_reply_err(req, ENOSYS);
}

static void do_symlink(fuse_req_t req, fuse_ino_t nodeid, const void *inarg)
{
	char *name = (char *) inarg;
	char *linkname = ((char *) inarg) + strlen((char *) inarg) + 1;

	if (req->se->op.symlink)
		req->se->op.symlink(req, linkname, nodeid, name);
	else
		fuse_reply_err(req, ENOSYS);
}

static void do_rename(fuse_req_t req, fuse_ino_t nodeid, const void *inarg)
{
	struct fuse_rename_in *arg = (struct fuse_rename_in *) inarg;
	char *oldname = PARAM(arg);
	char *newname = oldname + strlen(oldname) + 1;

	if (req->se->op.rename)
		req->se->op.rename(req, nodeid, oldname, arg->newdir, newname,
				  0);
	else
		fuse_reply_err(req, ENOSYS);
}

static void do_rename2(fuse_req_t req, fuse_ino_t nodeid, const void *inarg)
{
	struct fuse_rename2_in *arg = (struct fuse_rename2_in *) inarg;
	char *oldname = PARAM(arg);
	char *newname = oldname + strlen(oldname) + 1;

	if (req->se->op.rename)
		req->se->op.rename(req, nodeid, oldname, arg->newdir, newname,
				  arg->flags);
	else
		fuse_reply_err(req, ENOSYS);
}

static void do_link(fuse_req_t req, fuse_ino_t nodeid, const void *inarg)
{
	struct fuse_link_in *arg = (struct fuse_link_in *) inarg;

	if (req->se->op.link)
		req->se->op.link(req, arg->oldnodeid, nodeid, PARAM(arg));
	else
		fuse_reply_err(req, ENOSYS);
}

static void do_create(fuse_req_t req, fuse_ino_t nodeid, const void *inarg)
{
	struct fuse_create_in *arg = (struct fuse_create_in *) inarg;

	if (req->se->op.create) {
		struct fuse_file_info fi;
		char *name = PARAM(arg);

		memset(&fi, 0, sizeof(fi));
		fi.flags = arg->flags;

		if (req->se->conn.proto_minor >= 12)
			req->ctx.umask = arg->umask;
		else
			name = (char *) inarg + sizeof(struct fuse_open_in);

		req->se->op.create(req, nodeid, name, arg->mode, &fi);
	} else
		fuse_reply_err(req, ENOSYS);
}

static void do_open(fuse_req_t req, fuse_ino_t nodeid, const void *inarg)
{
	struct fuse_open_in *arg = (struct fuse_open_in *) inarg;
	struct fuse_file_info fi;

	memset(&fi, 0, sizeof(fi));
	fi.flags = arg->flags;

	if (req->se->op.open)
		req->se->op.open(req, nodeid, &fi);
	else
		fuse_reply_open(req, &fi);
}

static void do_read(fuse_req_t req, fuse_ino_t nodeid, const void *inarg)
{
	struct fuse_read_in *arg = (struct fuse_read_in *) inarg;

	if (req->se->op.read) {
		struct fuse_file_info fi;

		memset(&fi, 0, sizeof(fi));
		fi.fh = arg->fh;
		if (req->se->conn.proto_minor >= 9) {
			fi.lock_owner = arg->lock_owner;
			fi.flags = arg->flags;
		}
		req->se->op.read(req, nodeid, arg->size, arg->offset, &fi);
	} else
		fuse_reply_err(req, ENOSYS);
}

static void do_write(fuse_req_t req, fuse_ino_t nodeid, const void *inarg)
{
	struct fuse_write_in *arg = (struct fuse_write_in *) inarg;
	struct fuse_file_info fi;
	char *param;

	memset(&fi, 0, sizeof(fi));
	fi.fh = arg->fh;
	fi.writepage = (arg->write_flags & FUSE_WRITE_CACHE) != 0;

	if (req->se->conn.proto_minor < 9) {
		param = ((char *) arg) + FUSE_COMPAT_WRITE_IN_SIZE;
	} else {
		fi.lock_owner = arg->lock_owner;
		fi.flags = arg->flags;
		param = PARAM(arg);
	}

	if (req->se->op.write)
		req->se->op.write(req, nodeid, param, arg->size,
				 arg->offset, &fi);
	else
		fuse_reply_err(req, ENOSYS);
}

static void do_write_buf(fuse_req_t req, fuse_ino_t nodeid, const void *inarg,
			 const struct fuse_buf *ibuf)
{
	struct fuse_session *se = req->se;
	struct fuse_bufvec bufv = {
		.buf[0] = *ibuf,
		.count = 1,
	};
	struct fuse_write_in *arg = (struct fuse_write_in *) inarg;
	struct fuse_file_info fi;

	memset(&fi, 0, sizeof(fi));
	fi.fh = arg->fh;
	fi.writepage = arg->write_flags & FUSE_WRITE_CACHE;

	if (se->conn.proto_minor < 9) {
		bufv.buf[0].mem = ((char *) arg) + FUSE_COMPAT_WRITE_IN_SIZE;
		bufv.buf[0].size -= sizeof(struct fuse_in_header) +
			FUSE_COMPAT_WRITE_IN_SIZE;
		assert(!(bufv.buf[0].flags & FUSE_BUF_IS_FD));
	} else {
		fi.lock_owner = arg->lock_owner;
		fi.flags = arg->flags;
		if (!(bufv.buf[0].flags & FUSE_BUF_IS_FD))
			bufv.buf[0].mem = PARAM(arg);

		bufv.buf[0].size -= sizeof(struct fuse_in_header) +
			sizeof(struct fuse_write_in);
	}
	if (bufv.buf[0].size < arg->size) {
		fuse_log(FUSE_LOG_ERR, "fuse: do_write_buf: buffer size too small\n");
		fuse_reply_err(req, EIO);
		goto out;
	}
	bufv.buf[0].size = arg->size;

	se->op.write_buf(req, nodeid, &bufv, arg->offset, &fi);

out:
	/* Need to reset the pipe if ->write_buf() didn't consume all data */
	if ((ibuf->flags & FUSE_BUF_IS_FD) && bufv.idx < bufv.count)
		fuse_ll_clear_pipe(se);
}

static void do_flush(fuse_req_t req, fuse_ino_t nodeid, const void *inarg)
{
	struct fuse_flush_in *arg = (struct fuse_flush_in *) inarg;
	struct fuse_file_info fi;

	memset(&fi, 0, sizeof(fi));
	fi.fh = arg->fh;
	fi.flush = 1;
	if (req->se->conn.proto_minor >= 7)
		fi.lock_owner = arg->lock_owner;

	if (req->se->op.flush)
		req->se->op.flush(req, nodeid, &fi);
	else
		fuse_reply_err(req, ENOSYS);
}

static void do_release(fuse_req_t req, fuse_ino_t nodeid, const void *inarg)
{
	struct fuse_release_in *arg = (struct fuse_release_in *) inarg;
	struct fuse_file_info fi;

	memset(&fi, 0, sizeof(fi));
	fi.flags = arg->flags;
	fi.fh = arg->fh;
	if (req->se->conn.proto_minor >= 8) {
		fi.flush = (arg->release_flags & FUSE_RELEASE_FLUSH) ? 1 : 0;
		fi.lock_owner = arg->lock_owner;
	}
	if (arg->release_flags & FUSE_RELEASE_FLOCK_UNLOCK) {
		fi.flock_release = 1;
		fi.lock_owner = arg->lock_owner;
	}

	if (req->se->op.release)
		req->se->op.release(req, nodeid, &fi);
	else
		fuse_reply_err(req, 0);
}

static void do_fsync(fuse_req_t req, fuse_ino_t nodeid, const void *inarg)
{
	struct fuse_fsync_in *arg = (struct fuse_fsync_in *) inarg;
	struct fuse_file_info fi;
	int datasync = arg->fsync_flags & 1;

	memset(&fi, 0, sizeof(fi));
	fi.fh = arg->fh;

	if (req->se->op.fsync)
		req->se->op.fsync(req, nodeid, datasync, &fi);
	else
		fuse_reply_err(req, ENOSYS);
}

static void do_opendir(fuse_req_t req, fuse_ino_t nodeid, const void *inarg)
{
	struct fuse_open_in *arg = (struct fuse_open_in *) inarg;
	struct fuse_file_info fi;

	memset(&fi, 0, sizeof(fi));
	fi.flags = arg->flags;

	if (req->se->op.opendir)
		req->se->op.opendir(req, nodeid, &fi);
	else
		fuse_reply_open(req, &fi);
}

static void do_readdir(fuse_req_t req, fuse_ino_t nodeid, const void *inarg)
{
	struct fuse_read_in *arg = (struct fuse_read_in *) inarg;
	struct fuse_file_info fi;

	memset(&fi, 0, sizeof(fi));
	fi.fh = arg->fh;

	if (req->se->op.readdir)
		req->se->op.readdir(req, nodeid, arg->size, arg->offset, &fi);
	else
		fuse_reply_err(req, ENOSYS);
}

static void do_readdirplus(fuse_req_t req, fuse_ino_t nodeid, const void *inarg)
{
	struct fuse_read_in *arg = (struct fuse_read_in *) inarg;
	struct fuse_file_info fi;

	memset(&fi, 0, sizeof(fi));
	fi.fh = arg->fh;

	if (req->se->op.readdirplus)
		req->se->op.readdirplus(req, nodeid, arg->size, arg->offset, &fi);
	else
		fuse_reply_err(req, ENOSYS);
}

static void do_releasedir(fuse_req_t req, fuse_ino_t nodeid, const void *inarg)
{
	struct fuse_release_in *arg = (struct fuse_release_in *) inarg;
	struct fuse_file_info fi;

	memset(&fi, 0, sizeof(fi));
	fi.flags = arg->flags;
	fi.fh = arg->fh;

	if (req->se->op.releasedir)
		req->se->op.releasedir(req, nodeid, &fi);
	else
		fuse_reply_err(req, 0);
}

static void do_fsyncdir(fuse_req_t req, fuse_ino_t nodeid, const void *inarg)
{
	struct fuse_fsync_in *arg = (struct fuse_fsync_in *) inarg;
	struct fuse_file_info fi;
	int datasync = arg->fsync_flags & 1;

	memset(&fi, 0, sizeof(fi));
	fi.fh = arg->fh;

	if (req->se->op.fsyncdir)
		req->se->op.fsyncdir(req, nodeid, datasync, &fi);
	else
		fuse_reply_err(req, ENOSYS);
}

static void do_statfs(fuse_req_t req, fuse_ino_t nodeid, const void *inarg)
{
	(void) nodeid;
	(void) inarg;

	if (req->se->op.statfs)
		req->se->op.statfs(req, nodeid);
	else {
		struct statvfs buf = {
			.f_namemax = 255,
			.f_bsize = 512,
		};
		fuse_reply_statfs(req, &buf);
	}
}

static void do_setxattr(fuse_req_t req, fuse_ino_t nodeid, const void *inarg)
{
	struct fuse_setxattr_in *arg = (struct fuse_setxattr_in *) inarg;
	char *name = PARAM(arg);
	char *value = name + strlen(name) + 1;

	if (req->se->op.setxattr)
		req->se->op.setxattr(req, nodeid, name, value, arg->size,
				    arg->flags);
	else
		fuse_reply_err(req, ENOSYS);
}

static void do_getxattr(fuse_req_t req, fuse_ino_t nodeid, const void *inarg)
{
	struct fuse_getxattr_in *arg = (struct fuse_getxattr_in *) inarg;

	if (req->se->op.getxattr)
		req->se->op.getxattr(req, nodeid, PARAM(arg), arg->size);
	else
		fuse_reply_err(req, ENOSYS);
}

static void do_listxattr(fuse_req_t req, fuse_ino_t nodeid, const void *inarg)
{
	struct fuse_getxattr_in *arg = (struct fuse_getxattr_in *) inarg;

	if (req->se->op.listxattr)
		req->se->op.listxattr(req, nodeid, arg->size);
	else
		fuse_reply_err(req, ENOSYS);
}

static void do_removexattr(fuse_req_t req, fuse_ino_t nodeid, const void *inarg)
{
	char *name = (char *) inarg;

	if (req->se->op.removexattr)
		req->se->op.removexattr(req, nodeid, name);
	else
		fuse_reply_err(req, ENOSYS);
}

static void convert_fuse_file_lock(struct fuse_file_lock *fl,
				   struct flock *flock)
{
	memset(flock, 0, sizeof(struct flock));
	flock->l_type = fl->type;
	flock->l_whence = SEEK_SET;
	flock->l_start = fl->start;
	if (fl->end == OFFSET_MAX)
		flock->l_len = 0;
	else
		flock->l_len = fl->end - fl->start + 1;
	flock->l_pid = fl->pid;
}

static void do_getlk(fuse_req_t req, fuse_ino_t nodeid, const void *inarg)
{
	struct fuse_lk_in *arg = (struct fuse_lk_in *) inarg;
	struct fuse_file_info fi;
	struct flock flock;

	memset(&fi, 0, sizeof(fi));
	fi.fh = arg->fh;
	fi.lock_owner = arg->owner;

	convert_fuse_file_lock(&arg->lk, &flock);
	if (req->se->op.getlk)
		req->se->op.getlk(req, nodeid, &fi, &flock);
	else
		fuse_reply_err(req, ENOSYS);
}

static void do_setlk_common(fuse_req_t req, fuse_ino_t nodeid,
			    const void *inarg, int sleep)
{
	struct fuse_lk_in *arg = (struct fuse_lk_in *) inarg;
	struct fuse_file_info fi;
	struct flock flock;

	memset(&fi, 0, sizeof(fi));
	fi.fh = arg->fh;
	fi.lock_owner = arg->owner;

	if (arg->lk_flags & FUSE_LK_FLOCK) {
		int op = 0;

		switch (arg->lk.type) {
		case F_RDLCK:
			op = LOCK_SH;
			break;
		case F_WRLCK:
			op = LOCK_EX;
			break;
		case F_UNLCK:
			op = LOCK_UN;
			break;
		}
		if (!sleep)
			op |= LOCK_NB;

		if (req->se->op.flock)
			req->se->op.flock(req, nodeid, &fi, op);
		else
			fuse_reply_err(req, ENOSYS);
	} else {
		convert_fuse_file_lock(&arg->lk, &flock);
		if (req->se->op.setlk)
			req->se->op.setlk(req, nodeid, &fi, &flock, sleep);
		else
			fuse_reply_err(req, ENOSYS);
	}
}

static void do_setlk(fuse_req_t req, fuse_ino_t nodeid, const void *inarg)
{
	do_setlk_common(req, nodeid, inarg, 0);
}

static void do_setlkw(fuse_req_t req, fuse_ino_t nodeid, const void *inarg)
{
	do_setlk_common(req, nodeid, inarg, 1);
}

static int find_interrupted(struct fuse_session *se, struct fuse_req *req)
{
	struct fuse_req *curr;

	for (curr = se->list.next; curr != &se->list; curr = curr->next) {
		if (curr->unique == req->u.i.unique) {
			fuse_interrupt_func_t func;
			void *data;

			curr->ctr++;
			pthread_mutex_unlock(&se->lock);

			/* Ugh, ugly locking */
			pthread_mutex_lock(&curr->lock);
			pthread_mutex_lock(&se->lock);
			curr->interrupted = 1;
			func = curr->u.ni.func;
			data = curr->u.ni.data;
			pthread_mutex_unlock(&se->lock);
			if (func)
				func(curr, data);
			pthread_mutex_unlock(&curr->lock);

			pthread_mutex_lock(&se->lock);
			curr->ctr--;
			if (!curr->ctr)
				destroy_req(curr);

			return 1;
		}
	}
	for (curr = se->interrupts.next; curr != &se->interrupts;
	     curr = curr->next) {
		if (curr->u.i.unique == req->u.i.unique)
			return 1;
	}
	return 0;
}

static void do_interrupt(fuse_req_t req, fuse_ino_t nodeid, const void *inarg)
{
	struct fuse_interrupt_in *arg = (struct fuse_interrupt_in *) inarg;
	struct fuse_session *se = req->se;

	(void) nodeid;
	if (se->debug)
		fuse_log(FUSE_LOG_DEBUG, "INTERRUPT: %llu\n",
			(unsigned long long) arg->unique);

	req->u.i.unique = arg->unique;

	pthread_mutex_lock(&se->lock);
	if (find_interrupted(se, req))
		destroy_req(req);
	else
		list_add_req(req, &se->interrupts);
	pthread_mutex_unlock(&se->lock);
}

static struct fuse_req *check_interrupt(struct fuse_session *se,
					struct fuse_req *req)
{
	struct fuse_req *curr;

	for (curr = se->interrupts.next; curr != &se->interrupts;
	     curr = curr->next) {
		if (curr->u.i.unique == req->unique) {
			req->interrupted = 1;
			list_del_req(curr);
			free(curr);
			return NULL;
		}
	}
	curr = se->interrupts.next;
	if (curr != &se->interrupts) {
		list_del_req(curr);
		list_init_req(curr);
		return curr;
	} else
		return NULL;
}

static void do_bmap(fuse_req_t req, fuse_ino_t nodeid, const void *inarg)
{
	struct fuse_bmap_in *arg = (struct fuse_bmap_in *) inarg;

	if (req->se->op.bmap)
		req->se->op.bmap(req, nodeid, arg->blocksize, arg->block);
	else
		fuse_reply_err(req, ENOSYS);
}

static void do_ioctl(fuse_req_t req, fuse_ino_t nodeid, const void *inarg)
{
	struct fuse_ioctl_in *arg = (struct fuse_ioctl_in *) inarg;
	unsigned int flags = arg->flags;
	void *in_buf = arg->in_size ? PARAM(arg) : NULL;
	struct fuse_file_info fi;

	if (flags & FUSE_IOCTL_DIR &&
	    !(req->se->conn.want & FUSE_CAP_IOCTL_DIR)) {
		fuse_reply_err(req, ENOTTY);
		return;
	}

	memset(&fi, 0, sizeof(fi));
	fi.fh = arg->fh;

	if (sizeof(void *) == 4 && req->se->conn.proto_minor >= 16 &&
	    !(flags & FUSE_IOCTL_32BIT)) {
		req->ioctl_64bit = 1;
	}

	if (req->se->op.ioctl)
		req->se->op.ioctl(req, nodeid, arg->cmd,
				 (void *)(uintptr_t)arg->arg, &fi, flags,
				 in_buf, arg->in_size, arg->out_size);
	else
		fuse_reply_err(req, ENOSYS);
}

void fuse_pollhandle_destroy(struct fuse_pollhandle *ph)
{
	free(ph);
}

static void do_poll(fuse_req_t req, fuse_ino_t nodeid, const void *inarg)
{
	struct fuse_poll_in *arg = (struct fuse_poll_in *) inarg;
	struct fuse_file_info fi;

	memset(&fi, 0, sizeof(fi));
	fi.fh = arg->fh;
	fi.poll_events = arg->events;

	if (req->se->op.poll) {
		struct fuse_pollhandle *ph = NULL;

		if (arg->flags & FUSE_POLL_SCHEDULE_NOTIFY) {
			ph = malloc(sizeof(struct fuse_pollhandle));
			if (ph == NULL) {
				fuse_reply_err(req, ENOMEM);
				return;
			}
			ph->kh = arg->kh;
			ph->se = req->se;
		}

		req->se->op.poll(req, nodeid, &fi, ph);
	} else {
		fuse_reply_err(req, ENOSYS);
	}
}

static void do_fallocate(fuse_req_t req, fuse_ino_t nodeid, const void *inarg)
{
	struct fuse_fallocate_in *arg = (struct fuse_fallocate_in *) inarg;
	struct fuse_file_info fi;

	memset(&fi, 0, sizeof(fi));
	fi.fh = arg->fh;

	if (req->se->op.fallocate)
		req->se->op.fallocate(req, nodeid, arg->mode, arg->offset, arg->length, &fi);
	else
		fuse_reply_err(req, ENOSYS);
}

static void do_copy_file_range(fuse_req_t req, fuse_ino_t nodeid_in, const void *inarg)
{
	struct fuse_copy_file_range_in *arg = (struct fuse_copy_file_range_in *) inarg;
	struct fuse_file_info fi_in, fi_out;

	memset(&fi_in, 0, sizeof(fi_in));
	fi_in.fh = arg->fh_in;

	memset(&fi_out, 0, sizeof(fi_out));
	fi_out.fh = arg->fh_out;


	if (req->se->op.copy_file_range)
		req->se->op.copy_file_range(req, nodeid_in, arg->off_in,
					    &fi_in, arg->nodeid_out,
					    arg->off_out, &fi_out, arg->len,
					    arg->flags);
	else
		fuse_reply_err(req, ENOSYS);
}

static void do_lseek(fuse_req_t req, fuse_ino_t nodeid, const void *inarg)
{
	struct fuse_lseek_in *arg = (struct fuse_lseek_in *) inarg;
	struct fuse_file_info fi;

	memset(&fi, 0, sizeof(fi));
	fi.fh = arg->fh;

	if (req->se->op.lseek)
		req->se->op.lseek(req, nodeid, arg->offset, arg->whence, &fi);
	else
		fuse_reply_err(req, ENOSYS);
}

static void do_init(fuse_req_t req, fuse_ino_t nodeid, const void *inarg)
{
	struct fuse_init_in *arg = (struct fuse_init_in *) inarg;
	struct fuse_init_out outarg;
	struct fuse_session *se = req->se;
	size_t bufsize = se->bufsize;
	size_t outargsize = sizeof(outarg);

	(void) nodeid;
	if (se->debug) {
		fuse_log(FUSE_LOG_DEBUG, "INIT: %u.%u\n", arg->major, arg->minor);
		if (arg->major == 7 && arg->minor >= 6) {
			fuse_log(FUSE_LOG_DEBUG, "flags=0x%08x\n", arg->flags);
			fuse_log(FUSE_LOG_DEBUG, "max_readahead=0x%08x\n",
				arg->max_readahead);
		}
	}
	se->conn.proto_major = arg->major;
	se->conn.proto_minor = arg->minor;
	se->conn.capable = 0;
	se->conn.want = 0;

	memset(&outarg, 0, sizeof(outarg));
	outarg.major = FUSE_KERNEL_VERSION;
	outarg.minor = FUSE_KERNEL_MINOR_VERSION;

	if (arg->major < 7) {
		fuse_log(FUSE_LOG_ERR, "fuse: unsupported protocol version: %u.%u\n",
			arg->major, arg->minor);
		fuse_reply_err(req, EPROTO);
		return;
	}

	if (arg->major > 7) {
		/* Wait for a second INIT request with a 7.X version */
		send_reply_ok(req, &outarg, sizeof(outarg));
		return;
	}

	if (arg->minor >= 6) {
		if (arg->max_readahead < se->conn.max_readahead)
			se->conn.max_readahead = arg->max_readahead;
		if (arg->flags & FUSE_ASYNC_READ)
			se->conn.capable |= FUSE_CAP_ASYNC_READ;
		if (arg->flags & FUSE_POSIX_LOCKS)
			se->conn.capable |= FUSE_CAP_POSIX_LOCKS;
		if (arg->flags & FUSE_ATOMIC_O_TRUNC)
			se->conn.capable |= FUSE_CAP_ATOMIC_O_TRUNC;
		if (arg->flags & FUSE_EXPORT_SUPPORT)
			se->conn.capable |= FUSE_CAP_EXPORT_SUPPORT;
		if (arg->flags & FUSE_DONT_MASK)
			se->conn.capable |= FUSE_CAP_DONT_MASK;
		if (arg->flags & FUSE_FLOCK_LOCKS)
			se->conn.capable |= FUSE_CAP_FLOCK_LOCKS;
		if (arg->flags & FUSE_AUTO_INVAL_DATA)
			se->conn.capable |= FUSE_CAP_AUTO_INVAL_DATA;
		if (arg->flags & FUSE_DO_READDIRPLUS)
			se->conn.capable |= FUSE_CAP_READDIRPLUS;
		if (arg->flags & FUSE_READDIRPLUS_AUTO)
			se->conn.capable |= FUSE_CAP_READDIRPLUS_AUTO;
		if (arg->flags & FUSE_ASYNC_DIO)
			se->conn.capable |= FUSE_CAP_ASYNC_DIO;
		if (arg->flags & FUSE_WRITEBACK_CACHE)
			se->conn.capable |= FUSE_CAP_WRITEBACK_CACHE;
		if (arg->flags & FUSE_NO_OPEN_SUPPORT)
			se->conn.capable |= FUSE_CAP_NO_OPEN_SUPPORT;
		if (arg->flags & FUSE_PARALLEL_DIROPS)
			se->conn.capable |= FUSE_CAP_PARALLEL_DIROPS;
		if (arg->flags & FUSE_POSIX_ACL)
			se->conn.capable |= FUSE_CAP_POSIX_ACL;
		if (arg->flags & FUSE_HANDLE_KILLPRIV)
			se->conn.capable |= FUSE_CAP_HANDLE_KILLPRIV;
		if (arg->flags & FUSE_NO_OPENDIR_SUPPORT)
			se->conn.capable |= FUSE_CAP_NO_OPENDIR_SUPPORT;
		if (!(arg->flags & FUSE_MAX_PAGES)) {
			size_t max_bufsize =
				FUSE_DEFAULT_MAX_PAGES_PER_REQ * getpagesize()
				+ FUSE_BUFFER_HEADER_SIZE;
			if (bufsize > max_bufsize) {
				bufsize = max_bufsize;
			}
		}
	} else {
		se->conn.max_readahead = 0;
	}

	if (se->conn.proto_minor >= 14) {
#ifdef HAVE_SPLICE
#ifdef HAVE_VMSPLICE
		se->conn.capable |= FUSE_CAP_SPLICE_WRITE | FUSE_CAP_SPLICE_MOVE;
#endif
		se->conn.capable |= FUSE_CAP_SPLICE_READ;
#endif
	}
	if (se->conn.proto_minor >= 18)
		se->conn.capable |= FUSE_CAP_IOCTL_DIR;

	/* Default settings for modern filesystems.
	 *
	 * Most of these capabilities were disabled by default in
	 * libfuse2 for backwards compatibility reasons. In libfuse3,
	 * we can finally enable them by default (as long as they're
	 * supported by the kernel).
	 */
#define LL_SET_DEFAULT(cond, cap) \
	if ((cond) && (se->conn.capable & (cap))) \
		se->conn.want |= (cap)
	LL_SET_DEFAULT(1, FUSE_CAP_ASYNC_READ);
	LL_SET_DEFAULT(1, FUSE_CAP_PARALLEL_DIROPS);
	LL_SET_DEFAULT(1, FUSE_CAP_AUTO_INVAL_DATA);
	LL_SET_DEFAULT(1, FUSE_CAP_HANDLE_KILLPRIV);
	LL_SET_DEFAULT(1, FUSE_CAP_ASYNC_DIO);
	LL_SET_DEFAULT(1, FUSE_CAP_IOCTL_DIR);
	LL_SET_DEFAULT(1, FUSE_CAP_ATOMIC_O_TRUNC);
	LL_SET_DEFAULT(se->op.write_buf, FUSE_CAP_SPLICE_READ);
	LL_SET_DEFAULT(se->op.getlk && se->op.setlk,
		       FUSE_CAP_POSIX_LOCKS);
	LL_SET_DEFAULT(se->op.flock, FUSE_CAP_FLOCK_LOCKS);
	LL_SET_DEFAULT(se->op.readdirplus, FUSE_CAP_READDIRPLUS);
	LL_SET_DEFAULT(se->op.readdirplus && se->op.readdir,
		       FUSE_CAP_READDIRPLUS_AUTO);
	se->conn.time_gran = 1;
	
	if (bufsize < FUSE_MIN_READ_BUFFER) {
		fuse_log(FUSE_LOG_ERR, "fuse: warning: buffer size too small: %zu\n",
			bufsize);
		bufsize = FUSE_MIN_READ_BUFFER;
	}
	se->bufsize = bufsize;

	if (se->conn.max_write > bufsize - FUSE_BUFFER_HEADER_SIZE)
		se->conn.max_write = bufsize - FUSE_BUFFER_HEADER_SIZE;

	se->got_init = 1;
	if (se->op.init)
		se->op.init(se->userdata, &se->conn);

	if (se->conn.want & (~se->conn.capable)) {
		fuse_log(FUSE_LOG_ERR, "fuse: error: filesystem requested capabilities "
			"0x%x that are not supported by kernel, aborting.\n",
			se->conn.want & (~se->conn.capable));
		fuse_reply_err(req, EPROTO);
		se->error = -EPROTO;
		fuse_session_exit(se);
		return;
	}

	unsigned max_read_mo = get_max_read(se->mo);
	if (se->conn.max_read != max_read_mo) {
		fuse_log(FUSE_LOG_ERR, "fuse: error: init() and fuse_session_new() "
			"requested different maximum read size (%u vs %u)\n",
			se->conn.max_read, max_read_mo);
		fuse_reply_err(req, EPROTO);
		se->error = -EPROTO;
		fuse_session_exit(se);
		return;
	}

	if (se->conn.max_write < bufsize - FUSE_BUFFER_HEADER_SIZE) {
		se->bufsize = se->conn.max_write + FUSE_BUFFER_HEADER_SIZE;
	}
	if (arg->flags & FUSE_MAX_PAGES) {
		outarg.flags |= FUSE_MAX_PAGES;
		outarg.max_pages = (se->conn.max_write - 1) / getpagesize() + 1;
	}

	/* Always enable big writes, this is superseded
	   by the max_write option */
	outarg.flags |= FUSE_BIG_WRITES;

	if (se->conn.want & FUSE_CAP_ASYNC_READ)
		outarg.flags |= FUSE_ASYNC_READ;
	if (se->conn.want & FUSE_CAP_POSIX_LOCKS)
		outarg.flags |= FUSE_POSIX_LOCKS;
	if (se->conn.want & FUSE_CAP_ATOMIC_O_TRUNC)
		outarg.flags |= FUSE_ATOMIC_O_TRUNC;
	if (se->conn.want & FUSE_CAP_EXPORT_SUPPORT)
		outarg.flags |= FUSE_EXPORT_SUPPORT;
	if (se->conn.want & FUSE_CAP_DONT_MASK)
		outarg.flags |= FUSE_DONT_MASK;
	if (se->conn.want & FUSE_CAP_FLOCK_LOCKS)
		outarg.flags |= FUSE_FLOCK_LOCKS;
	if (se->conn.want & FUSE_CAP_AUTO_INVAL_DATA)
		outarg.flags |= FUSE_AUTO_INVAL_DATA;
	if (se->conn.want & FUSE_CAP_READDIRPLUS)
		outarg.flags |= FUSE_DO_READDIRPLUS;
	if (se->conn.want & FUSE_CAP_READDIRPLUS_AUTO)
		outarg.flags |= FUSE_READDIRPLUS_AUTO;
	if (se->conn.want & FUSE_CAP_ASYNC_DIO)
		outarg.flags |= FUSE_ASYNC_DIO;
	if (se->conn.want & FUSE_CAP_WRITEBACK_CACHE)
		outarg.flags |= FUSE_WRITEBACK_CACHE;
	if (se->conn.want & FUSE_CAP_POSIX_ACL)
		outarg.flags |= FUSE_POSIX_ACL;
	outarg.max_readahead = se->conn.max_readahead;
	outarg.max_write = se->conn.max_write;
	if (se->conn.proto_minor >= 13) {
		if (se->conn.max_background >= (1 << 16))
			se->conn.max_background = (1 << 16) - 1;
		if (se->conn.congestion_threshold > se->conn.max_background)
			se->conn.congestion_threshold = se->conn.max_background;
		if (!se->conn.congestion_threshold) {
			se->conn.congestion_threshold =
				se->conn.max_background * 3 / 4;
		}

		outarg.max_background = se->conn.max_background;
		outarg.congestion_threshold = se->conn.congestion_threshold;
	}
	if (se->conn.proto_minor >= 23)
		outarg.time_gran = se->conn.time_gran;

	if (se->debug) {
		fuse_log(FUSE_LOG_DEBUG, "   INIT: %u.%u\n", outarg.major, outarg.minor);
		fuse_log(FUSE_LOG_DEBUG, "   flags=0x%08x\n", outarg.flags);
		fuse_log(FUSE_LOG_DEBUG, "   max_readahead=0x%08x\n",
			outarg.max_readahead);
		fuse_log(FUSE_LOG_DEBUG, "   max_write=0x%08x\n", outarg.max_write);
		fuse_log(FUSE_LOG_DEBUG, "   max_background=%i\n",
			outarg.max_background);
		fuse_log(FUSE_LOG_DEBUG, "   congestion_threshold=%i\n",
			outarg.congestion_threshold);
		fuse_log(FUSE_LOG_DEBUG, "   time_gran=%u\n",
			outarg.time_gran);
	}
	if (arg->minor < 5)
		outargsize = FUSE_COMPAT_INIT_OUT_SIZE;
	else if (arg->minor < 23)
		outargsize = FUSE_COMPAT_22_INIT_OUT_SIZE;

	send_reply_ok(req, &outarg, outargsize);
}

static void do_destroy(fuse_req_t req, fuse_ino_t nodeid, const void *inarg)
{
	struct fuse_session *se = req->se;

	(void) nodeid;
	(void) inarg;

	se->got_destroy = 1;
	if (se->op.destroy)
		se->op.destroy(se->userdata);

	send_reply_ok(req, NULL, 0);
}

static void list_del_nreq(struct fuse_notify_req *nreq)
{
	struct fuse_notify_req *prev = nreq->prev;
	struct fuse_notify_req *next = nreq->next;
	prev->next = next;
	next->prev = prev;
}

static void list_add_nreq(struct fuse_notify_req *nreq,
			  struct fuse_notify_req *next)
{
	struct fuse_notify_req *prev = next->prev;
	nreq->next = next;
	nreq->prev = prev;
	prev->next = nreq;
	next->prev = nreq;
}

static void list_init_nreq(struct fuse_notify_req *nreq)
{
	nreq->next = nreq;
	nreq->prev = nreq;
}

static void do_notify_reply(fuse_req_t req, fuse_ino_t nodeid,
			    const void *inarg, const struct fuse_buf *buf)
{
	struct fuse_session *se = req->se;
	struct fuse_notify_req *nreq;
	struct fuse_notify_req *head;

	pthread_mutex_lock(&se->lock);
	head = &se->notify_list;
	for (nreq = head->next; nreq != head; nreq = nreq->next) {
		if (nreq->unique == req->unique) {
			list_del_nreq(nreq);
			break;
		}
	}
	pthread_mutex_unlock(&se->lock);

	if (nreq != head)
		nreq->reply(nreq, req, nodeid, inarg, buf);
}

static int send_notify_iov(struct fuse_session *se, int notify_code,
			   struct iovec *iov, int count)
{
	struct fuse_out_header out;

	if (!se->got_init)
		return -ENOTCONN;

	out.unique = 0;
	out.error = notify_code;
	iov[0].iov_base = &out;
	iov[0].iov_len = sizeof(struct fuse_out_header);

	return fuse_send_msg(se, NULL, iov, count);
}

int fuse_lowlevel_notify_poll(struct fuse_pollhandle *ph)
{
	if (ph != NULL) {
		struct fuse_notify_poll_wakeup_out outarg;
		struct iovec iov[2];

		outarg.kh = ph->kh;

		iov[1].iov_base = &outarg;
		iov[1].iov_len = sizeof(outarg);

		return send_notify_iov(ph->se, FUSE_NOTIFY_POLL, iov, 2);
	} else {
		return 0;
	}
}

int fuse_lowlevel_notify_inval_inode(struct fuse_session *se, fuse_ino_t ino,
				     off_t off, off_t len)
{
	struct fuse_notify_inval_inode_out outarg;
	struct iovec iov[2];

	if (!se)
		return -EINVAL;

	if (se->conn.proto_major < 6 || se->conn.proto_minor < 12)
		return -ENOSYS;
	
	outarg.ino = ino;
	outarg.off = off;
	outarg.len = len;

	iov[1].iov_base = &outarg;
	iov[1].iov_len = sizeof(outarg);

	return send_notify_iov(se, FUSE_NOTIFY_INVAL_INODE, iov, 2);
}

int fuse_lowlevel_notify_inval_entry(struct fuse_session *se, fuse_ino_t parent,
				     const char *name, size_t namelen)
{
	struct fuse_notify_inval_entry_out outarg;
	struct iovec iov[3];

	if (!se)
		return -EINVAL;
	
	if (se->conn.proto_major < 6 || se->conn.proto_minor < 12)
		return -ENOSYS;

	outarg.parent = parent;
	outarg.namelen = namelen;
	outarg.padding = 0;

	iov[1].iov_base = &outarg;
	iov[1].iov_len = sizeof(outarg);
	iov[2].iov_base = (void *)name;
	iov[2].iov_len = namelen + 1;

	return send_notify_iov(se, FUSE_NOTIFY_INVAL_ENTRY, iov, 3);
}

int fuse_lowlevel_notify_delete(struct fuse_session *se,
				fuse_ino_t parent, fuse_ino_t child,
				const char *name, size_t namelen)
{
	struct fuse_notify_delete_out outarg;
	struct iovec iov[3];

	if (!se)
		return -EINVAL;

	if (se->conn.proto_major < 6 || se->conn.proto_minor < 18)
		return -ENOSYS;

	outarg.parent = parent;
	outarg.child = child;
	outarg.namelen = namelen;
	outarg.padding = 0;

	iov[1].iov_base = &outarg;
	iov[1].iov_len = sizeof(outarg);
	iov[2].iov_base = (void *)name;
	iov[2].iov_len = namelen + 1;

	return send_notify_iov(se, FUSE_NOTIFY_DELETE, iov, 3);
}

int fuse_lowlevel_notify_store(struct fuse_session *se, fuse_ino_t ino,
			       off_t offset, struct fuse_bufvec *bufv,
			       enum fuse_buf_copy_flags flags)
{
	struct fuse_out_header out;
	struct fuse_notify_store_out outarg;
	struct iovec iov[3];
	size_t size = fuse_buf_size(bufv);
	int res;

	if (!se)
		return -EINVAL;

	if (se->conn.proto_major < 6 || se->conn.proto_minor < 15)
		return -ENOSYS;

	out.unique = 0;
	out.error = FUSE_NOTIFY_STORE;

	outarg.nodeid = ino;
	outarg.offset = offset;
	outarg.size = size;
	outarg.padding = 0;

	iov[0].iov_base = &out;
	iov[0].iov_len = sizeof(out);
	iov[1].iov_base = &outarg;
	iov[1].iov_len = sizeof(outarg);

	res = fuse_send_data_iov(se, NULL, iov, 2, bufv, flags);
	if (res > 0)
		res = -res;

	return res;
}

struct fuse_retrieve_req {
	struct fuse_notify_req nreq;
	void *cookie;
};

static void fuse_ll_retrieve_reply(struct fuse_notify_req *nreq,
				   fuse_req_t req, fuse_ino_t ino,
				   const void *inarg,
				   const struct fuse_buf *ibuf)
{
	struct fuse_session *se = req->se;
	struct fuse_retrieve_req *rreq =
		container_of(nreq, struct fuse_retrieve_req, nreq);
	const struct fuse_notify_retrieve_in *arg = inarg;
	struct fuse_bufvec bufv = {
		.buf[0] = *ibuf,
		.count = 1,
	};

	if (!(bufv.buf[0].flags & FUSE_BUF_IS_FD))
		bufv.buf[0].mem = PARAM(arg);

	bufv.buf[0].size -= sizeof(struct fuse_in_header) +
		sizeof(struct fuse_notify_retrieve_in);

	if (bufv.buf[0].size < arg->size) {
		fuse_log(FUSE_LOG_ERR, "fuse: retrieve reply: buffer size too small\n");
		fuse_reply_none(req);
		goto out;
	}
	bufv.buf[0].size = arg->size;

	if (se->op.retrieve_reply) {
		se->op.retrieve_reply(req, rreq->cookie, ino,
					  arg->offset, &bufv);
	} else {
		fuse_reply_none(req);
	}
out:
	free(rreq);
	if ((ibuf->flags & FUSE_BUF_IS_FD) && bufv.idx < bufv.count)
		fuse_ll_clear_pipe(se);
}

int fuse_lowlevel_notify_retrieve(struct fuse_session *se, fuse_ino_t ino,
				  size_t size, off_t offset, void *cookie)
{
	struct fuse_notify_retrieve_out outarg;
	struct iovec iov[2];
	struct fuse_retrieve_req *rreq;
	int err;

	if (!se)
		return -EINVAL;

	if (se->conn.proto_major < 6 || se->conn.proto_minor < 15)
		return -ENOSYS;

	rreq = malloc(sizeof(*rreq));
	if (rreq == NULL)
		return -ENOMEM;

	pthread_mutex_lock(&se->lock);
	rreq->cookie = cookie;
	rreq->nreq.unique = se->notify_ctr++;
	rreq->nreq.reply = fuse_ll_retrieve_reply;
	list_add_nreq(&rreq->nreq, &se->notify_list);
	pthread_mutex_unlock(&se->lock);

	outarg.notify_unique = rreq->nreq.unique;
	outarg.nodeid = ino;
	outarg.offset = offset;
	outarg.size = size;
	outarg.padding = 0;

	iov[1].iov_base = &outarg;
	iov[1].iov_len = sizeof(outarg);

	err = send_notify_iov(se, FUSE_NOTIFY_RETRIEVE, iov, 2);
	if (err) {
		pthread_mutex_lock(&se->lock);
		list_del_nreq(&rreq->nreq);
		pthread_mutex_unlock(&se->lock);
		free(rreq);
	}

	return err;
}

void *fuse_req_userdata(fuse_req_t req)
{
	return req->se->userdata;
}

const struct fuse_ctx *fuse_req_ctx(fuse_req_t req)
{
	return &req->ctx;
}

void fuse_req_interrupt_func(fuse_req_t req, fuse_interrupt_func_t func,
			     void *data)
{
	pthread_mutex_lock(&req->lock);
	pthread_mutex_lock(&req->se->lock);
	req->u.ni.func = func;
	req->u.ni.data = data;
	pthread_mutex_unlock(&req->se->lock);
	if (req->interrupted && func)
		func(req, data);
	pthread_mutex_unlock(&req->lock);
}

int fuse_req_interrupted(fuse_req_t req)
{
	int interrupted;

	pthread_mutex_lock(&req->se->lock);
	interrupted = req->interrupted;
	pthread_mutex_unlock(&req->se->lock);

	return interrupted;
}

static struct {
	void (*func)(fuse_req_t, fuse_ino_t, const void *);
	const char *name;
} fuse_ll_ops[] = {
	[FUSE_LOOKUP]	   = { do_lookup,      "LOOKUP"	     },
	[FUSE_FORGET]	   = { do_forget,      "FORGET"	     },
	[FUSE_GETATTR]	   = { do_getattr,     "GETATTR"     },
	[FUSE_SETATTR]	   = { do_setattr,     "SETATTR"     },
	[FUSE_READLINK]	   = { do_readlink,    "READLINK"    },
	[FUSE_SYMLINK]	   = { do_symlink,     "SYMLINK"     },
	[FUSE_MKNOD]	   = { do_mknod,       "MKNOD"	     },
	[FUSE_MKDIR]	   = { do_mkdir,       "MKDIR"	     },
	[FUSE_UNLINK]	   = { do_unlink,      "UNLINK"	     },
	[FUSE_RMDIR]	   = { do_rmdir,       "RMDIR"	     },
	[FUSE_RENAME]	   = { do_rename,      "RENAME"	     },
	[FUSE_LINK]	   = { do_link,	       "LINK"	     },
	[FUSE_OPEN]	   = { do_open,	       "OPEN"	     },
	[FUSE_READ]	   = { do_read,	       "READ"	     },
	[FUSE_WRITE]	   = { do_write,       "WRITE"	     },
	[FUSE_STATFS]	   = { do_statfs,      "STATFS"	     },
	[FUSE_RELEASE]	   = { do_release,     "RELEASE"     },
	[FUSE_FSYNC]	   = { do_fsync,       "FSYNC"	     },
	[FUSE_SETXATTR]	   = { do_setxattr,    "SETXATTR"    },
	[FUSE_GETXATTR]	   = { do_getxattr,    "GETXATTR"    },
	[FUSE_LISTXATTR]   = { do_listxattr,   "LISTXATTR"   },
	[FUSE_REMOVEXATTR] = { do_removexattr, "REMOVEXATTR" },
	[FUSE_FLUSH]	   = { do_flush,       "FLUSH"	     },
	[FUSE_INIT]	   = { do_init,	       "INIT"	     },
	[FUSE_OPENDIR]	   = { do_opendir,     "OPENDIR"     },
	[FUSE_READDIR]	   = { do_readdir,     "READDIR"     },
	[FUSE_RELEASEDIR]  = { do_releasedir,  "RELEASEDIR"  },
	[FUSE_FSYNCDIR]	   = { do_fsyncdir,    "FSYNCDIR"    },
	[FUSE_GETLK]	   = { do_getlk,       "GETLK"	     },
	[FUSE_SETLK]	   = { do_setlk,       "SETLK"	     },
	[FUSE_SETLKW]	   = { do_setlkw,      "SETLKW"	     },
	[FUSE_ACCESS]	   = { do_access,      "ACCESS"	     },
	[FUSE_CREATE]	   = { do_create,      "CREATE"	     },
	[FUSE_INTERRUPT]   = { do_interrupt,   "INTERRUPT"   },
	[FUSE_BMAP]	   = { do_bmap,	       "BMAP"	     },
	[FUSE_IOCTL]	   = { do_ioctl,       "IOCTL"	     },
	[FUSE_POLL]	   = { do_poll,        "POLL"	     },
	[FUSE_FALLOCATE]   = { do_fallocate,   "FALLOCATE"   },
	[FUSE_DESTROY]	   = { do_destroy,     "DESTROY"     },
	[FUSE_NOTIFY_REPLY] = { (void *) 1,    "NOTIFY_REPLY" },
	[FUSE_BATCH_FORGET] = { do_batch_forget, "BATCH_FORGET" },
	[FUSE_READDIRPLUS] = { do_readdirplus,	"READDIRPLUS"},
	[FUSE_RENAME2]     = { do_rename2,      "RENAME2"    },
	[FUSE_COPY_FILE_RANGE] = { do_copy_file_range, "COPY_FILE_RANGE" },
	[FUSE_LSEEK]	   = { do_lseek,       "LSEEK"	     },
	[CUSE_INIT]	   = { cuse_lowlevel_init, "CUSE_INIT"   },
};

#define FUSE_MAXOP (sizeof(fuse_ll_ops) / sizeof(fuse_ll_ops[0]))

static const char *opname(enum fuse_opcode opcode)
{
	if (opcode >= FUSE_MAXOP || !fuse_ll_ops[opcode].name)
		return "???";
	else
		return fuse_ll_ops[opcode].name;
}

static int fuse_ll_copy_from_pipe(struct fuse_bufvec *dst,
				  struct fuse_bufvec *src)
{
	ssize_t res = fuse_buf_copy(dst, src, 0);
	if (res < 0) {
		fuse_log(FUSE_LOG_ERR, "fuse: copy from pipe: %s\n", strerror(-res));
		return res;
	}
	if ((size_t)res < fuse_buf_size(dst)) {
		fuse_log(FUSE_LOG_ERR, "fuse: copy from pipe: short read\n");
		return -1;
	}
	return 0;
}

void fuse_session_process_buf(struct fuse_session *se,
			      const struct fuse_buf *buf)
{
	fuse_session_process_buf_int(se, buf, NULL);
}

void fuse_session_process_buf_int(struct fuse_session *se,
				  const struct fuse_buf *buf, struct fuse_chan *ch)
{
	const size_t write_header_size = sizeof(struct fuse_in_header) +
		sizeof(struct fuse_write_in);
	struct fuse_bufvec bufv = { .buf[0] = *buf, .count = 1 };
	struct fuse_bufvec tmpbuf = FUSE_BUFVEC_INIT(write_header_size);
	struct fuse_in_header *in;
	const void *inarg;
	struct fuse_req *req;
	void *mbuf = NULL;
	int err;
	int res;

	if (buf->flags & FUSE_BUF_IS_FD) {
		if (buf->size < tmpbuf.buf[0].size)
			tmpbuf.buf[0].size = buf->size;

		mbuf = malloc(tmpbuf.buf[0].size);
		if (mbuf == NULL) {
			fuse_log(FUSE_LOG_ERR, "fuse: failed to allocate header\n");
			goto clear_pipe;
		}
		tmpbuf.buf[0].mem = mbuf;

		res = fuse_ll_copy_from_pipe(&tmpbuf, &bufv);
		if (res < 0)
			goto clear_pipe;

		in = mbuf;
	} else {
		in = buf->mem;
	}

	if (se->debug) {
		fuse_log(FUSE_LOG_DEBUG,
			"unique: %llu, opcode: %s (%i), nodeid: %llu, insize: %zu, pid: %u\n",
			(unsigned long long) in->unique,
			opname((enum fuse_opcode) in->opcode), in->opcode,
			(unsigned long long) in->nodeid, buf->size, in->pid);
	}

	req = fuse_ll_alloc_req(se);
	if (req == NULL) {
		struct fuse_out_header out = {
			.unique = in->unique,
			.error = -ENOMEM,
		};
		struct iovec iov = {
			.iov_base = &out,
			.iov_len = sizeof(struct fuse_out_header),
		};

		fuse_send_msg(se, ch, &iov, 1);
		goto clear_pipe;
	}

	req->unique = in->unique;
	req->ctx.uid = in->uid;
	req->ctx.gid = in->gid;
	req->ctx.pid = in->pid;
	req->ch = ch ? fuse_chan_get(ch) : NULL;

	err = EIO;
	if (!se->got_init) {
		enum fuse_opcode expected;

		expected = se->cuse_data ? CUSE_INIT : FUSE_INIT;
		if (in->opcode != expected)
			goto reply_err;
	} else if (in->opcode == FUSE_INIT || in->opcode == CUSE_INIT)
		goto reply_err;

	err = EACCES;
	/* Implement -o allow_root */
	if (se->deny_others && in->uid != se->owner && in->uid != 0 &&
		 in->opcode != FUSE_INIT && in->opcode != FUSE_READ &&
		 in->opcode != FUSE_WRITE && in->opcode != FUSE_FSYNC &&
		 in->opcode != FUSE_RELEASE && in->opcode != FUSE_READDIR &&
		 in->opcode != FUSE_FSYNCDIR && in->opcode != FUSE_RELEASEDIR &&
		 in->opcode != FUSE_NOTIFY_REPLY &&
		 in->opcode != FUSE_READDIRPLUS)
		goto reply_err;

	err = ENOSYS;
	if (in->opcode >= FUSE_MAXOP || !fuse_ll_ops[in->opcode].func)
		goto reply_err;
	if (in->opcode != FUSE_INTERRUPT) {
		struct fuse_req *intr;
		pthread_mutex_lock(&se->lock);
		intr = check_interrupt(se, req);
		list_add_req(req, &se->list);
		pthread_mutex_unlock(&se->lock);
		if (intr)
			fuse_reply_err(intr, EAGAIN);
	}

	if ((buf->flags & FUSE_BUF_IS_FD) && write_header_size < buf->size &&
	    (in->opcode != FUSE_WRITE || !se->op.write_buf) &&
	    in->opcode != FUSE_NOTIFY_REPLY) {
		void *newmbuf;

		err = ENOMEM;
		newmbuf = realloc(mbuf, buf->size);
		if (newmbuf == NULL)
			goto reply_err;
		mbuf = newmbuf;

		tmpbuf = FUSE_BUFVEC_INIT(buf->size - write_header_size);
		tmpbuf.buf[0].mem = (char *)mbuf + write_header_size;

		res = fuse_ll_copy_from_pipe(&tmpbuf, &bufv);
		err = -res;
		if (res < 0)
			goto reply_err;

		in = mbuf;
	}

	inarg = (void *) &in[1];
	if (in->opcode == FUSE_WRITE && se->op.write_buf)
		do_write_buf(req, in->nodeid, inarg, buf);
	else if (in->opcode == FUSE_NOTIFY_REPLY)
		do_notify_reply(req, in->nodeid, inarg, buf);
	else
		fuse_ll_ops[in->opcode].func(req, in->nodeid, inarg);

out_free:
	free(mbuf);
	return;

reply_err:
	fuse_reply_err(req, err);
clear_pipe:
	if (buf->flags & FUSE_BUF_IS_FD)
		fuse_ll_clear_pipe(se);
	goto out_free;
}

#define LL_OPTION(n,o,v) \
	{ n, offsetof(struct fuse_session, o), v }

static const struct fuse_opt fuse_ll_opts[] = {
	LL_OPTION("debug", debug, 1),
	LL_OPTION("-d", debug, 1),
	LL_OPTION("--debug", debug, 1),
	LL_OPTION("allow_root", deny_others, 1),
	FUSE_OPT_END
};

void fuse_lowlevel_version(void)
{
	printf("using FUSE kernel interface version %i.%i\n",
	       FUSE_KERNEL_VERSION, FUSE_KERNEL_MINOR_VERSION);
	fuse_mount_version();
}

void fuse_lowlevel_help(void)
{
	/* These are not all options, but the ones that are
	   potentially of interest to an end-user */
	printf(
"    -o allow_other         allow access by all users\n"
"    -o allow_root          allow access by root\n"
"    -o auto_unmount        auto unmount on process termination\n");
}

void fuse_session_destroy(struct fuse_session *se)
{
	struct fuse_ll_pipe *llp;

	if (se->got_init && !se->got_destroy) {
		if (se->op.destroy)
			se->op.destroy(se->userdata);
	}
	llp = pthread_getspecific(se->pipe_key);
	if (llp != NULL)
		fuse_ll_pipe_free(llp);
	pthread_key_delete(se->pipe_key);
	pthread_mutex_destroy(&se->lock);
	free(se->cuse_data);
	if (se->fd != -1)
		close(se->fd);
	destroy_mount_opts(se->mo);
	free(se);
}


static void fuse_ll_pipe_destructor(void *data)
{
	struct fuse_ll_pipe *llp = data;
	fuse_ll_pipe_free(llp);
}

int fuse_session_receive_buf(struct fuse_session *se, struct fuse_buf *buf)
{
	return fuse_session_receive_buf_int(se, buf, NULL);
}

int fuse_session_receive_buf_int(struct fuse_session *se, struct fuse_buf *buf,
				 struct fuse_chan *ch)
{
	int err;
	ssize_t res;
#ifdef HAVE_SPLICE
	size_t bufsize = se->bufsize;
	struct fuse_ll_pipe *llp;
	struct fuse_buf tmpbuf;

	if (se->conn.proto_minor < 14 || !(se->conn.want & FUSE_CAP_SPLICE_READ))
		goto fallback;

	llp = fuse_ll_get_pipe(se);
	if (llp == NULL)
		goto fallback;

	if (llp->size < bufsize) {
		if (llp->can_grow) {
			res = fcntl(llp->pipe[0], F_SETPIPE_SZ, bufsize);
			if (res == -1) {
				llp->can_grow = 0;
				res = grow_pipe_to_max(llp->pipe[0]);
				if (res > 0)
					llp->size = res;
				goto fallback;
			}
			llp->size = res;
		}
		if (llp->size < bufsize)
			goto fallback;
	}

	res = splice(ch ? ch->fd : se->fd,
		     NULL, llp->pipe[1], NULL, bufsize, 0);
	err = errno;

	if (fuse_session_exited(se))
		return 0;

	if (res == -1) {
		if (err == ENODEV) {
			/* Filesystem was unmounted, or connection was aborted
			   via /sys/fs/fuse/connections */
			fuse_session_exit(se);
			return 0;
		}
		if (err != EINTR && err != EAGAIN)
			perror("fuse: splice from device");
		return -err;
	}

	if (res < sizeof(struct fuse_in_header)) {
		fuse_log(FUSE_LOG_ERR, "short splice from fuse device\n");
		return -EIO;
	}

	tmpbuf = (struct fuse_buf) {
		.size = res,
		.flags = FUSE_BUF_IS_FD,
		.fd = llp->pipe[0],
	};

	/*
	 * Don't bother with zero copy for small requests.
	 * fuse_loop_mt() needs to check for FORGET so this more than
	 * just an optimization.
	 */
	if (res < sizeof(struct fuse_in_header) +
	    sizeof(struct fuse_write_in) + pagesize) {
		struct fuse_bufvec src = { .buf[0] = tmpbuf, .count = 1 };
		struct fuse_bufvec dst = { .count = 1 };

		if (!buf->mem) {
			buf->mem = malloc(se->bufsize);
			if (!buf->mem) {
				fuse_log(FUSE_LOG_ERR,
					"fuse: failed to allocate read buffer\n");
				return -ENOMEM;
			}
		}
		buf->size = se->bufsize;
		buf->flags = 0;
		dst.buf[0] = *buf;

		res = fuse_buf_copy(&dst, &src, 0);
		if (res < 0) {
			fuse_log(FUSE_LOG_ERR, "fuse: copy from pipe: %s\n",
				strerror(-res));
			fuse_ll_clear_pipe(se);
			return res;
		}
		if (res < tmpbuf.size) {
			fuse_log(FUSE_LOG_ERR, "fuse: copy from pipe: short read\n");
			fuse_ll_clear_pipe(se);
			return -EIO;
		}
		assert(res == tmpbuf.size);

	} else {
		/* Don't overwrite buf->mem, as that would cause a leak */
		buf->fd = tmpbuf.fd;
		buf->flags = tmpbuf.flags;
	}
	buf->size = tmpbuf.size;

	return res;

fallback:
#endif
	if (!buf->mem) {
		buf->mem = malloc(se->bufsize);
		if (!buf->mem) {
			fuse_log(FUSE_LOG_ERR,
				"fuse: failed to allocate read buffer\n");
			return -ENOMEM;
		}
	}

restart:
	res = read(ch ? ch->fd : se->fd, buf->mem, se->bufsize);
	err = errno;

	if (fuse_session_exited(se))
		return 0;
	if (res == -1) {
		/* ENOENT means the operation was interrupted, it's safe
		   to restart */
		if (err == ENOENT)
			goto restart;

		if (err == ENODEV) {
			/* Filesystem was unmounted, or connection was aborted
			   via /sys/fs/fuse/connections */
			fuse_session_exit(se);
			return 0;
		}
		/* Errors occurring during normal operation: EINTR (read
		   interrupted), EAGAIN (nonblocking I/O), ENODEV (filesystem
		   umounted) */
		if (err != EINTR && err != EAGAIN)
			perror("fuse: reading device");
		return -err;
	}
	if ((size_t) res < sizeof(struct fuse_in_header)) {
		fuse_log(FUSE_LOG_ERR, "short read on fuse device\n");
		return -EIO;
	}

	buf->size = res;

	return res;
}

struct fuse_session *fuse_session_new(struct fuse_args *args,
				      const struct fuse_lowlevel_ops *op,
				      size_t op_size, void *userdata)
{
	int err;
	struct fuse_session *se;
	struct mount_opts *mo;

	if (sizeof(struct fuse_lowlevel_ops) < op_size) {
		fuse_log(FUSE_LOG_ERR, "fuse: warning: library too old, some operations may not work\n");
		op_size = sizeof(struct fuse_lowlevel_ops);
	}

	if (args->argc == 0) {
		fuse_log(FUSE_LOG_ERR, "fuse: empty argv passed to fuse_session_new().\n");
		return NULL;
	}

	se = (struct fuse_session *) calloc(1, sizeof(struct fuse_session));
	if (se == NULL) {
		fuse_log(FUSE_LOG_ERR, "fuse: failed to allocate fuse object\n");
		goto out1;
	}
	se->fd = -1;
	se->conn.max_write = UINT_MAX;
	se->conn.max_readahead = UINT_MAX;

	/* Parse options */
	if(fuse_opt_parse(args, se, fuse_ll_opts, NULL) == -1)
		goto out2;
	if(se->deny_others) {
		/* Allowing access only by root is done by instructing
		 * kernel to allow access by everyone, and then restricting
		 * access to root and mountpoint owner in libfuse.
		 */
		// We may be adding the option a second time, but
		// that doesn't hurt.
		if(fuse_opt_add_arg(args, "-oallow_other") == -1)
			goto out2;
	}
	mo = parse_mount_opts(args);
	if (mo == NULL)
		goto out3;

	if(args->argc == 1 &&
	   args->argv[0][0] == '-') {
		fuse_log(FUSE_LOG_ERR, "fuse: warning: argv[0] looks like an option, but "
			"will be ignored\n");
	} else if (args->argc != 1) {
		int i;
		fuse_log(FUSE_LOG_ERR, "fuse: unknown option(s): `");
		for(i = 1; i < args->argc-1; i++)
			fuse_log(FUSE_LOG_ERR, "%s ", args->argv[i]);
		fuse_log(FUSE_LOG_ERR, "%s'\n", args->argv[i]);
		goto out4;
	}

	if (se->debug)
		fuse_log(FUSE_LOG_DEBUG, "FUSE library version: %s\n", PACKAGE_VERSION);

	se->bufsize = FUSE_MAX_MAX_PAGES * getpagesize() +
		FUSE_BUFFER_HEADER_SIZE;

	list_init_req(&se->list);
	list_init_req(&se->interrupts);
	list_init_nreq(&se->notify_list);
	se->notify_ctr = 1;
	fuse_mutex_init(&se->lock);

	err = pthread_key_create(&se->pipe_key, fuse_ll_pipe_destructor);
	if (err) {
		fuse_log(FUSE_LOG_ERR, "fuse: failed to create thread specific key: %s\n",
			strerror(err));
		goto out5;
	}

	memcpy(&se->op, op, op_size);
	se->owner = getuid();
	se->userdata = userdata;

	se->mo = mo;
	return se;

out5:
	pthread_mutex_destroy(&se->lock);
out4:
	fuse_opt_free_args(args);
out3:
	free(mo);
out2:
	free(se);
out1:
	return NULL;
}

int fuse_session_mount(struct fuse_session *se, const char *mountpoint)
{
	int fd;

	/*
	 * Make sure file descriptors 0, 1 and 2 are open, otherwise chaos
	 * would ensue.
	 */
	do {
		fd = open("/dev/null", O_RDWR);
		if (fd > 2)
			close(fd);
	} while (fd >= 0 && fd <= 2);

	/*
	 * To allow FUSE daemons to run without privileges, the caller may open
	 * /dev/fuse before launching the file system and pass on the file
	 * descriptor by specifying /dev/fd/N as the mount point. Note that the
	 * parent process takes care of performing the mount in this case.
	 */
	fd = fuse_mnt_parse_fuse_fd(mountpoint);
	if (fd != -1) {
		if (fcntl(fd, F_GETFD) == -1) {
			fuse_log(FUSE_LOG_ERR,
				"fuse: Invalid file descriptor /dev/fd/%u\n",
				fd);
			return -1;
		}
		se->fd = fd;
		return 0;
	}

	/* Open channel */
	fd = fuse_kern_mount(mountpoint, se->mo);
	if (fd == -1)
		return -1;
	se->fd = fd;

	/* Save mountpoint */
	se->mountpoint = strdup(mountpoint);
	if (se->mountpoint == NULL)
		goto error_out;

	return 0;

error_out:
	fuse_kern_unmount(mountpoint, fd);
	return -1;
}

int fuse_session_fd(struct fuse_session *se)
{
	return se->fd;
}

void fuse_session_unmount(struct fuse_session *se)
{
	if (se->mountpoint != NULL) {
		fuse_kern_unmount(se->mountpoint, se->fd);
		free(se->mountpoint);
		se->mountpoint = NULL;
	}
}

#ifdef linux
int fuse_req_getgroups(fuse_req_t req, int size, gid_t list[])
{
	char *buf;
	size_t bufsize = 1024;
	char path[128];
	int ret;
	int fd;
	unsigned long pid = req->ctx.pid;
	char *s;

	sprintf(path, "/proc/%lu/task/%lu/status", pid, pid);

retry:
	buf = malloc(bufsize);
	if (buf == NULL)
		return -ENOMEM;

	ret = -EIO;
	fd = open(path, O_RDONLY);
	if (fd == -1)
		goto out_free;

	ret = read(fd, buf, bufsize);
	close(fd);
	if (ret < 0) {
		ret = -EIO;
		goto out_free;
	}

	if ((size_t)ret == bufsize) {
		free(buf);
		bufsize *= 4;
		goto retry;
	}

	ret = -EIO;
	s = strstr(buf, "\nGroups:");
	if (s == NULL)
		goto out_free;

	s += 8;
	ret = 0;
	while (1) {
		char *end;
		unsigned long val = strtoul(s, &end, 0);
		if (end == s)
			break;

		s = end;
		if (ret < size)
			list[ret] = val;
		ret++;
	}

out_free:
	free(buf);
	return ret;
}
#else /* linux */
/*
 * This is currently not implemented on other than Linux...
 */
int fuse_req_getgroups(fuse_req_t req, int size, gid_t list[])
{
	(void) req; (void) size; (void) list;
	return -ENOSYS;
}
#endif

void fuse_session_exit(struct fuse_session *se)
{
	se->exited = 1;
}

void fuse_session_reset(struct fuse_session *se)
{
	se->exited = 0;
	se->error = 0;
}

int fuse_session_exited(struct fuse_session *se)
{
	return se->exited;
}
