/*
 * QEMU Block driver for  NBD
 *
 * Copyright (C) 2016 Red Hat, Inc.
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
#include "qapi/error.h"
#include "nbd-client.h"

#define HANDLE_TO_INDEX(bs, handle) ((handle) ^ ((uint64_t)(intptr_t)bs))
#define INDEX_TO_HANDLE(bs, index)  ((index)  ^ ((uint64_t)(intptr_t)bs))

static void nbd_recv_coroutines_enter_all(NBDClientSession *s)
{
    int i;

    for (i = 0; i < MAX_NBD_REQUESTS; i++) {
        NBDClientRequest *req = &s->requests[i];

        if (req->coroutine && req->receiving) {
            aio_co_wake(req->coroutine);
        }
    }
}

static void nbd_teardown_connection(BlockDriverState *bs)
{
    NBDClientSession *client = nbd_get_client_session(bs);

    if (!client->ioc) { /* Already closed */
        return;
    }

    /* finish any pending coroutines */
    qio_channel_shutdown(client->ioc,
                         QIO_CHANNEL_SHUTDOWN_BOTH,
                         NULL);
    BDRV_POLL_WHILE(bs, client->read_reply_co);

    nbd_client_detach_aio_context(bs);
    object_unref(OBJECT(client->sioc));
    client->sioc = NULL;
    object_unref(OBJECT(client->ioc));
    client->ioc = NULL;
}

static coroutine_fn void nbd_read_reply_entry(void *opaque)
{
    NBDClientSession *s = opaque;
    uint64_t i;
    int ret = 0;
    Error *local_err = NULL;

    while (!s->quit) {
        assert(s->reply.handle == 0);
        ret = nbd_receive_reply(s->ioc, &s->reply, &local_err);
        if (ret < 0) {
            error_report_err(local_err);
        }
        if (ret <= 0) {
            break;
        }

        /* There's no need for a mutex on the receive side, because the
         * handler acts as a synchronization point and ensures that only
         * one coroutine is called until the reply finishes.
         */
        i = HANDLE_TO_INDEX(s, s->reply.handle);
        if (i >= MAX_NBD_REQUESTS ||
            !s->requests[i].coroutine ||
            !s->requests[i].receiving) {
            break;
        }

        /* We're woken up again by the request itself.  Note that there
         * is no race between yielding and reentering read_reply_co.  This
         * is because:
         *
         * - if the request runs on the same AioContext, it is only
         *   entered after we yield
         *
         * - if the request runs on a different AioContext, reentering
         *   read_reply_co happens through a bottom half, which can only
         *   run after we yield.
         */
        aio_co_wake(s->requests[i].coroutine);
        qemu_coroutine_yield();
    }

    s->quit = true;
    nbd_recv_coroutines_enter_all(s);
    s->read_reply_co = NULL;
}

static int nbd_co_send_request(BlockDriverState *bs,
                               NBDRequest *request,
                               QEMUIOVector *qiov)
{
    NBDClientSession *s = nbd_get_client_session(bs);
    int rc, ret, i;

    qemu_co_mutex_lock(&s->send_mutex);
    while (s->in_flight == MAX_NBD_REQUESTS) {
        qemu_co_queue_wait(&s->free_sema, &s->send_mutex);
    }
    s->in_flight++;

    for (i = 0; i < MAX_NBD_REQUESTS; i++) {
        if (s->requests[i].coroutine == NULL) {
            break;
        }
    }

    g_assert(qemu_in_coroutine());
    assert(i < MAX_NBD_REQUESTS);

    s->requests[i].coroutine = qemu_coroutine_self();
    s->requests[i].receiving = false;

    request->handle = INDEX_TO_HANDLE(s, i);

    if (s->quit) {
        rc = -EIO;
        goto err;
    }
    if (!s->ioc) {
        rc = -EPIPE;
        goto err;
    }

    if (qiov) {
        qio_channel_set_cork(s->ioc, true);
        rc = nbd_send_request(s->ioc, request);
        if (rc >= 0 && !s->quit) {
            ret = nbd_rwv(s->ioc, qiov->iov, qiov->niov, request->len, false,
                          NULL);
            if (ret != request->len) {
                rc = -EIO;
            }
        }
        qio_channel_set_cork(s->ioc, false);
    } else {
        rc = nbd_send_request(s->ioc, request);
    }

err:
    if (rc < 0) {
        s->quit = true;
        s->requests[i].coroutine = NULL;
        s->in_flight--;
        qemu_co_queue_next(&s->free_sema);
    }
    qemu_co_mutex_unlock(&s->send_mutex);
    return rc;
}

static void nbd_co_receive_reply(NBDClientSession *s,
                                 NBDRequest *request,
                                 NBDReply *reply,
                                 QEMUIOVector *qiov)
{
    int i = HANDLE_TO_INDEX(s, request->handle);
    int ret;

    /* Wait until we're woken up by nbd_read_reply_entry.  */
    s->requests[i].receiving = true;
    qemu_coroutine_yield();
    s->requests[i].receiving = false;
    *reply = s->reply;
    if (reply->handle != request->handle || !s->ioc || s->quit) {
        reply->error = EIO;
    } else {
        if (qiov && reply->error == 0) {
            ret = nbd_rwv(s->ioc, qiov->iov, qiov->niov, request->len, true,
                          NULL);
            if (ret != request->len) {
                reply->error = EIO;
                s->quit = true;
            }
        }

        /* Tell the read handler to read another header.  */
        s->reply.handle = 0;
    }

    s->requests[i].coroutine = NULL;

    /* Kick the read_reply_co to get the next reply.  */
    if (s->read_reply_co) {
        aio_co_wake(s->read_reply_co);
    }

    qemu_co_mutex_lock(&s->send_mutex);
    s->in_flight--;
    qemu_co_queue_next(&s->free_sema);
    qemu_co_mutex_unlock(&s->send_mutex);
}

int nbd_client_co_preadv(BlockDriverState *bs, uint64_t offset,
                         uint64_t bytes, QEMUIOVector *qiov, int flags)
{
    NBDClientSession *client = nbd_get_client_session(bs);
    NBDRequest request = {
        .type = NBD_CMD_READ,
        .from = offset,
        .len = bytes,
    };
    NBDReply reply;
    ssize_t ret;

    assert(bytes <= NBD_MAX_BUFFER_SIZE);
    assert(!flags);

    ret = nbd_co_send_request(bs, &request, NULL);
    if (ret < 0) {
        reply.error = -ret;
    } else {
        nbd_co_receive_reply(client, &request, &reply, qiov);
    }
    return -reply.error;
}

int nbd_client_co_pwritev(BlockDriverState *bs, uint64_t offset,
                          uint64_t bytes, QEMUIOVector *qiov, int flags)
{
    NBDClientSession *client = nbd_get_client_session(bs);
    NBDRequest request = {
        .type = NBD_CMD_WRITE,
        .from = offset,
        .len = bytes,
    };
    NBDReply reply;
    ssize_t ret;

    if (flags & BDRV_REQ_FUA) {
        assert(client->info.flags & NBD_FLAG_SEND_FUA);
        request.flags |= NBD_CMD_FLAG_FUA;
    }

    assert(bytes <= NBD_MAX_BUFFER_SIZE);

    ret = nbd_co_send_request(bs, &request, qiov);
    if (ret < 0) {
        reply.error = -ret;
    } else {
        nbd_co_receive_reply(client, &request, &reply, NULL);
    }
    return -reply.error;
}

int nbd_client_co_pwrite_zeroes(BlockDriverState *bs, int64_t offset,
                                int bytes, BdrvRequestFlags flags)
{
    ssize_t ret;
    NBDClientSession *client = nbd_get_client_session(bs);
    NBDRequest request = {
        .type = NBD_CMD_WRITE_ZEROES,
        .from = offset,
        .len = bytes,
    };
    NBDReply reply;

    if (!(client->info.flags & NBD_FLAG_SEND_WRITE_ZEROES)) {
        return -ENOTSUP;
    }

    if (flags & BDRV_REQ_FUA) {
        assert(client->info.flags & NBD_FLAG_SEND_FUA);
        request.flags |= NBD_CMD_FLAG_FUA;
    }
    if (!(flags & BDRV_REQ_MAY_UNMAP)) {
        request.flags |= NBD_CMD_FLAG_NO_HOLE;
    }

    ret = nbd_co_send_request(bs, &request, NULL);
    if (ret < 0) {
        reply.error = -ret;
    } else {
        nbd_co_receive_reply(client, &request, &reply, NULL);
    }
    return -reply.error;
}

int nbd_client_co_flush(BlockDriverState *bs)
{
    NBDClientSession *client = nbd_get_client_session(bs);
    NBDRequest request = { .type = NBD_CMD_FLUSH };
    NBDReply reply;
    ssize_t ret;

    if (!(client->info.flags & NBD_FLAG_SEND_FLUSH)) {
        return 0;
    }

    request.from = 0;
    request.len = 0;

    ret = nbd_co_send_request(bs, &request, NULL);
    if (ret < 0) {
        reply.error = -ret;
    } else {
        nbd_co_receive_reply(client, &request, &reply, NULL);
    }
    return -reply.error;
}

int nbd_client_co_pdiscard(BlockDriverState *bs, int64_t offset, int bytes)
{
    NBDClientSession *client = nbd_get_client_session(bs);
    NBDRequest request = {
        .type = NBD_CMD_TRIM,
        .from = offset,
        .len = bytes,
    };
    NBDReply reply;
    ssize_t ret;

    if (!(client->info.flags & NBD_FLAG_SEND_TRIM)) {
        return 0;
    }

    ret = nbd_co_send_request(bs, &request, NULL);
    if (ret < 0) {
        reply.error = -ret;
    } else {
        nbd_co_receive_reply(client, &request, &reply, NULL);
    }
    return -reply.error;

}

void nbd_client_detach_aio_context(BlockDriverState *bs)
{
    NBDClientSession *client = nbd_get_client_session(bs);
    qio_channel_detach_aio_context(QIO_CHANNEL(client->ioc));
}

void nbd_client_attach_aio_context(BlockDriverState *bs,
                                   AioContext *new_context)
{
    NBDClientSession *client = nbd_get_client_session(bs);
    qio_channel_attach_aio_context(QIO_CHANNEL(client->ioc), new_context);
    aio_co_schedule(new_context, client->read_reply_co);
}

void nbd_client_close(BlockDriverState *bs)
{
    NBDClientSession *client = nbd_get_client_session(bs);
    NBDRequest request = { .type = NBD_CMD_DISC };

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
    NBDClientSession *client = nbd_get_client_session(bs);
    int ret;

    /* NBD handshake */
    logout("session init %s\n", export);
    qio_channel_set_blocking(QIO_CHANNEL(sioc), true, NULL);

    client->info.request_sizes = true;
    ret = nbd_receive_negotiate(QIO_CHANNEL(sioc), export,
                                tlscreds, hostname,
                                &client->ioc, &client->info, errp);
    if (ret < 0) {
        logout("Failed to negotiate with the NBD server\n");
        return ret;
    }
    if (client->info.flags & NBD_FLAG_SEND_FUA) {
        bs->supported_write_flags = BDRV_REQ_FUA;
        bs->supported_zero_flags |= BDRV_REQ_FUA;
    }
    if (client->info.flags & NBD_FLAG_SEND_WRITE_ZEROES) {
        bs->supported_zero_flags |= BDRV_REQ_MAY_UNMAP;
    }
    if (client->info.min_block > bs->bl.request_alignment) {
        bs->bl.request_alignment = client->info.min_block;
    }

    qemu_co_mutex_init(&client->send_mutex);
    qemu_co_queue_init(&client->free_sema);
    client->sioc = sioc;
    object_ref(OBJECT(client->sioc));

    if (!client->ioc) {
        client->ioc = QIO_CHANNEL(sioc);
        object_ref(OBJECT(client->ioc));
    }

    /* Now that we're connected, set the socket to be non-blocking and
     * kick the reply mechanism.  */
    qio_channel_set_blocking(QIO_CHANNEL(sioc), false, NULL);
    client->read_reply_co = qemu_coroutine_create(nbd_read_reply_entry, client);
    nbd_client_attach_aio_context(bs, bdrv_get_aio_context(bs));

    logout("Established connection with NBD server\n");
    return 0;
}
