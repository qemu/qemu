/*
 * AioContext POSIX event loop implementation internal APIs
 *
 * Copyright IBM, Corp. 2008
 * Copyright Red Hat, Inc. 2020
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

#ifndef AIO_POSIX_H
#define AIO_POSIX_H

#include "block/aio.h"

struct AioHandler {
    GPollFD pfd;
    IOHandler *io_read;
    IOHandler *io_write;
    AioPollFn *io_poll;
    IOHandler *io_poll_ready;
    IOHandler *io_poll_begin;
    IOHandler *io_poll_end;
    void *opaque;
    QLIST_ENTRY(AioHandler) node;
    QLIST_ENTRY(AioHandler) node_ready; /* only used during aio_poll() */
    QLIST_ENTRY(AioHandler) node_deleted;
    QLIST_ENTRY(AioHandler) node_poll;
#ifdef CONFIG_LINUX_IO_URING
    QSLIST_ENTRY(AioHandler) node_submitted;
    unsigned flags; /* see fdmon-io_uring.c */
#endif
    int64_t poll_idle_timeout; /* when to stop userspace polling */
    bool poll_ready; /* has polling detected an event? */
    AioPolledEvent poll;
};

/* Add a handler to a ready list */
void aio_add_ready_handler(AioHandlerList *ready_list, AioHandler *node,
                           int revents);

extern const FDMonOps fdmon_poll_ops;

#ifdef CONFIG_EPOLL_CREATE1
bool fdmon_epoll_try_upgrade(AioContext *ctx, unsigned npfd);
void fdmon_epoll_setup(AioContext *ctx);
void fdmon_epoll_disable(AioContext *ctx);
#else
static inline bool fdmon_epoll_try_upgrade(AioContext *ctx, unsigned npfd)
{
    return false;
}

static inline void fdmon_epoll_setup(AioContext *ctx)
{
}

static inline void fdmon_epoll_disable(AioContext *ctx)
{
}
#endif /* !CONFIG_EPOLL_CREATE1 */

#ifdef CONFIG_LINUX_IO_URING
bool fdmon_io_uring_setup(AioContext *ctx);
void fdmon_io_uring_destroy(AioContext *ctx);
#else
static inline bool fdmon_io_uring_setup(AioContext *ctx)
{
    return false;
}

static inline void fdmon_io_uring_destroy(AioContext *ctx)
{
}
#endif /* !CONFIG_LINUX_IO_URING */

#endif /* AIO_POSIX_H */
