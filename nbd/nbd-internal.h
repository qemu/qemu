/*
 * NBD Internal Declarations
 *
 * Copyright (C) 2016 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef NBD_INTERNAL_H
#define NBD_INTERNAL_H
#include "block/nbd.h"
#include "sysemu/block-backend.h"
#include "io/channel-tls.h"

#include "qemu/coroutine.h"
#include "qemu/iov.h"

#ifndef _WIN32
#include <sys/ioctl.h>
#endif
#if defined(__sun__) || defined(__HAIKU__)
#include <sys/ioccom.h>
#endif

#ifdef __linux__
#include <linux/fs.h>
#endif

#include "qemu/bswap.h"
#include "qemu/queue.h"
#include "qemu/main-loop.h"

/* #define DEBUG_NBD */

#ifdef DEBUG_NBD
#define DEBUG_NBD_PRINT 1
#else
#define DEBUG_NBD_PRINT 0
#endif

#define TRACE(msg, ...) do { \
    if (DEBUG_NBD_PRINT) { \
        LOG(msg, ## __VA_ARGS__); \
    } \
} while (0)

#define LOG(msg, ...) do { \
    fprintf(stderr, "%s:%s():L%d: " msg "\n", \
            __FILE__, __FUNCTION__, __LINE__, ## __VA_ARGS__); \
} while (0)

/* This is all part of the "official" NBD API.
 *
 * The most up-to-date documentation is available at:
 * https://github.com/yoe/nbd/blob/master/doc/proto.md
 */

#define NBD_REQUEST_SIZE        (4 + 2 + 2 + 8 + 8 + 4)
#define NBD_REPLY_SIZE          (4 + 4 + 8)
#define NBD_REQUEST_MAGIC       0x25609513
#define NBD_REPLY_MAGIC         0x67446698
#define NBD_OPTS_MAGIC          0x49484156454F5054LL
#define NBD_CLIENT_MAGIC        0x0000420281861253LL
#define NBD_REP_MAGIC           0x0003e889045565a9LL

#define NBD_SET_SOCK            _IO(0xab, 0)
#define NBD_SET_BLKSIZE         _IO(0xab, 1)
#define NBD_SET_SIZE            _IO(0xab, 2)
#define NBD_DO_IT               _IO(0xab, 3)
#define NBD_CLEAR_SOCK          _IO(0xab, 4)
#define NBD_CLEAR_QUE           _IO(0xab, 5)
#define NBD_PRINT_DEBUG         _IO(0xab, 6)
#define NBD_SET_SIZE_BLOCKS     _IO(0xab, 7)
#define NBD_DISCONNECT          _IO(0xab, 8)
#define NBD_SET_TIMEOUT         _IO(0xab, 9)
#define NBD_SET_FLAGS           _IO(0xab, 10)

#define NBD_OPT_EXPORT_NAME     (1)
#define NBD_OPT_ABORT           (2)
#define NBD_OPT_LIST            (3)
#define NBD_OPT_PEEK_EXPORT     (4)
#define NBD_OPT_STARTTLS        (5)

/* NBD errors are based on errno numbers, so there is a 1:1 mapping,
 * but only a limited set of errno values is specified in the protocol.
 * Everything else is squashed to EINVAL.
 */
#define NBD_SUCCESS    0
#define NBD_EPERM      1
#define NBD_EIO        5
#define NBD_ENOMEM     12
#define NBD_EINVAL     22
#define NBD_ENOSPC     28
#define NBD_ESHUTDOWN  108

static inline ssize_t read_sync(QIOChannel *ioc, void *buffer, size_t size)
{
    struct iovec iov = { .iov_base = buffer, .iov_len = size };
    /* Sockets are kept in blocking mode in the negotiation phase.  After
     * that, a non-readable socket simply means that another thread stole
     * our request/reply.  Synchronization is done with recv_coroutine, so
     * that this is coroutine-safe.
     */
    return nbd_wr_syncv(ioc, &iov, 1, size, true);
}

static inline ssize_t write_sync(QIOChannel *ioc, const void *buffer,
                                 size_t size)
{
    struct iovec iov = { .iov_base = (void *) buffer, .iov_len = size };

    return nbd_wr_syncv(ioc, &iov, 1, size, false);
}

struct NBDTLSHandshakeData {
    GMainLoop *loop;
    bool complete;
    Error *error;
};


void nbd_tls_handshake(QIOTask *task,
                       void *opaque);

#endif
