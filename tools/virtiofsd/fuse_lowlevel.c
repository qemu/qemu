/*
 * FUSE: Filesystem in Userspace
 * Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
 *
 * Implementation of (most of) the low-level FUSE API. The session loop
 * functions are implemented in separate files.
 *
 * This program can be distributed under the terms of the GNU LGPLv2.
 * See the file COPYING.LIB
 */

#include "qemu/osdep.h"
#include "fuse_i.h"
#include "standard-headers/linux/fuse.h"
#include "fuse_misc.h"
#include "fuse_opt.h"
#include "fuse_virtio.h"

#include <sys/file.h>

#define THREAD_POOL_SIZE 0

#define OFFSET_MAX 0x7fffffffffffffffLL

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
    *attr = (struct fuse_attr){
        .ino = stbuf->st_ino,
        .mode = stbuf->st_mode,
        .nlink = stbuf->st_nlink,
        .uid = stbuf->st_uid,
        .gid = stbuf->st_gid,
        .rdev = stbuf->st_rdev,
        .size = stbuf->st_size,
        .blksize = stbuf->st_blksize,
        .blocks = stbuf->st_blocks,
        .atime = stbuf->st_atime,
        .mtime = stbuf->st_mtime,
        .ctime = stbuf->st_ctime,
        .atimensec = ST_ATIM_NSEC(stbuf),
        .mtimensec = ST_MTIM_NSEC(stbuf),
        .ctimensec = ST_CTIM_NSEC(stbuf),
    };
}

static void convert_attr(const struct fuse_setattr_in *attr, struct stat *stbuf)
{
    stbuf->st_mode = attr->mode;
    stbuf->st_uid = attr->uid;
    stbuf->st_gid = attr->gid;
    stbuf->st_size = attr->size;
    stbuf->st_atime = attr->atime;
    stbuf->st_mtime = attr->mtime;
    stbuf->st_ctime = attr->ctime;
    ST_ATIM_NSEC_SET(stbuf, attr->atimensec);
    ST_MTIM_NSEC_SET(stbuf, attr->mtimensec);
    ST_CTIM_NSEC_SET(stbuf, attr->ctimensec);
}

static size_t iov_length(const struct iovec *iov, size_t count)
{
    size_t seg;
    size_t ret = 0;

    for (seg = 0; seg < count; seg++) {
        ret += iov[seg].iov_len;
    }
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
    g_free(req);
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
    req->ch = NULL;
    pthread_mutex_unlock(&se->lock);
    if (!ctr) {
        destroy_req(req);
    }
}

static struct fuse_req *fuse_ll_alloc_req(struct fuse_session *se)
{
    struct fuse_req *req;

    req = g_try_new0(struct fuse_req, 1);
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
    if (out->unique == 0) {
        fuse_log(FUSE_LOG_DEBUG, "NOTIFY: code=%d length=%u\n", out->error,
                 out->len);
    } else if (out->error) {
        fuse_log(FUSE_LOG_DEBUG,
                 "   unique: %llu, error: %i (%s), outsize: %i\n",
                 (unsigned long long)out->unique, out->error,
                 strerror(-out->error), out->len);
    } else {
        fuse_log(FUSE_LOG_DEBUG, "   unique: %llu, success, outsize: %i\n",
                 (unsigned long long)out->unique, out->len);
    }

    if (fuse_lowlevel_is_virtio(se)) {
        return virtio_send_msg(se, ch, iov, count);
    }

    abort(); /* virtio should have taken it before here */
    return 0;
}


int fuse_send_reply_iov_nofree(fuse_req_t req, int error, struct iovec *iov,
                               int count)
{
    struct fuse_out_header out = {
        .unique = req->unique,
        .error = error,
    };

    if (error <= -1000 || error > 0) {
        fuse_log(FUSE_LOG_ERR, "fuse: bad error value: %i\n", error);
        out.error = -ERANGE;
    }

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
        iov[1].iov_base = (void *)arg;
        iov[1].iov_len = argsize;
        count++;
    }
    return send_reply_iov(req, error, iov, count);
}

int fuse_reply_iov(fuse_req_t req, const struct iovec *iov, int count)
{
    g_autofree struct iovec *padded_iov = NULL;

    padded_iov = g_try_new(struct iovec, count + 1);
    if (padded_iov == NULL) {
        return fuse_reply_err(req, ENOMEM);
    }

    memcpy(padded_iov + 1, iov, count * sizeof(struct iovec));
    count++;

    return send_reply_iov(req, 0, padded_iov, count);
}


/*
 * 'buf` is allowed to be empty so that the proper size may be
 * allocated by the caller
 */
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

    if ((buf == NULL) || (entlen_padded > bufsize)) {
        return entlen_padded;
    }

    dirent = (struct fuse_dirent *)buf;
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
    *kstatfs = (struct fuse_kstatfs){
        .bsize = stbuf->f_bsize,
        .frsize = stbuf->f_frsize,
        .blocks = stbuf->f_blocks,
        .bfree = stbuf->f_bfree,
        .bavail = stbuf->f_bavail,
        .files = stbuf->f_files,
        .ffree = stbuf->f_ffree,
        .namelen = stbuf->f_namemax,
    };
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
    if (t > (double)ULONG_MAX) {
        return ULONG_MAX;
    } else if (t < 0.0) {
        return 0;
    } else {
        return (unsigned long)t;
    }
}

static unsigned int calc_timeout_nsec(double t)
{
    double f = t - (double)calc_timeout_sec(t);
    if (f < 0.0) {
        return 0;
    } else if (f >= 0.999999999) {
        return 999999999;
    } else {
        return (unsigned int)(f * 1.0e9);
    }
}

static void fill_entry(struct fuse_entry_out *arg,
                       const struct fuse_entry_param *e)
{
    *arg = (struct fuse_entry_out){
        .nodeid = e->ino,
        .generation = e->generation,
        .entry_valid = calc_timeout_sec(e->entry_timeout),
        .entry_valid_nsec = calc_timeout_nsec(e->entry_timeout),
        .attr_valid = calc_timeout_sec(e->attr_timeout),
        .attr_valid_nsec = calc_timeout_nsec(e->attr_timeout),
    };
    convert_stat(&e->attr, &arg->attr);

    arg->attr.flags = e->attr_flags;
}

/*
 * `buf` is allowed to be empty so that the proper size may be
 * allocated by the caller
 */
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
    if ((buf == NULL) || (entlen_padded > bufsize)) {
        return entlen_padded;
    }

    struct fuse_direntplus *dp = (struct fuse_direntplus *)buf;
    memset(&dp->entry_out, 0, sizeof(dp->entry_out));
    fill_entry(&dp->entry_out, e);

    struct fuse_dirent *dirent = &dp->dirent;
    *dirent = (struct fuse_dirent){
        .ino = e->attr.st_ino,
        .off = off,
        .namelen = namelen,
        .type = (e->attr.st_mode & S_IFMT) >> 12,
    };
    memcpy(dirent->name, name, namelen);
    memset(dirent->name + namelen, 0, entlen_padded - entlen);

    return entlen_padded;
}

static void fill_open(struct fuse_open_out *arg, const struct fuse_file_info *f)
{
    arg->fh = f->fh;
    if (f->direct_io) {
        arg->open_flags |= FOPEN_DIRECT_IO;
    }
    if (f->keep_cache) {
        arg->open_flags |= FOPEN_KEEP_CACHE;
    }
    if (f->cache_readdir) {
        arg->open_flags |= FOPEN_CACHE_DIR;
    }
    if (f->nonseekable) {
        arg->open_flags |= FOPEN_NONSEEKABLE;
    }
}

int fuse_reply_entry(fuse_req_t req, const struct fuse_entry_param *e)
{
    struct fuse_entry_out arg;
    size_t size = sizeof(arg);

    memset(&arg, 0, sizeof(arg));
    fill_entry(&arg, e);
    return send_reply_ok(req, &arg, size);
}

int fuse_reply_create(fuse_req_t req, const struct fuse_entry_param *e,
                      const struct fuse_file_info *f)
{
    char buf[sizeof(struct fuse_entry_out) + sizeof(struct fuse_open_out)];
    size_t entrysize = sizeof(struct fuse_entry_out);
    struct fuse_entry_out *earg = (struct fuse_entry_out *)buf;
    struct fuse_open_out *oarg = (struct fuse_open_out *)(buf + entrysize);

    memset(buf, 0, sizeof(buf));
    fill_entry(earg, e);
    fill_open(oarg, f);
    return send_reply_ok(req, buf, entrysize + sizeof(struct fuse_open_out));
}

int fuse_reply_attr(fuse_req_t req, const struct stat *attr,
                    double attr_timeout)
{
    struct fuse_attr_out arg;
    size_t size = sizeof(arg);

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
                                       struct fuse_chan *ch, struct iovec *iov,
                                       int iov_count, struct fuse_bufvec *buf,
                                       size_t len)
{
    /* Optimize common case */
    if (buf->count == 1 && buf->idx == 0 && buf->off == 0 &&
        !(buf->buf[0].flags & FUSE_BUF_IS_FD)) {
        /*
         * FIXME: also avoid memory copy if there are multiple buffers
         * but none of them contain an fd
         */

        iov[iov_count].iov_base = buf->buf[0].mem;
        iov[iov_count].iov_len = len;
        iov_count++;
        return fuse_send_msg(se, ch, iov, iov_count);
    }

    if (fuse_lowlevel_is_virtio(se) && buf->count == 1 &&
        buf->buf[0].flags == (FUSE_BUF_IS_FD | FUSE_BUF_FD_SEEK)) {
        return virtio_send_data_iov(se, ch, iov, iov_count, buf, len);
    }

    abort(); /* Will have taken vhost path */
    return 0;
}

static int fuse_send_data_iov(struct fuse_session *se, struct fuse_chan *ch,
                              struct iovec *iov, int iov_count,
                              struct fuse_bufvec *buf)
{
    size_t len = fuse_buf_size(buf);

    return fuse_send_data_iov_fallback(se, ch, iov, iov_count, buf, len);
}

int fuse_reply_data(fuse_req_t req, struct fuse_bufvec *bufv)
{
    struct iovec iov[2];
    struct fuse_out_header out = {
        .unique = req->unique,
    };
    int res;

    iov[0].iov_base = &out;
    iov[0].iov_len = sizeof(struct fuse_out_header);

    res = fuse_send_data_iov(req->se, req->ch, iov, 1, bufv);
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
    size_t size = sizeof(arg);

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
        if (lock->l_len == 0) {
            arg.lk.end = OFFSET_MAX;
        } else {
            arg.lk.end = lock->l_start + lock->l_len - 1;
        }
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

    fiov = g_try_new(struct fuse_ioctl_iovec, count);
    if (!fiov) {
        return NULL;
    }

    for (i = 0; i < count; i++) {
        fiov[i].base = (uintptr_t)iov[i].iov_base;
        fiov[i].len = iov[i].iov_len;
    }

    return fiov;
}

int fuse_reply_ioctl_retry(fuse_req_t req, const struct iovec *in_iov,
                           size_t in_count, const struct iovec *out_iov,
                           size_t out_count)
{
    struct fuse_ioctl_out arg;
    g_autofree struct fuse_ioctl_iovec *in_fiov = NULL;
    g_autofree struct fuse_ioctl_iovec *out_fiov = NULL;
    struct iovec iov[4];
    size_t count = 1;

    memset(&arg, 0, sizeof(arg));
    arg.flags |= FUSE_IOCTL_RETRY;
    arg.in_iovs = in_count;
    arg.out_iovs = out_count;
    iov[count].iov_base = &arg;
    iov[count].iov_len = sizeof(arg);
    count++;

    /* Can't handle non-compat 64bit ioctls on 32bit */
    if (sizeof(void *) == 4 && req->ioctl_64bit) {
        return fuse_reply_err(req, EINVAL);
    }

    if (in_count) {
        in_fiov = fuse_ioctl_iovec_copy(in_iov, in_count);
        if (!in_fiov) {
            return fuse_reply_err(req, ENOMEM);
        }

        iov[count].iov_base = (void *)in_fiov;
        iov[count].iov_len = sizeof(in_fiov[0]) * in_count;
        count++;
    }
    if (out_count) {
        out_fiov = fuse_ioctl_iovec_copy(out_iov, out_count);
        if (!out_fiov) {
            return fuse_reply_err(req, ENOMEM);
        }

        iov[count].iov_base = (void *)out_fiov;
        iov[count].iov_len = sizeof(out_fiov[0]) * out_count;
        count++;
    }

    return send_reply_iov(req, 0, iov, count);
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
        iov[count].iov_base = (char *)buf;
        iov[count].iov_len = size;
        count++;
    }

    return send_reply_iov(req, 0, iov, count);
}

int fuse_reply_ioctl_iov(fuse_req_t req, int result, const struct iovec *iov,
                         int count)
{
    g_autofree struct iovec *padded_iov = NULL;
    struct fuse_ioctl_out arg;

    padded_iov = g_try_new(struct iovec, count + 2);
    if (padded_iov == NULL) {
        return fuse_reply_err(req, ENOMEM);
    }

    memset(&arg, 0, sizeof(arg));
    arg.result = result;
    padded_iov[1].iov_base = &arg;
    padded_iov[1].iov_len = sizeof(arg);

    memcpy(&padded_iov[2], iov, count * sizeof(struct iovec));

    return send_reply_iov(req, 0, padded_iov, count + 2);
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

static void do_lookup(fuse_req_t req, fuse_ino_t nodeid,
                      struct fuse_mbuf_iter *iter)
{
    const char *name = fuse_mbuf_iter_advance_str(iter);
    if (!name) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    if (req->se->op.lookup) {
        req->se->op.lookup(req, nodeid, name);
    } else {
        fuse_reply_err(req, ENOSYS);
    }
}

static void do_forget(fuse_req_t req, fuse_ino_t nodeid,
                      struct fuse_mbuf_iter *iter)
{
    struct fuse_forget_in *arg;

    arg = fuse_mbuf_iter_advance(iter, sizeof(*arg));
    if (!arg) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    if (req->se->op.forget) {
        req->se->op.forget(req, nodeid, arg->nlookup);
    } else {
        fuse_reply_none(req);
    }
}

static void do_batch_forget(fuse_req_t req, fuse_ino_t nodeid,
                            struct fuse_mbuf_iter *iter)
{
    struct fuse_batch_forget_in *arg;
    struct fuse_forget_data *forgets;
    size_t scount;

    (void)nodeid;

    arg = fuse_mbuf_iter_advance(iter, sizeof(*arg));
    if (!arg) {
        fuse_reply_none(req);
        return;
    }

    /*
     * Prevent integer overflow.  The compiler emits the following warning
     * unless we use the scount local variable:
     *
     * error: comparison is always false due to limited range of data type
     * [-Werror=type-limits]
     *
     * This may be true on 64-bit hosts but we need this check for 32-bit
     * hosts.
     */
    scount = arg->count;
    if (scount > SIZE_MAX / sizeof(forgets[0])) {
        fuse_reply_none(req);
        return;
    }

    forgets = fuse_mbuf_iter_advance(iter, arg->count * sizeof(forgets[0]));
    if (!forgets) {
        fuse_reply_none(req);
        return;
    }

    if (req->se->op.forget_multi) {
        req->se->op.forget_multi(req, arg->count, forgets);
    } else if (req->se->op.forget) {
        unsigned int i;

        for (i = 0; i < arg->count; i++) {
            struct fuse_req *dummy_req;

            dummy_req = fuse_ll_alloc_req(req->se);
            if (dummy_req == NULL) {
                break;
            }

            dummy_req->unique = req->unique;
            dummy_req->ctx = req->ctx;
            dummy_req->ch = NULL;

            req->se->op.forget(dummy_req, forgets[i].ino, forgets[i].nlookup);
        }
        fuse_reply_none(req);
    } else {
        fuse_reply_none(req);
    }
}

static void do_getattr(fuse_req_t req, fuse_ino_t nodeid,
                       struct fuse_mbuf_iter *iter)
{
    struct fuse_file_info *fip = NULL;
    struct fuse_file_info fi;

    struct fuse_getattr_in *arg;

    arg = fuse_mbuf_iter_advance(iter, sizeof(*arg));
    if (!arg) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    if (arg->getattr_flags & FUSE_GETATTR_FH) {
        memset(&fi, 0, sizeof(fi));
        fi.fh = arg->fh;
        fip = &fi;
    }

    if (req->se->op.getattr) {
        req->se->op.getattr(req, nodeid, fip);
    } else {
        fuse_reply_err(req, ENOSYS);
    }
}

static void do_setattr(fuse_req_t req, fuse_ino_t nodeid,
                       struct fuse_mbuf_iter *iter)
{
    if (req->se->op.setattr) {
        struct fuse_setattr_in *arg;
        struct fuse_file_info *fi = NULL;
        struct fuse_file_info fi_store;
        struct stat stbuf;

        arg = fuse_mbuf_iter_advance(iter, sizeof(*arg));
        if (!arg) {
            fuse_reply_err(req, EINVAL);
            return;
        }

        memset(&stbuf, 0, sizeof(stbuf));
        convert_attr(arg, &stbuf);
        if (arg->valid & FATTR_FH) {
            arg->valid &= ~FATTR_FH;
            memset(&fi_store, 0, sizeof(fi_store));
            fi = &fi_store;
            fi->fh = arg->fh;
        }
        arg->valid &= FUSE_SET_ATTR_MODE | FUSE_SET_ATTR_UID |
                      FUSE_SET_ATTR_GID | FUSE_SET_ATTR_SIZE |
                      FUSE_SET_ATTR_ATIME | FUSE_SET_ATTR_MTIME |
                      FUSE_SET_ATTR_ATIME_NOW | FUSE_SET_ATTR_MTIME_NOW |
                      FUSE_SET_ATTR_CTIME | FUSE_SET_ATTR_KILL_SUIDGID;

        req->se->op.setattr(req, nodeid, &stbuf, arg->valid, fi);
    } else {
        fuse_reply_err(req, ENOSYS);
    }
}

static void do_access(fuse_req_t req, fuse_ino_t nodeid,
                      struct fuse_mbuf_iter *iter)
{
    struct fuse_access_in *arg;

    arg = fuse_mbuf_iter_advance(iter, sizeof(*arg));
    if (!arg) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    if (req->se->op.access) {
        req->se->op.access(req, nodeid, arg->mask);
    } else {
        fuse_reply_err(req, ENOSYS);
    }
}

static void do_readlink(fuse_req_t req, fuse_ino_t nodeid,
                        struct fuse_mbuf_iter *iter)
{
    (void)iter;

    if (req->se->op.readlink) {
        req->se->op.readlink(req, nodeid);
    } else {
        fuse_reply_err(req, ENOSYS);
    }
}

static int parse_secctx_fill_req(fuse_req_t req, struct fuse_mbuf_iter *iter)
{
    struct fuse_secctx_header *fsecctx_header;
    struct fuse_secctx *fsecctx;
    const void *secctx;
    const char *name;

    fsecctx_header = fuse_mbuf_iter_advance(iter, sizeof(*fsecctx_header));
    if (!fsecctx_header) {
        return -EINVAL;
    }

    /*
     * As of now maximum of one security context is supported. It can
     * change in future though.
     */
    if (fsecctx_header->nr_secctx > 1) {
        return -EINVAL;
    }

    /* No security context sent. Maybe no LSM supports it */
    if (!fsecctx_header->nr_secctx) {
        return 0;
    }

    fsecctx = fuse_mbuf_iter_advance(iter, sizeof(*fsecctx));
    if (!fsecctx) {
        return -EINVAL;
    }

    /* struct fsecctx with zero sized context is not expected */
    if (!fsecctx->size) {
        return -EINVAL;
    }
    name = fuse_mbuf_iter_advance_str(iter);
    if (!name) {
        return -EINVAL;
    }

    secctx = fuse_mbuf_iter_advance(iter, fsecctx->size);
    if (!secctx) {
        return -EINVAL;
    }

    req->secctx.name = name;
    req->secctx.ctx = secctx;
    req->secctx.ctxlen = fsecctx->size;
    return 0;
}

static void do_mknod(fuse_req_t req, fuse_ino_t nodeid,
                     struct fuse_mbuf_iter *iter)
{
    struct fuse_mknod_in *arg;
    const char *name;
    bool secctx_enabled = req->se->conn.want & FUSE_CAP_SECURITY_CTX;
    int err;

    arg = fuse_mbuf_iter_advance(iter, sizeof(*arg));
    name = fuse_mbuf_iter_advance_str(iter);
    if (!arg || !name) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    req->ctx.umask = arg->umask;

    if (secctx_enabled) {
        err = parse_secctx_fill_req(req, iter);
        if (err) {
            fuse_reply_err(req, -err);
            return;
        }
    }

    if (req->se->op.mknod) {
        req->se->op.mknod(req, nodeid, name, arg->mode, arg->rdev);
    } else {
        fuse_reply_err(req, ENOSYS);
    }
}

static void do_mkdir(fuse_req_t req, fuse_ino_t nodeid,
                     struct fuse_mbuf_iter *iter)
{
    struct fuse_mkdir_in *arg;
    const char *name;
    bool secctx_enabled = req->se->conn.want & FUSE_CAP_SECURITY_CTX;
    int err;

    arg = fuse_mbuf_iter_advance(iter, sizeof(*arg));
    name = fuse_mbuf_iter_advance_str(iter);
    if (!arg || !name) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    req->ctx.umask = arg->umask;

    if (secctx_enabled) {
        err = parse_secctx_fill_req(req, iter);
        if (err) {
            fuse_reply_err(req, err);
            return;
        }
    }

    if (req->se->op.mkdir) {
        req->se->op.mkdir(req, nodeid, name, arg->mode);
    } else {
        fuse_reply_err(req, ENOSYS);
    }
}

static void do_unlink(fuse_req_t req, fuse_ino_t nodeid,
                      struct fuse_mbuf_iter *iter)
{
    const char *name = fuse_mbuf_iter_advance_str(iter);

    if (!name) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    if (req->se->op.unlink) {
        req->se->op.unlink(req, nodeid, name);
    } else {
        fuse_reply_err(req, ENOSYS);
    }
}

static void do_rmdir(fuse_req_t req, fuse_ino_t nodeid,
                     struct fuse_mbuf_iter *iter)
{
    const char *name = fuse_mbuf_iter_advance_str(iter);

    if (!name) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    if (req->se->op.rmdir) {
        req->se->op.rmdir(req, nodeid, name);
    } else {
        fuse_reply_err(req, ENOSYS);
    }
}

static void do_symlink(fuse_req_t req, fuse_ino_t nodeid,
                       struct fuse_mbuf_iter *iter)
{
    const char *name = fuse_mbuf_iter_advance_str(iter);
    const char *linkname = fuse_mbuf_iter_advance_str(iter);
    bool secctx_enabled = req->se->conn.want & FUSE_CAP_SECURITY_CTX;
    int err;

    if (!name || !linkname) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    if (secctx_enabled) {
        err = parse_secctx_fill_req(req, iter);
        if (err) {
            fuse_reply_err(req, err);
            return;
        }
    }

    if (req->se->op.symlink) {
        req->se->op.symlink(req, linkname, nodeid, name);
    } else {
        fuse_reply_err(req, ENOSYS);
    }
}

static void do_rename(fuse_req_t req, fuse_ino_t nodeid,
                      struct fuse_mbuf_iter *iter)
{
    struct fuse_rename_in *arg;
    const char *oldname;
    const char *newname;

    arg = fuse_mbuf_iter_advance(iter, sizeof(*arg));
    oldname = fuse_mbuf_iter_advance_str(iter);
    newname = fuse_mbuf_iter_advance_str(iter);
    if (!arg || !oldname || !newname) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    if (req->se->op.rename) {
        req->se->op.rename(req, nodeid, oldname, arg->newdir, newname, 0);
    } else {
        fuse_reply_err(req, ENOSYS);
    }
}

static void do_rename2(fuse_req_t req, fuse_ino_t nodeid,
                       struct fuse_mbuf_iter *iter)
{
    struct fuse_rename2_in *arg;
    const char *oldname;
    const char *newname;

    arg = fuse_mbuf_iter_advance(iter, sizeof(*arg));
    oldname = fuse_mbuf_iter_advance_str(iter);
    newname = fuse_mbuf_iter_advance_str(iter);
    if (!arg || !oldname || !newname) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    if (req->se->op.rename) {
        req->se->op.rename(req, nodeid, oldname, arg->newdir, newname,
                           arg->flags);
    } else {
        fuse_reply_err(req, ENOSYS);
    }
}

static void do_link(fuse_req_t req, fuse_ino_t nodeid,
                    struct fuse_mbuf_iter *iter)
{
    struct fuse_link_in *arg = fuse_mbuf_iter_advance(iter, sizeof(*arg));
    const char *name = fuse_mbuf_iter_advance_str(iter);

    if (!arg || !name) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    if (req->se->op.link) {
        req->se->op.link(req, arg->oldnodeid, nodeid, name);
    } else {
        fuse_reply_err(req, ENOSYS);
    }
}

static void do_create(fuse_req_t req, fuse_ino_t nodeid,
                      struct fuse_mbuf_iter *iter)
{
    bool secctx_enabled = req->se->conn.want & FUSE_CAP_SECURITY_CTX;

    if (req->se->op.create) {
        struct fuse_create_in *arg;
        struct fuse_file_info fi;
        const char *name;

        arg = fuse_mbuf_iter_advance(iter, sizeof(*arg));
        name = fuse_mbuf_iter_advance_str(iter);
        if (!arg || !name) {
            fuse_reply_err(req, EINVAL);
            return;
        }

        if (secctx_enabled) {
            int err;
            err = parse_secctx_fill_req(req, iter);
            if (err) {
                fuse_reply_err(req, err);
                return;
            }
        }

        memset(&fi, 0, sizeof(fi));
        fi.flags = arg->flags;
        fi.kill_priv = arg->open_flags & FUSE_OPEN_KILL_SUIDGID;

        req->ctx.umask = arg->umask;

        req->se->op.create(req, nodeid, name, arg->mode, &fi);
    } else {
        fuse_reply_err(req, ENOSYS);
    }
}

static void do_open(fuse_req_t req, fuse_ino_t nodeid,
                    struct fuse_mbuf_iter *iter)
{
    struct fuse_open_in *arg;
    struct fuse_file_info fi;

    arg = fuse_mbuf_iter_advance(iter, sizeof(*arg));
    if (!arg) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    /* File creation is handled by do_create() or do_mknod() */
    if (arg->flags & (O_CREAT | O_TMPFILE)) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    memset(&fi, 0, sizeof(fi));
    fi.flags = arg->flags;
    fi.kill_priv = arg->open_flags & FUSE_OPEN_KILL_SUIDGID;

    if (req->se->op.open) {
        req->se->op.open(req, nodeid, &fi);
    } else {
        fuse_reply_open(req, &fi);
    }
}

static void do_read(fuse_req_t req, fuse_ino_t nodeid,
                    struct fuse_mbuf_iter *iter)
{
    if (req->se->op.read) {
        struct fuse_read_in *arg;
        struct fuse_file_info fi;

        arg = fuse_mbuf_iter_advance(iter, sizeof(*arg));
        if (!arg) {
            fuse_reply_err(req, EINVAL);
            return;
        }

        memset(&fi, 0, sizeof(fi));
        fi.fh = arg->fh;
        fi.lock_owner = arg->lock_owner;
        fi.flags = arg->flags;
        req->se->op.read(req, nodeid, arg->size, arg->offset, &fi);
    } else {
        fuse_reply_err(req, ENOSYS);
    }
}

static void do_write(fuse_req_t req, fuse_ino_t nodeid,
                     struct fuse_mbuf_iter *iter)
{
    struct fuse_write_in *arg;
    struct fuse_file_info fi;
    const char *param;

    arg = fuse_mbuf_iter_advance(iter, sizeof(*arg));
    if (!arg) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    param = fuse_mbuf_iter_advance(iter, arg->size);
    if (!param) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    memset(&fi, 0, sizeof(fi));
    fi.fh = arg->fh;
    fi.writepage = (arg->write_flags & FUSE_WRITE_CACHE) != 0;
    fi.kill_priv = !!(arg->write_flags & FUSE_WRITE_KILL_PRIV);

    fi.lock_owner = arg->lock_owner;
    fi.flags = arg->flags;

    if (req->se->op.write) {
        req->se->op.write(req, nodeid, param, arg->size, arg->offset, &fi);
    } else {
        fuse_reply_err(req, ENOSYS);
    }
}

static void do_write_buf(fuse_req_t req, fuse_ino_t nodeid,
                         struct fuse_mbuf_iter *iter, struct fuse_bufvec *ibufv)
{
    struct fuse_session *se = req->se;
    struct fuse_bufvec *pbufv = ibufv;
    struct fuse_bufvec tmpbufv = {
        .buf[0] = ibufv->buf[0],
        .count = 1,
    };
    struct fuse_write_in *arg;
    size_t arg_size = sizeof(*arg);
    struct fuse_file_info fi;

    memset(&fi, 0, sizeof(fi));

    arg = fuse_mbuf_iter_advance(iter, arg_size);
    if (!arg) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    fi.lock_owner = arg->lock_owner;
    fi.flags = arg->flags;
    fi.fh = arg->fh;
    fi.writepage = !!(arg->write_flags & FUSE_WRITE_CACHE);
    fi.kill_priv = !!(arg->write_flags & FUSE_WRITE_KILL_PRIV);

    if (ibufv->count == 1) {
        assert(!(tmpbufv.buf[0].flags & FUSE_BUF_IS_FD));
        tmpbufv.buf[0].mem = ((char *)arg) + arg_size;
        tmpbufv.buf[0].size -= sizeof(struct fuse_in_header) + arg_size;
        pbufv = &tmpbufv;
    } else {
        /*
         *  Input bufv contains the headers in the first element
         * and the data in the rest, we need to skip that first element
         */
        ibufv->buf[0].size = 0;
    }

    if (fuse_buf_size(pbufv) != arg->size) {
        fuse_log(FUSE_LOG_ERR,
                 "fuse: do_write_buf: buffer size doesn't match arg->size\n");
        fuse_reply_err(req, EIO);
        return;
    }

    se->op.write_buf(req, nodeid, pbufv, arg->offset, &fi);
}

static void do_flush(fuse_req_t req, fuse_ino_t nodeid,
                     struct fuse_mbuf_iter *iter)
{
    struct fuse_flush_in *arg;
    struct fuse_file_info fi;

    arg = fuse_mbuf_iter_advance(iter, sizeof(*arg));
    if (!arg) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    memset(&fi, 0, sizeof(fi));
    fi.fh = arg->fh;
    fi.flush = 1;
    fi.lock_owner = arg->lock_owner;

    if (req->se->op.flush) {
        req->se->op.flush(req, nodeid, &fi);
    } else {
        fuse_reply_err(req, ENOSYS);
    }
}

static void do_release(fuse_req_t req, fuse_ino_t nodeid,
                       struct fuse_mbuf_iter *iter)
{
    struct fuse_release_in *arg;
    struct fuse_file_info fi;

    arg = fuse_mbuf_iter_advance(iter, sizeof(*arg));
    if (!arg) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    memset(&fi, 0, sizeof(fi));
    fi.flags = arg->flags;
    fi.fh = arg->fh;
    fi.flush = (arg->release_flags & FUSE_RELEASE_FLUSH) ? 1 : 0;
    fi.lock_owner = arg->lock_owner;

    if (arg->release_flags & FUSE_RELEASE_FLOCK_UNLOCK) {
        fi.flock_release = 1;
    }

    if (req->se->op.release) {
        req->se->op.release(req, nodeid, &fi);
    } else {
        fuse_reply_err(req, 0);
    }
}

static void do_fsync(fuse_req_t req, fuse_ino_t nodeid,
                     struct fuse_mbuf_iter *iter)
{
    struct fuse_fsync_in *arg;
    struct fuse_file_info fi;
    int datasync;

    arg = fuse_mbuf_iter_advance(iter, sizeof(*arg));
    if (!arg) {
        fuse_reply_err(req, EINVAL);
        return;
    }
    datasync = arg->fsync_flags & 1;

    memset(&fi, 0, sizeof(fi));
    fi.fh = arg->fh;

    if (req->se->op.fsync) {
        if (fi.fh == (uint64_t)-1) {
            req->se->op.fsync(req, nodeid, datasync, NULL);
        } else {
            req->se->op.fsync(req, nodeid, datasync, &fi);
        }
    } else {
        fuse_reply_err(req, ENOSYS);
    }
}

static void do_opendir(fuse_req_t req, fuse_ino_t nodeid,
                       struct fuse_mbuf_iter *iter)
{
    struct fuse_open_in *arg;
    struct fuse_file_info fi;

    arg = fuse_mbuf_iter_advance(iter, sizeof(*arg));
    if (!arg) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    memset(&fi, 0, sizeof(fi));
    fi.flags = arg->flags;

    if (req->se->op.opendir) {
        req->se->op.opendir(req, nodeid, &fi);
    } else {
        fuse_reply_open(req, &fi);
    }
}

static void do_readdir(fuse_req_t req, fuse_ino_t nodeid,
                       struct fuse_mbuf_iter *iter)
{
    struct fuse_read_in *arg;
    struct fuse_file_info fi;

    arg = fuse_mbuf_iter_advance(iter, sizeof(*arg));
    if (!arg) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    memset(&fi, 0, sizeof(fi));
    fi.fh = arg->fh;

    if (req->se->op.readdir) {
        req->se->op.readdir(req, nodeid, arg->size, arg->offset, &fi);
    } else {
        fuse_reply_err(req, ENOSYS);
    }
}

static void do_readdirplus(fuse_req_t req, fuse_ino_t nodeid,
                           struct fuse_mbuf_iter *iter)
{
    struct fuse_read_in *arg;
    struct fuse_file_info fi;

    arg = fuse_mbuf_iter_advance(iter, sizeof(*arg));
    if (!arg) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    memset(&fi, 0, sizeof(fi));
    fi.fh = arg->fh;

    if (req->se->op.readdirplus) {
        req->se->op.readdirplus(req, nodeid, arg->size, arg->offset, &fi);
    } else {
        fuse_reply_err(req, ENOSYS);
    }
}

static void do_releasedir(fuse_req_t req, fuse_ino_t nodeid,
                          struct fuse_mbuf_iter *iter)
{
    struct fuse_release_in *arg;
    struct fuse_file_info fi;

    arg = fuse_mbuf_iter_advance(iter, sizeof(*arg));
    if (!arg) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    memset(&fi, 0, sizeof(fi));
    fi.flags = arg->flags;
    fi.fh = arg->fh;

    if (req->se->op.releasedir) {
        req->se->op.releasedir(req, nodeid, &fi);
    } else {
        fuse_reply_err(req, 0);
    }
}

static void do_fsyncdir(fuse_req_t req, fuse_ino_t nodeid,
                        struct fuse_mbuf_iter *iter)
{
    struct fuse_fsync_in *arg;
    struct fuse_file_info fi;
    int datasync;

    arg = fuse_mbuf_iter_advance(iter, sizeof(*arg));
    if (!arg) {
        fuse_reply_err(req, EINVAL);
        return;
    }
    datasync = arg->fsync_flags & 1;

    memset(&fi, 0, sizeof(fi));
    fi.fh = arg->fh;

    if (req->se->op.fsyncdir) {
        req->se->op.fsyncdir(req, nodeid, datasync, &fi);
    } else {
        fuse_reply_err(req, ENOSYS);
    }
}

static void do_statfs(fuse_req_t req, fuse_ino_t nodeid,
                      struct fuse_mbuf_iter *iter)
{
    (void)nodeid;
    (void)iter;

    if (req->se->op.statfs) {
        req->se->op.statfs(req, nodeid);
    } else {
        struct statvfs buf = {
            .f_namemax = 255,
            .f_bsize = 512,
        };
        fuse_reply_statfs(req, &buf);
    }
}

static void do_setxattr(fuse_req_t req, fuse_ino_t nodeid,
                        struct fuse_mbuf_iter *iter)
{
    struct fuse_setxattr_in *arg;
    const char *name;
    const char *value;
    bool setxattr_ext = req->se->conn.want & FUSE_CAP_SETXATTR_EXT;

    if (setxattr_ext) {
        arg = fuse_mbuf_iter_advance(iter, sizeof(*arg));
    } else {
        arg = fuse_mbuf_iter_advance(iter, FUSE_COMPAT_SETXATTR_IN_SIZE);
    }
    name = fuse_mbuf_iter_advance_str(iter);
    if (!arg || !name) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    value = fuse_mbuf_iter_advance(iter, arg->size);
    if (!value) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    if (req->se->op.setxattr) {
        uint32_t setxattr_flags = setxattr_ext ? arg->setxattr_flags : 0;
        req->se->op.setxattr(req, nodeid, name, value, arg->size, arg->flags,
                             setxattr_flags);
    } else {
        fuse_reply_err(req, ENOSYS);
    }
}

static void do_getxattr(fuse_req_t req, fuse_ino_t nodeid,
                        struct fuse_mbuf_iter *iter)
{
    struct fuse_getxattr_in *arg;
    const char *name;

    arg = fuse_mbuf_iter_advance(iter, sizeof(*arg));
    name = fuse_mbuf_iter_advance_str(iter);
    if (!arg || !name) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    if (req->se->op.getxattr) {
        req->se->op.getxattr(req, nodeid, name, arg->size);
    } else {
        fuse_reply_err(req, ENOSYS);
    }
}

static void do_listxattr(fuse_req_t req, fuse_ino_t nodeid,
                         struct fuse_mbuf_iter *iter)
{
    struct fuse_getxattr_in *arg;

    arg = fuse_mbuf_iter_advance(iter, sizeof(*arg));
    if (!arg) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    if (req->se->op.listxattr) {
        req->se->op.listxattr(req, nodeid, arg->size);
    } else {
        fuse_reply_err(req, ENOSYS);
    }
}

static void do_removexattr(fuse_req_t req, fuse_ino_t nodeid,
                           struct fuse_mbuf_iter *iter)
{
    const char *name = fuse_mbuf_iter_advance_str(iter);

    if (!name) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    if (req->se->op.removexattr) {
        req->se->op.removexattr(req, nodeid, name);
    } else {
        fuse_reply_err(req, ENOSYS);
    }
}

static void convert_fuse_file_lock(struct fuse_file_lock *fl,
                                   struct flock *flock)
{
    memset(flock, 0, sizeof(struct flock));
    flock->l_type = fl->type;
    flock->l_whence = SEEK_SET;
    flock->l_start = fl->start;
    if (fl->end == OFFSET_MAX) {
        flock->l_len = 0;
    } else {
        flock->l_len = fl->end - fl->start + 1;
    }
    flock->l_pid = fl->pid;
}

static void do_getlk(fuse_req_t req, fuse_ino_t nodeid,
                     struct fuse_mbuf_iter *iter)
{
    struct fuse_lk_in *arg;
    struct fuse_file_info fi;
    struct flock flock;

    arg = fuse_mbuf_iter_advance(iter, sizeof(*arg));
    if (!arg) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    memset(&fi, 0, sizeof(fi));
    fi.fh = arg->fh;
    fi.lock_owner = arg->owner;

    convert_fuse_file_lock(&arg->lk, &flock);
    if (req->se->op.getlk) {
        req->se->op.getlk(req, nodeid, &fi, &flock);
    } else {
        fuse_reply_err(req, ENOSYS);
    }
}

static void do_setlk_common(fuse_req_t req, fuse_ino_t nodeid,
                            struct fuse_mbuf_iter *iter, int sleep)
{
    struct fuse_lk_in *arg;
    struct fuse_file_info fi;
    struct flock flock;

    arg = fuse_mbuf_iter_advance(iter, sizeof(*arg));
    if (!arg) {
        fuse_reply_err(req, EINVAL);
        return;
    }

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
        if (!sleep) {
            op |= LOCK_NB;
        }

        if (req->se->op.flock) {
            req->se->op.flock(req, nodeid, &fi, op);
        } else {
            fuse_reply_err(req, ENOSYS);
        }
    } else {
        convert_fuse_file_lock(&arg->lk, &flock);
        if (req->se->op.setlk) {
            req->se->op.setlk(req, nodeid, &fi, &flock, sleep);
        } else {
            fuse_reply_err(req, ENOSYS);
        }
    }
}

static void do_setlk(fuse_req_t req, fuse_ino_t nodeid,
                     struct fuse_mbuf_iter *iter)
{
    do_setlk_common(req, nodeid, iter, 0);
}

static void do_setlkw(fuse_req_t req, fuse_ino_t nodeid,
                      struct fuse_mbuf_iter *iter)
{
    do_setlk_common(req, nodeid, iter, 1);
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
            if (func) {
                func(curr, data);
            }
            pthread_mutex_unlock(&curr->lock);

            pthread_mutex_lock(&se->lock);
            curr->ctr--;
            if (!curr->ctr) {
                destroy_req(curr);
            }

            return 1;
        }
    }
    for (curr = se->interrupts.next; curr != &se->interrupts;
         curr = curr->next) {
        if (curr->u.i.unique == req->u.i.unique) {
            return 1;
        }
    }
    return 0;
}

static void do_interrupt(fuse_req_t req, fuse_ino_t nodeid,
                         struct fuse_mbuf_iter *iter)
{
    struct fuse_interrupt_in *arg;
    struct fuse_session *se = req->se;

    (void)nodeid;

    arg = fuse_mbuf_iter_advance(iter, sizeof(*arg));
    if (!arg) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    fuse_log(FUSE_LOG_DEBUG, "INTERRUPT: %llu\n",
             (unsigned long long)arg->unique);

    req->u.i.unique = arg->unique;

    pthread_mutex_lock(&se->lock);
    if (find_interrupted(se, req)) {
        destroy_req(req);
    } else {
        list_add_req(req, &se->interrupts);
    }
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
            g_free(curr);
            return NULL;
        }
    }
    curr = se->interrupts.next;
    if (curr != &se->interrupts) {
        list_del_req(curr);
        list_init_req(curr);
        return curr;
    } else {
        return NULL;
    }
}

static void do_bmap(fuse_req_t req, fuse_ino_t nodeid,
                    struct fuse_mbuf_iter *iter)
{
    struct fuse_bmap_in *arg = fuse_mbuf_iter_advance(iter, sizeof(*arg));

    if (!arg) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    if (req->se->op.bmap) {
        req->se->op.bmap(req, nodeid, arg->blocksize, arg->block);
    } else {
        fuse_reply_err(req, ENOSYS);
    }
}

static void do_ioctl(fuse_req_t req, fuse_ino_t nodeid,
                     struct fuse_mbuf_iter *iter)
{
    struct fuse_ioctl_in *arg;
    unsigned int flags;
    void *in_buf = NULL;
    struct fuse_file_info fi;

    arg = fuse_mbuf_iter_advance(iter, sizeof(*arg));
    if (!arg) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    flags = arg->flags;
    if (flags & FUSE_IOCTL_DIR && !(req->se->conn.want & FUSE_CAP_IOCTL_DIR)) {
        fuse_reply_err(req, ENOTTY);
        return;
    }

    if (arg->in_size) {
        in_buf = fuse_mbuf_iter_advance(iter, arg->in_size);
        if (!in_buf) {
            fuse_reply_err(req, EINVAL);
            return;
        }
    }

    memset(&fi, 0, sizeof(fi));
    fi.fh = arg->fh;

    if (sizeof(void *) == 4 && !(flags & FUSE_IOCTL_32BIT)) {
        req->ioctl_64bit = 1;
    }

    if (req->se->op.ioctl) {
        req->se->op.ioctl(req, nodeid, arg->cmd, (void *)(uintptr_t)arg->arg,
                          &fi, flags, in_buf, arg->in_size, arg->out_size);
    } else {
        fuse_reply_err(req, ENOSYS);
    }
}

void fuse_pollhandle_destroy(struct fuse_pollhandle *ph)
{
    free(ph);
}

static void do_poll(fuse_req_t req, fuse_ino_t nodeid,
                    struct fuse_mbuf_iter *iter)
{
    struct fuse_poll_in *arg;
    struct fuse_file_info fi;

    arg = fuse_mbuf_iter_advance(iter, sizeof(*arg));
    if (!arg) {
        fuse_reply_err(req, EINVAL);
        return;
    }

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

static void do_fallocate(fuse_req_t req, fuse_ino_t nodeid,
                         struct fuse_mbuf_iter *iter)
{
    struct fuse_fallocate_in *arg;
    struct fuse_file_info fi;

    arg = fuse_mbuf_iter_advance(iter, sizeof(*arg));
    if (!arg) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    memset(&fi, 0, sizeof(fi));
    fi.fh = arg->fh;

    if (req->se->op.fallocate) {
        req->se->op.fallocate(req, nodeid, arg->mode, arg->offset, arg->length,
                              &fi);
    } else {
        fuse_reply_err(req, ENOSYS);
    }
}

static void do_copy_file_range(fuse_req_t req, fuse_ino_t nodeid_in,
                               struct fuse_mbuf_iter *iter)
{
    struct fuse_copy_file_range_in *arg;
    struct fuse_file_info fi_in, fi_out;

    arg = fuse_mbuf_iter_advance(iter, sizeof(*arg));
    if (!arg) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    memset(&fi_in, 0, sizeof(fi_in));
    fi_in.fh = arg->fh_in;

    memset(&fi_out, 0, sizeof(fi_out));
    fi_out.fh = arg->fh_out;


    if (req->se->op.copy_file_range) {
        req->se->op.copy_file_range(req, nodeid_in, arg->off_in, &fi_in,
                                    arg->nodeid_out, arg->off_out, &fi_out,
                                    arg->len, arg->flags);
    } else {
        fuse_reply_err(req, ENOSYS);
    }
}

static void do_lseek(fuse_req_t req, fuse_ino_t nodeid,
                     struct fuse_mbuf_iter *iter)
{
    struct fuse_lseek_in *arg;
    struct fuse_file_info fi;

    arg = fuse_mbuf_iter_advance(iter, sizeof(*arg));
    if (!arg) {
        fuse_reply_err(req, EINVAL);
        return;
    }
    memset(&fi, 0, sizeof(fi));
    fi.fh = arg->fh;

    if (req->se->op.lseek) {
        req->se->op.lseek(req, nodeid, arg->offset, arg->whence, &fi);
    } else {
        fuse_reply_err(req, ENOSYS);
    }
}

static void do_syncfs(fuse_req_t req, fuse_ino_t nodeid,
                      struct fuse_mbuf_iter *iter)
{
    if (req->se->op.syncfs) {
        req->se->op.syncfs(req, nodeid);
    } else {
        fuse_reply_err(req, ENOSYS);
    }
}

static void do_init(fuse_req_t req, fuse_ino_t nodeid,
                    struct fuse_mbuf_iter *iter)
{
    size_t compat_size = offsetof(struct fuse_init_in, max_readahead);
    size_t compat2_size = offsetof(struct fuse_init_in, flags) +
                              sizeof(uint32_t);
    /* Fuse structure extended with minor version 36 */
    size_t compat3_size = endof(struct fuse_init_in, unused);
    struct fuse_init_in *arg;
    struct fuse_init_out outarg;
    struct fuse_session *se = req->se;
    size_t bufsize = se->bufsize;
    size_t outargsize = sizeof(outarg);
    uint64_t flags = 0;

    (void)nodeid;

    /* First consume the old fields... */
    arg = fuse_mbuf_iter_advance(iter, compat_size);
    if (!arg) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    /* ...and now consume the new fields. */
    if (arg->major == 7 && arg->minor >= 6) {
        if (!fuse_mbuf_iter_advance(iter, compat2_size - compat_size)) {
            fuse_reply_err(req, EINVAL);
            return;
        }
        flags |= arg->flags;
    }

    /*
     * fuse_init_in was extended again with minor version 36. Just read
     * current known size of fuse_init so that future extension and
     * header rebase does not cause breakage.
     */
    if (sizeof(*arg) > compat2_size && (arg->flags & FUSE_INIT_EXT)) {
        if (!fuse_mbuf_iter_advance(iter, compat3_size - compat2_size)) {
            fuse_reply_err(req, EINVAL);
            return;
        }
        flags |= (uint64_t) arg->flags2 << 32;
    }

    fuse_log(FUSE_LOG_DEBUG, "INIT: %u.%u\n", arg->major, arg->minor);
    if (arg->major == 7 && arg->minor >= 6) {
        fuse_log(FUSE_LOG_DEBUG, "flags=0x%016" PRIx64 "\n", flags);
        fuse_log(FUSE_LOG_DEBUG, "max_readahead=0x%08x\n", arg->max_readahead);
    }
    se->conn.proto_major = arg->major;
    se->conn.proto_minor = arg->minor;
    se->conn.capable = 0;
    se->conn.want = 0;

    memset(&outarg, 0, sizeof(outarg));
    outarg.major = FUSE_KERNEL_VERSION;
    outarg.minor = FUSE_KERNEL_MINOR_VERSION;

    if (arg->major < 7 || (arg->major == 7 && arg->minor < 31)) {
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

    if (arg->max_readahead < se->conn.max_readahead) {
        se->conn.max_readahead = arg->max_readahead;
    }
    if (flags & FUSE_ASYNC_READ) {
        se->conn.capable |= FUSE_CAP_ASYNC_READ;
    }
    if (flags & FUSE_POSIX_LOCKS) {
        se->conn.capable |= FUSE_CAP_POSIX_LOCKS;
    }
    if (flags & FUSE_ATOMIC_O_TRUNC) {
        se->conn.capable |= FUSE_CAP_ATOMIC_O_TRUNC;
    }
    if (flags & FUSE_EXPORT_SUPPORT) {
        se->conn.capable |= FUSE_CAP_EXPORT_SUPPORT;
    }
    if (flags & FUSE_DONT_MASK) {
        se->conn.capable |= FUSE_CAP_DONT_MASK;
    }
    if (flags & FUSE_FLOCK_LOCKS) {
        se->conn.capable |= FUSE_CAP_FLOCK_LOCKS;
    }
    if (flags & FUSE_AUTO_INVAL_DATA) {
        se->conn.capable |= FUSE_CAP_AUTO_INVAL_DATA;
    }
    if (flags & FUSE_DO_READDIRPLUS) {
        se->conn.capable |= FUSE_CAP_READDIRPLUS;
    }
    if (flags & FUSE_READDIRPLUS_AUTO) {
        se->conn.capable |= FUSE_CAP_READDIRPLUS_AUTO;
    }
    if (flags & FUSE_ASYNC_DIO) {
        se->conn.capable |= FUSE_CAP_ASYNC_DIO;
    }
    if (flags & FUSE_WRITEBACK_CACHE) {
        se->conn.capable |= FUSE_CAP_WRITEBACK_CACHE;
    }
    if (flags & FUSE_NO_OPEN_SUPPORT) {
        se->conn.capable |= FUSE_CAP_NO_OPEN_SUPPORT;
    }
    if (flags & FUSE_PARALLEL_DIROPS) {
        se->conn.capable |= FUSE_CAP_PARALLEL_DIROPS;
    }
    if (flags & FUSE_POSIX_ACL) {
        se->conn.capable |= FUSE_CAP_POSIX_ACL;
    }
    if (flags & FUSE_HANDLE_KILLPRIV) {
        se->conn.capable |= FUSE_CAP_HANDLE_KILLPRIV;
    }
    if (flags & FUSE_NO_OPENDIR_SUPPORT) {
        se->conn.capable |= FUSE_CAP_NO_OPENDIR_SUPPORT;
    }
    if (!(flags & FUSE_MAX_PAGES)) {
        size_t max_bufsize = FUSE_DEFAULT_MAX_PAGES_PER_REQ * getpagesize() +
                             FUSE_BUFFER_HEADER_SIZE;
        if (bufsize > max_bufsize) {
            bufsize = max_bufsize;
        }
    }
    if (flags & FUSE_SUBMOUNTS) {
        se->conn.capable |= FUSE_CAP_SUBMOUNTS;
    }
    if (flags & FUSE_HANDLE_KILLPRIV_V2) {
        se->conn.capable |= FUSE_CAP_HANDLE_KILLPRIV_V2;
    }
    if (flags & FUSE_SETXATTR_EXT) {
        se->conn.capable |= FUSE_CAP_SETXATTR_EXT;
    }
    if (flags & FUSE_SECURITY_CTX) {
        se->conn.capable |= FUSE_CAP_SECURITY_CTX;
    }
#ifdef HAVE_SPLICE
#ifdef HAVE_VMSPLICE
    se->conn.capable |= FUSE_CAP_SPLICE_WRITE | FUSE_CAP_SPLICE_MOVE;
#endif
    se->conn.capable |= FUSE_CAP_SPLICE_READ;
#endif
    se->conn.capable |= FUSE_CAP_IOCTL_DIR;

    /*
     * Default settings for modern filesystems.
     *
     * Most of these capabilities were disabled by default in
     * libfuse2 for backwards compatibility reasons. In libfuse3,
     * we can finally enable them by default (as long as they're
     * supported by the kernel).
     */
#define LL_SET_DEFAULT(cond, cap)             \
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
    LL_SET_DEFAULT(se->op.getlk && se->op.setlk, FUSE_CAP_POSIX_LOCKS);
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

    if (se->conn.max_write > bufsize - FUSE_BUFFER_HEADER_SIZE) {
        se->conn.max_write = bufsize - FUSE_BUFFER_HEADER_SIZE;
    }

    se->got_init = 1;
    se->got_destroy = 0;
    if (se->op.init) {
        se->op.init(se->userdata, &se->conn);
    }

    if (se->conn.want & (~se->conn.capable)) {
        fuse_log(FUSE_LOG_ERR,
                 "fuse: error: filesystem requested capabilities "
                 "0x%" PRIx64 " that are not supported by kernel, aborting.\n",
                 se->conn.want & (~se->conn.capable));
        fuse_reply_err(req, EPROTO);
        se->error = -EPROTO;
        fuse_session_exit(se);
        return;
    }

    if (se->conn.max_write < bufsize - FUSE_BUFFER_HEADER_SIZE) {
        se->bufsize = se->conn.max_write + FUSE_BUFFER_HEADER_SIZE;
    }
    if (flags & FUSE_MAX_PAGES) {
        outarg.flags |= FUSE_MAX_PAGES;
        outarg.max_pages = (se->conn.max_write - 1) / getpagesize() + 1;
    }

    /*
     * Always enable big writes, this is superseded
     * by the max_write option
     */
    outarg.flags |= FUSE_BIG_WRITES;

    if (se->conn.want & FUSE_CAP_ASYNC_READ) {
        outarg.flags |= FUSE_ASYNC_READ;
    }
    if (se->conn.want & FUSE_CAP_PARALLEL_DIROPS) {
        outarg.flags |= FUSE_PARALLEL_DIROPS;
    }
    if (se->conn.want & FUSE_CAP_POSIX_LOCKS) {
        outarg.flags |= FUSE_POSIX_LOCKS;
    }
    if (se->conn.want & FUSE_CAP_ATOMIC_O_TRUNC) {
        outarg.flags |= FUSE_ATOMIC_O_TRUNC;
    }
    if (se->conn.want & FUSE_CAP_EXPORT_SUPPORT) {
        outarg.flags |= FUSE_EXPORT_SUPPORT;
    }
    if (se->conn.want & FUSE_CAP_DONT_MASK) {
        outarg.flags |= FUSE_DONT_MASK;
    }
    if (se->conn.want & FUSE_CAP_FLOCK_LOCKS) {
        outarg.flags |= FUSE_FLOCK_LOCKS;
    }
    if (se->conn.want & FUSE_CAP_AUTO_INVAL_DATA) {
        outarg.flags |= FUSE_AUTO_INVAL_DATA;
    }
    if (se->conn.want & FUSE_CAP_READDIRPLUS) {
        outarg.flags |= FUSE_DO_READDIRPLUS;
    }
    if (se->conn.want & FUSE_CAP_READDIRPLUS_AUTO) {
        outarg.flags |= FUSE_READDIRPLUS_AUTO;
    }
    if (se->conn.want & FUSE_CAP_ASYNC_DIO) {
        outarg.flags |= FUSE_ASYNC_DIO;
    }
    if (se->conn.want & FUSE_CAP_WRITEBACK_CACHE) {
        outarg.flags |= FUSE_WRITEBACK_CACHE;
    }
    if (se->conn.want & FUSE_CAP_POSIX_ACL) {
        outarg.flags |= FUSE_POSIX_ACL;
    }
    outarg.max_readahead = se->conn.max_readahead;
    outarg.max_write = se->conn.max_write;
    if (se->conn.max_background >= (1 << 16)) {
        se->conn.max_background = (1 << 16) - 1;
    }
    if (se->conn.congestion_threshold > se->conn.max_background) {
        se->conn.congestion_threshold = se->conn.max_background;
    }
    if (!se->conn.congestion_threshold) {
        se->conn.congestion_threshold = se->conn.max_background * 3 / 4;
    }

    outarg.max_background = se->conn.max_background;
    outarg.congestion_threshold = se->conn.congestion_threshold;
    outarg.time_gran = se->conn.time_gran;

    if (se->conn.want & FUSE_CAP_HANDLE_KILLPRIV_V2) {
        outarg.flags |= FUSE_HANDLE_KILLPRIV_V2;
    }

    if (se->conn.want & FUSE_CAP_SETXATTR_EXT) {
        outarg.flags |= FUSE_SETXATTR_EXT;
    }

    if (se->conn.want & FUSE_CAP_SECURITY_CTX) {
        /* bits 32..63 get shifted down 32 bits into the flags2 field */
        outarg.flags2 |= FUSE_SECURITY_CTX >> 32;
    }

    fuse_log(FUSE_LOG_DEBUG, "   INIT: %u.%u\n", outarg.major, outarg.minor);
    fuse_log(FUSE_LOG_DEBUG, "   flags2=0x%08x flags=0x%08x\n", outarg.flags2,
             outarg.flags);
    fuse_log(FUSE_LOG_DEBUG, "   max_readahead=0x%08x\n", outarg.max_readahead);
    fuse_log(FUSE_LOG_DEBUG, "   max_write=0x%08x\n", outarg.max_write);
    fuse_log(FUSE_LOG_DEBUG, "   max_background=%i\n", outarg.max_background);
    fuse_log(FUSE_LOG_DEBUG, "   congestion_threshold=%i\n",
             outarg.congestion_threshold);
    fuse_log(FUSE_LOG_DEBUG, "   time_gran=%u\n", outarg.time_gran);

    send_reply_ok(req, &outarg, outargsize);
}

static void do_destroy(fuse_req_t req, fuse_ino_t nodeid,
                       struct fuse_mbuf_iter *iter)
{
    struct fuse_session *se = req->se;

    (void)nodeid;
    (void)iter;

    se->got_destroy = 1;
    se->got_init = 0;
    if (se->op.destroy) {
        se->op.destroy(se->userdata);
    }

    send_reply_ok(req, NULL, 0);
}

int fuse_lowlevel_notify_store(struct fuse_session *se, fuse_ino_t ino,
                               off_t offset, struct fuse_bufvec *bufv)
{
    struct fuse_out_header out = {
        .error = FUSE_NOTIFY_STORE,
    };
    struct fuse_notify_store_out outarg = {
        .nodeid = ino,
        .offset = offset,
        .size = fuse_buf_size(bufv),
    };
    struct iovec iov[3];
    int res;

    if (!se) {
        return -EINVAL;
    }

    iov[0].iov_base = &out;
    iov[0].iov_len = sizeof(out);
    iov[1].iov_base = &outarg;
    iov[1].iov_len = sizeof(outarg);

    res = fuse_send_data_iov(se, NULL, iov, 2, bufv);
    if (res > 0) {
        res = -res;
    }

    return res;
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
    if (req->interrupted && func) {
        func(req, data);
    }
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
    void (*func)(fuse_req_t, fuse_ino_t, struct fuse_mbuf_iter *);
    const char *name;
} fuse_ll_ops[] = {
    [FUSE_LOOKUP] = { do_lookup, "LOOKUP" },
    [FUSE_FORGET] = { do_forget, "FORGET" },
    [FUSE_GETATTR] = { do_getattr, "GETATTR" },
    [FUSE_SETATTR] = { do_setattr, "SETATTR" },
    [FUSE_READLINK] = { do_readlink, "READLINK" },
    [FUSE_SYMLINK] = { do_symlink, "SYMLINK" },
    [FUSE_MKNOD] = { do_mknod, "MKNOD" },
    [FUSE_MKDIR] = { do_mkdir, "MKDIR" },
    [FUSE_UNLINK] = { do_unlink, "UNLINK" },
    [FUSE_RMDIR] = { do_rmdir, "RMDIR" },
    [FUSE_RENAME] = { do_rename, "RENAME" },
    [FUSE_LINK] = { do_link, "LINK" },
    [FUSE_OPEN] = { do_open, "OPEN" },
    [FUSE_READ] = { do_read, "READ" },
    [FUSE_WRITE] = { do_write, "WRITE" },
    [FUSE_STATFS] = { do_statfs, "STATFS" },
    [FUSE_RELEASE] = { do_release, "RELEASE" },
    [FUSE_FSYNC] = { do_fsync, "FSYNC" },
    [FUSE_SETXATTR] = { do_setxattr, "SETXATTR" },
    [FUSE_GETXATTR] = { do_getxattr, "GETXATTR" },
    [FUSE_LISTXATTR] = { do_listxattr, "LISTXATTR" },
    [FUSE_REMOVEXATTR] = { do_removexattr, "REMOVEXATTR" },
    [FUSE_FLUSH] = { do_flush, "FLUSH" },
    [FUSE_INIT] = { do_init, "INIT" },
    [FUSE_OPENDIR] = { do_opendir, "OPENDIR" },
    [FUSE_READDIR] = { do_readdir, "READDIR" },
    [FUSE_RELEASEDIR] = { do_releasedir, "RELEASEDIR" },
    [FUSE_FSYNCDIR] = { do_fsyncdir, "FSYNCDIR" },
    [FUSE_GETLK] = { do_getlk, "GETLK" },
    [FUSE_SETLK] = { do_setlk, "SETLK" },
    [FUSE_SETLKW] = { do_setlkw, "SETLKW" },
    [FUSE_ACCESS] = { do_access, "ACCESS" },
    [FUSE_CREATE] = { do_create, "CREATE" },
    [FUSE_INTERRUPT] = { do_interrupt, "INTERRUPT" },
    [FUSE_BMAP] = { do_bmap, "BMAP" },
    [FUSE_IOCTL] = { do_ioctl, "IOCTL" },
    [FUSE_POLL] = { do_poll, "POLL" },
    [FUSE_FALLOCATE] = { do_fallocate, "FALLOCATE" },
    [FUSE_DESTROY] = { do_destroy, "DESTROY" },
    [FUSE_NOTIFY_REPLY] = { NULL, "NOTIFY_REPLY" },
    [FUSE_BATCH_FORGET] = { do_batch_forget, "BATCH_FORGET" },
    [FUSE_READDIRPLUS] = { do_readdirplus, "READDIRPLUS" },
    [FUSE_RENAME2] = { do_rename2, "RENAME2" },
    [FUSE_COPY_FILE_RANGE] = { do_copy_file_range, "COPY_FILE_RANGE" },
    [FUSE_LSEEK] = { do_lseek, "LSEEK" },
    [FUSE_SYNCFS] = { do_syncfs, "SYNCFS" },
};

#define FUSE_MAXOP (sizeof(fuse_ll_ops) / sizeof(fuse_ll_ops[0]))

static const char *opname(enum fuse_opcode opcode)
{
    if (opcode >= FUSE_MAXOP || !fuse_ll_ops[opcode].name) {
        return "???";
    } else {
        return fuse_ll_ops[opcode].name;
    }
}

void fuse_session_process_buf(struct fuse_session *se,
                              const struct fuse_buf *buf)
{
    struct fuse_bufvec bufv = { .buf[0] = *buf, .count = 1 };
    fuse_session_process_buf_int(se, &bufv, NULL);
}

/*
 * Restriction:
 *   bufv is normally a single entry buffer, except for a write
 *   where (if it's in memory) then the bufv may be multiple entries,
 *   where the first entry contains all headers and subsequent entries
 *   contain data
 *   bufv shall not use any offsets etc to make the data anything
 *   other than contiguous starting from 0.
 */
void fuse_session_process_buf_int(struct fuse_session *se,
                                  struct fuse_bufvec *bufv,
                                  struct fuse_chan *ch)
{
    const struct fuse_buf *buf = bufv->buf;
    struct fuse_mbuf_iter iter = FUSE_MBUF_ITER_INIT(buf);
    struct fuse_in_header *in;
    struct fuse_req *req;
    int err;

    /* The first buffer must be a memory buffer */
    assert(!(buf->flags & FUSE_BUF_IS_FD));

    in = fuse_mbuf_iter_advance(&iter, sizeof(*in));
    assert(in); /* caller guarantees the input buffer is large enough */

    fuse_log(
        FUSE_LOG_DEBUG,
        "unique: %llu, opcode: %s (%i), nodeid: %llu, insize: %zu, pid: %u\n",
        (unsigned long long)in->unique, opname((enum fuse_opcode)in->opcode),
        in->opcode, (unsigned long long)in->nodeid, buf->size, in->pid);

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
        return;
    }

    req->unique = in->unique;
    req->ctx.uid = in->uid;
    req->ctx.gid = in->gid;
    req->ctx.pid = in->pid;
    req->ch = ch;

    /*
     * INIT and DESTROY requests are serialized, all other request types
     * run in parallel.  This prevents races between FUSE_INIT and ordinary
     * requests, FUSE_INIT and FUSE_INIT, FUSE_INIT and FUSE_DESTROY, and
     * FUSE_DESTROY and FUSE_DESTROY.
     */
    if (in->opcode == FUSE_INIT || in->opcode == CUSE_INIT ||
        in->opcode == FUSE_DESTROY) {
        pthread_rwlock_wrlock(&se->init_rwlock);
    } else {
        pthread_rwlock_rdlock(&se->init_rwlock);
    }

    err = EIO;
    if (!se->got_init) {
        enum fuse_opcode expected;

        expected = se->cuse_data ? CUSE_INIT : FUSE_INIT;
        if (in->opcode != expected) {
            goto reply_err;
        }
    } else if (in->opcode == FUSE_INIT || in->opcode == CUSE_INIT) {
        if (fuse_lowlevel_is_virtio(se)) {
            /*
             * TODO: This is after a hard reboot typically, we need to do
             * a destroy, but we can't reply to this request yet so
             * we can't use do_destroy
             */
            fuse_log(FUSE_LOG_DEBUG, "%s: reinit\n", __func__);
            se->got_destroy = 1;
            se->got_init = 0;
            if (se->op.destroy) {
                se->op.destroy(se->userdata);
            }
        } else {
            goto reply_err;
        }
    }

    err = EACCES;
    /* Implement -o allow_root */
    if (se->deny_others && in->uid != se->owner && in->uid != 0 &&
        in->opcode != FUSE_INIT && in->opcode != FUSE_READ &&
        in->opcode != FUSE_WRITE && in->opcode != FUSE_FSYNC &&
        in->opcode != FUSE_RELEASE && in->opcode != FUSE_READDIR &&
        in->opcode != FUSE_FSYNCDIR && in->opcode != FUSE_RELEASEDIR &&
        in->opcode != FUSE_NOTIFY_REPLY && in->opcode != FUSE_READDIRPLUS) {
        goto reply_err;
    }

    err = ENOSYS;
    if (in->opcode >= FUSE_MAXOP || !fuse_ll_ops[in->opcode].func) {
        goto reply_err;
    }
    if (in->opcode != FUSE_INTERRUPT) {
        struct fuse_req *intr;
        pthread_mutex_lock(&se->lock);
        intr = check_interrupt(se, req);
        list_add_req(req, &se->list);
        pthread_mutex_unlock(&se->lock);
        if (intr) {
            fuse_reply_err(intr, EAGAIN);
        }
    }

    if (in->opcode == FUSE_WRITE && se->op.write_buf) {
        do_write_buf(req, in->nodeid, &iter, bufv);
    } else {
        fuse_ll_ops[in->opcode].func(req, in->nodeid, &iter);
    }

    pthread_rwlock_unlock(&se->init_rwlock);
    return;

reply_err:
    fuse_reply_err(req, err);
    pthread_rwlock_unlock(&se->init_rwlock);
}

#define LL_OPTION(n, o, v)                     \
    {                                          \
        n, offsetof(struct fuse_session, o), v \
    }

static const struct fuse_opt fuse_ll_opts[] = {
    LL_OPTION("debug", debug, 1),
    LL_OPTION("-d", debug, 1),
    LL_OPTION("--debug", debug, 1),
    LL_OPTION("allow_root", deny_others, 1),
    LL_OPTION("--socket-path=%s", vu_socket_path, 0),
    LL_OPTION("--socket-group=%s", vu_socket_group, 0),
    LL_OPTION("--fd=%d", vu_listen_fd, 0),
    LL_OPTION("--thread-pool-size=%d", thread_pool_size, 0),
    FUSE_OPT_END
};

void fuse_lowlevel_version(void)
{
    printf("using FUSE kernel interface version %i.%i\n", FUSE_KERNEL_VERSION,
           FUSE_KERNEL_MINOR_VERSION);
}

void fuse_lowlevel_help(void)
{
    /*
     * These are not all options, but the ones that are
     * potentially of interest to an end-user
     */
    printf(
        "    -o allow_root              allow access by root\n"
        "    --socket-path=PATH         path for the vhost-user socket\n"
        "    --socket-group=GRNAME      name of group for the vhost-user socket\n"
        "    --fd=FDNUM                 fd number of vhost-user socket\n"
        "    --thread-pool-size=NUM     thread pool size limit (default %d)\n",
        THREAD_POOL_SIZE);
}

void fuse_session_destroy(struct fuse_session *se)
{
    if (se->got_init && !se->got_destroy) {
        if (se->op.destroy) {
            se->op.destroy(se->userdata);
        }
    }
    pthread_rwlock_destroy(&se->init_rwlock);
    pthread_mutex_destroy(&se->lock);
    free(se->cuse_data);
    if (se->fd != -1) {
        close(se->fd);
    }

    if (fuse_lowlevel_is_virtio(se)) {
        virtio_session_close(se);
    }

    free(se->vu_socket_path);
    se->vu_socket_path = NULL;

    g_free(se);
}


struct fuse_session *fuse_session_new(struct fuse_args *args,
                                      const struct fuse_lowlevel_ops *op,
                                      size_t op_size, void *userdata)
{
    struct fuse_session *se;

    if (sizeof(struct fuse_lowlevel_ops) < op_size) {
        fuse_log(
            FUSE_LOG_ERR,
            "fuse: warning: library too old, some operations may not work\n");
        op_size = sizeof(struct fuse_lowlevel_ops);
    }

    if (args->argc == 0) {
        fuse_log(FUSE_LOG_ERR,
                 "fuse: empty argv passed to fuse_session_new().\n");
        return NULL;
    }

    se = g_try_new0(struct fuse_session, 1);
    if (se == NULL) {
        fuse_log(FUSE_LOG_ERR, "fuse: failed to allocate fuse object\n");
        goto out1;
    }
    se->fd = -1;
    se->vu_listen_fd = -1;
    se->thread_pool_size = THREAD_POOL_SIZE;
    se->conn.max_write = UINT_MAX;
    se->conn.max_readahead = UINT_MAX;

    /* Parse options */
    if (fuse_opt_parse(args, se, fuse_ll_opts, NULL) == -1) {
        goto out2;
    }
    if (args->argc == 1 && args->argv[0][0] == '-') {
        fuse_log(FUSE_LOG_ERR,
                 "fuse: warning: argv[0] looks like an option, but "
                 "will be ignored\n");
    } else if (args->argc != 1) {
        int i;
        fuse_log(FUSE_LOG_ERR, "fuse: unknown option(s): `");
        for (i = 1; i < args->argc - 1; i++) {
            fuse_log(FUSE_LOG_ERR, "%s ", args->argv[i]);
        }
        fuse_log(FUSE_LOG_ERR, "%s'\n", args->argv[i]);
        goto out4;
    }

    if (!se->vu_socket_path && se->vu_listen_fd < 0) {
        fuse_log(FUSE_LOG_ERR, "fuse: missing --socket-path or --fd option\n");
        goto out4;
    }
    if (se->vu_socket_path && se->vu_listen_fd >= 0) {
        fuse_log(FUSE_LOG_ERR,
                 "fuse: --socket-path and --fd cannot be given together\n");
        goto out4;
    }
    if (se->vu_socket_group && !se->vu_socket_path) {
        fuse_log(FUSE_LOG_ERR,
                 "fuse: --socket-group can only be used with --socket-path\n");
        goto out4;
    }

    se->bufsize = FUSE_MAX_MAX_PAGES * getpagesize() + FUSE_BUFFER_HEADER_SIZE;

    list_init_req(&se->list);
    list_init_req(&se->interrupts);
    fuse_mutex_init(&se->lock);
    pthread_rwlock_init(&se->init_rwlock, NULL);

    memcpy(&se->op, op, op_size);
    se->owner = getuid();
    se->userdata = userdata;

    return se;

out4:
    fuse_opt_free_args(args);
out2:
    g_free(se);
out1:
    return NULL;
}

int fuse_session_mount(struct fuse_session *se)
{
    return virtio_session_mount(se);
}

int fuse_session_fd(struct fuse_session *se)
{
    return se->fd;
}

void fuse_session_unmount(struct fuse_session *se)
{
}

int fuse_lowlevel_is_virtio(struct fuse_session *se)
{
    return !!se->virtio_dev;
}

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
