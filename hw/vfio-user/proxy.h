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

#include "qemu/queue.h"
#include "qemu/sockets.h"
#include "qemu/thread.h"
#include "hw/vfio/vfio-device.h"
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
    bool pending;
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
    uint64_t max_xfer_size;
    uint64_t max_send_fds;
    uint64_t max_dma;
    uint64_t dma_pgsizes;
    uint64_t max_bitmap;
    uint64_t migr_pgsize;
    int flags;
    uint32_t wait_time;
    QemuCond close_cv;
    AioContext *ctx;
    QEMUBH *req_bh;
    bool async_ops;

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
    VFIOUserWRMulti *wr_multi;
    int num_outgoing;
    enum proxy_state state;
} VFIOUserProxy;

/* VFIOProxy flags */
#define VFIO_PROXY_CLIENT        0x1
#define VFIO_PROXY_FORCE_QUEUED  0x4
#define VFIO_PROXY_NO_POST       0x8
#define VFIO_PROXY_USE_MULTI     0x16

/* coalescing high and low water marks for VFIOProxy num_outgoing */
#define VFIO_USER_OUT_HIGH       1024
#define VFIO_USER_OUT_LOW        128

typedef struct VFIODevice VFIODevice;

VFIOUserProxy *vfio_user_connect_dev(SocketAddress *addr, Error **errp);
void vfio_user_disconnect(VFIOUserProxy *proxy);
void vfio_user_set_handler(VFIODevice *vbasedev,
                           void (*handler)(void *opaque, VFIOUserMsg *msg),
                           void *reqarg);
bool vfio_user_validate_version(VFIOUserProxy *proxy, Error **errp);

VFIOUserFDs *vfio_user_getfds(int numfds);
void vfio_user_putfds(VFIOUserMsg *msg);

void vfio_user_disable_posted_writes(VFIOUserProxy *proxy);

void vfio_user_request_msg(VFIOUserHdr *hdr, uint16_t cmd,
                           uint32_t size, uint32_t flags);
void vfio_user_wait_reqs(VFIOUserProxy *proxy);
bool vfio_user_send_wait(VFIOUserProxy *proxy, VFIOUserHdr *hdr,
                         VFIOUserFDs *fds, int rsize, Error **errp);
bool vfio_user_send_nowait(VFIOUserProxy *proxy, VFIOUserHdr *hdr,
                           VFIOUserFDs *fds, int rsize, Error **errp);
bool vfio_user_send_async(VFIOUserProxy *proxy, VFIOUserHdr *hdr,
                          VFIOUserFDs *fds, Error **errp);

void vfio_user_send_reply(VFIOUserProxy *proxy, VFIOUserHdr *hdr, int size);
void vfio_user_send_error(VFIOUserProxy *proxy, VFIOUserHdr *hdr, int error);

void vfio_user_flush_multi(VFIOUserProxy *proxy);
void vfio_user_create_multi(VFIOUserProxy *proxy);
void vfio_user_add_multi(VFIOUserProxy *proxy, uint8_t index,
                         off_t offset, uint32_t count, void *data);

#endif /* VFIO_USER_PROXY_H */
