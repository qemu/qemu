/*
 * vfio protocol over a UNIX socket.
 *
 * Copyright © 2018, 2021 Oracle and/or its affiliates.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include <sys/ioctl.h>

#include "hw/vfio/vfio-device.h"
#include "hw/vfio-user/proxy.h"
#include "hw/vfio-user/trace.h"
#include "qapi/error.h"
#include "qobject/qbool.h"
#include "qobject/qdict.h"
#include "qobject/qjson.h"
#include "qobject/qnum.h"
#include "qemu/error-report.h"
#include "qemu/lockable.h"
#include "qemu/main-loop.h"
#include "qemu/thread.h"
#include "system/iothread.h"

static IOThread *vfio_user_iothread;

static void vfio_user_shutdown(VFIOUserProxy *proxy);
static VFIOUserMsg *vfio_user_getmsg(VFIOUserProxy *proxy, VFIOUserHdr *hdr,
                                     VFIOUserFDs *fds);
static void vfio_user_recycle(VFIOUserProxy *proxy, VFIOUserMsg *msg);

static void vfio_user_recv(void *opaque);
static void vfio_user_send(void *opaque);

static void vfio_user_request(void *opaque);

static inline void vfio_user_set_error(VFIOUserHdr *hdr, uint32_t err)
{
    hdr->flags |= VFIO_USER_ERROR;
    hdr->error_reply = err;
}

/*
 * Functions called by main, CPU, or iothread threads
 */

static void vfio_user_shutdown(VFIOUserProxy *proxy)
{
    qio_channel_shutdown(proxy->ioc, QIO_CHANNEL_SHUTDOWN_READ, NULL);
    qio_channel_set_aio_fd_handler(proxy->ioc, proxy->ctx, NULL,
                                   proxy->ctx, NULL, NULL);
}

/*
 * Same return values as qio_channel_writev_full():
 *
 * QIO_CHANNEL_ERR_BLOCK: *errp not set
 * -1: *errp will be populated
 * otherwise: bytes written
 */
static ssize_t vfio_user_send_qio(VFIOUserProxy *proxy, VFIOUserMsg *msg,
                                  Error **errp)
{
    VFIOUserFDs *fds =  msg->fds;
    struct iovec iov = {
        .iov_base = msg->hdr,
        .iov_len = msg->hdr->size,
    };
    size_t numfds = 0;
    int *fdp = NULL;
    ssize_t ret;

    if (fds != NULL && fds->send_fds != 0) {
        numfds = fds->send_fds;
        fdp = fds->fds;
    }

    ret = qio_channel_writev_full(proxy->ioc, &iov, 1, fdp, numfds, 0, errp);

    if (ret == -1) {
        vfio_user_set_error(msg->hdr, EIO);
        vfio_user_shutdown(proxy);
    }
    trace_vfio_user_send_write(msg->hdr->id, ret);

    return ret;
}

static VFIOUserMsg *vfio_user_getmsg(VFIOUserProxy *proxy, VFIOUserHdr *hdr,
                                     VFIOUserFDs *fds)
{
    VFIOUserMsg *msg;

    msg = QTAILQ_FIRST(&proxy->free);
    if (msg != NULL) {
        QTAILQ_REMOVE(&proxy->free, msg, next);
    } else {
        msg = g_malloc0(sizeof(*msg));
        qemu_cond_init(&msg->cv);
    }

    msg->hdr = hdr;
    msg->fds = fds;
    return msg;
}

/*
 * Recycle a message list entry to the free list.
 */
static void vfio_user_recycle(VFIOUserProxy *proxy, VFIOUserMsg *msg)
{
    if (msg->type == VFIO_MSG_NONE) {
        error_printf("vfio_user_recycle - freeing free msg\n");
        return;
    }

    /* free msg buffer if no one is waiting to consume the reply */
    if (msg->type == VFIO_MSG_NOWAIT || msg->type == VFIO_MSG_ASYNC) {
        g_free(msg->hdr);
        if (msg->fds != NULL) {
            g_free(msg->fds);
        }
    }

    msg->type = VFIO_MSG_NONE;
    msg->hdr = NULL;
    msg->fds = NULL;
    msg->complete = false;
    msg->pending = false;
    QTAILQ_INSERT_HEAD(&proxy->free, msg, next);
}

VFIOUserFDs *vfio_user_getfds(int numfds)
{
    VFIOUserFDs *fds = g_malloc0(sizeof(*fds) + (numfds * sizeof(int)));

    fds->fds = (int *)((char *)fds + sizeof(*fds));

    return fds;
}

/*
 * Functions only called by iothread
 */

/*
 * Process a received message.
 */
static void vfio_user_process(VFIOUserProxy *proxy, VFIOUserMsg *msg,
                              bool isreply)
{

    /*
     * Replies signal a waiter, if none just check for errors
     * and free the message buffer.
     *
     * Requests get queued for the BH.
     */
    if (isreply) {
        msg->complete = true;
        if (msg->type == VFIO_MSG_WAIT) {
            qemu_cond_signal(&msg->cv);
        } else {
            if (msg->hdr->flags & VFIO_USER_ERROR) {
                error_printf("vfio_user_process: error reply on async ");
                error_printf("request command %x error %s\n",
                             msg->hdr->command,
                             strerror(msg->hdr->error_reply));
            }
            /* youngest nowait msg has been ack'd */
            if (proxy->last_nowait == msg) {
                proxy->last_nowait = NULL;
            }
            vfio_user_recycle(proxy, msg);
        }
    } else {
        QTAILQ_INSERT_TAIL(&proxy->incoming, msg, next);
        qemu_bh_schedule(proxy->req_bh);
    }
}

/*
 * Complete a partial message read
 */
static int vfio_user_complete(VFIOUserProxy *proxy, Error **errp)
{
    VFIOUserMsg *msg = proxy->part_recv;
    size_t msgleft = proxy->recv_left;
    bool isreply;
    char *data;
    int ret;

    data = (char *)msg->hdr + (msg->hdr->size - msgleft);
    while (msgleft > 0) {
        ret = qio_channel_read(proxy->ioc, data, msgleft, errp);

        /* error or would block */
        if (ret <= 0) {
            /* try for rest on next iternation */
            if (ret == QIO_CHANNEL_ERR_BLOCK) {
                proxy->recv_left = msgleft;
            }
            return ret;
        }
        trace_vfio_user_recv_read(msg->hdr->id, ret);

        msgleft -= ret;
        data += ret;
    }

    /*
     * Read complete message, process it.
     */
    proxy->part_recv = NULL;
    proxy->recv_left = 0;
    isreply = (msg->hdr->flags & VFIO_USER_TYPE) == VFIO_USER_REPLY;
    vfio_user_process(proxy, msg, isreply);

    /* return positive value */
    return 1;
}

/*
 * Receive and process one incoming message.
 *
 * For replies, find matching outgoing request and wake any waiters.
 * For requests, queue in incoming list and run request BH.
 */
static int vfio_user_recv_one(VFIOUserProxy *proxy, Error **errp)
{
    VFIOUserMsg *msg = NULL;
    g_autofree int *fdp = NULL;
    VFIOUserFDs *reqfds;
    VFIOUserHdr hdr;
    struct iovec iov = {
        .iov_base = &hdr,
        .iov_len = sizeof(hdr),
    };
    bool isreply = false;
    int i, ret;
    size_t msgleft, numfds = 0;
    char *data = NULL;
    char *buf = NULL;

    /*
     * Complete any partial reads
     */
    if (proxy->part_recv != NULL) {
        ret = vfio_user_complete(proxy, errp);

        /* still not complete, try later */
        if (ret == QIO_CHANNEL_ERR_BLOCK) {
            return ret;
        }

        if (ret <= 0) {
            goto fatal;
        }
        /* else fall into reading another msg */
    }

    /*
     * Read header
     */
    ret = qio_channel_readv_full(proxy->ioc, &iov, 1, &fdp, &numfds, 0,
                                 errp);
    if (ret == QIO_CHANNEL_ERR_BLOCK) {
        return ret;
    }

    /* read error or other side closed connection */
    if (ret <= 0) {
        goto fatal;
    }

    if (ret < sizeof(hdr)) {
        error_setg(errp, "short read of header");
        goto fatal;
    }

    /*
     * Validate header
     */
    if (hdr.size < sizeof(VFIOUserHdr)) {
        error_setg(errp, "bad header size");
        goto fatal;
    }
    switch (hdr.flags & VFIO_USER_TYPE) {
    case VFIO_USER_REQUEST:
        isreply = false;
        break;
    case VFIO_USER_REPLY:
        isreply = true;
        break;
    default:
        error_setg(errp, "unknown message type");
        goto fatal;
    }
    trace_vfio_user_recv_hdr(proxy->sockname, hdr.id, hdr.command, hdr.size,
                             hdr.flags);

    /*
     * For replies, find the matching pending request.
     * For requests, reap incoming FDs.
     */
    if (isreply) {
        QTAILQ_FOREACH(msg, &proxy->pending, next) {
            if (hdr.id == msg->id) {
                break;
            }
        }
        if (msg == NULL) {
            error_setg(errp, "unexpected reply");
            goto err;
        }
        QTAILQ_REMOVE(&proxy->pending, msg, next);

        /*
         * Process any received FDs
         */
        if (numfds != 0) {
            if (msg->fds == NULL || msg->fds->recv_fds < numfds) {
                error_setg(errp, "unexpected FDs");
                goto err;
            }
            msg->fds->recv_fds = numfds;
            memcpy(msg->fds->fds, fdp, numfds * sizeof(int));
        }
    } else {
        if (numfds != 0) {
            reqfds = vfio_user_getfds(numfds);
            memcpy(reqfds->fds, fdp, numfds * sizeof(int));
        } else {
            reqfds = NULL;
        }
    }

    /*
     * Put the whole message into a single buffer.
     */
    if (isreply) {
        if (hdr.size > msg->rsize) {
            error_setg(errp, "reply larger than recv buffer");
            goto err;
        }
        *msg->hdr = hdr;
        data = (char *)msg->hdr + sizeof(hdr);
    } else {
        if (hdr.size > proxy->max_xfer_size + sizeof(VFIOUserDMARW)) {
            error_setg(errp, "vfio_user_recv request larger than max");
            goto err;
        }
        buf = g_malloc0(hdr.size);
        memcpy(buf, &hdr, sizeof(hdr));
        data = buf + sizeof(hdr);
        msg = vfio_user_getmsg(proxy, (VFIOUserHdr *)buf, reqfds);
        msg->type = VFIO_MSG_REQ;
    }

    /*
     * Read rest of message.
     */
    msgleft = hdr.size - sizeof(hdr);
    while (msgleft > 0) {
        ret = qio_channel_read(proxy->ioc, data, msgleft, errp);

        /* prepare to complete read on next iternation */
        if (ret == QIO_CHANNEL_ERR_BLOCK) {
            proxy->part_recv = msg;
            proxy->recv_left = msgleft;
            return ret;
        }

        if (ret <= 0) {
            goto fatal;
        }
        trace_vfio_user_recv_read(hdr.id, ret);

        msgleft -= ret;
        data += ret;
    }

    vfio_user_process(proxy, msg, isreply);
    return 0;

    /*
     * fatal means the other side closed or we don't trust the stream
     * err means this message is corrupt
     */
fatal:
    vfio_user_shutdown(proxy);
    proxy->state = VFIO_PROXY_ERROR;

    /* set error if server side closed */
    if (ret == 0) {
        error_setg(errp, "server closed socket");
    }

err:
    for (i = 0; i < numfds; i++) {
        close(fdp[i]);
    }
    if (isreply && msg != NULL) {
        /* force an error to keep sending thread from hanging */
        vfio_user_set_error(msg->hdr, EINVAL);
        msg->complete = true;
        qemu_cond_signal(&msg->cv);
    }
    return -1;
}

static void vfio_user_recv(void *opaque)
{
    VFIOUserProxy *proxy = opaque;

    QEMU_LOCK_GUARD(&proxy->lock);

    if (proxy->state == VFIO_PROXY_CONNECTED) {
        Error *local_err = NULL;

        while (vfio_user_recv_one(proxy, &local_err) == 0) {
            ;
        }

        if (local_err != NULL) {
            error_report_err(local_err);
        }
    }
}

/*
 * Send a single message, same return semantics as vfio_user_send_qio().
 *
 * Sent async messages are freed, others are moved to pending queue.
 */
static ssize_t vfio_user_send_one(VFIOUserProxy *proxy, Error **errp)
{
    VFIOUserMsg *msg;
    ssize_t ret;

    msg = QTAILQ_FIRST(&proxy->outgoing);
    ret = vfio_user_send_qio(proxy, msg, errp);
    if (ret < 0) {
        return ret;
    }

    QTAILQ_REMOVE(&proxy->outgoing, msg, next);
    proxy->num_outgoing--;
    if (msg->type == VFIO_MSG_ASYNC) {
        vfio_user_recycle(proxy, msg);
    } else {
        QTAILQ_INSERT_TAIL(&proxy->pending, msg, next);
        msg->pending = true;
    }

    return ret;
}

/*
 * Send messages from outgoing queue when the socket buffer has space.
 * If we deplete 'outgoing', remove ourselves from the poll list.
 */
static void vfio_user_send(void *opaque)
{
    VFIOUserProxy *proxy = opaque;

    QEMU_LOCK_GUARD(&proxy->lock);

    if (proxy->state == VFIO_PROXY_CONNECTED) {
        while (!QTAILQ_EMPTY(&proxy->outgoing)) {
            Error *local_err = NULL;
            int ret;

            ret = vfio_user_send_one(proxy, &local_err);

            if (ret == QIO_CHANNEL_ERR_BLOCK) {
                return;
            } else if (ret == -1) {
                error_report_err(local_err);
                return;
            }
        }
        qio_channel_set_aio_fd_handler(proxy->ioc, proxy->ctx,
                                       vfio_user_recv, NULL, NULL, proxy);

        /* queue empty - send any pending multi write msgs */
        if (proxy->wr_multi != NULL) {
            vfio_user_flush_multi(proxy);
        }
    }
}

static void vfio_user_close_cb(void *opaque)
{
    VFIOUserProxy *proxy = opaque;

    QEMU_LOCK_GUARD(&proxy->lock);

    proxy->state = VFIO_PROXY_CLOSED;
    qemu_cond_signal(&proxy->close_cv);
}


/*
 * Functions called by main or CPU threads
 */

/*
 * Process incoming requests.
 *
 * The bus-specific callback has the form:
 *    request(opaque, msg)
 * where 'opaque' was specified in vfio_user_set_handler
 * and 'msg' is the inbound message.
 *
 * The callback is responsible for disposing of the message buffer,
 * usually by re-using it when calling vfio_send_reply or vfio_send_error,
 * both of which free their message buffer when the reply is sent.
 *
 * If the callback uses a new buffer, it needs to free the old one.
 */
static void vfio_user_request(void *opaque)
{
    VFIOUserProxy *proxy = opaque;
    VFIOUserMsgQ new, free;
    VFIOUserMsg *msg, *m1;

    /* reap all incoming */
    QTAILQ_INIT(&new);
    WITH_QEMU_LOCK_GUARD(&proxy->lock) {
        QTAILQ_FOREACH_SAFE(msg, &proxy->incoming, next, m1) {
            QTAILQ_REMOVE(&proxy->incoming, msg, next);
            QTAILQ_INSERT_TAIL(&new, msg, next);
        }
    }

    /* process list */
    QTAILQ_INIT(&free);
    QTAILQ_FOREACH_SAFE(msg, &new, next, m1) {
        QTAILQ_REMOVE(&new, msg, next);
        trace_vfio_user_recv_request(msg->hdr->command);
        proxy->request(proxy->req_arg, msg);
        QTAILQ_INSERT_HEAD(&free, msg, next);
    }

    /* free list */
    WITH_QEMU_LOCK_GUARD(&proxy->lock) {
        QTAILQ_FOREACH_SAFE(msg, &free, next, m1) {
            vfio_user_recycle(proxy, msg);
        }
    }
}

/*
 * Messages are queued onto the proxy's outgoing list.
 *
 * It handles 3 types of messages:
 *
 * async messages - replies and posted writes
 *
 * There will be no reply from the server, so message
 * buffers are freed after they're sent.
 *
 * nowait messages - map/unmap during address space transactions
 *
 * These are also sent async, but a reply is expected so that
 * vfio_wait_reqs() can wait for the youngest nowait request.
 * They transition from the outgoing list to the pending list
 * when sent, and are freed when the reply is received.
 *
 * wait messages - all other requests
 *
 * The reply to these messages is waited for by their caller.
 * They also transition from outgoing to pending when sent, but
 * the message buffer is returned to the caller with the reply
 * contents.  The caller is responsible for freeing these messages.
 *
 * As an optimization, if the outgoing list and the socket send
 * buffer are empty, the message is sent inline instead of being
 * added to the outgoing list.  The rest of the transitions are
 * unchanged.
 */
static bool vfio_user_send_queued(VFIOUserProxy *proxy, VFIOUserMsg *msg,
                                  Error **errp)
{
    int ret;

    /* older coalesced writes go first */
    if (proxy->wr_multi != NULL &&
        ((msg->hdr->flags & VFIO_USER_TYPE) == VFIO_USER_REQUEST)) {
        vfio_user_flush_multi(proxy);
    }

    /*
     * Unsent outgoing msgs - add to tail
     */
    if (!QTAILQ_EMPTY(&proxy->outgoing)) {
        QTAILQ_INSERT_TAIL(&proxy->outgoing, msg, next);
        proxy->num_outgoing++;
        return true;
    }

    /*
     * Try inline - if blocked, queue it and kick send poller
     */
    if (proxy->flags & VFIO_PROXY_FORCE_QUEUED) {
        ret = QIO_CHANNEL_ERR_BLOCK;
    } else {
        ret = vfio_user_send_qio(proxy, msg, errp);
    }

    if (ret == QIO_CHANNEL_ERR_BLOCK) {
        QTAILQ_INSERT_HEAD(&proxy->outgoing, msg, next);
        proxy->num_outgoing = 1;
        qio_channel_set_aio_fd_handler(proxy->ioc, proxy->ctx,
                                       vfio_user_recv, proxy->ctx,
                                       vfio_user_send, proxy);
        return true;
    }
    if (ret == -1) {
        return false;
    }

    /*
     * Sent - free async, add others to pending
     */
    if (msg->type == VFIO_MSG_ASYNC) {
        vfio_user_recycle(proxy, msg);
    } else {
        QTAILQ_INSERT_TAIL(&proxy->pending, msg, next);
        msg->pending = true;
    }

    return true;
}

/*
 * nowait send - vfio_wait_reqs() can wait for it later
 *
 * Returns false if we did not successfully receive a reply message, in which
 * case @errp will be populated.
 *
 * In either case, ownership of @hdr and @fds is taken, and the caller must
 * *not* free them itself.
 */
bool vfio_user_send_nowait(VFIOUserProxy *proxy, VFIOUserHdr *hdr,
                           VFIOUserFDs *fds, int rsize, Error **errp)
{
    VFIOUserMsg *msg;

    QEMU_LOCK_GUARD(&proxy->lock);

    msg = vfio_user_getmsg(proxy, hdr, fds);
    msg->id = hdr->id;
    msg->rsize = rsize ? rsize : hdr->size;
    msg->type = VFIO_MSG_NOWAIT;

    if (hdr->flags & VFIO_USER_NO_REPLY) {
        error_setg_errno(errp, EINVAL, "%s on NO_REPLY message", __func__);
        vfio_user_recycle(proxy, msg);
        return false;
    }

    if (!vfio_user_send_queued(proxy, msg, errp)) {
        vfio_user_recycle(proxy, msg);
        return false;
    }

    proxy->last_nowait = msg;

    return true;
}

/*
 * Returns false if we did not successfully receive a reply message, in which
 * case @errp will be populated.
 *
 * In either case, the caller must free @hdr and @fds if needed.
 */
bool vfio_user_send_wait(VFIOUserProxy *proxy, VFIOUserHdr *hdr,
                         VFIOUserFDs *fds, int rsize, Error **errp)
{
    VFIOUserMsg *msg;
    bool ok = false;

    if (hdr->flags & VFIO_USER_NO_REPLY) {
        error_setg_errno(errp, EINVAL, "%s on NO_REPLY message", __func__);
        return false;
    }

    qemu_mutex_lock(&proxy->lock);

    msg = vfio_user_getmsg(proxy, hdr, fds);
    msg->id = hdr->id;
    msg->rsize = rsize ? rsize : hdr->size;
    msg->type = VFIO_MSG_WAIT;

    ok = vfio_user_send_queued(proxy, msg, errp);

    if (ok) {
        while (!msg->complete) {
            if (!qemu_cond_timedwait(&msg->cv, &proxy->lock,
                                     proxy->wait_time)) {
                VFIOUserMsgQ *list;

                list = msg->pending ? &proxy->pending : &proxy->outgoing;
                QTAILQ_REMOVE(list, msg, next);
                error_setg_errno(errp, ETIMEDOUT,
                                 "timed out waiting for reply");
                ok = false;
                break;
            }
        }
    }

    vfio_user_recycle(proxy, msg);

    qemu_mutex_unlock(&proxy->lock);

    return ok;
}

/*
 * async send - msg can be queued, but will be freed when sent
 *
 * Returns false on failure, in which case @errp will be populated.
 *
 * In either case, ownership of @hdr and @fds is taken, and the caller must
 * *not* free them itself.
 */
bool vfio_user_send_async(VFIOUserProxy *proxy, VFIOUserHdr *hdr,
                          VFIOUserFDs *fds, Error **errp)
{
    VFIOUserMsg *msg;

    QEMU_LOCK_GUARD(&proxy->lock);

    msg = vfio_user_getmsg(proxy, hdr, fds);
    msg->id = hdr->id;
    msg->rsize = 0;
    msg->type = VFIO_MSG_ASYNC;

    if (!(hdr->flags & (VFIO_USER_NO_REPLY | VFIO_USER_REPLY))) {
        error_setg_errno(errp, EINVAL, "%s on sync message", __func__);
        vfio_user_recycle(proxy, msg);
        return false;
    }

    if (!vfio_user_send_queued(proxy, msg, errp)) {
        vfio_user_recycle(proxy, msg);
        return false;
    }

    return true;
}

void vfio_user_wait_reqs(VFIOUserProxy *proxy)
{
    VFIOUserMsg *msg;

    /*
     * Any DMA map/unmap requests sent in the middle
     * of a memory region transaction were sent nowait.
     * Wait for them here.
     */
    qemu_mutex_lock(&proxy->lock);
    if (proxy->last_nowait != NULL) {
        /*
         * Change type to WAIT to wait for reply
         */
        msg = proxy->last_nowait;
        msg->type = VFIO_MSG_WAIT;
        proxy->last_nowait = NULL;
        while (!msg->complete) {
            if (!qemu_cond_timedwait(&msg->cv, &proxy->lock,
                                     proxy->wait_time)) {
                VFIOUserMsgQ *list;

                list = msg->pending ? &proxy->pending : &proxy->outgoing;
                QTAILQ_REMOVE(list, msg, next);
                error_printf("vfio_wait_reqs - timed out\n");
                break;
            }
        }

        if (msg->hdr->flags & VFIO_USER_ERROR) {
            error_printf("vfio_user_wait_reqs - error reply on async ");
            error_printf("request: command %x error %s\n", msg->hdr->command,
                         strerror(msg->hdr->error_reply));
        }

        /*
         * Change type back to NOWAIT to free
         */
        msg->type = VFIO_MSG_NOWAIT;
        vfio_user_recycle(proxy, msg);
    }

    qemu_mutex_unlock(&proxy->lock);
}

/*
 * Reply to an incoming request.
 */
void vfio_user_send_reply(VFIOUserProxy *proxy, VFIOUserHdr *hdr, int size)
{
    Error *local_err = NULL;

    if (size < sizeof(VFIOUserHdr)) {
        error_printf("%s: size too small", __func__);
        g_free(hdr);
        return;
    }

    /*
     * convert header to associated reply
     */
    hdr->flags = VFIO_USER_REPLY;
    hdr->size = size;

    if (!vfio_user_send_async(proxy, hdr, NULL, &local_err)) {
        error_report_err(local_err);
    }
}

/*
 * Send an error reply to an incoming request.
 */
void vfio_user_send_error(VFIOUserProxy *proxy, VFIOUserHdr *hdr, int error)
{
    Error *local_err = NULL;

    /*
     * convert header to associated reply
     */
    hdr->flags = VFIO_USER_REPLY;
    hdr->flags |= VFIO_USER_ERROR;
    hdr->error_reply = error;
    hdr->size = sizeof(*hdr);

    if (!vfio_user_send_async(proxy, hdr, NULL, &local_err)) {
        error_report_err(local_err);
    }
}

/*
 * Close FDs erroneously received in an incoming request.
 */
void vfio_user_putfds(VFIOUserMsg *msg)
{
    VFIOUserFDs *fds = msg->fds;
    int i;

    for (i = 0; i < fds->recv_fds; i++) {
        close(fds->fds[i]);
    }
    g_free(fds);
    msg->fds = NULL;
}

void
vfio_user_disable_posted_writes(VFIOUserProxy *proxy)
{
    WITH_QEMU_LOCK_GUARD(&proxy->lock) {
         proxy->flags |= VFIO_PROXY_NO_POST;
    }
}

static QLIST_HEAD(, VFIOUserProxy) vfio_user_sockets =
    QLIST_HEAD_INITIALIZER(vfio_user_sockets);

VFIOUserProxy *vfio_user_connect_dev(SocketAddress *addr, Error **errp)
{
    VFIOUserProxy *proxy;
    QIOChannelSocket *sioc;
    QIOChannel *ioc;
    char *sockname;

    if (addr->type != SOCKET_ADDRESS_TYPE_UNIX) {
        error_setg(errp, "vfio_user_connect - bad address family");
        return NULL;
    }
    sockname = addr->u.q_unix.path;

    sioc = qio_channel_socket_new();
    ioc = QIO_CHANNEL(sioc);
    if (qio_channel_socket_connect_sync(sioc, addr, errp)) {
        object_unref(OBJECT(ioc));
        return NULL;
    }
    qio_channel_set_blocking(ioc, false, NULL);

    proxy = g_malloc0(sizeof(VFIOUserProxy));
    proxy->sockname = g_strdup_printf("unix:%s", sockname);
    proxy->ioc = ioc;

    /* init defaults */
    proxy->max_xfer_size = VFIO_USER_DEF_MAX_XFER;
    proxy->max_send_fds = VFIO_USER_DEF_MAX_FDS;
    proxy->max_dma = VFIO_USER_DEF_MAP_MAX;
    proxy->dma_pgsizes = VFIO_USER_DEF_PGSIZE;
    proxy->max_bitmap = VFIO_USER_DEF_MAX_BITMAP;
    proxy->migr_pgsize = VFIO_USER_DEF_PGSIZE;

    proxy->flags = VFIO_PROXY_CLIENT;
    proxy->state = VFIO_PROXY_CONNECTED;

    qemu_mutex_init(&proxy->lock);
    qemu_cond_init(&proxy->close_cv);

    if (vfio_user_iothread == NULL) {
        vfio_user_iothread = iothread_create("VFIO user", errp);
    }

    proxy->ctx = iothread_get_aio_context(vfio_user_iothread);
    proxy->req_bh = qemu_bh_new(vfio_user_request, proxy);

    QTAILQ_INIT(&proxy->outgoing);
    QTAILQ_INIT(&proxy->incoming);
    QTAILQ_INIT(&proxy->free);
    QTAILQ_INIT(&proxy->pending);
    QLIST_INSERT_HEAD(&vfio_user_sockets, proxy, next);

    return proxy;
}

void vfio_user_set_handler(VFIODevice *vbasedev,
                           void (*handler)(void *opaque, VFIOUserMsg *msg),
                           void *req_arg)
{
    VFIOUserProxy *proxy = vbasedev->proxy;

    proxy->request = handler;
    proxy->req_arg = req_arg;
    qio_channel_set_aio_fd_handler(proxy->ioc, proxy->ctx,
                                   vfio_user_recv, NULL, NULL, proxy);
}

void vfio_user_disconnect(VFIOUserProxy *proxy)
{
    VFIOUserMsg *r1, *r2;

    qemu_mutex_lock(&proxy->lock);

    /* our side is quitting */
    if (proxy->state == VFIO_PROXY_CONNECTED) {
        vfio_user_shutdown(proxy);
        if (!QTAILQ_EMPTY(&proxy->pending)) {
            error_printf("vfio_user_disconnect: outstanding requests\n");
        }
    }
    object_unref(OBJECT(proxy->ioc));
    proxy->ioc = NULL;
    qemu_bh_delete(proxy->req_bh);
    proxy->req_bh = NULL;

    proxy->state = VFIO_PROXY_CLOSING;
    QTAILQ_FOREACH_SAFE(r1, &proxy->outgoing, next, r2) {
        qemu_cond_destroy(&r1->cv);
        QTAILQ_REMOVE(&proxy->outgoing, r1, next);
        g_free(r1);
    }
    QTAILQ_FOREACH_SAFE(r1, &proxy->incoming, next, r2) {
        qemu_cond_destroy(&r1->cv);
        QTAILQ_REMOVE(&proxy->incoming, r1, next);
        g_free(r1);
    }
    QTAILQ_FOREACH_SAFE(r1, &proxy->pending, next, r2) {
        qemu_cond_destroy(&r1->cv);
        QTAILQ_REMOVE(&proxy->pending, r1, next);
        g_free(r1);
    }
    QTAILQ_FOREACH_SAFE(r1, &proxy->free, next, r2) {
        qemu_cond_destroy(&r1->cv);
        QTAILQ_REMOVE(&proxy->free, r1, next);
        g_free(r1);
    }

    /*
     * Make sure the iothread isn't blocking anywhere
     * with a ref to this proxy by waiting for a BH
     * handler to run after the proxy fd handlers were
     * deleted above.
     */
    aio_bh_schedule_oneshot(proxy->ctx, vfio_user_close_cb, proxy);

    while (proxy->state != VFIO_PROXY_CLOSED) {
        qemu_cond_wait(&proxy->close_cv, &proxy->lock);
    }

    /* we now hold the only ref to proxy */
    qemu_mutex_unlock(&proxy->lock);
    qemu_cond_destroy(&proxy->close_cv);
    qemu_mutex_destroy(&proxy->lock);

    QLIST_REMOVE(proxy, next);
    if (QLIST_EMPTY(&vfio_user_sockets)) {
        iothread_destroy(vfio_user_iothread);
        vfio_user_iothread = NULL;
    }

    g_free(proxy->sockname);
    g_free(proxy);
}

void vfio_user_request_msg(VFIOUserHdr *hdr, uint16_t cmd,
                           uint32_t size, uint32_t flags)
{
    static uint16_t next_id;

    hdr->id = qatomic_fetch_inc(&next_id);
    hdr->command = cmd;
    hdr->size = size;
    hdr->flags = (flags & ~VFIO_USER_TYPE) | VFIO_USER_REQUEST;
    hdr->error_reply = 0;
}

struct cap_entry {
    const char *name;
    bool (*check)(VFIOUserProxy *proxy, QObject *qobj, Error **errp);
};

static bool caps_parse(VFIOUserProxy *proxy, QDict *qdict,
                       struct cap_entry caps[], Error **errp)
{
    QObject *qobj;
    struct cap_entry *p;

    for (p = caps; p->name != NULL; p++) {
        qobj = qdict_get(qdict, p->name);
        if (qobj != NULL) {
            if (!p->check(proxy, qobj, errp)) {
                return false;
            }
            qdict_del(qdict, p->name);
        }
    }

    /* warning, for now */
    if (qdict_size(qdict) != 0) {
        warn_report("spurious capabilities");
    }
    return true;
}

static bool check_migr_pgsize(VFIOUserProxy *proxy, QObject *qobj, Error **errp)
{
    QNum *qn = qobject_to(QNum, qobj);
    uint64_t pgsize;

    if (qn == NULL || !qnum_get_try_uint(qn, &pgsize)) {
        error_setg(errp, "malformed %s", VFIO_USER_CAP_PGSIZE);
        return false;
    }

    /* must be larger than default */
    if (pgsize & (VFIO_USER_DEF_PGSIZE - 1)) {
        error_setg(errp, "pgsize 0x%"PRIx64" too small", pgsize);
        return false;
    }

    proxy->migr_pgsize = pgsize;
    return true;
}

static bool check_bitmap(VFIOUserProxy *proxy, QObject *qobj, Error **errp)
{
    QNum *qn = qobject_to(QNum, qobj);
    uint64_t bitmap_size;

    if (qn == NULL || !qnum_get_try_uint(qn, &bitmap_size)) {
        error_setg(errp, "malformed %s", VFIO_USER_CAP_MAX_BITMAP);
        return false;
    }

    /* can only lower it */
    if (bitmap_size > VFIO_USER_DEF_MAX_BITMAP) {
        error_setg(errp, "%s too large", VFIO_USER_CAP_MAX_BITMAP);
        return false;
    }

    proxy->max_bitmap = bitmap_size;
    return true;
}

static struct cap_entry caps_migr[] = {
    { VFIO_USER_CAP_PGSIZE, check_migr_pgsize },
    { VFIO_USER_CAP_MAX_BITMAP, check_bitmap },
    { NULL }
};

static bool check_max_fds(VFIOUserProxy *proxy, QObject *qobj, Error **errp)
{
    QNum *qn = qobject_to(QNum, qobj);
    uint64_t max_send_fds;

    if (qn == NULL || !qnum_get_try_uint(qn, &max_send_fds) ||
        max_send_fds > VFIO_USER_MAX_MAX_FDS) {
        error_setg(errp, "malformed %s", VFIO_USER_CAP_MAX_FDS);
        return false;
    }
    proxy->max_send_fds = max_send_fds;
    return true;
}

static bool check_max_xfer(VFIOUserProxy *proxy, QObject *qobj, Error **errp)
{
    QNum *qn = qobject_to(QNum, qobj);
    uint64_t max_xfer_size;

    if (qn == NULL || !qnum_get_try_uint(qn, &max_xfer_size) ||
        max_xfer_size > VFIO_USER_MAX_MAX_XFER) {
        error_setg(errp, "malformed %s", VFIO_USER_CAP_MAX_XFER);
        return false;
    }
    proxy->max_xfer_size = max_xfer_size;
    return true;
}

static bool check_pgsizes(VFIOUserProxy *proxy, QObject *qobj, Error **errp)
{
    QNum *qn = qobject_to(QNum, qobj);
    uint64_t pgsizes;

    if (qn == NULL || !qnum_get_try_uint(qn, &pgsizes)) {
        error_setg(errp, "malformed %s", VFIO_USER_CAP_PGSIZES);
        return false;
    }

    /* must be larger than default */
    if (pgsizes & (VFIO_USER_DEF_PGSIZE - 1)) {
        error_setg(errp, "pgsize 0x%"PRIx64" too small", pgsizes);
        return false;
    }

    proxy->dma_pgsizes = pgsizes;
    return true;
}

static bool check_max_dma(VFIOUserProxy *proxy, QObject *qobj, Error **errp)
{
    QNum *qn = qobject_to(QNum, qobj);
    uint64_t max_dma;

    if (qn == NULL || !qnum_get_try_uint(qn, &max_dma)) {
        error_setg(errp, "malformed %s", VFIO_USER_CAP_MAP_MAX);
        return false;
    }

    /* can only lower it */
    if (max_dma > VFIO_USER_DEF_MAP_MAX) {
        error_setg(errp, "%s too large", VFIO_USER_CAP_MAP_MAX);
        return false;
    }

    proxy->max_dma = max_dma;
    return true;
}

static bool check_migr(VFIOUserProxy *proxy, QObject *qobj, Error **errp)
{
    QDict *qdict = qobject_to(QDict, qobj);

    if (qdict == NULL) {
        error_setg(errp, "malformed %s", VFIO_USER_CAP_MAX_FDS);
        return true;
    }
    return caps_parse(proxy, qdict, caps_migr, errp);
}

static bool check_multi(VFIOUserProxy *proxy, QObject *qobj, Error **errp)
{
    QBool *qb = qobject_to(QBool, qobj);

    if (qb == NULL) {
        error_setg(errp, "malformed %s", VFIO_USER_CAP_MULTI);
        return false;
    }
    if (qbool_get_bool(qb)) {
        proxy->flags |= VFIO_PROXY_USE_MULTI;
    }
    return true;
}

static struct cap_entry caps_cap[] = {
    { VFIO_USER_CAP_MAX_FDS, check_max_fds },
    { VFIO_USER_CAP_MAX_XFER, check_max_xfer },
    { VFIO_USER_CAP_PGSIZES, check_pgsizes },
    { VFIO_USER_CAP_MAP_MAX, check_max_dma },
    { VFIO_USER_CAP_MIGR, check_migr },
    { VFIO_USER_CAP_MULTI, check_multi },
    { NULL }
};

static bool check_cap(VFIOUserProxy *proxy, QObject *qobj, Error **errp)
{
   QDict *qdict = qobject_to(QDict, qobj);

    if (qdict == NULL) {
        error_setg(errp, "malformed %s", VFIO_USER_CAP);
        return false;
    }
    return caps_parse(proxy, qdict, caps_cap, errp);
}

static struct cap_entry ver_0_0[] = {
    { VFIO_USER_CAP, check_cap },
    { NULL }
};

static bool caps_check(VFIOUserProxy *proxy, int minor, const char *caps,
                       Error **errp)
{
    QObject *qobj;
    QDict *qdict;
    bool ret;

    qobj = qobject_from_json(caps, NULL);
    if (qobj == NULL) {
        error_setg(errp, "malformed capabilities %s", caps);
        return false;
    }
    qdict = qobject_to(QDict, qobj);
    if (qdict == NULL) {
        error_setg(errp, "capabilities %s not an object", caps);
        qobject_unref(qobj);
        return false;
    }
    ret = caps_parse(proxy, qdict, ver_0_0, errp);

    qobject_unref(qobj);
    return ret;
}

static GString *caps_json(void)
{
    QDict *dict = qdict_new();
    QDict *capdict = qdict_new();
    QDict *migdict = qdict_new();
    GString *str;

    qdict_put_int(migdict, VFIO_USER_CAP_PGSIZE, VFIO_USER_DEF_PGSIZE);
    qdict_put_int(migdict, VFIO_USER_CAP_MAX_BITMAP, VFIO_USER_DEF_MAX_BITMAP);
    qdict_put_obj(capdict, VFIO_USER_CAP_MIGR, QOBJECT(migdict));

    qdict_put_int(capdict, VFIO_USER_CAP_MAX_FDS, VFIO_USER_MAX_MAX_FDS);
    qdict_put_int(capdict, VFIO_USER_CAP_MAX_XFER, VFIO_USER_DEF_MAX_XFER);
    qdict_put_int(capdict, VFIO_USER_CAP_PGSIZES, VFIO_USER_DEF_PGSIZE);
    qdict_put_int(capdict, VFIO_USER_CAP_MAP_MAX, VFIO_USER_DEF_MAP_MAX);
    qdict_put_bool(capdict, VFIO_USER_CAP_MULTI, true);

    qdict_put_obj(dict, VFIO_USER_CAP, QOBJECT(capdict));

    str = qobject_to_json(QOBJECT(dict));
    qobject_unref(dict);
    return str;
}

bool vfio_user_validate_version(VFIOUserProxy *proxy, Error **errp)
{
    g_autofree VFIOUserVersion *msgp = NULL;
    GString *caps;
    char *reply;
    int size, caplen;

    caps = caps_json();
    caplen = caps->len + 1;
    size = sizeof(*msgp) + caplen;
    msgp = g_malloc0(size);

    vfio_user_request_msg(&msgp->hdr, VFIO_USER_VERSION, size, 0);
    msgp->major = VFIO_USER_MAJOR_VER;
    msgp->minor = VFIO_USER_MINOR_VER;
    memcpy(&msgp->capabilities, caps->str, caplen);
    g_string_free(caps, true);
    trace_vfio_user_version(msgp->major, msgp->minor, msgp->capabilities);

    if (!vfio_user_send_wait(proxy, &msgp->hdr, NULL, 0, errp)) {
        return false;
    }

    if (msgp->hdr.flags & VFIO_USER_ERROR) {
        error_setg_errno(errp, msgp->hdr.error_reply, "version reply");
        return false;
    }

    if (msgp->major != VFIO_USER_MAJOR_VER ||
        msgp->minor > VFIO_USER_MINOR_VER) {
        error_setg(errp, "incompatible server version");
        return false;
    }

    reply = msgp->capabilities;
    if (reply[msgp->hdr.size - sizeof(*msgp) - 1] != '\0') {
        error_setg(errp, "corrupt version reply");
        return false;
    }

    if (!caps_check(proxy, msgp->minor, reply, errp)) {
        return false;
    }

    trace_vfio_user_version(msgp->major, msgp->minor, msgp->capabilities);
    return true;
}

void vfio_user_flush_multi(VFIOUserProxy *proxy)
{
    VFIOUserMsg *msg;
    VFIOUserWRMulti *wm = proxy->wr_multi;
    Error *local_err = NULL;

    proxy->wr_multi = NULL;

    /* adjust size for actual # of writes */
    wm->hdr.size -= (VFIO_USER_MULTI_MAX - wm->wr_cnt) * sizeof(VFIOUserWROne);

    msg = vfio_user_getmsg(proxy, &wm->hdr, NULL);
    msg->id = wm->hdr.id;
    msg->rsize = 0;
    msg->type = VFIO_MSG_ASYNC;
    trace_vfio_user_wrmulti("flush", wm->wr_cnt);

    if (!vfio_user_send_queued(proxy, msg, &local_err)) {
        error_report_err(local_err);
        vfio_user_recycle(proxy, msg);
    }
}

void vfio_user_create_multi(VFIOUserProxy *proxy)
{
    VFIOUserWRMulti *wm;

    wm = g_malloc0(sizeof(*wm));
    vfio_user_request_msg(&wm->hdr, VFIO_USER_REGION_WRITE_MULTI,
                          sizeof(*wm), VFIO_USER_NO_REPLY);
    proxy->wr_multi = wm;
}

void vfio_user_add_multi(VFIOUserProxy *proxy, uint8_t index,
                         off_t offset, uint32_t count, void *data)
{
    VFIOUserWRMulti *wm = proxy->wr_multi;
    VFIOUserWROne *w1 = &wm->wrs[wm->wr_cnt];

    w1->offset = offset;
    w1->region = index;
    w1->count = count;
    memcpy(&w1->data, data, count);

    wm->wr_cnt++;
    trace_vfio_user_wrmulti("add", wm->wr_cnt);
    if (wm->wr_cnt == VFIO_USER_MULTI_MAX ||
        proxy->num_outgoing < VFIO_USER_OUT_LOW) {
        vfio_user_flush_multi(proxy);
    }
}
