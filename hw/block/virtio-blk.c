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

#include "qemu/osdep.h"
#include "qemu/defer-call.h"
#include "qapi/error.h"
#include "qemu/iov.h"
#include "qemu/module.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "block/block_int.h"
#include "trace.h"
#include "hw/block/block.h"
#include "hw/qdev-properties.h"
#include "sysemu/blockdev.h"
#include "sysemu/block-ram-registrar.h"
#include "sysemu/sysemu.h"
#include "sysemu/runstate.h"
#include "hw/virtio/virtio-blk.h"
#include "scsi/constants.h"
#ifdef __linux__
# include <scsi/sg.h>
#endif
#include "hw/virtio/virtio-bus.h"
#include "migration/qemu-file-types.h"
#include "hw/virtio/virtio-access.h"
#include "hw/virtio/virtio-blk-common.h"
#include "qemu/coroutine.h"

static void virtio_blk_ioeventfd_attach(VirtIOBlock *s);

static void virtio_blk_init_request(VirtIOBlock *s, VirtQueue *vq,
                                    VirtIOBlockReq *req)
{
    req->dev = s;
    req->vq = vq;
    req->qiov.size = 0;
    req->in_len = 0;
    req->next = NULL;
    req->mr_next = NULL;
}

static void virtio_blk_free_request(VirtIOBlockReq *req)
{
    g_free(req);
}

static void virtio_blk_req_complete(VirtIOBlockReq *req, unsigned char status)
{
    VirtIOBlock *s = req->dev;
    VirtIODevice *vdev = VIRTIO_DEVICE(s);

    trace_virtio_blk_req_complete(vdev, req, status);

    stb_p(&req->in->status, status);
    iov_discard_undo(&req->inhdr_undo);
    iov_discard_undo(&req->outhdr_undo);
    virtqueue_push(req->vq, &req->elem, req->in_len);
    if (qemu_in_iothread()) {
        virtio_notify_irqfd(vdev, req->vq);
    } else {
        virtio_notify(vdev, req->vq);
    }
}

static int virtio_blk_handle_rw_error(VirtIOBlockReq *req, int error,
    bool is_read, bool acct_failed)
{
    VirtIOBlock *s = req->dev;
    BlockErrorAction action = blk_get_error_action(s->blk, is_read, error);

    if (action == BLOCK_ERROR_ACTION_STOP) {
        /* Break the link as the next request is going to be parsed from the
         * ring again. Otherwise we may end up doing a double completion! */
        req->mr_next = NULL;

        WITH_QEMU_LOCK_GUARD(&s->rq_lock) {
            req->next = s->rq;
            s->rq = req;
        }
    } else if (action == BLOCK_ERROR_ACTION_REPORT) {
        virtio_blk_req_complete(req, VIRTIO_BLK_S_IOERR);
        if (acct_failed) {
            block_acct_failed(blk_get_stats(s->blk), &req->acct);
        }
        virtio_blk_free_request(req);
    }

    blk_error_action(s->blk, action, is_read, error);
    return action != BLOCK_ERROR_ACTION_IGNORE;
}

static void virtio_blk_rw_complete(void *opaque, int ret)
{
    VirtIOBlockReq *next = opaque;
    VirtIOBlock *s = next->dev;
    VirtIODevice *vdev = VIRTIO_DEVICE(s);

    while (next) {
        VirtIOBlockReq *req = next;
        next = req->mr_next;
        trace_virtio_blk_rw_complete(vdev, req, ret);

        if (req->qiov.nalloc != -1) {
            /* If nalloc is != -1 req->qiov is a local copy of the original
             * external iovec. It was allocated in submit_requests to be
             * able to merge requests. */
            qemu_iovec_destroy(&req->qiov);
        }

        if (ret) {
            int p = virtio_ldl_p(VIRTIO_DEVICE(s), &req->out.type);
            bool is_read = !(p & VIRTIO_BLK_T_OUT);
            /* Note that memory may be dirtied on read failure.  If the
             * virtio request is not completed here, as is the case for
             * BLOCK_ERROR_ACTION_STOP, the memory may not be copied
             * correctly during live migration.  While this is ugly,
             * it is acceptable because the device is free to write to
             * the memory until the request is completed (which will
             * happen on the other side of the migration).
             */
            if (virtio_blk_handle_rw_error(req, -ret, is_read, true)) {
                continue;
            }
        }

        virtio_blk_req_complete(req, VIRTIO_BLK_S_OK);
        block_acct_done(blk_get_stats(s->blk), &req->acct);
        virtio_blk_free_request(req);
    }
}

static void virtio_blk_flush_complete(void *opaque, int ret)
{
    VirtIOBlockReq *req = opaque;
    VirtIOBlock *s = req->dev;

    if (ret && virtio_blk_handle_rw_error(req, -ret, 0, true)) {
        return;
    }

    virtio_blk_req_complete(req, VIRTIO_BLK_S_OK);
    block_acct_done(blk_get_stats(s->blk), &req->acct);
    virtio_blk_free_request(req);
}

static void virtio_blk_discard_write_zeroes_complete(void *opaque, int ret)
{
    VirtIOBlockReq *req = opaque;
    VirtIOBlock *s = req->dev;
    bool is_write_zeroes = (virtio_ldl_p(VIRTIO_DEVICE(s), &req->out.type) &
                            ~VIRTIO_BLK_T_BARRIER) == VIRTIO_BLK_T_WRITE_ZEROES;

    if (ret && virtio_blk_handle_rw_error(req, -ret, false, is_write_zeroes)) {
        return;
    }

    virtio_blk_req_complete(req, VIRTIO_BLK_S_OK);
    if (is_write_zeroes) {
        block_acct_done(blk_get_stats(s->blk), &req->acct);
    }
    virtio_blk_free_request(req);
}

#ifdef __linux__

typedef struct {
    VirtIOBlockReq *req;
    struct sg_io_hdr hdr;
} VirtIOBlockIoctlReq;

static void virtio_blk_ioctl_complete(void *opaque, int status)
{
    VirtIOBlockIoctlReq *ioctl_req = opaque;
    VirtIOBlockReq *req = ioctl_req->req;
    VirtIOBlock *s = req->dev;
    VirtIODevice *vdev = VIRTIO_DEVICE(s);
    struct virtio_scsi_inhdr *scsi;
    struct sg_io_hdr *hdr;

    scsi = (void *)req->elem.in_sg[req->elem.in_num - 2].iov_base;

    if (status) {
        status = VIRTIO_BLK_S_UNSUPP;
        virtio_stl_p(vdev, &scsi->errors, 255);
        goto out;
    }

    hdr = &ioctl_req->hdr;
    /*
     * From SCSI-Generic-HOWTO: "Some lower level drivers (e.g. ide-scsi)
     * clear the masked_status field [hence status gets cleared too, see
     * block/scsi_ioctl.c] even when a CHECK_CONDITION or COMMAND_TERMINATED
     * status has occurred.  However they do set DRIVER_SENSE in driver_status
     * field. Also a (sb_len_wr > 0) indicates there is a sense buffer.
     */
    if (hdr->status == 0 && hdr->sb_len_wr > 0) {
        hdr->status = CHECK_CONDITION;
    }

    virtio_stl_p(vdev, &scsi->errors,
                 hdr->status | (hdr->msg_status << 8) |
                 (hdr->host_status << 16) | (hdr->driver_status << 24));
    virtio_stl_p(vdev, &scsi->residual, hdr->resid);
    virtio_stl_p(vdev, &scsi->sense_len, hdr->sb_len_wr);
    virtio_stl_p(vdev, &scsi->data_len, hdr->dxfer_len);

out:
    virtio_blk_req_complete(req, status);
    virtio_blk_free_request(req);
    g_free(ioctl_req);
}

#endif

static VirtIOBlockReq *virtio_blk_get_request(VirtIOBlock *s, VirtQueue *vq)
{
    VirtIOBlockReq *req = virtqueue_pop(vq, sizeof(VirtIOBlockReq));

    if (req) {
        virtio_blk_init_request(s, vq, req);
    }
    return req;
}

static int virtio_blk_handle_scsi_req(VirtIOBlockReq *req)
{
    int status = VIRTIO_BLK_S_OK;
    struct virtio_scsi_inhdr *scsi = NULL;
    VirtIOBlock *blk = req->dev;
    VirtIODevice *vdev = VIRTIO_DEVICE(blk);
    VirtQueueElement *elem = &req->elem;

#ifdef __linux__
    int i;
    VirtIOBlockIoctlReq *ioctl_req;
    BlockAIOCB *acb;
#endif

    /*
     * We require at least one output segment each for the virtio_blk_outhdr
     * and the SCSI command block.
     *
     * We also at least require the virtio_blk_inhdr, the virtio_scsi_inhdr
     * and the sense buffer pointer in the input segments.
     */
    if (elem->out_num < 2 || elem->in_num < 3) {
        status = VIRTIO_BLK_S_IOERR;
        goto fail;
    }

    /*
     * The scsi inhdr is placed in the second-to-last input segment, just
     * before the regular inhdr.
     */
    scsi = (void *)elem->in_sg[elem->in_num - 2].iov_base;

    if (!virtio_has_feature(blk->host_features, VIRTIO_BLK_F_SCSI)) {
        status = VIRTIO_BLK_S_UNSUPP;
        goto fail;
    }

    /*
     * No support for bidirection commands yet.
     */
    if (elem->out_num > 2 && elem->in_num > 3) {
        status = VIRTIO_BLK_S_UNSUPP;
        goto fail;
    }

#ifdef __linux__
    ioctl_req = g_new0(VirtIOBlockIoctlReq, 1);
    ioctl_req->req = req;
    ioctl_req->hdr.interface_id = 'S';
    ioctl_req->hdr.cmd_len = elem->out_sg[1].iov_len;
    ioctl_req->hdr.cmdp = elem->out_sg[1].iov_base;
    ioctl_req->hdr.dxfer_len = 0;

    if (elem->out_num > 2) {
        /*
         * If there are more than the minimally required 2 output segments
         * there is write payload starting from the third iovec.
         */
        ioctl_req->hdr.dxfer_direction = SG_DXFER_TO_DEV;
        ioctl_req->hdr.iovec_count = elem->out_num - 2;

        for (i = 0; i < ioctl_req->hdr.iovec_count; i++) {
            ioctl_req->hdr.dxfer_len += elem->out_sg[i + 2].iov_len;
        }

        ioctl_req->hdr.dxferp = elem->out_sg + 2;

    } else if (elem->in_num > 3) {
        /*
         * If we have more than 3 input segments the guest wants to actually
         * read data.
         */
        ioctl_req->hdr.dxfer_direction = SG_DXFER_FROM_DEV;
        ioctl_req->hdr.iovec_count = elem->in_num - 3;
        for (i = 0; i < ioctl_req->hdr.iovec_count; i++) {
            ioctl_req->hdr.dxfer_len += elem->in_sg[i].iov_len;
        }

        ioctl_req->hdr.dxferp = elem->in_sg;
    } else {
        /*
         * Some SCSI commands don't actually transfer any data.
         */
        ioctl_req->hdr.dxfer_direction = SG_DXFER_NONE;
    }

    ioctl_req->hdr.sbp = elem->in_sg[elem->in_num - 3].iov_base;
    ioctl_req->hdr.mx_sb_len = elem->in_sg[elem->in_num - 3].iov_len;

    acb = blk_aio_ioctl(blk->blk, SG_IO, &ioctl_req->hdr,
                        virtio_blk_ioctl_complete, ioctl_req);
    if (!acb) {
        g_free(ioctl_req);
        status = VIRTIO_BLK_S_UNSUPP;
        goto fail;
    }
    return -EINPROGRESS;
#else
    abort();
#endif

fail:
    /* Just put anything nonzero so that the ioctl fails in the guest.  */
    if (scsi) {
        virtio_stl_p(vdev, &scsi->errors, 255);
    }
    return status;
}

static void virtio_blk_handle_scsi(VirtIOBlockReq *req)
{
    int status;

    status = virtio_blk_handle_scsi_req(req);
    if (status != -EINPROGRESS) {
        virtio_blk_req_complete(req, status);
        virtio_blk_free_request(req);
    }
}

static inline void submit_requests(VirtIOBlock *s, MultiReqBuffer *mrb,
                                   int start, int num_reqs, int niov)
{
    BlockBackend *blk = s->blk;
    QEMUIOVector *qiov = &mrb->reqs[start]->qiov;
    int64_t sector_num = mrb->reqs[start]->sector_num;
    bool is_write = mrb->is_write;
    BdrvRequestFlags flags = 0;

    if (num_reqs > 1) {
        int i;
        struct iovec *tmp_iov = qiov->iov;
        int tmp_niov = qiov->niov;

        /* mrb->reqs[start]->qiov was initialized from external so we can't
         * modify it here. We need to initialize it locally and then add the
         * external iovecs. */
        qemu_iovec_init(qiov, niov);

        for (i = 0; i < tmp_niov; i++) {
            qemu_iovec_add(qiov, tmp_iov[i].iov_base, tmp_iov[i].iov_len);
        }

        for (i = start + 1; i < start + num_reqs; i++) {
            qemu_iovec_concat(qiov, &mrb->reqs[i]->qiov, 0,
                              mrb->reqs[i]->qiov.size);
            mrb->reqs[i - 1]->mr_next = mrb->reqs[i];
        }

        trace_virtio_blk_submit_multireq(VIRTIO_DEVICE(mrb->reqs[start]->dev),
                                         mrb, start, num_reqs,
                                         sector_num << BDRV_SECTOR_BITS,
                                         qiov->size, is_write);
        block_acct_merge_done(blk_get_stats(blk),
                              is_write ? BLOCK_ACCT_WRITE : BLOCK_ACCT_READ,
                              num_reqs - 1);
    }

    if (blk_ram_registrar_ok(&s->blk_ram_registrar)) {
        flags |= BDRV_REQ_REGISTERED_BUF;
    }

    if (is_write) {
        blk_aio_pwritev(blk, sector_num << BDRV_SECTOR_BITS, qiov,
                        flags, virtio_blk_rw_complete,
                        mrb->reqs[start]);
    } else {
        blk_aio_preadv(blk, sector_num << BDRV_SECTOR_BITS, qiov,
                       flags, virtio_blk_rw_complete,
                       mrb->reqs[start]);
    }
}

static int multireq_compare(const void *a, const void *b)
{
    const VirtIOBlockReq *req1 = *(VirtIOBlockReq **)a,
                         *req2 = *(VirtIOBlockReq **)b;

    /*
     * Note that we can't simply subtract sector_num1 from sector_num2
     * here as that could overflow the return value.
     */
    if (req1->sector_num > req2->sector_num) {
        return 1;
    } else if (req1->sector_num < req2->sector_num) {
        return -1;
    } else {
        return 0;
    }
}

static void virtio_blk_submit_multireq(VirtIOBlock *s, MultiReqBuffer *mrb)
{
    int i = 0, start = 0, num_reqs = 0, niov = 0, nb_sectors = 0;
    uint32_t max_transfer;
    int64_t sector_num = 0;

    if (mrb->num_reqs == 1) {
        submit_requests(s, mrb, 0, 1, -1);
        mrb->num_reqs = 0;
        return;
    }

    max_transfer = blk_get_max_transfer(mrb->reqs[0]->dev->blk);

    qsort(mrb->reqs, mrb->num_reqs, sizeof(*mrb->reqs),
          &multireq_compare);

    for (i = 0; i < mrb->num_reqs; i++) {
        VirtIOBlockReq *req = mrb->reqs[i];
        if (num_reqs > 0) {
            /*
             * NOTE: We cannot merge the requests in below situations:
             * 1. requests are not sequential
             * 2. merge would exceed maximum number of IOVs
             * 3. merge would exceed maximum transfer length of backend device
             */
            if (sector_num + nb_sectors != req->sector_num ||
                niov > blk_get_max_iov(s->blk) - req->qiov.niov ||
                req->qiov.size > max_transfer ||
                nb_sectors > (max_transfer -
                              req->qiov.size) / BDRV_SECTOR_SIZE) {
                submit_requests(s, mrb, start, num_reqs, niov);
                num_reqs = 0;
            }
        }

        if (num_reqs == 0) {
            sector_num = req->sector_num;
            nb_sectors = niov = 0;
            start = i;
        }

        nb_sectors += req->qiov.size / BDRV_SECTOR_SIZE;
        niov += req->qiov.niov;
        num_reqs++;
    }

    submit_requests(s, mrb, start, num_reqs, niov);
    mrb->num_reqs = 0;
}

static void virtio_blk_handle_flush(VirtIOBlockReq *req, MultiReqBuffer *mrb)
{
    VirtIOBlock *s = req->dev;

    block_acct_start(blk_get_stats(s->blk), &req->acct, 0,
                     BLOCK_ACCT_FLUSH);

    /*
     * Make sure all outstanding writes are posted to the backing device.
     */
    if (mrb->is_write && mrb->num_reqs > 0) {
        virtio_blk_submit_multireq(s, mrb);
    }
    blk_aio_flush(s->blk, virtio_blk_flush_complete, req);
}

static bool virtio_blk_sect_range_ok(VirtIOBlock *dev,
                                     uint64_t sector, size_t size)
{
    uint64_t nb_sectors = size >> BDRV_SECTOR_BITS;
    uint64_t total_sectors;

    if (nb_sectors > BDRV_REQUEST_MAX_SECTORS) {
        return false;
    }
    if (sector & dev->sector_mask) {
        return false;
    }
    if (size % dev->conf.conf.logical_block_size) {
        return false;
    }
    blk_get_geometry(dev->blk, &total_sectors);
    if (sector > total_sectors || nb_sectors > total_sectors - sector) {
        return false;
    }
    return true;
}

static uint8_t virtio_blk_handle_discard_write_zeroes(VirtIOBlockReq *req,
    struct virtio_blk_discard_write_zeroes *dwz_hdr, bool is_write_zeroes)
{
    VirtIOBlock *s = req->dev;
    VirtIODevice *vdev = VIRTIO_DEVICE(s);
    uint64_t sector;
    uint32_t num_sectors, flags, max_sectors;
    uint8_t err_status;
    int bytes;

    sector = virtio_ldq_p(vdev, &dwz_hdr->sector);
    num_sectors = virtio_ldl_p(vdev, &dwz_hdr->num_sectors);
    flags = virtio_ldl_p(vdev, &dwz_hdr->flags);
    max_sectors = is_write_zeroes ? s->conf.max_write_zeroes_sectors :
                  s->conf.max_discard_sectors;

    /*
     * max_sectors is at most BDRV_REQUEST_MAX_SECTORS, this check
     * make us sure that "num_sectors << BDRV_SECTOR_BITS" can fit in
     * the integer variable.
     */
    if (unlikely(num_sectors > max_sectors)) {
        err_status = VIRTIO_BLK_S_IOERR;
        goto err;
    }

    bytes = num_sectors << BDRV_SECTOR_BITS;

    if (unlikely(!virtio_blk_sect_range_ok(s, sector, bytes))) {
        err_status = VIRTIO_BLK_S_IOERR;
        goto err;
    }

    /*
     * The device MUST set the status byte to VIRTIO_BLK_S_UNSUPP for discard
     * and write zeroes commands if any unknown flag is set.
     */
    if (unlikely(flags & ~VIRTIO_BLK_WRITE_ZEROES_FLAG_UNMAP)) {
        err_status = VIRTIO_BLK_S_UNSUPP;
        goto err;
    }

    if (is_write_zeroes) { /* VIRTIO_BLK_T_WRITE_ZEROES */
        int blk_aio_flags = 0;

        if (flags & VIRTIO_BLK_WRITE_ZEROES_FLAG_UNMAP) {
            blk_aio_flags |= BDRV_REQ_MAY_UNMAP;
        }

        block_acct_start(blk_get_stats(s->blk), &req->acct, bytes,
                         BLOCK_ACCT_WRITE);

        blk_aio_pwrite_zeroes(s->blk, sector << BDRV_SECTOR_BITS,
                              bytes, blk_aio_flags,
                              virtio_blk_discard_write_zeroes_complete, req);
    } else { /* VIRTIO_BLK_T_DISCARD */
        /*
         * The device MUST set the status byte to VIRTIO_BLK_S_UNSUPP for
         * discard commands if the unmap flag is set.
         */
        if (unlikely(flags & VIRTIO_BLK_WRITE_ZEROES_FLAG_UNMAP)) {
            err_status = VIRTIO_BLK_S_UNSUPP;
            goto err;
        }

        blk_aio_pdiscard(s->blk, sector << BDRV_SECTOR_BITS, bytes,
                         virtio_blk_discard_write_zeroes_complete, req);
    }

    return VIRTIO_BLK_S_OK;

err:
    if (is_write_zeroes) {
        block_acct_invalid(blk_get_stats(s->blk), BLOCK_ACCT_WRITE);
    }
    return err_status;
}

typedef struct ZoneCmdData {
    VirtIOBlockReq *req;
    struct iovec *in_iov;
    unsigned in_num;
    union {
        struct {
            unsigned int nr_zones;
            BlockZoneDescriptor *zones;
        } zone_report_data;
        struct {
            int64_t offset;
        } zone_append_data;
    };
} ZoneCmdData;

/*
 * check zoned_request: error checking before issuing requests. If all checks
 * passed, return true.
 * append: true if only zone append requests issued.
 */
static bool check_zoned_request(VirtIOBlock *s, int64_t offset, int64_t len,
                             bool append, uint8_t *status) {
    BlockDriverState *bs = blk_bs(s->blk);
    int index;

    if (!virtio_has_feature(s->host_features, VIRTIO_BLK_F_ZONED)) {
        *status = VIRTIO_BLK_S_UNSUPP;
        return false;
    }

    if (offset < 0 || len < 0 || len > (bs->total_sectors << BDRV_SECTOR_BITS)
        || offset > (bs->total_sectors << BDRV_SECTOR_BITS) - len) {
        *status = VIRTIO_BLK_S_ZONE_INVALID_CMD;
        return false;
    }

    if (append) {
        if (bs->bl.write_granularity) {
            if ((offset % bs->bl.write_granularity) != 0) {
                *status = VIRTIO_BLK_S_ZONE_UNALIGNED_WP;
                return false;
            }
        }

        index = offset / bs->bl.zone_size;
        if (BDRV_ZT_IS_CONV(bs->wps->wp[index])) {
            *status = VIRTIO_BLK_S_ZONE_INVALID_CMD;
            return false;
        }

        if (len / 512 > bs->bl.max_append_sectors) {
            if (bs->bl.max_append_sectors == 0) {
                *status = VIRTIO_BLK_S_UNSUPP;
            } else {
                *status = VIRTIO_BLK_S_ZONE_INVALID_CMD;
            }
            return false;
        }
    }
    return true;
}

static void virtio_blk_zone_report_complete(void *opaque, int ret)
{
    ZoneCmdData *data = opaque;
    VirtIOBlockReq *req = data->req;
    VirtIODevice *vdev = VIRTIO_DEVICE(req->dev);
    struct iovec *in_iov = data->in_iov;
    unsigned in_num = data->in_num;
    int64_t zrp_size, n, j = 0;
    int64_t nz = data->zone_report_data.nr_zones;
    int8_t err_status = VIRTIO_BLK_S_OK;
    struct virtio_blk_zone_report zrp_hdr = (struct virtio_blk_zone_report) {
        .nr_zones = cpu_to_le64(nz),
    };

    trace_virtio_blk_zone_report_complete(vdev, req, nz, ret);
    if (ret) {
        err_status = VIRTIO_BLK_S_ZONE_INVALID_CMD;
        goto out;
    }

    zrp_size = sizeof(struct virtio_blk_zone_report)
               + sizeof(struct virtio_blk_zone_descriptor) * nz;
    n = iov_from_buf(in_iov, in_num, 0, &zrp_hdr, sizeof(zrp_hdr));
    if (n != sizeof(zrp_hdr)) {
        virtio_error(vdev, "Driver provided input buffer that is too small!");
        err_status = VIRTIO_BLK_S_ZONE_INVALID_CMD;
        goto out;
    }

    for (size_t i = sizeof(zrp_hdr); i < zrp_size;
        i += sizeof(struct virtio_blk_zone_descriptor), ++j) {
        struct virtio_blk_zone_descriptor desc =
            (struct virtio_blk_zone_descriptor) {
                .z_start = cpu_to_le64(data->zone_report_data.zones[j].start
                    >> BDRV_SECTOR_BITS),
                .z_cap = cpu_to_le64(data->zone_report_data.zones[j].cap
                    >> BDRV_SECTOR_BITS),
                .z_wp = cpu_to_le64(data->zone_report_data.zones[j].wp
                    >> BDRV_SECTOR_BITS),
        };

        switch (data->zone_report_data.zones[j].type) {
        case BLK_ZT_CONV:
            desc.z_type = VIRTIO_BLK_ZT_CONV;
            break;
        case BLK_ZT_SWR:
            desc.z_type = VIRTIO_BLK_ZT_SWR;
            break;
        case BLK_ZT_SWP:
            desc.z_type = VIRTIO_BLK_ZT_SWP;
            break;
        default:
            g_assert_not_reached();
        }

        switch (data->zone_report_data.zones[j].state) {
        case BLK_ZS_RDONLY:
            desc.z_state = VIRTIO_BLK_ZS_RDONLY;
            break;
        case BLK_ZS_OFFLINE:
            desc.z_state = VIRTIO_BLK_ZS_OFFLINE;
            break;
        case BLK_ZS_EMPTY:
            desc.z_state = VIRTIO_BLK_ZS_EMPTY;
            break;
        case BLK_ZS_CLOSED:
            desc.z_state = VIRTIO_BLK_ZS_CLOSED;
            break;
        case BLK_ZS_FULL:
            desc.z_state = VIRTIO_BLK_ZS_FULL;
            break;
        case BLK_ZS_EOPEN:
            desc.z_state = VIRTIO_BLK_ZS_EOPEN;
            break;
        case BLK_ZS_IOPEN:
            desc.z_state = VIRTIO_BLK_ZS_IOPEN;
            break;
        case BLK_ZS_NOT_WP:
            desc.z_state = VIRTIO_BLK_ZS_NOT_WP;
            break;
        default:
            g_assert_not_reached();
        }

        /* TODO: it takes O(n^2) time complexity. Optimizations required. */
        n = iov_from_buf(in_iov, in_num, i, &desc, sizeof(desc));
        if (n != sizeof(desc)) {
            virtio_error(vdev, "Driver provided input buffer "
                               "for descriptors that is too small!");
            err_status = VIRTIO_BLK_S_ZONE_INVALID_CMD;
        }
    }

out:
    virtio_blk_req_complete(req, err_status);
    virtio_blk_free_request(req);
    g_free(data->zone_report_data.zones);
    g_free(data);
}

static void virtio_blk_handle_zone_report(VirtIOBlockReq *req,
                                         struct iovec *in_iov,
                                         unsigned in_num)
{
    VirtIOBlock *s = req->dev;
    VirtIODevice *vdev = VIRTIO_DEVICE(s);
    unsigned int nr_zones;
    ZoneCmdData *data;
    int64_t zone_size, offset;
    uint8_t err_status;

    if (req->in_len < sizeof(struct virtio_blk_inhdr) +
            sizeof(struct virtio_blk_zone_report) +
            sizeof(struct virtio_blk_zone_descriptor)) {
        virtio_error(vdev, "in buffer too small for zone report");
        return;
    }

    /* start byte offset of the zone report */
    offset = virtio_ldq_p(vdev, &req->out.sector) << BDRV_SECTOR_BITS;
    if (!check_zoned_request(s, offset, 0, false, &err_status)) {
        goto out;
    }
    nr_zones = (req->in_len - sizeof(struct virtio_blk_inhdr) -
                sizeof(struct virtio_blk_zone_report)) /
               sizeof(struct virtio_blk_zone_descriptor);
    trace_virtio_blk_handle_zone_report(vdev, req,
                                        offset >> BDRV_SECTOR_BITS, nr_zones);

    zone_size = sizeof(BlockZoneDescriptor) * nr_zones;
    data = g_malloc(sizeof(ZoneCmdData));
    data->req = req;
    data->in_iov = in_iov;
    data->in_num = in_num;
    data->zone_report_data.nr_zones = nr_zones;
    data->zone_report_data.zones = g_malloc(zone_size),

    blk_aio_zone_report(s->blk, offset, &data->zone_report_data.nr_zones,
                        data->zone_report_data.zones,
                        virtio_blk_zone_report_complete, data);
    return;
out:
    virtio_blk_req_complete(req, err_status);
    virtio_blk_free_request(req);
}

static void virtio_blk_zone_mgmt_complete(void *opaque, int ret)
{
    VirtIOBlockReq *req = opaque;
    VirtIOBlock *s = req->dev;
    VirtIODevice *vdev = VIRTIO_DEVICE(s);
    int8_t err_status = VIRTIO_BLK_S_OK;
    trace_virtio_blk_zone_mgmt_complete(vdev, req,ret);

    if (ret) {
        err_status = VIRTIO_BLK_S_ZONE_INVALID_CMD;
    }

    virtio_blk_req_complete(req, err_status);
    virtio_blk_free_request(req);
}

static int virtio_blk_handle_zone_mgmt(VirtIOBlockReq *req, BlockZoneOp op)
{
    VirtIOBlock *s = req->dev;
    VirtIODevice *vdev = VIRTIO_DEVICE(s);
    BlockDriverState *bs = blk_bs(s->blk);
    int64_t offset = virtio_ldq_p(vdev, &req->out.sector) << BDRV_SECTOR_BITS;
    uint64_t len;
    uint64_t capacity = bs->total_sectors << BDRV_SECTOR_BITS;
    uint8_t err_status = VIRTIO_BLK_S_OK;

    uint32_t type = virtio_ldl_p(vdev, &req->out.type);
    if (type == VIRTIO_BLK_T_ZONE_RESET_ALL) {
        /* Entire drive capacity */
        offset = 0;
        len = capacity;
        trace_virtio_blk_handle_zone_reset_all(vdev, req, 0,
                                               bs->total_sectors);
    } else {
        if (bs->bl.zone_size > capacity - offset) {
            /* The zoned device allows the last smaller zone. */
            len = capacity - bs->bl.zone_size * (bs->bl.nr_zones - 1);
        } else {
            len = bs->bl.zone_size;
        }
        trace_virtio_blk_handle_zone_mgmt(vdev, req, op,
                                          offset >> BDRV_SECTOR_BITS,
                                          len >> BDRV_SECTOR_BITS);
    }

    if (!check_zoned_request(s, offset, len, false, &err_status)) {
        goto out;
    }

    blk_aio_zone_mgmt(s->blk, op, offset, len,
                      virtio_blk_zone_mgmt_complete, req);

    return 0;
out:
    virtio_blk_req_complete(req, err_status);
    virtio_blk_free_request(req);
    return err_status;
}

static void virtio_blk_zone_append_complete(void *opaque, int ret)
{
    ZoneCmdData *data = opaque;
    VirtIOBlockReq *req = data->req;
    VirtIODevice *vdev = VIRTIO_DEVICE(req->dev);
    int64_t append_sector, n;
    uint8_t err_status = VIRTIO_BLK_S_OK;

    if (ret) {
        err_status = VIRTIO_BLK_S_ZONE_INVALID_CMD;
        goto out;
    }

    virtio_stq_p(vdev, &append_sector,
                 data->zone_append_data.offset >> BDRV_SECTOR_BITS);
    n = iov_from_buf(data->in_iov, data->in_num, 0, &append_sector,
                     sizeof(append_sector));
    if (n != sizeof(append_sector)) {
        virtio_error(vdev, "Driver provided input buffer less than size of "
                           "append_sector");
        err_status = VIRTIO_BLK_S_ZONE_INVALID_CMD;
        goto out;
    }
    trace_virtio_blk_zone_append_complete(vdev, req, append_sector, ret);

out:
    virtio_blk_req_complete(req, err_status);
    virtio_blk_free_request(req);
    g_free(data);
}

static int virtio_blk_handle_zone_append(VirtIOBlockReq *req,
                                         struct iovec *out_iov,
                                         struct iovec *in_iov,
                                         uint64_t out_num,
                                         unsigned in_num) {
    VirtIOBlock *s = req->dev;
    VirtIODevice *vdev = VIRTIO_DEVICE(s);
    uint8_t err_status = VIRTIO_BLK_S_OK;

    int64_t offset = virtio_ldq_p(vdev, &req->out.sector) << BDRV_SECTOR_BITS;
    int64_t len = iov_size(out_iov, out_num);
    ZoneCmdData *data;

    trace_virtio_blk_handle_zone_append(vdev, req, offset >> BDRV_SECTOR_BITS);
    if (!check_zoned_request(s, offset, len, true, &err_status)) {
        goto out;
    }

    data = g_malloc(sizeof(ZoneCmdData));
    data->req = req;
    data->in_iov = in_iov;
    data->in_num = in_num;
    data->zone_append_data.offset = offset;
    qemu_iovec_init_external(&req->qiov, out_iov, out_num);

    block_acct_start(blk_get_stats(s->blk), &req->acct, len,
                     BLOCK_ACCT_ZONE_APPEND);

    blk_aio_zone_append(s->blk, &data->zone_append_data.offset, &req->qiov, 0,
                        virtio_blk_zone_append_complete, data);
    return 0;

out:
    virtio_blk_req_complete(req, err_status);
    virtio_blk_free_request(req);
    return err_status;
}

static int virtio_blk_handle_request(VirtIOBlockReq *req, MultiReqBuffer *mrb)
{
    uint32_t type;
    struct iovec *in_iov = req->elem.in_sg;
    struct iovec *out_iov = req->elem.out_sg;
    unsigned in_num = req->elem.in_num;
    unsigned out_num = req->elem.out_num;
    VirtIOBlock *s = req->dev;
    VirtIODevice *vdev = VIRTIO_DEVICE(s);

    if (req->elem.out_num < 1 || req->elem.in_num < 1) {
        virtio_error(vdev, "virtio-blk missing headers");
        return -1;
    }

    if (unlikely(iov_to_buf(out_iov, out_num, 0, &req->out,
                            sizeof(req->out)) != sizeof(req->out))) {
        virtio_error(vdev, "virtio-blk request outhdr too short");
        return -1;
    }

    iov_discard_front_undoable(&out_iov, &out_num, sizeof(req->out),
                               &req->outhdr_undo);

    if (in_iov[in_num - 1].iov_len < sizeof(struct virtio_blk_inhdr)) {
        virtio_error(vdev, "virtio-blk request inhdr too short");
        iov_discard_undo(&req->outhdr_undo);
        return -1;
    }

    /* We always touch the last byte, so just see how big in_iov is.  */
    req->in_len = iov_size(in_iov, in_num);
    req->in = (void *)in_iov[in_num - 1].iov_base
              + in_iov[in_num - 1].iov_len
              - sizeof(struct virtio_blk_inhdr);
    iov_discard_back_undoable(in_iov, &in_num, sizeof(struct virtio_blk_inhdr),
                              &req->inhdr_undo);

    type = virtio_ldl_p(vdev, &req->out.type);

    /* VIRTIO_BLK_T_OUT defines the command direction. VIRTIO_BLK_T_BARRIER
     * is an optional flag. Although a guest should not send this flag if
     * not negotiated we ignored it in the past. So keep ignoring it. */
    switch (type & ~(VIRTIO_BLK_T_OUT | VIRTIO_BLK_T_BARRIER)) {
    case VIRTIO_BLK_T_IN:
    {
        bool is_write = type & VIRTIO_BLK_T_OUT;
        req->sector_num = virtio_ldq_p(vdev, &req->out.sector);

        if (is_write) {
            qemu_iovec_init_external(&req->qiov, out_iov, out_num);
            trace_virtio_blk_handle_write(vdev, req, req->sector_num,
                                          req->qiov.size / BDRV_SECTOR_SIZE);
        } else {
            qemu_iovec_init_external(&req->qiov, in_iov, in_num);
            trace_virtio_blk_handle_read(vdev, req, req->sector_num,
                                         req->qiov.size / BDRV_SECTOR_SIZE);
        }

        if (!virtio_blk_sect_range_ok(s, req->sector_num, req->qiov.size)) {
            virtio_blk_req_complete(req, VIRTIO_BLK_S_IOERR);
            block_acct_invalid(blk_get_stats(s->blk),
                               is_write ? BLOCK_ACCT_WRITE : BLOCK_ACCT_READ);
            virtio_blk_free_request(req);
            return 0;
        }

        block_acct_start(blk_get_stats(s->blk), &req->acct, req->qiov.size,
                         is_write ? BLOCK_ACCT_WRITE : BLOCK_ACCT_READ);

        /* merge would exceed maximum number of requests or IO direction
         * changes */
        if (mrb->num_reqs > 0 && (mrb->num_reqs == VIRTIO_BLK_MAX_MERGE_REQS ||
                                  is_write != mrb->is_write ||
                                  !s->conf.request_merging)) {
            virtio_blk_submit_multireq(s, mrb);
        }

        assert(mrb->num_reqs < VIRTIO_BLK_MAX_MERGE_REQS);
        mrb->reqs[mrb->num_reqs++] = req;
        mrb->is_write = is_write;
        break;
    }
    case VIRTIO_BLK_T_FLUSH:
        virtio_blk_handle_flush(req, mrb);
        break;
    case VIRTIO_BLK_T_ZONE_REPORT:
        virtio_blk_handle_zone_report(req, in_iov, in_num);
        break;
    case VIRTIO_BLK_T_ZONE_OPEN:
        virtio_blk_handle_zone_mgmt(req, BLK_ZO_OPEN);
        break;
    case VIRTIO_BLK_T_ZONE_CLOSE:
        virtio_blk_handle_zone_mgmt(req, BLK_ZO_CLOSE);
        break;
    case VIRTIO_BLK_T_ZONE_FINISH:
        virtio_blk_handle_zone_mgmt(req, BLK_ZO_FINISH);
        break;
    case VIRTIO_BLK_T_ZONE_RESET:
        virtio_blk_handle_zone_mgmt(req, BLK_ZO_RESET);
        break;
    case VIRTIO_BLK_T_ZONE_RESET_ALL:
        virtio_blk_handle_zone_mgmt(req, BLK_ZO_RESET);
        break;
    case VIRTIO_BLK_T_SCSI_CMD:
        virtio_blk_handle_scsi(req);
        break;
    case VIRTIO_BLK_T_GET_ID:
    {
        /*
         * NB: per existing s/n string convention the string is
         * terminated by '\0' only when shorter than buffer.
         */
        const char *serial = s->conf.serial ? s->conf.serial : "";
        size_t size = MIN(strlen(serial) + 1,
                          MIN(iov_size(in_iov, in_num),
                              VIRTIO_BLK_ID_BYTES));
        iov_from_buf(in_iov, in_num, 0, serial, size);
        virtio_blk_req_complete(req, VIRTIO_BLK_S_OK);
        virtio_blk_free_request(req);
        break;
    }
    case VIRTIO_BLK_T_ZONE_APPEND & ~VIRTIO_BLK_T_OUT:
        /*
         * Passing out_iov/out_num and in_iov/in_num is not safe
         * to access req->elem.out_sg directly because it may be
         * modified by virtio_blk_handle_request().
         */
        virtio_blk_handle_zone_append(req, out_iov, in_iov, out_num, in_num);
        break;
    /*
     * VIRTIO_BLK_T_DISCARD and VIRTIO_BLK_T_WRITE_ZEROES are defined with
     * VIRTIO_BLK_T_OUT flag set. We masked this flag in the switch statement,
     * so we must mask it for these requests, then we will check if it is set.
     */
    case VIRTIO_BLK_T_DISCARD & ~VIRTIO_BLK_T_OUT:
    case VIRTIO_BLK_T_WRITE_ZEROES & ~VIRTIO_BLK_T_OUT:
    {
        struct virtio_blk_discard_write_zeroes dwz_hdr;
        size_t out_len = iov_size(out_iov, out_num);
        bool is_write_zeroes = (type & ~VIRTIO_BLK_T_BARRIER) ==
                               VIRTIO_BLK_T_WRITE_ZEROES;
        uint8_t err_status;

        /*
         * Unsupported if VIRTIO_BLK_T_OUT is not set or the request contains
         * more than one segment.
         */
        if (unlikely(!(type & VIRTIO_BLK_T_OUT) ||
                     out_len > sizeof(dwz_hdr))) {
            virtio_blk_req_complete(req, VIRTIO_BLK_S_UNSUPP);
            virtio_blk_free_request(req);
            return 0;
        }

        if (unlikely(iov_to_buf(out_iov, out_num, 0, &dwz_hdr,
                                sizeof(dwz_hdr)) != sizeof(dwz_hdr))) {
            iov_discard_undo(&req->inhdr_undo);
            iov_discard_undo(&req->outhdr_undo);
            virtio_error(vdev, "virtio-blk discard/write_zeroes header"
                         " too short");
            return -1;
        }

        err_status = virtio_blk_handle_discard_write_zeroes(req, &dwz_hdr,
                                                            is_write_zeroes);
        if (err_status != VIRTIO_BLK_S_OK) {
            virtio_blk_req_complete(req, err_status);
            virtio_blk_free_request(req);
        }

        break;
    }
    default:
        virtio_blk_req_complete(req, VIRTIO_BLK_S_UNSUPP);
        virtio_blk_free_request(req);
    }
    return 0;
}

void virtio_blk_handle_vq(VirtIOBlock *s, VirtQueue *vq)
{
    VirtIOBlockReq *req;
    MultiReqBuffer mrb = {};
    bool suppress_notifications = virtio_queue_get_notification(vq);

    defer_call_begin();

    do {
        if (suppress_notifications) {
            virtio_queue_set_notification(vq, 0);
        }

        while ((req = virtio_blk_get_request(s, vq))) {
            if (virtio_blk_handle_request(req, &mrb)) {
                virtqueue_detach_element(req->vq, &req->elem, 0);
                virtio_blk_free_request(req);
                break;
            }
        }

        if (suppress_notifications) {
            virtio_queue_set_notification(vq, 1);
        }
    } while (!virtio_queue_empty(vq));

    if (mrb.num_reqs) {
        virtio_blk_submit_multireq(s, &mrb);
    }

    defer_call_end();
}

static void virtio_blk_handle_output(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIOBlock *s = (VirtIOBlock *)vdev;

    if (!s->ioeventfd_disabled && !s->ioeventfd_started) {
        /* Some guests kick before setting VIRTIO_CONFIG_S_DRIVER_OK so start
         * ioeventfd here instead of waiting for .set_status().
         */
        virtio_device_start_ioeventfd(vdev);
        if (!s->ioeventfd_disabled) {
            return;
        }
    }

    virtio_blk_handle_vq(s, vq);
}

static void virtio_blk_dma_restart_bh(void *opaque)
{
    VirtIOBlockReq *req = opaque;
    VirtIOBlock *s = req->dev; /* we're called with at least one request */

    MultiReqBuffer mrb = {};

    while (req) {
        VirtIOBlockReq *next = req->next;
        if (virtio_blk_handle_request(req, &mrb)) {
            /* Device is now broken and won't do any processing until it gets
             * reset. Already queued requests will be lost: let's purge them.
             */
            while (req) {
                next = req->next;
                virtqueue_detach_element(req->vq, &req->elem, 0);
                virtio_blk_free_request(req);
                req = next;
            }
            break;
        }
        req = next;
    }

    if (mrb.num_reqs) {
        virtio_blk_submit_multireq(s, &mrb);
    }

    /* Paired with inc in virtio_blk_dma_restart_cb() */
    blk_dec_in_flight(s->conf.conf.blk);
}

static void virtio_blk_dma_restart_cb(void *opaque, bool running,
                                      RunState state)
{
    VirtIOBlock *s = opaque;
    uint16_t num_queues = s->conf.num_queues;
    g_autofree VirtIOBlockReq **vq_rq = NULL;
    VirtIOBlockReq *rq;

    if (!running) {
        return;
    }

    /* Split the device-wide s->rq request list into per-vq request lists */
    vq_rq = g_new0(VirtIOBlockReq *, num_queues);

    WITH_QEMU_LOCK_GUARD(&s->rq_lock) {
        rq = s->rq;
        s->rq = NULL;
    }

    while (rq) {
        VirtIOBlockReq *next = rq->next;
        uint16_t idx = virtio_get_queue_index(rq->vq);

        /* Only num_queues vqs were created so vq_rq[idx] is within bounds */
        assert(idx < num_queues);
        rq->next = vq_rq[idx];
        vq_rq[idx] = rq;
        rq = next;
    }

    /* Schedule a BH to submit the requests in each vq's AioContext */
    for (uint16_t i = 0; i < num_queues; i++) {
        if (!vq_rq[i]) {
            continue;
        }

        /* Paired with dec in virtio_blk_dma_restart_bh() */
        blk_inc_in_flight(s->conf.conf.blk);

        aio_bh_schedule_oneshot(s->vq_aio_context[i],
                                virtio_blk_dma_restart_bh,
                                vq_rq[i]);
    }
}

static void virtio_blk_reset(VirtIODevice *vdev)
{
    VirtIOBlock *s = VIRTIO_BLK(vdev);
    VirtIOBlockReq *req;

    /* Dataplane has stopped... */
    assert(!s->ioeventfd_started);

    /* ...but requests may still be in flight. */
    blk_drain(s->blk);

    /* We drop queued requests after blk_drain() because blk_drain() itself can
     * produce them. */
    WITH_QEMU_LOCK_GUARD(&s->rq_lock) {
        while (s->rq) {
            req = s->rq;
            s->rq = req->next;

            /* No other threads can access req->vq here */
            virtqueue_detach_element(req->vq, &req->elem, 0);

            virtio_blk_free_request(req);
        }
    }

    blk_set_enable_write_cache(s->blk, s->original_wce);
}

/* coalesce internal state, copy to pci i/o region 0
 */
static void virtio_blk_update_config(VirtIODevice *vdev, uint8_t *config)
{
    VirtIOBlock *s = VIRTIO_BLK(vdev);
    BlockConf *conf = &s->conf.conf;
    BlockDriverState *bs = blk_bs(s->blk);
    struct virtio_blk_config blkcfg;
    uint64_t capacity;
    int64_t length;
    int blk_size = conf->logical_block_size;

    blk_get_geometry(s->blk, &capacity);
    memset(&blkcfg, 0, sizeof(blkcfg));
    virtio_stq_p(vdev, &blkcfg.capacity, capacity);
    virtio_stl_p(vdev, &blkcfg.seg_max,
                 s->conf.seg_max_adjust ? s->conf.queue_size - 2 : 128 - 2);
    virtio_stw_p(vdev, &blkcfg.geometry.cylinders, conf->cyls);
    virtio_stl_p(vdev, &blkcfg.blk_size, blk_size);
    virtio_stw_p(vdev, &blkcfg.min_io_size, conf->min_io_size / blk_size);
    virtio_stl_p(vdev, &blkcfg.opt_io_size, conf->opt_io_size / blk_size);
    blkcfg.geometry.heads = conf->heads;
    /*
     * We must ensure that the block device capacity is a multiple of
     * the logical block size. If that is not the case, let's use
     * sector_mask to adopt the geometry to have a correct picture.
     * For those devices where the capacity is ok for the given geometry
     * we don't touch the sector value of the geometry, since some devices
     * (like s390 dasd) need a specific value. Here the capacity is already
     * cyls*heads*secs*blk_size and the sector value is not block size
     * divided by 512 - instead it is the amount of blk_size blocks
     * per track (cylinder).
     */
    length = blk_getlength(s->blk);
    if (length > 0 && length / conf->heads / conf->secs % blk_size) {
        blkcfg.geometry.sectors = conf->secs & ~s->sector_mask;
    } else {
        blkcfg.geometry.sectors = conf->secs;
    }
    blkcfg.size_max = 0;
    blkcfg.physical_block_exp = get_physical_block_exp(conf);
    blkcfg.alignment_offset = 0;
    blkcfg.wce = blk_enable_write_cache(s->blk);
    virtio_stw_p(vdev, &blkcfg.num_queues, s->conf.num_queues);
    if (virtio_has_feature(s->host_features, VIRTIO_BLK_F_DISCARD)) {
        uint32_t discard_granularity = conf->discard_granularity;
        if (discard_granularity == -1 || !s->conf.report_discard_granularity) {
            discard_granularity = blk_size;
        }
        virtio_stl_p(vdev, &blkcfg.max_discard_sectors,
                     s->conf.max_discard_sectors);
        virtio_stl_p(vdev, &blkcfg.discard_sector_alignment,
                     discard_granularity >> BDRV_SECTOR_BITS);
        /*
         * We support only one segment per request since multiple segments
         * are not widely used and there are no userspace APIs that allow
         * applications to submit multiple segments in a single call.
         */
        virtio_stl_p(vdev, &blkcfg.max_discard_seg, 1);
    }
    if (virtio_has_feature(s->host_features, VIRTIO_BLK_F_WRITE_ZEROES)) {
        virtio_stl_p(vdev, &blkcfg.max_write_zeroes_sectors,
                     s->conf.max_write_zeroes_sectors);
        blkcfg.write_zeroes_may_unmap = 1;
        virtio_stl_p(vdev, &blkcfg.max_write_zeroes_seg, 1);
    }
    if (bs->bl.zoned != BLK_Z_NONE) {
        switch (bs->bl.zoned) {
        case BLK_Z_HM:
            blkcfg.zoned.model = VIRTIO_BLK_Z_HM;
            break;
        case BLK_Z_HA:
            blkcfg.zoned.model = VIRTIO_BLK_Z_HA;
            break;
        default:
            g_assert_not_reached();
        }

        virtio_stl_p(vdev, &blkcfg.zoned.zone_sectors,
                     bs->bl.zone_size / 512);
        virtio_stl_p(vdev, &blkcfg.zoned.max_active_zones,
                     bs->bl.max_active_zones);
        virtio_stl_p(vdev, &blkcfg.zoned.max_open_zones,
                     bs->bl.max_open_zones);
        virtio_stl_p(vdev, &blkcfg.zoned.write_granularity, blk_size);
        virtio_stl_p(vdev, &blkcfg.zoned.max_append_sectors,
                     bs->bl.max_append_sectors);
    } else {
        blkcfg.zoned.model = VIRTIO_BLK_Z_NONE;
    }
    memcpy(config, &blkcfg, s->config_size);
}

static void virtio_blk_set_config(VirtIODevice *vdev, const uint8_t *config)
{
    VirtIOBlock *s = VIRTIO_BLK(vdev);
    struct virtio_blk_config blkcfg;

    memcpy(&blkcfg, config, s->config_size);

    blk_set_enable_write_cache(s->blk, blkcfg.wce != 0);
}

static uint64_t virtio_blk_get_features(VirtIODevice *vdev, uint64_t features,
                                        Error **errp)
{
    VirtIOBlock *s = VIRTIO_BLK(vdev);

    /* Firstly sync all virtio-blk possible supported features */
    features |= s->host_features;

    virtio_add_feature(&features, VIRTIO_BLK_F_SEG_MAX);
    virtio_add_feature(&features, VIRTIO_BLK_F_GEOMETRY);
    virtio_add_feature(&features, VIRTIO_BLK_F_TOPOLOGY);
    virtio_add_feature(&features, VIRTIO_BLK_F_BLK_SIZE);
    if (virtio_has_feature(features, VIRTIO_F_VERSION_1)) {
        if (virtio_has_feature(s->host_features, VIRTIO_BLK_F_SCSI)) {
            error_setg(errp, "Please set scsi=off for virtio-blk devices in order to use virtio 1.0");
            return 0;
        }
    } else {
        virtio_clear_feature(&features, VIRTIO_F_ANY_LAYOUT);
        virtio_add_feature(&features, VIRTIO_BLK_F_SCSI);
    }

    if (blk_enable_write_cache(s->blk) ||
        (s->conf.x_enable_wce_if_config_wce &&
         virtio_has_feature(features, VIRTIO_BLK_F_CONFIG_WCE))) {
        virtio_add_feature(&features, VIRTIO_BLK_F_WCE);
    }
    if (!blk_is_writable(s->blk)) {
        virtio_add_feature(&features, VIRTIO_BLK_F_RO);
    }
    if (s->conf.num_queues > 1) {
        virtio_add_feature(&features, VIRTIO_BLK_F_MQ);
    }

    return features;
}

static void virtio_blk_set_status(VirtIODevice *vdev, uint8_t status)
{
    VirtIOBlock *s = VIRTIO_BLK(vdev);

    if (!(status & (VIRTIO_CONFIG_S_DRIVER | VIRTIO_CONFIG_S_DRIVER_OK))) {
        assert(!s->ioeventfd_started);
    }

    if (!(status & VIRTIO_CONFIG_S_DRIVER_OK)) {
        return;
    }

    /* A guest that supports VIRTIO_BLK_F_CONFIG_WCE must be able to send
     * cache flushes.  Thus, the "auto writethrough" behavior is never
     * necessary for guests that support the VIRTIO_BLK_F_CONFIG_WCE feature.
     * Leaving it enabled would break the following sequence:
     *
     *     Guest started with "-drive cache=writethrough"
     *     Guest sets status to 0
     *     Guest sets DRIVER bit in status field
     *     Guest reads host features (WCE=0, CONFIG_WCE=1)
     *     Guest writes guest features (WCE=0, CONFIG_WCE=1)
     *     Guest writes 1 to the WCE configuration field (writeback mode)
     *     Guest sets DRIVER_OK bit in status field
     *
     * s->blk would erroneously be placed in writethrough mode.
     */
    if (!virtio_vdev_has_feature(vdev, VIRTIO_BLK_F_CONFIG_WCE)) {
        blk_set_enable_write_cache(s->blk,
                                   virtio_vdev_has_feature(vdev,
                                                           VIRTIO_BLK_F_WCE));
    }
}

static void virtio_blk_save_device(VirtIODevice *vdev, QEMUFile *f)
{
    VirtIOBlock *s = VIRTIO_BLK(vdev);

    WITH_QEMU_LOCK_GUARD(&s->rq_lock) {
        VirtIOBlockReq *req = s->rq;

        while (req) {
            qemu_put_sbyte(f, 1);

            if (s->conf.num_queues > 1) {
                qemu_put_be32(f, virtio_get_queue_index(req->vq));
            }

            qemu_put_virtqueue_element(vdev, f, &req->elem);
            req = req->next;
        }
    }

    qemu_put_sbyte(f, 0);
}

static int virtio_blk_load_device(VirtIODevice *vdev, QEMUFile *f,
                                  int version_id)
{
    VirtIOBlock *s = VIRTIO_BLK(vdev);

    while (qemu_get_sbyte(f)) {
        unsigned nvqs = s->conf.num_queues;
        unsigned vq_idx = 0;
        VirtIOBlockReq *req;

        if (nvqs > 1) {
            vq_idx = qemu_get_be32(f);

            if (vq_idx >= nvqs) {
                error_report("Invalid virtqueue index in request list: %#x",
                             vq_idx);
                return -EINVAL;
            }
        }

        req = qemu_get_virtqueue_element(vdev, f, sizeof(VirtIOBlockReq));
        virtio_blk_init_request(s, virtio_get_queue(vdev, vq_idx), req);

        WITH_QEMU_LOCK_GUARD(&s->rq_lock) {
            req->next = s->rq;
            s->rq = req;
        }
    }

    return 0;
}

static void virtio_resize_cb(void *opaque)
{
    VirtIODevice *vdev = opaque;

    assert(qemu_get_current_aio_context() == qemu_get_aio_context());
    virtio_notify_config(vdev);
}

static void virtio_blk_resize(void *opaque)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(opaque);

    /*
     * virtio_notify_config() needs to acquire the BQL,
     * so it can't be called from an iothread. Instead, schedule
     * it to be run in the main context BH.
     */
    aio_bh_schedule_oneshot(qemu_get_aio_context(), virtio_resize_cb, vdev);
}

static void virtio_blk_ioeventfd_detach(VirtIOBlock *s)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(s);

    for (uint16_t i = 0; i < s->conf.num_queues; i++) {
        VirtQueue *vq = virtio_get_queue(vdev, i);
        virtio_queue_aio_detach_host_notifier(vq, s->vq_aio_context[i]);
    }
}

static void virtio_blk_ioeventfd_attach(VirtIOBlock *s)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(s);

    for (uint16_t i = 0; i < s->conf.num_queues; i++) {
        VirtQueue *vq = virtio_get_queue(vdev, i);
        virtio_queue_aio_attach_host_notifier(vq, s->vq_aio_context[i]);
    }
}

/* Suspend virtqueue ioeventfd processing during drain */
static void virtio_blk_drained_begin(void *opaque)
{
    VirtIOBlock *s = opaque;

    if (s->ioeventfd_started) {
        virtio_blk_ioeventfd_detach(s);
    }
}

/* Resume virtqueue ioeventfd processing after drain */
static void virtio_blk_drained_end(void *opaque)
{
    VirtIOBlock *s = opaque;

    if (s->ioeventfd_started) {
        virtio_blk_ioeventfd_attach(s);
    }
}

static const BlockDevOps virtio_block_ops = {
    .resize_cb     = virtio_blk_resize,
    .drained_begin = virtio_blk_drained_begin,
    .drained_end   = virtio_blk_drained_end,
};

static bool
validate_iothread_vq_mapping_list(IOThreadVirtQueueMappingList *list,
        uint16_t num_queues, Error **errp)
{
    g_autofree unsigned long *vqs = bitmap_new(num_queues);
    g_autoptr(GHashTable) iothreads =
        g_hash_table_new(g_str_hash, g_str_equal);

    for (IOThreadVirtQueueMappingList *node = list; node; node = node->next) {
        const char *name = node->value->iothread;
        uint16List *vq;

        if (!iothread_by_id(name)) {
            error_setg(errp, "IOThread \"%s\" object does not exist", name);
            return false;
        }

        if (!g_hash_table_add(iothreads, (gpointer)name)) {
            error_setg(errp,
                    "duplicate IOThread name \"%s\" in iothread-vq-mapping",
                    name);
            return false;
        }

        if (node != list) {
            if (!!node->value->vqs != !!list->value->vqs) {
                error_setg(errp, "either all items in iothread-vq-mapping "
                                 "must have vqs or none of them must have it");
                return false;
            }
        }

        for (vq = node->value->vqs; vq; vq = vq->next) {
            if (vq->value >= num_queues) {
                error_setg(errp, "vq index %u for IOThread \"%s\" must be "
                        "less than num_queues %u in iothread-vq-mapping",
                        vq->value, name, num_queues);
                return false;
            }

            if (test_and_set_bit(vq->value, vqs)) {
                error_setg(errp, "cannot assign vq %u to IOThread \"%s\" "
                        "because it is already assigned", vq->value, name);
                return false;
            }
        }
    }

    if (list->value->vqs) {
        for (uint16_t i = 0; i < num_queues; i++) {
            if (!test_bit(i, vqs)) {
                error_setg(errp,
                        "missing vq %u IOThread assignment in iothread-vq-mapping",
                        i);
                return false;
            }
        }
    }

    return true;
}

/**
 * apply_iothread_vq_mapping:
 * @iothread_vq_mapping_list: The mapping of virtqueues to IOThreads.
 * @vq_aio_context: The array of AioContext pointers to fill in.
 * @num_queues: The length of @vq_aio_context.
 * @errp: If an error occurs, a pointer to the area to store the error.
 *
 * Fill in the AioContext for each virtqueue in the @vq_aio_context array given
 * the iothread-vq-mapping parameter in @iothread_vq_mapping_list.
 *
 * Returns: %true on success, %false on failure.
 **/
static bool apply_iothread_vq_mapping(
        IOThreadVirtQueueMappingList *iothread_vq_mapping_list,
        AioContext **vq_aio_context,
        uint16_t num_queues,
        Error **errp)
{
    IOThreadVirtQueueMappingList *node;
    size_t num_iothreads = 0;
    size_t cur_iothread = 0;

    if (!validate_iothread_vq_mapping_list(iothread_vq_mapping_list,
                                           num_queues, errp)) {
        return false;
    }

    for (node = iothread_vq_mapping_list; node; node = node->next) {
        num_iothreads++;
    }

    for (node = iothread_vq_mapping_list; node; node = node->next) {
        IOThread *iothread = iothread_by_id(node->value->iothread);
        AioContext *ctx = iothread_get_aio_context(iothread);

        /* Released in virtio_blk_vq_aio_context_cleanup() */
        object_ref(OBJECT(iothread));

        if (node->value->vqs) {
            uint16List *vq;

            /* Explicit vq:IOThread assignment */
            for (vq = node->value->vqs; vq; vq = vq->next) {
                assert(vq->value < num_queues);
                vq_aio_context[vq->value] = ctx;
            }
        } else {
            /* Round-robin vq:IOThread assignment */
            for (unsigned i = cur_iothread; i < num_queues;
                 i += num_iothreads) {
                vq_aio_context[i] = ctx;
            }
        }

        cur_iothread++;
    }

    return true;
}

/* Context: BQL held */
static bool virtio_blk_vq_aio_context_init(VirtIOBlock *s, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(s);
    VirtIOBlkConf *conf = &s->conf;
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(vdev)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);

    if (conf->iothread && conf->iothread_vq_mapping_list) {
        error_setg(errp,
                   "iothread and iothread-vq-mapping properties cannot be set "
                   "at the same time");
        return false;
    }

    if (conf->iothread || conf->iothread_vq_mapping_list) {
        if (!k->set_guest_notifiers || !k->ioeventfd_assign) {
            error_setg(errp,
                       "device is incompatible with iothread "
                       "(transport does not support notifiers)");
            return false;
        }
        if (!virtio_device_ioeventfd_enabled(vdev)) {
            error_setg(errp, "ioeventfd is required for iothread");
            return false;
        }

        /*
         * If ioeventfd is (re-)enabled while the guest is running there could
         * be block jobs that can conflict.
         */
        if (blk_op_is_blocked(conf->conf.blk, BLOCK_OP_TYPE_DATAPLANE, errp)) {
            error_prepend(errp, "cannot start virtio-blk ioeventfd: ");
            return false;
        }
    }

    s->vq_aio_context = g_new(AioContext *, conf->num_queues);

    if (conf->iothread_vq_mapping_list) {
        if (!apply_iothread_vq_mapping(conf->iothread_vq_mapping_list,
                                       s->vq_aio_context,
                                       conf->num_queues,
                                       errp)) {
            g_free(s->vq_aio_context);
            s->vq_aio_context = NULL;
            return false;
        }
    } else if (conf->iothread) {
        AioContext *ctx = iothread_get_aio_context(conf->iothread);
        for (unsigned i = 0; i < conf->num_queues; i++) {
            s->vq_aio_context[i] = ctx;
        }

        /* Released in virtio_blk_vq_aio_context_cleanup() */
        object_ref(OBJECT(conf->iothread));
    } else {
        AioContext *ctx = qemu_get_aio_context();
        for (unsigned i = 0; i < conf->num_queues; i++) {
            s->vq_aio_context[i] = ctx;
        }
    }

    return true;
}

/* Context: BQL held */
static void virtio_blk_vq_aio_context_cleanup(VirtIOBlock *s)
{
    VirtIOBlkConf *conf = &s->conf;

    assert(!s->ioeventfd_started);

    if (conf->iothread_vq_mapping_list) {
        IOThreadVirtQueueMappingList *node;

        for (node = conf->iothread_vq_mapping_list; node; node = node->next) {
            IOThread *iothread = iothread_by_id(node->value->iothread);
            object_unref(OBJECT(iothread));
        }
    }

    if (conf->iothread) {
        object_unref(OBJECT(conf->iothread));
    }

    g_free(s->vq_aio_context);
    s->vq_aio_context = NULL;
}

/* Context: BQL held */
static int virtio_blk_start_ioeventfd(VirtIODevice *vdev)
{
    VirtIOBlock *s = VIRTIO_BLK(vdev);
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(s)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    unsigned i;
    unsigned nvqs = s->conf.num_queues;
    Error *local_err = NULL;
    int r;

    if (s->ioeventfd_started || s->ioeventfd_starting) {
        return 0;
    }

    s->ioeventfd_starting = true;

    /* Set up guest notifier (irq) */
    r = k->set_guest_notifiers(qbus->parent, nvqs, true);
    if (r != 0) {
        error_report("virtio-blk failed to set guest notifier (%d), "
                     "ensure -accel kvm is set.", r);
        goto fail_guest_notifiers;
    }

    /*
     * Batch all the host notifiers in a single transaction to avoid
     * quadratic time complexity in address_space_update_ioeventfds().
     */
    memory_region_transaction_begin();

    /* Set up virtqueue notify */
    for (i = 0; i < nvqs; i++) {
        r = virtio_bus_set_host_notifier(VIRTIO_BUS(qbus), i, true);
        if (r != 0) {
            int j = i;

            fprintf(stderr, "virtio-blk failed to set host notifier (%d)\n", r);
            while (i--) {
                virtio_bus_set_host_notifier(VIRTIO_BUS(qbus), i, false);
            }

            /*
             * The transaction expects the ioeventfds to be open when it
             * commits. Do it now, before the cleanup loop.
             */
            memory_region_transaction_commit();

            while (j--) {
                virtio_bus_cleanup_host_notifier(VIRTIO_BUS(qbus), j);
            }
            goto fail_host_notifiers;
        }
    }

    memory_region_transaction_commit();

    /*
     * Try to change the AioContext so that block jobs and other operations can
     * co-locate their activity in the same AioContext. If it fails, nevermind.
     */
    assert(nvqs > 0); /* enforced during ->realize() */
    r = blk_set_aio_context(s->conf.conf.blk, s->vq_aio_context[0],
                            &local_err);
    if (r < 0) {
        warn_report_err(local_err);
    }

    /*
     * These fields must be visible to the IOThread when it processes the
     * virtqueue, otherwise it will think ioeventfd has not started yet.
     *
     * Make sure ->ioeventfd_started is false when blk_set_aio_context() is
     * called above so that draining does not cause the host notifier to be
     * detached/attached prematurely.
     */
    s->ioeventfd_starting = false;
    s->ioeventfd_started = true;
    smp_wmb(); /* paired with aio_notify_accept() on the read side */

    /*
     * Get this show started by hooking up our callbacks.  If drained now,
     * virtio_blk_drained_end() will do this later.
     * Attaching the notifier also kicks the virtqueues, processing any requests
     * they may already have.
     */
    if (!blk_in_drain(s->conf.conf.blk)) {
        virtio_blk_ioeventfd_attach(s);
    }
    return 0;

  fail_host_notifiers:
    k->set_guest_notifiers(qbus->parent, nvqs, false);
  fail_guest_notifiers:
    s->ioeventfd_disabled = true;
    s->ioeventfd_starting = false;
    return -ENOSYS;
}

/* Stop notifications for new requests from guest.
 *
 * Context: BH in IOThread
 */
static void virtio_blk_ioeventfd_stop_vq_bh(void *opaque)
{
    VirtQueue *vq = opaque;
    EventNotifier *host_notifier = virtio_queue_get_host_notifier(vq);

    virtio_queue_aio_detach_host_notifier(vq, qemu_get_current_aio_context());

    /*
     * Test and clear notifier after disabling event, in case poll callback
     * didn't have time to run.
     */
    virtio_queue_host_notifier_read(host_notifier);
}

/* Context: BQL held */
static void virtio_blk_stop_ioeventfd(VirtIODevice *vdev)
{
    VirtIOBlock *s = VIRTIO_BLK(vdev);
    BusState *qbus = qdev_get_parent_bus(DEVICE(s));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    unsigned i;
    unsigned nvqs = s->conf.num_queues;

    if (!s->ioeventfd_started || s->ioeventfd_stopping) {
        return;
    }

    /* Better luck next time. */
    if (s->ioeventfd_disabled) {
        s->ioeventfd_disabled = false;
        s->ioeventfd_started = false;
        return;
    }
    s->ioeventfd_stopping = true;

    if (!blk_in_drain(s->conf.conf.blk)) {
        for (i = 0; i < nvqs; i++) {
            VirtQueue *vq = virtio_get_queue(vdev, i);
            AioContext *ctx = s->vq_aio_context[i];

            aio_wait_bh_oneshot(ctx, virtio_blk_ioeventfd_stop_vq_bh, vq);
        }
    }

    /*
     * Batch all the host notifiers in a single transaction to avoid
     * quadratic time complexity in address_space_update_ioeventfds().
     */
    memory_region_transaction_begin();

    for (i = 0; i < nvqs; i++) {
        virtio_bus_set_host_notifier(VIRTIO_BUS(qbus), i, false);
    }

    /*
     * The transaction expects the ioeventfds to be open when it
     * commits. Do it now, before the cleanup loop.
     */
    memory_region_transaction_commit();

    for (i = 0; i < nvqs; i++) {
        virtio_bus_cleanup_host_notifier(VIRTIO_BUS(qbus), i);
    }

    /*
     * Set ->ioeventfd_started to false before draining so that host notifiers
     * are not detached/attached anymore.
     */
    s->ioeventfd_started = false;

    /* Wait for virtio_blk_dma_restart_bh() and in flight I/O to complete */
    blk_drain(s->conf.conf.blk);

    /*
     * Try to switch bs back to the QEMU main loop. If other users keep the
     * BlockBackend in the iothread, that's ok
     */
    blk_set_aio_context(s->conf.conf.blk, qemu_get_aio_context(), NULL);

    /* Clean up guest notifier (irq) */
    k->set_guest_notifiers(qbus->parent, nvqs, false);

    s->ioeventfd_stopping = false;
}

static void virtio_blk_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIOBlock *s = VIRTIO_BLK(dev);
    VirtIOBlkConf *conf = &s->conf;
    BlockDriverState *bs;
    Error *err = NULL;
    unsigned i;

    if (!conf->conf.blk) {
        error_setg(errp, "drive property not set");
        return;
    }
    if (!blk_is_inserted(conf->conf.blk)) {
        error_setg(errp, "Device needs media, but drive is empty");
        return;
    }
    if (conf->num_queues == VIRTIO_BLK_AUTO_NUM_QUEUES) {
        conf->num_queues = 1;
    }
    if (!conf->num_queues) {
        error_setg(errp, "num-queues property must be larger than 0");
        return;
    }
    if (conf->queue_size <= 2) {
        error_setg(errp, "invalid queue-size property (%" PRIu16 "), "
                   "must be > 2", conf->queue_size);
        return;
    }
    if (!is_power_of_2(conf->queue_size) ||
        conf->queue_size > VIRTQUEUE_MAX_SIZE) {
        error_setg(errp, "invalid queue-size property (%" PRIu16 "), "
                   "must be a power of 2 (max %d)",
                   conf->queue_size, VIRTQUEUE_MAX_SIZE);
        return;
    }

    if (!blkconf_apply_backend_options(&conf->conf,
                                       !blk_supports_write_perm(conf->conf.blk),
                                       true, errp)) {
        return;
    }
    s->original_wce = blk_enable_write_cache(conf->conf.blk);
    if (!blkconf_geometry(&conf->conf, NULL, 65535, 255, 255, errp)) {
        return;
    }

    if (!blkconf_blocksizes(&conf->conf, errp)) {
        return;
    }

    bs = blk_bs(conf->conf.blk);
    if (bs->bl.zoned != BLK_Z_NONE) {
        virtio_add_feature(&s->host_features, VIRTIO_BLK_F_ZONED);
        if (bs->bl.zoned == BLK_Z_HM) {
            virtio_clear_feature(&s->host_features, VIRTIO_BLK_F_DISCARD);
        }
    }

    if (virtio_has_feature(s->host_features, VIRTIO_BLK_F_DISCARD) &&
        (!conf->max_discard_sectors ||
         conf->max_discard_sectors > BDRV_REQUEST_MAX_SECTORS)) {
        error_setg(errp, "invalid max-discard-sectors property (%" PRIu32 ")"
                   ", must be between 1 and %d",
                   conf->max_discard_sectors, (int)BDRV_REQUEST_MAX_SECTORS);
        return;
    }

    if (virtio_has_feature(s->host_features, VIRTIO_BLK_F_WRITE_ZEROES) &&
        (!conf->max_write_zeroes_sectors ||
         conf->max_write_zeroes_sectors > BDRV_REQUEST_MAX_SECTORS)) {
        error_setg(errp, "invalid max-write-zeroes-sectors property (%" PRIu32
                   "), must be between 1 and %d",
                   conf->max_write_zeroes_sectors,
                   (int)BDRV_REQUEST_MAX_SECTORS);
        return;
    }

    s->config_size = virtio_get_config_size(&virtio_blk_cfg_size_params,
                                            s->host_features);
    virtio_init(vdev, VIRTIO_ID_BLOCK, s->config_size);

    qemu_mutex_init(&s->rq_lock);

    s->blk = conf->conf.blk;
    s->rq = NULL;
    s->sector_mask = (s->conf.conf.logical_block_size / BDRV_SECTOR_SIZE) - 1;

    for (i = 0; i < conf->num_queues; i++) {
        virtio_add_queue(vdev, conf->queue_size, virtio_blk_handle_output);
    }
    qemu_coroutine_inc_pool_size(conf->num_queues * conf->queue_size / 2);

    /* Don't start ioeventfd if transport does not support notifiers. */
    if (!virtio_device_ioeventfd_enabled(vdev)) {
        s->ioeventfd_disabled = true;
    }

    virtio_blk_vq_aio_context_init(s, &err);
    if (err != NULL) {
        error_propagate(errp, err);
        for (i = 0; i < conf->num_queues; i++) {
            virtio_del_queue(vdev, i);
        }
        virtio_cleanup(vdev);
        return;
    }

    /*
     * This must be after virtio_init() so virtio_blk_dma_restart_cb() gets
     * called after ->start_ioeventfd() has already set blk's AioContext.
     */
    s->change =
        qdev_add_vm_change_state_handler(dev, virtio_blk_dma_restart_cb, s);

    blk_ram_registrar_init(&s->blk_ram_registrar, s->blk);
    blk_set_dev_ops(s->blk, &virtio_block_ops, s);

    blk_iostatus_enable(s->blk);

    add_boot_device_lchs(dev, "/disk@0,0",
                         conf->conf.lcyls,
                         conf->conf.lheads,
                         conf->conf.lsecs);
}

static void virtio_blk_device_unrealize(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIOBlock *s = VIRTIO_BLK(dev);
    VirtIOBlkConf *conf = &s->conf;
    unsigned i;

    blk_drain(s->blk);
    del_boot_device_lchs(dev, "/disk@0,0");
    virtio_blk_vq_aio_context_cleanup(s);
    for (i = 0; i < conf->num_queues; i++) {
        virtio_del_queue(vdev, i);
    }
    qemu_coroutine_dec_pool_size(conf->num_queues * conf->queue_size / 2);
    qemu_mutex_destroy(&s->rq_lock);
    blk_ram_registrar_destroy(&s->blk_ram_registrar);
    qemu_del_vm_change_state_handler(s->change);
    blockdev_mark_auto_del(s->blk);
    virtio_cleanup(vdev);
}

static void virtio_blk_instance_init(Object *obj)
{
    VirtIOBlock *s = VIRTIO_BLK(obj);

    device_add_bootindex_property(obj, &s->conf.conf.bootindex,
                                  "bootindex", "/disk@0,0",
                                  DEVICE(obj));
}

static const VMStateDescription vmstate_virtio_blk = {
    .name = "virtio-blk",
    .minimum_version_id = 2,
    .version_id = 2,
    .fields = (const VMStateField[]) {
        VMSTATE_VIRTIO_DEVICE,
        VMSTATE_END_OF_LIST()
    },
};

static Property virtio_blk_properties[] = {
    DEFINE_BLOCK_PROPERTIES(VirtIOBlock, conf.conf),
    DEFINE_BLOCK_ERROR_PROPERTIES(VirtIOBlock, conf.conf),
    DEFINE_BLOCK_CHS_PROPERTIES(VirtIOBlock, conf.conf),
    DEFINE_PROP_STRING("serial", VirtIOBlock, conf.serial),
    DEFINE_PROP_BIT64("config-wce", VirtIOBlock, host_features,
                      VIRTIO_BLK_F_CONFIG_WCE, true),
#ifdef __linux__
    DEFINE_PROP_BIT64("scsi", VirtIOBlock, host_features,
                      VIRTIO_BLK_F_SCSI, false),
#endif
    DEFINE_PROP_BIT("request-merging", VirtIOBlock, conf.request_merging, 0,
                    true),
    DEFINE_PROP_UINT16("num-queues", VirtIOBlock, conf.num_queues,
                       VIRTIO_BLK_AUTO_NUM_QUEUES),
    DEFINE_PROP_UINT16("queue-size", VirtIOBlock, conf.queue_size, 256),
    DEFINE_PROP_BOOL("seg-max-adjust", VirtIOBlock, conf.seg_max_adjust, true),
    DEFINE_PROP_LINK("iothread", VirtIOBlock, conf.iothread, TYPE_IOTHREAD,
                     IOThread *),
    DEFINE_PROP_IOTHREAD_VQ_MAPPING_LIST("iothread-vq-mapping", VirtIOBlock,
                                         conf.iothread_vq_mapping_list),
    DEFINE_PROP_BIT64("discard", VirtIOBlock, host_features,
                      VIRTIO_BLK_F_DISCARD, true),
    DEFINE_PROP_BOOL("report-discard-granularity", VirtIOBlock,
                     conf.report_discard_granularity, true),
    DEFINE_PROP_BIT64("write-zeroes", VirtIOBlock, host_features,
                      VIRTIO_BLK_F_WRITE_ZEROES, true),
    DEFINE_PROP_UINT32("max-discard-sectors", VirtIOBlock,
                       conf.max_discard_sectors, BDRV_REQUEST_MAX_SECTORS),
    DEFINE_PROP_UINT32("max-write-zeroes-sectors", VirtIOBlock,
                       conf.max_write_zeroes_sectors, BDRV_REQUEST_MAX_SECTORS),
    DEFINE_PROP_BOOL("x-enable-wce-if-config-wce", VirtIOBlock,
                     conf.x_enable_wce_if_config_wce, true),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_blk_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    device_class_set_props(dc, virtio_blk_properties);
    dc->vmsd = &vmstate_virtio_blk;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    vdc->realize = virtio_blk_device_realize;
    vdc->unrealize = virtio_blk_device_unrealize;
    vdc->get_config = virtio_blk_update_config;
    vdc->set_config = virtio_blk_set_config;
    vdc->get_features = virtio_blk_get_features;
    vdc->set_status = virtio_blk_set_status;
    vdc->reset = virtio_blk_reset;
    vdc->save = virtio_blk_save_device;
    vdc->load = virtio_blk_load_device;
    vdc->start_ioeventfd = virtio_blk_start_ioeventfd;
    vdc->stop_ioeventfd = virtio_blk_stop_ioeventfd;
}

static const TypeInfo virtio_blk_info = {
    .name = TYPE_VIRTIO_BLK,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VirtIOBlock),
    .instance_init = virtio_blk_instance_init,
    .class_init = virtio_blk_class_init,
};

static void virtio_register_types(void)
{
    type_register_static(&virtio_blk_info);
}

type_init(virtio_register_types)
