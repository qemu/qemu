/*
 * Sharing QEMU block devices via vhost-user protocal
 *
 * Copyright (c) Coiby Xu <coiby.xu@gmail.com>.
 * Copyright (c) 2020 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#ifndef VHOST_USER_BLK_SERVER_H
#define VHOST_USER_BLK_SERVER_H
#include "util/vhost-user-server.h"

typedef struct VuBlockDev VuBlockDev;
#define TYPE_VHOST_USER_BLK_SERVER "vhost-user-blk-server"
#define VHOST_USER_BLK_SERVER(obj) \
   OBJECT_CHECK(VuBlockDev, obj, TYPE_VHOST_USER_BLK_SERVER)

/* vhost user block device */
struct VuBlockDev {
    Object parent_obj;
    char *node_name;
    SocketAddress *addr;
    AioContext *ctx;
    VuServer vu_server;
    bool running;
    uint32_t blk_size;
    BlockBackend *backend;
    QIOChannelSocket *sioc;
    QTAILQ_ENTRY(VuBlockDev) next;
    struct virtio_blk_config blkcfg;
    bool writable;
};

#endif /* VHOST_USER_BLK_SERVER_H */
