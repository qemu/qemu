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
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/lockable.h"
#include "system/iothread.h"

static IOThread *vfio_user_iothread;

static void vfio_user_shutdown(VFIOUserProxy *proxy);


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
 * Functions only called by iothread
 */

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

    QTAILQ_INIT(&proxy->outgoing);
    QTAILQ_INIT(&proxy->incoming);
    QTAILQ_INIT(&proxy->free);
    QTAILQ_INIT(&proxy->pending);
    QLIST_INSERT_HEAD(&vfio_user_sockets, proxy, next);

    return proxy;
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
