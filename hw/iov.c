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

size_t iov_to_buf(const struct iovec *iov, const unsigned int iovcnt,
                  void *buf, size_t offset, size_t size)
{
    uint8_t *ptr;
    size_t iov_off, buf_off;
    unsigned int i;

    ptr = buf;
    iov_off = 0;
    buf_off = 0;
    for (i = 0; i < iovcnt && size; i++) {
        if (offset < (iov_off + iov[i].iov_len)) {
            size_t len = MIN((iov_off + iov[i].iov_len) - offset , size);

            memcpy(ptr + buf_off, iov[i].iov_base + (offset - iov_off), len);

            buf_off += len;
            offset += len;
            size -= len;
        }
        iov_off += iov[i].iov_len;
    }
    return buf_off;
}

size_t iov_size(const struct iovec *iov, const unsigned int iovcnt)
{
    size_t len;
    unsigned int i;

    len = 0;
    for (i = 0; i < iovcnt; i++) {
        len += iov[i].iov_len;
    }
    return len;
}
