/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * poll(2) file descriptor monitoring
 *
 * Uses ppoll(2) when available, g_poll() otherwise.
 */

#include "qemu/osdep.h"
#include "aio-posix.h"
#include "qemu/rcu_queue.h"

/*
 * These thread-local variables are used only in fdmon_poll_wait() around the
 * call to the poll() system call.  In particular they are not used while
 * aio_poll is performing callbacks, which makes it much easier to think about
 * reentrancy!
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

static int fdmon_poll_wait(AioContext *ctx, AioHandlerList *ready_list,
                            int64_t timeout)
{
    AioHandler *node;
    int ret;

    assert(npfd == 0);

    QLIST_FOREACH_RCU(node, &ctx->aio_handlers, node) {
        if (!QLIST_IS_INSERTED(node, node_deleted) && node->pfd.events
                && aio_node_check(ctx, node->is_external)) {
            add_pollfd(node);
        }
    }

    /* epoll(7) is faster above a certain number of fds */
    if (fdmon_epoll_try_upgrade(ctx, npfd)) {
        npfd = 0; /* we won't need pollfds[], reset npfd */
        return ctx->fdmon_ops->wait(ctx, ready_list, timeout);
    }

    ret = qemu_poll_ns(pollfds, npfd, timeout);
    if (ret > 0) {
        int i;

        for (i = 0; i < npfd; i++) {
            int revents = pollfds[i].revents;

            if (revents) {
                aio_add_ready_handler(ready_list, nodes[i], revents);
            }
        }
    }

    npfd = 0;
    return ret;
}

static void fdmon_poll_update(AioContext *ctx,
                              AioHandler *old_node,
                              AioHandler *new_node)
{
    /* Do nothing, AioHandler already contains the state we'll need */
}

const FDMonOps fdmon_poll_ops = {
    .update = fdmon_poll_update,
    .wait = fdmon_poll_wait,
    .need_wait = aio_poll_disabled,
};
