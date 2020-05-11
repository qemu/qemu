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
#include "qemu/rcu.h"
#include "qemu/rcu_queue.h"
#include "qemu/sockets.h"
#include "qemu/cutils.h"
#include "trace.h"
#include "aio-posix.h"

/* Stop userspace polling on a handler if it isn't active for some time */
#define POLL_IDLE_INTERVAL_NS (7 * NANOSECONDS_PER_SECOND)

bool aio_poll_disabled(AioContext *ctx)
{
    return atomic_read(&ctx->poll_disable_cnt);
}

void aio_add_ready_handler(AioHandlerList *ready_list,
                           AioHandler *node,
                           int revents)
{
    QLIST_SAFE_REMOVE(node, node_ready); /* remove from nested parent's list */
    node->pfd.revents = revents;
    QLIST_INSERT_HEAD(ready_list, node, node_ready);
}

static AioHandler *find_aio_handler(AioContext *ctx, int fd)
{
    AioHandler *node;

    QLIST_FOREACH(node, &ctx->aio_handlers, node) {
        if (node->pfd.fd == fd) {
            if (!QLIST_IS_INSERTED(node, node_deleted)) {
                return node;
            }
        }
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

    node->pfd.revents = 0;

    /* If the fd monitor has already marked it deleted, leave it alone */
    if (QLIST_IS_INSERTED(node, node_deleted)) {
        return false;
    }

    /* If a read is in progress, just mark the node as deleted */
    if (qemu_lockcnt_count(&ctx->list_lock)) {
        QLIST_INSERT_HEAD_RCU(&ctx->deleted_aio_handlers, node, node_deleted);
        return false;
    }
    /* Otherwise, delete it for real.  We can't just mark it as
     * deleted because deleted nodes are only cleaned up while
     * no one is walking the handlers list.
     */
    QLIST_SAFE_REMOVE(node, node_poll);
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

    /* No need to order poll_disable_cnt writes against other updates;
     * the counter is only used to avoid wasting time and latency on
     * iterated polling when the system call will be ultimately necessary.
     * Changing handlers is a rare event, and a little wasted polling until
     * the aio_notify below is not an issue.
     */
    atomic_set(&ctx->poll_disable_cnt,
               atomic_read(&ctx->poll_disable_cnt) + poll_disable_change);

    ctx->fdmon_ops->update(ctx, node, new_node);
    if (node) {
        deleted = aio_remove_fd_handler(ctx, node);
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

static bool poll_set_started(AioContext *ctx, bool started)
{
    AioHandler *node;
    bool progress = false;

    if (started == ctx->poll_started) {
        return false;
    }

    ctx->poll_started = started;

    qemu_lockcnt_inc(&ctx->list_lock);
    QLIST_FOREACH(node, &ctx->poll_aio_handlers, node_poll) {
        IOHandler *fn;

        if (QLIST_IS_INSERTED(node, node_deleted)) {
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

        /* Poll one last time in case ->io_poll_end() raced with the event */
        if (!started) {
            progress = node->io_poll(node->opaque) || progress;
        }
    }
    qemu_lockcnt_dec(&ctx->list_lock);

    return progress;
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

static void aio_free_deleted_handlers(AioContext *ctx)
{
    AioHandler *node;

    if (QLIST_EMPTY_RCU(&ctx->deleted_aio_handlers)) {
        return;
    }
    if (!qemu_lockcnt_dec_if_lock(&ctx->list_lock)) {
        return; /* we are nested, let the parent do the freeing */
    }

    while ((node = QLIST_FIRST_RCU(&ctx->deleted_aio_handlers))) {
        QLIST_REMOVE(node, node);
        QLIST_REMOVE(node, node_deleted);
        QLIST_SAFE_REMOVE(node, node_poll);
        g_free(node);
    }

    qemu_lockcnt_inc_and_unlock(&ctx->list_lock);
}

static bool aio_dispatch_handler(AioContext *ctx, AioHandler *node)
{
    bool progress = false;
    int revents;

    revents = node->pfd.revents & node->pfd.events;
    node->pfd.revents = 0;

    /*
     * Start polling AioHandlers when they become ready because activity is
     * likely to continue.  Note that starvation is theoretically possible when
     * fdmon_supports_polling(), but only until the fd fires for the first
     * time.
     */
    if (!QLIST_IS_INSERTED(node, node_deleted) &&
        !QLIST_IS_INSERTED(node, node_poll) &&
        node->io_poll) {
        trace_poll_add(ctx, node, node->pfd.fd, revents);
        if (ctx->poll_started && node->io_poll_begin) {
            node->io_poll_begin(node->opaque);
        }
        QLIST_INSERT_HEAD(&ctx->poll_aio_handlers, node, node_poll);
    }

    if (!QLIST_IS_INSERTED(node, node_deleted) &&
        (revents & (G_IO_IN | G_IO_HUP | G_IO_ERR)) &&
        aio_node_check(ctx, node->is_external) &&
        node->io_read) {
        node->io_read(node->opaque);

        /* aio_notify() does not count as progress */
        if (node->opaque != &ctx->notifier) {
            progress = true;
        }
    }
    if (!QLIST_IS_INSERTED(node, node_deleted) &&
        (revents & (G_IO_OUT | G_IO_ERR)) &&
        aio_node_check(ctx, node->is_external) &&
        node->io_write) {
        node->io_write(node->opaque);
        progress = true;
    }

    return progress;
}

/*
 * If we have a list of ready handlers then this is more efficient than
 * scanning all handlers with aio_dispatch_handlers().
 */
static bool aio_dispatch_ready_handlers(AioContext *ctx,
                                        AioHandlerList *ready_list)
{
    bool progress = false;
    AioHandler *node;

    while ((node = QLIST_FIRST(ready_list))) {
        QLIST_REMOVE(node, node_ready);
        progress = aio_dispatch_handler(ctx, node) || progress;
    }

    return progress;
}

/* Slower than aio_dispatch_ready_handlers() but only used via glib */
static bool aio_dispatch_handlers(AioContext *ctx)
{
    AioHandler *node, *tmp;
    bool progress = false;

    QLIST_FOREACH_SAFE_RCU(node, &ctx->aio_handlers, node, tmp) {
        progress = aio_dispatch_handler(ctx, node) || progress;
    }

    return progress;
}

void aio_dispatch(AioContext *ctx)
{
    qemu_lockcnt_inc(&ctx->list_lock);
    aio_bh_poll(ctx);
    aio_dispatch_handlers(ctx);
    aio_free_deleted_handlers(ctx);
    qemu_lockcnt_dec(&ctx->list_lock);

    timerlistgroup_run_timers(&ctx->tlg);
}

static bool run_poll_handlers_once(AioContext *ctx,
                                   int64_t now,
                                   int64_t *timeout)
{
    bool progress = false;
    AioHandler *node;
    AioHandler *tmp;

    QLIST_FOREACH_SAFE(node, &ctx->poll_aio_handlers, node_poll, tmp) {
        if (aio_node_check(ctx, node->is_external) &&
            node->io_poll(node->opaque)) {
            node->poll_idle_timeout = now + POLL_IDLE_INTERVAL_NS;

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

static bool fdmon_supports_polling(AioContext *ctx)
{
    return ctx->fdmon_ops->need_wait != aio_poll_disabled;
}

static bool remove_idle_poll_handlers(AioContext *ctx, int64_t now)
{
    AioHandler *node;
    AioHandler *tmp;
    bool progress = false;

    /*
     * File descriptor monitoring implementations without userspace polling
     * support suffer from starvation when a subset of handlers is polled
     * because fds will not be processed in a timely fashion.  Don't remove
     * idle poll handlers.
     */
    if (!fdmon_supports_polling(ctx)) {
        return false;
    }

    QLIST_FOREACH_SAFE(node, &ctx->poll_aio_handlers, node_poll, tmp) {
        if (node->poll_idle_timeout == 0LL) {
            node->poll_idle_timeout = now + POLL_IDLE_INTERVAL_NS;
        } else if (now >= node->poll_idle_timeout) {
            trace_poll_remove(ctx, node, node->pfd.fd);
            node->poll_idle_timeout = 0LL;
            QLIST_SAFE_REMOVE(node, node_poll);
            if (ctx->poll_started && node->io_poll_end) {
                node->io_poll_end(node->opaque);

                /*
                 * Final poll in case ->io_poll_end() races with an event.
                 * Nevermind about re-adding the handler in the rare case where
                 * this causes progress.
                 */
                progress = node->io_poll(node->opaque) || progress;
            }
        }
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

    /*
     * Optimization: ->io_poll() handlers often contain RCU read critical
     * sections and we therefore see many rcu_read_lock() -> rcu_read_unlock()
     * -> rcu_read_lock() -> ... sequences with expensive memory
     * synchronization primitives.  Make the entire polling loop an RCU
     * critical section because nested rcu_read_lock()/rcu_read_unlock() calls
     * are cheap.
     */
    RCU_READ_LOCK_GUARD();

    start_time = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
    do {
        progress = run_poll_handlers_once(ctx, start_time, timeout);
        elapsed_time = qemu_clock_get_ns(QEMU_CLOCK_REALTIME) - start_time;
        max_ns = qemu_soonest_timeout(*timeout, max_ns);
        assert(!(max_ns && progress));
    } while (elapsed_time < max_ns && !ctx->fdmon_ops->need_wait(ctx));

    if (remove_idle_poll_handlers(ctx, start_time + elapsed_time)) {
        *timeout = 0;
        progress = true;
    }

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
    int64_t max_ns;

    if (QLIST_EMPTY_RCU(&ctx->poll_aio_handlers)) {
        return false;
    }

    max_ns = qemu_soonest_timeout(*timeout, ctx->poll_ns);
    if (max_ns && !ctx->fdmon_ops->need_wait(ctx)) {
        poll_set_started(ctx, true);

        if (run_poll_handlers(ctx, max_ns, timeout)) {
            return true;
        }
    }

    if (poll_set_started(ctx, false)) {
        *timeout = 0;
        return true;
    }

    return false;
}

bool aio_poll(AioContext *ctx, bool blocking)
{
    AioHandlerList ready_list = QLIST_HEAD_INITIALIZER(ready_list);
    int ret = 0;
    bool progress;
    int64_t timeout;
    int64_t start = 0;

    /*
     * There cannot be two concurrent aio_poll calls for the same AioContext (or
     * an aio_poll concurrent with a GSource prepare/check/dispatch callback).
     * We rely on this below to avoid slow locked accesses to ctx->notify_me.
     */
    assert(in_aio_context_home_thread(ctx));

    /* aio_notify can avoid the expensive event_notifier_set if
     * everything (file descriptors, bottom halves, timers) will
     * be re-evaluated before the next blocking poll().  This is
     * already true when aio_poll is called with blocking == false;
     * if blocking == true, it is only true after poll() returns,
     * so disable the optimization now.
     */
    if (blocking) {
        atomic_set(&ctx->notify_me, atomic_read(&ctx->notify_me) + 2);
        /*
         * Write ctx->notify_me before computing the timeout
         * (reading bottom half flags, etc.).  Pairs with
         * smp_mb in aio_notify().
         */
        smp_mb();
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
    if (timeout || ctx->fdmon_ops->need_wait(ctx)) {
        ret = ctx->fdmon_ops->wait(ctx, &ready_list, timeout);
    }

    if (blocking) {
        /* Finish the poll before clearing the flag.  */
        atomic_store_release(&ctx->notify_me, atomic_read(&ctx->notify_me) - 2);
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

    progress |= aio_bh_poll(ctx);

    if (ret > 0) {
        progress |= aio_dispatch_ready_handlers(ctx, &ready_list);
    }

    aio_free_deleted_handlers(ctx);

    qemu_lockcnt_dec(&ctx->list_lock);

    progress |= timerlistgroup_run_timers(&ctx->tlg);

    return progress;
}

void aio_context_setup(AioContext *ctx)
{
    ctx->fdmon_ops = &fdmon_poll_ops;
    ctx->epollfd = -1;

    /* Use the fastest fd monitoring implementation if available */
    if (fdmon_io_uring_setup(ctx)) {
        return;
    }

    fdmon_epoll_setup(ctx);
}

void aio_context_destroy(AioContext *ctx)
{
    fdmon_io_uring_destroy(ctx);
    fdmon_epoll_disable(ctx);
    aio_free_deleted_handlers(ctx);
}

void aio_context_use_g_source(AioContext *ctx)
{
    /*
     * Disable io_uring when the glib main loop is used because it doesn't
     * support mixed glib/aio_poll() usage. It relies on aio_poll() being
     * called regularly so that changes to the monitored file descriptors are
     * submitted, otherwise a list of pending fd handlers builds up.
     */
    fdmon_io_uring_destroy(ctx);
    aio_free_deleted_handlers(ctx);
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
