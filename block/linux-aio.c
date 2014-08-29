/*
 * Linux native AIO support.
 *
 * Copyright (C) 2009 IBM, Corp.
 * Copyright (C) 2009 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "qemu-common.h"
#include "block/aio.h"
#include "qemu/queue.h"
#include "block/raw-aio.h"
#include "qemu/event_notifier.h"

#include <libaio.h>

/*
 * Queue size (per-device).
 *
 * XXX: eventually we need to communicate this to the guest and/or make it
 *      tunable by the guest.  If we get more outstanding requests at a time
 *      than this we will get EAGAIN from io_submit which is communicated to
 *      the guest as an I/O error.
 */
#define MAX_EVENTS 128

#define MAX_QUEUED_IO  128

struct qemu_laiocb {
    BlockDriverAIOCB common;
    struct qemu_laio_state *ctx;
    struct iocb iocb;
    ssize_t ret;
    size_t nbytes;
    QEMUIOVector *qiov;
    bool is_read;
    QLIST_ENTRY(qemu_laiocb) node;
};

typedef struct {
    struct iocb *iocbs[MAX_QUEUED_IO];
    int plugged;
    unsigned int size;
    unsigned int idx;
} LaioQueue;

struct qemu_laio_state {
    io_context_t ctx;
    EventNotifier e;

    /* io queue for submit at batch */
    LaioQueue io_q;

    /* I/O completion processing */
    QEMUBH *completion_bh;
    struct io_event events[MAX_EVENTS];
    int event_idx;
    int event_max;
};

static inline ssize_t io_event_ret(struct io_event *ev)
{
    return (ssize_t)(((uint64_t)ev->res2 << 32) | ev->res);
}

/*
 * Completes an AIO request (calls the callback and frees the ACB).
 */
static void qemu_laio_process_completion(struct qemu_laio_state *s,
    struct qemu_laiocb *laiocb)
{
    int ret;

    ret = laiocb->ret;
    if (ret != -ECANCELED) {
        if (ret == laiocb->nbytes) {
            ret = 0;
        } else if (ret >= 0) {
            /* Short reads mean EOF, pad with zeros. */
            if (laiocb->is_read) {
                qemu_iovec_memset(laiocb->qiov, ret, 0,
                    laiocb->qiov->size - ret);
            } else {
                ret = -EINVAL;
            }
        }

        laiocb->common.cb(laiocb->common.opaque, ret);
    }

    qemu_aio_release(laiocb);
}

/* The completion BH fetches completed I/O requests and invokes their
 * callbacks.
 *
 * The function is somewhat tricky because it supports nested event loops, for
 * example when a request callback invokes aio_poll().  In order to do this,
 * the completion events array and index are kept in qemu_laio_state.  The BH
 * reschedules itself as long as there are completions pending so it will
 * either be called again in a nested event loop or will be called after all
 * events have been completed.  When there are no events left to complete, the
 * BH returns without rescheduling.
 */
static void qemu_laio_completion_bh(void *opaque)
{
    struct qemu_laio_state *s = opaque;

    /* Fetch more completion events when empty */
    if (s->event_idx == s->event_max) {
        do {
            struct timespec ts = { 0 };
            s->event_max = io_getevents(s->ctx, MAX_EVENTS, MAX_EVENTS,
                                        s->events, &ts);
        } while (s->event_max == -EINTR);

        s->event_idx = 0;
        if (s->event_max <= 0) {
            s->event_max = 0;
            return; /* no more events */
        }
    }

    /* Reschedule so nested event loops see currently pending completions */
    qemu_bh_schedule(s->completion_bh);

    /* Process completion events */
    while (s->event_idx < s->event_max) {
        struct iocb *iocb = s->events[s->event_idx].obj;
        struct qemu_laiocb *laiocb =
                container_of(iocb, struct qemu_laiocb, iocb);

        laiocb->ret = io_event_ret(&s->events[s->event_idx]);
        s->event_idx++;

        qemu_laio_process_completion(s, laiocb);
    }
}

static void qemu_laio_completion_cb(EventNotifier *e)
{
    struct qemu_laio_state *s = container_of(e, struct qemu_laio_state, e);

    if (event_notifier_test_and_clear(&s->e)) {
        qemu_bh_schedule(s->completion_bh);
    }
}

static void laio_cancel(BlockDriverAIOCB *blockacb)
{
    struct qemu_laiocb *laiocb = (struct qemu_laiocb *)blockacb;
    struct io_event event;
    int ret;

    if (laiocb->ret != -EINPROGRESS)
        return;

    /*
     * Note that as of Linux 2.6.31 neither the block device code nor any
     * filesystem implements cancellation of AIO request.
     * Thus the polling loop below is the normal code path.
     */
    ret = io_cancel(laiocb->ctx->ctx, &laiocb->iocb, &event);
    if (ret == 0) {
        laiocb->ret = -ECANCELED;
        return;
    }

    /*
     * We have to wait for the iocb to finish.
     *
     * The only way to get the iocb status update is by polling the io context.
     * We might be able to do this slightly more optimal by removing the
     * O_NONBLOCK flag.
     */
    while (laiocb->ret == -EINPROGRESS) {
        qemu_laio_completion_cb(&laiocb->ctx->e);
    }
}

static const AIOCBInfo laio_aiocb_info = {
    .aiocb_size         = sizeof(struct qemu_laiocb),
    .cancel             = laio_cancel,
};

static void ioq_init(LaioQueue *io_q)
{
    io_q->size = MAX_QUEUED_IO;
    io_q->idx = 0;
    io_q->plugged = 0;
}

static int ioq_submit(struct qemu_laio_state *s)
{
    int ret, i = 0;
    int len = s->io_q.idx;

    do {
        ret = io_submit(s->ctx, len, s->io_q.iocbs);
    } while (i++ < 3 && ret == -EAGAIN);

    /* empty io queue */
    s->io_q.idx = 0;

    if (ret < 0) {
        i = 0;
    } else {
        i = ret;
    }

    for (; i < len; i++) {
        struct qemu_laiocb *laiocb =
            container_of(s->io_q.iocbs[i], struct qemu_laiocb, iocb);

        laiocb->ret = (ret < 0) ? ret : -EIO;
        qemu_laio_process_completion(s, laiocb);
    }
    return ret;
}

static void ioq_enqueue(struct qemu_laio_state *s, struct iocb *iocb)
{
    unsigned int idx = s->io_q.idx;

    s->io_q.iocbs[idx++] = iocb;
    s->io_q.idx = idx;

    /* submit immediately if queue is full */
    if (idx == s->io_q.size) {
        ioq_submit(s);
    }
}

void laio_io_plug(BlockDriverState *bs, void *aio_ctx)
{
    struct qemu_laio_state *s = aio_ctx;

    s->io_q.plugged++;
}

int laio_io_unplug(BlockDriverState *bs, void *aio_ctx, bool unplug)
{
    struct qemu_laio_state *s = aio_ctx;
    int ret = 0;

    assert(s->io_q.plugged > 0 || !unplug);

    if (unplug && --s->io_q.plugged > 0) {
        return 0;
    }

    if (s->io_q.idx > 0) {
        ret = ioq_submit(s);
    }

    return ret;
}

BlockDriverAIOCB *laio_submit(BlockDriverState *bs, void *aio_ctx, int fd,
        int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
        BlockDriverCompletionFunc *cb, void *opaque, int type)
{
    struct qemu_laio_state *s = aio_ctx;
    struct qemu_laiocb *laiocb;
    struct iocb *iocbs;
    off_t offset = sector_num * 512;

    laiocb = qemu_aio_get(&laio_aiocb_info, bs, cb, opaque);
    laiocb->nbytes = nb_sectors * 512;
    laiocb->ctx = s;
    laiocb->ret = -EINPROGRESS;
    laiocb->is_read = (type == QEMU_AIO_READ);
    laiocb->qiov = qiov;

    iocbs = &laiocb->iocb;

    switch (type) {
    case QEMU_AIO_WRITE:
        io_prep_pwritev(iocbs, fd, qiov->iov, qiov->niov, offset);
	break;
    case QEMU_AIO_READ:
        io_prep_preadv(iocbs, fd, qiov->iov, qiov->niov, offset);
	break;
    /* Currently Linux kernel does not support other operations */
    default:
        fprintf(stderr, "%s: invalid AIO request type 0x%x.\n",
                        __func__, type);
        goto out_free_aiocb;
    }
    io_set_eventfd(&laiocb->iocb, event_notifier_get_fd(&s->e));

    if (!s->io_q.plugged) {
        if (io_submit(s->ctx, 1, &iocbs) < 0) {
            goto out_free_aiocb;
        }
    } else {
        ioq_enqueue(s, iocbs);
    }
    return &laiocb->common;

out_free_aiocb:
    qemu_aio_release(laiocb);
    return NULL;
}

void laio_detach_aio_context(void *s_, AioContext *old_context)
{
    struct qemu_laio_state *s = s_;

    aio_set_event_notifier(old_context, &s->e, NULL);
    qemu_bh_delete(s->completion_bh);
}

void laio_attach_aio_context(void *s_, AioContext *new_context)
{
    struct qemu_laio_state *s = s_;

    s->completion_bh = aio_bh_new(new_context, qemu_laio_completion_bh, s);
    aio_set_event_notifier(new_context, &s->e, qemu_laio_completion_cb);
}

void *laio_init(void)
{
    struct qemu_laio_state *s;

    s = g_malloc0(sizeof(*s));
    if (event_notifier_init(&s->e, false) < 0) {
        goto out_free_state;
    }

    if (io_setup(MAX_EVENTS, &s->ctx) != 0) {
        goto out_close_efd;
    }

    ioq_init(&s->io_q);

    return s;

out_close_efd:
    event_notifier_cleanup(&s->e);
out_free_state:
    g_free(s);
    return NULL;
}

void laio_cleanup(void *s_)
{
    struct qemu_laio_state *s = s_;

    event_notifier_cleanup(&s->e);

    if (io_destroy(s->ctx) != 0) {
        fprintf(stderr, "%s: destroy AIO context %p failed\n",
                        __func__, &s->ctx);
    }
    g_free(s);
}
