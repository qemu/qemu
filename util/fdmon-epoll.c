/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * epoll(7) file descriptor monitoring
 */

#include "qemu/osdep.h"
#include <sys/epoll.h>
#include "qemu/rcu_queue.h"
#include "aio-posix.h"

/* The fd number threshold to switch to epoll */
#define EPOLL_ENABLE_THRESHOLD 64

void fdmon_epoll_disable(AioContext *ctx)
{
    if (ctx->epollfd >= 0) {
        close(ctx->epollfd);
        ctx->epollfd = -1;
    }

    /* Switch back */
    ctx->fdmon_ops = &fdmon_poll_ops;
}

static inline int epoll_events_from_pfd(int pfd_events)
{
    return (pfd_events & G_IO_IN ? EPOLLIN : 0) |
           (pfd_events & G_IO_OUT ? EPOLLOUT : 0) |
           (pfd_events & G_IO_HUP ? EPOLLHUP : 0) |
           (pfd_events & G_IO_ERR ? EPOLLERR : 0);
}

static void fdmon_epoll_update(AioContext *ctx,
                               AioHandler *old_node,
                               AioHandler *new_node)
{
    struct epoll_event event = {
        .data.ptr = new_node,
        .events = new_node ? epoll_events_from_pfd(new_node->pfd.events) : 0,
    };
    int r;

    if (!new_node) {
        r = epoll_ctl(ctx->epollfd, EPOLL_CTL_DEL, old_node->pfd.fd, &event);
    } else if (!old_node) {
        r = epoll_ctl(ctx->epollfd, EPOLL_CTL_ADD, new_node->pfd.fd, &event);
    } else {
        r = epoll_ctl(ctx->epollfd, EPOLL_CTL_MOD, new_node->pfd.fd, &event);
    }

    if (r) {
        fdmon_epoll_disable(ctx);
    }
}

static int fdmon_epoll_wait(AioContext *ctx, AioHandlerList *ready_list,
                            int64_t timeout)
{
    GPollFD pfd = {
        .fd = ctx->epollfd,
        .events = G_IO_IN | G_IO_OUT | G_IO_HUP | G_IO_ERR,
    };
    AioHandler *node;
    int i, ret = 0;
    struct epoll_event events[128];

    /* Fall back while external clients are disabled */
    if (qatomic_read(&ctx->external_disable_cnt)) {
        return fdmon_poll_ops.wait(ctx, ready_list, timeout);
    }

    if (timeout > 0) {
        ret = qemu_poll_ns(&pfd, 1, timeout);
        if (ret > 0) {
            timeout = 0;
        }
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
            int revents = (ev & EPOLLIN ? G_IO_IN : 0) |
                          (ev & EPOLLOUT ? G_IO_OUT : 0) |
                          (ev & EPOLLHUP ? G_IO_HUP : 0) |
                          (ev & EPOLLERR ? G_IO_ERR : 0);

            node = events[i].data.ptr;
            aio_add_ready_handler(ready_list, node, revents);
        }
    }
out:
    return ret;
}

static const FDMonOps fdmon_epoll_ops = {
    .update = fdmon_epoll_update,
    .wait = fdmon_epoll_wait,
    .need_wait = aio_poll_disabled,
};

static bool fdmon_epoll_try_enable(AioContext *ctx)
{
    AioHandler *node;
    struct epoll_event event;

    QLIST_FOREACH_RCU(node, &ctx->aio_handlers, node) {
        int r;
        if (QLIST_IS_INSERTED(node, node_deleted) || !node->pfd.events) {
            continue;
        }
        event.events = epoll_events_from_pfd(node->pfd.events);
        event.data.ptr = node;
        r = epoll_ctl(ctx->epollfd, EPOLL_CTL_ADD, node->pfd.fd, &event);
        if (r) {
            return false;
        }
    }

    ctx->fdmon_ops = &fdmon_epoll_ops;
    return true;
}

bool fdmon_epoll_try_upgrade(AioContext *ctx, unsigned npfd)
{
    if (ctx->epollfd < 0) {
        return false;
    }

    /* Do not upgrade while external clients are disabled */
    if (qatomic_read(&ctx->external_disable_cnt)) {
        return false;
    }

    if (npfd >= EPOLL_ENABLE_THRESHOLD) {
        if (fdmon_epoll_try_enable(ctx)) {
            return true;
        } else {
            fdmon_epoll_disable(ctx);
        }
    }
    return false;
}

void fdmon_epoll_setup(AioContext *ctx)
{
    ctx->epollfd = epoll_create1(EPOLL_CLOEXEC);
    if (ctx->epollfd == -1) {
        fprintf(stderr, "Failed to create epoll instance: %s", strerror(errno));
    }
}
