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

/* This is all part of the "official" NBD API.
 *
 * The most up-to-date documentation is available at:
 * https://github.com/yoe/nbd/blob/master/doc/proto.md
 */

/* Size of all NBD_OPT_*, without payload */
#define NBD_REQUEST_SIZE            (4 + 2 + 2 + 8 + 8 + 4)
/* Size of all NBD_REP_* sent in answer to most NBD_OPT_*, without payload */
#define NBD_REPLY_SIZE              (4 + 4 + 8)
/* Size of reply to NBD_OPT_EXPORT_NAME */
#define NBD_REPLY_EXPORT_NAME_SIZE  (8 + 2 + 124)
/* Size of oldstyle negotiation */
#define NBD_OLDSTYLE_NEGOTIATE_SIZE (8 + 8 + 8 + 4 + 124)

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

/* nbd_read_eof
 * Tries to read @size bytes from @ioc. Returns number of bytes actually read.
 * May return a value >= 0 and < size only on EOF, i.e. when iteratively called
 * qio_channel_readv() returns 0. So, there is no need to call nbd_read_eof
 * iteratively.
 */
static inline ssize_t nbd_read_eof(QIOChannel *ioc, void *buffer, size_t size,
                                   Error **errp)
{
    struct iovec iov = { .iov_base = buffer, .iov_len = size };
    /* Sockets are kept in blocking mode in the negotiation phase.  After
     * that, a non-readable socket simply means that another thread stole
     * our request/reply.  Synchronization is done with recv_coroutine, so
     * that this is coroutine-safe.
     */
    return nbd_rwv(ioc, &iov, 1, size, true, errp);
}

/* nbd_read
 * Reads @size bytes from @ioc. Returns 0 on success.
 */
static inline int nbd_read(QIOChannel *ioc, void *buffer, size_t size,
                           Error **errp)
{
    ssize_t ret = nbd_read_eof(ioc, buffer, size, errp);

    if (ret >= 0 && ret != size) {
        ret = -EINVAL;
        error_setg(errp, "End of file");
    }

    return ret < 0 ? ret : 0;
}

/* nbd_write
 * Writes @size bytes to @ioc. Returns 0 on success.
 */
static inline int nbd_write(QIOChannel *ioc, const void *buffer, size_t size,
                            Error **errp)
{
    struct iovec iov = { .iov_base = (void *) buffer, .iov_len = size };

    ssize_t ret = nbd_rwv(ioc, &iov, 1, size, false, errp);

    assert(ret < 0 || ret == size);

    return ret < 0 ? ret : 0;
}

struct NBDTLSHandshakeData {
    GMainLoop *loop;
    bool complete;
    Error *error;
};


void nbd_tls_handshake(QIOTask *task,
                       void *opaque);
const char *nbd_opt_lookup(uint32_t opt);
const char *nbd_rep_lookup(uint32_t rep);
const char *nbd_info_lookup(uint16_t info);
const char *nbd_cmd_lookup(uint16_t info);

int nbd_drop(QIOChannel *ioc, size_t size, Error **errp);

#endif
