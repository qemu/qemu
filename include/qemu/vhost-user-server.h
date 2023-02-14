/*
 * Sharing QEMU devices via vhost-user protocol
 *
 * Copyright (c) Coiby Xu <coiby.xu@gmail.com>.
 * Copyright (c) 2020 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#ifndef VHOST_USER_SERVER_H
#define VHOST_USER_SERVER_H

#include "subprojects/libvhost-user/libvhost-user.h" /* only for the type definitions */
#include "io/channel-socket.h"
#include "io/channel-file.h"
#include "io/net-listener.h"
#include "qapi/error.h"
#include "standard-headers/linux/virtio_blk.h"

/* A kick fd that we monitor on behalf of libvhost-user */
typedef struct VuFdWatch {
    VuDev *vu_dev;
    int fd; /*kick fd*/
    void *pvt;
    vu_watch_cb cb;
    QTAILQ_ENTRY(VuFdWatch) next;
} VuFdWatch;

/**
 * VuServer:
 * A vhost-user server instance with user-defined VuDevIface callbacks.
 * Vhost-user device backends can be implemented using VuServer. VuDevIface
 * callbacks and virtqueue kicks run in the given AioContext.
 */
typedef struct {
    QIONetListener *listener;
    QEMUBH *restart_listener_bh;
    AioContext *ctx;
    int max_queues;
    const VuDevIface *vu_iface;

    /* Protected by ctx lock */
    unsigned int refcount;
    bool wait_idle;
    VuDev vu_dev;
    QIOChannel *ioc; /* The I/O channel with the client */
    QIOChannelSocket *sioc; /* The underlying data channel with the client */
    QTAILQ_HEAD(, VuFdWatch) vu_fd_watches;

    Coroutine *co_trip; /* coroutine for processing VhostUserMsg */
} VuServer;

bool vhost_user_server_start(VuServer *server,
                             SocketAddress *unix_socket,
                             AioContext *ctx,
                             uint16_t max_queues,
                             const VuDevIface *vu_iface,
                             Error **errp);

void vhost_user_server_stop(VuServer *server);

void vhost_user_server_ref(VuServer *server);
void vhost_user_server_unref(VuServer *server);

void vhost_user_server_attach_aio_context(VuServer *server, AioContext *ctx);
void vhost_user_server_detach_aio_context(VuServer *server);

#endif /* VHOST_USER_SERVER_H */
