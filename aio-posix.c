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

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "block/block.h"
#include "qemu/queue.h"
#include "qemu/sockets.h"
#ifdef CONFIG_EPOLL
#include <sys/epoll.h>
#endif

struct AioHandler
{
    GPollFD pfd;
    IOHandler *io_read;
    IOHandler *io_write;
    int deleted;
    void *opaque;
    bool is_external;
    QLIST_ENTRY(AioHandler) node;
};

#ifdef CONFIG_EPOLL

/* The fd number threashold to switch to epoll */
#define EPOLL_ENABLE_THRESHOLD 64

static void aio_epoll_disable(AioContext *ctx)
{
    ctx->epoll_available = false;
    if (!ctx->epoll_enabled) {
        return;
    }
    ctx->epoll_enabled = false;
    close(ctx->epollfd);
}

static inline int epoll_events_from_pfd(int pfd_events)
{
    return (pfd_events & G_IO_IN ? EPOLLIN : 0) |
           (pfd_events & G_IO_OUT ? EPOLLOUT : 0) |
           (pfd_events & G_IO_HUP ? EPOLLHUP : 0) |
           (pfd_events & G_IO_ERR ? EPOLLERR : 0);
}

static bool aio_epoll_try_enable(AioContext *ctx)
{
    AioHandler *node;
    struct epoll_event event;

    QLIST_FOREACH(node, &ctx->aio_handlers, node) {
        int r;
        if (node->deleted || !node->pfd.events) {
            continue;
        }
        event.events = epoll_events_from_pfd(node->pfd.events);
        event.data.ptr = node;
        r = epoll_ctl(ctx->epollfd, EPOLL_CTL_ADD, node->pfd.fd, &event);
        if (r) {
            return false;
        }
    }
    ctx->epoll_enabled = true;
    return true;
}

static void aio_epoll_update(AioContext *ctx, AioHandler *node, bool is_new)
{
    struct epoll_event event;
    int r;

    if (!ctx->epoll_enabled) {
        return;
    }
    if (!node->pfd.events) {
        r = epoll_ctl(ctx->epollfd, EPOLL_CTL_DEL, node->pfd.fd, &event);
        if (r) {
            aio_epoll_disable(ctx);
        }
    } else {
        event.data.ptr = node;
        event.events = epoll_events_from_pfd(node->pfd.events);
        if (is_new) {
            r = epoll_ctl(ctx->epollfd, EPOLL_CTL_ADD, node->pfd.fd, &event);
            if (r) {
                aio_epoll_disable(ctx);
            }
        } else {
            r = epoll_ctl(ctx->epollfd, EPOLL_CTL_MOD, node->pfd.fd, &event);
            if (r) {
                aio_epoll_disable(ctx);
            }
        }
    }
}

static int aio_epoll(AioContext *ctx, GPollFD *pfds,
                     unsigned npfd, int64_t timeout)
{
    AioHandler *node;
    int i, ret = 0;
    struct epoll_event events[128];

    assert(npfd == 1);
    assert(pfds[0].fd == ctx->epollfd);
    if (timeout > 0) {
        ret = qemu_poll_ns(pfds, npfd, timeout);
    }
    if (timeout <= 0 || ret > 0) {
        ret = epoll_wait(ctx->epollfd, events,
                         sizeof(events) / sizeof(events[0]),
                         timeout);
        if (ret <= 0) {
            goto out;
        }
        for (i = 0; i < ret; i++) {
            int ev = events[i].events;
            node = events[i].data.ptr;
            node->pfd.revents = (ev & EPOLLIN ? G_IO_IN : 0) |
                (ev & EPOLLOUT ? G_IO_OUT : 0) |
                (ev & EPOLLHUP ? G_IO_HUP : 0) |
                (ev & EPOLLERR ? G_IO_ERR : 0);
        }
    }
out:
    return ret;
}

static bool aio_epoll_enabled(AioContext *ctx)
{
    /* Fall back to ppoll when external clients are disabled. */
    return !aio_external_disabled(ctx) && ctx->epoll_enabled;
}

static bool aio_epoll_check_poll(AioContext *ctx, GPollFD *pfds,
                                 unsigned npfd, int64_t timeout)
{
    if (!ctx->epoll_available) {
        return false;
    }
    if (aio_epoll_enabled(ctx)) {
        return true;
    }
    if (npfd >= EPOLL_ENABLE_THRESHOLD) {
        if (aio_epoll_try_enable(ctx)) {
            return true;
        } else {
            aio_epoll_disable(ctx);
        }
    }
    return false;
}

#else

static void aio_epoll_update(AioContext *ctx, AioHandler *node, bool is_new)
{
}

static int aio_epoll(AioContext *ctx, GPollFD *pfds,
                     unsigned npfd, int64_t timeout)
{
    assert(false);
}

static bool aio_epoll_enabled(AioContext *ctx)
{
    return false;
}

static bool aio_epoll_check_poll(AioContext *ctx, GPollFD *pfds,
                          unsigned npfd, int64_t timeout)
{
    return false;
}

#endif

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
                        bool is_external,
                        IOHandler *io_read,
                        IOHandler *io_write,
                        void *opaque)
{
    AioHandler *node;
    bool is_new = false;
    bool deleted = false;

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
                deleted = true;
            }
        }
    } else {
        if (node == NULL) {
            /* Alloc and insert if it's not already there */
            node = g_new0(AioHandler, 1);
            node->pfd.fd = fd;
            QLIST_INSERT_HEAD(&ctx->aio_handlers, node, node);

            g_source_add_poll(&ctx->source, &node->pfd);
            is_new = true;
        }
        /* Update handler with latest information */
        node->io_read = io_read;
        node->io_write = io_write;
        node->opaque = opaque;
        node->is_external = is_external;

        node->pfd.events = (io_read ? G_IO_IN | G_IO_HUP | G_IO_ERR : 0);
        node->pfd.events |= (io_write ? G_IO_OUT | G_IO_ERR : 0);
    }

    aio_epoll_update(ctx, node, is_new);
    aio_notify(ctx);
    if (deleted) {
        g_free(node);
    }
}

void aio_set_event_notifier(AioContext *ctx,
                            EventNotifier *notifier,
                            bool is_external,
                            EventNotifierHandler *io_read)
{
    aio_set_fd_handler(ctx, event_notifier_get_fd(notifier),
                       is_external, (IOHandler *)io_read, NULL, notifier);
}

bool aio_prepare(AioContext *ctx)
{
    return false;
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

bool aio_dispatch(AioContext *ctx)
{
    AioHandler *node;
    bool progress = false;

    /*
     * If there are callbacks left that have been queued, we need to call them.
     * Do not call select in this case, because it is possible that the caller
     * does not need a complete flush (as is the case for aio_poll loops).
     */
    if (aio_bh_poll(ctx)) {
        progress = true;
    }

    /*
     * We have to walk very carefully in case aio_set_fd_handler is
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

            /* aio_notify() does not count as progress */
            if (node->opaque != &ctx->notifier) {
                progress = true;
            }
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

    /* Run our timers */
    progress |= timerlistgroup_run_timers(&ctx->tlg);

    return progress;
}

/* These thread-local variables are used only in a small part of aio_poll
 * around the call to the poll() system call.  In particular they are not
 * used while aio_poll is performing callbacks, which makes it much easier
 * to think about reentrancy!
 *
 * Stack-allocated arrays would be perfect but they have size limitations;
 * heap allocation is expensive enough that we want to reuse arrays across
 * calls to aio_poll().  And because poll() has to be called without holding
 * any lock, the arrays cannot be stored in AioContext.  Thread-local data
 * has none of the disadvantages of these three options.
 */
static __thread GPollFD *pollfds;
static __thread AioHandler **nodes;
static __thread unsigned npfd, nalloc;
static __thread Notifier pollfds_cleanup_notifier;

static void pollfds_cleanup(Notifier *n, void *unused)
{
    g_assert(npfd == 0);
    g_free(pollfds);
    g_free(nodes);
    nalloc = 0;
}

static void add_pollfd(AioHandler *node)
{
    if (npfd == nalloc) {
        if (nalloc == 0) {
            pollfds_cleanup_notifier.notify = pollfds_cleanup;
            qemu_thread_atexit_add(&pollfds_cleanup_notifier);
            nalloc = 8;
        } else {
            g_assert(nalloc <= INT_MAX);
            nalloc *= 2;
        }
        pollfds = g_renew(GPollFD, pollfds, nalloc);
        nodes = g_renew(AioHandler *, nodes, nalloc);
    }
    nodes[npfd] = node;
    pollfds[npfd] = (GPollFD) {
        .fd = node->pfd.fd,
        .events = node->pfd.events,
    };
    npfd++;
}

bool aio_poll(AioContext *ctx, bool blocking)
{
    AioHandler *node;
    int i, ret;
    bool progress;
    int64_t timeout;

    aio_context_acquire(ctx);
    progress = false;

    /* aio_notify can avoid the expensive event_notifier_set if
     * everything (file descriptors, bottom halves, timers) will
     * be re-evaluated before the next blocking poll().  This is
     * already true when aio_poll is called with blocking == false;
     * if blocking == true, it is only true after poll() returns,
     * so disable the optimization now.
     */
    if (blocking) {
        atomic_add(&ctx->notify_me, 2);
    }

    ctx->walking_handlers++;

    assert(npfd == 0);

    /* fill pollfds */
    QLIST_FOREACH(node, &ctx->aio_handlers, node) {
        if (!node->deleted && node->pfd.events
            && !aio_epoll_enabled(ctx)
            && aio_node_check(ctx, node->is_external)) {
            add_pollfd(node);
        }
    }

    timeout = blocking ? aio_compute_timeout(ctx) : 0;

    /* wait until next event */
    if (timeout) {
        aio_context_release(ctx);
    }
    if (aio_epoll_check_poll(ctx, pollfds, npfd, timeout)) {
        AioHandler epoll_handler;

        epoll_handler.pfd.fd = ctx->epollfd;
        epoll_handler.pfd.events = G_IO_IN | G_IO_OUT | G_IO_HUP | G_IO_ERR;
        npfd = 0;
        add_pollfd(&epoll_handler);
        ret = aio_epoll(ctx, pollfds, npfd, timeout);
    } else  {
        ret = qemu_poll_ns(pollfds, npfd, timeout);
    }
    if (blocking) {
        atomic_sub(&ctx->notify_me, 2);
    }
    if (timeout) {
        aio_context_acquire(ctx);
    }

    aio_notify_accept(ctx);

    /* if we have any readable fds, dispatch event */
    if (ret > 0) {
        for (i = 0; i < npfd; i++) {
            nodes[i]->pfd.revents = pollfds[i].revents;
        }
    }

    npfd = 0;
    ctx->walking_handlers--;

    /* Run dispatch even if there were no readable fds to run timers */
    if (aio_dispatch(ctx)) {
        progress = true;
    }

    aio_context_release(ctx);

    return progress;
}

void aio_context_setup(AioContext *ctx, Error **errp)
{
#ifdef CONFIG_EPOLL
    assert(!ctx->epollfd);
    ctx->epollfd = epoll_create1(EPOLL_CLOEXEC);
    if (ctx->epollfd == -1) {
        ctx->epoll_available = false;
    } else {
        ctx->epoll_available = true;
    }
#endif
}
