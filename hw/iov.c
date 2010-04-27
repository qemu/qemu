/*
 * Helpers for getting linearized buffers from iov / filling buffers into iovs
 *
 * Copyright IBM, Corp. 2007, 2008
 * Copyright (C) 2010 Red Hat, Inc.
 *
 * Author(s):
 *  Anthony Liguori <aliguori@us.ibm.com>
 *  Amit Shah <amit.shah@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#include "iov.h"

size_t iov_from_buf(struct iovec *iov, unsigned int iovcnt,
                    const void *buf, size_t size)
{
    size_t offset;
    unsigned int i;

    offset = 0;
    for (i = 0; offset < size && i < iovcnt; i++) {
        size_t len;

        len = MIN(iov[i].iov_len, size - offset);

        memcpy(iov[i].iov_base, buf + offset, len);
        offset += len;
    }
    return offset;
}
