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
#include "block_int.h"
#include "module.h"
#include "qemu_socket.h"

#include <sys/types.h>
#include <unistd.h>

#define EN_OPTSTR ":exportname="

/* #define DEBUG_NBD */

#if defined(DEBUG_NBD)
#define logout(fmt, ...) \
                fprintf(stderr, "nbd\t%-24s" fmt, __func__, ##__VA_ARGS__)
#else
#define logout(fmt, ...) ((void)0)
#endif

typedef struct BDRVNBDState {
    int sock;
    off_t size;
    size_t blocksize;
    char *export_name; /* An NBD server may export several devices */

    /* If it begins with  '/', this is a UNIX domain socket. Otherwise,
     * it's a string of the form <hostname|ip4|\[ip6\]>:port
     */
    char *host_spec;
} BDRVNBDState;

static int nbd_config(BDRVNBDState *s, const char *filename, int flags)
{
    char *file;
    char *export_name;
    const char *host_spec;
    const char *unixpath;
    int err = -EINVAL;

    file = g_strdup(filename);

    export_name = strstr(file, EN_OPTSTR);
    if (export_name) {
        if (export_name[strlen(EN_OPTSTR)] == 0) {
            goto out;
        }
        export_name[0] = 0; /* truncate 'file' */
        export_name += strlen(EN_OPTSTR);
        s->export_name = g_strdup(export_name);
    }

    /* extract the host_spec - fail if it's not nbd:... */
    if (!strstart(file, "nbd:", &host_spec)) {
        goto out;
    }

    /* are we a UNIX or TCP socket? */
    if (strstart(host_spec, "unix:", &unixpath)) {
        if (unixpath[0] != '/') { /* We demand  an absolute path*/
            goto out;
        }
        s->host_spec = g_strdup(unixpath);
    } else {
        s->host_spec = g_strdup(host_spec);
    }

    err = 0;

out:
    g_free(file);
    if (err != 0) {
        g_free(s->export_name);
        g_free(s->host_spec);
    }
    return err;
}

static int nbd_establish_connection(BlockDriverState *bs)
{
    BDRVNBDState *s = bs->opaque;
    int sock;
    int ret;
    off_t size;
    size_t blocksize;
    uint32_t nbdflags;

    if (s->host_spec[0] == '/') {
        sock = unix_socket_outgoing(s->host_spec);
    } else {
        sock = tcp_socket_outgoing_spec(s->host_spec);
    }

    /* Failed to establish connection */
    if (sock == -1) {
        logout("Failed to establish connection to NBD server\n");
        return -errno;
    }

    /* NBD handshake */
    ret = nbd_receive_negotiate(sock, s->export_name, &nbdflags, &size,
                                &blocksize);
    if (ret == -1) {
        logout("Failed to negotiate with the NBD server\n");
        closesocket(sock);
        return -errno;
    }

    /* Now that we're connected, set the socket to be non-blocking */
    socket_set_nonblock(sock);

    s->sock = sock;
    s->size = size;
    s->blocksize = blocksize;

    logout("Established connection with NBD server\n");
    return 0;
}

static void nbd_teardown_connection(BlockDriverState *bs)
{
    BDRVNBDState *s = bs->opaque;
    struct nbd_request request;

    request.type = NBD_CMD_DISC;
    request.handle = (uint64_t)(intptr_t)bs;
    request.from = 0;
    request.len = 0;
    nbd_send_request(s->sock, &request);

    closesocket(s->sock);
}

static int nbd_open(BlockDriverState *bs, const char* filename, int flags)
{
    BDRVNBDState *s = bs->opaque;
    int result;

    /* Pop the config into our state object. Exit if invalid. */
    result = nbd_config(s, filename, flags);
    if (result != 0) {
        return result;
    }

    /* establish TCP connection, return error if it fails
     * TODO: Configurable retry-until-timeout behaviour.
     */
    result = nbd_establish_connection(bs);

    return result;
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
    g_free(s->export_name);
    g_free(s->host_spec);

    nbd_teardown_connection(bs);
}

static int64_t nbd_getlength(BlockDriverState *bs)
{
    BDRVNBDState *s = bs->opaque;

    return s->size;
}

static BlockDriver bdrv_nbd = {
    .format_name	= "nbd",
    .instance_size	= sizeof(BDRVNBDState),
    .bdrv_file_open	= nbd_open,
    .bdrv_read		= nbd_read,
    .bdrv_write		= nbd_write,
    .bdrv_close		= nbd_close,
    .bdrv_getlength	= nbd_getlength,
    .protocol_name	= "nbd",
};

static void bdrv_nbd_init(void)
{
    bdrv_register(&bdrv_nbd);
}

block_init(bdrv_nbd_init);
