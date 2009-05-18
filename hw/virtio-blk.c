/*
 * Virtio Block Device
 *
 * Copyright IBM, Corp. 2007
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include <qemu-common.h>
#include <sysemu.h>
#include "virtio-blk.h"
#include "block_int.h"
#ifdef __linux__
# include <scsi/sg.h>
#endif

typedef struct VirtIOBlock
{
    VirtIODevice vdev;
    BlockDriverState *bs;
    VirtQueue *vq;
    void *rq;
} VirtIOBlock;

static VirtIOBlock *to_virtio_blk(VirtIODevice *vdev)
{
    return (VirtIOBlock *)vdev;
}

typedef struct VirtIOBlockReq
{
    VirtIOBlock *dev;
    VirtQueueElement elem;
    struct virtio_blk_inhdr *in;
    struct virtio_blk_outhdr *out;
    struct virtio_scsi_inhdr *scsi;
    QEMUIOVector qiov;
    struct VirtIOBlockReq *next;
} VirtIOBlockReq;

static void virtio_blk_req_complete(VirtIOBlockReq *req, int status)
{
    VirtIOBlock *s = req->dev;

    req->in->status = status;
    virtqueue_push(s->vq, &req->elem, req->qiov.size + sizeof(*req->in));
    virtio_notify(&s->vdev, s->vq);

    qemu_free(req);
}

static int virtio_blk_handle_write_error(VirtIOBlockReq *req, int error)
{
    BlockInterfaceErrorAction action = drive_get_onerror(req->dev->bs);
    VirtIOBlock *s = req->dev;

    if (action == BLOCK_ERR_IGNORE)
        return 0;

    if ((error == ENOSPC && action == BLOCK_ERR_STOP_ENOSPC)
            || action == BLOCK_ERR_STOP_ANY) {
        req->next = s->rq;
        s->rq = req;
        vm_stop(0);
    } else {
        virtio_blk_req_complete(req, VIRTIO_BLK_S_IOERR);
    }

    return 1;
}

static void virtio_blk_rw_complete(void *opaque, int ret)
{
    VirtIOBlockReq *req = opaque;

    if (ret && (req->out->type & VIRTIO_BLK_T_OUT)) {
        if (virtio_blk_handle_write_error(req, -ret))
            return;
    }

    virtio_blk_req_complete(req, VIRTIO_BLK_S_OK);
}

static VirtIOBlockReq *virtio_blk_alloc_request(VirtIOBlock *s)
{
    VirtIOBlockReq *req = qemu_mallocz(sizeof(*req));
    req->dev = s;
    return req;
}

static VirtIOBlockReq *virtio_blk_get_request(VirtIOBlock *s)
{
    VirtIOBlockReq *req = virtio_blk_alloc_request(s);

    if (req != NULL) {
        if (!virtqueue_pop(s->vq, &req->elem)) {
            qemu_free(req);
            return NULL;
        }
    }

    return req;
}

#ifdef __linux__
static void virtio_blk_handle_scsi(VirtIOBlockReq *req)
{
    struct sg_io_hdr hdr;
    int ret, size = 0;
    int status;
    int i;

    /*
     * We require at least one output segment each for the virtio_blk_outhdr
     * and the SCSI command block.
     *
     * We also at least require the virtio_blk_inhdr, the virtio_scsi_inhdr
     * and the sense buffer pointer in the input segments.
     */
    if (req->elem.out_num < 2 || req->elem.in_num < 3) {
        virtio_blk_req_complete(req, VIRTIO_BLK_S_IOERR);
        return;
    }

    /*
     * No support for bidirection commands yet.
     */
    if (req->elem.out_num > 2 && req->elem.in_num > 3) {
        virtio_blk_req_complete(req, VIRTIO_BLK_S_UNSUPP);
        return;
    }

    /*
     * The scsi inhdr is placed in the second-to-last input segment, just
     * before the regular inhdr.
     */
    req->scsi = (void *)req->elem.in_sg[req->elem.in_num - 2].iov_base;
    size = sizeof(*req->in) + sizeof(*req->scsi);

    memset(&hdr, 0, sizeof(struct sg_io_hdr));
    hdr.interface_id = 'S';
    hdr.cmd_len = req->elem.out_sg[1].iov_len;
    hdr.cmdp = req->elem.out_sg[1].iov_base;
    hdr.dxfer_len = 0;

    if (req->elem.out_num > 2) {
        /*
         * If there are more than the minimally required 2 output segments
         * there is write payload starting from the third iovec.
         */
        hdr.dxfer_direction = SG_DXFER_TO_DEV;
        hdr.iovec_count = req->elem.out_num - 2;

        for (i = 0; i < hdr.iovec_count; i++)
            hdr.dxfer_len += req->elem.out_sg[i + 2].iov_len;

        hdr.dxferp = req->elem.out_sg + 2;

    } else if (req->elem.in_num > 3) {
        /*
         * If we have more than 3 input segments the guest wants to actually
         * read data.
         */
        hdr.dxfer_direction = SG_DXFER_FROM_DEV;
        hdr.iovec_count = req->elem.in_num - 3;
        for (i = 0; i < hdr.iovec_count; i++)
            hdr.dxfer_len += req->elem.in_sg[i].iov_len;

        hdr.dxferp = req->elem.in_sg;
        size += hdr.dxfer_len;
    } else {
        /*
         * Some SCSI commands don't actually transfer any data.
         */
        hdr.dxfer_direction = SG_DXFER_NONE;
    }

    hdr.sbp = req->elem.in_sg[req->elem.in_num - 3].iov_base;
    hdr.mx_sb_len = req->elem.in_sg[req->elem.in_num - 3].iov_len;
    size += hdr.mx_sb_len;

    ret = bdrv_ioctl(req->dev->bs, SG_IO, &hdr);
    if (ret) {
        status = VIRTIO_BLK_S_UNSUPP;
        hdr.status = ret;
        hdr.resid = hdr.dxfer_len;
    } else if (hdr.status) {
        status = VIRTIO_BLK_S_IOERR;
    } else {
        status = VIRTIO_BLK_S_OK;
    }

    req->scsi->errors = hdr.status;
    req->scsi->residual = hdr.resid;
    req->scsi->sense_len = hdr.sb_len_wr;
    req->scsi->data_len = hdr.dxfer_len;

    virtio_blk_req_complete(req, status);
}
#else
static void virtio_blk_handle_scsi(VirtIOBlockReq *req)
{
    virtio_blk_req_complete(req, VIRTIO_BLK_S_UNSUPP);
}
#endif /* __linux__ */

static void virtio_blk_handle_write(VirtIOBlockReq *req)
{
    bdrv_aio_writev(req->dev->bs, req->out->sector, &req->qiov,
                    req->qiov.size / 512, virtio_blk_rw_complete, req);
}

static void virtio_blk_handle_read(VirtIOBlockReq *req)
{
    bdrv_aio_readv(req->dev->bs, req->out->sector, &req->qiov,
                   req->qiov.size / 512, virtio_blk_rw_complete, req);
}

static void virtio_blk_handle_output(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIOBlock *s = to_virtio_blk(vdev);
    VirtIOBlockReq *req;

    while ((req = virtio_blk_get_request(s))) {
        if (req->elem.out_num < 1 || req->elem.in_num < 1) {
            fprintf(stderr, "virtio-blk missing headers\n");
            exit(1);
        }

        if (req->elem.out_sg[0].iov_len < sizeof(*req->out) ||
            req->elem.in_sg[req->elem.in_num - 1].iov_len < sizeof(*req->in)) {
            fprintf(stderr, "virtio-blk header not in correct element\n");
            exit(1);
        }

        req->out = (void *)req->elem.out_sg[0].iov_base;
        req->in = (void *)req->elem.in_sg[req->elem.in_num - 1].iov_base;

        if (req->out->type & VIRTIO_BLK_T_SCSI_CMD) {
            virtio_blk_handle_scsi(req);
        } else if (req->out->type & VIRTIO_BLK_T_OUT) {
            qemu_iovec_init_external(&req->qiov, &req->elem.out_sg[1],
                                     req->elem.out_num - 1);
            virtio_blk_handle_write(req);
        } else {
            qemu_iovec_init_external(&req->qiov, &req->elem.in_sg[0],
                                     req->elem.in_num - 1);
            virtio_blk_handle_read(req);
        }
    }
    /*
     * FIXME: Want to check for completions before returning to guest mode,
     * so cached reads and writes are reported as quickly as possible. But
     * that should be done in the generic block layer.
     */
}

static void virtio_blk_dma_restart_cb(void *opaque, int running, int reason)
{
    VirtIOBlock *s = opaque;
    VirtIOBlockReq *req = s->rq;

    if (!running)
        return;

    s->rq = NULL;

    while (req) {
        virtio_blk_handle_write(req);
        req = req->next;
    }
}

static void virtio_blk_reset(VirtIODevice *vdev)
{
    /*
     * This should cancel pending requests, but can't do nicely until there
     * are per-device request lists.
     */
    qemu_aio_flush();
}

static void virtio_blk_update_config(VirtIODevice *vdev, uint8_t *config)
{
    VirtIOBlock *s = to_virtio_blk(vdev);
    struct virtio_blk_config blkcfg;
    uint64_t capacity;
    int cylinders, heads, secs;

    bdrv_get_geometry(s->bs, &capacity);
    bdrv_get_geometry_hint(s->bs, &cylinders, &heads, &secs);
    stq_raw(&blkcfg.capacity, capacity);
    stl_raw(&blkcfg.seg_max, 128 - 2);
    stw_raw(&blkcfg.cylinders, cylinders);
    blkcfg.heads = heads;
    blkcfg.sectors = secs;
    memcpy(config, &blkcfg, sizeof(blkcfg));
}

static uint32_t virtio_blk_get_features(VirtIODevice *vdev)
{
    uint32_t features = 0;

    features |= (1 << VIRTIO_BLK_F_SEG_MAX);
    features |= (1 << VIRTIO_BLK_F_GEOMETRY);
#ifdef __linux__
    features |= (1 << VIRTIO_BLK_F_SCSI);
#endif

    return features;
}

static void virtio_blk_save(QEMUFile *f, void *opaque)
{
    VirtIOBlock *s = opaque;
    VirtIOBlockReq *req = s->rq;

    virtio_save(&s->vdev, f);
    
    while (req) {
        qemu_put_sbyte(f, 1);
        qemu_put_buffer(f, (unsigned char*)&req->elem, sizeof(req->elem));
        req = req->next;
    }
    qemu_put_sbyte(f, 0);
}

static int virtio_blk_load(QEMUFile *f, void *opaque, int version_id)
{
    VirtIOBlock *s = opaque;

    if (version_id != 2)
        return -EINVAL;

    virtio_load(&s->vdev, f);
    while (qemu_get_sbyte(f)) {
        VirtIOBlockReq *req = virtio_blk_alloc_request(s);
        qemu_get_buffer(f, (unsigned char*)&req->elem, sizeof(req->elem));
        req->next = s->rq;
        s->rq = req->next;
    }

    return 0;
}

VirtIODevice *virtio_blk_init(DeviceState *dev)
{
    VirtIOBlock *s;
    int cylinders, heads, secs;
    static int virtio_blk_id;
    BlockDriverState *bs;

    s = (VirtIOBlock *)virtio_common_init("virtio-blk", VIRTIO_ID_BLOCK,
                                          sizeof(struct virtio_blk_config),
                                          sizeof(VirtIOBlock));

    bs = qdev_init_bdrv(dev, IF_VIRTIO);
    s->vdev.get_config = virtio_blk_update_config;
    s->vdev.get_features = virtio_blk_get_features;
    s->vdev.reset = virtio_blk_reset;
    s->bs = bs;
    s->rq = NULL;
    bs->private = dev;
    bdrv_guess_geometry(s->bs, &cylinders, &heads, &secs);
    bdrv_set_geometry_hint(s->bs, cylinders, heads, secs);

    s->vq = virtio_add_queue(&s->vdev, 128, virtio_blk_handle_output);

    qemu_add_vm_change_state_handler(virtio_blk_dma_restart_cb, s);
    register_savevm("virtio-blk", virtio_blk_id++, 2,
                    virtio_blk_save, virtio_blk_load, s);

    return &s->vdev;
}
