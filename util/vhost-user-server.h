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

#include "contrib/libvhost-user/libvhost-user.h"
#include "io/channel-socket.h"
#include "io/channel-file.h"
#include "io/net-listener.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "standard-headers/linux/virtio_blk.h"

typedef struct VuFdWatch {
    VuDev *vu_dev;
    int fd; /*kick fd*/
    void *pvt;
    vu_watch_cb cb;
    bool processing;
    QTAILQ_ENTRY(VuFdWatch) next;
} VuFdWatch;

typedef struct VuServer VuServer;
typedef void DevicePanicNotifierFn(VuServer *server);

struct VuServer {
    QIONetListener *listener;
    AioContext *ctx;
    DevicePanicNotifierFn *device_panic_notifier;
    int max_queues;
    const VuDevIface *vu_iface;
    VuDev vu_dev;
    QIOChannel *ioc; /* The I/O channel with the client */
    QIOChannelSocket *sioc; /* The underlying data channel with the client */
    /* IOChannel for fd provided via VHOST_USER_SET_SLAVE_REQ_FD */
    QIOChannel *ioc_slave;
    QIOChannelSocket *sioc_slave;
    Coroutine *co_trip; /* coroutine for processing VhostUserMsg */
    QTAILQ_HEAD(, VuFdWatch) vu_fd_watches;
    /* restart coroutine co_trip if AIOContext is changed */
    bool aio_context_changed;
    bool processing_msg;
};

bool vhost_user_server_start(VuServer *server,
                             SocketAddress *unix_socket,
                             AioContext *ctx,
                             uint16_t max_queues,
                             DevicePanicNotifierFn *device_panic_notifier,
                             const VuDevIface *vu_iface,
                             Error **errp);

void vhost_user_server_stop(VuServer *server);

void vhost_user_server_set_aio_context(VuServer *server, AioContext *ctx);

#endif /* VHOST_USER_SERVER_H */
