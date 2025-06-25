#ifndef VFIO_USER_PROXY_H
#define VFIO_USER_PROXY_H

/*
 * vfio protocol over a UNIX socket.
 *
 * Copyright © 2018, 2021 Oracle and/or its affiliates.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "io/channel.h"
#include "io/channel-socket.h"

#include "qemu/sockets.h"
#include "hw/vfio-user/protocol.h"

typedef struct {
    int send_fds;
    int recv_fds;
    int *fds;
} VFIOUserFDs;

enum msg_type {
    VFIO_MSG_NONE,
    VFIO_MSG_ASYNC,
    VFIO_MSG_WAIT,
    VFIO_MSG_NOWAIT,
    VFIO_MSG_REQ,
};

typedef struct VFIOUserMsg {
    QTAILQ_ENTRY(VFIOUserMsg) next;
    VFIOUserHdr *hdr;
    VFIOUserFDs *fds;
    uint32_t rsize;
    uint32_t id;
    QemuCond cv;
    bool complete;
    enum msg_type type;
} VFIOUserMsg;


enum proxy_state {
    VFIO_PROXY_CONNECTED = 1,
    VFIO_PROXY_ERROR = 2,
    VFIO_PROXY_CLOSING = 3,
    VFIO_PROXY_CLOSED = 4,
};

typedef QTAILQ_HEAD(VFIOUserMsgQ, VFIOUserMsg) VFIOUserMsgQ;

typedef struct VFIOUserProxy {
    QLIST_ENTRY(VFIOUserProxy) next;
    char *sockname;
    struct QIOChannel *ioc;
    void (*request)(void *opaque, VFIOUserMsg *msg);
    void *req_arg;
    int flags;
    QemuCond close_cv;
    AioContext *ctx;
    QEMUBH *req_bh;

    /*
     * above only changed when BQL is held
     * below are protected by per-proxy lock
     */
    QemuMutex lock;
    VFIOUserMsgQ free;
    VFIOUserMsgQ pending;
    VFIOUserMsgQ incoming;
    VFIOUserMsgQ outgoing;
    VFIOUserMsg *last_nowait;
    VFIOUserMsg *part_recv;
    size_t recv_left;
    enum proxy_state state;
} VFIOUserProxy;

/* VFIOProxy flags */
#define VFIO_PROXY_CLIENT        0x1

typedef struct VFIODevice VFIODevice;

VFIOUserProxy *vfio_user_connect_dev(SocketAddress *addr, Error **errp);
void vfio_user_disconnect(VFIOUserProxy *proxy);
void vfio_user_set_handler(VFIODevice *vbasedev,
                           void (*handler)(void *opaque, VFIOUserMsg *msg),
                           void *reqarg);

#endif /* VFIO_USER_PROXY_H */
