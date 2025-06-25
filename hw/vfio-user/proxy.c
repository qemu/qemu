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
#include "qemu/error-report.h"
#include "qemu/lockable.h"
#include "qemu/main-loop.h"
#include "system/iothread.h"

static IOThread *vfio_user_iothread;

static void vfio_user_shutdown(VFIOUserProxy *proxy);
static VFIOUserMsg *vfio_user_getmsg(VFIOUserProxy *proxy, VFIOUserHdr *hdr,
                                     VFIOUserFDs *fds);
static VFIOUserFDs *vfio_user_getfds(int numfds);
static void vfio_user_recycle(VFIOUserProxy *proxy, VFIOUserMsg *msg);

static void vfio_user_recv(void *opaque);
static void vfio_user_cb(void *opaque);

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
    QTAILQ_INSERT_HEAD(&proxy->free, msg, next);
}

static VFIOUserFDs *vfio_user_getfds(int numfds)
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

static void vfio_user_cb(void *opaque)
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
    aio_bh_schedule_oneshot(proxy->ctx, vfio_user_cb, proxy);
    qemu_cond_wait(&proxy->close_cv, &proxy->lock);

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
