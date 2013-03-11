/*
 * Linux AIO request queue
 *
 * Copyright 2012 IBM, Corp.
 * Copyright 2012 Red Hat, Inc. and/or its affiliates
 *
 * Authors:
 *   Stefan Hajnoczi <stefanha@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "ioq.h"

void ioq_init(IOQueue *ioq, int fd, unsigned int max_reqs)
{
    int rc;

    ioq->fd = fd;
    ioq->max_reqs = max_reqs;

    memset(&ioq->io_ctx, 0, sizeof ioq->io_ctx);
    rc = io_setup(max_reqs, &ioq->io_ctx);
    if (rc != 0) {
        fprintf(stderr, "ioq io_setup failed %d\n", rc);
        exit(1);
    }

    rc = event_notifier_init(&ioq->io_notifier, 0);
    if (rc != 0) {
        fprintf(stderr, "ioq io event notifier creation failed %d\n", rc);
        exit(1);
    }

    ioq->freelist = g_malloc0(sizeof ioq->freelist[0] * max_reqs);
    ioq->freelist_idx = 0;

    ioq->queue = g_malloc0(sizeof ioq->queue[0] * max_reqs);
    ioq->queue_idx = 0;
}

void ioq_cleanup(IOQueue *ioq)
{
    g_free(ioq->freelist);
    g_free(ioq->queue);

    event_notifier_cleanup(&ioq->io_notifier);
    io_destroy(ioq->io_ctx);
}

EventNotifier *ioq_get_notifier(IOQueue *ioq)
{
    return &ioq->io_notifier;
}

struct iocb *ioq_get_iocb(IOQueue *ioq)
{
    /* Underflow cannot happen since ioq is sized for max_reqs */
    assert(ioq->freelist_idx != 0);

    struct iocb *iocb = ioq->freelist[--ioq->freelist_idx];
    ioq->queue[ioq->queue_idx++] = iocb;
    return iocb;
}

void ioq_put_iocb(IOQueue *ioq, struct iocb *iocb)
{
    /* Overflow cannot happen since ioq is sized for max_reqs */
    assert(ioq->freelist_idx != ioq->max_reqs);

    ioq->freelist[ioq->freelist_idx++] = iocb;
}

struct iocb *ioq_rdwr(IOQueue *ioq, bool read, struct iovec *iov,
                      unsigned int count, long long offset)
{
    struct iocb *iocb = ioq_get_iocb(ioq);

    if (read) {
        io_prep_preadv(iocb, ioq->fd, iov, count, offset);
    } else {
        io_prep_pwritev(iocb, ioq->fd, iov, count, offset);
    }
    io_set_eventfd(iocb, event_notifier_get_fd(&ioq->io_notifier));
    return iocb;
}

int ioq_submit(IOQueue *ioq)
{
    int rc = io_submit(ioq->io_ctx, ioq->queue_idx, ioq->queue);
    ioq->queue_idx = 0; /* reset */
    return rc;
}

int ioq_run_completion(IOQueue *ioq, IOQueueCompletion *completion,
                       void *opaque)
{
    struct io_event events[ioq->max_reqs];
    int nevents, i;

    do {
        nevents = io_getevents(ioq->io_ctx, 0, ioq->max_reqs, events, NULL);
    } while (nevents < 0 && errno == EINTR);
    if (nevents < 0) {
        return nevents;
    }

    for (i = 0; i < nevents; i++) {
        ssize_t ret = ((uint64_t)events[i].res2 << 32) | events[i].res;

        completion(events[i].obj, ret, opaque);
        ioq_put_iocb(ioq, events[i].obj);
    }
    return nevents;
}
