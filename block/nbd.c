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

#define MAX_NBD_REQUESTS	16
#define HANDLE_TO_INDEX(bs, handle) ((handle) ^ ((uint64_t)(intptr_t)bs))
#define INDEX_TO_HANDLE(bs, index)  ((index)  ^ ((uint64_t)(intptr_t)bs))

typedef struct BDRVNBDState {
    int sock;
    uint32_t nbdflags;
    off_t size;
    size_t blocksize;
    char *export_name; /* An NBD server may export several devices */

    CoMutex send_mutex;
    CoMutex free_sema;
    Coroutine *send_coroutine;
    int in_flight;

    Coroutine *recv_coroutine[MAX_NBD_REQUESTS];
    struct nbd_reply reply;

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

static void nbd_coroutine_start(BDRVNBDState *s, struct nbd_request *request)
{
    int i;

    /* Poor man semaphore.  The free_sema is locked when no other request
     * can be accepted, and unlocked after receiving one reply.  */
    if (s->in_flight >= MAX_NBD_REQUESTS - 1) {
        qemu_co_mutex_lock(&s->free_sema);
        assert(s->in_flight < MAX_NBD_REQUESTS);
    }
    s->in_flight++;

    for (i = 0; i < MAX_NBD_REQUESTS; i++) {
        if (s->recv_coroutine[i] == NULL) {
            s->recv_coroutine[i] = qemu_coroutine_self();
            break;
        }
    }

    assert(i < MAX_NBD_REQUESTS);
    request->handle = INDEX_TO_HANDLE(s, i);
}

static int nbd_have_request(void *opaque)
{
    BDRVNBDState *s = opaque;

    return s->in_flight > 0;
}

static void nbd_reply_ready(void *opaque)
{
    BDRVNBDState *s = opaque;
    int i;

    if (s->reply.handle == 0) {
        /* No reply already in flight.  Fetch a header.  */
        if (nbd_receive_reply(s->sock, &s->reply) < 0) {
            s->reply.handle = 0;
            goto fail;
        }
    }

    /* There's no need for a mutex on the receive side, because the
     * handler acts as a synchronization point and ensures that only
     * one coroutine is called until the reply finishes.  */
    i = HANDLE_TO_INDEX(s, s->reply.handle);
    if (s->recv_coroutine[i]) {
        qemu_coroutine_enter(s->recv_coroutine[i], NULL);
        return;
    }

fail:
    for (i = 0; i < MAX_NBD_REQUESTS; i++) {
        if (s->recv_coroutine[i]) {
            qemu_coroutine_enter(s->recv_coroutine[i], NULL);
        }
    }
}

static void nbd_restart_write(void *opaque)
{
    BDRVNBDState *s = opaque;
    qemu_coroutine_enter(s->send_coroutine, NULL);
}

static int nbd_co_send_request(BDRVNBDState *s, struct nbd_request *request,
                               struct iovec *iov, int offset)
{
    int rc, ret;

    qemu_co_mutex_lock(&s->send_mutex);
    s->send_coroutine = qemu_coroutine_self();
    qemu_aio_set_fd_handler(s->sock, nbd_reply_ready, nbd_restart_write,
                            nbd_have_request, NULL, s);
    rc = nbd_send_request(s->sock, request);
    if (rc != -1 && iov) {
        ret = qemu_co_sendv(s->sock, iov, request->len, offset);
        if (ret != request->len) {
            errno = -EIO;
            rc = -1;
        }
    }
    qemu_aio_set_fd_handler(s->sock, nbd_reply_ready, NULL,
                            nbd_have_request, NULL, s);
    s->send_coroutine = NULL;
    qemu_co_mutex_unlock(&s->send_mutex);
    return rc;
}

static void nbd_co_receive_reply(BDRVNBDState *s, struct nbd_request *request,
                                 struct nbd_reply *reply,
                                 struct iovec *iov, int offset)
{
    int ret;

    /* Wait until we're woken up by the read handler.  TODO: perhaps
     * peek at the next reply and avoid yielding if it's ours?  */
    qemu_coroutine_yield();
    *reply = s->reply;
    if (reply->handle != request->handle) {
        reply->error = EIO;
    } else {
        if (iov && reply->error == 0) {
            ret = qemu_co_recvv(s->sock, iov, request->len, offset);
            if (ret != request->len) {
                reply->error = EIO;
            }
        }

        /* Tell the read handler to read another header.  */
        s->reply.handle = 0;
    }
}

static void nbd_coroutine_end(BDRVNBDState *s, struct nbd_request *request)
{
    int i = HANDLE_TO_INDEX(s, request->handle);
    s->recv_coroutine[i] = NULL;
    if (s->in_flight-- == MAX_NBD_REQUESTS) {
        qemu_co_mutex_unlock(&s->free_sema);
    }
}

static int nbd_establish_connection(BlockDriverState *bs)
{
    BDRVNBDState *s = bs->opaque;
    int sock;
    int ret;
    off_t size;
    size_t blocksize;

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
    ret = nbd_receive_negotiate(sock, s->export_name, &s->nbdflags, &size,
                                &blocksize);
    if (ret == -1) {
        logout("Failed to negotiate with the NBD server\n");
        closesocket(sock);
        return -errno;
    }

    /* Now that we're connected, set the socket to be non-blocking and
     * kick the reply mechanism.  */
    socket_set_nonblock(sock);
    qemu_aio_set_fd_handler(s->sock, nbd_reply_ready, NULL,
                            nbd_have_request, NULL, s);

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
    request.from = 0;
    request.len = 0;
    nbd_send_request(s->sock, &request);

    qemu_aio_set_fd_handler(s->sock, NULL, NULL, NULL, NULL, NULL);
    closesocket(s->sock);
}

static int nbd_open(BlockDriverState *bs, const char* filename, int flags)
{
    BDRVNBDState *s = bs->opaque;
    int result;

    qemu_co_mutex_init(&s->send_mutex);
    qemu_co_mutex_init(&s->free_sema);

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

static int nbd_co_readv_1(BlockDriverState *bs, int64_t sector_num,
                          int nb_sectors, QEMUIOVector *qiov,
                          int offset)
{
    BDRVNBDState *s = bs->opaque;
    struct nbd_request request;
    struct nbd_reply reply;

    request.type = NBD_CMD_READ;
    request.from = sector_num * 512;
    request.len = nb_sectors * 512;

    nbd_coroutine_start(s, &request);
    if (nbd_co_send_request(s, &request, NULL, 0) == -1) {
        reply.error = errno;
    } else {
        nbd_co_receive_reply(s, &request, &reply, qiov->iov, offset);
    }
    nbd_coroutine_end(s, &request);
    return -reply.error;

}

static int nbd_co_writev_1(BlockDriverState *bs, int64_t sector_num,
                           int nb_sectors, QEMUIOVector *qiov,
                           int offset)
{
    BDRVNBDState *s = bs->opaque;
    struct nbd_request request;
    struct nbd_reply reply;

    request.type = NBD_CMD_WRITE;
    if (!bdrv_enable_write_cache(bs) && (s->nbdflags & NBD_FLAG_SEND_FUA)) {
        request.type |= NBD_CMD_FLAG_FUA;
    }

    request.from = sector_num * 512;
    request.len = nb_sectors * 512;

    nbd_coroutine_start(s, &request);
    if (nbd_co_send_request(s, &request, qiov->iov, offset) == -1) {
        reply.error = errno;
    } else {
        nbd_co_receive_reply(s, &request, &reply, NULL, 0);
    }
    nbd_coroutine_end(s, &request);
    return -reply.error;
}

/* qemu-nbd has a limit of slightly less than 1M per request.  Try to
 * remain aligned to 4K. */
#define NBD_MAX_SECTORS 2040

static int nbd_co_readv(BlockDriverState *bs, int64_t sector_num,
                        int nb_sectors, QEMUIOVector *qiov)
{
    int offset = 0;
    int ret;
    while (nb_sectors > NBD_MAX_SECTORS) {
        ret = nbd_co_readv_1(bs, sector_num, NBD_MAX_SECTORS, qiov, offset);
        if (ret < 0) {
            return ret;
        }
        offset += NBD_MAX_SECTORS * 512;
        sector_num += NBD_MAX_SECTORS;
        nb_sectors -= NBD_MAX_SECTORS;
    }
    return nbd_co_readv_1(bs, sector_num, nb_sectors, qiov, offset);
}

static int nbd_co_writev(BlockDriverState *bs, int64_t sector_num,
                         int nb_sectors, QEMUIOVector *qiov)
{
    int offset = 0;
    int ret;
    while (nb_sectors > NBD_MAX_SECTORS) {
        ret = nbd_co_writev_1(bs, sector_num, NBD_MAX_SECTORS, qiov, offset);
        if (ret < 0) {
            return ret;
        }
        offset += NBD_MAX_SECTORS * 512;
        sector_num += NBD_MAX_SECTORS;
        nb_sectors -= NBD_MAX_SECTORS;
    }
    return nbd_co_writev_1(bs, sector_num, nb_sectors, qiov, offset);
}

static int nbd_co_flush(BlockDriverState *bs)
{
    BDRVNBDState *s = bs->opaque;
    struct nbd_request request;
    struct nbd_reply reply;

    if (!(s->nbdflags & NBD_FLAG_SEND_FLUSH)) {
        return 0;
    }

    request.type = NBD_CMD_FLUSH;
    if (s->nbdflags & NBD_FLAG_SEND_FUA) {
        request.type |= NBD_CMD_FLAG_FUA;
    }

    request.from = 0;
    request.len = 0;

    nbd_coroutine_start(s, &request);
    if (nbd_co_send_request(s, &request, NULL, 0) == -1) {
        reply.error = errno;
    } else {
        nbd_co_receive_reply(s, &request, &reply, NULL, 0);
    }
    nbd_coroutine_end(s, &request);
    return -reply.error;
}

static int nbd_co_discard(BlockDriverState *bs, int64_t sector_num,
                          int nb_sectors)
{
    BDRVNBDState *s = bs->opaque;
    struct nbd_request request;
    struct nbd_reply reply;

    if (!(s->nbdflags & NBD_FLAG_SEND_TRIM)) {
        return 0;
    }
    request.type = NBD_CMD_TRIM;
    request.from = sector_num * 512;;
    request.len = nb_sectors * 512;

    nbd_coroutine_start(s, &request);
    if (nbd_co_send_request(s, &request, NULL, 0) == -1) {
        reply.error = errno;
    } else {
        nbd_co_receive_reply(s, &request, &reply, NULL, 0);
    }
    nbd_coroutine_end(s, &request);
    return -reply.error;
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
    .format_name         = "nbd",
    .instance_size       = sizeof(BDRVNBDState),
    .bdrv_file_open      = nbd_open,
    .bdrv_co_readv       = nbd_co_readv,
    .bdrv_co_writev      = nbd_co_writev,
    .bdrv_close          = nbd_close,
    .bdrv_co_flush_to_os = nbd_co_flush,
    .bdrv_co_discard     = nbd_co_discard,
    .bdrv_getlength      = nbd_getlength,
    .protocol_name       = "nbd",
};

static void bdrv_nbd_init(void)
{
    bdrv_register(&bdrv_nbd);
}

block_init(bdrv_nbd_init);
