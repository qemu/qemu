/*
 * QEMU aio implementation
 *
 * Copyright IBM Corp., 2008
 * Copyright Red Hat Inc., 2012
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *  Paolo Bonzini     <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu-common.h"
#include "block/block.h"
#include "qemu/queue.h"
#include "qemu/sockets.h"

struct AioHandler {
    EventNotifier *e;
    EventNotifierHandler *io_notify;
    AioFlushEventNotifierHandler *io_flush;
    GPollFD pfd;
    int deleted;
    QLIST_ENTRY(AioHandler) node;
};

void aio_set_event_notifier(AioContext *ctx,
                            EventNotifier *e,
                            EventNotifierHandler *io_notify,
                            AioFlushEventNotifierHandler *io_flush)
{
    AioHandler *node;

    QLIST_FOREACH(node, &ctx->aio_handlers, node) {
        if (node->e == e && !node->deleted) {
            break;
        }
    }

    /* Are we deleting the fd handler? */
    if (!io_notify) {
        if (node) {
            g_source_remove_poll(&ctx->source, &node->pfd);

            /* If the lock is held, just mark the node as deleted */
            if (ctx->walking_handlers) {
                node->deleted = 1;
                node->pfd.revents = 0;
            } else {
                /* Otherwise, delete it for real.  We can't just mark it as
                 * deleted because deleted nodes are only cleaned up after
                 * releasing the walking_handlers lock.
                 */
                QLIST_REMOVE(node, node);
                g_free(node);
            }
        }
    } else {
        if (node == NULL) {
            /* Alloc and insert if it's not already there */
            node = g_malloc0(sizeof(AioHandler));
            node->e = e;
            node->pfd.fd = (uintptr_t)event_notifier_get_handle(e);
            node->pfd.events = G_IO_IN;
            QLIST_INSERT_HEAD(&ctx->aio_handlers, node, node);

            g_source_add_poll(&ctx->source, &node->pfd);
        }
        /* Update handler with latest information */
        node->io_notify = io_notify;
        node->io_flush = io_flush;
    }

    aio_notify(ctx);
}

bool aio_pending(AioContext *ctx)
{
    AioHandler *node;

    QLIST_FOREACH(node, &ctx->aio_handlers, node) {
        if (node->pfd.revents && node->io_notify) {
            return true;
        }
    }

    return false;
}

bool aio_poll(AioContext *ctx, bool blocking)
{
    AioHandler *node;
    HANDLE events[MAXIMUM_WAIT_OBJECTS + 1];
    bool busy, progress;
    int count;

    progress = false;

    /*
     * If there are callbacks left that have been queued, we need to call then.
     * Do not call select in this case, because it is possible that the caller
     * does not need a complete flush (as is the case for qemu_aio_wait loops).
     */
    if (aio_bh_poll(ctx)) {
        blocking = false;
        progress = true;
    }

    /*
     * Then dispatch any pending callbacks from the GSource.
     *
     * We have to walk very carefully in case qemu_aio_set_fd_handler is
     * called while we're walking.
     */
    node = QLIST_FIRST(&ctx->aio_handlers);
    while (node) {
        AioHandler *tmp;

        ctx->walking_handlers++;

        if (node->pfd.revents && node->io_notify) {
            node->pfd.revents = 0;
            node->io_notify(node->e);
            progress = true;
        }

        tmp = node;
        node = QLIST_NEXT(node, node);

        ctx->walking_handlers--;

        if (!ctx->walking_handlers && tmp->deleted) {
            QLIST_REMOVE(tmp, node);
            g_free(tmp);
        }
    }

    if (progress && !blocking) {
        return true;
    }

    ctx->walking_handlers++;

    /* fill fd sets */
    busy = false;
    count = 0;
    QLIST_FOREACH(node, &ctx->aio_handlers, node) {
        /* If there aren't pending AIO operations, don't invoke callbacks.
         * Otherwise, if there are no AIO requests, qemu_aio_wait() would
         * wait indefinitely.
         */
        if (!node->deleted && node->io_flush) {
            if (node->io_flush(node->e) == 0) {
                continue;
            }
            busy = true;
        }
        if (!node->deleted && node->io_notify) {
            events[count++] = event_notifier_get_handle(node->e);
        }
    }

    ctx->walking_handlers--;

    /* No AIO operations?  Get us out of here */
    if (!busy) {
        return progress;
    }

    /* wait until next event */
    while (count > 0) {
        int timeout = blocking ? INFINITE : 0;
        int ret = WaitForMultipleObjects(count, events, FALSE, timeout);

        /* if we have any signaled events, dispatch event */
        if ((DWORD) (ret - WAIT_OBJECT_0) >= count) {
            break;
        }

        blocking = false;

        /* we have to walk very carefully in case
         * qemu_aio_set_fd_handler is called while we're walking */
        node = QLIST_FIRST(&ctx->aio_handlers);
        while (node) {
            AioHandler *tmp;

            ctx->walking_handlers++;

            if (!node->deleted &&
                event_notifier_get_handle(node->e) == events[ret - WAIT_OBJECT_0] &&
                node->io_notify) {
                node->io_notify(node->e);
                progress = true;
            }

            tmp = node;
            node = QLIST_NEXT(node, node);

            ctx->walking_handlers--;

            if (!ctx->walking_handlers && tmp->deleted) {
                QLIST_REMOVE(tmp, node);
                g_free(tmp);
            }
        }

        /* Try again, but only call each handler once.  */
        events[ret - WAIT_OBJECT_0] = events[--count];
    }

    assert(progress || busy);
    return true;
}
