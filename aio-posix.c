/*
 * QEMU aio implementation
 *
 * Copyright IBM, Corp. 2008
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
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

struct AioHandler
{
    GPollFD pfd;
    IOHandler *io_read;
    IOHandler *io_write;
    AioFlushHandler *io_flush;
    int deleted;
    int pollfds_idx;
    void *opaque;
    QLIST_ENTRY(AioHandler) node;
};

static AioHandler *find_aio_handler(AioContext *ctx, int fd)
{
    AioHandler *node;

    QLIST_FOREACH(node, &ctx->aio_handlers, node) {
        if (node->pfd.fd == fd)
            if (!node->deleted)
                return node;
    }

    return NULL;
}

void aio_set_fd_handler(AioContext *ctx,
                        int fd,
                        IOHandler *io_read,
                        IOHandler *io_write,
                        AioFlushHandler *io_flush,
                        void *opaque)
{
    AioHandler *node;

    node = find_aio_handler(ctx, fd);

    /* Are we deleting the fd handler? */
    if (!io_read && !io_write) {
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
            node->pfd.fd = fd;
            QLIST_INSERT_HEAD(&ctx->aio_handlers, node, node);

            g_source_add_poll(&ctx->source, &node->pfd);
        }
        /* Update handler with latest information */
        node->io_read = io_read;
        node->io_write = io_write;
        node->io_flush = io_flush;
        node->opaque = opaque;
        node->pollfds_idx = -1;

        node->pfd.events = (io_read ? G_IO_IN | G_IO_HUP | G_IO_ERR : 0);
        node->pfd.events |= (io_write ? G_IO_OUT | G_IO_ERR : 0);
    }

    aio_notify(ctx);
}

void aio_set_event_notifier(AioContext *ctx,
                            EventNotifier *notifier,
                            EventNotifierHandler *io_read,
                            AioFlushEventNotifierHandler *io_flush)
{
    aio_set_fd_handler(ctx, event_notifier_get_fd(notifier),
                       (IOHandler *)io_read, NULL,
                       (AioFlushHandler *)io_flush, notifier);
}

bool aio_pending(AioContext *ctx)
{
    AioHandler *node;

    QLIST_FOREACH(node, &ctx->aio_handlers, node) {
        int revents;

        revents = node->pfd.revents & node->pfd.events;
        if (revents & (G_IO_IN | G_IO_HUP | G_IO_ERR) && node->io_read) {
            return true;
        }
        if (revents & (G_IO_OUT | G_IO_ERR) && node->io_write) {
            return true;
        }
    }

    return false;
}

static bool aio_dispatch(AioContext *ctx)
{
    AioHandler *node;
    bool progress = false;

    /*
     * We have to walk very carefully in case qemu_aio_set_fd_handler is
     * called while we're walking.
     */
    node = QLIST_FIRST(&ctx->aio_handlers);
    while (node) {
        AioHandler *tmp;
        int revents;

        ctx->walking_handlers++;

        revents = node->pfd.revents & node->pfd.events;
        node->pfd.revents = 0;

        if (!node->deleted &&
            (revents & (G_IO_IN | G_IO_HUP | G_IO_ERR)) &&
            node->io_read) {
            node->io_read(node->opaque);
            progress = true;
        }
        if (!node->deleted &&
            (revents & (G_IO_OUT | G_IO_ERR)) &&
            node->io_write) {
            node->io_write(node->opaque);
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
    return progress;
}

bool aio_poll(AioContext *ctx, bool blocking)
{
    AioHandler *node;
    int ret;
    bool busy, progress;

    progress = false;

    /*
     * If there are callbacks left that have been queued, we need to call them.
     * Do not call select in this case, because it is possible that the caller
     * does not need a complete flush (as is the case for qemu_aio_wait loops).
     */
    if (aio_bh_poll(ctx)) {
        blocking = false;
        progress = true;
    }

    if (aio_dispatch(ctx)) {
        progress = true;
    }

    if (progress && !blocking) {
        return true;
    }

    ctx->walking_handlers++;

    g_array_set_size(ctx->pollfds, 0);

    /* fill pollfds */
    busy = false;
    QLIST_FOREACH(node, &ctx->aio_handlers, node) {
        node->pollfds_idx = -1;

        /* If there aren't pending AIO operations, don't invoke callbacks.
         * Otherwise, if there are no AIO requests, qemu_aio_wait() would
         * wait indefinitely.
         */
        if (!node->deleted && node->io_flush) {
            if (node->io_flush(node->opaque) == 0) {
                continue;
            }
            busy = true;
        }
        if (!node->deleted && node->pfd.events) {
            GPollFD pfd = {
                .fd = node->pfd.fd,
                .events = node->pfd.events,
            };
            node->pollfds_idx = ctx->pollfds->len;
            g_array_append_val(ctx->pollfds, pfd);
        }
    }

    ctx->walking_handlers--;

    /* No AIO operations?  Get us out of here */
    if (!busy) {
        return progress;
    }

    /* wait until next event */
    ret = g_poll((GPollFD *)ctx->pollfds->data,
                 ctx->pollfds->len,
                 blocking ? -1 : 0);

    /* if we have any readable fds, dispatch event */
    if (ret > 0) {
        QLIST_FOREACH(node, &ctx->aio_handlers, node) {
            if (node->pollfds_idx != -1) {
                GPollFD *pfd = &g_array_index(ctx->pollfds, GPollFD,
                                              node->pollfds_idx);
                node->pfd.revents = pfd->revents;
            }
        }
        if (aio_dispatch(ctx)) {
            progress = true;
        }
    }

    assert(progress || busy);
    return true;
}
