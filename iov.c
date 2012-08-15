/*
 * Helpers for getting linearized buffers from iov / filling buffers into iovs
 *
 * Copyright IBM, Corp. 2007, 2008
 * Copyright (C) 2010 Red Hat, Inc.
 *
 * Author(s):
 *  Anthony Liguori <aliguori@us.ibm.com>
 *  Amit Shah <amit.shah@redhat.com>
 *  Michael Tokarev <mjt@tls.msk.ru>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "iov.h"

#ifdef _WIN32
# include <windows.h>
# include <winsock2.h>
#else
# include <sys/types.h>
# include <sys/socket.h>
#endif

size_t iov_from_buf(struct iovec *iov, unsigned int iov_cnt,
                    size_t offset, const void *buf, size_t bytes)
{
    size_t done;
    unsigned int i;
    for (i = 0, done = 0; (offset || done < bytes) && i < iov_cnt; i++) {
        if (offset < iov[i].iov_len) {
            size_t len = MIN(iov[i].iov_len - offset, bytes - done);
            memcpy(iov[i].iov_base + offset, buf + done, len);
            done += len;
            offset = 0;
        } else {
            offset -= iov[i].iov_len;
        }
    }
    assert(offset == 0);
    return done;
}

size_t iov_to_buf(const struct iovec *iov, const unsigned int iov_cnt,
                  size_t offset, void *buf, size_t bytes)
{
    size_t done;
    unsigned int i;
    for (i = 0, done = 0; (offset || done < bytes) && i < iov_cnt; i++) {
        if (offset < iov[i].iov_len) {
            size_t len = MIN(iov[i].iov_len - offset, bytes - done);
            memcpy(buf + done, iov[i].iov_base + offset, len);
            done += len;
            offset = 0;
        } else {
            offset -= iov[i].iov_len;
        }
    }
    assert(offset == 0);
    return done;
}

size_t iov_memset(const struct iovec *iov, const unsigned int iov_cnt,
                  size_t offset, int fillc, size_t bytes)
{
    size_t done;
    unsigned int i;
    for (i = 0, done = 0; (offset || done < bytes) && i < iov_cnt; i++) {
        if (offset < iov[i].iov_len) {
            size_t len = MIN(iov[i].iov_len - offset, bytes - done);
            memset(iov[i].iov_base + offset, fillc, len);
            done += len;
            offset = 0;
        } else {
            offset -= iov[i].iov_len;
        }
    }
    assert(offset == 0);
    return done;
}

size_t iov_size(const struct iovec *iov, const unsigned int iov_cnt)
{
    size_t len;
    unsigned int i;

    len = 0;
    for (i = 0; i < iov_cnt; i++) {
        len += iov[i].iov_len;
    }
    return len;
}

/* helper function for iov_send_recv() */
static ssize_t
do_send_recv(int sockfd, struct iovec *iov, unsigned iov_cnt, bool do_send)
{
#if defined CONFIG_IOVEC && defined CONFIG_POSIX
    ssize_t ret;
    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = iov;
    msg.msg_iovlen = iov_cnt;
    do {
        ret = do_send
            ? sendmsg(sockfd, &msg, 0)
            : recvmsg(sockfd, &msg, 0);
    } while (ret < 0 && errno == EINTR);
    return ret;
#else
    /* else send piece-by-piece */
    /*XXX Note: windows has WSASend() and WSARecv() */
    unsigned i = 0;
    ssize_t ret = 0;
    while (i < iov_cnt) {
        ssize_t r = do_send
            ? send(sockfd, iov[i].iov_base, iov[i].iov_len, 0)
            : recv(sockfd, iov[i].iov_base, iov[i].iov_len, 0);
        if (r > 0) {
            ret += r;
        } else if (!r) {
            break;
        } else if (errno == EINTR) {
            continue;
        } else {
            /* else it is some "other" error,
             * only return if there was no data processed. */
            if (ret == 0) {
                ret = -1;
            }
            break;
        }
        i++;
    }
    return ret;
#endif
}

ssize_t iov_send_recv(int sockfd, struct iovec *iov, unsigned iov_cnt,
                      size_t offset, size_t bytes,
                      bool do_send)
{
    ssize_t ret;
    unsigned si, ei;            /* start and end indexes */
    if (bytes == 0) {
        /* Catch the do-nothing case early, as otherwise we will pass an
         * empty iovec to sendmsg/recvmsg(), and not all implementations
         * accept this.
         */
        return 0;
    }

    /* Find the start position, skipping `offset' bytes:
     * first, skip all full-sized vector elements, */
    for (si = 0; si < iov_cnt && offset >= iov[si].iov_len; ++si) {
        offset -= iov[si].iov_len;
    }
    if (offset) {
        assert(si < iov_cnt);
        /* second, skip `offset' bytes from the (now) first element,
         * undo it on exit */
        iov[si].iov_base += offset;
        iov[si].iov_len -= offset;
    }
    /* Find the end position skipping `bytes' bytes: */
    /* first, skip all full-sized elements */
    for (ei = si; ei < iov_cnt && iov[ei].iov_len <= bytes; ++ei) {
        bytes -= iov[ei].iov_len;
    }
    if (bytes) {
        /* second, fixup the last element, and remember
         * the length we've cut from the end of it in `bytes' */
        size_t tail;
        assert(ei < iov_cnt);
        assert(iov[ei].iov_len > bytes);
        tail = iov[ei].iov_len - bytes;
        iov[ei].iov_len = bytes;
        bytes = tail;  /* bytes is now equal to the tail size */
        ++ei;
    }

    ret = do_send_recv(sockfd, iov + si, ei - si, do_send);

    /* Undo the changes above */
    if (offset) {
        iov[si].iov_base -= offset;
        iov[si].iov_len += offset;
    }
    if (bytes) {
        iov[ei-1].iov_len += bytes;
    }

    return ret;
}


void iov_hexdump(const struct iovec *iov, const unsigned int iov_cnt,
                 FILE *fp, const char *prefix, size_t limit)
{
    unsigned int i, v, b;
    uint8_t *c;

    c = iov[0].iov_base;
    for (i = 0, v = 0, b = 0; b < limit; i++, b++) {
        if (i == iov[v].iov_len) {
            i = 0; v++;
            if (v == iov_cnt) {
                break;
            }
            c = iov[v].iov_base;
        }
        if ((b % 16) == 0) {
            fprintf(fp, "%s: %04x:", prefix, b);
        }
        if ((b % 4) == 0) {
            fprintf(fp, " ");
        }
        fprintf(fp, " %02x", c[i]);
        if ((b % 16) == 15) {
            fprintf(fp, "\n");
        }
    }
    if ((b % 16) != 0) {
        fprintf(fp, "\n");
    }
}
