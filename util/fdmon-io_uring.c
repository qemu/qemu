/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Linux io_uring file descriptor monitoring
 *
 * The Linux io_uring API supports file descriptor monitoring with a few
 * advantages over existing APIs like poll(2) and epoll(7):
 *
 * 1. Userspace polling of events is possible because the completion queue (cq
 *    ring) is shared between the kernel and userspace.  This allows
 *    applications that rely on userspace polling to also monitor file
 *    descriptors in the same userspace polling loop.
 *
 * 2. Submission and completion is batched and done together in a single system
 *    call.  This minimizes the number of system calls.
 *
 * 3. File descriptor monitoring is O(1) like epoll(7) so it scales better than
 *    poll(2).
 *
 * 4. Nanosecond timeouts are supported so it requires fewer syscalls than
 *    epoll(7).
 *
 * This code only monitors file descriptors and does not do asynchronous disk
 * I/O.  Implementing disk I/O efficiently has other requirements and should
 * use a separate io_uring so it does not make sense to unify the code.
 *
 * File descriptor monitoring is implemented using the following operations:
 *
 * 1. IORING_OP_POLL_ADD - adds a file descriptor to be monitored.
 * 2. IORING_OP_POLL_REMOVE - removes a file descriptor being monitored.  When
 *    the poll mask changes for a file descriptor it is first removed and then
 *    re-added with the new poll mask, so this operation is also used as part
 *    of modifying an existing monitored file descriptor.
 * 3. IORING_OP_TIMEOUT - added every time a blocking syscall is made to wait
 *    for events.  This operation self-cancels if another event completes
 *    before the timeout.
 *
 * io_uring calls the submission queue the "sq ring" and the completion queue
 * the "cq ring".  Ring entries are called "sqe" and "cqe", respectively.
 *
 * The code is structured so that sq/cq rings are only modified within
 * fdmon_io_uring_wait().  Changes to AioHandlers are made by enqueuing them on
 * ctx->submit_list so that fdmon_io_uring_wait() can submit IORING_OP_POLL_ADD
 * and/or IORING_OP_POLL_REMOVE sqes for them.
 */

#include "qemu/osdep.h"
#include <poll.h>
#include "qapi/error.h"
#include "qemu/defer-call.h"
#include "qemu/rcu_queue.h"
#include "aio-posix.h"
#include "trace.h"

enum {
    FDMON_IO_URING_ENTRIES  = 128, /* sq/cq ring size */

    /* AioHandler::flags */
    FDMON_IO_URING_PENDING            = (1 << 0),
    FDMON_IO_URING_ADD                = (1 << 1),
    FDMON_IO_URING_REMOVE             = (1 << 2),
    FDMON_IO_URING_DELETE_AIO_HANDLER = (1 << 3),
};

static inline int poll_events_from_pfd(int pfd_events)
{
    return (pfd_events & G_IO_IN ? POLLIN : 0) |
           (pfd_events & G_IO_OUT ? POLLOUT : 0) |
           (pfd_events & G_IO_HUP ? POLLHUP : 0) |
           (pfd_events & G_IO_ERR ? POLLERR : 0);
}

static inline int pfd_events_from_poll(int poll_events)
{
    return (poll_events & POLLIN ? G_IO_IN : 0) |
           (poll_events & POLLOUT ? G_IO_OUT : 0) |
           (poll_events & POLLHUP ? G_IO_HUP : 0) |
           (poll_events & POLLERR ? G_IO_ERR : 0);
}

/*
 * Returns an sqe for submitting a request. Only called from the AioContext
 * thread.
 */
static struct io_uring_sqe *get_sqe(AioContext *ctx)
{
    struct io_uring *ring = &ctx->fdmon_io_uring;
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    int ret;

    if (likely(sqe)) {
        return sqe;
    }

    /* No free sqes left, submit pending sqes first */
    do {
        ret = io_uring_submit(ring);
    } while (ret == -EINTR);

    assert(ret > 1);
    sqe = io_uring_get_sqe(ring);
    assert(sqe);
    return sqe;
}

/* Atomically enqueue an AioHandler for sq ring submission */
static void enqueue(AioHandlerSList *head, AioHandler *node, unsigned flags)
{
    unsigned old_flags;

    old_flags = qatomic_fetch_or(&node->flags, FDMON_IO_URING_PENDING | flags);
    if (!(old_flags & FDMON_IO_URING_PENDING)) {
        QSLIST_INSERT_HEAD_ATOMIC(head, node, node_submitted);
    }
}

/* Dequeue an AioHandler for sq ring submission.  Called by fill_sq_ring(). */
static AioHandler *dequeue(AioHandlerSList *head, unsigned *flags)
{
    AioHandler *node = QSLIST_FIRST(head);

    if (!node) {
        return NULL;
    }

    /* Doesn't need to be atomic since fill_sq_ring() moves the list */
    QSLIST_REMOVE_HEAD(head, node_submitted);

    /*
     * Don't clear FDMON_IO_URING_REMOVE.  It's sticky so it can serve two
     * purposes: telling fill_sq_ring() to submit IORING_OP_POLL_REMOVE and
     * telling process_cqe() to delete the AioHandler when its
     * IORING_OP_POLL_ADD completes.
     */
    *flags = qatomic_fetch_and(&node->flags, ~(FDMON_IO_URING_PENDING |
                                              FDMON_IO_URING_ADD));
    return node;
}

static void fdmon_io_uring_update(AioContext *ctx,
                                  AioHandler *old_node,
                                  AioHandler *new_node)
{
    if (new_node) {
        enqueue(&ctx->submit_list, new_node, FDMON_IO_URING_ADD);
    }

    if (old_node) {
        /*
         * Deletion is tricky because IORING_OP_POLL_ADD and
         * IORING_OP_POLL_REMOVE are async.  We need to wait for the original
         * IORING_OP_POLL_ADD to complete before this handler can be freed
         * safely.
         *
         * It's possible that the file descriptor becomes ready and the
         * IORING_OP_POLL_ADD cqe is enqueued before IORING_OP_POLL_REMOVE is
         * submitted, too.
         *
         * Mark this handler deleted right now but don't place it on
         * ctx->deleted_aio_handlers yet.  Instead, manually fudge the list
         * entry to make QLIST_IS_INSERTED() think this handler has been
         * inserted and other code recognizes this AioHandler as deleted.
         *
         * Once the original IORING_OP_POLL_ADD completes we enqueue the
         * handler on the real ctx->deleted_aio_handlers list to be freed.
         */
        assert(!QLIST_IS_INSERTED(old_node, node_deleted));
        old_node->node_deleted.le_prev = &old_node->node_deleted.le_next;

        enqueue(&ctx->submit_list, old_node, FDMON_IO_URING_REMOVE);
    }
}

static void fdmon_io_uring_add_sqe(AioContext *ctx,
        void (*prep_sqe)(struct io_uring_sqe *sqe, void *opaque),
        void *opaque, CqeHandler *cqe_handler)
{
    struct io_uring_sqe *sqe = get_sqe(ctx);

    prep_sqe(sqe, opaque);
    io_uring_sqe_set_data(sqe, cqe_handler);

    trace_fdmon_io_uring_add_sqe(ctx, opaque, sqe->opcode, sqe->fd, sqe->off,
                                 cqe_handler);
}

static void fdmon_special_cqe_handler(CqeHandler *cqe_handler)
{
    /*
     * This is an empty function that is never called. It is used as a function
     * pointer to distinguish it from ordinary cqe handlers.
     */
}

static void add_poll_add_sqe(AioContext *ctx, AioHandler *node)
{
    struct io_uring_sqe *sqe = get_sqe(ctx);
    int events = poll_events_from_pfd(node->pfd.events);

    io_uring_prep_poll_add(sqe, node->pfd.fd, events);
    node->internal_cqe_handler.cb = fdmon_special_cqe_handler;
    io_uring_sqe_set_data(sqe, &node->internal_cqe_handler);
}

static void add_poll_remove_sqe(AioContext *ctx, AioHandler *node)
{
    struct io_uring_sqe *sqe = get_sqe(ctx);
    CqeHandler *cqe_handler = &node->internal_cqe_handler;

#ifdef LIBURING_HAVE_DATA64
    io_uring_prep_poll_remove(sqe, (uintptr_t)cqe_handler);
#else
    io_uring_prep_poll_remove(sqe, cqe_handler);
#endif
    io_uring_sqe_set_data(sqe, NULL);
}

/* Add sqes from ctx->submit_list for submission */
static void fill_sq_ring(AioContext *ctx)
{
    AioHandlerSList submit_list;
    AioHandler *node;
    unsigned flags;

    QSLIST_MOVE_ATOMIC(&submit_list, &ctx->submit_list);

    while ((node = dequeue(&submit_list, &flags))) {
        /* Order matters, just in case both flags were set */
        if (flags & FDMON_IO_URING_ADD) {
            add_poll_add_sqe(ctx, node);
        }
        if (flags & FDMON_IO_URING_REMOVE) {
            add_poll_remove_sqe(ctx, node);
        }
        if (flags & FDMON_IO_URING_DELETE_AIO_HANDLER) {
            /*
             * process_cqe() sets this flag after ADD and REMOVE have been
             * cleared. They cannot be set again, so they must be clear.
             */
            assert(!(flags & FDMON_IO_URING_ADD));
            assert(!(flags & FDMON_IO_URING_REMOVE));

            QLIST_INSERT_HEAD_RCU(&ctx->deleted_aio_handlers, node, node_deleted);
        }
    }
}

static bool process_cqe_aio_handler(AioContext *ctx,
                                    AioHandlerList *ready_list,
                                    AioHandler *node,
                                    struct io_uring_cqe *cqe)
{
    unsigned flags;

    /*
     * Deletion can only happen when IORING_OP_POLL_ADD completes.  If we race
     * with enqueue() here then we can safely clear the FDMON_IO_URING_REMOVE
     * bit before IORING_OP_POLL_REMOVE is submitted.
     */
    flags = qatomic_fetch_and(&node->flags, ~FDMON_IO_URING_REMOVE);
    if (flags & FDMON_IO_URING_REMOVE) {
        if (flags & FDMON_IO_URING_PENDING) {
            /* Still on ctx->submit_list, defer deletion until fill_sq_ring() */
            qatomic_or(&node->flags, FDMON_IO_URING_DELETE_AIO_HANDLER);
        } else {
            QLIST_INSERT_HEAD_RCU(&ctx->deleted_aio_handlers, node, node_deleted);
        }
        return false;
    }

    aio_add_ready_handler(ready_list, node, pfd_events_from_poll(cqe->res));

    /* IORING_OP_POLL_ADD is one-shot so we must re-arm it */
    add_poll_add_sqe(ctx, node);
    return true;
}

/* Returns true if a handler became ready */
static bool process_cqe(AioContext *ctx,
                        AioHandlerList *ready_list,
                        struct io_uring_cqe *cqe)
{
    CqeHandler *cqe_handler = io_uring_cqe_get_data(cqe);

    /* poll_timeout and poll_remove have a zero user_data field */
    if (!cqe_handler) {
        return false;
    }

    /*
     * Special handling for AioHandler cqes. They need ready_list and have a
     * return value.
     */
    if (cqe_handler->cb == fdmon_special_cqe_handler) {
        AioHandler *node = container_of(cqe_handler, AioHandler,
                                        internal_cqe_handler);
        return process_cqe_aio_handler(ctx, ready_list, node, cqe);
    }

    cqe_handler->cqe = *cqe;

    /* Handlers are invoked later by fdmon_io_uring_dispatch() */
    QSIMPLEQ_INSERT_TAIL(&ctx->cqe_handler_ready_list, cqe_handler, next);
    return false;
}

static int process_cq_ring(AioContext *ctx, AioHandlerList *ready_list)
{
    struct io_uring *ring = &ctx->fdmon_io_uring;
    struct io_uring_cqe *cqe;
    unsigned num_cqes = 0;
    unsigned num_ready = 0;
    unsigned head;

#ifdef HAVE_IO_URING_CQ_HAS_OVERFLOW
    /* If the CQ overflowed then fetch CQEs with a syscall */
    if (io_uring_cq_has_overflow(ring)) {
        io_uring_get_events(ring);
    }
#endif

    io_uring_for_each_cqe(ring, head, cqe) {
        if (process_cqe(ctx, ready_list, cqe)) {
            num_ready++;
        }

        num_cqes++;
    }

    io_uring_cq_advance(ring, num_cqes);
    return num_ready;
}

/* This is where SQEs are submitted in the glib event loop */
static void fdmon_io_uring_gsource_prepare(AioContext *ctx)
{
    fill_sq_ring(ctx);
    if (io_uring_sq_ready(&ctx->fdmon_io_uring)) {
        while (io_uring_submit(&ctx->fdmon_io_uring) == -EINTR) {
            /* Keep trying if syscall was interrupted */
        }
    }
}

static bool fdmon_io_uring_gsource_check(AioContext *ctx)
{
    gpointer tag = ctx->io_uring_fd_tag;
    return g_source_query_unix_fd(&ctx->source, tag) & G_IO_IN;
}

/* Dispatch CQE handlers that are ready */
static bool fdmon_io_uring_dispatch(AioContext *ctx)
{
    CqeHandlerSimpleQ *ready_list = &ctx->cqe_handler_ready_list;
    bool progress = false;

    /* Handlers may use defer_call() to coalesce frequent operations */
    defer_call_begin();

    while (!QSIMPLEQ_EMPTY(ready_list)) {
        CqeHandler *cqe_handler = QSIMPLEQ_FIRST(ready_list);

        QSIMPLEQ_REMOVE_HEAD(ready_list, next);

        trace_fdmon_io_uring_cqe_handler(ctx, cqe_handler,
                                         cqe_handler->cqe.res);
        cqe_handler->cb(cqe_handler);
        progress = true;
    }

    defer_call_end();

    return progress;
}


/* This is where CQEs are processed in the glib event loop */
static void fdmon_io_uring_gsource_dispatch(AioContext *ctx,
                                            AioHandlerList *ready_list)
{
    process_cq_ring(ctx, ready_list);
}

static int fdmon_io_uring_wait(AioContext *ctx, AioHandlerList *ready_list,
                               int64_t timeout)
{
    struct __kernel_timespec ts;
    unsigned wait_nr = 1; /* block until at least one cqe is ready */
    int ret;

    if (timeout == 0) {
        wait_nr = 0; /* non-blocking */
    } else if (timeout > 0) {
        /* Add a timeout that self-cancels when another cqe becomes ready */
        struct io_uring_sqe *sqe;

        ts = (struct __kernel_timespec){
            .tv_sec = timeout / NANOSECONDS_PER_SECOND,
            .tv_nsec = timeout % NANOSECONDS_PER_SECOND,
        };

        sqe = get_sqe(ctx);
        io_uring_prep_timeout(sqe, &ts, 1, 0);
        io_uring_sqe_set_data(sqe, NULL);
    }

    fill_sq_ring(ctx);

    /*
     * Loop to handle signals in both cases:
     * 1. If no SQEs were submitted, then -EINTR is returned.
     * 2. If SQEs were submitted then the number of SQEs submitted is returned
     *    rather than -EINTR.
     */
    do {
        ret = io_uring_submit_and_wait(&ctx->fdmon_io_uring, wait_nr);
    } while (ret == -EINTR ||
             (ret >= 0 && wait_nr > io_uring_cq_ready(&ctx->fdmon_io_uring)));

    assert(ret >= 0);

    return process_cq_ring(ctx, ready_list);
}

static bool fdmon_io_uring_need_wait(AioContext *ctx)
{
    /* Have io_uring events completed? */
    if (io_uring_cq_ready(&ctx->fdmon_io_uring)) {
        return true;
    }

    /* Are there pending sqes to submit? */
    if (io_uring_sq_ready(&ctx->fdmon_io_uring)) {
        return true;
    }

    /* Do we need to process AioHandlers for io_uring changes? */
    if (!QSLIST_EMPTY_RCU(&ctx->submit_list)) {
        return true;
    }

    return false;
}

static const FDMonOps fdmon_io_uring_ops = {
    .update = fdmon_io_uring_update,
    .wait = fdmon_io_uring_wait,
    .need_wait = fdmon_io_uring_need_wait,
    .dispatch = fdmon_io_uring_dispatch,
    .gsource_prepare = fdmon_io_uring_gsource_prepare,
    .gsource_check = fdmon_io_uring_gsource_check,
    .gsource_dispatch = fdmon_io_uring_gsource_dispatch,
    .add_sqe = fdmon_io_uring_add_sqe,
};

bool fdmon_io_uring_setup(AioContext *ctx, Error **errp)
{
    int ret;

    ctx->io_uring_fd_tag = NULL;

    ret = io_uring_queue_init(FDMON_IO_URING_ENTRIES, &ctx->fdmon_io_uring, 0);
    if (ret != 0) {
        error_setg_errno(errp, -ret, "Failed to initialize io_uring");
        return false;
    }

    QSLIST_INIT(&ctx->submit_list);
    QSIMPLEQ_INIT(&ctx->cqe_handler_ready_list);
    ctx->fdmon_ops = &fdmon_io_uring_ops;
    ctx->io_uring_fd_tag = g_source_add_unix_fd(&ctx->source,
            ctx->fdmon_io_uring.ring_fd, G_IO_IN);
    return true;
}

void fdmon_io_uring_destroy(AioContext *ctx)
{
    AioHandler *node;

    if (ctx->fdmon_ops != &fdmon_io_uring_ops) {
        return;
    }

    io_uring_queue_exit(&ctx->fdmon_io_uring);

    /* Move handlers due to be removed onto the deleted list */
    while ((node = QSLIST_FIRST_RCU(&ctx->submit_list))) {
        unsigned flags = qatomic_fetch_and(&node->flags,
                ~(FDMON_IO_URING_PENDING |
                  FDMON_IO_URING_ADD |
                  FDMON_IO_URING_REMOVE |
                  FDMON_IO_URING_DELETE_AIO_HANDLER));

        if ((flags & FDMON_IO_URING_REMOVE) ||
            (flags & FDMON_IO_URING_DELETE_AIO_HANDLER)) {
            QLIST_INSERT_HEAD_RCU(&ctx->deleted_aio_handlers,
                                  node, node_deleted);
        }

        QSLIST_REMOVE_HEAD_RCU(&ctx->submit_list, node_submitted);
    }

    g_source_remove_unix_fd(&ctx->source, ctx->io_uring_fd_tag);
    ctx->io_uring_fd_tag = NULL;

    assert(QSIMPLEQ_EMPTY(&ctx->cqe_handler_ready_list));

    qemu_lockcnt_lock(&ctx->list_lock);
    fdmon_poll_downgrade(ctx);
    qemu_lockcnt_unlock(&ctx->list_lock);
}
