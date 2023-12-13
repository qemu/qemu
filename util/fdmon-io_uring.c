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
#include "qemu/rcu_queue.h"
#include "aio-posix.h"

enum {
    FDMON_IO_URING_ENTRIES  = 128, /* sq/cq ring size */

    /* AioHandler::flags */
    FDMON_IO_URING_PENDING  = (1 << 0),
    FDMON_IO_URING_ADD      = (1 << 1),
    FDMON_IO_URING_REMOVE   = (1 << 2),
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
 * Returns an sqe for submitting a request.  Only be called within
 * fdmon_io_uring_wait().
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

static void add_poll_add_sqe(AioContext *ctx, AioHandler *node)
{
    struct io_uring_sqe *sqe = get_sqe(ctx);
    int events = poll_events_from_pfd(node->pfd.events);

    io_uring_prep_poll_add(sqe, node->pfd.fd, events);
    io_uring_sqe_set_data(sqe, node);
}

static void add_poll_remove_sqe(AioContext *ctx, AioHandler *node)
{
    struct io_uring_sqe *sqe = get_sqe(ctx);

#ifdef LIBURING_HAVE_DATA64
    io_uring_prep_poll_remove(sqe, (uintptr_t)node);
#else
    io_uring_prep_poll_remove(sqe, node);
#endif
    io_uring_sqe_set_data(sqe, NULL);
}

/* Add a timeout that self-cancels when another cqe becomes ready */
static void add_timeout_sqe(AioContext *ctx, int64_t ns)
{
    struct io_uring_sqe *sqe;
    struct __kernel_timespec ts = {
        .tv_sec = ns / NANOSECONDS_PER_SECOND,
        .tv_nsec = ns % NANOSECONDS_PER_SECOND,
    };

    sqe = get_sqe(ctx);
    io_uring_prep_timeout(sqe, &ts, 1, 0);
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
    }
}

/* Returns true if a handler became ready */
static bool process_cqe(AioContext *ctx,
                        AioHandlerList *ready_list,
                        struct io_uring_cqe *cqe)
{
    AioHandler *node = io_uring_cqe_get_data(cqe);
    unsigned flags;

    /* poll_timeout and poll_remove have a zero user_data field */
    if (!node) {
        return false;
    }

    /*
     * Deletion can only happen when IORING_OP_POLL_ADD completes.  If we race
     * with enqueue() here then we can safely clear the FDMON_IO_URING_REMOVE
     * bit before IORING_OP_POLL_REMOVE is submitted.
     */
    flags = qatomic_fetch_and(&node->flags, ~FDMON_IO_URING_REMOVE);
    if (flags & FDMON_IO_URING_REMOVE) {
        QLIST_INSERT_HEAD_RCU(&ctx->deleted_aio_handlers, node, node_deleted);
        return false;
    }

    aio_add_ready_handler(ready_list, node, pfd_events_from_poll(cqe->res));

    /* IORING_OP_POLL_ADD is one-shot so we must re-arm it */
    add_poll_add_sqe(ctx, node);
    return true;
}

static int process_cq_ring(AioContext *ctx, AioHandlerList *ready_list)
{
    struct io_uring *ring = &ctx->fdmon_io_uring;
    struct io_uring_cqe *cqe;
    unsigned num_cqes = 0;
    unsigned num_ready = 0;
    unsigned head;

    io_uring_for_each_cqe(ring, head, cqe) {
        if (process_cqe(ctx, ready_list, cqe)) {
            num_ready++;
        }

        num_cqes++;
    }

    io_uring_cq_advance(ring, num_cqes);
    return num_ready;
}

static int fdmon_io_uring_wait(AioContext *ctx, AioHandlerList *ready_list,
                               int64_t timeout)
{
    unsigned wait_nr = 1; /* block until at least one cqe is ready */
    int ret;

    if (timeout == 0) {
        wait_nr = 0; /* non-blocking */
    } else if (timeout > 0) {
        add_timeout_sqe(ctx, timeout);
    }

    fill_sq_ring(ctx);

    do {
        ret = io_uring_submit_and_wait(&ctx->fdmon_io_uring, wait_nr);
    } while (ret == -EINTR);

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
};

bool fdmon_io_uring_setup(AioContext *ctx)
{
    int ret;

    ret = io_uring_queue_init(FDMON_IO_URING_ENTRIES, &ctx->fdmon_io_uring, 0);
    if (ret != 0) {
        return false;
    }

    QSLIST_INIT(&ctx->submit_list);
    ctx->fdmon_ops = &fdmon_io_uring_ops;
    return true;
}

void fdmon_io_uring_destroy(AioContext *ctx)
{
    if (ctx->fdmon_ops == &fdmon_io_uring_ops) {
        AioHandler *node;

        io_uring_queue_exit(&ctx->fdmon_io_uring);

        /* Move handlers due to be removed onto the deleted list */
        while ((node = QSLIST_FIRST_RCU(&ctx->submit_list))) {
            unsigned flags = qatomic_fetch_and(&node->flags,
                    ~(FDMON_IO_URING_PENDING |
                      FDMON_IO_URING_ADD |
                      FDMON_IO_URING_REMOVE));

            if (flags & FDMON_IO_URING_REMOVE) {
                QLIST_INSERT_HEAD_RCU(&ctx->deleted_aio_handlers, node, node_deleted);
            }

            QSLIST_REMOVE_HEAD_RCU(&ctx->submit_list, node_submitted);
        }

        ctx->fdmon_ops = &fdmon_poll_ops;
    }
}
