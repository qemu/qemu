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
#include "block.h"
#include "qemu-queue.h"
#include "qemu_socket.h"

typedef struct AioHandler AioHandler;

/* The list of registered AIO handlers */
static QLIST_HEAD(, AioHandler) aio_handlers;

/* This is a simple lock used to protect the aio_handlers list.  Specifically,
 * it's used to ensure that no callbacks are removed while we're walking and
 * dispatching callbacks.
 */
static int walking_handlers;

struct AioHandler
{
    int fd;
    IOHandler *io_read;
    IOHandler *io_write;
    AioFlushHandler *io_flush;
    int deleted;
    void *opaque;
    QLIST_ENTRY(AioHandler) node;
};

static AioHandler *find_aio_handler(int fd)
{
    AioHandler *node;

    QLIST_FOREACH(node, &aio_handlers, node) {
        if (node->fd == fd)
            if (!node->deleted)
                return node;
    }

    return NULL;
}

int qemu_aio_set_fd_handler(int fd,
                            IOHandler *io_read,
                            IOHandler *io_write,
                            AioFlushHandler *io_flush,
                            void *opaque)
{
    AioHandler *node;

    node = find_aio_handler(fd);

    /* Are we deleting the fd handler? */
    if (!io_read && !io_write) {
        if (node) {
            /* If the lock is held, just mark the node as deleted */
            if (walking_handlers)
                node->deleted = 1;
            else {
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
            node->fd = fd;
            QLIST_INSERT_HEAD(&aio_handlers, node, node);
        }
        /* Update handler with latest information */
        node->io_read = io_read;
        node->io_write = io_write;
        node->io_flush = io_flush;
        node->opaque = opaque;
    }

    qemu_set_fd_handler2(fd, NULL, io_read, io_write, opaque);

    return 0;
}

void qemu_aio_flush(void)
{
    while (qemu_aio_wait());
}

bool qemu_aio_wait(void)
{
    AioHandler *node;
    fd_set rdfds, wrfds;
    int max_fd = -1;
    int ret;
    bool busy;

    /*
     * If there are callbacks left that have been queued, we need to call then.
     * Do not call select in this case, because it is possible that the caller
     * does not need a complete flush (as is the case for qemu_aio_wait loops).
     */
    if (qemu_bh_poll()) {
        return true;
    }

    walking_handlers = 1;

    FD_ZERO(&rdfds);
    FD_ZERO(&wrfds);

    /* fill fd sets */
    busy = false;
    QLIST_FOREACH(node, &aio_handlers, node) {
        /* If there aren't pending AIO operations, don't invoke callbacks.
         * Otherwise, if there are no AIO requests, qemu_aio_wait() would
         * wait indefinitely.
         */
        if (node->io_flush) {
            if (node->io_flush(node->opaque) == 0) {
                continue;
            }
            busy = true;
        }
        if (!node->deleted && node->io_read) {
            FD_SET(node->fd, &rdfds);
            max_fd = MAX(max_fd, node->fd + 1);
        }
        if (!node->deleted && node->io_write) {
            FD_SET(node->fd, &wrfds);
            max_fd = MAX(max_fd, node->fd + 1);
        }
    }

    walking_handlers = 0;

    /* No AIO operations?  Get us out of here */
    if (!busy) {
        return false;
    }

    /* wait until next event */
    ret = select(max_fd, &rdfds, &wrfds, NULL, NULL);

    /* if we have any readable fds, dispatch event */
    if (ret > 0) {
        walking_handlers = 1;

        /* we have to walk very carefully in case
         * qemu_aio_set_fd_handler is called while we're walking */
        node = QLIST_FIRST(&aio_handlers);
        while (node) {
            AioHandler *tmp;

            if (!node->deleted &&
                FD_ISSET(node->fd, &rdfds) &&
                node->io_read) {
                node->io_read(node->opaque);
            }
            if (!node->deleted &&
                FD_ISSET(node->fd, &wrfds) &&
                node->io_write) {
                node->io_write(node->opaque);
            }

            tmp = node;
            node = QLIST_NEXT(node, node);

            if (tmp->deleted) {
                QLIST_REMOVE(tmp, node);
                g_free(tmp);
            }
        }

        walking_handlers = 0;
    }

    return true;
}
