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
 */

#include "qemu-common.h"
#include "block.h"
#include "sys-queue.h"
#include "qemu_socket.h"

typedef struct AioHandler AioHandler;

/* The list of registered AIO handlers */
static LIST_HEAD(, AioHandler) aio_handlers;

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
    LIST_ENTRY(AioHandler) node;
};

static AioHandler *find_aio_handler(int fd)
{
    AioHandler *node;

    LIST_FOREACH(node, &aio_handlers, node) {
        if (node->fd == fd)
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
                LIST_REMOVE(node, node);
                qemu_free(node);
            }
        }
    } else {
        if (node == NULL) {
            /* Alloc and insert if it's not already there */
            node = qemu_mallocz(sizeof(AioHandler));
            if (node == NULL)
                return -ENOMEM;
            node->fd = fd;
            LIST_INSERT_HEAD(&aio_handlers, node, node);
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
    AioHandler *node;
    int ret;

    do {
        ret = 0;

        LIST_FOREACH(node, &aio_handlers, node) {
            ret |= node->io_flush(node->opaque);
        }

        qemu_aio_wait();
    } while (ret > 0);
}

void qemu_aio_wait(void)
{
    int ret;

    if (qemu_bh_poll())
        return;

    do {
        AioHandler *node;
        fd_set rdfds, wrfds;
        int max_fd = -1;

        walking_handlers = 1;

        FD_ZERO(&rdfds);
        FD_ZERO(&wrfds);

        /* fill fd sets */
        LIST_FOREACH(node, &aio_handlers, node) {
            /* If there aren't pending AIO operations, don't invoke callbacks.
             * Otherwise, if there are no AIO requests, qemu_aio_wait() would
             * wait indefinitely.
             */
            if (node->io_flush && node->io_flush(node->opaque) == 0)
                continue;

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
        if (max_fd == -1)
            break;

        /* wait until next event */
        ret = select(max_fd, &rdfds, &wrfds, NULL, NULL);
        if (ret == -1 && errno == EINTR)
            continue;

        /* if we have any readable fds, dispatch event */
        if (ret > 0) {
            walking_handlers = 1;

            /* we have to walk very carefully in case
             * qemu_aio_set_fd_handler is called while we're walking */
            node = LIST_FIRST(&aio_handlers);
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
                node = LIST_NEXT(node, node);

                if (tmp->deleted) {
                    LIST_REMOVE(tmp, node);
                    qemu_free(tmp);
                }
            }

            walking_handlers = 0;
        }
    } while (ret == 0);
}
