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
#include "block/block.h"
#include "qemu/rcu_queue.h"
#include "qemu/sockets.h"
#include "qemu/cutils.h"
#include "trace.h"
#ifdef CONFIG_EPOLL_CREATE1
#include <sys/epoll.h>
#endif

struct AioHandler
{
    GPollFD pfd;
    IOHandler *io_read;
    IOHandler *io_write;
    AioPollFn *io_poll;
    IOHandler *io_poll_begin;
    IOHandler *io_poll_end;
    int deleted;
    void *opaque;
    bool is_external;
    QLIST_ENTRY(AioHandler) node;
};

#ifdef CONFIG_EPOLL_CREATE1

/* The fd number threshold to switch to epoll */
#define EPOLL_ENABLE_THRESHOLD 64

static void aio_epoll_disable(AioContext *ctx)
{
    ctx->epoll_enabled = false;
    if (!ctx->epoll_available) {
        return;
    }
    ctx->epoll_available = false;
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

    QLIST_FOREACH_RCU(node, &ctx->aio_handlers, node) {
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
    int ctl;

    if (!ctx->epoll_enabled) {
        return;
    }
    if (!node->pfd.events) {
        ctl = EPOLL_CTL_DEL;
    } else {
        event.data.ptr = node;
        event.events = epoll_events_from_pfd(node->pfd.events);
        ctl = is_new ? EPOLL_CTL_ADD : EPOLL_CTL_MOD;
    }

    r = epoll_ctl(ctx->epollfd, ctl, node->pfd.fd, &event);
    if (r) {
        aio_epoll_disable(ctx);
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
                         ARRAY_SIZE(events),
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

static bool aio_remove_fd_handler(AioContext *ctx, AioHandler *node)
{
    /* If the GSource is in the process of being destroyed then
     * g_source_remove_poll() causes an assertion failure.  Skip
     * removal in that case, because glib cleans up its state during
     * destruction anyway.
     */
    if (!g_source_is_destroyed(&ctx->source)) {
        g_source_remove_poll(&ctx->source, &node->pfd);
    }

    /* If a read is in progress, just mark the node as deleted */
    if (qemu_lockcnt_count(&ctx->list_lock)) {
        node->deleted = 1;
        node->pfd.revents = 0;
        return false;
    }
    /* Otherwise, delete it for real.  We can't just mark it as
     * deleted because deleted nodes are only cleaned up while
     * no one is walking the handlers list.
     */
    QLIST_REMOVE(node, node);
    return true;
}

void aio_set_fd_handler(AioContext *ctx,
                        int fd,
                        bool is_external,
                        IOHandler *io_read,
                        IOHandler *io_write,
                        AioPollFn *io_poll,
                        void *opaque)
{
    AioHandler *node;
    AioHandler *new_node = NULL;
    bool is_new = false;
    bool deleted = false;
    int poll_disable_change;

    qemu_lockcnt_lock(&ctx->list_lock);

    node = find_aio_handler(ctx, fd);

    /* Are we deleting the fd handler? */
    if (!io_read && !io_write && !io_poll) {
        if (node == NULL) {
            qemu_lockcnt_unlock(&ctx->list_lock);
            return;
        }
        /* Clean events in order to unregister fd from the ctx epoll. */
        node->pfd.events = 0;

        poll_disable_change = -!node->io_poll;
    } else {
        poll_disable_change = !io_poll - (node && !node->io_poll);
        if (node == NULL) {
            is_new = true;
        }
        /* Alloc and insert if it's not already there */
        new_node = g_new0(AioHandler, 1);

        /* Update handler with latest information */
        new_node->io_read = io_read;
        new_node->io_write = io_write;
        new_node->io_poll = io_poll;
        new_node->opaque = opaque;
        new_node->is_external = is_external;

        if (is_new) {
            new_node->pfd.fd = fd;
        } else {
            new_node->pfd = node->pfd;
        }
        g_source_add_poll(&ctx->source, &new_node->pfd);

        new_node->pfd.events = (io_read ? G_IO_IN | G_IO_HUP | G_IO_ERR : 0);
        new_node->pfd.events |= (io_write ? G_IO_OUT | G_IO_ERR : 0);

        QLIST_INSERT_HEAD_RCU(&ctx->aio_handlers, new_node, node);
    }
    if (node) {
        deleted = aio_remove_fd_handler(ctx, node);
    }

    /* No need to order poll_disable_cnt writes against other updates;
     * the counter is only used to avoid wasting time and latency on
     * iterated polling when the system call will be ultimately necessary.
     * Changing handlers is a rare event, and a little wasted polling until
     * the aio_notify below is not an issue.
     */
    atomic_set(&ctx->poll_disable_cnt,
               atomic_read(&ctx->poll_disable_cnt) + poll_disable_change);

    if (new_node) {
        aio_epoll_update(ctx, new_node, is_new);
    } else if (node) {
        /* Unregister deleted fd_handler */
        aio_epoll_update(ctx, node, false);
    }
    qemu_lockcnt_unlock(&ctx->list_lock);
    aio_notify(ctx);

    if (deleted) {
        g_free(node);
    }
}

void aio_set_fd_poll(AioContext *ctx, int fd,
                     IOHandler *io_poll_begin,
                     IOHandler *io_poll_end)
{
    AioHandler *node = find_aio_handler(ctx, fd);

    if (!node) {
        return;
    }

    node->io_poll_begin = io_poll_begin;
    node->io_poll_end = io_poll_end;
}

void aio_set_event_notifier(AioContext *ctx,
                            EventNotifier *notifier,
                            bool is_external,
                            EventNotifierHandler *io_read,
                            AioPollFn *io_poll)
{
    aio_set_fd_handler(ctx, event_notifier_get_fd(notifier), is_external,
                       (IOHandler *)io_read, NULL, io_poll, notifier);
}

void aio_set_event_notifier_poll(AioContext *ctx,
                                 EventNotifier *notifier,
                                 EventNotifierHandler *io_poll_begin,
                                 EventNotifierHandler *io_poll_end)
{
    aio_set_fd_poll(ctx, event_notifier_get_fd(notifier),
                    (IOHandler *)io_poll_begin,
                    (IOHandler *)io_poll_end);
}

static void poll_set_started(AioContext *ctx, bool started)
{
    AioHandler *node;

    if (started == ctx->poll_started) {
        return;
    }

    ctx->poll_started = started;

    qemu_lockcnt_inc(&ctx->list_lock);
    QLIST_FOREACH_RCU(node, &ctx->aio_handlers, node) {
        IOHandler *fn;

        if (node->deleted) {
            continue;
        }

        if (started) {
            fn = node->io_poll_begin;
        } else {
            fn = node->io_poll_end;
        }

        if (fn) {
            fn(node->opaque);
        }
    }
    qemu_lockcnt_dec(&ctx->list_lock);
}


bool aio_prepare(AioContext *ctx)
{
    /* Poll mode cannot be used with glib's event loop, disable it. */
    poll_set_started(ctx, false);

    return false;
}

bool aio_pending(AioContext *ctx)
{
    AioHandler *node;
    bool result = false;

    /*
     * We have to walk very carefully in case aio_set_fd_handler is
     * called while we're walking.
     */
    qemu_lockcnt_inc(&ctx->list_lock);

    QLIST_FOREACH_RCU(node, &ctx->aio_handlers, node) {
        int revents;

        revents = node->pfd.revents & node->pfd.events;
        if (revents & (G_IO_IN | G_IO_HUP | G_IO_ERR) && node->io_read &&
            aio_node_check(ctx, node->is_external)) {
            result = true;
            break;
        }
        if (revents & (G_IO_OUT | G_IO_ERR) && node->io_write &&
            aio_node_check(ctx, node->is_external)) {
            result = true;
            break;
        }
    }
    qemu_lockcnt_dec(&ctx->list_lock);

    return result;
}

static bool aio_dispatch_handlers(AioContext *ctx)
{
    AioHandler *node, *tmp;
    bool progress = false;

    QLIST_FOREACH_SAFE_RCU(node, &ctx->aio_handlers, node, tmp) {
        int revents;

        revents = node->pfd.revents & node->pfd.events;
        node->pfd.revents = 0;

        if (!node->deleted &&
            (revents & (G_IO_IN | G_IO_HUP | G_IO_ERR)) &&
            aio_node_check(ctx, node->is_external) &&
            node->io_read) {
            node->io_read(node->opaque);

            /* aio_notify() does not count as progress */
            if (node->opaque != &ctx->notifier) {
                progress = true;
            }
        }
        if (!node->deleted &&
            (revents & (G_IO_OUT | G_IO_ERR)) &&
            aio_node_check(ctx, node->is_external) &&
            node->io_write) {
            node->io_write(node->opaque);
            progress = true;
        }

        if (node->deleted) {
            if (qemu_lockcnt_dec_if_lock(&ctx->list_lock)) {
                QLIST_REMOVE(node, node);
                g_free(node);
                qemu_lockcnt_inc_and_unlock(&ctx->list_lock);
            }
        }
    }

    return progress;
}

void aio_dispatch(AioContext *ctx)
{
    qemu_lockcnt_inc(&ctx->list_lock);
    aio_bh_poll(ctx);
    aio_dispatch_handlers(ctx);
    qemu_lockcnt_dec(&ctx->list_lock);

    timerlistgroup_run_timers(&ctx->tlg);
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

static bool run_poll_handlers_once(AioContext *ctx, int64_t *timeout)
{
    bool progress = false;
    AioHandler *node;

    QLIST_FOREACH_RCU(node, &ctx->aio_handlers, node) {
        if (!node->deleted && node->io_poll &&
            aio_node_check(ctx, node->is_external) &&
            node->io_poll(node->opaque)) {
            /*
             * Polling was successful, exit try_poll_mode immediately
             * to adjust the next polling time.
             */
            *timeout = 0;
            if (node->opaque != &ctx->notifier) {
                progress = true;
            }
        }

        /* Caller handles freeing deleted nodes.  Don't do it here. */
    }

    return progress;
}

/* run_poll_handlers:
 * @ctx: the AioContext
 * @max_ns: maximum time to poll for, in nanoseconds
 *
 * Polls for a given time.
 *
 * Note that ctx->notify_me must be non-zero so this function can detect
 * aio_notify().
 *
 * Note that the caller must have incremented ctx->list_lock.
 *
 * Returns: true if progress was made, false otherwise
 */
static bool run_poll_handlers(AioContext *ctx, int64_t max_ns, int64_t *timeout)
{
    bool progress;
    int64_t start_time, elapsed_time;

    assert(ctx->notify_me);
    assert(qemu_lockcnt_count(&ctx->list_lock) > 0);

    trace_run_poll_handlers_begin(ctx, max_ns, *timeout);

    start_time = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
    do {
        progress = run_poll_handlers_once(ctx, timeout);
        elapsed_time = qemu_clock_get_ns(QEMU_CLOCK_REALTIME) - start_time;
        max_ns = qemu_soonest_timeout(*timeout, max_ns);
        assert(!(max_ns && progress));
    } while (elapsed_time < max_ns && !atomic_read(&ctx->poll_disable_cnt));

    /* If time has passed with no successful polling, adjust *timeout to
     * keep the same ending time.
     */
    if (*timeout != -1) {
        *timeout -= MIN(*timeout, elapsed_time);
    }

    trace_run_poll_handlers_end(ctx, progress, *timeout);
    return progress;
}

/* try_poll_mode:
 * @ctx: the AioContext
 * @timeout: timeout for blocking wait, computed by the caller and updated if
 *    polling succeeds.
 *
 * ctx->notify_me must be non-zero so this function can detect aio_notify().
 *
 * Note that the caller must have incremented ctx->list_lock.
 *
 * Returns: true if progress was made, false otherwise
 */
static bool try_poll_mode(AioContext *ctx, int64_t *timeout)
{
    int64_t max_ns = qemu_soonest_timeout(*timeout, ctx->poll_ns);

    if (max_ns && !atomic_read(&ctx->poll_disable_cnt)) {
        poll_set_started(ctx, true);

        if (run_poll_handlers(ctx, max_ns, timeout)) {
            return true;
        }
    }

    poll_set_started(ctx, false);

    /* Even if we don't run busy polling, try polling once in case it can make
     * progress and the caller will be able to avoid ppoll(2)/epoll_wait(2).
     */
    return run_poll_handlers_once(ctx, timeout);
}

bool aio_poll(AioContext *ctx, bool blocking)
{
    AioHandler *node;
    int i;
    int ret = 0;
    bool progress;
    int64_t timeout;
    int64_t start = 0;

    assert(in_aio_context_home_thread(ctx));

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

    qemu_lockcnt_inc(&ctx->list_lock);

    if (ctx->poll_max_ns) {
        start = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
    }

    timeout = blocking ? aio_compute_timeout(ctx) : 0;
    progress = try_poll_mode(ctx, &timeout);
    assert(!(timeout && progress));

    /* If polling is allowed, non-blocking aio_poll does not need the
     * system call---a single round of run_poll_handlers_once suffices.
     */
    if (timeout || atomic_read(&ctx->poll_disable_cnt)) {
        assert(npfd == 0);

        /* fill pollfds */

        if (!aio_epoll_enabled(ctx)) {
            QLIST_FOREACH_RCU(node, &ctx->aio_handlers, node) {
                if (!node->deleted && node->pfd.events
                    && aio_node_check(ctx, node->is_external)) {
                    add_pollfd(node);
                }
            }
        }

        /* wait until next event */
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
    }

    if (blocking) {
        atomic_sub(&ctx->notify_me, 2);
        aio_notify_accept(ctx);
    }

    /* Adjust polling time */
    if (ctx->poll_max_ns) {
        int64_t block_ns = qemu_clock_get_ns(QEMU_CLOCK_REALTIME) - start;

        if (block_ns <= ctx->poll_ns) {
            /* This is the sweet spot, no adjustment needed */
        } else if (block_ns > ctx->poll_max_ns) {
            /* We'd have to poll for too long, poll less */
            int64_t old = ctx->poll_ns;

            if (ctx->poll_shrink) {
                ctx->poll_ns /= ctx->poll_shrink;
            } else {
                ctx->poll_ns = 0;
            }

            trace_poll_shrink(ctx, old, ctx->poll_ns);
        } else if (ctx->poll_ns < ctx->poll_max_ns &&
                   block_ns < ctx->poll_max_ns) {
            /* There is room to grow, poll longer */
            int64_t old = ctx->poll_ns;
            int64_t grow = ctx->poll_grow;

            if (grow == 0) {
                grow = 2;
            }

            if (ctx->poll_ns) {
                ctx->poll_ns *= grow;
            } else {
                ctx->poll_ns = 4000; /* start polling at 4 microseconds */
            }

            if (ctx->poll_ns > ctx->poll_max_ns) {
                ctx->poll_ns = ctx->poll_max_ns;
            }

            trace_poll_grow(ctx, old, ctx->poll_ns);
        }
    }

    /* if we have any readable fds, dispatch event */
    if (ret > 0) {
        for (i = 0; i < npfd; i++) {
            nodes[i]->pfd.revents = pollfds[i].revents;
        }
    }

    npfd = 0;

    progress |= aio_bh_poll(ctx);

    if (ret > 0) {
        progress |= aio_dispatch_handlers(ctx);
    }

    qemu_lockcnt_dec(&ctx->list_lock);

    progress |= timerlistgroup_run_timers(&ctx->tlg);

    return progress;
}

void aio_context_setup(AioContext *ctx)
{
#ifdef CONFIG_EPOLL_CREATE1
    assert(!ctx->epollfd);
    ctx->epollfd = epoll_create1(EPOLL_CLOEXEC);
    if (ctx->epollfd == -1) {
        fprintf(stderr, "Failed to create epoll instance: %s", strerror(errno));
        ctx->epoll_available = false;
    } else {
        ctx->epoll_available = true;
    }
#endif
}

void aio_context_destroy(AioContext *ctx)
{
#ifdef CONFIG_EPOLL_CREATE1
    aio_epoll_disable(ctx);
#endif
}

void aio_context_set_poll_params(AioContext *ctx, int64_t max_ns,
                                 int64_t grow, int64_t shrink, Error **errp)
{
    /* No thread synchronization here, it doesn't matter if an incorrect value
     * is used once.
     */
    ctx->poll_max_ns = max_ns;
    ctx->poll_ns = 0;
    ctx->poll_grow = grow;
    ctx->poll_shrink = shrink;

    aio_notify(ctx);
}
