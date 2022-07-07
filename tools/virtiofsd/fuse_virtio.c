/*
 * virtio-fs glue for FUSE
 * Copyright (C) 2018 Red Hat, Inc. and/or its affiliates
 *
 * Authors:
 *   Dave Gilbert  <dgilbert@redhat.com>
 *
 * Implements the glue between libfuse and libvhost-user
 *
 * This program can be distributed under the terms of the GNU LGPLv2.
 * See the file COPYING.LIB
 */

#include "qemu/osdep.h"
#include "qemu/iov.h"
#include "qapi/error.h"
#include "fuse_i.h"
#include "standard-headers/linux/fuse.h"
#include "fuse_misc.h"
#include "fuse_opt.h"
#include "fuse_virtio.h"

#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <grp.h>

#include "libvhost-user.h"

struct fv_VuDev;
struct fv_QueueInfo {
    pthread_t thread;
    /*
     * This lock protects the VuVirtq preventing races between
     * fv_queue_thread() and fv_queue_worker().
     */
    pthread_mutex_t vq_lock;

    struct fv_VuDev *virtio_dev;

    /* Our queue index, corresponds to array position */
    int qidx;
    int kick_fd;
    int kill_fd; /* For killing the thread */
};

/* A FUSE request */
typedef struct {
    VuVirtqElement elem;
    struct fuse_chan ch;

    /* Used to complete requests that involve no reply */
    bool reply_sent;
} FVRequest;

/*
 * We pass the dev element into libvhost-user
 * and then use it to get back to the outer
 * container for other data.
 */
struct fv_VuDev {
    VuDev dev;
    struct fuse_session *se;

    /*
     * Either handle virtqueues or vhost-user protocol messages.  Don't do
     * both at the same time since that could lead to race conditions if
     * virtqueues or memory tables change while another thread is accessing
     * them.
     *
     * The assumptions are:
     * 1. fv_queue_thread() reads/writes to virtqueues and only reads VuDev.
     * 2. virtio_loop() reads/writes virtqueues and VuDev.
     */
    pthread_rwlock_t vu_dispatch_rwlock;

    /*
     * The following pair of fields are only accessed in the main
     * virtio_loop
     */
    size_t nqueues;
    struct fv_QueueInfo **qi;
};

/* Callback from libvhost-user */
static uint64_t fv_get_features(VuDev *dev)
{
    return 1ULL << VIRTIO_F_VERSION_1;
}

/* Callback from libvhost-user */
static void fv_set_features(VuDev *dev, uint64_t features)
{
}

/*
 * Callback from libvhost-user if there's a new fd we're supposed to listen
 * to, typically a queue kick?
 */
static void fv_set_watch(VuDev *dev, int fd, int condition, vu_watch_cb cb,
                         void *data)
{
    fuse_log(FUSE_LOG_WARNING, "%s: TODO! fd=%d\n", __func__, fd);
}

/*
 * Callback from libvhost-user if we're no longer supposed to listen on an fd
 */
static void fv_remove_watch(VuDev *dev, int fd)
{
    fuse_log(FUSE_LOG_WARNING, "%s: TODO! fd=%d\n", __func__, fd);
}

/* Callback from libvhost-user to panic */
static void fv_panic(VuDev *dev, const char *err)
{
    fuse_log(FUSE_LOG_ERR, "%s: libvhost-user: %s\n", __func__, err);
    /* TODO: Allow reconnects?? */
    exit(EXIT_FAILURE);
}

/*
 * Copy from an iovec into a fuse_buf (memory only)
 * Caller must ensure there is space
 */
static size_t copy_from_iov(struct fuse_buf *buf, size_t out_num,
                            const struct iovec *out_sg,
                            size_t max)
{
    void *dest = buf->mem;
    size_t copied = 0;

    while (out_num && max) {
        size_t onelen = out_sg->iov_len;
        onelen = MIN(onelen, max);
        memcpy(dest, out_sg->iov_base, onelen);
        dest += onelen;
        copied += onelen;
        out_sg++;
        out_num--;
        max -= onelen;
    }

    return copied;
}

/*
 * Skip 'skip' bytes in the iov; 'sg_1stindex' is set as
 * the index for the 1st iovec to read data from, and
 * 'sg_1stskip' is the number of bytes to skip in that entry.
 *
 * Returns True if there are at least 'skip' bytes in the iovec
 *
 */
static bool skip_iov(const struct iovec *sg, size_t sg_size,
                     size_t skip,
                     size_t *sg_1stindex, size_t *sg_1stskip)
{
    size_t vec;

    for (vec = 0; vec < sg_size; vec++) {
        if (sg[vec].iov_len > skip) {
            *sg_1stskip = skip;
            *sg_1stindex = vec;

            return true;
        }

        skip -= sg[vec].iov_len;
    }

    *sg_1stindex = vec;
    *sg_1stskip = 0;
    return skip == 0;
}

/*
 * Copy from one iov to another, the given number of bytes
 * The caller must have checked sizes.
 */
static void copy_iov(struct iovec *src_iov, int src_count,
                     struct iovec *dst_iov, int dst_count, size_t to_copy)
{
    size_t dst_offset = 0;
    /* Outer loop copies 'src' elements */
    while (to_copy) {
        assert(src_count);
        size_t src_len = src_iov[0].iov_len;
        size_t src_offset = 0;

        if (src_len > to_copy) {
            src_len = to_copy;
        }
        /* Inner loop copies contents of one 'src' to maybe multiple dst. */
        while (src_len) {
            assert(dst_count);
            size_t dst_len = dst_iov[0].iov_len - dst_offset;
            if (dst_len > src_len) {
                dst_len = src_len;
            }

            memcpy(dst_iov[0].iov_base + dst_offset,
                   src_iov[0].iov_base + src_offset, dst_len);
            src_len -= dst_len;
            to_copy -= dst_len;
            src_offset += dst_len;
            dst_offset += dst_len;

            assert(dst_offset <= dst_iov[0].iov_len);
            if (dst_offset == dst_iov[0].iov_len) {
                dst_offset = 0;
                dst_iov++;
                dst_count--;
            }
        }
        src_iov++;
        src_count--;
    }
}

/*
 * pthread_rwlock_rdlock() and pthread_rwlock_wrlock can fail if
 * a deadlock condition is detected or the current thread already
 * owns the lock. They can also fail, like pthread_rwlock_unlock(),
 * if the mutex wasn't properly initialized. None of these are ever
 * expected to happen.
 */
static void vu_dispatch_rdlock(struct fv_VuDev *vud)
{
    int ret = pthread_rwlock_rdlock(&vud->vu_dispatch_rwlock);
    assert(ret == 0);
}

static void vu_dispatch_wrlock(struct fv_VuDev *vud)
{
    int ret = pthread_rwlock_wrlock(&vud->vu_dispatch_rwlock);
    assert(ret == 0);
}

static void vu_dispatch_unlock(struct fv_VuDev *vud)
{
    int ret = pthread_rwlock_unlock(&vud->vu_dispatch_rwlock);
    assert(ret == 0);
}

static void vq_send_element(struct fv_QueueInfo *qi, VuVirtqElement *elem,
                            ssize_t len)
{
    struct fuse_session *se = qi->virtio_dev->se;
    VuDev *dev = &se->virtio_dev->dev;
    VuVirtq *q = vu_get_queue(dev, qi->qidx);

    vu_dispatch_rdlock(qi->virtio_dev);
    pthread_mutex_lock(&qi->vq_lock);
    vu_queue_push(dev, q, elem, len);
    vu_queue_notify(dev, q);
    pthread_mutex_unlock(&qi->vq_lock);
    vu_dispatch_unlock(qi->virtio_dev);
}

/*
 * Called back by ll whenever it wants to send a reply/message back
 * The 1st element of the iov starts with the fuse_out_header
 * 'unique'==0 means it's a notify message.
 */
int virtio_send_msg(struct fuse_session *se, struct fuse_chan *ch,
                    struct iovec *iov, int count)
{
    FVRequest *req = container_of(ch, FVRequest, ch);
    struct fv_QueueInfo *qi = ch->qi;
    VuVirtqElement *elem = &req->elem;
    int ret = 0;

    assert(count >= 1);
    assert(iov[0].iov_len >= sizeof(struct fuse_out_header));

    struct fuse_out_header *out = iov[0].iov_base;
    /* TODO: Endianness! */

    size_t tosend_len = iov_size(iov, count);

    /* unique == 0 is notification, which we don't support */
    assert(out->unique);
    assert(!req->reply_sent);

    /* The 'in' part of the elem is to qemu */
    unsigned int in_num = elem->in_num;
    struct iovec *in_sg = elem->in_sg;
    size_t in_len = iov_size(in_sg, in_num);
    fuse_log(FUSE_LOG_DEBUG, "%s: elem %d: with %d in desc of length %zd\n",
             __func__, elem->index, in_num, in_len);

    /*
     * The elem should have room for a 'fuse_out_header' (out from fuse)
     * plus the data based on the len in the header.
     */
    if (in_len < sizeof(struct fuse_out_header)) {
        fuse_log(FUSE_LOG_ERR, "%s: elem %d too short for out_header\n",
                 __func__, elem->index);
        ret = -E2BIG;
        goto err;
    }
    if (in_len < tosend_len) {
        fuse_log(FUSE_LOG_ERR, "%s: elem %d too small for data len %zd\n",
                 __func__, elem->index, tosend_len);
        ret = -E2BIG;
        goto err;
    }

    copy_iov(iov, count, in_sg, in_num, tosend_len);

    vq_send_element(qi, elem, tosend_len);
    req->reply_sent = true;

err:
    return ret;
}

/*
 * Callback from fuse_send_data_iov_* when it's virtio and the buffer
 * is a single FD with FUSE_BUF_IS_FD | FUSE_BUF_FD_SEEK
 * We need send the iov and then the buffer.
 * Return 0 on success
 */
int virtio_send_data_iov(struct fuse_session *se, struct fuse_chan *ch,
                         struct iovec *iov, int count, struct fuse_bufvec *buf,
                         size_t len)
{
    FVRequest *req = container_of(ch, FVRequest, ch);
    struct fv_QueueInfo *qi = ch->qi;
    VuVirtqElement *elem = &req->elem;
    int ret = 0;
    g_autofree struct iovec *in_sg_cpy = NULL;

    assert(count >= 1);
    assert(iov[0].iov_len >= sizeof(struct fuse_out_header));

    struct fuse_out_header *out = iov[0].iov_base;
    /* TODO: Endianness! */

    size_t iov_len = iov_size(iov, count);
    size_t tosend_len = iov_len + len;

    out->len = tosend_len;

    fuse_log(FUSE_LOG_DEBUG, "%s: count=%d len=%zd iov_len=%zd\n", __func__,
             count, len, iov_len);

    /* unique == 0 is notification which we don't support */
    assert(out->unique);

    assert(!req->reply_sent);

    /* The 'in' part of the elem is to qemu */
    unsigned int in_num = elem->in_num;
    struct iovec *in_sg = elem->in_sg;
    size_t in_len = iov_size(in_sg, in_num);
    fuse_log(FUSE_LOG_DEBUG, "%s: elem %d: with %d in desc of length %zd\n",
             __func__, elem->index, in_num, in_len);

    /*
     * The elem should have room for a 'fuse_out_header' (out from fuse)
     * plus the data based on the len in the header.
     */
    if (in_len < sizeof(struct fuse_out_header)) {
        fuse_log(FUSE_LOG_ERR, "%s: elem %d too short for out_header\n",
                 __func__, elem->index);
        return E2BIG;
    }
    if (in_len < tosend_len) {
        fuse_log(FUSE_LOG_ERR, "%s: elem %d too small for data len %zd\n",
                 __func__, elem->index, tosend_len);
        return E2BIG;
    }

    /* TODO: Limit to 'len' */

    /* First copy the header data from iov->in_sg */
    copy_iov(iov, count, in_sg, in_num, iov_len);

    /*
     * Build a copy of the in_sg iov so we can skip bits in it,
     * including changing the offsets
     */
    in_sg_cpy = g_new(struct iovec, in_num);
    memcpy(in_sg_cpy, in_sg, sizeof(struct iovec) * in_num);
    /* These get updated as we skip */
    struct iovec *in_sg_ptr = in_sg_cpy;
    unsigned int in_sg_cpy_count = in_num;

    /* skip over parts of in_sg that contained the header iov */
    iov_discard_front(&in_sg_ptr, &in_sg_cpy_count, iov_len);

    do {
        fuse_log(FUSE_LOG_DEBUG, "%s: in_sg_cpy_count=%d len remaining=%zd\n",
                 __func__, in_sg_cpy_count, len);

        ret = preadv(buf->buf[0].fd, in_sg_ptr, in_sg_cpy_count,
                     buf->buf[0].pos);

        if (ret == -1) {
            ret = errno;
            if (ret == EINTR) {
                continue;
            }
            fuse_log(FUSE_LOG_DEBUG, "%s: preadv failed (%m) len=%zd\n",
                     __func__, len);
            return ret;
        }

        if (!ret) {
            /* EOF case? */
            fuse_log(FUSE_LOG_DEBUG, "%s: !ret len remaining=%zd\n", __func__,
                     len);
            break;
        }
        fuse_log(FUSE_LOG_DEBUG, "%s: preadv ret=%d len=%zd\n", __func__,
                 ret, len);

        len -= ret;
        /* Short read. Retry reading remaining bytes */
        if (len) {
            fuse_log(FUSE_LOG_DEBUG, "%s: ret < len\n", __func__);
            /* Skip over this much next time around */
            iov_discard_front(&in_sg_ptr, &in_sg_cpy_count, ret);
            buf->buf[0].pos += ret;
        }
    } while (len);

    /* Need to fix out->len on EOF */
    if (len) {
        struct fuse_out_header *out_sg = in_sg[0].iov_base;

        tosend_len -= len;
        out_sg->len = tosend_len;
    }

    vq_send_element(qi, elem, tosend_len);
    req->reply_sent = true;
    return 0;
}

static __thread bool clone_fs_called;

/* Process one FVRequest in a thread pool */
static void fv_queue_worker(gpointer data, gpointer user_data)
{
    struct fv_QueueInfo *qi = user_data;
    struct fuse_session *se = qi->virtio_dev->se;
    FVRequest *req = data;
    VuVirtqElement *elem = &req->elem;
    struct fuse_buf fbuf = {};
    bool allocated_bufv = false;
    struct fuse_bufvec bufv;
    struct fuse_bufvec *pbufv;
    struct fuse_in_header inh;

    assert(se->bufsize > sizeof(struct fuse_in_header));

    if (!clone_fs_called) {
        int ret;

        /* unshare FS for xattr operation */
        ret = unshare(CLONE_FS);
        /* should not fail */
        assert(ret == 0);

        clone_fs_called = true;
    }

    /*
     * An element contains one request and the space to send our response
     * They're spread over multiple descriptors in a scatter/gather set
     * and we can't trust the guest to keep them still; so copy in/out.
     */
    fbuf.mem = g_malloc(se->bufsize);

    fuse_mutex_init(&req->ch.lock);
    req->ch.fd = -1;
    req->ch.qi = qi;

    /* The 'out' part of the elem is from qemu */
    unsigned int out_num = elem->out_num;
    struct iovec *out_sg = elem->out_sg;
    size_t out_len = iov_size(out_sg, out_num);
    fuse_log(FUSE_LOG_DEBUG,
             "%s: elem %d: with %d out desc of length %zd\n",
             __func__, elem->index, out_num, out_len);

    /*
     * The elem should contain a 'fuse_in_header' (in to fuse)
     * plus the data based on the len in the header.
     */
    if (out_len < sizeof(struct fuse_in_header)) {
        fuse_log(FUSE_LOG_ERR, "%s: elem %d too short for in_header\n",
                 __func__, elem->index);
        assert(0); /* TODO */
    }
    if (out_len > se->bufsize) {
        fuse_log(FUSE_LOG_ERR, "%s: elem %d too large for buffer\n", __func__,
                 elem->index);
        assert(0); /* TODO */
    }
    /* Copy just the fuse_in_header and look at it */
    copy_from_iov(&fbuf, out_num, out_sg,
                  sizeof(struct fuse_in_header));
    memcpy(&inh, fbuf.mem, sizeof(struct fuse_in_header));

    pbufv = NULL; /* Compiler thinks an unitialised path */
    if (inh.opcode == FUSE_WRITE &&
        out_len >= (sizeof(struct fuse_in_header) +
                    sizeof(struct fuse_write_in))) {
        /*
         * For a write we don't actually need to copy the
         * data, we can just do it straight out of guest memory
         * but we must still copy the headers in case the guest
         * was nasty and changed them while we were using them.
         */
        fuse_log(FUSE_LOG_DEBUG, "%s: Write special case\n", __func__);

        fbuf.size = copy_from_iov(&fbuf, out_num, out_sg,
                                  sizeof(struct fuse_in_header) +
                                  sizeof(struct fuse_write_in));
        /* That copy reread the in_header, make sure we use the original */
        memcpy(fbuf.mem, &inh, sizeof(struct fuse_in_header));

        /* Allocate the bufv, with space for the rest of the iov */
        pbufv = g_try_malloc(sizeof(struct fuse_bufvec) +
                             sizeof(struct fuse_buf) * out_num);
        if (!pbufv) {
            fuse_log(FUSE_LOG_ERR, "%s: pbufv malloc failed\n",
                    __func__);
            goto out;
        }

        allocated_bufv = true;
        pbufv->count = 1;
        pbufv->buf[0] = fbuf;

        size_t iovindex, pbufvindex, iov_bytes_skip;
        pbufvindex = 1; /* 2 headers, 1 fusebuf */

        if (!skip_iov(out_sg, out_num,
                      sizeof(struct fuse_in_header) +
                      sizeof(struct fuse_write_in),
                      &iovindex, &iov_bytes_skip)) {
            fuse_log(FUSE_LOG_ERR, "%s: skip failed\n",
                    __func__);
            goto out;
        }

        for (; iovindex < out_num; iovindex++, pbufvindex++) {
            pbufv->count++;
            pbufv->buf[pbufvindex].pos = ~0; /* Dummy */
            pbufv->buf[pbufvindex].flags = 0;
            pbufv->buf[pbufvindex].mem = out_sg[iovindex].iov_base;
            pbufv->buf[pbufvindex].size = out_sg[iovindex].iov_len;

            if (iov_bytes_skip) {
                pbufv->buf[pbufvindex].mem += iov_bytes_skip;
                pbufv->buf[pbufvindex].size -= iov_bytes_skip;
                iov_bytes_skip = 0;
            }
        }
    } else {
        /* Normal (non fast write) path */

        copy_from_iov(&fbuf, out_num, out_sg, se->bufsize);
        /* That copy reread the in_header, make sure we use the original */
        memcpy(fbuf.mem, &inh, sizeof(struct fuse_in_header));
        fbuf.size = out_len;

        /* TODO! Endianness of header */

        /* TODO: Add checks for fuse_session_exited */
        bufv.buf[0] = fbuf;
        bufv.count = 1;
        pbufv = &bufv;
    }
    pbufv->idx = 0;
    pbufv->off = 0;
    fuse_session_process_buf_int(se, pbufv, &req->ch);

out:
    if (allocated_bufv) {
        g_free(pbufv);
    }

    /* If the request has no reply, still recycle the virtqueue element */
    if (!req->reply_sent) {
        fuse_log(FUSE_LOG_DEBUG, "%s: elem %d no reply sent\n", __func__,
                 elem->index);
        vq_send_element(qi, elem, 0);
    }

    pthread_mutex_destroy(&req->ch.lock);
    g_free(fbuf.mem);
    free(req);
}

/* Thread function for individual queues, created when a queue is 'started' */
static void *fv_queue_thread(void *opaque)
{
    struct fv_QueueInfo *qi = opaque;
    struct VuDev *dev = &qi->virtio_dev->dev;
    struct VuVirtq *q = vu_get_queue(dev, qi->qidx);
    struct fuse_session *se = qi->virtio_dev->se;
    GThreadPool *pool = NULL;
    GList *req_list = NULL;

    if (se->thread_pool_size) {
        fuse_log(FUSE_LOG_DEBUG, "%s: Creating thread pool for Queue %d\n",
                 __func__, qi->qidx);
        pool = g_thread_pool_new(fv_queue_worker, qi, se->thread_pool_size,
                                 FALSE, NULL);
        if (!pool) {
            fuse_log(FUSE_LOG_ERR, "%s: g_thread_pool_new failed\n", __func__);
            return NULL;
        }
    }

    fuse_log(FUSE_LOG_INFO, "%s: Start for queue %d kick_fd %d\n", __func__,
             qi->qidx, qi->kick_fd);
    while (1) {
        struct pollfd pf[2];

        pf[0].fd = qi->kick_fd;
        pf[0].events = POLLIN;
        pf[0].revents = 0;
        pf[1].fd = qi->kill_fd;
        pf[1].events = POLLIN;
        pf[1].revents = 0;

        fuse_log(FUSE_LOG_DEBUG, "%s: Waiting for Queue %d event\n", __func__,
                 qi->qidx);
        int poll_res = ppoll(pf, 2, NULL, NULL);

        if (poll_res == -1) {
            if (errno == EINTR) {
                fuse_log(FUSE_LOG_INFO, "%s: ppoll interrupted, going around\n",
                         __func__);
                continue;
            }
            fuse_log(FUSE_LOG_ERR, "fv_queue_thread ppoll: %m\n");
            break;
        }
        assert(poll_res >= 1);
        if (pf[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            fuse_log(FUSE_LOG_ERR, "%s: Unexpected poll revents %x Queue %d\n",
                     __func__, pf[0].revents, qi->qidx);
            break;
        }
        if (pf[1].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            fuse_log(FUSE_LOG_ERR,
                     "%s: Unexpected poll revents %x Queue %d killfd\n",
                     __func__, pf[1].revents, qi->qidx);
            break;
        }
        if (pf[1].revents) {
            fuse_log(FUSE_LOG_INFO, "%s: kill event on queue %d - quitting\n",
                     __func__, qi->qidx);
            break;
        }
        assert(pf[0].revents & POLLIN);
        fuse_log(FUSE_LOG_DEBUG, "%s: Got queue event on Queue %d\n", __func__,
                 qi->qidx);

        eventfd_t evalue;
        if (eventfd_read(qi->kick_fd, &evalue)) {
            fuse_log(FUSE_LOG_ERR, "Eventfd_read for queue: %m\n");
            break;
        }
        /* Mutual exclusion with virtio_loop() */
        vu_dispatch_rdlock(qi->virtio_dev);
        pthread_mutex_lock(&qi->vq_lock);
        /* out is from guest, in is too guest */
        unsigned int in_bytes, out_bytes;
        vu_queue_get_avail_bytes(dev, q, &in_bytes, &out_bytes, ~0, ~0);

        fuse_log(FUSE_LOG_DEBUG,
                 "%s: Queue %d gave evalue: %zx available: in: %u out: %u\n",
                 __func__, qi->qidx, (size_t)evalue, in_bytes, out_bytes);

        while (1) {
            FVRequest *req = vu_queue_pop(dev, q, sizeof(FVRequest));
            if (!req) {
                break;
            }

            req->reply_sent = false;

            if (!se->thread_pool_size) {
                req_list = g_list_prepend(req_list, req);
            } else {
                g_thread_pool_push(pool, req, NULL);
            }
        }

        pthread_mutex_unlock(&qi->vq_lock);
        vu_dispatch_unlock(qi->virtio_dev);

        /* Process all the requests. */
        if (!se->thread_pool_size && req_list != NULL) {
            req_list = g_list_reverse(req_list);
            g_list_foreach(req_list, fv_queue_worker, qi);
            g_list_free(req_list);
            req_list = NULL;
        }
    }

    if (pool) {
        g_thread_pool_free(pool, FALSE, TRUE);
    }

    return NULL;
}

static void fv_queue_cleanup_thread(struct fv_VuDev *vud, int qidx)
{
    int ret;
    struct fv_QueueInfo *ourqi;

    assert(qidx < vud->nqueues);
    ourqi = vud->qi[qidx];

    /* Kill the thread */
    if (eventfd_write(ourqi->kill_fd, 1)) {
        fuse_log(FUSE_LOG_ERR, "Eventfd_write for queue %d: %s\n",
                 qidx, strerror(errno));
    }
    ret = pthread_join(ourqi->thread, NULL);
    if (ret) {
        fuse_log(FUSE_LOG_ERR, "%s: Failed to join thread idx %d err %d\n",
                 __func__, qidx, ret);
    }
    pthread_mutex_destroy(&ourqi->vq_lock);
    close(ourqi->kill_fd);
    ourqi->kick_fd = -1;
    g_free(vud->qi[qidx]);
    vud->qi[qidx] = NULL;
}

static void stop_all_queues(struct fv_VuDev *vud)
{
    for (int i = 0; i < vud->nqueues; i++) {
        if (!vud->qi[i]) {
            continue;
        }

        fuse_log(FUSE_LOG_INFO, "%s: Stopping queue %d thread\n", __func__, i);
        fv_queue_cleanup_thread(vud, i);
    }
}

/* Callback from libvhost-user on start or stop of a queue */
static void fv_queue_set_started(VuDev *dev, int qidx, bool started)
{
    struct fv_VuDev *vud = container_of(dev, struct fv_VuDev, dev);
    struct fv_QueueInfo *ourqi;

    fuse_log(FUSE_LOG_INFO, "%s: qidx=%d started=%d\n", __func__, qidx,
             started);
    assert(qidx >= 0);

    /*
     * Ignore additional request queues for now.  passthrough_ll.c must be
     * audited for thread-safety issues first.  It was written with a
     * well-behaved client in mind and may not protect against all types of
     * races yet.
     */
    if (qidx > 1) {
        fuse_log(FUSE_LOG_ERR,
                 "%s: multiple request queues not yet implemented, please only "
                 "configure 1 request queue\n",
                 __func__);
        exit(EXIT_FAILURE);
    }

    if (started) {
        /* Fire up a thread to watch this queue */
        if (qidx >= vud->nqueues) {
            vud->qi = g_realloc_n(vud->qi, qidx + 1, sizeof(vud->qi[0]));
            memset(vud->qi + vud->nqueues, 0,
                   sizeof(vud->qi[0]) * (1 + (qidx - vud->nqueues)));
            vud->nqueues = qidx + 1;
        }
        if (!vud->qi[qidx]) {
            vud->qi[qidx] = g_new0(struct fv_QueueInfo, 1);
            vud->qi[qidx]->virtio_dev = vud;
            vud->qi[qidx]->qidx = qidx;
        } else {
            /* Shouldn't have been started */
            assert(vud->qi[qidx]->kick_fd == -1);
        }
        ourqi = vud->qi[qidx];
        ourqi->kick_fd = dev->vq[qidx].kick_fd;

        ourqi->kill_fd = eventfd(0, EFD_CLOEXEC | EFD_SEMAPHORE);
        assert(ourqi->kill_fd != -1);
        pthread_mutex_init(&ourqi->vq_lock, NULL);

        if (pthread_create(&ourqi->thread, NULL, fv_queue_thread, ourqi)) {
            fuse_log(FUSE_LOG_ERR, "%s: Failed to create thread for queue %d\n",
                     __func__, qidx);
            assert(0);
        }
    } else {
        /*
         * Temporarily drop write-lock taken in virtio_loop() so that
         * the queue thread doesn't block in virtio_send_msg().
         */
        vu_dispatch_unlock(vud);
        fv_queue_cleanup_thread(vud, qidx);
        vu_dispatch_wrlock(vud);
    }
}

static bool fv_queue_order(VuDev *dev, int qidx)
{
    return false;
}

static const VuDevIface fv_iface = {
    .get_features = fv_get_features,
    .set_features = fv_set_features,

    /* Don't need process message, we've not got any at vhost-user level */
    .queue_set_started = fv_queue_set_started,

    .queue_is_processed_in_order = fv_queue_order,
};

/*
 * Main loop; this mostly deals with events on the vhost-user
 * socket itself, and not actual fuse data.
 */
int virtio_loop(struct fuse_session *se)
{
    fuse_log(FUSE_LOG_INFO, "%s: Entry\n", __func__);

    while (!fuse_session_exited(se)) {
        struct pollfd pf[1];
        bool ok;
        pf[0].fd = se->vu_socketfd;
        pf[0].events = POLLIN;
        pf[0].revents = 0;

        fuse_log(FUSE_LOG_DEBUG, "%s: Waiting for VU event\n", __func__);
        int poll_res = ppoll(pf, 1, NULL, NULL);

        if (poll_res == -1) {
            if (errno == EINTR) {
                fuse_log(FUSE_LOG_INFO, "%s: ppoll interrupted, going around\n",
                         __func__);
                continue;
            }
            fuse_log(FUSE_LOG_ERR, "virtio_loop ppoll: %m\n");
            break;
        }
        assert(poll_res == 1);
        if (pf[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            fuse_log(FUSE_LOG_ERR, "%s: Unexpected poll revents %x\n", __func__,
                     pf[0].revents);
            break;
        }
        assert(pf[0].revents & POLLIN);
        fuse_log(FUSE_LOG_DEBUG, "%s: Got VU event\n", __func__);
        /* Mutual exclusion with fv_queue_thread() */
        vu_dispatch_wrlock(se->virtio_dev);

        ok = vu_dispatch(&se->virtio_dev->dev);

        vu_dispatch_unlock(se->virtio_dev);

        if (!ok) {
            fuse_log(FUSE_LOG_ERR, "%s: vu_dispatch failed\n", __func__);
            break;
        }
    }

    /*
     * Make sure all fv_queue_thread()s quit on exit, as we're about to
     * free virtio dev and fuse session, no one should access them anymore.
     */
    stop_all_queues(se->virtio_dev);
    fuse_log(FUSE_LOG_INFO, "%s: Exit\n", __func__);

    return 0;
}

static void strreplace(char *s, char old, char new)
{
    for (; *s; ++s) {
        if (*s == old) {
            *s = new;
        }
    }
}

static bool fv_socket_lock(struct fuse_session *se)
{
    g_autofree gchar *sk_name = NULL;
    g_autofree gchar *pidfile = NULL;
    g_autofree gchar *state = NULL;
    g_autofree gchar *dir = NULL;
    Error *local_err = NULL;

    state = qemu_get_local_state_dir();
    dir = g_build_filename(state, "run", "virtiofsd", NULL);

    if (g_mkdir_with_parents(dir, S_IRWXU) < 0) {
        fuse_log(FUSE_LOG_ERR, "%s: Failed to create directory %s: %s\n",
                 __func__, dir, strerror(errno));
        return false;
    }

    sk_name = g_strdup(se->vu_socket_path);
    strreplace(sk_name, '/', '.');
    pidfile = g_strdup_printf("%s/%s.pid", dir, sk_name);

    if (!qemu_write_pidfile(pidfile, &local_err)) {
        error_report_err(local_err);
        return false;
    }

    return true;
}

static int fv_create_listen_socket(struct fuse_session *se)
{
    struct sockaddr_un un;
    mode_t old_umask;

    /* Nothing to do if fd is already initialized */
    if (se->vu_listen_fd >= 0) {
        return 0;
    }

    if (strlen(se->vu_socket_path) >= sizeof(un.sun_path)) {
        fuse_log(FUSE_LOG_ERR, "Socket path too long\n");
        return -1;
    }

    if (!strlen(se->vu_socket_path)) {
        fuse_log(FUSE_LOG_ERR, "Socket path is empty\n");
        return -1;
    }

    /* Check the vu_socket_path is already used */
    if (!fv_socket_lock(se)) {
        return -1;
    }

    /*
     * Create the Unix socket to communicate with qemu
     * based on QEMU's vhost-user-bridge
     */
    unlink(se->vu_socket_path);
    strcpy(un.sun_path, se->vu_socket_path);
    size_t addr_len = sizeof(un);

    int listen_sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_sock == -1) {
        fuse_log(FUSE_LOG_ERR, "vhost socket creation: %m\n");
        return -1;
    }
    un.sun_family = AF_UNIX;

    /*
     * Unfortunately bind doesn't let you set the mask on the socket,
     * so set umask appropriately and restore it later.
     */
    if (se->vu_socket_group) {
        old_umask = umask(S_IROTH | S_IWOTH | S_IXOTH);
    } else {
        old_umask = umask(S_IRGRP | S_IWGRP | S_IXGRP |
                          S_IROTH | S_IWOTH | S_IXOTH);
    }
    if (bind(listen_sock, (struct sockaddr *)&un, addr_len) == -1) {
        fuse_log(FUSE_LOG_ERR, "vhost socket bind: %m\n");
        close(listen_sock);
        umask(old_umask);
        return -1;
    }
    if (se->vu_socket_group) {
        struct group *g = getgrnam(se->vu_socket_group);
        if (g) {
            if (chown(se->vu_socket_path, -1, g->gr_gid) == -1) {
                fuse_log(FUSE_LOG_WARNING,
                         "vhost socket failed to set group to %s (%d): %m\n",
                         se->vu_socket_group, g->gr_gid);
            }
        } else {
            fuse_log(FUSE_LOG_ERR,
                     "vhost socket: unable to find group '%s'\n",
                     se->vu_socket_group);
            close(listen_sock);
            umask(old_umask);
            return -1;
        }
    }
    umask(old_umask);

    if (listen(listen_sock, 1) == -1) {
        fuse_log(FUSE_LOG_ERR, "vhost socket listen: %m\n");
        close(listen_sock);
        return -1;
    }

    se->vu_listen_fd = listen_sock;
    return 0;
}

int virtio_session_mount(struct fuse_session *se)
{
    int ret;

    /*
     * Test that unshare(CLONE_FS) works. fv_queue_worker() will need it. It's
     * an unprivileged system call but some Docker/Moby versions are known to
     * reject it via seccomp when CAP_SYS_ADMIN is not given.
     *
     * Note that the program is single-threaded here so this syscall has no
     * visible effect and is safe to make.
     */
    ret = unshare(CLONE_FS);
    if (ret == -1 && errno == EPERM) {
        fuse_log(FUSE_LOG_ERR, "unshare(CLONE_FS) failed with EPERM. If "
                "running in a container please check that the container "
                "runtime seccomp policy allows unshare.\n");
        return -1;
    }

    ret = fv_create_listen_socket(se);
    if (ret < 0) {
        return ret;
    }

    se->fd = -1;

    fuse_log(FUSE_LOG_INFO, "%s: Waiting for vhost-user socket connection...\n",
             __func__);
    int data_sock = accept(se->vu_listen_fd, NULL, NULL);
    if (data_sock == -1) {
        fuse_log(FUSE_LOG_ERR, "vhost socket accept: %m\n");
        close(se->vu_listen_fd);
        return -1;
    }
    close(se->vu_listen_fd);
    se->vu_listen_fd = -1;
    fuse_log(FUSE_LOG_INFO, "%s: Received vhost-user socket connection\n",
             __func__);

    /* TODO: Some cleanup/deallocation! */
    se->virtio_dev = g_new0(struct fv_VuDev, 1);

    se->vu_socketfd = data_sock;
    se->virtio_dev->se = se;
    pthread_rwlock_init(&se->virtio_dev->vu_dispatch_rwlock, NULL);
    if (!vu_init(&se->virtio_dev->dev, 2, se->vu_socketfd, fv_panic, NULL,
                 fv_set_watch, fv_remove_watch, &fv_iface)) {
        fuse_log(FUSE_LOG_ERR, "%s: vu_init failed\n", __func__);
        return -1;
    }

    return 0;
}

void virtio_session_close(struct fuse_session *se)
{
    close(se->vu_socketfd);

    if (!se->virtio_dev) {
        return;
    }

    g_free(se->virtio_dev->qi);
    pthread_rwlock_destroy(&se->virtio_dev->vu_dispatch_rwlock);
    g_free(se->virtio_dev);
    se->virtio_dev = NULL;
}
