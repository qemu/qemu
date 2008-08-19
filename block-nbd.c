/*
 * QEMU Block driver for  NBD
 *
 * Copyright (C) 2008 Bull S.A.S.
 *     Author: Laurent Vivier <Laurent.Vivier@bull.net>
 *
 * Some parts:
 *    Copyright (C) 2007 Anthony Liguori <anthony@codemonkey.ws>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu-common.h"
#include "nbd.h"

#include <sys/types.h>
#include <unistd.h>

typedef struct BDRVNBDState {
    int sock;
    off_t size;
    size_t blocksize;
} BDRVNBDState;

static int nbd_open(BlockDriverState *bs, const char* filename, int flags)
{
    BDRVNBDState *s = bs->opaque;
    const char *host;
    const char *unixpath;
    int sock;
    off_t size;
    size_t blocksize;
    int ret;

    if ((flags & BDRV_O_CREAT))
        return -EINVAL;

    if (!strstart(filename, "nbd:", &host))
        return -EINVAL;

    if (strstart(host, "unix:", &unixpath)) {

        if (unixpath[0] != '/')
            return -EINVAL;

        sock = unix_socket_outgoing(unixpath);

    } else {
        uint16_t port;
        char *p, *r;
        char hostname[128];

        pstrcpy(hostname, 128, host);

        p = strchr(hostname, ':');
        if (p == NULL)
            return -EINVAL;

        *p = '\0';
        p++;

        port = strtol(p, &r, 0);
        if (r == p)
            return -EINVAL;
        sock = tcp_socket_outgoing(hostname, port);
    }

    if (sock == -1)
        return -errno;

    ret = nbd_receive_negotiate(sock, &size, &blocksize);
    if (ret == -1)
        return -errno;

    s->sock = sock;
    s->size = size;
    s->blocksize = blocksize;

    return 0;
}

static int nbd_read(BlockDriverState *bs, int64_t sector_num,
                    uint8_t *buf, int nb_sectors)
{
    BDRVNBDState *s = bs->opaque;
    struct nbd_request request;
    struct nbd_reply reply;

    request.type = NBD_CMD_READ;
    request.handle = (uint64_t)(intptr_t)bs;
    request.from = sector_num * 512;;
    request.len = nb_sectors * 512;

    if (nbd_send_request(s->sock, &request) == -1)
        return -errno;

    if (nbd_receive_reply(s->sock, &reply) == -1)
        return -errno;

    if (reply.error !=0)
        return -reply.error;

    if (reply.handle != request.handle)
        return -EIO;

    if (nbd_wr_sync(s->sock, buf, request.len, 1) != request.len)
        return -EIO;

    return 0;
}

static int nbd_write(BlockDriverState *bs, int64_t sector_num,
                     const uint8_t *buf, int nb_sectors)
{
    BDRVNBDState *s = bs->opaque;
    struct nbd_request request;
    struct nbd_reply reply;

    request.type = NBD_CMD_WRITE;
    request.handle = (uint64_t)(intptr_t)bs;
    request.from = sector_num * 512;;
    request.len = nb_sectors * 512;

    if (nbd_send_request(s->sock, &request) == -1)
        return -errno;

    if (nbd_wr_sync(s->sock, (uint8_t*)buf, request.len, 0) != request.len)
        return -EIO;

    if (nbd_receive_reply(s->sock, &reply) == -1)
        return -errno;

    if (reply.error !=0)
        return -reply.error;

    if (reply.handle != request.handle)
        return -EIO;

    return 0;
}

static void nbd_close(BlockDriverState *bs)
{
    BDRVNBDState *s = bs->opaque;
    struct nbd_request request;

    request.type = NBD_CMD_DISC;
    request.handle = (uint64_t)(intptr_t)bs;
    request.from = 0;
    request.len = 0;
    nbd_send_request(s->sock, &request);

    close(s->sock);
}

static int64_t nbd_getlength(BlockDriverState *bs)
{
    BDRVNBDState *s = bs->opaque;

    return s->size;
}

BlockDriver bdrv_nbd = {
    "nbd",
    sizeof(BDRVNBDState),
    NULL, /* no probe for protocols */
    nbd_open,
    nbd_read,
    nbd_write,
    nbd_close,
    .bdrv_getlength = nbd_getlength,
    .protocol_name = "nbd",
};
