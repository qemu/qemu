/*
 * Sharing QEMU block devices via vhost-user protocol
 *
 * Parts of the code based on nbd/server.c.
 *
 * Copyright (c) Coiby Xu <coiby.xu@gmail.com>.
 * Copyright (c) 2020 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "block/block.h"
#include "subprojects/libvhost-user/libvhost-user.h" /* only for the type definitions */
#include "standard-headers/linux/virtio_blk.h"
#include "qemu/vhost-user-server.h"
#include "vhost-user-blk-server.h"
#include "qapi/error.h"
#include "qom/object_interfaces.h"
#include "util/block-helpers.h"
#include "virtio-blk-handler.h"

enum {
    VHOST_USER_BLK_NUM_QUEUES_DEFAULT = 1,
};

typedef struct VuBlkReq {
    VuVirtqElement elem;
    VuServer *server;
    struct VuVirtq *vq;
} VuBlkReq;

/* vhost user block device */
typedef struct {
    BlockExport export;
    VuServer vu_server;
    VirtioBlkHandler handler;
    QIOChannelSocket *sioc;
    struct virtio_blk_config blkcfg;
} VuBlkExport;

static void vu_blk_req_complete(VuBlkReq *req, size_t in_len)
{
    VuDev *vu_dev = &req->server->vu_dev;

    vu_queue_push(vu_dev, req->vq, &req->elem, in_len);
    vu_queue_notify(vu_dev, req->vq);

    free(req);
}

/*
 * Called with server in_flight counter increased, must decrease before
 * returning.
 */
static void coroutine_fn vu_blk_virtio_process_req(void *opaque)
{
    VuBlkReq *req = opaque;
    VuServer *server = req->server;
    VuVirtqElement *elem = &req->elem;
    VuBlkExport *vexp = container_of(server, VuBlkExport, vu_server);
    VirtioBlkHandler *handler = &vexp->handler;
    struct iovec *in_iov = elem->in_sg;
    struct iovec *out_iov = elem->out_sg;
    unsigned in_num = elem->in_num;
    unsigned out_num = elem->out_num;
    int in_len;

    in_len = virtio_blk_process_req(handler, in_iov, out_iov,
                                    in_num, out_num);
    if (in_len < 0) {
        free(req);
        vhost_user_server_dec_in_flight(server);
        return;
    }

    vu_blk_req_complete(req, in_len);
    vhost_user_server_dec_in_flight(server);
}

static void vu_blk_process_vq(VuDev *vu_dev, int idx)
{
    VuServer *server = container_of(vu_dev, VuServer, vu_dev);
    VuVirtq *vq = vu_get_queue(vu_dev, idx);

    while (1) {
        VuBlkReq *req;

        req = vu_queue_pop(vu_dev, vq, sizeof(VuBlkReq));
        if (!req) {
            break;
        }

        req->server = server;
        req->vq = vq;

        Coroutine *co =
            qemu_coroutine_create(vu_blk_virtio_process_req, req);

        vhost_user_server_inc_in_flight(server);
        qemu_coroutine_enter(co);
    }
}

static void vu_blk_queue_set_started(VuDev *vu_dev, int idx, bool started)
{
    VuVirtq *vq;

    assert(vu_dev);

    vq = vu_get_queue(vu_dev, idx);
    vu_set_queue_handler(vu_dev, vq, started ? vu_blk_process_vq : NULL);
}

static uint64_t vu_blk_get_features(VuDev *dev)
{
    uint64_t features;
    VuServer *server = container_of(dev, VuServer, vu_dev);
    VuBlkExport *vexp = container_of(server, VuBlkExport, vu_server);
    features = 1ull << VIRTIO_BLK_F_SIZE_MAX |
               1ull << VIRTIO_BLK_F_SEG_MAX |
               1ull << VIRTIO_BLK_F_TOPOLOGY |
               1ull << VIRTIO_BLK_F_BLK_SIZE |
               1ull << VIRTIO_BLK_F_FLUSH |
               1ull << VIRTIO_BLK_F_DISCARD |
               1ull << VIRTIO_BLK_F_WRITE_ZEROES |
               1ull << VIRTIO_BLK_F_CONFIG_WCE |
               1ull << VIRTIO_BLK_F_MQ |
               1ull << VIRTIO_F_VERSION_1 |
               1ull << VIRTIO_RING_F_INDIRECT_DESC |
               1ull << VIRTIO_RING_F_EVENT_IDX |
               1ull << VHOST_USER_F_PROTOCOL_FEATURES;

    if (!vexp->handler.writable) {
        features |= 1ull << VIRTIO_BLK_F_RO;
    }

    return features;
}

static uint64_t vu_blk_get_protocol_features(VuDev *dev)
{
    return 1ull << VHOST_USER_PROTOCOL_F_CONFIG;
}

static int
vu_blk_get_config(VuDev *vu_dev, uint8_t *config, uint32_t len)
{
    VuServer *server = container_of(vu_dev, VuServer, vu_dev);
    VuBlkExport *vexp = container_of(server, VuBlkExport, vu_server);

    if (len > sizeof(struct virtio_blk_config)) {
        return -1;
    }

    memcpy(config, &vexp->blkcfg, len);
    return 0;
}

static int
vu_blk_set_config(VuDev *vu_dev, const uint8_t *data,
                    uint32_t offset, uint32_t size, uint32_t flags)
{
    VuServer *server = container_of(vu_dev, VuServer, vu_dev);
    VuBlkExport *vexp = container_of(server, VuBlkExport, vu_server);
    uint8_t wce;

    /* don't support live migration */
    if (flags != VHOST_SET_CONFIG_TYPE_FRONTEND) {
        return -EINVAL;
    }

    if (offset != offsetof(struct virtio_blk_config, wce) ||
        size != 1) {
        return -EINVAL;
    }

    wce = *data;
    vexp->blkcfg.wce = wce;
    blk_set_enable_write_cache(vexp->export.blk, wce);
    return 0;
}

/*
 * When the client disconnects, it sends a VHOST_USER_NONE request
 * and vu_process_message will simple call exit which cause the VM
 * to exit abruptly.
 * To avoid this issue,  process VHOST_USER_NONE request ahead
 * of vu_process_message.
 *
 */
static int vu_blk_process_msg(VuDev *dev, VhostUserMsg *vmsg, int *do_reply)
{
    if (vmsg->request == VHOST_USER_NONE) {
        dev->panic(dev, "disconnect");
        return true;
    }
    return false;
}

static const VuDevIface vu_blk_iface = {
    .get_features          = vu_blk_get_features,
    .queue_set_started     = vu_blk_queue_set_started,
    .get_protocol_features = vu_blk_get_protocol_features,
    .get_config            = vu_blk_get_config,
    .set_config            = vu_blk_set_config,
    .process_msg           = vu_blk_process_msg,
};

static void blk_aio_attached(AioContext *ctx, void *opaque)
{
    VuBlkExport *vexp = opaque;

    /*
     * The actual attach will happen in vu_blk_drained_end() and we just
     * restore ctx here.
     */
    vexp->export.ctx = ctx;
}

static void blk_aio_detach(void *opaque)
{
    VuBlkExport *vexp = opaque;

    /*
     * The actual detach already happened in vu_blk_drained_begin() but from
     * this point on we must not access ctx anymore.
     */
    vexp->export.ctx = NULL;
}

static void
vu_blk_initialize_config(BlockDriverState *bs,
                         struct virtio_blk_config *config,
                         uint32_t blk_size,
                         uint16_t num_queues)
{
    config->capacity =
        cpu_to_le64(bdrv_getlength(bs) >> VIRTIO_BLK_SECTOR_BITS);
    config->blk_size = cpu_to_le32(blk_size);
    config->size_max = cpu_to_le32(0);
    config->seg_max = cpu_to_le32(128 - 2);
    config->min_io_size = cpu_to_le16(1);
    config->opt_io_size = cpu_to_le32(1);
    config->num_queues = cpu_to_le16(num_queues);
    config->max_discard_sectors =
        cpu_to_le32(VIRTIO_BLK_MAX_DISCARD_SECTORS);
    config->max_discard_seg = cpu_to_le32(1);
    config->discard_sector_alignment =
        cpu_to_le32(blk_size >> VIRTIO_BLK_SECTOR_BITS);
    config->max_write_zeroes_sectors
        = cpu_to_le32(VIRTIO_BLK_MAX_WRITE_ZEROES_SECTORS);
    config->max_write_zeroes_seg = cpu_to_le32(1);
}

static void vu_blk_exp_request_shutdown(BlockExport *exp)
{
    VuBlkExport *vexp = container_of(exp, VuBlkExport, export);

    vhost_user_server_stop(&vexp->vu_server);
}

static void vu_blk_exp_resize(void *opaque)
{
    VuBlkExport *vexp = opaque;
    BlockDriverState *bs = blk_bs(vexp->handler.blk);
    int64_t new_size = bdrv_getlength(bs);

    if (new_size < 0) {
        error_printf("Failed to get length of block node '%s'",
                     bdrv_get_node_name(bs));
        return;
    }

    vexp->blkcfg.capacity = cpu_to_le64(new_size >> VIRTIO_BLK_SECTOR_BITS);

    vu_config_change_msg(&vexp->vu_server.vu_dev);
}

/* Called with vexp->export.ctx acquired */
static void vu_blk_drained_begin(void *opaque)
{
    VuBlkExport *vexp = opaque;

    vexp->vu_server.quiescing = true;
    vhost_user_server_detach_aio_context(&vexp->vu_server);
}

/* Called with vexp->export.blk AioContext acquired */
static void vu_blk_drained_end(void *opaque)
{
    VuBlkExport *vexp = opaque;

    vexp->vu_server.quiescing = false;
    vhost_user_server_attach_aio_context(&vexp->vu_server, vexp->export.ctx);
}

/*
 * Ensures that bdrv_drained_begin() waits until in-flight requests complete
 * and the server->co_trip coroutine has terminated. It will be restarted in
 * vhost_user_server_attach_aio_context().
 *
 * Called with vexp->export.ctx acquired.
 */
static bool vu_blk_drained_poll(void *opaque)
{
    VuBlkExport *vexp = opaque;
    VuServer *server = &vexp->vu_server;

    return server->co_trip || vhost_user_server_has_in_flight(server);
}

static const BlockDevOps vu_blk_dev_ops = {
    .drained_begin = vu_blk_drained_begin,
    .drained_end   = vu_blk_drained_end,
    .drained_poll  = vu_blk_drained_poll,
    .resize_cb = vu_blk_exp_resize,
};

static int vu_blk_exp_create(BlockExport *exp, BlockExportOptions *opts,
                             Error **errp)
{
    VuBlkExport *vexp = container_of(exp, VuBlkExport, export);
    BlockExportOptionsVhostUserBlk *vu_opts = &opts->u.vhost_user_blk;
    Error *local_err = NULL;
    uint64_t logical_block_size;
    uint16_t num_queues = VHOST_USER_BLK_NUM_QUEUES_DEFAULT;

    vexp->blkcfg.wce = 0;

    if (vu_opts->has_logical_block_size) {
        logical_block_size = vu_opts->logical_block_size;
    } else {
        logical_block_size = VIRTIO_BLK_SECTOR_SIZE;
    }
    check_block_size(exp->id, "logical-block-size", logical_block_size,
                     &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return -EINVAL;
    }

    if (vu_opts->has_num_queues) {
        num_queues = vu_opts->num_queues;
    }
    if (num_queues == 0) {
        error_setg(errp, "num-queues must be greater than 0");
        return -EINVAL;
    }
    vexp->handler.blk = exp->blk;
    vexp->handler.serial = g_strdup("vhost_user_blk");
    vexp->handler.logical_block_size = logical_block_size;
    vexp->handler.writable = opts->writable;

    vu_blk_initialize_config(blk_bs(exp->blk), &vexp->blkcfg,
                             logical_block_size, num_queues);

    blk_add_aio_context_notifier(exp->blk, blk_aio_attached, blk_aio_detach,
                                 vexp);

    blk_set_dev_ops(exp->blk, &vu_blk_dev_ops, vexp);

    if (!vhost_user_server_start(&vexp->vu_server, vu_opts->addr, exp->ctx,
                                 num_queues, &vu_blk_iface, errp)) {
        blk_remove_aio_context_notifier(exp->blk, blk_aio_attached,
                                        blk_aio_detach, vexp);
        g_free(vexp->handler.serial);
        return -EADDRNOTAVAIL;
    }

    return 0;
}

static void vu_blk_exp_delete(BlockExport *exp)
{
    VuBlkExport *vexp = container_of(exp, VuBlkExport, export);

    blk_remove_aio_context_notifier(exp->blk, blk_aio_attached, blk_aio_detach,
                                    vexp);
    g_free(vexp->handler.serial);
}

const BlockExportDriver blk_exp_vhost_user_blk = {
    .type               = BLOCK_EXPORT_TYPE_VHOST_USER_BLK,
    .instance_size      = sizeof(VuBlkExport),
    .create             = vu_blk_exp_create,
    .delete             = vu_blk_exp_delete,
    .request_shutdown   = vu_blk_exp_request_shutdown,
};
