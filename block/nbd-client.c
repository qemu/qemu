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

#include "trace.h"
#include "qapi/error.h"
#include "nbd-client.h"

#define HANDLE_TO_INDEX(bs, handle) ((handle) ^ (uint64_t)(intptr_t)(bs))
#define INDEX_TO_HANDLE(bs, index)  ((index)  ^ (uint64_t)(intptr_t)(bs))

static void nbd_recv_coroutines_wake_all(NBDClientSession *s)
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

    assert(client->ioc);

    /* finish any pending coroutines */
    qio_channel_shutdown(client->ioc,
                         QIO_CHANNEL_SHUTDOWN_BOTH,
                         NULL);
    BDRV_POLL_WHILE(bs, client->connection_co);

    nbd_client_detach_aio_context(bs);
    object_unref(OBJECT(client->sioc));
    client->sioc = NULL;
    object_unref(OBJECT(client->ioc));
    client->ioc = NULL;
}

static coroutine_fn void nbd_connection_entry(void *opaque)
{
    NBDClientSession *s = opaque;
    uint64_t i;
    int ret = 0;
    Error *local_err = NULL;

    while (!s->quit) {
        /*
         * The NBD client can only really be considered idle when it has
         * yielded from qio_channel_readv_all_eof(), waiting for data. This is
         * the point where the additional scheduled coroutine entry happens
         * after nbd_client_attach_aio_context().
         *
         * Therefore we keep an additional in_flight reference all the time and
         * only drop it temporarily here.
         */
        assert(s->reply.handle == 0);
        ret = nbd_receive_reply(s->bs, s->ioc, &s->reply, &local_err);

        if (local_err) {
            trace_nbd_read_reply_entry_fail(ret, error_get_pretty(local_err));
            error_free(local_err);
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
            !s->requests[i].receiving ||
            (nbd_reply_is_structured(&s->reply) && !s->info.structured_reply))
        {
            break;
        }

        /* We're woken up again by the request itself.  Note that there
         * is no race between yielding and reentering connection_co.  This
         * is because:
         *
         * - if the request runs on the same AioContext, it is only
         *   entered after we yield
         *
         * - if the request runs on a different AioContext, reentering
         *   connection_co happens through a bottom half, which can only
         *   run after we yield.
         */
        aio_co_wake(s->requests[i].coroutine);
        qemu_coroutine_yield();
    }

    s->quit = true;
    nbd_recv_coroutines_wake_all(s);
    bdrv_dec_in_flight(s->bs);

    s->connection_co = NULL;
    aio_wait_kick();
}

static int nbd_co_send_request(BlockDriverState *bs,
                               NBDRequest *request,
                               QEMUIOVector *qiov)
{
    NBDClientSession *s = nbd_get_client_session(bs);
    int rc, i;

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
    s->requests[i].offset = request->from;
    s->requests[i].receiving = false;

    request->handle = INDEX_TO_HANDLE(s, i);

    if (s->quit) {
        rc = -EIO;
        goto err;
    }
    assert(s->ioc);

    if (qiov) {
        qio_channel_set_cork(s->ioc, true);
        rc = nbd_send_request(s->ioc, request);
        if (rc >= 0 && !s->quit) {
            if (qio_channel_writev_all(s->ioc, qiov->iov, qiov->niov,
                                       NULL) < 0) {
                rc = -EIO;
            }
        } else if (rc >= 0) {
            rc = -EIO;
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

static inline uint16_t payload_advance16(uint8_t **payload)
{
    *payload += 2;
    return lduw_be_p(*payload - 2);
}

static inline uint32_t payload_advance32(uint8_t **payload)
{
    *payload += 4;
    return ldl_be_p(*payload - 4);
}

static inline uint64_t payload_advance64(uint8_t **payload)
{
    *payload += 8;
    return ldq_be_p(*payload - 8);
}

static int nbd_parse_offset_hole_payload(NBDClientSession *client,
                                         NBDStructuredReplyChunk *chunk,
                                         uint8_t *payload, uint64_t orig_offset,
                                         QEMUIOVector *qiov, Error **errp)
{
    uint64_t offset;
    uint32_t hole_size;

    if (chunk->length != sizeof(offset) + sizeof(hole_size)) {
        error_setg(errp, "Protocol error: invalid payload for "
                         "NBD_REPLY_TYPE_OFFSET_HOLE");
        return -EINVAL;
    }

    offset = payload_advance64(&payload);
    hole_size = payload_advance32(&payload);

    if (!hole_size || offset < orig_offset || hole_size > qiov->size ||
        offset > orig_offset + qiov->size - hole_size) {
        error_setg(errp, "Protocol error: server sent chunk exceeding requested"
                         " region");
        return -EINVAL;
    }
    if (client->info.min_block &&
        !QEMU_IS_ALIGNED(hole_size, client->info.min_block)) {
        trace_nbd_structured_read_compliance("hole");
    }

    qemu_iovec_memset(qiov, offset - orig_offset, 0, hole_size);

    return 0;
}

/* nbd_parse_blockstatus_payload
 * Based on our request, we expect only one extent in reply, for the
 * base:allocation context.
 */
static int nbd_parse_blockstatus_payload(NBDClientSession *client,
                                         NBDStructuredReplyChunk *chunk,
                                         uint8_t *payload, uint64_t orig_length,
                                         NBDExtent *extent, Error **errp)
{
    uint32_t context_id;

    /* The server succeeded, so it must have sent [at least] one extent */
    if (chunk->length < sizeof(context_id) + sizeof(*extent)) {
        error_setg(errp, "Protocol error: invalid payload for "
                         "NBD_REPLY_TYPE_BLOCK_STATUS");
        return -EINVAL;
    }

    context_id = payload_advance32(&payload);
    if (client->info.context_id != context_id) {
        error_setg(errp, "Protocol error: unexpected context id %d for "
                         "NBD_REPLY_TYPE_BLOCK_STATUS, when negotiated context "
                         "id is %d", context_id,
                         client->info.context_id);
        return -EINVAL;
    }

    extent->length = payload_advance32(&payload);
    extent->flags = payload_advance32(&payload);

    if (extent->length == 0) {
        error_setg(errp, "Protocol error: server sent status chunk with "
                   "zero length");
        return -EINVAL;
    }

    /*
     * A server sending unaligned block status is in violation of the
     * protocol, but as qemu-nbd 3.1 is such a server (at least for
     * POSIX files that are not a multiple of 512 bytes, since qemu
     * rounds files up to 512-byte multiples but lseek(SEEK_HOLE)
     * still sees an implicit hole beyond the real EOF), it's nicer to
     * work around the misbehaving server. If the request included
     * more than the final unaligned block, truncate it back to an
     * aligned result; if the request was only the final block, round
     * up to the full block and change the status to fully-allocated
     * (always a safe status, even if it loses information).
     */
    if (client->info.min_block && !QEMU_IS_ALIGNED(extent->length,
                                                   client->info.min_block)) {
        trace_nbd_parse_blockstatus_compliance("extent length is unaligned");
        if (extent->length > client->info.min_block) {
            extent->length = QEMU_ALIGN_DOWN(extent->length,
                                             client->info.min_block);
        } else {
            extent->length = client->info.min_block;
            extent->flags = 0;
        }
    }

    /*
     * We used NBD_CMD_FLAG_REQ_ONE, so the server should not have
     * sent us any more than one extent, nor should it have included
     * status beyond our request in that extent. However, it's easy
     * enough to ignore the server's noncompliance without killing the
     * connection; just ignore trailing extents, and clamp things to
     * the length of our request.
     */
    if (chunk->length > sizeof(context_id) + sizeof(*extent)) {
        trace_nbd_parse_blockstatus_compliance("more than one extent");
    }
    if (extent->length > orig_length) {
        extent->length = orig_length;
        trace_nbd_parse_blockstatus_compliance("extent length too large");
    }

    return 0;
}

/* nbd_parse_error_payload
 * on success @errp contains message describing nbd error reply
 */
static int nbd_parse_error_payload(NBDStructuredReplyChunk *chunk,
                                   uint8_t *payload, int *request_ret,
                                   Error **errp)
{
    uint32_t error;
    uint16_t message_size;

    assert(chunk->type & (1 << 15));

    if (chunk->length < sizeof(error) + sizeof(message_size)) {
        error_setg(errp,
                   "Protocol error: invalid payload for structured error");
        return -EINVAL;
    }

    error = nbd_errno_to_system_errno(payload_advance32(&payload));
    if (error == 0) {
        error_setg(errp, "Protocol error: server sent structured error chunk "
                         "with error = 0");
        return -EINVAL;
    }

    *request_ret = -error;
    message_size = payload_advance16(&payload);

    if (message_size > chunk->length - sizeof(error) - sizeof(message_size)) {
        error_setg(errp, "Protocol error: server sent structured error chunk "
                         "with incorrect message size");
        return -EINVAL;
    }

    /* TODO: Add a trace point to mention the server complaint */

    /* TODO handle ERROR_OFFSET */

    return 0;
}

static int nbd_co_receive_offset_data_payload(NBDClientSession *s,
                                              uint64_t orig_offset,
                                              QEMUIOVector *qiov, Error **errp)
{
    QEMUIOVector sub_qiov;
    uint64_t offset;
    size_t data_size;
    int ret;
    NBDStructuredReplyChunk *chunk = &s->reply.structured;

    assert(nbd_reply_is_structured(&s->reply));

    /* The NBD spec requires at least one byte of payload */
    if (chunk->length <= sizeof(offset)) {
        error_setg(errp, "Protocol error: invalid payload for "
                         "NBD_REPLY_TYPE_OFFSET_DATA");
        return -EINVAL;
    }

    if (nbd_read64(s->ioc, &offset, "OFFSET_DATA offset", errp) < 0) {
        return -EIO;
    }

    data_size = chunk->length - sizeof(offset);
    assert(data_size);
    if (offset < orig_offset || data_size > qiov->size ||
        offset > orig_offset + qiov->size - data_size) {
        error_setg(errp, "Protocol error: server sent chunk exceeding requested"
                         " region");
        return -EINVAL;
    }
    if (s->info.min_block && !QEMU_IS_ALIGNED(data_size, s->info.min_block)) {
        trace_nbd_structured_read_compliance("data");
    }

    qemu_iovec_init(&sub_qiov, qiov->niov);
    qemu_iovec_concat(&sub_qiov, qiov, offset - orig_offset, data_size);
    ret = qio_channel_readv_all(s->ioc, sub_qiov.iov, sub_qiov.niov, errp);
    qemu_iovec_destroy(&sub_qiov);

    return ret < 0 ? -EIO : 0;
}

#define NBD_MAX_MALLOC_PAYLOAD 1000
/* nbd_co_receive_structured_payload
 */
static coroutine_fn int nbd_co_receive_structured_payload(
        NBDClientSession *s, void **payload, Error **errp)
{
    int ret;
    uint32_t len;

    assert(nbd_reply_is_structured(&s->reply));

    len = s->reply.structured.length;

    if (len == 0) {
        return 0;
    }

    if (payload == NULL) {
        error_setg(errp, "Unexpected structured payload");
        return -EINVAL;
    }

    if (len > NBD_MAX_MALLOC_PAYLOAD) {
        error_setg(errp, "Payload too large");
        return -EINVAL;
    }

    *payload = g_new(char, len);
    ret = nbd_read(s->ioc, *payload, len, "structured payload", errp);
    if (ret < 0) {
        g_free(*payload);
        *payload = NULL;
        return ret;
    }

    return 0;
}

/* nbd_co_do_receive_one_chunk
 * for simple reply:
 *   set request_ret to received reply error
 *   if qiov is not NULL: read payload to @qiov
 * for structured reply chunk:
 *   if error chunk: read payload, set @request_ret, do not set @payload
 *   else if offset_data chunk: read payload data to @qiov, do not set @payload
 *   else: read payload to @payload
 *
 * If function fails, @errp contains corresponding error message, and the
 * connection with the server is suspect.  If it returns 0, then the
 * transaction succeeded (although @request_ret may be a negative errno
 * corresponding to the server's error reply), and errp is unchanged.
 */
static coroutine_fn int nbd_co_do_receive_one_chunk(
        NBDClientSession *s, uint64_t handle, bool only_structured,
        int *request_ret, QEMUIOVector *qiov, void **payload, Error **errp)
{
    int ret;
    int i = HANDLE_TO_INDEX(s, handle);
    void *local_payload = NULL;
    NBDStructuredReplyChunk *chunk;

    if (payload) {
        *payload = NULL;
    }
    *request_ret = 0;

    /* Wait until we're woken up by nbd_connection_entry.  */
    s->requests[i].receiving = true;
    qemu_coroutine_yield();
    s->requests[i].receiving = false;
    if (s->quit) {
        error_setg(errp, "Connection closed");
        return -EIO;
    }
    assert(s->ioc);

    assert(s->reply.handle == handle);

    if (nbd_reply_is_simple(&s->reply)) {
        if (only_structured) {
            error_setg(errp, "Protocol error: simple reply when structured "
                             "reply chunk was expected");
            return -EINVAL;
        }

        *request_ret = -nbd_errno_to_system_errno(s->reply.simple.error);
        if (*request_ret < 0 || !qiov) {
            return 0;
        }

        return qio_channel_readv_all(s->ioc, qiov->iov, qiov->niov,
                                     errp) < 0 ? -EIO : 0;
    }

    /* handle structured reply chunk */
    assert(s->info.structured_reply);
    chunk = &s->reply.structured;

    if (chunk->type == NBD_REPLY_TYPE_NONE) {
        if (!(chunk->flags & NBD_REPLY_FLAG_DONE)) {
            error_setg(errp, "Protocol error: NBD_REPLY_TYPE_NONE chunk without"
                       " NBD_REPLY_FLAG_DONE flag set");
            return -EINVAL;
        }
        if (chunk->length) {
            error_setg(errp, "Protocol error: NBD_REPLY_TYPE_NONE chunk with"
                       " nonzero length");
            return -EINVAL;
        }
        return 0;
    }

    if (chunk->type == NBD_REPLY_TYPE_OFFSET_DATA) {
        if (!qiov) {
            error_setg(errp, "Unexpected NBD_REPLY_TYPE_OFFSET_DATA chunk");
            return -EINVAL;
        }

        return nbd_co_receive_offset_data_payload(s, s->requests[i].offset,
                                                  qiov, errp);
    }

    if (nbd_reply_type_is_error(chunk->type)) {
        payload = &local_payload;
    }

    ret = nbd_co_receive_structured_payload(s, payload, errp);
    if (ret < 0) {
        return ret;
    }

    if (nbd_reply_type_is_error(chunk->type)) {
        ret = nbd_parse_error_payload(chunk, local_payload, request_ret, errp);
        g_free(local_payload);
        return ret;
    }

    return 0;
}

/* nbd_co_receive_one_chunk
 * Read reply, wake up connection_co and set s->quit if needed.
 * Return value is a fatal error code or normal nbd reply error code
 */
static coroutine_fn int nbd_co_receive_one_chunk(
        NBDClientSession *s, uint64_t handle, bool only_structured,
        int *request_ret, QEMUIOVector *qiov, NBDReply *reply, void **payload,
        Error **errp)
{
    int ret = nbd_co_do_receive_one_chunk(s, handle, only_structured,
                                          request_ret, qiov, payload, errp);

    if (ret < 0) {
        s->quit = true;
    } else {
        /* For assert at loop start in nbd_connection_entry */
        if (reply) {
            *reply = s->reply;
        }
        s->reply.handle = 0;
    }

    if (s->connection_co) {
        aio_co_wake(s->connection_co);
    }

    return ret;
}

typedef struct NBDReplyChunkIter {
    int ret;
    int request_ret;
    Error *err;
    bool done, only_structured;
} NBDReplyChunkIter;

static void nbd_iter_channel_error(NBDReplyChunkIter *iter,
                                   int ret, Error **local_err)
{
    assert(ret < 0);

    if (!iter->ret) {
        iter->ret = ret;
        error_propagate(&iter->err, *local_err);
    } else {
        error_free(*local_err);
    }

    *local_err = NULL;
}

static void nbd_iter_request_error(NBDReplyChunkIter *iter, int ret)
{
    assert(ret < 0);

    if (!iter->request_ret) {
        iter->request_ret = ret;
    }
}

/* NBD_FOREACH_REPLY_CHUNK
 */
#define NBD_FOREACH_REPLY_CHUNK(s, iter, handle, structured, \
                                qiov, reply, payload) \
    for (iter = (NBDReplyChunkIter) { .only_structured = structured }; \
         nbd_reply_chunk_iter_receive(s, &iter, handle, qiov, reply, payload);)

/* nbd_reply_chunk_iter_receive
 */
static bool nbd_reply_chunk_iter_receive(NBDClientSession *s,
                                         NBDReplyChunkIter *iter,
                                         uint64_t handle,
                                         QEMUIOVector *qiov, NBDReply *reply,
                                         void **payload)
{
    int ret, request_ret;
    NBDReply local_reply;
    NBDStructuredReplyChunk *chunk;
    Error *local_err = NULL;
    if (s->quit) {
        error_setg(&local_err, "Connection closed");
        nbd_iter_channel_error(iter, -EIO, &local_err);
        goto break_loop;
    }

    if (iter->done) {
        /* Previous iteration was last. */
        goto break_loop;
    }

    if (reply == NULL) {
        reply = &local_reply;
    }

    ret = nbd_co_receive_one_chunk(s, handle, iter->only_structured,
                                   &request_ret, qiov, reply, payload,
                                   &local_err);
    if (ret < 0) {
        nbd_iter_channel_error(iter, ret, &local_err);
    } else if (request_ret < 0) {
        nbd_iter_request_error(iter, request_ret);
    }

    /* Do not execute the body of NBD_FOREACH_REPLY_CHUNK for simple reply. */
    if (nbd_reply_is_simple(reply) || s->quit) {
        goto break_loop;
    }

    chunk = &reply->structured;
    iter->only_structured = true;

    if (chunk->type == NBD_REPLY_TYPE_NONE) {
        /* NBD_REPLY_FLAG_DONE is already checked in nbd_co_receive_one_chunk */
        assert(chunk->flags & NBD_REPLY_FLAG_DONE);
        goto break_loop;
    }

    if (chunk->flags & NBD_REPLY_FLAG_DONE) {
        /* This iteration is last. */
        iter->done = true;
    }

    /* Execute the loop body */
    return true;

break_loop:
    s->requests[HANDLE_TO_INDEX(s, handle)].coroutine = NULL;

    qemu_co_mutex_lock(&s->send_mutex);
    s->in_flight--;
    qemu_co_queue_next(&s->free_sema);
    qemu_co_mutex_unlock(&s->send_mutex);

    return false;
}

static int nbd_co_receive_return_code(NBDClientSession *s, uint64_t handle,
                                      int *request_ret, Error **errp)
{
    NBDReplyChunkIter iter;

    NBD_FOREACH_REPLY_CHUNK(s, iter, handle, false, NULL, NULL, NULL) {
        /* nbd_reply_chunk_iter_receive does all the work */
    }

    error_propagate(errp, iter.err);
    *request_ret = iter.request_ret;
    return iter.ret;
}

static int nbd_co_receive_cmdread_reply(NBDClientSession *s, uint64_t handle,
                                        uint64_t offset, QEMUIOVector *qiov,
                                        int *request_ret, Error **errp)
{
    NBDReplyChunkIter iter;
    NBDReply reply;
    void *payload = NULL;
    Error *local_err = NULL;

    NBD_FOREACH_REPLY_CHUNK(s, iter, handle, s->info.structured_reply,
                            qiov, &reply, &payload)
    {
        int ret;
        NBDStructuredReplyChunk *chunk = &reply.structured;

        assert(nbd_reply_is_structured(&reply));

        switch (chunk->type) {
        case NBD_REPLY_TYPE_OFFSET_DATA:
            /* special cased in nbd_co_receive_one_chunk, data is already
             * in qiov */
            break;
        case NBD_REPLY_TYPE_OFFSET_HOLE:
            ret = nbd_parse_offset_hole_payload(s, &reply.structured, payload,
                                                offset, qiov, &local_err);
            if (ret < 0) {
                s->quit = true;
                nbd_iter_channel_error(&iter, ret, &local_err);
            }
            break;
        default:
            if (!nbd_reply_type_is_error(chunk->type)) {
                /* not allowed reply type */
                s->quit = true;
                error_setg(&local_err,
                           "Unexpected reply type: %d (%s) for CMD_READ",
                           chunk->type, nbd_reply_type_lookup(chunk->type));
                nbd_iter_channel_error(&iter, -EINVAL, &local_err);
            }
        }

        g_free(payload);
        payload = NULL;
    }

    error_propagate(errp, iter.err);
    *request_ret = iter.request_ret;
    return iter.ret;
}

static int nbd_co_receive_blockstatus_reply(NBDClientSession *s,
                                            uint64_t handle, uint64_t length,
                                            NBDExtent *extent,
                                            int *request_ret, Error **errp)
{
    NBDReplyChunkIter iter;
    NBDReply reply;
    void *payload = NULL;
    Error *local_err = NULL;
    bool received = false;

    assert(!extent->length);
    NBD_FOREACH_REPLY_CHUNK(s, iter, handle, false, NULL, &reply, &payload) {
        int ret;
        NBDStructuredReplyChunk *chunk = &reply.structured;

        assert(nbd_reply_is_structured(&reply));

        switch (chunk->type) {
        case NBD_REPLY_TYPE_BLOCK_STATUS:
            if (received) {
                s->quit = true;
                error_setg(&local_err, "Several BLOCK_STATUS chunks in reply");
                nbd_iter_channel_error(&iter, -EINVAL, &local_err);
            }
            received = true;

            ret = nbd_parse_blockstatus_payload(s, &reply.structured,
                                                payload, length, extent,
                                                &local_err);
            if (ret < 0) {
                s->quit = true;
                nbd_iter_channel_error(&iter, ret, &local_err);
            }
            break;
        default:
            if (!nbd_reply_type_is_error(chunk->type)) {
                s->quit = true;
                error_setg(&local_err,
                           "Unexpected reply type: %d (%s) "
                           "for CMD_BLOCK_STATUS",
                           chunk->type, nbd_reply_type_lookup(chunk->type));
                nbd_iter_channel_error(&iter, -EINVAL, &local_err);
            }
        }

        g_free(payload);
        payload = NULL;
    }

    if (!extent->length && !iter.request_ret) {
        error_setg(&local_err, "Server did not reply with any status extents");
        nbd_iter_channel_error(&iter, -EIO, &local_err);
    }

    error_propagate(errp, iter.err);
    *request_ret = iter.request_ret;
    return iter.ret;
}

static int nbd_co_request(BlockDriverState *bs, NBDRequest *request,
                          QEMUIOVector *write_qiov)
{
    int ret, request_ret;
    Error *local_err = NULL;
    NBDClientSession *client = nbd_get_client_session(bs);

    assert(request->type != NBD_CMD_READ);
    if (write_qiov) {
        assert(request->type == NBD_CMD_WRITE);
        assert(request->len == iov_size(write_qiov->iov, write_qiov->niov));
    } else {
        assert(request->type != NBD_CMD_WRITE);
    }
    ret = nbd_co_send_request(bs, request, write_qiov);
    if (ret < 0) {
        return ret;
    }

    ret = nbd_co_receive_return_code(client, request->handle,
                                     &request_ret, &local_err);
    if (local_err) {
        trace_nbd_co_request_fail(request->from, request->len, request->handle,
                                  request->flags, request->type,
                                  nbd_cmd_lookup(request->type),
                                  ret, error_get_pretty(local_err));
        error_free(local_err);
    }
    return ret ? ret : request_ret;
}

int nbd_client_co_preadv(BlockDriverState *bs, uint64_t offset,
                         uint64_t bytes, QEMUIOVector *qiov, int flags)
{
    int ret, request_ret;
    Error *local_err = NULL;
    NBDClientSession *client = nbd_get_client_session(bs);
    NBDRequest request = {
        .type = NBD_CMD_READ,
        .from = offset,
        .len = bytes,
    };

    assert(bytes <= NBD_MAX_BUFFER_SIZE);
    assert(!flags);

    if (!bytes) {
        return 0;
    }
    /*
     * Work around the fact that the block layer doesn't do
     * byte-accurate sizing yet - if the read exceeds the server's
     * advertised size because the block layer rounded size up, then
     * truncate the request to the server and tail-pad with zero.
     */
    if (offset >= client->info.size) {
        assert(bytes < BDRV_SECTOR_SIZE);
        qemu_iovec_memset(qiov, 0, 0, bytes);
        return 0;
    }
    if (offset + bytes > client->info.size) {
        uint64_t slop = offset + bytes - client->info.size;

        assert(slop < BDRV_SECTOR_SIZE);
        qemu_iovec_memset(qiov, bytes - slop, 0, slop);
        request.len -= slop;
    }

    ret = nbd_co_send_request(bs, &request, NULL);
    if (ret < 0) {
        return ret;
    }

    ret = nbd_co_receive_cmdread_reply(client, request.handle, offset, qiov,
                                       &request_ret, &local_err);
    if (local_err) {
        trace_nbd_co_request_fail(request.from, request.len, request.handle,
                                  request.flags, request.type,
                                  nbd_cmd_lookup(request.type),
                                  ret, error_get_pretty(local_err));
        error_free(local_err);
    }
    return ret ? ret : request_ret;
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

    assert(!(client->info.flags & NBD_FLAG_READ_ONLY));
    if (flags & BDRV_REQ_FUA) {
        assert(client->info.flags & NBD_FLAG_SEND_FUA);
        request.flags |= NBD_CMD_FLAG_FUA;
    }

    assert(bytes <= NBD_MAX_BUFFER_SIZE);

    if (!bytes) {
        return 0;
    }
    return nbd_co_request(bs, &request, qiov);
}

int nbd_client_co_pwrite_zeroes(BlockDriverState *bs, int64_t offset,
                                int bytes, BdrvRequestFlags flags)
{
    NBDClientSession *client = nbd_get_client_session(bs);
    NBDRequest request = {
        .type = NBD_CMD_WRITE_ZEROES,
        .from = offset,
        .len = bytes,
    };

    assert(!(client->info.flags & NBD_FLAG_READ_ONLY));
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

    if (!bytes) {
        return 0;
    }
    return nbd_co_request(bs, &request, NULL);
}

int nbd_client_co_flush(BlockDriverState *bs)
{
    NBDClientSession *client = nbd_get_client_session(bs);
    NBDRequest request = { .type = NBD_CMD_FLUSH };

    if (!(client->info.flags & NBD_FLAG_SEND_FLUSH)) {
        return 0;
    }

    request.from = 0;
    request.len = 0;

    return nbd_co_request(bs, &request, NULL);
}

int nbd_client_co_pdiscard(BlockDriverState *bs, int64_t offset, int bytes)
{
    NBDClientSession *client = nbd_get_client_session(bs);
    NBDRequest request = {
        .type = NBD_CMD_TRIM,
        .from = offset,
        .len = bytes,
    };

    assert(!(client->info.flags & NBD_FLAG_READ_ONLY));
    if (!(client->info.flags & NBD_FLAG_SEND_TRIM) || !bytes) {
        return 0;
    }

    return nbd_co_request(bs, &request, NULL);
}

int coroutine_fn nbd_client_co_block_status(BlockDriverState *bs,
                                            bool want_zero,
                                            int64_t offset, int64_t bytes,
                                            int64_t *pnum, int64_t *map,
                                            BlockDriverState **file)
{
    int ret, request_ret;
    NBDExtent extent = { 0 };
    NBDClientSession *client = nbd_get_client_session(bs);
    Error *local_err = NULL;

    NBDRequest request = {
        .type = NBD_CMD_BLOCK_STATUS,
        .from = offset,
        .len = MIN(MIN_NON_ZERO(QEMU_ALIGN_DOWN(INT_MAX,
                                                bs->bl.request_alignment),
                                client->info.max_block),
                   MIN(bytes, client->info.size - offset)),
        .flags = NBD_CMD_FLAG_REQ_ONE,
    };

    if (!client->info.base_allocation) {
        *pnum = bytes;
        *map = offset;
        *file = bs;
        return BDRV_BLOCK_DATA | BDRV_BLOCK_OFFSET_VALID;
    }

    /*
     * Work around the fact that the block layer doesn't do
     * byte-accurate sizing yet - if the status request exceeds the
     * server's advertised size because the block layer rounded size
     * up, we truncated the request to the server (above), or are
     * called on just the hole.
     */
    if (offset >= client->info.size) {
        *pnum = bytes;
        assert(bytes < BDRV_SECTOR_SIZE);
        /* Intentionally don't report offset_valid for the hole */
        return BDRV_BLOCK_ZERO;
    }

    if (client->info.min_block) {
        assert(QEMU_IS_ALIGNED(request.len, client->info.min_block));
    }
    ret = nbd_co_send_request(bs, &request, NULL);
    if (ret < 0) {
        return ret;
    }

    ret = nbd_co_receive_blockstatus_reply(client, request.handle, bytes,
                                           &extent, &request_ret, &local_err);
    if (local_err) {
        trace_nbd_co_request_fail(request.from, request.len, request.handle,
                                  request.flags, request.type,
                                  nbd_cmd_lookup(request.type),
                                  ret, error_get_pretty(local_err));
        error_free(local_err);
    }
    if (ret < 0 || request_ret < 0) {
        return ret ? ret : request_ret;
    }

    assert(extent.length);
    *pnum = extent.length;
    *map = offset;
    *file = bs;
    return (extent.flags & NBD_STATE_HOLE ? 0 : BDRV_BLOCK_DATA) |
        (extent.flags & NBD_STATE_ZERO ? BDRV_BLOCK_ZERO : 0) |
        BDRV_BLOCK_OFFSET_VALID;
}

void nbd_client_detach_aio_context(BlockDriverState *bs)
{
    NBDClientSession *client = nbd_get_client_session(bs);
    qio_channel_detach_aio_context(QIO_CHANNEL(client->ioc));
}

static void nbd_client_attach_aio_context_bh(void *opaque)
{
    BlockDriverState *bs = opaque;
    NBDClientSession *client = nbd_get_client_session(bs);

    /* The node is still drained, so we know the coroutine has yielded in
     * nbd_read_eof(), the only place where bs->in_flight can reach 0, or it is
     * entered for the first time. Both places are safe for entering the
     * coroutine.*/
    qemu_aio_coroutine_enter(bs->aio_context, client->connection_co);
    bdrv_dec_in_flight(bs);
}

void nbd_client_attach_aio_context(BlockDriverState *bs,
                                   AioContext *new_context)
{
    NBDClientSession *client = nbd_get_client_session(bs);
    qio_channel_attach_aio_context(QIO_CHANNEL(client->ioc), new_context);

    bdrv_inc_in_flight(bs);

    /* Need to wait here for the BH to run because the BH must run while the
     * node is still drained. */
    aio_wait_bh_oneshot(new_context, nbd_client_attach_aio_context_bh, bs);
}

void nbd_client_close(BlockDriverState *bs)
{
    NBDClientSession *client = nbd_get_client_session(bs);
    NBDRequest request = { .type = NBD_CMD_DISC };

    assert(client->ioc);

    nbd_send_request(client->ioc, &request);

    nbd_teardown_connection(bs);
}

static QIOChannelSocket *nbd_establish_connection(SocketAddress *saddr,
                                                  Error **errp)
{
    QIOChannelSocket *sioc;
    Error *local_err = NULL;

    sioc = qio_channel_socket_new();
    qio_channel_set_name(QIO_CHANNEL(sioc), "nbd-client");

    qio_channel_socket_connect_sync(sioc, saddr, &local_err);
    if (local_err) {
        object_unref(OBJECT(sioc));
        error_propagate(errp, local_err);
        return NULL;
    }

    qio_channel_set_delay(QIO_CHANNEL(sioc), false);

    return sioc;
}

static int nbd_client_connect(BlockDriverState *bs,
                              SocketAddress *saddr,
                              const char *export,
                              QCryptoTLSCreds *tlscreds,
                              const char *hostname,
                              const char *x_dirty_bitmap,
                              Error **errp)
{
    NBDClientSession *client = nbd_get_client_session(bs);
    int ret;

    /*
     * establish TCP connection, return error if it fails
     * TODO: Configurable retry-until-timeout behaviour.
     */
    QIOChannelSocket *sioc = nbd_establish_connection(saddr, errp);

    if (!sioc) {
        return -ECONNREFUSED;
    }

    /* NBD handshake */
    logout("session init %s\n", export);
    qio_channel_set_blocking(QIO_CHANNEL(sioc), true, NULL);

    client->info.request_sizes = true;
    client->info.structured_reply = true;
    client->info.base_allocation = true;
    client->info.x_dirty_bitmap = g_strdup(x_dirty_bitmap);
    client->info.name = g_strdup(export ?: "");
    ret = nbd_receive_negotiate(QIO_CHANNEL(sioc), tlscreds, hostname,
                                &client->ioc, &client->info, errp);
    g_free(client->info.x_dirty_bitmap);
    g_free(client->info.name);
    if (ret < 0) {
        logout("Failed to negotiate with the NBD server\n");
        object_unref(OBJECT(sioc));
        return ret;
    }
    if (x_dirty_bitmap && !client->info.base_allocation) {
        error_setg(errp, "requested x-dirty-bitmap %s not found",
                   x_dirty_bitmap);
        ret = -EINVAL;
        goto fail;
    }
    if (client->info.flags & NBD_FLAG_READ_ONLY) {
        ret = bdrv_apply_auto_read_only(bs, "NBD export is read-only", errp);
        if (ret < 0) {
            goto fail;
        }
    }
    if (client->info.flags & NBD_FLAG_SEND_FUA) {
        bs->supported_write_flags = BDRV_REQ_FUA;
        bs->supported_zero_flags |= BDRV_REQ_FUA;
    }
    if (client->info.flags & NBD_FLAG_SEND_WRITE_ZEROES) {
        bs->supported_zero_flags |= BDRV_REQ_MAY_UNMAP;
    }

    client->sioc = sioc;

    if (!client->ioc) {
        client->ioc = QIO_CHANNEL(sioc);
        object_ref(OBJECT(client->ioc));
    }

    /* Now that we're connected, set the socket to be non-blocking and
     * kick the reply mechanism.  */
    qio_channel_set_blocking(QIO_CHANNEL(sioc), false, NULL);
    client->connection_co = qemu_coroutine_create(nbd_connection_entry, client);
    bdrv_inc_in_flight(bs);
    nbd_client_attach_aio_context(bs, bdrv_get_aio_context(bs));

    logout("Established connection with NBD server\n");
    return 0;

 fail:
    /*
     * We have connected, but must fail for other reasons. The
     * connection is still blocking; send NBD_CMD_DISC as a courtesy
     * to the server.
     */
    {
        NBDRequest request = { .type = NBD_CMD_DISC };

        nbd_send_request(client->ioc ?: QIO_CHANNEL(sioc), &request);

        object_unref(OBJECT(sioc));

        return ret;
    }
}

int nbd_client_init(BlockDriverState *bs,
                    SocketAddress *saddr,
                    const char *export,
                    QCryptoTLSCreds *tlscreds,
                    const char *hostname,
                    const char *x_dirty_bitmap,
                    Error **errp)
{
    NBDClientSession *client = nbd_get_client_session(bs);

    client->bs = bs;
    qemu_co_mutex_init(&client->send_mutex);
    qemu_co_queue_init(&client->free_sema);

    return nbd_client_connect(bs, saddr, export, tlscreds, hostname,
                              x_dirty_bitmap, errp);
}
