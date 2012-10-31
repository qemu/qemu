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
#include "qemu-aio.h"
#include "qemu-queue.h"
#include "block/raw-aio.h"
#include "event_notifier.h"

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

struct qemu_laio_state {
    io_context_t ctx;
    EventNotifier e;
    int count;
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

    s->count--;

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

static void qemu_laio_completion_cb(EventNotifier *e)
{
    struct qemu_laio_state *s = container_of(e, struct qemu_laio_state, e);

    while (event_notifier_test_and_clear(&s->e)) {
        struct io_event events[MAX_EVENTS];
        struct timespec ts = { 0 };
        int nevents, i;

        do {
            nevents = io_getevents(s->ctx, MAX_EVENTS, MAX_EVENTS, events, &ts);
        } while (nevents == -EINTR);

        for (i = 0; i < nevents; i++) {
            struct iocb *iocb = events[i].obj;
            struct qemu_laiocb *laiocb =
                    container_of(iocb, struct qemu_laiocb, iocb);

            laiocb->ret = io_event_ret(&events[i]);
            qemu_laio_process_completion(s, laiocb);
        }
    }
}

static int qemu_laio_flush_cb(EventNotifier *e)
{
    struct qemu_laio_state *s = container_of(e, struct qemu_laio_state, e);

    return (s->count > 0) ? 1 : 0;
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
    s->count++;

    if (io_submit(s->ctx, 1, &iocbs) < 0)
        goto out_dec_count;
    return &laiocb->common;

out_dec_count:
    s->count--;
out_free_aiocb:
    qemu_aio_release(laiocb);
    return NULL;
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

    qemu_aio_set_event_notifier(&s->e, qemu_laio_completion_cb,
                                qemu_laio_flush_cb);

    return s;

out_close_efd:
    event_notifier_cleanup(&s->e);
out_free_state:
    g_free(s);
    return NULL;
}
