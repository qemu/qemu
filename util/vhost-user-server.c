/*
 * Sharing QEMU devices via vhost-user protocol
 *
 * Copyright (c) Coiby Xu <coiby.xu@gmail.com>.
 * Copyright (c) 2020 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "vhost-user-server.h"

static void vmsg_close_fds(VhostUserMsg *vmsg)
{
    int i;
    for (i = 0; i < vmsg->fd_num; i++) {
        close(vmsg->fds[i]);
    }
}

static void vmsg_unblock_fds(VhostUserMsg *vmsg)
{
    int i;
    for (i = 0; i < vmsg->fd_num; i++) {
        qemu_set_nonblock(vmsg->fds[i]);
    }
}

static void vu_accept(QIONetListener *listener, QIOChannelSocket *sioc,
                      gpointer opaque);

static void close_client(VuServer *server)
{
    /*
     * Before closing the client
     *
     * 1. Let vu_client_trip stop processing new vhost-user msg
     *
     * 2. remove kick_handler
     *
     * 3. wait for the kick handler to be finished
     *
     * 4. wait for the current vhost-user msg to be finished processing
     */

    QIOChannelSocket *sioc = server->sioc;
    /* When this is set vu_client_trip will stop new processing vhost-user message */
    server->sioc = NULL;

    while (server->processing_msg) {
        if (server->ioc->read_coroutine) {
            server->ioc->read_coroutine = NULL;
            qio_channel_set_aio_fd_handler(server->ioc, server->ioc->ctx, NULL,
                                           NULL, server->ioc);
            server->processing_msg = false;
        }
    }

    vu_deinit(&server->vu_dev);

    /* vu_deinit() should have called remove_watch() */
    assert(QTAILQ_EMPTY(&server->vu_fd_watches));

    object_unref(OBJECT(sioc));
    object_unref(OBJECT(server->ioc));
}

static void panic_cb(VuDev *vu_dev, const char *buf)
{
    VuServer *server = container_of(vu_dev, VuServer, vu_dev);

    /* avoid while loop in close_client */
    server->processing_msg = false;

    if (buf) {
        error_report("vu_panic: %s", buf);
    }

    if (server->sioc) {
        close_client(server);
    }

    /*
     * Set the callback function for network listener so another
     * vhost-user client can connect to this server
     */
    qio_net_listener_set_client_func(server->listener,
                                     vu_accept,
                                     server,
                                     NULL);
}

static bool coroutine_fn
vu_message_read(VuDev *vu_dev, int conn_fd, VhostUserMsg *vmsg)
{
    struct iovec iov = {
        .iov_base = (char *)vmsg,
        .iov_len = VHOST_USER_HDR_SIZE,
    };
    int rc, read_bytes = 0;
    Error *local_err = NULL;
    const size_t max_fds = G_N_ELEMENTS(vmsg->fds);
    VuServer *server = container_of(vu_dev, VuServer, vu_dev);
    QIOChannel *ioc = server->ioc;

    vmsg->fd_num = 0;
    if (!ioc) {
        error_report_err(local_err);
        goto fail;
    }

    assert(qemu_in_coroutine());
    do {
        size_t nfds = 0;
        int *fds = NULL;

        /*
         * qio_channel_readv_full may have short reads, keeping calling it
         * until getting VHOST_USER_HDR_SIZE or 0 bytes in total
         */
        rc = qio_channel_readv_full(ioc, &iov, 1, &fds, &nfds, &local_err);
        if (rc < 0) {
            if (rc == QIO_CHANNEL_ERR_BLOCK) {
                assert(local_err == NULL);
                qio_channel_yield(ioc, G_IO_IN);
                continue;
            } else {
                error_report_err(local_err);
                goto fail;
            }
        }

        if (nfds > 0) {
            if (vmsg->fd_num + nfds > max_fds) {
                error_report("A maximum of %zu fds are allowed, "
                             "however got %zu fds now",
                             max_fds, vmsg->fd_num + nfds);
                g_free(fds);
                goto fail;
            }
            memcpy(vmsg->fds + vmsg->fd_num, fds, nfds * sizeof(vmsg->fds[0]));
            vmsg->fd_num += nfds;
            g_free(fds);
        }

        if (rc == 0) { /* socket closed */
            goto fail;
        }

        iov.iov_base += rc;
        iov.iov_len -= rc;
        read_bytes += rc;
    } while (read_bytes != VHOST_USER_HDR_SIZE);

    /* qio_channel_readv_full will make socket fds blocking, unblock them */
    vmsg_unblock_fds(vmsg);
    if (vmsg->size > sizeof(vmsg->payload)) {
        error_report("Error: too big message request: %d, "
                     "size: vmsg->size: %u, "
                     "while sizeof(vmsg->payload) = %zu",
                     vmsg->request, vmsg->size, sizeof(vmsg->payload));
        goto fail;
    }

    struct iovec iov_payload = {
        .iov_base = (char *)&vmsg->payload,
        .iov_len = vmsg->size,
    };
    if (vmsg->size) {
        rc = qio_channel_readv_all_eof(ioc, &iov_payload, 1, &local_err);
        if (rc != 1) {
            if (local_err) {
                error_report_err(local_err);
            }
            goto fail;
        }
    }

    return true;

fail:
    vmsg_close_fds(vmsg);

    return false;
}


static void vu_client_start(VuServer *server);
static coroutine_fn void vu_client_trip(void *opaque)
{
    VuServer *server = opaque;

    while (!server->aio_context_changed && server->sioc) {
        server->processing_msg = true;
        vu_dispatch(&server->vu_dev);
        server->processing_msg = false;
    }

    if (server->aio_context_changed && server->sioc) {
        server->aio_context_changed = false;
        vu_client_start(server);
    }
}

static void vu_client_start(VuServer *server)
{
    server->co_trip = qemu_coroutine_create(vu_client_trip, server);
    aio_co_enter(server->ctx, server->co_trip);
}

/*
 * a wrapper for vu_kick_cb
 *
 * since aio_dispatch can only pass one user data pointer to the
 * callback function, pack VuDev and pvt into a struct. Then unpack it
 * and pass them to vu_kick_cb
 */
static void kick_handler(void *opaque)
{
    VuFdWatch *vu_fd_watch = opaque;
    vu_fd_watch->processing = true;
    vu_fd_watch->cb(vu_fd_watch->vu_dev, 0, vu_fd_watch->pvt);
    vu_fd_watch->processing = false;
}


static VuFdWatch *find_vu_fd_watch(VuServer *server, int fd)
{

    VuFdWatch *vu_fd_watch, *next;
    QTAILQ_FOREACH_SAFE(vu_fd_watch, &server->vu_fd_watches, next, next) {
        if (vu_fd_watch->fd == fd) {
            return vu_fd_watch;
        }
    }
    return NULL;
}

static void
set_watch(VuDev *vu_dev, int fd, int vu_evt,
          vu_watch_cb cb, void *pvt)
{

    VuServer *server = container_of(vu_dev, VuServer, vu_dev);
    g_assert(vu_dev);
    g_assert(fd >= 0);
    g_assert(cb);

    VuFdWatch *vu_fd_watch = find_vu_fd_watch(server, fd);

    if (!vu_fd_watch) {
        VuFdWatch *vu_fd_watch = g_new0(VuFdWatch, 1);

        QTAILQ_INSERT_TAIL(&server->vu_fd_watches, vu_fd_watch, next);

        vu_fd_watch->fd = fd;
        vu_fd_watch->cb = cb;
        qemu_set_nonblock(fd);
        aio_set_fd_handler(server->ioc->ctx, fd, true, kick_handler,
                           NULL, NULL, vu_fd_watch);
        vu_fd_watch->vu_dev = vu_dev;
        vu_fd_watch->pvt = pvt;
    }
}


static void remove_watch(VuDev *vu_dev, int fd)
{
    VuServer *server;
    g_assert(vu_dev);
    g_assert(fd >= 0);

    server = container_of(vu_dev, VuServer, vu_dev);

    VuFdWatch *vu_fd_watch = find_vu_fd_watch(server, fd);

    if (!vu_fd_watch) {
        return;
    }
    aio_set_fd_handler(server->ioc->ctx, fd, true, NULL, NULL, NULL, NULL);

    QTAILQ_REMOVE(&server->vu_fd_watches, vu_fd_watch, next);
    g_free(vu_fd_watch);
}


static void vu_accept(QIONetListener *listener, QIOChannelSocket *sioc,
                      gpointer opaque)
{
    VuServer *server = opaque;

    if (server->sioc) {
        warn_report("Only one vhost-user client is allowed to "
                    "connect the server one time");
        return;
    }

    if (!vu_init(&server->vu_dev, server->max_queues, sioc->fd, panic_cb,
                 vu_message_read, set_watch, remove_watch, server->vu_iface)) {
        error_report("Failed to initialize libvhost-user");
        return;
    }

    /*
     * Unset the callback function for network listener to make another
     * vhost-user client keeping waiting until this client disconnects
     */
    qio_net_listener_set_client_func(server->listener,
                                     NULL,
                                     NULL,
                                     NULL);
    server->sioc = sioc;
    /*
     * Increase the object reference, so sioc will not freed by
     * qio_net_listener_channel_func which will call object_unref(OBJECT(sioc))
     */
    object_ref(OBJECT(server->sioc));
    qio_channel_set_name(QIO_CHANNEL(sioc), "vhost-user client");
    server->ioc = QIO_CHANNEL(sioc);
    object_ref(OBJECT(server->ioc));
    qio_channel_attach_aio_context(server->ioc, server->ctx);
    qio_channel_set_blocking(server->ioc, false, NULL);
    vu_client_start(server);
}


void vhost_user_server_stop(VuServer *server)
{
    if (server->sioc) {
        close_client(server);
    }

    if (server->listener) {
        qio_net_listener_disconnect(server->listener);
        object_unref(OBJECT(server->listener));
    }

}

void vhost_user_server_set_aio_context(VuServer *server, AioContext *ctx)
{
    VuFdWatch *vu_fd_watch, *next;
    void *opaque = NULL;
    IOHandler *io_read = NULL;
    bool attach;

    server->ctx = ctx ? ctx : qemu_get_aio_context();

    if (!server->sioc) {
        /* not yet serving any client*/
        return;
    }

    if (ctx) {
        qio_channel_attach_aio_context(server->ioc, ctx);
        server->aio_context_changed = true;
        io_read = kick_handler;
        attach = true;
    } else {
        qio_channel_detach_aio_context(server->ioc);
        /* server->ioc->ctx keeps the old AioConext */
        ctx = server->ioc->ctx;
        attach = false;
    }

    QTAILQ_FOREACH_SAFE(vu_fd_watch, &server->vu_fd_watches, next, next) {
        if (vu_fd_watch->cb) {
            opaque = attach ? vu_fd_watch : NULL;
            aio_set_fd_handler(ctx, vu_fd_watch->fd, true,
                               io_read, NULL, NULL,
                               opaque);
        }
    }
}


bool vhost_user_server_start(VuServer *server,
                             SocketAddress *socket_addr,
                             AioContext *ctx,
                             uint16_t max_queues,
                             const VuDevIface *vu_iface,
                             Error **errp)
{
    QIONetListener *listener = qio_net_listener_new();
    if (qio_net_listener_open_sync(listener, socket_addr, 1,
                                   errp) < 0) {
        object_unref(OBJECT(listener));
        return false;
    }

    /* zero out unspecified fields */
    *server = (VuServer) {
        .listener              = listener,
        .vu_iface              = vu_iface,
        .max_queues            = max_queues,
        .ctx                   = ctx,
    };

    qio_net_listener_set_name(server->listener, "vhost-user-backend-listener");

    qio_net_listener_set_client_func(server->listener,
                                     vu_accept,
                                     server,
                                     NULL);

    QTAILQ_INIT(&server->vu_fd_watches);
    return true;
}
