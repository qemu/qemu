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
    IOHandler *io_read;
    IOHandler *io_write;
    EventNotifierHandler *io_notify;
    GPollFD pfd;
    int deleted;
    void *opaque;
    QLIST_ENTRY(AioHandler) node;
};

void aio_set_fd_handler(AioContext *ctx,
                        int fd,
                        IOHandler *io_read,
                        IOHandler *io_write,
                        void *opaque)
{
    /* fd is a SOCKET in our case */
    AioHandler *node;

    QLIST_FOREACH(node, &ctx->aio_handlers, node) {
        if (node->pfd.fd == fd && !node->deleted) {
            break;
        }
    }

    /* Are we deleting the fd handler? */
    if (!io_read && !io_write) {
        if (node) {
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
        HANDLE event;

        if (node == NULL) {
            /* Alloc and insert if it's not already there */
            node = g_malloc0(sizeof(AioHandler));
            node->pfd.fd = fd;
            QLIST_INSERT_HEAD(&ctx->aio_handlers, node, node);
        }

        node->pfd.events = 0;
        if (node->io_read) {
            node->pfd.events |= G_IO_IN;
        }
        if (node->io_write) {
            node->pfd.events |= G_IO_OUT;
        }

        node->e = &ctx->notifier;

        /* Update handler with latest information */
        node->opaque = opaque;
        node->io_read = io_read;
        node->io_write = io_write;

        event = event_notifier_get_handle(&ctx->notifier);
        WSAEventSelect(node->pfd.fd, event,
                       FD_READ | FD_ACCEPT | FD_CLOSE |
                       FD_CONNECT | FD_WRITE | FD_OOB);
    }

    aio_notify(ctx);
}

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

bool aio_prepare(AioContext *ctx)
{
    static struct timeval tv0;
    AioHandler *node;
    bool have_select_revents = false;
    fd_set rfds, wfds;

    /* fill fd sets */
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    QLIST_FOREACH(node, &ctx->aio_handlers, node) {
        if (node->io_read) {
            FD_SET ((SOCKET)node->pfd.fd, &rfds);
        }
        if (node->io_write) {
            FD_SET ((SOCKET)node->pfd.fd, &wfds);
        }
    }

    if (select(0, &rfds, &wfds, NULL, &tv0) > 0) {
        QLIST_FOREACH(node, &ctx->aio_handlers, node) {
            node->pfd.revents = 0;
            if (FD_ISSET(node->pfd.fd, &rfds)) {
                node->pfd.revents |= G_IO_IN;
                have_select_revents = true;
            }

            if (FD_ISSET(node->pfd.fd, &wfds)) {
                node->pfd.revents |= G_IO_OUT;
                have_select_revents = true;
            }
        }
    }

    return have_select_revents;
}

bool aio_pending(AioContext *ctx)
{
    AioHandler *node;

    QLIST_FOREACH(node, &ctx->aio_handlers, node) {
        if (node->pfd.revents && node->io_notify) {
            return true;
        }

        if ((node->pfd.revents & G_IO_IN) && node->io_read) {
            return true;
        }
        if ((node->pfd.revents & G_IO_OUT) && node->io_write) {
            return true;
        }
    }

    return false;
}

static bool aio_dispatch_handlers(AioContext *ctx, HANDLE event)
{
    AioHandler *node;
    bool progress = false;

    /*
     * We have to walk very carefully in case aio_set_fd_handler is
     * called while we're walking.
     */
    node = QLIST_FIRST(&ctx->aio_handlers);
    while (node) {
        AioHandler *tmp;
        int revents = node->pfd.revents;

        ctx->walking_handlers++;

        if (!node->deleted &&
            (revents || event_notifier_get_handle(node->e) == event) &&
            node->io_notify) {
            node->pfd.revents = 0;
            node->io_notify(node->e);

            /* aio_notify() does not count as progress */
            if (node->e != &ctx->notifier) {
                progress = true;
            }
        }

        if (!node->deleted &&
            (node->io_read || node->io_write)) {
            node->pfd.revents = 0;
            if ((revents & G_IO_IN) && node->io_read) {
                node->io_read(node->opaque);
                progress = true;
            }
            if ((revents & G_IO_OUT) && node->io_write) {
                node->io_write(node->opaque);
                progress = true;
            }

            /* if the next select() will return an event, we have progressed */
            if (event == event_notifier_get_handle(&ctx->notifier)) {
                WSANETWORKEVENTS ev;
                WSAEnumNetworkEvents(node->pfd.fd, event, &ev);
                if (ev.lNetworkEvents) {
                    progress = true;
                }
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

    return progress;
}

bool aio_dispatch(AioContext *ctx)
{
    bool progress;

    progress = aio_bh_poll(ctx);
    progress |= aio_dispatch_handlers(ctx, INVALID_HANDLE_VALUE);
    progress |= timerlistgroup_run_timers(&ctx->tlg);
    return progress;
}

bool aio_poll(AioContext *ctx, bool blocking)
{
    AioHandler *node;
    HANDLE events[MAXIMUM_WAIT_OBJECTS + 1];
    bool was_dispatching, progress, have_select_revents, first;
    int count;
    int timeout;

    if (aio_prepare(ctx)) {
        blocking = false;
        have_select_revents = true;
    }

    was_dispatching = ctx->dispatching;
    progress = false;

    /* aio_notify can avoid the expensive event_notifier_set if
     * everything (file descriptors, bottom halves, timers) will
     * be re-evaluated before the next blocking poll().  This is
     * already true when aio_poll is called with blocking == false;
     * if blocking == true, it is only true after poll() returns.
     *
     * If we're in a nested event loop, ctx->dispatching might be true.
     * In that case we can restore it just before returning, but we
     * have to clear it now.
     */
    aio_set_dispatching(ctx, !blocking);

    ctx->walking_handlers++;

    /* fill fd sets */
    count = 0;
    QLIST_FOREACH(node, &ctx->aio_handlers, node) {
        if (!node->deleted && node->io_notify) {
            events[count++] = event_notifier_get_handle(node->e);
        }
    }

    ctx->walking_handlers--;
    first = true;

    /* wait until next event */
    while (count > 0) {
        HANDLE event;
        int ret;

        timeout = blocking
            ? qemu_timeout_ns_to_ms(aio_compute_timeout(ctx)) : 0;
        ret = WaitForMultipleObjects(count, events, FALSE, timeout);
        aio_set_dispatching(ctx, true);

        if (first && aio_bh_poll(ctx)) {
            progress = true;
        }
        first = false;

        /* if we have any signaled events, dispatch event */
        event = NULL;
        if ((DWORD) (ret - WAIT_OBJECT_0) < count) {
            event = events[ret - WAIT_OBJECT_0];
        } else if (!have_select_revents) {
            break;
        }

        have_select_revents = false;
        blocking = false;

        progress |= aio_dispatch_handlers(ctx, event);

        /* Try again, but only call each handler once.  */
        events[ret - WAIT_OBJECT_0] = events[--count];
    }

    progress |= timerlistgroup_run_timers(&ctx->tlg);

    aio_set_dispatching(ctx, was_dispatching);
    return progress;
}
