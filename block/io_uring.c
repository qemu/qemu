/*
 * Linux io_uring support.
 *
 * Copyright (C) 2009 IBM, Corp.
 * Copyright (C) 2009 Red Hat, Inc.
 * Copyright (C) 2019 Aarushi Mehta
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include <liburing.h>
#include "qemu-common.h"
#include "block/aio.h"
#include "qemu/queue.h"
#include "block/block.h"
#include "block/raw-aio.h"
#include "qemu/coroutine.h"
#include "qapi/error.h"
#include "trace.h"

/* io_uring ring size */
#define MAX_ENTRIES 128

typedef struct LuringAIOCB {
    Coroutine *co;
    struct io_uring_sqe sqeq;
    ssize_t ret;
    QEMUIOVector *qiov;
    bool is_read;
    QSIMPLEQ_ENTRY(LuringAIOCB) next;

    /*
     * Buffered reads may require resubmission, see
     * luring_resubmit_short_read().
     */
    int total_read;
    QEMUIOVector resubmit_qiov;
} LuringAIOCB;

typedef struct LuringQueue {
    int plugged;
    unsigned int in_queue;
    unsigned int in_flight;
    bool blocked;
    QSIMPLEQ_HEAD(, LuringAIOCB) submit_queue;
} LuringQueue;

typedef struct LuringState {
    AioContext *aio_context;

    struct io_uring ring;

    /* io queue for submit at batch.  Protected by AioContext lock. */
    LuringQueue io_q;

    /* I/O completion processing.  Only runs in I/O thread.  */
    QEMUBH *completion_bh;
} LuringState;

/**
 * luring_resubmit:
 *
 * Resubmit a request by appending it to submit_queue.  The caller must ensure
 * that ioq_submit() is called later so that submit_queue requests are started.
 */
static void luring_resubmit(LuringState *s, LuringAIOCB *luringcb)
{
    QSIMPLEQ_INSERT_TAIL(&s->io_q.submit_queue, luringcb, next);
    s->io_q.in_queue++;
}

/**
 * luring_resubmit_short_read:
 *
 * Before Linux commit 9d93a3f5a0c ("io_uring: punt short reads to async
 * context") a buffered I/O request with the start of the file range in the
 * page cache could result in a short read.  Applications need to resubmit the
 * remaining read request.
 *
 * This is a slow path but recent kernels never take it.
 */
static void luring_resubmit_short_read(LuringState *s, LuringAIOCB *luringcb,
                                       int nread)
{
    QEMUIOVector *resubmit_qiov;
    size_t remaining;

    trace_luring_resubmit_short_read(s, luringcb, nread);

    /* Update read position */
    luringcb->total_read = nread;
    remaining = luringcb->qiov->size - luringcb->total_read;

    /* Shorten qiov */
    resubmit_qiov = &luringcb->resubmit_qiov;
    if (resubmit_qiov->iov == NULL) {
        qemu_iovec_init(resubmit_qiov, luringcb->qiov->niov);
    } else {
        qemu_iovec_reset(resubmit_qiov);
    }
    qemu_iovec_concat(resubmit_qiov, luringcb->qiov, luringcb->total_read,
                      remaining);

    /* Update sqe */
    luringcb->sqeq.off = nread;
    luringcb->sqeq.addr = (__u64)(uintptr_t)luringcb->resubmit_qiov.iov;
    luringcb->sqeq.len = luringcb->resubmit_qiov.niov;

    luring_resubmit(s, luringcb);
}

/**
 * luring_process_completions:
 * @s: AIO state
 *
 * Fetches completed I/O requests, consumes cqes and invokes their callbacks
 * The function is somewhat tricky because it supports nested event loops, for
 * example when a request callback invokes aio_poll().
 *
 * Function schedules BH completion so it  can be called again in a nested
 * event loop.  When there are no events left  to complete the BH is being
 * canceled.
 *
 */
static void luring_process_completions(LuringState *s)
{
    struct io_uring_cqe *cqes;
    int total_bytes;
    /*
     * Request completion callbacks can run the nested event loop.
     * Schedule ourselves so the nested event loop will "see" remaining
     * completed requests and process them.  Without this, completion
     * callbacks that wait for other requests using a nested event loop
     * would hang forever.
     *
     * This workaround is needed because io_uring uses poll_wait, which
     * is woken up when new events are added to the uring, thus polling on
     * the same uring fd will block unless more events are received.
     *
     * Other leaf block drivers (drivers that access the data themselves)
     * are networking based, so they poll sockets for data and run the
     * correct coroutine.
     */
    qemu_bh_schedule(s->completion_bh);

    while (io_uring_peek_cqe(&s->ring, &cqes) == 0) {
        LuringAIOCB *luringcb;
        int ret;

        if (!cqes) {
            break;
        }

        luringcb = io_uring_cqe_get_data(cqes);
        ret = cqes->res;
        io_uring_cqe_seen(&s->ring, cqes);
        cqes = NULL;

        /* Change counters one-by-one because we can be nested. */
        s->io_q.in_flight--;
        trace_luring_process_completion(s, luringcb, ret);

        /* total_read is non-zero only for resubmitted read requests */
        total_bytes = ret + luringcb->total_read;

        if (ret < 0) {
            /*
             * Only writev/readv/fsync requests on regular files or host block
             * devices are submitted. Therefore -EAGAIN is not expected but it's
             * known to happen sometimes with Linux SCSI. Submit again and hope
             * the request completes successfully.
             *
             * For more information, see:
             * https://lore.kernel.org/io-uring/20210727165811.284510-3-axboe@kernel.dk/T/#u
             *
             * If the code is changed to submit other types of requests in the
             * future, then this workaround may need to be extended to deal with
             * genuine -EAGAIN results that should not be resubmitted
             * immediately.
             */
            if (ret == -EINTR || ret == -EAGAIN) {
                luring_resubmit(s, luringcb);
                continue;
            }
        } else if (!luringcb->qiov) {
            goto end;
        } else if (total_bytes == luringcb->qiov->size) {
            ret = 0;
        /* Only read/write */
        } else {
            /* Short Read/Write */
            if (luringcb->is_read) {
                if (ret > 0) {
                    luring_resubmit_short_read(s, luringcb, ret);
                    continue;
                } else {
                    /* Pad with zeroes */
                    qemu_iovec_memset(luringcb->qiov, total_bytes, 0,
                                      luringcb->qiov->size - total_bytes);
                    ret = 0;
                }
            } else {
                ret = -ENOSPC;
            }
        }
end:
        luringcb->ret = ret;
        qemu_iovec_destroy(&luringcb->resubmit_qiov);

        /*
         * If the coroutine is already entered it must be in ioq_submit()
         * and will notice luringcb->ret has been filled in when it
         * eventually runs later. Coroutines cannot be entered recursively
         * so avoid doing that!
         */
        if (!qemu_coroutine_entered(luringcb->co)) {
            aio_co_wake(luringcb->co);
        }
    }
    qemu_bh_cancel(s->completion_bh);
}

static int ioq_submit(LuringState *s)
{
    int ret = 0;
    LuringAIOCB *luringcb, *luringcb_next;

    while (s->io_q.in_queue > 0) {
        /*
         * Try to fetch sqes from the ring for requests waiting in
         * the overflow queue
         */
        QSIMPLEQ_FOREACH_SAFE(luringcb, &s->io_q.submit_queue, next,
                              luringcb_next) {
            struct io_uring_sqe *sqes = io_uring_get_sqe(&s->ring);
            if (!sqes) {
                break;
            }
            /* Prep sqe for submission */
            *sqes = luringcb->sqeq;
            QSIMPLEQ_REMOVE_HEAD(&s->io_q.submit_queue, next);
        }
        ret = io_uring_submit(&s->ring);
        trace_luring_io_uring_submit(s, ret);
        /* Prevent infinite loop if submission is refused */
        if (ret <= 0) {
            if (ret == -EAGAIN || ret == -EINTR) {
                continue;
            }
            break;
        }
        s->io_q.in_flight += ret;
        s->io_q.in_queue  -= ret;
    }
    s->io_q.blocked = (s->io_q.in_queue > 0);

    if (s->io_q.in_flight) {
        /*
         * We can try to complete something just right away if there are
         * still requests in-flight.
         */
        luring_process_completions(s);
    }
    return ret;
}

static void luring_process_completions_and_submit(LuringState *s)
{
    aio_context_acquire(s->aio_context);
    luring_process_completions(s);

    if (!s->io_q.plugged && s->io_q.in_queue > 0) {
        ioq_submit(s);
    }
    aio_context_release(s->aio_context);
}

static void qemu_luring_completion_bh(void *opaque)
{
    LuringState *s = opaque;
    luring_process_completions_and_submit(s);
}

static void qemu_luring_completion_cb(void *opaque)
{
    LuringState *s = opaque;
    luring_process_completions_and_submit(s);
}

static bool qemu_luring_poll_cb(void *opaque)
{
    LuringState *s = opaque;

    return io_uring_cq_ready(&s->ring);
}

static void qemu_luring_poll_ready(void *opaque)
{
    LuringState *s = opaque;

    luring_process_completions_and_submit(s);
}

static void ioq_init(LuringQueue *io_q)
{
    QSIMPLEQ_INIT(&io_q->submit_queue);
    io_q->plugged = 0;
    io_q->in_queue = 0;
    io_q->in_flight = 0;
    io_q->blocked = false;
}

void luring_io_plug(BlockDriverState *bs, LuringState *s)
{
    trace_luring_io_plug(s);
    s->io_q.plugged++;
}

void luring_io_unplug(BlockDriverState *bs, LuringState *s)
{
    assert(s->io_q.plugged);
    trace_luring_io_unplug(s, s->io_q.blocked, s->io_q.plugged,
                           s->io_q.in_queue, s->io_q.in_flight);
    if (--s->io_q.plugged == 0 &&
        !s->io_q.blocked && s->io_q.in_queue > 0) {
        ioq_submit(s);
    }
}

/**
 * luring_do_submit:
 * @fd: file descriptor for I/O
 * @luringcb: AIO control block
 * @s: AIO state
 * @offset: offset for request
 * @type: type of request
 *
 * Fetches sqes from ring, adds to pending queue and preps them
 *
 */
static int luring_do_submit(int fd, LuringAIOCB *luringcb, LuringState *s,
                            uint64_t offset, int type)
{
    int ret;
    struct io_uring_sqe *sqes = &luringcb->sqeq;

    switch (type) {
    case QEMU_AIO_WRITE:
        io_uring_prep_writev(sqes, fd, luringcb->qiov->iov,
                             luringcb->qiov->niov, offset);
        break;
    case QEMU_AIO_READ:
        io_uring_prep_readv(sqes, fd, luringcb->qiov->iov,
                            luringcb->qiov->niov, offset);
        break;
    case QEMU_AIO_FLUSH:
        io_uring_prep_fsync(sqes, fd, IORING_FSYNC_DATASYNC);
        break;
    default:
        fprintf(stderr, "%s: invalid AIO request type, aborting 0x%x.\n",
                        __func__, type);
        abort();
    }
    io_uring_sqe_set_data(sqes, luringcb);

    QSIMPLEQ_INSERT_TAIL(&s->io_q.submit_queue, luringcb, next);
    s->io_q.in_queue++;
    trace_luring_do_submit(s, s->io_q.blocked, s->io_q.plugged,
                           s->io_q.in_queue, s->io_q.in_flight);
    if (!s->io_q.blocked &&
        (!s->io_q.plugged ||
         s->io_q.in_flight + s->io_q.in_queue >= MAX_ENTRIES)) {
        ret = ioq_submit(s);
        trace_luring_do_submit_done(s, ret);
        return ret;
    }
    return 0;
}

int coroutine_fn luring_co_submit(BlockDriverState *bs, LuringState *s, int fd,
                                  uint64_t offset, QEMUIOVector *qiov, int type)
{
    int ret;
    LuringAIOCB luringcb = {
        .co         = qemu_coroutine_self(),
        .ret        = -EINPROGRESS,
        .qiov       = qiov,
        .is_read    = (type == QEMU_AIO_READ),
    };
    trace_luring_co_submit(bs, s, &luringcb, fd, offset, qiov ? qiov->size : 0,
                           type);
    ret = luring_do_submit(fd, &luringcb, s, offset, type);

    if (ret < 0) {
        return ret;
    }

    if (luringcb.ret == -EINPROGRESS) {
        qemu_coroutine_yield();
    }
    return luringcb.ret;
}

void luring_detach_aio_context(LuringState *s, AioContext *old_context)
{
    aio_set_fd_handler(old_context, s->ring.ring_fd, false,
                       NULL, NULL, NULL, NULL, s);
    qemu_bh_delete(s->completion_bh);
    s->aio_context = NULL;
}

void luring_attach_aio_context(LuringState *s, AioContext *new_context)
{
    s->aio_context = new_context;
    s->completion_bh = aio_bh_new(new_context, qemu_luring_completion_bh, s);
    aio_set_fd_handler(s->aio_context, s->ring.ring_fd, false,
                       qemu_luring_completion_cb, NULL,
                       qemu_luring_poll_cb, qemu_luring_poll_ready, s);
}

LuringState *luring_init(Error **errp)
{
    int rc;
    LuringState *s = g_new0(LuringState, 1);
    struct io_uring *ring = &s->ring;

    trace_luring_init_state(s, sizeof(*s));

    rc = io_uring_queue_init(MAX_ENTRIES, ring, 0);
    if (rc < 0) {
        error_setg_errno(errp, errno, "failed to init linux io_uring ring");
        g_free(s);
        return NULL;
    }

    ioq_init(&s->io_q);
    return s;

}

void luring_cleanup(LuringState *s)
{
    io_uring_queue_exit(&s->ring);
    trace_luring_cleanup_state(s);
    g_free(s);
}
