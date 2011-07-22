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
#include "qemu-error.h"
#include "trace.h"
#include "blockdev.h"
#include "virtio-blk.h"
#ifdef __linux__
# include <scsi/sg.h>
#endif

typedef struct VirtIOBlock
{
    VirtIODevice vdev;
    BlockDriverState *bs;
    VirtQueue *vq;
    void *rq;
    QEMUBH *bh;
    BlockConf *conf;
    char *serial;
    unsigned short sector_mask;
    DeviceState *qdev;
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

    trace_virtio_blk_req_complete(req, status);

    stb_p(&req->in->status, status);
    virtqueue_push(s->vq, &req->elem, req->qiov.size + sizeof(*req->in));
    virtio_notify(&s->vdev, s->vq);

    qemu_free(req);
}

static int virtio_blk_handle_rw_error(VirtIOBlockReq *req, int error,
    int is_read)
{
    BlockErrorAction action = bdrv_get_on_error(req->dev->bs, is_read);
    VirtIOBlock *s = req->dev;

    if (action == BLOCK_ERR_IGNORE) {
        bdrv_mon_event(s->bs, BDRV_ACTION_IGNORE, is_read);
        return 0;
    }

    if ((error == ENOSPC && action == BLOCK_ERR_STOP_ENOSPC)
            || action == BLOCK_ERR_STOP_ANY) {
        req->next = s->rq;
        s->rq = req;
        bdrv_mon_event(s->bs, BDRV_ACTION_STOP, is_read);
        vm_stop(VMSTOP_DISKFULL);
    } else {
        virtio_blk_req_complete(req, VIRTIO_BLK_S_IOERR);
        bdrv_mon_event(s->bs, BDRV_ACTION_REPORT, is_read);
    }

    return 1;
}

static void virtio_blk_rw_complete(void *opaque, int ret)
{
    VirtIOBlockReq *req = opaque;

    trace_virtio_blk_rw_complete(req, ret);

    if (ret) {
        int is_read = !(ldl_p(&req->out->type) & VIRTIO_BLK_T_OUT);
        if (virtio_blk_handle_rw_error(req, -ret, is_read))
            return;
    }

    virtio_blk_req_complete(req, VIRTIO_BLK_S_OK);
}

static void virtio_blk_flush_complete(void *opaque, int ret)
{
    VirtIOBlockReq *req = opaque;

    if (ret) {
        if (virtio_blk_handle_rw_error(req, -ret, 0)) {
            return;
        }
    }

    virtio_blk_req_complete(req, VIRTIO_BLK_S_OK);
}

static VirtIOBlockReq *virtio_blk_alloc_request(VirtIOBlock *s)
{
    VirtIOBlockReq *req = qemu_malloc(sizeof(*req));
    req->dev = s;
    req->qiov.size = 0;
    req->next = NULL;
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
    int ret;
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
    } else {
        /*
         * Some SCSI commands don't actually transfer any data.
         */
        hdr.dxfer_direction = SG_DXFER_NONE;
    }

    hdr.sbp = req->elem.in_sg[req->elem.in_num - 3].iov_base;
    hdr.mx_sb_len = req->elem.in_sg[req->elem.in_num - 3].iov_len;

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

    stl_p(&req->scsi->errors, hdr.status);
    stl_p(&req->scsi->residual, hdr.resid);
    stl_p(&req->scsi->sense_len, hdr.sb_len_wr);
    stl_p(&req->scsi->data_len, hdr.dxfer_len);

    virtio_blk_req_complete(req, status);
}
#else
static void virtio_blk_handle_scsi(VirtIOBlockReq *req)
{
    virtio_blk_req_complete(req, VIRTIO_BLK_S_UNSUPP);
}
#endif /* __linux__ */

typedef struct MultiReqBuffer {
    BlockRequest        blkreq[32];
    unsigned int        num_writes;
} MultiReqBuffer;

static void virtio_submit_multiwrite(BlockDriverState *bs, MultiReqBuffer *mrb)
{
    int i, ret;

    if (!mrb->num_writes) {
        return;
    }

    ret = bdrv_aio_multiwrite(bs, mrb->blkreq, mrb->num_writes);
    if (ret != 0) {
        for (i = 0; i < mrb->num_writes; i++) {
            if (mrb->blkreq[i].error) {
                virtio_blk_rw_complete(mrb->blkreq[i].opaque, -EIO);
            }
        }
    }

    mrb->num_writes = 0;
}

static void virtio_blk_handle_flush(VirtIOBlockReq *req, MultiReqBuffer *mrb)
{
    BlockDriverAIOCB *acb;

    /*
     * Make sure all outstanding writes are posted to the backing device.
     */
    virtio_submit_multiwrite(req->dev->bs, mrb);

    acb = bdrv_aio_flush(req->dev->bs, virtio_blk_flush_complete, req);
    if (!acb) {
        virtio_blk_flush_complete(req, -EIO);
    }
}

static void virtio_blk_handle_write(VirtIOBlockReq *req, MultiReqBuffer *mrb)
{
    BlockRequest *blkreq;
    uint64_t sector;

    sector = ldq_p(&req->out->sector);

    trace_virtio_blk_handle_write(req, sector, req->qiov.size / 512);

    if (sector & req->dev->sector_mask) {
        virtio_blk_rw_complete(req, -EIO);
        return;
    }
    if (req->qiov.size % req->dev->conf->logical_block_size) {
        virtio_blk_rw_complete(req, -EIO);
        return;
    }

    if (mrb->num_writes == 32) {
        virtio_submit_multiwrite(req->dev->bs, mrb);
    }

    blkreq = &mrb->blkreq[mrb->num_writes];
    blkreq->sector = sector;
    blkreq->nb_sectors = req->qiov.size / BDRV_SECTOR_SIZE;
    blkreq->qiov = &req->qiov;
    blkreq->cb = virtio_blk_rw_complete;
    blkreq->opaque = req;
    blkreq->error = 0;

    mrb->num_writes++;
}

static void virtio_blk_handle_read(VirtIOBlockReq *req)
{
    BlockDriverAIOCB *acb;
    uint64_t sector;

    sector = ldq_p(&req->out->sector);

    if (sector & req->dev->sector_mask) {
        virtio_blk_rw_complete(req, -EIO);
        return;
    }
    if (req->qiov.size % req->dev->conf->logical_block_size) {
        virtio_blk_rw_complete(req, -EIO);
        return;
    }

    acb = bdrv_aio_readv(req->dev->bs, sector, &req->qiov,
                         req->qiov.size / BDRV_SECTOR_SIZE,
                         virtio_blk_rw_complete, req);
    if (!acb) {
        virtio_blk_rw_complete(req, -EIO);
    }
}

static void virtio_blk_handle_request(VirtIOBlockReq *req,
    MultiReqBuffer *mrb)
{
    uint32_t type;

    if (req->elem.out_num < 1 || req->elem.in_num < 1) {
        error_report("virtio-blk missing headers");
        exit(1);
    }

    if (req->elem.out_sg[0].iov_len < sizeof(*req->out) ||
        req->elem.in_sg[req->elem.in_num - 1].iov_len < sizeof(*req->in)) {
        error_report("virtio-blk header not in correct element");
        exit(1);
    }

    req->out = (void *)req->elem.out_sg[0].iov_base;
    req->in = (void *)req->elem.in_sg[req->elem.in_num - 1].iov_base;

    type = ldl_p(&req->out->type);

    if (type & VIRTIO_BLK_T_FLUSH) {
        virtio_blk_handle_flush(req, mrb);
    } else if (type & VIRTIO_BLK_T_SCSI_CMD) {
        virtio_blk_handle_scsi(req);
    } else if (type & VIRTIO_BLK_T_GET_ID) {
        VirtIOBlock *s = req->dev;

        /*
         * NB: per existing s/n string convention the string is
         * terminated by '\0' only when shorter than buffer.
         */
        strncpy(req->elem.in_sg[0].iov_base,
                s->serial ? s->serial : "",
                MIN(req->elem.in_sg[0].iov_len, VIRTIO_BLK_ID_BYTES));
        virtio_blk_req_complete(req, VIRTIO_BLK_S_OK);
    } else if (type & VIRTIO_BLK_T_OUT) {
        qemu_iovec_init_external(&req->qiov, &req->elem.out_sg[1],
                                 req->elem.out_num - 1);
        virtio_blk_handle_write(req, mrb);
    } else {
        qemu_iovec_init_external(&req->qiov, &req->elem.in_sg[0],
                                 req->elem.in_num - 1);
        virtio_blk_handle_read(req);
    }
}

static void virtio_blk_handle_output(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIOBlock *s = to_virtio_blk(vdev);
    VirtIOBlockReq *req;
    MultiReqBuffer mrb = {
        .num_writes = 0,
    };

    while ((req = virtio_blk_get_request(s))) {
        virtio_blk_handle_request(req, &mrb);
    }

    virtio_submit_multiwrite(s->bs, &mrb);

    /*
     * FIXME: Want to check for completions before returning to guest mode,
     * so cached reads and writes are reported as quickly as possible. But
     * that should be done in the generic block layer.
     */
}

static void virtio_blk_dma_restart_bh(void *opaque)
{
    VirtIOBlock *s = opaque;
    VirtIOBlockReq *req = s->rq;
    MultiReqBuffer mrb = {
        .num_writes = 0,
    };

    qemu_bh_delete(s->bh);
    s->bh = NULL;

    s->rq = NULL;

    while (req) {
        virtio_blk_handle_request(req, &mrb);
        req = req->next;
    }

    virtio_submit_multiwrite(s->bs, &mrb);
}

static void virtio_blk_dma_restart_cb(void *opaque, int running, int reason)
{
    VirtIOBlock *s = opaque;

    if (!running)
        return;

    if (!s->bh) {
        s->bh = qemu_bh_new(virtio_blk_dma_restart_bh, s);
        qemu_bh_schedule(s->bh);
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

/* coalesce internal state, copy to pci i/o region 0
 */
static void virtio_blk_update_config(VirtIODevice *vdev, uint8_t *config)
{
    VirtIOBlock *s = to_virtio_blk(vdev);
    struct virtio_blk_config blkcfg;
    uint64_t capacity;
    int cylinders, heads, secs;

    bdrv_get_geometry(s->bs, &capacity);
    bdrv_get_geometry_hint(s->bs, &cylinders, &heads, &secs);
    memset(&blkcfg, 0, sizeof(blkcfg));
    stq_raw(&blkcfg.capacity, capacity);
    stl_raw(&blkcfg.seg_max, 128 - 2);
    stw_raw(&blkcfg.cylinders, cylinders);
    blkcfg.heads = heads;
    blkcfg.sectors = secs & ~s->sector_mask;
    blkcfg.blk_size = s->conf->logical_block_size;
    blkcfg.size_max = 0;
    blkcfg.physical_block_exp = get_physical_block_exp(s->conf);
    blkcfg.alignment_offset = 0;
    blkcfg.min_io_size = s->conf->min_io_size / blkcfg.blk_size;
    blkcfg.opt_io_size = s->conf->opt_io_size / blkcfg.blk_size;
    memcpy(config, &blkcfg, sizeof(struct virtio_blk_config));
}

static uint32_t virtio_blk_get_features(VirtIODevice *vdev, uint32_t features)
{
    VirtIOBlock *s = to_virtio_blk(vdev);

    features |= (1 << VIRTIO_BLK_F_SEG_MAX);
    features |= (1 << VIRTIO_BLK_F_GEOMETRY);
    features |= (1 << VIRTIO_BLK_F_TOPOLOGY);
    features |= (1 << VIRTIO_BLK_F_BLK_SIZE);

    if (bdrv_enable_write_cache(s->bs))
        features |= (1 << VIRTIO_BLK_F_WCACHE);
    
    if (bdrv_is_read_only(s->bs))
        features |= 1 << VIRTIO_BLK_F_RO;

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
        s->rq = req;

        virtqueue_map_sg(req->elem.in_sg, req->elem.in_addr,
            req->elem.in_num, 1);
        virtqueue_map_sg(req->elem.out_sg, req->elem.out_addr,
            req->elem.out_num, 0);
    }

    return 0;
}

static void virtio_blk_change_cb(void *opaque, int reason)
{
    VirtIOBlock *s = opaque;

    if (reason & CHANGE_SIZE) {
        virtio_notify_config(&s->vdev);
    }
}

VirtIODevice *virtio_blk_init(DeviceState *dev, BlockConf *conf,
                              char **serial)
{
    VirtIOBlock *s;
    int cylinders, heads, secs;
    static int virtio_blk_id;
    DriveInfo *dinfo;

    if (!conf->bs) {
        error_report("virtio-blk-pci: drive property not set");
        return NULL;
    }
    if (!bdrv_is_inserted(conf->bs)) {
        error_report("Device needs media, but drive is empty");
        return NULL;
    }

    if (!*serial) {
        /* try to fall back to value set with legacy -drive serial=... */
        dinfo = drive_get_by_blockdev(conf->bs);
        if (*dinfo->serial) {
            *serial = strdup(dinfo->serial);
        }
    }

    s = (VirtIOBlock *)virtio_common_init("virtio-blk", VIRTIO_ID_BLOCK,
                                          sizeof(struct virtio_blk_config),
                                          sizeof(VirtIOBlock));

    s->vdev.get_config = virtio_blk_update_config;
    s->vdev.get_features = virtio_blk_get_features;
    s->vdev.reset = virtio_blk_reset;
    s->bs = conf->bs;
    s->conf = conf;
    s->serial = *serial;
    s->rq = NULL;
    s->sector_mask = (s->conf->logical_block_size / BDRV_SECTOR_SIZE) - 1;
    bdrv_guess_geometry(s->bs, &cylinders, &heads, &secs);

    s->vq = virtio_add_queue(&s->vdev, 128, virtio_blk_handle_output);

    qemu_add_vm_change_state_handler(virtio_blk_dma_restart_cb, s);
    s->qdev = dev;
    register_savevm(dev, "virtio-blk", virtio_blk_id++, 2,
                    virtio_blk_save, virtio_blk_load, s);
    bdrv_set_removable(s->bs, 0);
    bdrv_set_change_cb(s->bs, virtio_blk_change_cb, s);
    s->bs->buffer_alignment = conf->logical_block_size;

    add_boot_device_path(conf->bootindex, dev, "/disk@0,0");

    return &s->vdev;
}

void virtio_blk_exit(VirtIODevice *vdev)
{
    VirtIOBlock *s = to_virtio_blk(vdev);
    unregister_savevm(s->qdev, "virtio-blk", s);
}
