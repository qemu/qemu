/*
 * Linux native AIO support.
 *
 * Copyright (C) 2009 IBM, Corp.
 * Copyright (C) 2009 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "block/aio.h"
#include "qemu/queue.h"
#include "block/block.h"
#include "block/raw-aio.h"
#include "qemu/event_notifier.h"
#include "qemu/coroutine.h"
#include "qapi/error.h"

#include <libaio.h>

/*
 * Queue size (per-device).
 *
 * XXX: eventually we need to communicate this to the guest and/or make it
 *      tunable by the guest.  If we get more outstanding requests at a time
 *      than this we will get EAGAIN from io_submit which is communicated to
 *      the guest as an I/O error.
 */
#define MAX_EVENTS 1024

/* Maximum number of requests in a batch. (default value) */
#define DEFAULT_MAX_BATCH 32

struct qemu_laiocb {
    Coroutine *co;
    LinuxAioState *ctx;
    struct iocb iocb;
    ssize_t ret;
    size_t nbytes;
    QEMUIOVector *qiov;
    bool is_read;
    QSIMPLEQ_ENTRY(qemu_laiocb) next;
};

typedef struct {
    int plugged;
    unsigned int in_queue;
    unsigned int in_flight;
    bool blocked;
    QSIMPLEQ_HEAD(, qemu_laiocb) pending;
} LaioQueue;

struct LinuxAioState {
    AioContext *aio_context;

    io_context_t ctx;
    EventNotifier e;

    /* io queue for submit at batch.  Protected by AioContext lock. */
    LaioQueue io_q;

    /* I/O completion processing.  Only runs in I/O thread.  */
    QEMUBH *completion_bh;
    int event_idx;
    int event_max;
};

static void ioq_submit(LinuxAioState *s);

static inline ssize_t io_event_ret(struct io_event *ev)
{
    return (ssize_t)(((uint64_t)ev->res2 << 32) | ev->res);
}

/*
 * Completes an AIO request.
 */
static void qemu_laio_process_completion(struct qemu_laiocb *laiocb)
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
                ret = -ENOSPC;
            }
        }
    }

    laiocb->ret = ret;

    /*
     * If the coroutine is already entered it must be in ioq_submit() and
     * will notice laio->ret has been filled in when it eventually runs
     * later.  Coroutines cannot be entered recursively so avoid doing
     * that!
     */
    if (!qemu_coroutine_entered(laiocb->co)) {
        aio_co_wake(laiocb->co);
    }
}

/**
 * aio_ring buffer which is shared between userspace and kernel.
 *
 * This copied from linux/fs/aio.c, common header does not exist
 * but AIO exists for ages so we assume ABI is stable.
 */
struct aio_ring {
    unsigned    id;    /* kernel internal index number */
    unsigned    nr;    /* number of io_events */
    unsigned    head;  /* Written to by userland or by kernel. */
    unsigned    tail;

    unsigned    magic;
    unsigned    compat_features;
    unsigned    incompat_features;
    unsigned    header_length;  /* size of aio_ring */

    struct io_event io_events[];
};

/**
 * io_getevents_peek:
 * @ctx: AIO context
 * @events: pointer on events array, output value

 * Returns the number of completed events and sets a pointer
 * on events array.  This function does not update the internal
 * ring buffer, only reads head and tail.  When @events has been
 * processed io_getevents_commit() must be called.
 */
static inline unsigned int io_getevents_peek(io_context_t ctx,
                                             struct io_event **events)
{
    struct aio_ring *ring = (struct aio_ring *)ctx;
    unsigned int head = ring->head, tail = ring->tail;
    unsigned int nr;

    nr = tail >= head ? tail - head : ring->nr - head;
    *events = ring->io_events + head;
    /* To avoid speculative loads of s->events[i] before observing tail.
       Paired with smp_wmb() inside linux/fs/aio.c: aio_complete(). */
    smp_rmb();

    return nr;
}

/**
 * io_getevents_commit:
 * @ctx: AIO context
 * @nr: the number of events on which head should be advanced
 *
 * Advances head of a ring buffer.
 */
static inline void io_getevents_commit(io_context_t ctx, unsigned int nr)
{
    struct aio_ring *ring = (struct aio_ring *)ctx;

    if (nr) {
        ring->head = (ring->head + nr) % ring->nr;
    }
}

/**
 * io_getevents_advance_and_peek:
 * @ctx: AIO context
 * @events: pointer on events array, output value
 * @nr: the number of events on which head should be advanced
 *
 * Advances head of a ring buffer and returns number of elements left.
 */
static inline unsigned int
io_getevents_advance_and_peek(io_context_t ctx,
                              struct io_event **events,
                              unsigned int nr)
{
    io_getevents_commit(ctx, nr);
    return io_getevents_peek(ctx, events);
}

/**
 * qemu_laio_process_completions:
 * @s: AIO state
 *
 * Fetches completed I/O requests and invokes their callbacks.
 *
 * The function is somewhat tricky because it supports nested event loops, for
 * example when a request callback invokes aio_poll().  In order to do this,
 * indices are kept in LinuxAioState.  Function schedules BH completion so it
 * can be called again in a nested event loop.  When there are no events left
 * to complete the BH is being canceled.
 */
static void qemu_laio_process_completions(LinuxAioState *s)
{
    struct io_event *events;

    /* Reschedule so nested event loops see currently pending completions */
    qemu_bh_schedule(s->completion_bh);

    while ((s->event_max = io_getevents_advance_and_peek(s->ctx, &events,
                                                         s->event_idx))) {
        for (s->event_idx = 0; s->event_idx < s->event_max; ) {
            struct iocb *iocb = events[s->event_idx].obj;
            struct qemu_laiocb *laiocb =
                container_of(iocb, struct qemu_laiocb, iocb);

            laiocb->ret = io_event_ret(&events[s->event_idx]);

            /* Change counters one-by-one because we can be nested. */
            s->io_q.in_flight--;
            s->event_idx++;
            qemu_laio_process_completion(laiocb);
        }
    }

    qemu_bh_cancel(s->completion_bh);

    /* If we are nested we have to notify the level above that we are done
     * by setting event_max to zero, upper level will then jump out of it's
     * own `for` loop.  If we are the last all counters droped to zero. */
    s->event_max = 0;
    s->event_idx = 0;
}

static void qemu_laio_process_completions_and_submit(LinuxAioState *s)
{
    aio_context_acquire(s->aio_context);
    qemu_laio_process_completions(s);

    if (!s->io_q.plugged && !QSIMPLEQ_EMPTY(&s->io_q.pending)) {
        ioq_submit(s);
    }
    aio_context_release(s->aio_context);
}

static void qemu_laio_completion_bh(void *opaque)
{
    LinuxAioState *s = opaque;

    qemu_laio_process_completions_and_submit(s);
}

static void qemu_laio_completion_cb(EventNotifier *e)
{
    LinuxAioState *s = container_of(e, LinuxAioState, e);

    if (event_notifier_test_and_clear(&s->e)) {
        qemu_laio_process_completions_and_submit(s);
    }
}

static bool qemu_laio_poll_cb(void *opaque)
{
    EventNotifier *e = opaque;
    LinuxAioState *s = container_of(e, LinuxAioState, e);
    struct io_event *events;

    if (!io_getevents_peek(s->ctx, &events)) {
        return false;
    }

    qemu_laio_process_completions_and_submit(s);
    return true;
}

static void ioq_init(LaioQueue *io_q)
{
    QSIMPLEQ_INIT(&io_q->pending);
    io_q->plugged = 0;
    io_q->in_queue = 0;
    io_q->in_flight = 0;
    io_q->blocked = false;
}

static void ioq_submit(LinuxAioState *s)
{
    int ret, len;
    struct qemu_laiocb *aiocb;
    struct iocb *iocbs[MAX_EVENTS];
    QSIMPLEQ_HEAD(, qemu_laiocb) completed;

    do {
        if (s->io_q.in_flight >= MAX_EVENTS) {
            break;
        }
        len = 0;
        QSIMPLEQ_FOREACH(aiocb, &s->io_q.pending, next) {
            iocbs[len++] = &aiocb->iocb;
            if (s->io_q.in_flight + len >= MAX_EVENTS) {
                break;
            }
        }

        ret = io_submit(s->ctx, len, iocbs);
        if (ret == -EAGAIN) {
            break;
        }
        if (ret < 0) {
            /* Fail the first request, retry the rest */
            aiocb = QSIMPLEQ_FIRST(&s->io_q.pending);
            QSIMPLEQ_REMOVE_HEAD(&s->io_q.pending, next);
            s->io_q.in_queue--;
            aiocb->ret = ret;
            qemu_laio_process_completion(aiocb);
            continue;
        }

        s->io_q.in_flight += ret;
        s->io_q.in_queue  -= ret;
        aiocb = container_of(iocbs[ret - 1], struct qemu_laiocb, iocb);
        QSIMPLEQ_SPLIT_AFTER(&s->io_q.pending, aiocb, next, &completed);
    } while (ret == len && !QSIMPLEQ_EMPTY(&s->io_q.pending));
    s->io_q.blocked = (s->io_q.in_queue > 0);

    if (s->io_q.in_flight) {
        /* We can try to complete something just right away if there are
         * still requests in-flight. */
        qemu_laio_process_completions(s);
        /*
         * Even we have completed everything (in_flight == 0), the queue can
         * have still pended requests (in_queue > 0).  We do not attempt to
         * repeat submission to avoid IO hang.  The reason is simple: s->e is
         * still set and completion callback will be called shortly and all
         * pended requests will be submitted from there.
         */
    }
}

static uint64_t laio_max_batch(LinuxAioState *s, uint64_t dev_max_batch)
{
    uint64_t max_batch = s->aio_context->aio_max_batch ?: DEFAULT_MAX_BATCH;

    /*
     * AIO context can be shared between multiple block devices, so
     * `dev_max_batch` allows reducing the batch size for latency-sensitive
     * devices.
     */
    max_batch = MIN_NON_ZERO(dev_max_batch, max_batch);

    /* limit the batch with the number of available events */
    max_batch = MIN_NON_ZERO(MAX_EVENTS - s->io_q.in_flight, max_batch);

    return max_batch;
}

void laio_io_plug(BlockDriverState *bs, LinuxAioState *s)
{
    s->io_q.plugged++;
}

void laio_io_unplug(BlockDriverState *bs, LinuxAioState *s,
                    uint64_t dev_max_batch)
{
    assert(s->io_q.plugged);
    if (s->io_q.in_queue >= laio_max_batch(s, dev_max_batch) ||
        (--s->io_q.plugged == 0 &&
         !s->io_q.blocked && !QSIMPLEQ_EMPTY(&s->io_q.pending))) {
        ioq_submit(s);
    }
}

static int laio_do_submit(int fd, struct qemu_laiocb *laiocb, off_t offset,
                          int type, uint64_t dev_max_batch)
{
    LinuxAioState *s = laiocb->ctx;
    struct iocb *iocbs = &laiocb->iocb;
    QEMUIOVector *qiov = laiocb->qiov;

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
        return -EIO;
    }
    io_set_eventfd(&laiocb->iocb, event_notifier_get_fd(&s->e));

    QSIMPLEQ_INSERT_TAIL(&s->io_q.pending, laiocb, next);
    s->io_q.in_queue++;
    if (!s->io_q.blocked &&
        (!s->io_q.plugged ||
         s->io_q.in_queue >= laio_max_batch(s, dev_max_batch))) {
        ioq_submit(s);
    }

    return 0;
}

int coroutine_fn laio_co_submit(BlockDriverState *bs, LinuxAioState *s, int fd,
                                uint64_t offset, QEMUIOVector *qiov, int type,
                                uint64_t dev_max_batch)
{
    int ret;
    struct qemu_laiocb laiocb = {
        .co         = qemu_coroutine_self(),
        .nbytes     = qiov->size,
        .ctx        = s,
        .ret        = -EINPROGRESS,
        .is_read    = (type == QEMU_AIO_READ),
        .qiov       = qiov,
    };

    ret = laio_do_submit(fd, &laiocb, offset, type, dev_max_batch);
    if (ret < 0) {
        return ret;
    }

    if (laiocb.ret == -EINPROGRESS) {
        qemu_coroutine_yield();
    }
    return laiocb.ret;
}

void laio_detach_aio_context(LinuxAioState *s, AioContext *old_context)
{
    aio_set_event_notifier(old_context, &s->e, false, NULL, NULL);
    qemu_bh_delete(s->completion_bh);
    s->aio_context = NULL;
}

void laio_attach_aio_context(LinuxAioState *s, AioContext *new_context)
{
    s->aio_context = new_context;
    s->completion_bh = aio_bh_new(new_context, qemu_laio_completion_bh, s);
    aio_set_event_notifier(new_context, &s->e, false,
                           qemu_laio_completion_cb,
                           qemu_laio_poll_cb);
}

LinuxAioState *laio_init(Error **errp)
{
    int rc;
    LinuxAioState *s;

    s = g_malloc0(sizeof(*s));
    rc = event_notifier_init(&s->e, false);
    if (rc < 0) {
        error_setg_errno(errp, -rc, "failed to to initialize event notifier");
        goto out_free_state;
    }

    rc = io_setup(MAX_EVENTS, &s->ctx);
    if (rc < 0) {
        error_setg_errno(errp, -rc, "failed to create linux AIO context");
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

void laio_cleanup(LinuxAioState *s)
{
    event_notifier_cleanup(&s->e);

    if (io_destroy(s->ctx) != 0) {
        fprintf(stderr, "%s: destroy AIO context %p failed\n",
                        __func__, &s->ctx);
    }
    g_free(s);
}
