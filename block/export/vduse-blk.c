/*
 * Export QEMU block device via VDUSE
 *
 * Copyright (C) 2022 Bytedance Inc. and/or its affiliates. All rights reserved.
 *
 * Author:
 *   Xie Yongji <xieyongji@bytedance.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <sys/eventfd.h>

#include "qapi/error.h"
#include "block/export.h"
#include "qemu/error-report.h"
#include "util/block-helpers.h"
#include "subprojects/libvduse/libvduse.h"
#include "virtio-blk-handler.h"

#include "standard-headers/linux/virtio_blk.h"

#define VDUSE_DEFAULT_NUM_QUEUE 1
#define VDUSE_DEFAULT_QUEUE_SIZE 256

typedef struct VduseBlkExport {
    BlockExport export;
    VirtioBlkHandler handler;
    VduseDev *dev;
    uint16_t num_queues;
    char *recon_file;
    unsigned int inflight; /* atomic */
    bool vqs_started;
} VduseBlkExport;

typedef struct VduseBlkReq {
    VduseVirtqElement elem;
    VduseVirtq *vq;
} VduseBlkReq;

static void vduse_blk_inflight_inc(VduseBlkExport *vblk_exp)
{
    if (qatomic_fetch_inc(&vblk_exp->inflight) == 0) {
        /* Prevent export from being deleted */
        blk_exp_ref(&vblk_exp->export);
    }
}

static void vduse_blk_inflight_dec(VduseBlkExport *vblk_exp)
{
    if (qatomic_fetch_dec(&vblk_exp->inflight) == 1) {
        /* Wake AIO_WAIT_WHILE() */
        aio_wait_kick();

        /* Now the export can be deleted */
        blk_exp_unref(&vblk_exp->export);
    }
}

static void vduse_blk_req_complete(VduseBlkReq *req, size_t in_len)
{
    vduse_queue_push(req->vq, &req->elem, in_len);
    vduse_queue_notify(req->vq);

    free(req);
}

static void coroutine_fn vduse_blk_virtio_process_req(void *opaque)
{
    VduseBlkReq *req = opaque;
    VduseVirtq *vq = req->vq;
    VduseDev *dev = vduse_queue_get_dev(vq);
    VduseBlkExport *vblk_exp = vduse_dev_get_priv(dev);
    VirtioBlkHandler *handler = &vblk_exp->handler;
    VduseVirtqElement *elem = &req->elem;
    struct iovec *in_iov = elem->in_sg;
    struct iovec *out_iov = elem->out_sg;
    unsigned in_num = elem->in_num;
    unsigned out_num = elem->out_num;
    int in_len;

    in_len = virtio_blk_process_req(handler, in_iov,
                                    out_iov, in_num, out_num);
    if (in_len < 0) {
        free(req);
        return;
    }

    vduse_blk_req_complete(req, in_len);
    vduse_blk_inflight_dec(vblk_exp);
}

static void vduse_blk_vq_handler(VduseDev *dev, VduseVirtq *vq)
{
    VduseBlkExport *vblk_exp = vduse_dev_get_priv(dev);

    while (1) {
        VduseBlkReq *req;

        req = vduse_queue_pop(vq, sizeof(VduseBlkReq));
        if (!req) {
            break;
        }
        req->vq = vq;

        Coroutine *co =
            qemu_coroutine_create(vduse_blk_virtio_process_req, req);

        vduse_blk_inflight_inc(vblk_exp);
        qemu_coroutine_enter(co);
    }
}

static void on_vduse_vq_kick(void *opaque)
{
    VduseVirtq *vq = opaque;
    VduseDev *dev = vduse_queue_get_dev(vq);
    int fd = vduse_queue_get_fd(vq);
    eventfd_t kick_data;

    if (eventfd_read(fd, &kick_data) == -1) {
        error_report("failed to read data from eventfd");
        return;
    }

    vduse_blk_vq_handler(dev, vq);
}

static void vduse_blk_enable_queue(VduseDev *dev, VduseVirtq *vq)
{
    VduseBlkExport *vblk_exp = vduse_dev_get_priv(dev);

    if (!vblk_exp->vqs_started) {
        return; /* vduse_blk_drained_end() will start vqs later */
    }

    aio_set_fd_handler(vblk_exp->export.ctx, vduse_queue_get_fd(vq),
                       on_vduse_vq_kick, NULL, NULL, NULL, vq);
    /* Make sure we don't miss any kick afer reconnecting */
    eventfd_write(vduse_queue_get_fd(vq), 1);
}

static void vduse_blk_disable_queue(VduseDev *dev, VduseVirtq *vq)
{
    VduseBlkExport *vblk_exp = vduse_dev_get_priv(dev);
    int fd = vduse_queue_get_fd(vq);

    if (fd < 0) {
        return;
    }

    aio_set_fd_handler(vblk_exp->export.ctx, fd,
                       NULL, NULL, NULL, NULL, NULL);
}

static const VduseOps vduse_blk_ops = {
    .enable_queue = vduse_blk_enable_queue,
    .disable_queue = vduse_blk_disable_queue,
};

static void on_vduse_dev_kick(void *opaque)
{
    VduseDev *dev = opaque;

    vduse_dev_handler(dev);
}

static void vduse_blk_attach_ctx(VduseBlkExport *vblk_exp, AioContext *ctx)
{
    aio_set_fd_handler(vblk_exp->export.ctx, vduse_dev_get_fd(vblk_exp->dev),
                       on_vduse_dev_kick, NULL, NULL, NULL,
                       vblk_exp->dev);

    /* Virtqueues are handled by vduse_blk_drained_end() */
}

static void vduse_blk_detach_ctx(VduseBlkExport *vblk_exp)
{
    aio_set_fd_handler(vblk_exp->export.ctx, vduse_dev_get_fd(vblk_exp->dev),
                       NULL, NULL, NULL, NULL, NULL);

    /* Virtqueues are handled by vduse_blk_drained_begin() */
}


static void blk_aio_attached(AioContext *ctx, void *opaque)
{
    VduseBlkExport *vblk_exp = opaque;

    vblk_exp->export.ctx = ctx;
    vduse_blk_attach_ctx(vblk_exp, ctx);
}

static void blk_aio_detach(void *opaque)
{
    VduseBlkExport *vblk_exp = opaque;

    vduse_blk_detach_ctx(vblk_exp);
    vblk_exp->export.ctx = NULL;
}

static void vduse_blk_resize(void *opaque)
{
    BlockExport *exp = opaque;
    VduseBlkExport *vblk_exp = container_of(exp, VduseBlkExport, export);
    struct virtio_blk_config config;

    config.capacity =
            cpu_to_le64(blk_getlength(exp->blk) >> VIRTIO_BLK_SECTOR_BITS);
    vduse_dev_update_config(vblk_exp->dev, sizeof(config.capacity),
                            offsetof(struct virtio_blk_config, capacity),
                            (char *)&config.capacity);
}

static void vduse_blk_stop_virtqueues(VduseBlkExport *vblk_exp)
{
    for (uint16_t i = 0; i < vblk_exp->num_queues; i++) {
        VduseVirtq *vq = vduse_dev_get_queue(vblk_exp->dev, i);
        vduse_blk_disable_queue(vblk_exp->dev, vq);
    }

    vblk_exp->vqs_started = false;
}

static void vduse_blk_start_virtqueues(VduseBlkExport *vblk_exp)
{
    vblk_exp->vqs_started = true;

    for (uint16_t i = 0; i < vblk_exp->num_queues; i++) {
        VduseVirtq *vq = vduse_dev_get_queue(vblk_exp->dev, i);
        vduse_blk_enable_queue(vblk_exp->dev, vq);
    }
}

static void vduse_blk_drained_begin(void *opaque)
{
    BlockExport *exp = opaque;
    VduseBlkExport *vblk_exp = container_of(exp, VduseBlkExport, export);

    vduse_blk_stop_virtqueues(vblk_exp);
}

static void vduse_blk_drained_end(void *opaque)
{
    BlockExport *exp = opaque;
    VduseBlkExport *vblk_exp = container_of(exp, VduseBlkExport, export);

    vduse_blk_start_virtqueues(vblk_exp);
}

static bool vduse_blk_drained_poll(void *opaque)
{
    BlockExport *exp = opaque;
    VduseBlkExport *vblk_exp = container_of(exp, VduseBlkExport, export);

    return qatomic_read(&vblk_exp->inflight) > 0;
}

static const BlockDevOps vduse_block_ops = {
    .resize_cb     = vduse_blk_resize,
    .drained_begin = vduse_blk_drained_begin,
    .drained_end   = vduse_blk_drained_end,
    .drained_poll  = vduse_blk_drained_poll,
};

static int vduse_blk_exp_create(BlockExport *exp, BlockExportOptions *opts,
                                Error **errp)
{
    VduseBlkExport *vblk_exp = container_of(exp, VduseBlkExport, export);
    BlockExportOptionsVduseBlk *vblk_opts = &opts->u.vduse_blk;
    uint64_t logical_block_size = VIRTIO_BLK_SECTOR_SIZE;
    uint16_t num_queues = VDUSE_DEFAULT_NUM_QUEUE;
    uint16_t queue_size = VDUSE_DEFAULT_QUEUE_SIZE;
    Error *local_err = NULL;
    struct virtio_blk_config config = { 0 };
    uint64_t features;
    int i, ret;

    if (vblk_opts->has_num_queues) {
        num_queues = vblk_opts->num_queues;
        if (num_queues == 0) {
            error_setg(errp, "num-queues must be greater than 0");
            return -EINVAL;
        }
    }

    if (vblk_opts->has_queue_size) {
        queue_size = vblk_opts->queue_size;
        if (queue_size <= 2 || !is_power_of_2(queue_size) ||
            queue_size > VIRTQUEUE_MAX_SIZE) {
            error_setg(errp, "queue-size is invalid");
            return -EINVAL;
        }
    }

    if (vblk_opts->has_logical_block_size) {
        logical_block_size = vblk_opts->logical_block_size;
        check_block_size(exp->id, "logical-block-size", logical_block_size,
                         &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return -EINVAL;
        }
    }
    vblk_exp->num_queues = num_queues;
    vblk_exp->handler.blk = exp->blk;
    vblk_exp->handler.serial = g_strdup(vblk_opts->serial ?: "");
    vblk_exp->handler.logical_block_size = logical_block_size;
    vblk_exp->handler.writable = opts->writable;
    vblk_exp->vqs_started = true;

    config.capacity =
            cpu_to_le64(blk_getlength(exp->blk) >> VIRTIO_BLK_SECTOR_BITS);
    config.seg_max = cpu_to_le32(queue_size - 2);
    config.min_io_size = cpu_to_le16(1);
    config.opt_io_size = cpu_to_le32(1);
    config.num_queues = cpu_to_le16(num_queues);
    config.blk_size = cpu_to_le32(logical_block_size);
    config.max_discard_sectors = cpu_to_le32(VIRTIO_BLK_MAX_DISCARD_SECTORS);
    config.max_discard_seg = cpu_to_le32(1);
    config.discard_sector_alignment =
        cpu_to_le32(logical_block_size >> VIRTIO_BLK_SECTOR_BITS);
    config.max_write_zeroes_sectors =
        cpu_to_le32(VIRTIO_BLK_MAX_WRITE_ZEROES_SECTORS);
    config.max_write_zeroes_seg = cpu_to_le32(1);

    features = vduse_get_virtio_features() |
               (1ULL << VIRTIO_BLK_F_SEG_MAX) |
               (1ULL << VIRTIO_BLK_F_TOPOLOGY) |
               (1ULL << VIRTIO_BLK_F_BLK_SIZE) |
               (1ULL << VIRTIO_BLK_F_FLUSH) |
               (1ULL << VIRTIO_BLK_F_DISCARD) |
               (1ULL << VIRTIO_BLK_F_WRITE_ZEROES);

    if (num_queues > 1) {
        features |= 1ULL << VIRTIO_BLK_F_MQ;
    }
    if (!opts->writable) {
        features |= 1ULL << VIRTIO_BLK_F_RO;
    }

    vblk_exp->dev = vduse_dev_create(vblk_opts->name, VIRTIO_ID_BLOCK, 0,
                                     features, num_queues,
                                     sizeof(struct virtio_blk_config),
                                     (char *)&config, &vduse_blk_ops,
                                     vblk_exp);
    if (!vblk_exp->dev) {
        error_setg(errp, "failed to create vduse device");
        ret = -ENOMEM;
        goto err_dev;
    }

    vblk_exp->recon_file = g_strdup_printf("%s/vduse-blk-%s",
                                           g_get_tmp_dir(), vblk_opts->name);
    if (vduse_set_reconnect_log_file(vblk_exp->dev, vblk_exp->recon_file)) {
        error_setg(errp, "failed to set reconnect log file");
        ret = -EINVAL;
        goto err;
    }

    for (i = 0; i < num_queues; i++) {
        vduse_dev_setup_queue(vblk_exp->dev, i, queue_size);
    }

    aio_set_fd_handler(exp->ctx, vduse_dev_get_fd(vblk_exp->dev),
                       on_vduse_dev_kick, NULL, NULL, NULL, vblk_exp->dev);

    blk_add_aio_context_notifier(exp->blk, blk_aio_attached, blk_aio_detach,
                                 vblk_exp);
    blk_set_dev_ops(exp->blk, &vduse_block_ops, exp);

    /*
     * We handle draining ourselves using an in-flight counter and by disabling
     * virtqueue fd handlers. Do not queue BlockBackend requests, they need to
     * complete so the in-flight counter reaches zero.
     */
    blk_set_disable_request_queuing(exp->blk, true);

    return 0;
err:
    vduse_dev_destroy(vblk_exp->dev);
    g_free(vblk_exp->recon_file);
err_dev:
    g_free(vblk_exp->handler.serial);
    return ret;
}

static void vduse_blk_exp_delete(BlockExport *exp)
{
    VduseBlkExport *vblk_exp = container_of(exp, VduseBlkExport, export);
    int ret;

    assert(qatomic_read(&vblk_exp->inflight) == 0);

    vduse_blk_detach_ctx(vblk_exp);
    blk_remove_aio_context_notifier(exp->blk, blk_aio_attached, blk_aio_detach,
                                    vblk_exp);
    ret = vduse_dev_destroy(vblk_exp->dev);
    if (ret != -EBUSY) {
        unlink(vblk_exp->recon_file);
    }
    g_free(vblk_exp->recon_file);
    g_free(vblk_exp->handler.serial);
}

/* Called with exp->ctx acquired */
static void vduse_blk_exp_request_shutdown(BlockExport *exp)
{
    VduseBlkExport *vblk_exp = container_of(exp, VduseBlkExport, export);

    vduse_blk_stop_virtqueues(vblk_exp);
}

const BlockExportDriver blk_exp_vduse_blk = {
    .type               = BLOCK_EXPORT_TYPE_VDUSE_BLK,
    .instance_size      = sizeof(VduseBlkExport),
    .create             = vduse_blk_exp_create,
    .delete             = vduse_blk_exp_delete,
    .request_shutdown   = vduse_blk_exp_request_shutdown,
};
