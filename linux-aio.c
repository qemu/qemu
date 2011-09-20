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
#include "block_int.h"
#include "block/raw-posix-aio.h"

#include <sys/eventfd.h>
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
    QLIST_ENTRY(qemu_laiocb) node;
};

struct qemu_laio_state {
    io_context_t ctx;
    int efd;
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
        if (ret == laiocb->nbytes)
            ret = 0;
        else if (ret >= 0)
            ret = -EINVAL;

        laiocb->common.cb(laiocb->common.opaque, ret);
    }

    qemu_aio_release(laiocb);
}

static void qemu_laio_completion_cb(void *opaque)
{
    struct qemu_laio_state *s = opaque;

    while (1) {
        struct io_event events[MAX_EVENTS];
        uint64_t val;
        ssize_t ret;
        struct timespec ts = { 0 };
        int nevents, i;

        do {
            ret = read(s->efd, &val, sizeof(val));
        } while (ret == -1 && errno == EINTR);

        if (ret == -1 && errno == EAGAIN)
            break;

        if (ret != 8)
            break;

        do {
            nevents = io_getevents(s->ctx, val, MAX_EVENTS, events, &ts);
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

static int qemu_laio_flush_cb(void *opaque)
{
    struct qemu_laio_state *s = opaque;

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
    while (laiocb->ret == -EINPROGRESS)
        qemu_laio_completion_cb(laiocb->ctx);
}

static AIOPool laio_pool = {
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

    laiocb = qemu_aio_get(&laio_pool, bs, cb, opaque);
    if (!laiocb)
        return NULL;
    laiocb->nbytes = nb_sectors * 512;
    laiocb->ctx = s;
    laiocb->ret = -EINPROGRESS;

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
    io_set_eventfd(&laiocb->iocb, s->efd);
    s->count++;

    if (io_submit(s->ctx, 1, &iocbs) < 0)
        goto out_dec_count;
    return &laiocb->common;

out_free_aiocb:
    qemu_aio_release(laiocb);
out_dec_count:
    s->count--;
    return NULL;
}

void *laio_init(void)
{
    struct qemu_laio_state *s;

    s = g_malloc0(sizeof(*s));
    s->efd = eventfd(0, 0);
    if (s->efd == -1)
        goto out_free_state;
    fcntl(s->efd, F_SETFL, O_NONBLOCK);

    if (io_setup(MAX_EVENTS, &s->ctx) != 0)
        goto out_close_efd;

    qemu_aio_set_fd_handler(s->efd, qemu_laio_completion_cb, NULL,
        qemu_laio_flush_cb, NULL, s);

    return s;

out_close_efd:
    close(s->efd);
out_free_state:
    g_free(s);
    return NULL;
}
