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
#ifdef HAVE_SYS_IOCCOM_H
#include <sys/ioccom.h>
#endif

#ifdef __linux__
#include <linux/fs.h>
#endif

#include "qemu/bswap.h"

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

#define NBD_INIT_MAGIC              0x4e42444d41474943LL /* ASCII "NBDMAGIC" */
#define NBD_REQUEST_MAGIC           0x25609513
#define NBD_OPTS_MAGIC              0x49484156454F5054LL /* ASCII "IHAVEOPT" */
#define NBD_CLIENT_MAGIC            0x0000420281861253LL
#define NBD_REP_MAGIC               0x0003e889045565a9LL

#define NBD_SET_SOCK                _IO(0xab, 0)
#define NBD_SET_BLKSIZE             _IO(0xab, 1)
#define NBD_SET_SIZE                _IO(0xab, 2)
#define NBD_DO_IT                   _IO(0xab, 3)
#define NBD_CLEAR_SOCK              _IO(0xab, 4)
#define NBD_CLEAR_QUE               _IO(0xab, 5)
#define NBD_PRINT_DEBUG             _IO(0xab, 6)
#define NBD_SET_SIZE_BLOCKS         _IO(0xab, 7)
#define NBD_DISCONNECT              _IO(0xab, 8)
#define NBD_SET_TIMEOUT             _IO(0xab, 9)
#define NBD_SET_FLAGS               _IO(0xab, 10)

/* nbd_write
 * Writes @size bytes to @ioc. Returns 0 on success.
 */
static inline int nbd_write(QIOChannel *ioc, const void *buffer, size_t size,
                            Error **errp)
{
    return qio_channel_write_all(ioc, buffer, size, errp) < 0 ? -EIO : 0;
}

struct NBDTLSHandshakeData {
    GMainLoop *loop;
    bool complete;
    Error *error;
};


void nbd_tls_handshake(QIOTask *task,
                       void *opaque);

int nbd_drop(QIOChannel *ioc, size_t size, Error **errp);

#endif
