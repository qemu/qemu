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
    GPollFD pfd;
    int deleted;
    QLIST_ENTRY(AioHandler) node;
};

void aio_set_event_notifier(AioContext *ctx,
                            EventNotifier *e,
                            EventNotifierHandler *io_notify)
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
    bool progress;
    int count;
    int timeout;

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

    /* Run timers */
    progress |= timerlistgroup_run_timers(&ctx->tlg);

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

            /* aio_notify() does not count as progress */
            if (node->e != &ctx->notifier) {
                progress = true;
            }
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
    count = 0;
    QLIST_FOREACH(node, &ctx->aio_handlers, node) {
        if (!node->deleted && node->io_notify) {
            events[count++] = event_notifier_get_handle(node->e);
        }
    }

    ctx->walking_handlers--;

    /* wait until next event */
    while (count > 0) {
        int ret;

        timeout = blocking ?
            qemu_timeout_ns_to_ms(timerlistgroup_deadline_ns(&ctx->tlg)) : 0;
        ret = WaitForMultipleObjects(count, events, FALSE, timeout);

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

                /* aio_notify() does not count as progress */
                if (node->e != &ctx->notifier) {
                    progress = true;
                }
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

    if (blocking) {
        /* Run the timers a second time. We do this because otherwise aio_wait
         * will not note progress - and will stop a drain early - if we have
         * a timer that was not ready to run entering g_poll but is ready
         * after g_poll. This will only do anything if a timer has expired.
         */
        progress |= timerlistgroup_run_timers(&ctx->tlg);
    }

    return progress;
}
