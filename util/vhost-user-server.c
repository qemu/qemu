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
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qemu/vhost-user-server.h"
#include "block/aio-wait.h"

/*
 * Theory of operation:
 *
 * VuServer is started and stopped by vhost_user_server_start() and
 * vhost_user_server_stop() from the main loop thread. Starting the server
 * opens a vhost-user UNIX domain socket and listens for incoming connections.
 * Only one connection is allowed at a time.
 *
 * The connection is handled by the vu_client_trip() coroutine in the
 * VuServer->ctx AioContext. The coroutine consists of a vu_dispatch() loop
 * where libvhost-user calls vu_message_read() to receive the next vhost-user
 * protocol messages over the UNIX domain socket.
 *
 * When virtqueues are set up libvhost-user calls set_watch() to monitor kick
 * fds. These fds are also handled in the VuServer->ctx AioContext.
 *
 * Both vu_client_trip() and kick fd monitoring can be stopped by shutting down
 * the socket connection. Shutting down the socket connection causes
 * vu_message_read() to fail since no more data can be received from the socket.
 * After vu_dispatch() fails, vu_client_trip() calls vu_deinit() to stop
 * libvhost-user before terminating the coroutine. vu_deinit() calls
 * remove_watch() to stop monitoring kick fds and this stops virtqueue
 * processing.
 *
 * When vu_client_trip() has finished cleaning up it schedules a BH in the main
 * loop thread to accept the next client connection.
 *
 * When libvhost-user detects an error it calls panic_cb() and sets the
 * dev->broken flag. Both vu_client_trip() and kick fd processing stop when
 * the dev->broken flag is set.
 *
 * It is possible to switch AioContexts using
 * vhost_user_server_detach_aio_context() and
 * vhost_user_server_attach_aio_context(). They stop monitoring fds in the old
 * AioContext and resume monitoring in the new AioContext. The vu_client_trip()
 * coroutine remains in a yielded state during the switch. This is made
 * possible by QIOChannel's support for spurious coroutine re-entry in
 * qio_channel_yield(). The coroutine will restart I/O when re-entered from the
 * new AioContext.
 */

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
        qemu_socket_set_nonblock(vmsg->fds[i]);
    }
}

static void panic_cb(VuDev *vu_dev, const char *buf)
{
    error_report("vu_panic: %s", buf);
}

void vhost_user_server_ref(VuServer *server)
{
    assert(!server->wait_idle);
    server->refcount++;
}

void vhost_user_server_unref(VuServer *server)
{
    server->refcount--;
    if (server->wait_idle && !server->refcount) {
        aio_co_wake(server->co_trip);
    }
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
        rc = qio_channel_readv_full(ioc, &iov, 1, &fds, &nfds, 0, &local_err);
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

static coroutine_fn void vu_client_trip(void *opaque)
{
    VuServer *server = opaque;
    VuDev *vu_dev = &server->vu_dev;

    while (!vu_dev->broken && vu_dispatch(vu_dev)) {
        /* Keep running */
    }

    if (server->refcount) {
        /* Wait for requests to complete before we can unmap the memory */
        server->wait_idle = true;
        qemu_coroutine_yield();
        server->wait_idle = false;
    }
    assert(server->refcount == 0);

    vu_deinit(vu_dev);

    /* vu_deinit() should have called remove_watch() */
    assert(QTAILQ_EMPTY(&server->vu_fd_watches));

    object_unref(OBJECT(server->sioc));
    server->sioc = NULL;

    object_unref(OBJECT(server->ioc));
    server->ioc = NULL;

    server->co_trip = NULL;
    if (server->restart_listener_bh) {
        qemu_bh_schedule(server->restart_listener_bh);
    }
    aio_wait_kick();
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
    VuDev *vu_dev = vu_fd_watch->vu_dev;

    vu_fd_watch->cb(vu_dev, 0, vu_fd_watch->pvt);

    /* Stop vu_client_trip() if an error occurred in vu_fd_watch->cb() */
    if (vu_dev->broken) {
        VuServer *server = container_of(vu_dev, VuServer, vu_dev);

        qio_channel_shutdown(server->ioc, QIO_CHANNEL_SHUTDOWN_BOTH, NULL);
    }
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
        qemu_socket_set_nonblock(fd);
        aio_set_fd_handler(server->ioc->ctx, fd, true, kick_handler,
                           NULL, NULL, NULL, vu_fd_watch);
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
    aio_set_fd_handler(server->ioc->ctx, fd, true,
                       NULL, NULL, NULL, NULL, NULL);

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

    /* TODO vu_message_write() spins if non-blocking! */
    qio_channel_set_blocking(server->ioc, false, NULL);

    server->co_trip = qemu_coroutine_create(vu_client_trip, server);

    aio_context_acquire(server->ctx);
    vhost_user_server_attach_aio_context(server, server->ctx);
    aio_context_release(server->ctx);
}

/* server->ctx acquired by caller */
void vhost_user_server_stop(VuServer *server)
{
    qemu_bh_delete(server->restart_listener_bh);
    server->restart_listener_bh = NULL;

    if (server->sioc) {
        VuFdWatch *vu_fd_watch;

        QTAILQ_FOREACH(vu_fd_watch, &server->vu_fd_watches, next) {
            aio_set_fd_handler(server->ctx, vu_fd_watch->fd, true,
                               NULL, NULL, NULL, NULL, vu_fd_watch);
        }

        qio_channel_shutdown(server->ioc, QIO_CHANNEL_SHUTDOWN_BOTH, NULL);

        AIO_WAIT_WHILE(server->ctx, server->co_trip);
    }

    if (server->listener) {
        qio_net_listener_disconnect(server->listener);
        object_unref(OBJECT(server->listener));
    }
}

/*
 * Allow the next client to connect to the server. Called from a BH in the main
 * loop.
 */
static void restart_listener_bh(void *opaque)
{
    VuServer *server = opaque;

    qio_net_listener_set_client_func(server->listener, vu_accept, server,
                                     NULL);
}

/* Called with ctx acquired */
void vhost_user_server_attach_aio_context(VuServer *server, AioContext *ctx)
{
    VuFdWatch *vu_fd_watch;

    server->ctx = ctx;

    if (!server->sioc) {
        return;
    }

    qio_channel_attach_aio_context(server->ioc, ctx);

    QTAILQ_FOREACH(vu_fd_watch, &server->vu_fd_watches, next) {
        aio_set_fd_handler(ctx, vu_fd_watch->fd, true, kick_handler, NULL,
                           NULL, NULL, vu_fd_watch);
    }

    aio_co_schedule(ctx, server->co_trip);
}

/* Called with server->ctx acquired */
void vhost_user_server_detach_aio_context(VuServer *server)
{
    if (server->sioc) {
        VuFdWatch *vu_fd_watch;

        QTAILQ_FOREACH(vu_fd_watch, &server->vu_fd_watches, next) {
            aio_set_fd_handler(server->ctx, vu_fd_watch->fd, true,
                               NULL, NULL, NULL, NULL, vu_fd_watch);
        }

        qio_channel_detach_aio_context(server->ioc);
    }

    server->ctx = NULL;
}

bool vhost_user_server_start(VuServer *server,
                             SocketAddress *socket_addr,
                             AioContext *ctx,
                             uint16_t max_queues,
                             const VuDevIface *vu_iface,
                             Error **errp)
{
    QEMUBH *bh;
    QIONetListener *listener;

    if (socket_addr->type != SOCKET_ADDRESS_TYPE_UNIX &&
        socket_addr->type != SOCKET_ADDRESS_TYPE_FD) {
        error_setg(errp, "Only socket address types 'unix' and 'fd' are supported");
        return false;
    }

    listener = qio_net_listener_new();
    if (qio_net_listener_open_sync(listener, socket_addr, 1,
                                   errp) < 0) {
        object_unref(OBJECT(listener));
        return false;
    }

    bh = qemu_bh_new(restart_listener_bh, server);

    /* zero out unspecified fields */
    *server = (VuServer) {
        .listener              = listener,
        .restart_listener_bh   = bh,
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
