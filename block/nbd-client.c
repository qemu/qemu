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

#include "qemu/osdep.h"
#include "nbd-client.h"

#define HANDLE_TO_INDEX(bs, handle) ((handle) ^ ((uint64_t)(intptr_t)bs))
#define INDEX_TO_HANDLE(bs, index)  ((index)  ^ ((uint64_t)(intptr_t)bs))

static void nbd_recv_coroutines_enter_all(NbdClientSession *s)
{
    int i;

    for (i = 0; i < MAX_NBD_REQUESTS; i++) {
        if (s->recv_coroutine[i]) {
            qemu_coroutine_enter(s->recv_coroutine[i], NULL);
        }
    }
}

static void nbd_teardown_connection(BlockDriverState *bs)
{
    NbdClientSession *client = nbd_get_client_session(bs);

    if (!client->ioc) { /* Already closed */
        return;
    }

    /* finish any pending coroutines */
    qio_channel_shutdown(client->ioc,
                         QIO_CHANNEL_SHUTDOWN_BOTH,
                         NULL);
    nbd_recv_coroutines_enter_all(client);

    nbd_client_detach_aio_context(bs);
    object_unref(OBJECT(client->sioc));
    client->sioc = NULL;
    object_unref(OBJECT(client->ioc));
    client->ioc = NULL;
}

static void nbd_reply_ready(void *opaque)
{
    BlockDriverState *bs = opaque;
    NbdClientSession *s = nbd_get_client_session(bs);
    uint64_t i;
    int ret;

    if (!s->ioc) { /* Already closed */
        return;
    }

    if (s->reply.handle == 0) {
        /* No reply already in flight.  Fetch a header.  It is possible
         * that another thread has done the same thing in parallel, so
         * the socket is not readable anymore.
         */
        ret = nbd_receive_reply(s->ioc, &s->reply);
        if (ret == -EAGAIN) {
            return;
        }
        if (ret < 0) {
            s->reply.handle = 0;
            goto fail;
        }
    }

    /* There's no need for a mutex on the receive side, because the
     * handler acts as a synchronization point and ensures that only
     * one coroutine is called until the reply finishes.  */
    i = HANDLE_TO_INDEX(s, s->reply.handle);
    if (i >= MAX_NBD_REQUESTS) {
        goto fail;
    }

    if (s->recv_coroutine[i]) {
        qemu_coroutine_enter(s->recv_coroutine[i], NULL);
        return;
    }

fail:
    nbd_teardown_connection(bs);
}

static void nbd_restart_write(void *opaque)
{
    BlockDriverState *bs = opaque;

    qemu_coroutine_enter(nbd_get_client_session(bs)->send_coroutine, NULL);
}

static int nbd_co_send_request(BlockDriverState *bs,
                               struct nbd_request *request,
                               QEMUIOVector *qiov, int offset)
{
    NbdClientSession *s = nbd_get_client_session(bs);
    AioContext *aio_context;
    int rc, ret, i;

    qemu_co_mutex_lock(&s->send_mutex);

    for (i = 0; i < MAX_NBD_REQUESTS; i++) {
        if (s->recv_coroutine[i] == NULL) {
            s->recv_coroutine[i] = qemu_coroutine_self();
            break;
        }
    }

    g_assert(qemu_in_coroutine());
    assert(i < MAX_NBD_REQUESTS);
    request->handle = INDEX_TO_HANDLE(s, i);

    if (!s->ioc) {
        qemu_co_mutex_unlock(&s->send_mutex);
        return -EPIPE;
    }

    s->send_coroutine = qemu_coroutine_self();
    aio_context = bdrv_get_aio_context(bs);

    aio_set_fd_handler(aio_context, s->sioc->fd, false,
                       nbd_reply_ready, nbd_restart_write, bs);
    if (qiov) {
        qio_channel_set_cork(s->ioc, true);
        rc = nbd_send_request(s->ioc, request);
        if (rc >= 0) {
            ret = nbd_wr_syncv(s->ioc, qiov->iov, qiov->niov,
                               offset, request->len, 0);
            if (ret != request->len) {
                rc = -EIO;
            }
        }
        qio_channel_set_cork(s->ioc, false);
    } else {
        rc = nbd_send_request(s->ioc, request);
    }
    aio_set_fd_handler(aio_context, s->sioc->fd, false,
                       nbd_reply_ready, NULL, bs);
    s->send_coroutine = NULL;
    qemu_co_mutex_unlock(&s->send_mutex);
    return rc;
}

static void nbd_co_receive_reply(NbdClientSession *s,
    struct nbd_request *request, struct nbd_reply *reply,
    QEMUIOVector *qiov, int offset)
{
    int ret;

    /* Wait until we're woken up by the read handler.  TODO: perhaps
     * peek at the next reply and avoid yielding if it's ours?  */
    qemu_coroutine_yield();
    *reply = s->reply;
    if (reply->handle != request->handle ||
        !s->ioc) {
        reply->error = EIO;
    } else {
        if (qiov && reply->error == 0) {
            ret = nbd_wr_syncv(s->ioc, qiov->iov, qiov->niov,
                               offset, request->len, 1);
            if (ret != request->len) {
                reply->error = EIO;
            }
        }

        /* Tell the read handler to read another header.  */
        s->reply.handle = 0;
    }
}

static void nbd_coroutine_start(NbdClientSession *s,
   struct nbd_request *request)
{
    /* Poor man semaphore.  The free_sema is locked when no other request
     * can be accepted, and unlocked after receiving one reply.  */
    if (s->in_flight >= MAX_NBD_REQUESTS - 1) {
        qemu_co_mutex_lock(&s->free_sema);
        assert(s->in_flight < MAX_NBD_REQUESTS);
    }
    s->in_flight++;

    /* s->recv_coroutine[i] is set as soon as we get the send_lock.  */
}

static void nbd_coroutine_end(NbdClientSession *s,
    struct nbd_request *request)
{
    int i = HANDLE_TO_INDEX(s, request->handle);
    s->recv_coroutine[i] = NULL;
    if (s->in_flight-- == MAX_NBD_REQUESTS) {
        qemu_co_mutex_unlock(&s->free_sema);
    }
}

static int nbd_co_readv_1(BlockDriverState *bs, int64_t sector_num,
                          int nb_sectors, QEMUIOVector *qiov,
                          int offset)
{
    NbdClientSession *client = nbd_get_client_session(bs);
    struct nbd_request request = { .type = NBD_CMD_READ };
    struct nbd_reply reply;
    ssize_t ret;

    request.from = sector_num * 512;
    request.len = nb_sectors * 512;

    nbd_coroutine_start(client, &request);
    ret = nbd_co_send_request(bs, &request, NULL, 0);
    if (ret < 0) {
        reply.error = -ret;
    } else {
        nbd_co_receive_reply(client, &request, &reply, qiov, offset);
    }
    nbd_coroutine_end(client, &request);
    return -reply.error;

}

static int nbd_co_writev_1(BlockDriverState *bs, int64_t sector_num,
                           int nb_sectors, QEMUIOVector *qiov,
                           int offset)
{
    NbdClientSession *client = nbd_get_client_session(bs);
    struct nbd_request request = { .type = NBD_CMD_WRITE };
    struct nbd_reply reply;
    ssize_t ret;

    if (!bdrv_enable_write_cache(bs) &&
        (client->nbdflags & NBD_FLAG_SEND_FUA)) {
        request.type |= NBD_CMD_FLAG_FUA;
    }

    request.from = sector_num * 512;
    request.len = nb_sectors * 512;

    nbd_coroutine_start(client, &request);
    ret = nbd_co_send_request(bs, &request, qiov, offset);
    if (ret < 0) {
        reply.error = -ret;
    } else {
        nbd_co_receive_reply(client, &request, &reply, NULL, 0);
    }
    nbd_coroutine_end(client, &request);
    return -reply.error;
}

/* qemu-nbd has a limit of slightly less than 1M per request.  Try to
 * remain aligned to 4K. */
#define NBD_MAX_SECTORS 2040

int nbd_client_co_readv(BlockDriverState *bs, int64_t sector_num,
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

int nbd_client_co_writev(BlockDriverState *bs, int64_t sector_num,
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

int nbd_client_co_flush(BlockDriverState *bs)
{
    NbdClientSession *client = nbd_get_client_session(bs);
    struct nbd_request request = { .type = NBD_CMD_FLUSH };
    struct nbd_reply reply;
    ssize_t ret;

    if (!(client->nbdflags & NBD_FLAG_SEND_FLUSH)) {
        return 0;
    }

    if (client->nbdflags & NBD_FLAG_SEND_FUA) {
        request.type |= NBD_CMD_FLAG_FUA;
    }

    request.from = 0;
    request.len = 0;

    nbd_coroutine_start(client, &request);
    ret = nbd_co_send_request(bs, &request, NULL, 0);
    if (ret < 0) {
        reply.error = -ret;
    } else {
        nbd_co_receive_reply(client, &request, &reply, NULL, 0);
    }
    nbd_coroutine_end(client, &request);
    return -reply.error;
}

int nbd_client_co_discard(BlockDriverState *bs, int64_t sector_num,
                          int nb_sectors)
{
    NbdClientSession *client = nbd_get_client_session(bs);
    struct nbd_request request = { .type = NBD_CMD_TRIM };
    struct nbd_reply reply;
    ssize_t ret;

    if (!(client->nbdflags & NBD_FLAG_SEND_TRIM)) {
        return 0;
    }
    request.from = sector_num * 512;
    request.len = nb_sectors * 512;

    nbd_coroutine_start(client, &request);
    ret = nbd_co_send_request(bs, &request, NULL, 0);
    if (ret < 0) {
        reply.error = -ret;
    } else {
        nbd_co_receive_reply(client, &request, &reply, NULL, 0);
    }
    nbd_coroutine_end(client, &request);
    return -reply.error;

}

void nbd_client_detach_aio_context(BlockDriverState *bs)
{
    aio_set_fd_handler(bdrv_get_aio_context(bs),
                       nbd_get_client_session(bs)->sioc->fd,
                       false, NULL, NULL, NULL);
}

void nbd_client_attach_aio_context(BlockDriverState *bs,
                                   AioContext *new_context)
{
    aio_set_fd_handler(new_context, nbd_get_client_session(bs)->sioc->fd,
                       false, nbd_reply_ready, NULL, bs);
}

void nbd_client_close(BlockDriverState *bs)
{
    NbdClientSession *client = nbd_get_client_session(bs);
    struct nbd_request request = {
        .type = NBD_CMD_DISC,
        .from = 0,
        .len = 0
    };

    if (client->ioc == NULL) {
        return;
    }

    nbd_send_request(client->ioc, &request);

    nbd_teardown_connection(bs);
}

int nbd_client_init(BlockDriverState *bs,
                    QIOChannelSocket *sioc,
                    const char *export,
                    QCryptoTLSCreds *tlscreds,
                    const char *hostname,
                    Error **errp)
{
    NbdClientSession *client = nbd_get_client_session(bs);
    int ret;

    /* NBD handshake */
    logout("session init %s\n", export);
    qio_channel_set_blocking(QIO_CHANNEL(sioc), true, NULL);

    ret = nbd_receive_negotiate(QIO_CHANNEL(sioc), export,
                                &client->nbdflags,
                                tlscreds, hostname,
                                &client->ioc,
                                &client->size, errp);
    if (ret < 0) {
        logout("Failed to negotiate with the NBD server\n");
        return ret;
    }

    qemu_co_mutex_init(&client->send_mutex);
    qemu_co_mutex_init(&client->free_sema);
    client->sioc = sioc;
    object_ref(OBJECT(client->sioc));

    if (!client->ioc) {
        client->ioc = QIO_CHANNEL(sioc);
        object_ref(OBJECT(client->ioc));
    }

    /* Now that we're connected, set the socket to be non-blocking and
     * kick the reply mechanism.  */
    qio_channel_set_blocking(QIO_CHANNEL(sioc), false, NULL);

    nbd_client_attach_aio_context(bs, bdrv_get_aio_context(bs));

    logout("Established connection with NBD server\n");
    return 0;
}
