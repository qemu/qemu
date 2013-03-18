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

#ifndef IOQ_H
#define IOQ_H

#include <libaio.h>
#include "qemu/event_notifier.h"

typedef struct {
    int fd;                         /* file descriptor */
    unsigned int max_reqs;          /* max length of freelist and queue */

    io_context_t io_ctx;            /* Linux AIO context */
    EventNotifier io_notifier;      /* Linux AIO eventfd */

    /* Requests can complete in any order so a free list is necessary to manage
     * available iocbs.
     */
    struct iocb **freelist;         /* free iocbs */
    unsigned int freelist_idx;

    /* Multiple requests are queued up before submitting them all in one go */
    struct iocb **queue;            /* queued iocbs */
    unsigned int queue_idx;
} IOQueue;

void ioq_init(IOQueue *ioq, int fd, unsigned int max_reqs);
void ioq_cleanup(IOQueue *ioq);
EventNotifier *ioq_get_notifier(IOQueue *ioq);
struct iocb *ioq_get_iocb(IOQueue *ioq);
void ioq_put_iocb(IOQueue *ioq, struct iocb *iocb);
struct iocb *ioq_rdwr(IOQueue *ioq, bool read, struct iovec *iov,
                      unsigned int count, long long offset);
int ioq_submit(IOQueue *ioq);

static inline unsigned int ioq_num_queued(IOQueue *ioq)
{
    return ioq->queue_idx;
}

typedef void IOQueueCompletion(struct iocb *iocb, ssize_t ret, void *opaque);
int ioq_run_completion(IOQueue *ioq, IOQueueCompletion *completion,
                       void *opaque);

#endif /* IOQ_H */
