/*
 * SCSI Device emulation
 *
 * Copyright (c) 2006 CodeSourcery.
 * Based on code by Fabrice Bellard
 *
 * Written by Paul Brook
 * Modifications:
 *  2009-Dec-12 Artyom Tarasenko : implemented stamdard inquiry for the case
 *                                 when the allocation length of CDB is smaller
 *                                 than 36.
 *  2009-Oct-13 Artyom Tarasenko : implemented the block descriptor in the
 *                                 MODE SENSE response.
 *
 * This code is licensed under the LGPL.
 *
 * Note that this file only handles the SCSI architecture model and device
 * commands.  Emulation of interface/link layer protocols is handled by
 * the host adapter emulator.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qemu/hw-version.h"
#include "qemu/memalign.h"
#include "hw/scsi/scsi.h"
#include "migration/qemu-file-types.h"
#include "migration/vmstate.h"
#include "hw/scsi/emulation.h"
#include "scsi/constants.h"
#include "sysemu/block-backend.h"
#include "sysemu/blockdev.h"
#include "hw/block/block.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "sysemu/dma.h"
#include "sysemu/sysemu.h"
#include "qemu/cutils.h"
#include "trace.h"
#include "qom/object.h"

#ifdef __linux
#include <scsi/sg.h>
#endif

#define SCSI_WRITE_SAME_MAX         (512 * KiB)
#define SCSI_DMA_BUF_SIZE           (128 * KiB)
#define SCSI_MAX_INQUIRY_LEN        256
#define SCSI_MAX_MODE_LEN           256

#define DEFAULT_DISCARD_GRANULARITY (4 * KiB)
#define DEFAULT_MAX_UNMAP_SIZE      (1 * GiB)
#define DEFAULT_MAX_IO_SIZE         INT_MAX     /* 2 GB - 1 block */

#define TYPE_SCSI_DISK_BASE         "scsi-disk-base"

OBJECT_DECLARE_TYPE(SCSIDiskState, SCSIDiskClass, SCSI_DISK_BASE)

struct SCSIDiskClass {
    SCSIDeviceClass parent_class;
    DMAIOFunc       *dma_readv;
    DMAIOFunc       *dma_writev;
    bool            (*need_fua_emulation)(SCSICommand *cmd);
    void            (*update_sense)(SCSIRequest *r);
};

typedef struct SCSIDiskReq {
    SCSIRequest req;
    /* Both sector and sector_count are in terms of BDRV_SECTOR_SIZE bytes.  */
    uint64_t sector;
    uint32_t sector_count;
    uint32_t buflen;
    bool started;
    bool need_fua_emulation;
    struct iovec iov;
    QEMUIOVector qiov;
    BlockAcctCookie acct;
} SCSIDiskReq;

#define SCSI_DISK_F_REMOVABLE             0
#define SCSI_DISK_F_DPOFUA                1
#define SCSI_DISK_F_NO_REMOVABLE_DEVOPS   2

struct SCSIDiskState {
    SCSIDevice qdev;
    uint32_t features;
    bool media_changed;
    bool media_event;
    bool eject_request;
    uint16_t port_index;
    uint64_t max_unmap_size;
    uint64_t max_io_size;
    uint32_t quirks;
    QEMUBH *bh;
    char *version;
    char *serial;
    char *vendor;
    char *product;
    char *device_id;
    bool tray_open;
    bool tray_locked;
    /*
     * 0x0000        - rotation rate not reported
     * 0x0001        - non-rotating medium (SSD)
     * 0x0002-0x0400 - reserved
     * 0x0401-0xffe  - rotations per minute
     * 0xffff        - reserved
     */
    uint16_t rotation_rate;
};

static void scsi_free_request(SCSIRequest *req)
{
    SCSIDiskReq *r = DO_UPCAST(SCSIDiskReq, req, req);

    qemu_vfree(r->iov.iov_base);
}

/* Helper function for command completion with sense.  */
static void scsi_check_condition(SCSIDiskReq *r, SCSISense sense)
{
    trace_scsi_disk_check_condition(r->req.tag, sense.key, sense.asc,
                                    sense.ascq);
    scsi_req_build_sense(&r->req, sense);
    scsi_req_complete(&r->req, CHECK_CONDITION);
}

static void scsi_init_iovec(SCSIDiskReq *r, size_t size)
{
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, r->req.dev);

    if (!r->iov.iov_base) {
        r->buflen = size;
        r->iov.iov_base = blk_blockalign(s->qdev.conf.blk, r->buflen);
    }
    r->iov.iov_len = MIN(r->sector_count * BDRV_SECTOR_SIZE, r->buflen);
    qemu_iovec_init_external(&r->qiov, &r->iov, 1);
}

static void scsi_disk_save_request(QEMUFile *f, SCSIRequest *req)
{
    SCSIDiskReq *r = DO_UPCAST(SCSIDiskReq, req, req);

    qemu_put_be64s(f, &r->sector);
    qemu_put_be32s(f, &r->sector_count);
    qemu_put_be32s(f, &r->buflen);
    if (r->buflen) {
        if (r->req.cmd.mode == SCSI_XFER_TO_DEV) {
            qemu_put_buffer(f, r->iov.iov_base, r->iov.iov_len);
        } else if (!req->retry) {
            uint32_t len = r->iov.iov_len;
            qemu_put_be32s(f, &len);
            qemu_put_buffer(f, r->iov.iov_base, r->iov.iov_len);
        }
    }
}

static void scsi_disk_load_request(QEMUFile *f, SCSIRequest *req)
{
    SCSIDiskReq *r = DO_UPCAST(SCSIDiskReq, req, req);

    qemu_get_be64s(f, &r->sector);
    qemu_get_be32s(f, &r->sector_count);
    qemu_get_be32s(f, &r->buflen);
    if (r->buflen) {
        scsi_init_iovec(r, r->buflen);
        if (r->req.cmd.mode == SCSI_XFER_TO_DEV) {
            qemu_get_buffer(f, r->iov.iov_base, r->iov.iov_len);
        } else if (!r->req.retry) {
            uint32_t len;
            qemu_get_be32s(f, &len);
            r->iov.iov_len = len;
            assert(r->iov.iov_len <= r->buflen);
            qemu_get_buffer(f, r->iov.iov_base, r->iov.iov_len);
        }
    }

    qemu_iovec_init_external(&r->qiov, &r->iov, 1);
}

/*
 * scsi_handle_rw_error has two return values.  False means that the error
 * must be ignored, true means that the error has been processed and the
 * caller should not do anything else for this request.  Note that
 * scsi_handle_rw_error always manages its reference counts, independent
 * of the return value.
 */
static bool scsi_handle_rw_error(SCSIDiskReq *r, int ret, bool acct_failed)
{
    bool is_read = (r->req.cmd.mode == SCSI_XFER_FROM_DEV);
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, r->req.dev);
    SCSIDiskClass *sdc = (SCSIDiskClass *) object_get_class(OBJECT(s));
    SCSISense sense = SENSE_CODE(NO_SENSE);
    int error = 0;
    bool req_has_sense = false;
    BlockErrorAction action;
    int status;

    if (ret < 0) {
        status = scsi_sense_from_errno(-ret, &sense);
        error = -ret;
    } else {
        /* A passthrough command has completed with nonzero status.  */
        status = ret;
        if (status == CHECK_CONDITION) {
            req_has_sense = true;
            error = scsi_sense_buf_to_errno(r->req.sense, sizeof(r->req.sense));
        } else {
            error = EINVAL;
        }
    }

    /*
     * Check whether the error has to be handled by the guest or should
     * rather follow the rerror=/werror= settings.  Guest-handled errors
     * are usually retried immediately, so do not post them to QMP and
     * do not account them as failed I/O.
     */
    if (req_has_sense &&
        scsi_sense_buf_is_guest_recoverable(r->req.sense, sizeof(r->req.sense))) {
        action = BLOCK_ERROR_ACTION_REPORT;
        acct_failed = false;
    } else {
        action = blk_get_error_action(s->qdev.conf.blk, is_read, error);
        blk_error_action(s->qdev.conf.blk, action, is_read, error);
    }

    switch (action) {
    case BLOCK_ERROR_ACTION_REPORT:
        if (acct_failed) {
            block_acct_failed(blk_get_stats(s->qdev.conf.blk), &r->acct);
        }
        if (req_has_sense) {
            sdc->update_sense(&r->req);
        } else if (status == CHECK_CONDITION) {
            scsi_req_build_sense(&r->req, sense);
        }
        scsi_req_complete(&r->req, status);
        return true;

    case BLOCK_ERROR_ACTION_IGNORE:
        return false;

    case BLOCK_ERROR_ACTION_STOP:
        scsi_req_retry(&r->req);
        return true;

    default:
        g_assert_not_reached();
    }
}

static bool scsi_disk_req_check_error(SCSIDiskReq *r, int ret, bool acct_failed)
{
    if (r->req.io_canceled) {
        scsi_req_cancel_complete(&r->req);
        return true;
    }

    if (ret < 0) {
        return scsi_handle_rw_error(r, ret, acct_failed);
    }

    return false;
}

static void scsi_aio_complete(void *opaque, int ret)
{
    SCSIDiskReq *r = (SCSIDiskReq *)opaque;
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, r->req.dev);

    assert(r->req.aiocb != NULL);
    r->req.aiocb = NULL;
    aio_context_acquire(blk_get_aio_context(s->qdev.conf.blk));
    if (scsi_disk_req_check_error(r, ret, true)) {
        goto done;
    }

    block_acct_done(blk_get_stats(s->qdev.conf.blk), &r->acct);
    scsi_req_complete(&r->req, GOOD);

done:
    aio_context_release(blk_get_aio_context(s->qdev.conf.blk));
    scsi_req_unref(&r->req);
}

static bool scsi_is_cmd_fua(SCSICommand *cmd)
{
    switch (cmd->buf[0]) {
    case READ_10:
    case READ_12:
    case READ_16:
    case WRITE_10:
    case WRITE_12:
    case WRITE_16:
        return (cmd->buf[1] & 8) != 0;

    case VERIFY_10:
    case VERIFY_12:
    case VERIFY_16:
    case WRITE_VERIFY_10:
    case WRITE_VERIFY_12:
    case WRITE_VERIFY_16:
        return true;

    case READ_6:
    case WRITE_6:
    default:
        return false;
    }
}

static void scsi_write_do_fua(SCSIDiskReq *r)
{
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, r->req.dev);

    assert(r->req.aiocb == NULL);
    assert(!r->req.io_canceled);

    if (r->need_fua_emulation) {
        block_acct_start(blk_get_stats(s->qdev.conf.blk), &r->acct, 0,
                         BLOCK_ACCT_FLUSH);
        r->req.aiocb = blk_aio_flush(s->qdev.conf.blk, scsi_aio_complete, r);
        return;
    }

    scsi_req_complete(&r->req, GOOD);
    scsi_req_unref(&r->req);
}

static void scsi_dma_complete_noio(SCSIDiskReq *r, int ret)
{
    assert(r->req.aiocb == NULL);
    if (scsi_disk_req_check_error(r, ret, false)) {
        goto done;
    }

    r->sector += r->sector_count;
    r->sector_count = 0;
    if (r->req.cmd.mode == SCSI_XFER_TO_DEV) {
        scsi_write_do_fua(r);
        return;
    } else {
        scsi_req_complete(&r->req, GOOD);
    }

done:
    scsi_req_unref(&r->req);
}

static void scsi_dma_complete(void *opaque, int ret)
{
    SCSIDiskReq *r = (SCSIDiskReq *)opaque;
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, r->req.dev);

    assert(r->req.aiocb != NULL);
    r->req.aiocb = NULL;

    aio_context_acquire(blk_get_aio_context(s->qdev.conf.blk));
    if (ret < 0) {
        block_acct_failed(blk_get_stats(s->qdev.conf.blk), &r->acct);
    } else {
        block_acct_done(blk_get_stats(s->qdev.conf.blk), &r->acct);
    }
    scsi_dma_complete_noio(r, ret);
    aio_context_release(blk_get_aio_context(s->qdev.conf.blk));
}

static void scsi_read_complete_noio(SCSIDiskReq *r, int ret)
{
    uint32_t n;

    assert(r->req.aiocb == NULL);
    if (scsi_disk_req_check_error(r, ret, false)) {
        goto done;
    }

    n = r->qiov.size / BDRV_SECTOR_SIZE;
    r->sector += n;
    r->sector_count -= n;
    scsi_req_data(&r->req, r->qiov.size);

done:
    scsi_req_unref(&r->req);
}

static void scsi_read_complete(void *opaque, int ret)
{
    SCSIDiskReq *r = (SCSIDiskReq *)opaque;
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, r->req.dev);

    assert(r->req.aiocb != NULL);
    r->req.aiocb = NULL;

    aio_context_acquire(blk_get_aio_context(s->qdev.conf.blk));
    if (ret < 0) {
        block_acct_failed(blk_get_stats(s->qdev.conf.blk), &r->acct);
    } else {
        block_acct_done(blk_get_stats(s->qdev.conf.blk), &r->acct);
        trace_scsi_disk_read_complete(r->req.tag, r->qiov.size);
    }
    scsi_read_complete_noio(r, ret);
    aio_context_release(blk_get_aio_context(s->qdev.conf.blk));
}

/* Actually issue a read to the block device.  */
static void scsi_do_read(SCSIDiskReq *r, int ret)
{
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, r->req.dev);
    SCSIDiskClass *sdc = (SCSIDiskClass *) object_get_class(OBJECT(s));

    assert (r->req.aiocb == NULL);
    if (scsi_disk_req_check_error(r, ret, false)) {
        goto done;
    }

    /* The request is used as the AIO opaque value, so add a ref.  */
    scsi_req_ref(&r->req);

    if (r->req.sg) {
        dma_acct_start(s->qdev.conf.blk, &r->acct, r->req.sg, BLOCK_ACCT_READ);
        r->req.residual -= r->req.sg->size;
        r->req.aiocb = dma_blk_io(blk_get_aio_context(s->qdev.conf.blk),
                                  r->req.sg, r->sector << BDRV_SECTOR_BITS,
                                  BDRV_SECTOR_SIZE,
                                  sdc->dma_readv, r, scsi_dma_complete, r,
                                  DMA_DIRECTION_FROM_DEVICE);
    } else {
        scsi_init_iovec(r, SCSI_DMA_BUF_SIZE);
        block_acct_start(blk_get_stats(s->qdev.conf.blk), &r->acct,
                         r->qiov.size, BLOCK_ACCT_READ);
        r->req.aiocb = sdc->dma_readv(r->sector << BDRV_SECTOR_BITS, &r->qiov,
                                      scsi_read_complete, r, r);
    }

done:
    scsi_req_unref(&r->req);
}

static void scsi_do_read_cb(void *opaque, int ret)
{
    SCSIDiskReq *r = (SCSIDiskReq *)opaque;
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, r->req.dev);

    assert (r->req.aiocb != NULL);
    r->req.aiocb = NULL;

    aio_context_acquire(blk_get_aio_context(s->qdev.conf.blk));
    if (ret < 0) {
        block_acct_failed(blk_get_stats(s->qdev.conf.blk), &r->acct);
    } else {
        block_acct_done(blk_get_stats(s->qdev.conf.blk), &r->acct);
    }
    scsi_do_read(opaque, ret);
    aio_context_release(blk_get_aio_context(s->qdev.conf.blk));
}

/* Read more data from scsi device into buffer.  */
static void scsi_read_data(SCSIRequest *req)
{
    SCSIDiskReq *r = DO_UPCAST(SCSIDiskReq, req, req);
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, r->req.dev);
    bool first;

    trace_scsi_disk_read_data_count(r->sector_count);
    if (r->sector_count == 0) {
        /* This also clears the sense buffer for REQUEST SENSE.  */
        scsi_req_complete(&r->req, GOOD);
        return;
    }

    /* No data transfer may already be in progress */
    assert(r->req.aiocb == NULL);

    /* The request is used as the AIO opaque value, so add a ref.  */
    scsi_req_ref(&r->req);
    if (r->req.cmd.mode == SCSI_XFER_TO_DEV) {
        trace_scsi_disk_read_data_invalid();
        scsi_read_complete_noio(r, -EINVAL);
        return;
    }

    if (!blk_is_available(req->dev->conf.blk)) {
        scsi_read_complete_noio(r, -ENOMEDIUM);
        return;
    }

    first = !r->started;
    r->started = true;
    if (first && r->need_fua_emulation) {
        block_acct_start(blk_get_stats(s->qdev.conf.blk), &r->acct, 0,
                         BLOCK_ACCT_FLUSH);
        r->req.aiocb = blk_aio_flush(s->qdev.conf.blk, scsi_do_read_cb, r);
    } else {
        scsi_do_read(r, 0);
    }
}

static void scsi_write_complete_noio(SCSIDiskReq *r, int ret)
{
    uint32_t n;

    assert (r->req.aiocb == NULL);
    if (scsi_disk_req_check_error(r, ret, false)) {
        goto done;
    }

    n = r->qiov.size / BDRV_SECTOR_SIZE;
    r->sector += n;
    r->sector_count -= n;
    if (r->sector_count == 0) {
        scsi_write_do_fua(r);
        return;
    } else {
        scsi_init_iovec(r, SCSI_DMA_BUF_SIZE);
        trace_scsi_disk_write_complete_noio(r->req.tag, r->qiov.size);
        scsi_req_data(&r->req, r->qiov.size);
    }

done:
    scsi_req_unref(&r->req);
}

static void scsi_write_complete(void * opaque, int ret)
{
    SCSIDiskReq *r = (SCSIDiskReq *)opaque;
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, r->req.dev);

    assert (r->req.aiocb != NULL);
    r->req.aiocb = NULL;

    aio_context_acquire(blk_get_aio_context(s->qdev.conf.blk));
    if (ret < 0) {
        block_acct_failed(blk_get_stats(s->qdev.conf.blk), &r->acct);
    } else {
        block_acct_done(blk_get_stats(s->qdev.conf.blk), &r->acct);
    }
    scsi_write_complete_noio(r, ret);
    aio_context_release(blk_get_aio_context(s->qdev.conf.blk));
}

static void scsi_write_data(SCSIRequest *req)
{
    SCSIDiskReq *r = DO_UPCAST(SCSIDiskReq, req, req);
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, r->req.dev);
    SCSIDiskClass *sdc = (SCSIDiskClass *) object_get_class(OBJECT(s));

    /* No data transfer may already be in progress */
    assert(r->req.aiocb == NULL);

    /* The request is used as the AIO opaque value, so add a ref.  */
    scsi_req_ref(&r->req);
    if (r->req.cmd.mode != SCSI_XFER_TO_DEV) {
        trace_scsi_disk_write_data_invalid();
        scsi_write_complete_noio(r, -EINVAL);
        return;
    }

    if (!r->req.sg && !r->qiov.size) {
        /* Called for the first time.  Ask the driver to send us more data.  */
        r->started = true;
        scsi_write_complete_noio(r, 0);
        return;
    }
    if (!blk_is_available(req->dev->conf.blk)) {
        scsi_write_complete_noio(r, -ENOMEDIUM);
        return;
    }

    if (r->req.cmd.buf[0] == VERIFY_10 || r->req.cmd.buf[0] == VERIFY_12 ||
        r->req.cmd.buf[0] == VERIFY_16) {
        if (r->req.sg) {
            scsi_dma_complete_noio(r, 0);
        } else {
            scsi_write_complete_noio(r, 0);
        }
        return;
    }

    if (r->req.sg) {
        dma_acct_start(s->qdev.conf.blk, &r->acct, r->req.sg, BLOCK_ACCT_WRITE);
        r->req.residual -= r->req.sg->size;
        r->req.aiocb = dma_blk_io(blk_get_aio_context(s->qdev.conf.blk),
                                  r->req.sg, r->sector << BDRV_SECTOR_BITS,
                                  BDRV_SECTOR_SIZE,
                                  sdc->dma_writev, r, scsi_dma_complete, r,
                                  DMA_DIRECTION_TO_DEVICE);
    } else {
        block_acct_start(blk_get_stats(s->qdev.conf.blk), &r->acct,
                         r->qiov.size, BLOCK_ACCT_WRITE);
        r->req.aiocb = sdc->dma_writev(r->sector << BDRV_SECTOR_BITS, &r->qiov,
                                       scsi_write_complete, r, r);
    }
}

/* Return a pointer to the data buffer.  */
static uint8_t *scsi_get_buf(SCSIRequest *req)
{
    SCSIDiskReq *r = DO_UPCAST(SCSIDiskReq, req, req);

    return (uint8_t *)r->iov.iov_base;
}

static int scsi_disk_emulate_vpd_page(SCSIRequest *req, uint8_t *outbuf)
{
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, req->dev);
    uint8_t page_code = req->cmd.buf[2];
    int start, buflen = 0;

    outbuf[buflen++] = s->qdev.type & 0x1f;
    outbuf[buflen++] = page_code;
    outbuf[buflen++] = 0x00;
    outbuf[buflen++] = 0x00;
    start = buflen;

    switch (page_code) {
    case 0x00: /* Supported page codes, mandatory */
    {
        trace_scsi_disk_emulate_vpd_page_00(req->cmd.xfer);
        outbuf[buflen++] = 0x00; /* list of supported pages (this page) */
        if (s->serial) {
            outbuf[buflen++] = 0x80; /* unit serial number */
        }
        outbuf[buflen++] = 0x83; /* device identification */
        if (s->qdev.type == TYPE_DISK) {
            outbuf[buflen++] = 0xb0; /* block limits */
            outbuf[buflen++] = 0xb1; /* block device characteristics */
            outbuf[buflen++] = 0xb2; /* thin provisioning */
        }
        break;
    }
    case 0x80: /* Device serial number, optional */
    {
        int l;

        if (!s->serial) {
            trace_scsi_disk_emulate_vpd_page_80_not_supported();
            return -1;
        }

        l = strlen(s->serial);
        if (l > 36) {
            l = 36;
        }

        trace_scsi_disk_emulate_vpd_page_80(req->cmd.xfer);
        memcpy(outbuf + buflen, s->serial, l);
        buflen += l;
        break;
    }

    case 0x83: /* Device identification page, mandatory */
    {
        int id_len = s->device_id ? MIN(strlen(s->device_id), 255 - 8) : 0;

        trace_scsi_disk_emulate_vpd_page_83(req->cmd.xfer);

        if (id_len) {
            outbuf[buflen++] = 0x2; /* ASCII */
            outbuf[buflen++] = 0;   /* not officially assigned */
            outbuf[buflen++] = 0;   /* reserved */
            outbuf[buflen++] = id_len; /* length of data following */
            memcpy(outbuf + buflen, s->device_id, id_len);
            buflen += id_len;
        }

        if (s->qdev.wwn) {
            outbuf[buflen++] = 0x1; /* Binary */
            outbuf[buflen++] = 0x3; /* NAA */
            outbuf[buflen++] = 0;   /* reserved */
            outbuf[buflen++] = 8;
            stq_be_p(&outbuf[buflen], s->qdev.wwn);
            buflen += 8;
        }

        if (s->qdev.port_wwn) {
            outbuf[buflen++] = 0x61; /* SAS / Binary */
            outbuf[buflen++] = 0x93; /* PIV / Target port / NAA */
            outbuf[buflen++] = 0;    /* reserved */
            outbuf[buflen++] = 8;
            stq_be_p(&outbuf[buflen], s->qdev.port_wwn);
            buflen += 8;
        }

        if (s->port_index) {
            outbuf[buflen++] = 0x61; /* SAS / Binary */

            /* PIV/Target port/relative target port */
            outbuf[buflen++] = 0x94;

            outbuf[buflen++] = 0;    /* reserved */
            outbuf[buflen++] = 4;
            stw_be_p(&outbuf[buflen + 2], s->port_index);
            buflen += 4;
        }
        break;
    }
    case 0xb0: /* block limits */
    {
        SCSIBlockLimits bl = {};

        if (s->qdev.type == TYPE_ROM) {
            trace_scsi_disk_emulate_vpd_page_b0_not_supported();
            return -1;
        }
        bl.wsnz = 1;
        bl.unmap_sectors =
            s->qdev.conf.discard_granularity / s->qdev.blocksize;
        bl.min_io_size =
            s->qdev.conf.min_io_size / s->qdev.blocksize;
        bl.opt_io_size =
            s->qdev.conf.opt_io_size / s->qdev.blocksize;
        bl.max_unmap_sectors =
            s->max_unmap_size / s->qdev.blocksize;
        bl.max_io_sectors =
            s->max_io_size / s->qdev.blocksize;
        /* 255 descriptors fit in 4 KiB with an 8-byte header */
        bl.max_unmap_descr = 255;

        if (s->qdev.type == TYPE_DISK) {
            int max_transfer_blk = blk_get_max_transfer(s->qdev.conf.blk);
            int max_io_sectors_blk =
                max_transfer_blk / s->qdev.blocksize;

            bl.max_io_sectors =
                MIN_NON_ZERO(max_io_sectors_blk, bl.max_io_sectors);
        }
        buflen += scsi_emulate_block_limits(outbuf + buflen, &bl);
        break;
    }
    case 0xb1: /* block device characteristics */
    {
        buflen = 0x40;
        outbuf[4] = (s->rotation_rate >> 8) & 0xff;
        outbuf[5] = s->rotation_rate & 0xff;
        outbuf[6] = 0; /* PRODUCT TYPE */
        outbuf[7] = 0; /* WABEREQ | WACEREQ | NOMINAL FORM FACTOR */
        outbuf[8] = 0; /* VBULS */
        break;
    }
    case 0xb2: /* thin provisioning */
    {
        buflen = 8;
        outbuf[4] = 0;
        outbuf[5] = 0xe0; /* unmap & write_same 10/16 all supported */
        outbuf[6] = s->qdev.conf.discard_granularity ? 2 : 1;
        outbuf[7] = 0;
        break;
    }
    default:
        return -1;
    }
    /* done with EVPD */
    assert(buflen - start <= 255);
    outbuf[start - 1] = buflen - start;
    return buflen;
}

static int scsi_disk_emulate_inquiry(SCSIRequest *req, uint8_t *outbuf)
{
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, req->dev);
    int buflen = 0;

    if (req->cmd.buf[1] & 0x1) {
        /* Vital product data */
        return scsi_disk_emulate_vpd_page(req, outbuf);
    }

    /* Standard INQUIRY data */
    if (req->cmd.buf[2] != 0) {
        return -1;
    }

    /* PAGE CODE == 0 */
    buflen = req->cmd.xfer;
    if (buflen > SCSI_MAX_INQUIRY_LEN) {
        buflen = SCSI_MAX_INQUIRY_LEN;
    }

    outbuf[0] = s->qdev.type & 0x1f;
    outbuf[1] = (s->features & (1 << SCSI_DISK_F_REMOVABLE)) ? 0x80 : 0;

    strpadcpy((char *) &outbuf[16], 16, s->product, ' ');
    strpadcpy((char *) &outbuf[8], 8, s->vendor, ' ');

    memset(&outbuf[32], 0, 4);
    memcpy(&outbuf[32], s->version, MIN(4, strlen(s->version)));
    /*
     * We claim conformance to SPC-3, which is required for guests
     * to ask for modern features like READ CAPACITY(16) or the
     * block characteristics VPD page by default.  Not all of SPC-3
     * is actually implemented, but we're good enough.
     */
    outbuf[2] = s->qdev.default_scsi_version;
    outbuf[3] = 2 | 0x10; /* Format 2, HiSup */

    if (buflen > 36) {
        outbuf[4] = buflen - 5; /* Additional Length = (Len - 1) - 4 */
    } else {
        /* If the allocation length of CDB is too small,
               the additional length is not adjusted */
        outbuf[4] = 36 - 5;
    }

    /* Sync data transfer and TCQ.  */
    outbuf[7] = 0x10 | (req->bus->info->tcq ? 0x02 : 0);
    return buflen;
}

static inline bool media_is_dvd(SCSIDiskState *s)
{
    uint64_t nb_sectors;
    if (s->qdev.type != TYPE_ROM) {
        return false;
    }
    if (!blk_is_available(s->qdev.conf.blk)) {
        return false;
    }
    blk_get_geometry(s->qdev.conf.blk, &nb_sectors);
    return nb_sectors > CD_MAX_SECTORS;
}

static inline bool media_is_cd(SCSIDiskState *s)
{
    uint64_t nb_sectors;
    if (s->qdev.type != TYPE_ROM) {
        return false;
    }
    if (!blk_is_available(s->qdev.conf.blk)) {
        return false;
    }
    blk_get_geometry(s->qdev.conf.blk, &nb_sectors);
    return nb_sectors <= CD_MAX_SECTORS;
}

static int scsi_read_disc_information(SCSIDiskState *s, SCSIDiskReq *r,
                                      uint8_t *outbuf)
{
    uint8_t type = r->req.cmd.buf[1] & 7;

    if (s->qdev.type != TYPE_ROM) {
        return -1;
    }

    /* Types 1/2 are only defined for Blu-Ray.  */
    if (type != 0) {
        scsi_check_condition(r, SENSE_CODE(INVALID_FIELD));
        return -1;
    }

    memset(outbuf, 0, 34);
    outbuf[1] = 32;
    outbuf[2] = 0xe; /* last session complete, disc finalized */
    outbuf[3] = 1;   /* first track on disc */
    outbuf[4] = 1;   /* # of sessions */
    outbuf[5] = 1;   /* first track of last session */
    outbuf[6] = 1;   /* last track of last session */
    outbuf[7] = 0x20; /* unrestricted use */
    outbuf[8] = 0x00; /* CD-ROM or DVD-ROM */
    /* 9-10-11: most significant byte corresponding bytes 4-5-6 */
    /* 12-23: not meaningful for CD-ROM or DVD-ROM */
    /* 24-31: disc bar code */
    /* 32: disc application code */
    /* 33: number of OPC tables */

    return 34;
}

static int scsi_read_dvd_structure(SCSIDiskState *s, SCSIDiskReq *r,
                                   uint8_t *outbuf)
{
    static const int rds_caps_size[5] = {
        [0] = 2048 + 4,
        [1] = 4 + 4,
        [3] = 188 + 4,
        [4] = 2048 + 4,
    };

    uint8_t media = r->req.cmd.buf[1];
    uint8_t layer = r->req.cmd.buf[6];
    uint8_t format = r->req.cmd.buf[7];
    int size = -1;

    if (s->qdev.type != TYPE_ROM) {
        return -1;
    }
    if (media != 0) {
        scsi_check_condition(r, SENSE_CODE(INVALID_FIELD));
        return -1;
    }

    if (format != 0xff) {
        if (!blk_is_available(s->qdev.conf.blk)) {
            scsi_check_condition(r, SENSE_CODE(NO_MEDIUM));
            return -1;
        }
        if (media_is_cd(s)) {
            scsi_check_condition(r, SENSE_CODE(INCOMPATIBLE_FORMAT));
            return -1;
        }
        if (format >= ARRAY_SIZE(rds_caps_size)) {
            return -1;
        }
        size = rds_caps_size[format];
        memset(outbuf, 0, size);
    }

    switch (format) {
    case 0x00: {
        /* Physical format information */
        uint64_t nb_sectors;
        if (layer != 0) {
            goto fail;
        }
        blk_get_geometry(s->qdev.conf.blk, &nb_sectors);

        outbuf[4] = 1;   /* DVD-ROM, part version 1 */
        outbuf[5] = 0xf; /* 120mm disc, minimum rate unspecified */
        outbuf[6] = 1;   /* one layer, read-only (per MMC-2 spec) */
        outbuf[7] = 0;   /* default densities */

        stl_be_p(&outbuf[12], (nb_sectors >> 2) - 1); /* end sector */
        stl_be_p(&outbuf[16], (nb_sectors >> 2) - 1); /* l0 end sector */
        break;
    }

    case 0x01: /* DVD copyright information, all zeros */
        break;

    case 0x03: /* BCA information - invalid field for no BCA info */
        return -1;

    case 0x04: /* DVD disc manufacturing information, all zeros */
        break;

    case 0xff: { /* List capabilities */
        int i;
        size = 4;
        for (i = 0; i < ARRAY_SIZE(rds_caps_size); i++) {
            if (!rds_caps_size[i]) {
                continue;
            }
            outbuf[size] = i;
            outbuf[size + 1] = 0x40; /* Not writable, readable */
            stw_be_p(&outbuf[size + 2], rds_caps_size[i]);
            size += 4;
        }
        break;
     }

    default:
        return -1;
    }

    /* Size of buffer, not including 2 byte size field */
    stw_be_p(outbuf, size - 2);
    return size;

fail:
    return -1;
}

static int scsi_event_status_media(SCSIDiskState *s, uint8_t *outbuf)
{
    uint8_t event_code, media_status;

    media_status = 0;
    if (s->tray_open) {
        media_status = MS_TRAY_OPEN;
    } else if (blk_is_inserted(s->qdev.conf.blk)) {
        media_status = MS_MEDIA_PRESENT;
    }

    /* Event notification descriptor */
    event_code = MEC_NO_CHANGE;
    if (media_status != MS_TRAY_OPEN) {
        if (s->media_event) {
            event_code = MEC_NEW_MEDIA;
            s->media_event = false;
        } else if (s->eject_request) {
            event_code = MEC_EJECT_REQUESTED;
            s->eject_request = false;
        }
    }

    outbuf[0] = event_code;
    outbuf[1] = media_status;

    /* These fields are reserved, just clear them. */
    outbuf[2] = 0;
    outbuf[3] = 0;
    return 4;
}

static int scsi_get_event_status_notification(SCSIDiskState *s, SCSIDiskReq *r,
                                              uint8_t *outbuf)
{
    int size;
    uint8_t *buf = r->req.cmd.buf;
    uint8_t notification_class_request = buf[4];
    if (s->qdev.type != TYPE_ROM) {
        return -1;
    }
    if ((buf[1] & 1) == 0) {
        /* asynchronous */
        return -1;
    }

    size = 4;
    outbuf[0] = outbuf[1] = 0;
    outbuf[3] = 1 << GESN_MEDIA; /* supported events */
    if (notification_class_request & (1 << GESN_MEDIA)) {
        outbuf[2] = GESN_MEDIA;
        size += scsi_event_status_media(s, &outbuf[size]);
    } else {
        outbuf[2] = 0x80;
    }
    stw_be_p(outbuf, size - 4);
    return size;
}

static int scsi_get_configuration(SCSIDiskState *s, uint8_t *outbuf)
{
    int current;

    if (s->qdev.type != TYPE_ROM) {
        return -1;
    }

    if (media_is_dvd(s)) {
        current = MMC_PROFILE_DVD_ROM;
    } else if (media_is_cd(s)) {
        current = MMC_PROFILE_CD_ROM;
    } else {
        current = MMC_PROFILE_NONE;
    }

    memset(outbuf, 0, 40);
    stl_be_p(&outbuf[0], 36); /* Bytes after the data length field */
    stw_be_p(&outbuf[6], current);
    /* outbuf[8] - outbuf[19]: Feature 0 - Profile list */
    outbuf[10] = 0x03; /* persistent, current */
    outbuf[11] = 8; /* two profiles */
    stw_be_p(&outbuf[12], MMC_PROFILE_DVD_ROM);
    outbuf[14] = (current == MMC_PROFILE_DVD_ROM);
    stw_be_p(&outbuf[16], MMC_PROFILE_CD_ROM);
    outbuf[18] = (current == MMC_PROFILE_CD_ROM);
    /* outbuf[20] - outbuf[31]: Feature 1 - Core feature */
    stw_be_p(&outbuf[20], 1);
    outbuf[22] = 0x08 | 0x03; /* version 2, persistent, current */
    outbuf[23] = 8;
    stl_be_p(&outbuf[24], 1); /* SCSI */
    outbuf[28] = 1; /* DBE = 1, mandatory */
    /* outbuf[32] - outbuf[39]: Feature 3 - Removable media feature */
    stw_be_p(&outbuf[32], 3);
    outbuf[34] = 0x08 | 0x03; /* version 2, persistent, current */
    outbuf[35] = 4;
    outbuf[36] = 0x39; /* tray, load=1, eject=1, unlocked at powerup, lock=1 */
    /* TODO: Random readable, CD read, DVD read, drive serial number,
       power management */
    return 40;
}

static int scsi_emulate_mechanism_status(SCSIDiskState *s, uint8_t *outbuf)
{
    if (s->qdev.type != TYPE_ROM) {
        return -1;
    }
    memset(outbuf, 0, 8);
    outbuf[5] = 1; /* CD-ROM */
    return 8;
}

static int mode_sense_page(SCSIDiskState *s, int page, uint8_t **p_outbuf,
                           int page_control)
{
    static const int mode_sense_valid[0x3f] = {
        [MODE_PAGE_VENDOR_SPECIFIC]        = (1 << TYPE_DISK) | (1 << TYPE_ROM),
        [MODE_PAGE_HD_GEOMETRY]            = (1 << TYPE_DISK),
        [MODE_PAGE_FLEXIBLE_DISK_GEOMETRY] = (1 << TYPE_DISK),
        [MODE_PAGE_CACHING]                = (1 << TYPE_DISK) | (1 << TYPE_ROM),
        [MODE_PAGE_R_W_ERROR]              = (1 << TYPE_DISK) | (1 << TYPE_ROM),
        [MODE_PAGE_AUDIO_CTL]              = (1 << TYPE_ROM),
        [MODE_PAGE_CAPABILITIES]           = (1 << TYPE_ROM),
        [MODE_PAGE_APPLE_VENDOR]           = (1 << TYPE_ROM),
    };

    uint8_t *p = *p_outbuf + 2;
    int length;

    assert(page < ARRAY_SIZE(mode_sense_valid));
    if ((mode_sense_valid[page] & (1 << s->qdev.type)) == 0) {
        return -1;
    }

    /*
     * If Changeable Values are requested, a mask denoting those mode parameters
     * that are changeable shall be returned. As we currently don't support
     * parameter changes via MODE_SELECT all bits are returned set to zero.
     * The buffer was already menset to zero by the caller of this function.
     *
     * The offsets here are off by two compared to the descriptions in the
     * SCSI specs, because those include a 2-byte header.  This is unfortunate,
     * but it is done so that offsets are consistent within our implementation
     * of MODE SENSE and MODE SELECT.  MODE SELECT has to deal with both
     * 2-byte and 4-byte headers.
     */
    switch (page) {
    case MODE_PAGE_HD_GEOMETRY:
        length = 0x16;
        if (page_control == 1) { /* Changeable Values */
            break;
        }
        /* if a geometry hint is available, use it */
        p[0] = (s->qdev.conf.cyls >> 16) & 0xff;
        p[1] = (s->qdev.conf.cyls >> 8) & 0xff;
        p[2] = s->qdev.conf.cyls & 0xff;
        p[3] = s->qdev.conf.heads & 0xff;
        /* Write precomp start cylinder, disabled */
        p[4] = (s->qdev.conf.cyls >> 16) & 0xff;
        p[5] = (s->qdev.conf.cyls >> 8) & 0xff;
        p[6] = s->qdev.conf.cyls & 0xff;
        /* Reduced current start cylinder, disabled */
        p[7] = (s->qdev.conf.cyls >> 16) & 0xff;
        p[8] = (s->qdev.conf.cyls >> 8) & 0xff;
        p[9] = s->qdev.conf.cyls & 0xff;
        /* Device step rate [ns], 200ns */
        p[10] = 0;
        p[11] = 200;
        /* Landing zone cylinder */
        p[12] = 0xff;
        p[13] =  0xff;
        p[14] = 0xff;
        /* Medium rotation rate [rpm], 5400 rpm */
        p[18] = (5400 >> 8) & 0xff;
        p[19] = 5400 & 0xff;
        break;

    case MODE_PAGE_FLEXIBLE_DISK_GEOMETRY:
        length = 0x1e;
        if (page_control == 1) { /* Changeable Values */
            break;
        }
        /* Transfer rate [kbit/s], 5Mbit/s */
        p[0] = 5000 >> 8;
        p[1] = 5000 & 0xff;
        /* if a geometry hint is available, use it */
        p[2] = s->qdev.conf.heads & 0xff;
        p[3] = s->qdev.conf.secs & 0xff;
        p[4] = s->qdev.blocksize >> 8;
        p[6] = (s->qdev.conf.cyls >> 8) & 0xff;
        p[7] = s->qdev.conf.cyls & 0xff;
        /* Write precomp start cylinder, disabled */
        p[8] = (s->qdev.conf.cyls >> 8) & 0xff;
        p[9] = s->qdev.conf.cyls & 0xff;
        /* Reduced current start cylinder, disabled */
        p[10] = (s->qdev.conf.cyls >> 8) & 0xff;
        p[11] = s->qdev.conf.cyls & 0xff;
        /* Device step rate [100us], 100us */
        p[12] = 0;
        p[13] = 1;
        /* Device step pulse width [us], 1us */
        p[14] = 1;
        /* Device head settle delay [100us], 100us */
        p[15] = 0;
        p[16] = 1;
        /* Motor on delay [0.1s], 0.1s */
        p[17] = 1;
        /* Motor off delay [0.1s], 0.1s */
        p[18] = 1;
        /* Medium rotation rate [rpm], 5400 rpm */
        p[26] = (5400 >> 8) & 0xff;
        p[27] = 5400 & 0xff;
        break;

    case MODE_PAGE_CACHING:
        length = 0x12;
        if (page_control == 1 || /* Changeable Values */
            blk_enable_write_cache(s->qdev.conf.blk)) {
            p[0] = 4; /* WCE */
        }
        break;

    case MODE_PAGE_R_W_ERROR:
        length = 10;
        if (page_control == 1) { /* Changeable Values */
            if (s->qdev.type == TYPE_ROM) {
                /* Automatic Write Reallocation Enabled */
                p[0] = 0x80;
            }
            break;
        }
        p[0] = 0x80; /* Automatic Write Reallocation Enabled */
        if (s->qdev.type == TYPE_ROM) {
            p[1] = 0x20; /* Read Retry Count */
        }
        break;

    case MODE_PAGE_AUDIO_CTL:
        length = 14;
        break;

    case MODE_PAGE_CAPABILITIES:
        length = 0x14;
        if (page_control == 1) { /* Changeable Values */
            break;
        }

        p[0] = 0x3b; /* CD-R & CD-RW read */
        p[1] = 0; /* Writing not supported */
        p[2] = 0x7f; /* Audio, composite, digital out,
                        mode 2 form 1&2, multi session */
        p[3] = 0xff; /* CD DA, DA accurate, RW supported,
                        RW corrected, C2 errors, ISRC,
                        UPC, Bar code */
        p[4] = 0x2d | (s->tray_locked ? 2 : 0);
        /* Locking supported, jumper present, eject, tray */
        p[5] = 0; /* no volume & mute control, no
                     changer */
        p[6] = (50 * 176) >> 8; /* 50x read speed */
        p[7] = (50 * 176) & 0xff;
        p[8] = 2 >> 8; /* Two volume levels */
        p[9] = 2 & 0xff;
        p[10] = 2048 >> 8; /* 2M buffer */
        p[11] = 2048 & 0xff;
        p[12] = (16 * 176) >> 8; /* 16x read speed current */
        p[13] = (16 * 176) & 0xff;
        p[16] = (16 * 176) >> 8; /* 16x write speed */
        p[17] = (16 * 176) & 0xff;
        p[18] = (16 * 176) >> 8; /* 16x write speed current */
        p[19] = (16 * 176) & 0xff;
        break;

     case MODE_PAGE_APPLE_VENDOR:
        if (s->quirks & (1 << SCSI_DISK_QUIRK_MODE_PAGE_APPLE_VENDOR)) {
            length = 0x1e;
            if (page_control == 1) { /* Changeable Values */
                break;
            }

            memset(p, 0, length);
            strcpy((char *)p + 8, "APPLE COMPUTER, INC   ");
            break;
        } else {
            return -1;
        }

    case MODE_PAGE_VENDOR_SPECIFIC:
        if (s->qdev.type == TYPE_DISK && (s->quirks &
            (1 << SCSI_DISK_QUIRK_MODE_PAGE_VENDOR_SPECIFIC_APPLE))) {
            length = 0x2;
            if (page_control == 1) { /* Changeable Values */
                p[0] = 0xff;
                p[1] = 0xff;
                break;
            }
            p[0] = 0;
            p[1] = 0;
            break;
        } else {
            return -1;
        }

    default:
        return -1;
    }

    assert(length < 256);
    (*p_outbuf)[0] = page;
    (*p_outbuf)[1] = length;
    *p_outbuf += length + 2;
    return length + 2;
}

static int scsi_disk_emulate_mode_sense(SCSIDiskReq *r, uint8_t *outbuf)
{
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, r->req.dev);
    uint64_t nb_sectors;
    bool dbd;
    int page, buflen, ret, page_control;
    uint8_t *p;
    uint8_t dev_specific_param;

    dbd = (r->req.cmd.buf[1] & 0x8) != 0;
    page = r->req.cmd.buf[2] & 0x3f;
    page_control = (r->req.cmd.buf[2] & 0xc0) >> 6;

    trace_scsi_disk_emulate_mode_sense((r->req.cmd.buf[0] == MODE_SENSE) ? 6 :
                                       10, page, r->req.cmd.xfer, page_control);
    memset(outbuf, 0, r->req.cmd.xfer);
    p = outbuf;

    if (s->qdev.type == TYPE_DISK) {
        dev_specific_param = s->features & (1 << SCSI_DISK_F_DPOFUA) ? 0x10 : 0;
        if (!blk_is_writable(s->qdev.conf.blk)) {
            dev_specific_param |= 0x80; /* Readonly.  */
        }
    } else {
        if (s->quirks & (1 << SCSI_DISK_QUIRK_MODE_SENSE_ROM_USE_DBD)) {
            /* Use DBD from the request... */
            dev_specific_param = 0x00;

            /*
             * ... unless we receive a request for MODE_PAGE_APPLE_VENDOR
             * which should never return a block descriptor even though DBD is
             * not set, otherwise CDROM detection fails in MacOS
             */
            if (s->quirks & (1 << SCSI_DISK_QUIRK_MODE_PAGE_APPLE_VENDOR) &&
                page == MODE_PAGE_APPLE_VENDOR) {
                dbd = true;
            }
        } else {
            /*
             * MMC prescribes that CD/DVD drives have no block descriptors,
             * and defines no device-specific parameter.
             */
            dev_specific_param = 0x00;
            dbd = true;
        }
    }

    if (r->req.cmd.buf[0] == MODE_SENSE) {
        p[1] = 0; /* Default media type.  */
        p[2] = dev_specific_param;
        p[3] = 0; /* Block descriptor length.  */
        p += 4;
    } else { /* MODE_SENSE_10 */
        p[2] = 0; /* Default media type.  */
        p[3] = dev_specific_param;
        p[6] = p[7] = 0; /* Block descriptor length.  */
        p += 8;
    }

    blk_get_geometry(s->qdev.conf.blk, &nb_sectors);
    if (!dbd && nb_sectors) {
        if (r->req.cmd.buf[0] == MODE_SENSE) {
            outbuf[3] = 8; /* Block descriptor length  */
        } else { /* MODE_SENSE_10 */
            outbuf[7] = 8; /* Block descriptor length  */
        }
        nb_sectors /= (s->qdev.blocksize / BDRV_SECTOR_SIZE);
        if (nb_sectors > 0xffffff) {
            nb_sectors = 0;
        }
        p[0] = 0; /* media density code */
        p[1] = (nb_sectors >> 16) & 0xff;
        p[2] = (nb_sectors >> 8) & 0xff;
        p[3] = nb_sectors & 0xff;
        p[4] = 0; /* reserved */
        p[5] = 0; /* bytes 5-7 are the sector size in bytes */
        p[6] = s->qdev.blocksize >> 8;
        p[7] = 0;
        p += 8;
    }

    if (page_control == 3) {
        /* Saved Values */
        scsi_check_condition(r, SENSE_CODE(SAVING_PARAMS_NOT_SUPPORTED));
        return -1;
    }

    if (page == 0x3f) {
        for (page = 0; page <= 0x3e; page++) {
            mode_sense_page(s, page, &p, page_control);
        }
    } else {
        ret = mode_sense_page(s, page, &p, page_control);
        if (ret == -1) {
            return -1;
        }
    }

    buflen = p - outbuf;
    /*
     * The mode data length field specifies the length in bytes of the
     * following data that is available to be transferred. The mode data
     * length does not include itself.
     */
    if (r->req.cmd.buf[0] == MODE_SENSE) {
        outbuf[0] = buflen - 1;
    } else { /* MODE_SENSE_10 */
        outbuf[0] = ((buflen - 2) >> 8) & 0xff;
        outbuf[1] = (buflen - 2) & 0xff;
    }
    return buflen;
}

static int scsi_disk_emulate_read_toc(SCSIRequest *req, uint8_t *outbuf)
{
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, req->dev);
    int start_track, format, msf, toclen;
    uint64_t nb_sectors;

    msf = req->cmd.buf[1] & 2;
    format = req->cmd.buf[2] & 0xf;
    start_track = req->cmd.buf[6];
    blk_get_geometry(s->qdev.conf.blk, &nb_sectors);
    trace_scsi_disk_emulate_read_toc(start_track, format, msf >> 1);
    nb_sectors /= s->qdev.blocksize / BDRV_SECTOR_SIZE;
    switch (format) {
    case 0:
        toclen = cdrom_read_toc(nb_sectors, outbuf, msf, start_track);
        break;
    case 1:
        /* multi session : only a single session defined */
        toclen = 12;
        memset(outbuf, 0, 12);
        outbuf[1] = 0x0a;
        outbuf[2] = 0x01;
        outbuf[3] = 0x01;
        break;
    case 2:
        toclen = cdrom_read_toc_raw(nb_sectors, outbuf, msf, start_track);
        break;
    default:
        return -1;
    }
    return toclen;
}

static int scsi_disk_emulate_start_stop(SCSIDiskReq *r)
{
    SCSIRequest *req = &r->req;
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, req->dev);
    bool start = req->cmd.buf[4] & 1;
    bool loej = req->cmd.buf[4] & 2; /* load on start, eject on !start */
    int pwrcnd = req->cmd.buf[4] & 0xf0;

    if (pwrcnd) {
        /* eject/load only happens for power condition == 0 */
        return 0;
    }

    if ((s->features & (1 << SCSI_DISK_F_REMOVABLE)) && loej) {
        if (!start && !s->tray_open && s->tray_locked) {
            scsi_check_condition(r,
                                 blk_is_inserted(s->qdev.conf.blk)
                                 ? SENSE_CODE(ILLEGAL_REQ_REMOVAL_PREVENTED)
                                 : SENSE_CODE(NOT_READY_REMOVAL_PREVENTED));
            return -1;
        }

        if (s->tray_open != !start) {
            blk_eject(s->qdev.conf.blk, !start);
            s->tray_open = !start;
        }
    }
    return 0;
}

static void scsi_disk_emulate_read_data(SCSIRequest *req)
{
    SCSIDiskReq *r = DO_UPCAST(SCSIDiskReq, req, req);
    int buflen = r->iov.iov_len;

    if (buflen) {
        trace_scsi_disk_emulate_read_data(buflen);
        r->iov.iov_len = 0;
        r->started = true;
        scsi_req_data(&r->req, buflen);
        return;
    }

    /* This also clears the sense buffer for REQUEST SENSE.  */
    scsi_req_complete(&r->req, GOOD);
}

static int scsi_disk_check_mode_select(SCSIDiskState *s, int page,
                                       uint8_t *inbuf, int inlen)
{
    uint8_t mode_current[SCSI_MAX_MODE_LEN];
    uint8_t mode_changeable[SCSI_MAX_MODE_LEN];
    uint8_t *p;
    int len, expected_len, changeable_len, i;

    /* The input buffer does not include the page header, so it is
     * off by 2 bytes.
     */
    expected_len = inlen + 2;
    if (expected_len > SCSI_MAX_MODE_LEN) {
        return -1;
    }

    /* MODE_PAGE_ALLS is only valid for MODE SENSE commands */
    if (page == MODE_PAGE_ALLS) {
        return -1;
    }

    p = mode_current;
    memset(mode_current, 0, inlen + 2);
    len = mode_sense_page(s, page, &p, 0);
    if (len < 0 || len != expected_len) {
        return -1;
    }

    p = mode_changeable;
    memset(mode_changeable, 0, inlen + 2);
    changeable_len = mode_sense_page(s, page, &p, 1);
    assert(changeable_len == len);

    /* Check that unchangeable bits are the same as what MODE SENSE
     * would return.
     */
    for (i = 2; i < len; i++) {
        if (((mode_current[i] ^ inbuf[i - 2]) & ~mode_changeable[i]) != 0) {
            return -1;
        }
    }
    return 0;
}

static void scsi_disk_apply_mode_select(SCSIDiskState *s, int page, uint8_t *p)
{
    switch (page) {
    case MODE_PAGE_CACHING:
        blk_set_enable_write_cache(s->qdev.conf.blk, (p[0] & 4) != 0);
        break;

    default:
        break;
    }
}

static int mode_select_pages(SCSIDiskReq *r, uint8_t *p, int len, bool change)
{
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, r->req.dev);

    while (len > 0) {
        int page, subpage, page_len;

        /* Parse both possible formats for the mode page headers.  */
        page = p[0] & 0x3f;
        if (p[0] & 0x40) {
            if (len < 4) {
                goto invalid_param_len;
            }
            subpage = p[1];
            page_len = lduw_be_p(&p[2]);
            p += 4;
            len -= 4;
        } else {
            if (len < 2) {
                goto invalid_param_len;
            }
            subpage = 0;
            page_len = p[1];
            p += 2;
            len -= 2;
        }

        if (subpage) {
            goto invalid_param;
        }
        if (page_len > len) {
            if (!(s->quirks & SCSI_DISK_QUIRK_MODE_PAGE_TRUNCATED)) {
                goto invalid_param_len;
            }
            trace_scsi_disk_mode_select_page_truncated(page, page_len, len);
        }

        if (!change) {
            if (scsi_disk_check_mode_select(s, page, p, page_len) < 0) {
                goto invalid_param;
            }
        } else {
            scsi_disk_apply_mode_select(s, page, p);
        }

        p += page_len;
        len -= page_len;
    }
    return 0;

invalid_param:
    scsi_check_condition(r, SENSE_CODE(INVALID_PARAM));
    return -1;

invalid_param_len:
    scsi_check_condition(r, SENSE_CODE(INVALID_PARAM_LEN));
    return -1;
}

static void scsi_disk_emulate_mode_select(SCSIDiskReq *r, uint8_t *inbuf)
{
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, r->req.dev);
    uint8_t *p = inbuf;
    int cmd = r->req.cmd.buf[0];
    int len = r->req.cmd.xfer;
    int hdr_len = (cmd == MODE_SELECT ? 4 : 8);
    int bd_len, bs;
    int pass;

    if ((r->req.cmd.buf[1] & 0x11) != 0x10) {
        if (!(s->quirks &
            (1 << SCSI_DISK_QUIRK_MODE_PAGE_VENDOR_SPECIFIC_APPLE))) {
            /* We only support PF=1, SP=0.  */
            goto invalid_field;
        }
    }

    if (len < hdr_len) {
        goto invalid_param_len;
    }

    bd_len = (cmd == MODE_SELECT ? p[3] : lduw_be_p(&p[6]));
    len -= hdr_len;
    p += hdr_len;
    if (len < bd_len) {
        goto invalid_param_len;
    }
    if (bd_len != 0 && bd_len != 8) {
        goto invalid_param;
    }

    /* Allow changing the block size */
    if (bd_len) {
        bs = p[5] << 16 | p[6] << 8 | p[7];

        /*
         * Since the existing code only checks/updates bits 8-15 of the block
         * size, restrict ourselves to the same requirement for now to ensure
         * that a block size set by a block descriptor and then read back by
         * a subsequent SCSI command will be the same
         */
        if (bs && !(bs & ~0xff00) && bs != s->qdev.blocksize) {
            s->qdev.blocksize = bs;
            trace_scsi_disk_mode_select_set_blocksize(s->qdev.blocksize);
        }
    }

    len -= bd_len;
    p += bd_len;

    /* Ensure no change is made if there is an error!  */
    for (pass = 0; pass < 2; pass++) {
        if (mode_select_pages(r, p, len, pass == 1) < 0) {
            assert(pass == 0);
            return;
        }
    }
    if (!blk_enable_write_cache(s->qdev.conf.blk)) {
        /* The request is used as the AIO opaque value, so add a ref.  */
        scsi_req_ref(&r->req);
        block_acct_start(blk_get_stats(s->qdev.conf.blk), &r->acct, 0,
                         BLOCK_ACCT_FLUSH);
        r->req.aiocb = blk_aio_flush(s->qdev.conf.blk, scsi_aio_complete, r);
        return;
    }

    scsi_req_complete(&r->req, GOOD);
    return;

invalid_param:
    scsi_check_condition(r, SENSE_CODE(INVALID_PARAM));
    return;

invalid_param_len:
    scsi_check_condition(r, SENSE_CODE(INVALID_PARAM_LEN));
    return;

invalid_field:
    scsi_check_condition(r, SENSE_CODE(INVALID_FIELD));
}

/* sector_num and nb_sectors expected to be in qdev blocksize */
static inline bool check_lba_range(SCSIDiskState *s,
                                   uint64_t sector_num, uint32_t nb_sectors)
{
    /*
     * The first line tests that no overflow happens when computing the last
     * sector.  The second line tests that the last accessed sector is in
     * range.
     *
     * Careful, the computations should not underflow for nb_sectors == 0,
     * and a 0-block read to the first LBA beyond the end of device is
     * valid.
     */
    return (sector_num <= sector_num + nb_sectors &&
            sector_num + nb_sectors <= s->qdev.max_lba + 1);
}

typedef struct UnmapCBData {
    SCSIDiskReq *r;
    uint8_t *inbuf;
    int count;
} UnmapCBData;

static void scsi_unmap_complete(void *opaque, int ret);

static void scsi_unmap_complete_noio(UnmapCBData *data, int ret)
{
    SCSIDiskReq *r = data->r;
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, r->req.dev);

    assert(r->req.aiocb == NULL);

    if (data->count > 0) {
        uint64_t sector_num = ldq_be_p(&data->inbuf[0]);
        uint32_t nb_sectors = ldl_be_p(&data->inbuf[8]) & 0xffffffffULL;
        r->sector = sector_num * (s->qdev.blocksize / BDRV_SECTOR_SIZE);
        r->sector_count = nb_sectors * (s->qdev.blocksize / BDRV_SECTOR_SIZE);

        if (!check_lba_range(s, sector_num, nb_sectors)) {
            block_acct_invalid(blk_get_stats(s->qdev.conf.blk),
                               BLOCK_ACCT_UNMAP);
            scsi_check_condition(r, SENSE_CODE(LBA_OUT_OF_RANGE));
            goto done;
        }

        block_acct_start(blk_get_stats(s->qdev.conf.blk), &r->acct,
                         r->sector_count * BDRV_SECTOR_SIZE,
                         BLOCK_ACCT_UNMAP);

        r->req.aiocb = blk_aio_pdiscard(s->qdev.conf.blk,
                                        r->sector * BDRV_SECTOR_SIZE,
                                        r->sector_count * BDRV_SECTOR_SIZE,
                                        scsi_unmap_complete, data);
        data->count--;
        data->inbuf += 16;
        return;
    }

    scsi_req_complete(&r->req, GOOD);

done:
    scsi_req_unref(&r->req);
    g_free(data);
}

static void scsi_unmap_complete(void *opaque, int ret)
{
    UnmapCBData *data = opaque;
    SCSIDiskReq *r = data->r;
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, r->req.dev);

    assert(r->req.aiocb != NULL);
    r->req.aiocb = NULL;

    aio_context_acquire(blk_get_aio_context(s->qdev.conf.blk));
    if (scsi_disk_req_check_error(r, ret, true)) {
        scsi_req_unref(&r->req);
        g_free(data);
    } else {
        block_acct_done(blk_get_stats(s->qdev.conf.blk), &r->acct);
        scsi_unmap_complete_noio(data, ret);
    }
    aio_context_release(blk_get_aio_context(s->qdev.conf.blk));
}

static void scsi_disk_emulate_unmap(SCSIDiskReq *r, uint8_t *inbuf)
{
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, r->req.dev);
    uint8_t *p = inbuf;
    int len = r->req.cmd.xfer;
    UnmapCBData *data;

    /* Reject ANCHOR=1.  */
    if (r->req.cmd.buf[1] & 0x1) {
        goto invalid_field;
    }

    if (len < 8) {
        goto invalid_param_len;
    }
    if (len < lduw_be_p(&p[0]) + 2) {
        goto invalid_param_len;
    }
    if (len < lduw_be_p(&p[2]) + 8) {
        goto invalid_param_len;
    }
    if (lduw_be_p(&p[2]) & 15) {
        goto invalid_param_len;
    }

    if (!blk_is_writable(s->qdev.conf.blk)) {
        block_acct_invalid(blk_get_stats(s->qdev.conf.blk), BLOCK_ACCT_UNMAP);
        scsi_check_condition(r, SENSE_CODE(WRITE_PROTECTED));
        return;
    }

    data = g_new0(UnmapCBData, 1);
    data->r = r;
    data->inbuf = &p[8];
    data->count = lduw_be_p(&p[2]) >> 4;

    /* The matching unref is in scsi_unmap_complete, before data is freed.  */
    scsi_req_ref(&r->req);
    scsi_unmap_complete_noio(data, 0);
    return;

invalid_param_len:
    block_acct_invalid(blk_get_stats(s->qdev.conf.blk), BLOCK_ACCT_UNMAP);
    scsi_check_condition(r, SENSE_CODE(INVALID_PARAM_LEN));
    return;

invalid_field:
    block_acct_invalid(blk_get_stats(s->qdev.conf.blk), BLOCK_ACCT_UNMAP);
    scsi_check_condition(r, SENSE_CODE(INVALID_FIELD));
}

typedef struct WriteSameCBData {
    SCSIDiskReq *r;
    int64_t sector;
    int nb_sectors;
    QEMUIOVector qiov;
    struct iovec iov;
} WriteSameCBData;

static void scsi_write_same_complete(void *opaque, int ret)
{
    WriteSameCBData *data = opaque;
    SCSIDiskReq *r = data->r;
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, r->req.dev);

    assert(r->req.aiocb != NULL);
    r->req.aiocb = NULL;
    aio_context_acquire(blk_get_aio_context(s->qdev.conf.blk));
    if (scsi_disk_req_check_error(r, ret, true)) {
        goto done;
    }

    block_acct_done(blk_get_stats(s->qdev.conf.blk), &r->acct);

    data->nb_sectors -= data->iov.iov_len / BDRV_SECTOR_SIZE;
    data->sector += data->iov.iov_len / BDRV_SECTOR_SIZE;
    data->iov.iov_len = MIN(data->nb_sectors * BDRV_SECTOR_SIZE,
                            data->iov.iov_len);
    if (data->iov.iov_len) {
        block_acct_start(blk_get_stats(s->qdev.conf.blk), &r->acct,
                         data->iov.iov_len, BLOCK_ACCT_WRITE);
        /* Reinitialize qiov, to handle unaligned WRITE SAME request
         * where final qiov may need smaller size */
        qemu_iovec_init_external(&data->qiov, &data->iov, 1);
        r->req.aiocb = blk_aio_pwritev(s->qdev.conf.blk,
                                       data->sector << BDRV_SECTOR_BITS,
                                       &data->qiov, 0,
                                       scsi_write_same_complete, data);
        aio_context_release(blk_get_aio_context(s->qdev.conf.blk));
        return;
    }

    scsi_req_complete(&r->req, GOOD);

done:
    scsi_req_unref(&r->req);
    qemu_vfree(data->iov.iov_base);
    g_free(data);
    aio_context_release(blk_get_aio_context(s->qdev.conf.blk));
}

static void scsi_disk_emulate_write_same(SCSIDiskReq *r, uint8_t *inbuf)
{
    SCSIRequest *req = &r->req;
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, req->dev);
    uint32_t nb_sectors = scsi_data_cdb_xfer(r->req.cmd.buf);
    WriteSameCBData *data;
    uint8_t *buf;
    int i, l;

    /* Fail if PBDATA=1 or LBDATA=1 or ANCHOR=1.  */
    if (nb_sectors == 0 || (req->cmd.buf[1] & 0x16)) {
        scsi_check_condition(r, SENSE_CODE(INVALID_FIELD));
        return;
    }

    if (!blk_is_writable(s->qdev.conf.blk)) {
        scsi_check_condition(r, SENSE_CODE(WRITE_PROTECTED));
        return;
    }
    if (!check_lba_range(s, r->req.cmd.lba, nb_sectors)) {
        scsi_check_condition(r, SENSE_CODE(LBA_OUT_OF_RANGE));
        return;
    }

    if ((req->cmd.buf[1] & 0x1) || buffer_is_zero(inbuf, s->qdev.blocksize)) {
        int flags = (req->cmd.buf[1] & 0x8) ? BDRV_REQ_MAY_UNMAP : 0;

        /* The request is used as the AIO opaque value, so add a ref.  */
        scsi_req_ref(&r->req);
        block_acct_start(blk_get_stats(s->qdev.conf.blk), &r->acct,
                         nb_sectors * s->qdev.blocksize,
                        BLOCK_ACCT_WRITE);
        r->req.aiocb = blk_aio_pwrite_zeroes(s->qdev.conf.blk,
                                r->req.cmd.lba * s->qdev.blocksize,
                                nb_sectors * s->qdev.blocksize,
                                flags, scsi_aio_complete, r);
        return;
    }

    data = g_new0(WriteSameCBData, 1);
    data->r = r;
    data->sector = r->req.cmd.lba * (s->qdev.blocksize / BDRV_SECTOR_SIZE);
    data->nb_sectors = nb_sectors * (s->qdev.blocksize / BDRV_SECTOR_SIZE);
    data->iov.iov_len = MIN(data->nb_sectors * BDRV_SECTOR_SIZE,
                            SCSI_WRITE_SAME_MAX);
    data->iov.iov_base = buf = blk_blockalign(s->qdev.conf.blk,
                                              data->iov.iov_len);
    qemu_iovec_init_external(&data->qiov, &data->iov, 1);

    for (i = 0; i < data->iov.iov_len; i += l) {
        l = MIN(s->qdev.blocksize, data->iov.iov_len - i);
        memcpy(&buf[i], inbuf, l);
    }

    scsi_req_ref(&r->req);
    block_acct_start(blk_get_stats(s->qdev.conf.blk), &r->acct,
                     data->iov.iov_len, BLOCK_ACCT_WRITE);
    r->req.aiocb = blk_aio_pwritev(s->qdev.conf.blk,
                                   data->sector << BDRV_SECTOR_BITS,
                                   &data->qiov, 0,
                                   scsi_write_same_complete, data);
}

static void scsi_disk_emulate_write_data(SCSIRequest *req)
{
    SCSIDiskReq *r = DO_UPCAST(SCSIDiskReq, req, req);

    if (r->iov.iov_len) {
        int buflen = r->iov.iov_len;
        trace_scsi_disk_emulate_write_data(buflen);
        r->iov.iov_len = 0;
        scsi_req_data(&r->req, buflen);
        return;
    }

    switch (req->cmd.buf[0]) {
    case MODE_SELECT:
    case MODE_SELECT_10:
        /* This also clears the sense buffer for REQUEST SENSE.  */
        scsi_disk_emulate_mode_select(r, r->iov.iov_base);
        break;

    case UNMAP:
        scsi_disk_emulate_unmap(r, r->iov.iov_base);
        break;

    case VERIFY_10:
    case VERIFY_12:
    case VERIFY_16:
        if (r->req.status == -1) {
            scsi_check_condition(r, SENSE_CODE(INVALID_FIELD));
        }
        break;

    case WRITE_SAME_10:
    case WRITE_SAME_16:
        scsi_disk_emulate_write_same(r, r->iov.iov_base);
        break;

    default:
        abort();
    }
}

static int32_t scsi_disk_emulate_command(SCSIRequest *req, uint8_t *buf)
{
    SCSIDiskReq *r = DO_UPCAST(SCSIDiskReq, req, req);
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, req->dev);
    uint64_t nb_sectors;
    uint8_t *outbuf;
    int buflen;

    switch (req->cmd.buf[0]) {
    case INQUIRY:
    case MODE_SENSE:
    case MODE_SENSE_10:
    case RESERVE:
    case RESERVE_10:
    case RELEASE:
    case RELEASE_10:
    case START_STOP:
    case ALLOW_MEDIUM_REMOVAL:
    case GET_CONFIGURATION:
    case GET_EVENT_STATUS_NOTIFICATION:
    case MECHANISM_STATUS:
    case REQUEST_SENSE:
        break;

    default:
        if (!blk_is_available(s->qdev.conf.blk)) {
            scsi_check_condition(r, SENSE_CODE(NO_MEDIUM));
            return 0;
        }
        break;
    }

    /*
     * FIXME: we shouldn't return anything bigger than 4k, but the code
     * requires the buffer to be as big as req->cmd.xfer in several
     * places.  So, do not allow CDBs with a very large ALLOCATION
     * LENGTH.  The real fix would be to modify scsi_read_data and
     * dma_buf_read, so that they return data beyond the buflen
     * as all zeros.
     */
    if (req->cmd.xfer > 65536) {
        goto illegal_request;
    }
    r->buflen = MAX(4096, req->cmd.xfer);

    if (!r->iov.iov_base) {
        r->iov.iov_base = blk_blockalign(s->qdev.conf.blk, r->buflen);
    }

    outbuf = r->iov.iov_base;
    memset(outbuf, 0, r->buflen);
    switch (req->cmd.buf[0]) {
    case TEST_UNIT_READY:
        assert(blk_is_available(s->qdev.conf.blk));
        break;
    case INQUIRY:
        buflen = scsi_disk_emulate_inquiry(req, outbuf);
        if (buflen < 0) {
            goto illegal_request;
        }
        break;
    case MODE_SENSE:
    case MODE_SENSE_10:
        buflen = scsi_disk_emulate_mode_sense(r, outbuf);
        if (buflen < 0) {
            goto illegal_request;
        }
        break;
    case READ_TOC:
        buflen = scsi_disk_emulate_read_toc(req, outbuf);
        if (buflen < 0) {
            goto illegal_request;
        }
        break;
    case RESERVE:
        if (req->cmd.buf[1] & 1) {
            goto illegal_request;
        }
        break;
    case RESERVE_10:
        if (req->cmd.buf[1] & 3) {
            goto illegal_request;
        }
        break;
    case RELEASE:
        if (req->cmd.buf[1] & 1) {
            goto illegal_request;
        }
        break;
    case RELEASE_10:
        if (req->cmd.buf[1] & 3) {
            goto illegal_request;
        }
        break;
    case START_STOP:
        if (scsi_disk_emulate_start_stop(r) < 0) {
            return 0;
        }
        break;
    case ALLOW_MEDIUM_REMOVAL:
        s->tray_locked = req->cmd.buf[4] & 1;
        blk_lock_medium(s->qdev.conf.blk, req->cmd.buf[4] & 1);
        break;
    case READ_CAPACITY_10:
        /* The normal LEN field for this command is zero.  */
        memset(outbuf, 0, 8);
        blk_get_geometry(s->qdev.conf.blk, &nb_sectors);
        if (!nb_sectors) {
            scsi_check_condition(r, SENSE_CODE(LUN_NOT_READY));
            return 0;
        }
        if ((req->cmd.buf[8] & 1) == 0 && req->cmd.lba) {
            goto illegal_request;
        }
        nb_sectors /= s->qdev.blocksize / BDRV_SECTOR_SIZE;
        /* Returned value is the address of the last sector.  */
        nb_sectors--;
        /* Remember the new size for read/write sanity checking. */
        s->qdev.max_lba = nb_sectors;
        /* Clip to 2TB, instead of returning capacity modulo 2TB. */
        if (nb_sectors > UINT32_MAX) {
            nb_sectors = UINT32_MAX;
        }
        outbuf[0] = (nb_sectors >> 24) & 0xff;
        outbuf[1] = (nb_sectors >> 16) & 0xff;
        outbuf[2] = (nb_sectors >> 8) & 0xff;
        outbuf[3] = nb_sectors & 0xff;
        outbuf[4] = 0;
        outbuf[5] = 0;
        outbuf[6] = s->qdev.blocksize >> 8;
        outbuf[7] = 0;
        break;
    case REQUEST_SENSE:
        /* Just return "NO SENSE".  */
        buflen = scsi_convert_sense(NULL, 0, outbuf, r->buflen,
                                    (req->cmd.buf[1] & 1) == 0);
        if (buflen < 0) {
            goto illegal_request;
        }
        break;
    case MECHANISM_STATUS:
        buflen = scsi_emulate_mechanism_status(s, outbuf);
        if (buflen < 0) {
            goto illegal_request;
        }
        break;
    case GET_CONFIGURATION:
        buflen = scsi_get_configuration(s, outbuf);
        if (buflen < 0) {
            goto illegal_request;
        }
        break;
    case GET_EVENT_STATUS_NOTIFICATION:
        buflen = scsi_get_event_status_notification(s, r, outbuf);
        if (buflen < 0) {
            goto illegal_request;
        }
        break;
    case READ_DISC_INFORMATION:
        buflen = scsi_read_disc_information(s, r, outbuf);
        if (buflen < 0) {
            goto illegal_request;
        }
        break;
    case READ_DVD_STRUCTURE:
        buflen = scsi_read_dvd_structure(s, r, outbuf);
        if (buflen < 0) {
            goto illegal_request;
        }
        break;
    case SERVICE_ACTION_IN_16:
        /* Service Action In subcommands. */
        if ((req->cmd.buf[1] & 31) == SAI_READ_CAPACITY_16) {
            trace_scsi_disk_emulate_command_SAI_16();
            memset(outbuf, 0, req->cmd.xfer);
            blk_get_geometry(s->qdev.conf.blk, &nb_sectors);
            if (!nb_sectors) {
                scsi_check_condition(r, SENSE_CODE(LUN_NOT_READY));
                return 0;
            }
            if ((req->cmd.buf[14] & 1) == 0 && req->cmd.lba) {
                goto illegal_request;
            }
            nb_sectors /= s->qdev.blocksize / BDRV_SECTOR_SIZE;
            /* Returned value is the address of the last sector.  */
            nb_sectors--;
            /* Remember the new size for read/write sanity checking. */
            s->qdev.max_lba = nb_sectors;
            outbuf[0] = (nb_sectors >> 56) & 0xff;
            outbuf[1] = (nb_sectors >> 48) & 0xff;
            outbuf[2] = (nb_sectors >> 40) & 0xff;
            outbuf[3] = (nb_sectors >> 32) & 0xff;
            outbuf[4] = (nb_sectors >> 24) & 0xff;
            outbuf[5] = (nb_sectors >> 16) & 0xff;
            outbuf[6] = (nb_sectors >> 8) & 0xff;
            outbuf[7] = nb_sectors & 0xff;
            outbuf[8] = 0;
            outbuf[9] = 0;
            outbuf[10] = s->qdev.blocksize >> 8;
            outbuf[11] = 0;
            outbuf[12] = 0;
            outbuf[13] = get_physical_block_exp(&s->qdev.conf);

            /* set TPE bit if the format supports discard */
            if (s->qdev.conf.discard_granularity) {
                outbuf[14] = 0x80;
            }

            /* Protection, exponent and lowest lba field left blank. */
            break;
        }
        trace_scsi_disk_emulate_command_SAI_unsupported();
        goto illegal_request;
    case SYNCHRONIZE_CACHE:
        /* The request is used as the AIO opaque value, so add a ref.  */
        scsi_req_ref(&r->req);
        block_acct_start(blk_get_stats(s->qdev.conf.blk), &r->acct, 0,
                         BLOCK_ACCT_FLUSH);
        r->req.aiocb = blk_aio_flush(s->qdev.conf.blk, scsi_aio_complete, r);
        return 0;
    case SEEK_10:
        trace_scsi_disk_emulate_command_SEEK_10(r->req.cmd.lba);
        if (r->req.cmd.lba > s->qdev.max_lba) {
            goto illegal_lba;
        }
        break;
    case MODE_SELECT:
        trace_scsi_disk_emulate_command_MODE_SELECT(r->req.cmd.xfer);
        break;
    case MODE_SELECT_10:
        trace_scsi_disk_emulate_command_MODE_SELECT_10(r->req.cmd.xfer);
        break;
    case UNMAP:
        trace_scsi_disk_emulate_command_UNMAP(r->req.cmd.xfer);
        break;
    case VERIFY_10:
    case VERIFY_12:
    case VERIFY_16:
        trace_scsi_disk_emulate_command_VERIFY((req->cmd.buf[1] >> 1) & 3);
        if (req->cmd.buf[1] & 6) {
            goto illegal_request;
        }
        break;
    case WRITE_SAME_10:
    case WRITE_SAME_16:
        trace_scsi_disk_emulate_command_WRITE_SAME(
                req->cmd.buf[0] == WRITE_SAME_10 ? 10 : 16, r->req.cmd.xfer);
        break;
    case FORMAT_UNIT:
        trace_scsi_disk_emulate_command_FORMAT_UNIT(r->req.cmd.xfer);
        break;
    default:
        trace_scsi_disk_emulate_command_UNKNOWN(buf[0],
                                                scsi_command_name(buf[0]));
        scsi_check_condition(r, SENSE_CODE(INVALID_OPCODE));
        return 0;
    }
    assert(!r->req.aiocb);
    r->iov.iov_len = MIN(r->buflen, req->cmd.xfer);
    if (r->iov.iov_len == 0) {
        scsi_req_complete(&r->req, GOOD);
    }
    if (r->req.cmd.mode == SCSI_XFER_TO_DEV) {
        assert(r->iov.iov_len == req->cmd.xfer);
        return -r->iov.iov_len;
    } else {
        return r->iov.iov_len;
    }

illegal_request:
    if (r->req.status == -1) {
        scsi_check_condition(r, SENSE_CODE(INVALID_FIELD));
    }
    return 0;

illegal_lba:
    scsi_check_condition(r, SENSE_CODE(LBA_OUT_OF_RANGE));
    return 0;
}

/* Execute a scsi command.  Returns the length of the data expected by the
   command.  This will be Positive for data transfers from the device
   (eg. disk reads), negative for transfers to the device (eg. disk writes),
   and zero if the command does not transfer any data.  */

static int32_t scsi_disk_dma_command(SCSIRequest *req, uint8_t *buf)
{
    SCSIDiskReq *r = DO_UPCAST(SCSIDiskReq, req, req);
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, req->dev);
    SCSIDiskClass *sdc = (SCSIDiskClass *) object_get_class(OBJECT(s));
    uint32_t len;
    uint8_t command;

    command = buf[0];

    if (!blk_is_available(s->qdev.conf.blk)) {
        scsi_check_condition(r, SENSE_CODE(NO_MEDIUM));
        return 0;
    }

    len = scsi_data_cdb_xfer(r->req.cmd.buf);
    switch (command) {
    case READ_6:
    case READ_10:
    case READ_12:
    case READ_16:
        trace_scsi_disk_dma_command_READ(r->req.cmd.lba, len);
        /* Protection information is not supported.  For SCSI versions 2 and
         * older (as determined by snooping the guest's INQUIRY commands),
         * there is no RD/WR/VRPROTECT, so skip this check in these versions.
         */
        if (s->qdev.scsi_version > 2 && (r->req.cmd.buf[1] & 0xe0)) {
            goto illegal_request;
        }
        if (!check_lba_range(s, r->req.cmd.lba, len)) {
            goto illegal_lba;
        }
        r->sector = r->req.cmd.lba * (s->qdev.blocksize / BDRV_SECTOR_SIZE);
        r->sector_count = len * (s->qdev.blocksize / BDRV_SECTOR_SIZE);
        break;
    case WRITE_6:
    case WRITE_10:
    case WRITE_12:
    case WRITE_16:
    case WRITE_VERIFY_10:
    case WRITE_VERIFY_12:
    case WRITE_VERIFY_16:
        if (!blk_is_writable(s->qdev.conf.blk)) {
            scsi_check_condition(r, SENSE_CODE(WRITE_PROTECTED));
            return 0;
        }
        trace_scsi_disk_dma_command_WRITE(
                (command & 0xe) == 0xe ? "And Verify " : "",
                r->req.cmd.lba, len);
        /* fall through */
    case VERIFY_10:
    case VERIFY_12:
    case VERIFY_16:
        /* We get here only for BYTCHK == 0x01 and only for scsi-block.
         * As far as DMA is concerned, we can treat it the same as a write;
         * scsi_block_do_sgio will send VERIFY commands.
         */
        if (s->qdev.scsi_version > 2 && (r->req.cmd.buf[1] & 0xe0)) {
            goto illegal_request;
        }
        if (!check_lba_range(s, r->req.cmd.lba, len)) {
            goto illegal_lba;
        }
        r->sector = r->req.cmd.lba * (s->qdev.blocksize / BDRV_SECTOR_SIZE);
        r->sector_count = len * (s->qdev.blocksize / BDRV_SECTOR_SIZE);
        break;
    default:
        abort();
    illegal_request:
        scsi_check_condition(r, SENSE_CODE(INVALID_FIELD));
        return 0;
    illegal_lba:
        scsi_check_condition(r, SENSE_CODE(LBA_OUT_OF_RANGE));
        return 0;
    }
    r->need_fua_emulation = sdc->need_fua_emulation(&r->req.cmd);
    if (r->sector_count == 0) {
        scsi_req_complete(&r->req, GOOD);
    }
    assert(r->iov.iov_len == 0);
    if (r->req.cmd.mode == SCSI_XFER_TO_DEV) {
        return -r->sector_count * BDRV_SECTOR_SIZE;
    } else {
        return r->sector_count * BDRV_SECTOR_SIZE;
    }
}

static void scsi_disk_reset(DeviceState *dev)
{
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev.qdev, dev);
    uint64_t nb_sectors;
    AioContext *ctx;

    scsi_device_purge_requests(&s->qdev, SENSE_CODE(RESET));

    ctx = blk_get_aio_context(s->qdev.conf.blk);
    aio_context_acquire(ctx);
    blk_get_geometry(s->qdev.conf.blk, &nb_sectors);
    aio_context_release(ctx);

    nb_sectors /= s->qdev.blocksize / BDRV_SECTOR_SIZE;
    if (nb_sectors) {
        nb_sectors--;
    }
    s->qdev.max_lba = nb_sectors;
    /* reset tray statuses */
    s->tray_locked = 0;
    s->tray_open = 0;

    s->qdev.scsi_version = s->qdev.default_scsi_version;
}

static void scsi_disk_resize_cb(void *opaque)
{
    SCSIDiskState *s = opaque;

    /* SPC lists this sense code as available only for
     * direct-access devices.
     */
    if (s->qdev.type == TYPE_DISK) {
        scsi_device_report_change(&s->qdev, SENSE_CODE(CAPACITY_CHANGED));
    }
}

static void scsi_cd_change_media_cb(void *opaque, bool load, Error **errp)
{
    SCSIDiskState *s = opaque;

    /*
     * When a CD gets changed, we have to report an ejected state and
     * then a loaded state to guests so that they detect tray
     * open/close and media change events.  Guests that do not use
     * GET_EVENT_STATUS_NOTIFICATION to detect such tray open/close
     * states rely on this behavior.
     *
     * media_changed governs the state machine used for unit attention
     * report.  media_event is used by GET EVENT STATUS NOTIFICATION.
     */
    s->media_changed = load;
    s->tray_open = !load;
    scsi_device_set_ua(&s->qdev, SENSE_CODE(UNIT_ATTENTION_NO_MEDIUM));
    s->media_event = true;
    s->eject_request = false;
}

static void scsi_cd_eject_request_cb(void *opaque, bool force)
{
    SCSIDiskState *s = opaque;

    s->eject_request = true;
    if (force) {
        s->tray_locked = false;
    }
}

static bool scsi_cd_is_tray_open(void *opaque)
{
    return ((SCSIDiskState *)opaque)->tray_open;
}

static bool scsi_cd_is_medium_locked(void *opaque)
{
    return ((SCSIDiskState *)opaque)->tray_locked;
}

static const BlockDevOps scsi_disk_removable_block_ops = {
    .change_media_cb = scsi_cd_change_media_cb,
    .eject_request_cb = scsi_cd_eject_request_cb,
    .is_tray_open = scsi_cd_is_tray_open,
    .is_medium_locked = scsi_cd_is_medium_locked,

    .resize_cb = scsi_disk_resize_cb,
};

static const BlockDevOps scsi_disk_block_ops = {
    .resize_cb = scsi_disk_resize_cb,
};

static void scsi_disk_unit_attention_reported(SCSIDevice *dev)
{
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, dev);
    if (s->media_changed) {
        s->media_changed = false;
        scsi_device_set_ua(&s->qdev, SENSE_CODE(MEDIUM_CHANGED));
    }
}

static void scsi_realize(SCSIDevice *dev, Error **errp)
{
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, dev);
    bool read_only;

    if (!s->qdev.conf.blk) {
        error_setg(errp, "drive property not set");
        return;
    }

    if (!(s->features & (1 << SCSI_DISK_F_REMOVABLE)) &&
        !blk_is_inserted(s->qdev.conf.blk)) {
        error_setg(errp, "Device needs media, but drive is empty");
        return;
    }

    if (!blkconf_blocksizes(&s->qdev.conf, errp)) {
        return;
    }

    if (blk_get_aio_context(s->qdev.conf.blk) != qemu_get_aio_context() &&
        !s->qdev.hba_supports_iothread)
    {
        error_setg(errp, "HBA does not support iothreads");
        return;
    }

    if (dev->type == TYPE_DISK) {
        if (!blkconf_geometry(&dev->conf, NULL, 65535, 255, 255, errp)) {
            return;
        }
    }

    read_only = !blk_supports_write_perm(s->qdev.conf.blk);
    if (dev->type == TYPE_ROM) {
        read_only = true;
    }

    if (!blkconf_apply_backend_options(&dev->conf, read_only,
                                       dev->type == TYPE_DISK, errp)) {
        return;
    }

    if (s->qdev.conf.discard_granularity == -1) {
        s->qdev.conf.discard_granularity =
            MAX(s->qdev.conf.logical_block_size, DEFAULT_DISCARD_GRANULARITY);
    }

    if (!s->version) {
        s->version = g_strdup(qemu_hw_version());
    }
    if (!s->vendor) {
        s->vendor = g_strdup("QEMU");
    }
    if (!s->device_id) {
        if (s->serial) {
            s->device_id = g_strdup_printf("%.20s", s->serial);
        } else {
            const char *str = blk_name(s->qdev.conf.blk);
            if (str && *str) {
                s->device_id = g_strdup(str);
            }
        }
    }

    if (blk_is_sg(s->qdev.conf.blk)) {
        error_setg(errp, "unwanted /dev/sg*");
        return;
    }

    if ((s->features & (1 << SCSI_DISK_F_REMOVABLE)) &&
            !(s->features & (1 << SCSI_DISK_F_NO_REMOVABLE_DEVOPS))) {
        blk_set_dev_ops(s->qdev.conf.blk, &scsi_disk_removable_block_ops, s);
    } else {
        blk_set_dev_ops(s->qdev.conf.blk, &scsi_disk_block_ops, s);
    }

    blk_iostatus_enable(s->qdev.conf.blk);

    add_boot_device_lchs(&dev->qdev, NULL,
                         dev->conf.lcyls,
                         dev->conf.lheads,
                         dev->conf.lsecs);
}

static void scsi_unrealize(SCSIDevice *dev)
{
    del_boot_device_lchs(&dev->qdev, NULL);
}

static void scsi_hd_realize(SCSIDevice *dev, Error **errp)
{
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, dev);
    AioContext *ctx = NULL;
    /* can happen for devices without drive. The error message for missing
     * backend will be issued in scsi_realize
     */
    if (s->qdev.conf.blk) {
        ctx = blk_get_aio_context(s->qdev.conf.blk);
        aio_context_acquire(ctx);
        if (!blkconf_blocksizes(&s->qdev.conf, errp)) {
            goto out;
        }
    }
    s->qdev.blocksize = s->qdev.conf.logical_block_size;
    s->qdev.type = TYPE_DISK;
    if (!s->product) {
        s->product = g_strdup("QEMU HARDDISK");
    }
    scsi_realize(&s->qdev, errp);
out:
    if (ctx) {
        aio_context_release(ctx);
    }
}

static void scsi_cd_realize(SCSIDevice *dev, Error **errp)
{
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, dev);
    AioContext *ctx;
    int ret;
    uint32_t blocksize = 2048;

    if (!dev->conf.blk) {
        /* Anonymous BlockBackend for an empty drive. As we put it into
         * dev->conf, qdev takes care of detaching on unplug. */
        dev->conf.blk = blk_new(qemu_get_aio_context(), 0, BLK_PERM_ALL);
        ret = blk_attach_dev(dev->conf.blk, &dev->qdev);
        assert(ret == 0);
    }

    if (dev->conf.physical_block_size != 0) {
        blocksize = dev->conf.physical_block_size;
    }

    ctx = blk_get_aio_context(dev->conf.blk);
    aio_context_acquire(ctx);
    s->qdev.blocksize = blocksize;
    s->qdev.type = TYPE_ROM;
    s->features |= 1 << SCSI_DISK_F_REMOVABLE;
    if (!s->product) {
        s->product = g_strdup("QEMU CD-ROM");
    }
    scsi_realize(&s->qdev, errp);
    aio_context_release(ctx);
}


static const SCSIReqOps scsi_disk_emulate_reqops = {
    .size         = sizeof(SCSIDiskReq),
    .free_req     = scsi_free_request,
    .send_command = scsi_disk_emulate_command,
    .read_data    = scsi_disk_emulate_read_data,
    .write_data   = scsi_disk_emulate_write_data,
    .get_buf      = scsi_get_buf,
};

static const SCSIReqOps scsi_disk_dma_reqops = {
    .size         = sizeof(SCSIDiskReq),
    .free_req     = scsi_free_request,
    .send_command = scsi_disk_dma_command,
    .read_data    = scsi_read_data,
    .write_data   = scsi_write_data,
    .get_buf      = scsi_get_buf,
    .load_request = scsi_disk_load_request,
    .save_request = scsi_disk_save_request,
};

static const SCSIReqOps *const scsi_disk_reqops_dispatch[256] = {
    [TEST_UNIT_READY]                 = &scsi_disk_emulate_reqops,
    [INQUIRY]                         = &scsi_disk_emulate_reqops,
    [MODE_SENSE]                      = &scsi_disk_emulate_reqops,
    [MODE_SENSE_10]                   = &scsi_disk_emulate_reqops,
    [START_STOP]                      = &scsi_disk_emulate_reqops,
    [ALLOW_MEDIUM_REMOVAL]            = &scsi_disk_emulate_reqops,
    [READ_CAPACITY_10]                = &scsi_disk_emulate_reqops,
    [READ_TOC]                        = &scsi_disk_emulate_reqops,
    [READ_DVD_STRUCTURE]              = &scsi_disk_emulate_reqops,
    [READ_DISC_INFORMATION]           = &scsi_disk_emulate_reqops,
    [GET_CONFIGURATION]               = &scsi_disk_emulate_reqops,
    [GET_EVENT_STATUS_NOTIFICATION]   = &scsi_disk_emulate_reqops,
    [MECHANISM_STATUS]                = &scsi_disk_emulate_reqops,
    [SERVICE_ACTION_IN_16]            = &scsi_disk_emulate_reqops,
    [REQUEST_SENSE]                   = &scsi_disk_emulate_reqops,
    [SYNCHRONIZE_CACHE]               = &scsi_disk_emulate_reqops,
    [SEEK_10]                         = &scsi_disk_emulate_reqops,
    [MODE_SELECT]                     = &scsi_disk_emulate_reqops,
    [MODE_SELECT_10]                  = &scsi_disk_emulate_reqops,
    [UNMAP]                           = &scsi_disk_emulate_reqops,
    [WRITE_SAME_10]                   = &scsi_disk_emulate_reqops,
    [WRITE_SAME_16]                   = &scsi_disk_emulate_reqops,
    [VERIFY_10]                       = &scsi_disk_emulate_reqops,
    [VERIFY_12]                       = &scsi_disk_emulate_reqops,
    [VERIFY_16]                       = &scsi_disk_emulate_reqops,
    [FORMAT_UNIT]                     = &scsi_disk_emulate_reqops,

    [READ_6]                          = &scsi_disk_dma_reqops,
    [READ_10]                         = &scsi_disk_dma_reqops,
    [READ_12]                         = &scsi_disk_dma_reqops,
    [READ_16]                         = &scsi_disk_dma_reqops,
    [WRITE_6]                         = &scsi_disk_dma_reqops,
    [WRITE_10]                        = &scsi_disk_dma_reqops,
    [WRITE_12]                        = &scsi_disk_dma_reqops,
    [WRITE_16]                        = &scsi_disk_dma_reqops,
    [WRITE_VERIFY_10]                 = &scsi_disk_dma_reqops,
    [WRITE_VERIFY_12]                 = &scsi_disk_dma_reqops,
    [WRITE_VERIFY_16]                 = &scsi_disk_dma_reqops,
};

static void scsi_disk_new_request_dump(uint32_t lun, uint32_t tag, uint8_t *buf)
{
    int i;
    int len = scsi_cdb_length(buf);
    char *line_buffer, *p;

    assert(len > 0 && len <= 16);
    line_buffer = g_malloc(len * 5 + 1);

    for (i = 0, p = line_buffer; i < len; i++) {
        p += sprintf(p, " 0x%02x", buf[i]);
    }
    trace_scsi_disk_new_request(lun, tag, line_buffer);

    g_free(line_buffer);
}

static SCSIRequest *scsi_new_request(SCSIDevice *d, uint32_t tag, uint32_t lun,
                                     uint8_t *buf, void *hba_private)
{
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, d);
    SCSIRequest *req;
    const SCSIReqOps *ops;
    uint8_t command;

    command = buf[0];
    ops = scsi_disk_reqops_dispatch[command];
    if (!ops) {
        ops = &scsi_disk_emulate_reqops;
    }
    req = scsi_req_alloc(ops, &s->qdev, tag, lun, hba_private);

    if (trace_event_get_state_backends(TRACE_SCSI_DISK_NEW_REQUEST)) {
        scsi_disk_new_request_dump(lun, tag, buf);
    }

    return req;
}

#ifdef __linux__
static int get_device_type(SCSIDiskState *s)
{
    uint8_t cmd[16];
    uint8_t buf[36];
    int ret;

    memset(cmd, 0, sizeof(cmd));
    memset(buf, 0, sizeof(buf));
    cmd[0] = INQUIRY;
    cmd[4] = sizeof(buf);

    ret = scsi_SG_IO_FROM_DEV(s->qdev.conf.blk, cmd, sizeof(cmd),
                              buf, sizeof(buf), s->qdev.io_timeout);
    if (ret < 0) {
        return -1;
    }
    s->qdev.type = buf[0];
    if (buf[1] & 0x80) {
        s->features |= 1 << SCSI_DISK_F_REMOVABLE;
    }
    return 0;
}

static void scsi_block_realize(SCSIDevice *dev, Error **errp)
{
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, dev);
    AioContext *ctx;
    int sg_version;
    int rc;

    if (!s->qdev.conf.blk) {
        error_setg(errp, "drive property not set");
        return;
    }

    if (s->rotation_rate) {
        error_report_once("rotation_rate is specified for scsi-block but is "
                          "not implemented. This option is deprecated and will "
                          "be removed in a future version");
    }

    ctx = blk_get_aio_context(s->qdev.conf.blk);
    aio_context_acquire(ctx);

    /* check we are using a driver managing SG_IO (version 3 and after) */
    rc = blk_ioctl(s->qdev.conf.blk, SG_GET_VERSION_NUM, &sg_version);
    if (rc < 0) {
        error_setg_errno(errp, -rc, "cannot get SG_IO version number");
        if (rc != -EPERM) {
            error_append_hint(errp, "Is this a SCSI device?\n");
        }
        goto out;
    }
    if (sg_version < 30000) {
        error_setg(errp, "scsi generic interface too old");
        goto out;
    }

    /* get device type from INQUIRY data */
    rc = get_device_type(s);
    if (rc < 0) {
        error_setg(errp, "INQUIRY failed");
        goto out;
    }

    /* Make a guess for the block size, we'll fix it when the guest sends.
     * READ CAPACITY.  If they don't, they likely would assume these sizes
     * anyway. (TODO: check in /sys).
     */
    if (s->qdev.type == TYPE_ROM || s->qdev.type == TYPE_WORM) {
        s->qdev.blocksize = 2048;
    } else {
        s->qdev.blocksize = 512;
    }

    /* Makes the scsi-block device not removable by using HMP and QMP eject
     * command.
     */
    s->features |= (1 << SCSI_DISK_F_NO_REMOVABLE_DEVOPS);

    scsi_realize(&s->qdev, errp);
    scsi_generic_read_device_inquiry(&s->qdev);

out:
    aio_context_release(ctx);
}

typedef struct SCSIBlockReq {
    SCSIDiskReq req;
    sg_io_hdr_t io_header;

    /* Selected bytes of the original CDB, copied into our own CDB.  */
    uint8_t cmd, cdb1, group_number;

    /* CDB passed to SG_IO.  */
    uint8_t cdb[16];
    BlockCompletionFunc *cb;
    void *cb_opaque;
} SCSIBlockReq;

static void scsi_block_sgio_complete(void *opaque, int ret)
{
    SCSIBlockReq *req = (SCSIBlockReq *)opaque;
    SCSIDiskReq *r = &req->req;
    SCSIDevice *s = r->req.dev;
    sg_io_hdr_t *io_hdr = &req->io_header;

    if (ret == 0) {
        if (io_hdr->host_status != SCSI_HOST_OK) {
            scsi_req_complete_failed(&r->req, io_hdr->host_status);
            scsi_req_unref(&r->req);
            return;
        }

        if (io_hdr->driver_status & SG_ERR_DRIVER_TIMEOUT) {
            ret = BUSY;
        } else {
            ret = io_hdr->status;
        }

        if (ret > 0) {
            aio_context_acquire(blk_get_aio_context(s->conf.blk));
            if (scsi_handle_rw_error(r, ret, true)) {
                aio_context_release(blk_get_aio_context(s->conf.blk));
                scsi_req_unref(&r->req);
                return;
            }
            aio_context_release(blk_get_aio_context(s->conf.blk));

            /* Ignore error.  */
            ret = 0;
        }
    }

    req->cb(req->cb_opaque, ret);
}

static BlockAIOCB *scsi_block_do_sgio(SCSIBlockReq *req,
                                      int64_t offset, QEMUIOVector *iov,
                                      int direction,
                                      BlockCompletionFunc *cb, void *opaque)
{
    sg_io_hdr_t *io_header = &req->io_header;
    SCSIDiskReq *r = &req->req;
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, r->req.dev);
    int nb_logical_blocks;
    uint64_t lba;
    BlockAIOCB *aiocb;

    /* This is not supported yet.  It can only happen if the guest does
     * reads and writes that are not aligned to one logical sectors
     * _and_ cover multiple MemoryRegions.
     */
    assert(offset % s->qdev.blocksize == 0);
    assert(iov->size % s->qdev.blocksize == 0);

    io_header->interface_id = 'S';

    /* The data transfer comes from the QEMUIOVector.  */
    io_header->dxfer_direction = direction;
    io_header->dxfer_len = iov->size;
    io_header->dxferp = (void *)iov->iov;
    io_header->iovec_count = iov->niov;
    assert(io_header->iovec_count == iov->niov); /* no overflow! */

    /* Build a new CDB with the LBA and length patched in, in case
     * DMA helpers split the transfer in multiple segments.  Do not
     * build a CDB smaller than what the guest wanted, and only build
     * a larger one if strictly necessary.
     */
    io_header->cmdp = req->cdb;
    lba = offset / s->qdev.blocksize;
    nb_logical_blocks = io_header->dxfer_len / s->qdev.blocksize;

    if ((req->cmd >> 5) == 0 && lba <= 0x1ffff) {
        /* 6-byte CDB */
        stl_be_p(&req->cdb[0], lba | (req->cmd << 24));
        req->cdb[4] = nb_logical_blocks;
        req->cdb[5] = 0;
        io_header->cmd_len = 6;
    } else if ((req->cmd >> 5) <= 1 && lba <= 0xffffffffULL) {
        /* 10-byte CDB */
        req->cdb[0] = (req->cmd & 0x1f) | 0x20;
        req->cdb[1] = req->cdb1;
        stl_be_p(&req->cdb[2], lba);
        req->cdb[6] = req->group_number;
        stw_be_p(&req->cdb[7], nb_logical_blocks);
        req->cdb[9] = 0;
        io_header->cmd_len = 10;
    } else if ((req->cmd >> 5) != 4 && lba <= 0xffffffffULL) {
        /* 12-byte CDB */
        req->cdb[0] = (req->cmd & 0x1f) | 0xA0;
        req->cdb[1] = req->cdb1;
        stl_be_p(&req->cdb[2], lba);
        stl_be_p(&req->cdb[6], nb_logical_blocks);
        req->cdb[10] = req->group_number;
        req->cdb[11] = 0;
        io_header->cmd_len = 12;
    } else {
        /* 16-byte CDB */
        req->cdb[0] = (req->cmd & 0x1f) | 0x80;
        req->cdb[1] = req->cdb1;
        stq_be_p(&req->cdb[2], lba);
        stl_be_p(&req->cdb[10], nb_logical_blocks);
        req->cdb[14] = req->group_number;
        req->cdb[15] = 0;
        io_header->cmd_len = 16;
    }

    /* The rest is as in scsi-generic.c.  */
    io_header->mx_sb_len = sizeof(r->req.sense);
    io_header->sbp = r->req.sense;
    io_header->timeout = s->qdev.io_timeout * 1000;
    io_header->usr_ptr = r;
    io_header->flags |= SG_FLAG_DIRECT_IO;
    req->cb = cb;
    req->cb_opaque = opaque;
    trace_scsi_disk_aio_sgio_command(r->req.tag, req->cdb[0], lba,
                                     nb_logical_blocks, io_header->timeout);
    aiocb = blk_aio_ioctl(s->qdev.conf.blk, SG_IO, io_header, scsi_block_sgio_complete, req);
    assert(aiocb != NULL);
    return aiocb;
}

static bool scsi_block_no_fua(SCSICommand *cmd)
{
    return false;
}

static BlockAIOCB *scsi_block_dma_readv(int64_t offset,
                                        QEMUIOVector *iov,
                                        BlockCompletionFunc *cb, void *cb_opaque,
                                        void *opaque)
{
    SCSIBlockReq *r = opaque;
    return scsi_block_do_sgio(r, offset, iov,
                              SG_DXFER_FROM_DEV, cb, cb_opaque);
}

static BlockAIOCB *scsi_block_dma_writev(int64_t offset,
                                         QEMUIOVector *iov,
                                         BlockCompletionFunc *cb, void *cb_opaque,
                                         void *opaque)
{
    SCSIBlockReq *r = opaque;
    return scsi_block_do_sgio(r, offset, iov,
                              SG_DXFER_TO_DEV, cb, cb_opaque);
}

static bool scsi_block_is_passthrough(SCSIDiskState *s, uint8_t *buf)
{
    switch (buf[0]) {
    case VERIFY_10:
    case VERIFY_12:
    case VERIFY_16:
        /* Check if BYTCHK == 0x01 (data-out buffer contains data
         * for the number of logical blocks specified in the length
         * field).  For other modes, do not use scatter/gather operation.
         */
        if ((buf[1] & 6) == 2) {
            return false;
        }
        break;

    case READ_6:
    case READ_10:
    case READ_12:
    case READ_16:
    case WRITE_6:
    case WRITE_10:
    case WRITE_12:
    case WRITE_16:
    case WRITE_VERIFY_10:
    case WRITE_VERIFY_12:
    case WRITE_VERIFY_16:
        /* MMC writing cannot be done via DMA helpers, because it sometimes
         * involves writing beyond the maximum LBA or to negative LBA (lead-in).
         * We might use scsi_block_dma_reqops as long as no writing commands are
         * seen, but performance usually isn't paramount on optical media.  So,
         * just make scsi-block operate the same as scsi-generic for them.
         */
        if (s->qdev.type != TYPE_ROM) {
            return false;
        }
        break;

    default:
        break;
    }

    return true;
}


static int32_t scsi_block_dma_command(SCSIRequest *req, uint8_t *buf)
{
    SCSIBlockReq *r = (SCSIBlockReq *)req;
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, req->dev);

    r->cmd = req->cmd.buf[0];
    switch (r->cmd >> 5) {
    case 0:
        /* 6-byte CDB.  */
        r->cdb1 = r->group_number = 0;
        break;
    case 1:
        /* 10-byte CDB.  */
        r->cdb1 = req->cmd.buf[1];
        r->group_number = req->cmd.buf[6];
        break;
    case 4:
        /* 12-byte CDB.  */
        r->cdb1 = req->cmd.buf[1];
        r->group_number = req->cmd.buf[10];
        break;
    case 5:
        /* 16-byte CDB.  */
        r->cdb1 = req->cmd.buf[1];
        r->group_number = req->cmd.buf[14];
        break;
    default:
        abort();
    }

    /* Protection information is not supported.  For SCSI versions 2 and
     * older (as determined by snooping the guest's INQUIRY commands),
     * there is no RD/WR/VRPROTECT, so skip this check in these versions.
     */
    if (s->qdev.scsi_version > 2 && (req->cmd.buf[1] & 0xe0)) {
        scsi_check_condition(&r->req, SENSE_CODE(INVALID_FIELD));
        return 0;
    }

    return scsi_disk_dma_command(req, buf);
}

static const SCSIReqOps scsi_block_dma_reqops = {
    .size         = sizeof(SCSIBlockReq),
    .free_req     = scsi_free_request,
    .send_command = scsi_block_dma_command,
    .read_data    = scsi_read_data,
    .write_data   = scsi_write_data,
    .get_buf      = scsi_get_buf,
    .load_request = scsi_disk_load_request,
    .save_request = scsi_disk_save_request,
};

static SCSIRequest *scsi_block_new_request(SCSIDevice *d, uint32_t tag,
                                           uint32_t lun, uint8_t *buf,
                                           void *hba_private)
{
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, d);

    if (scsi_block_is_passthrough(s, buf)) {
        return scsi_req_alloc(&scsi_generic_req_ops, &s->qdev, tag, lun,
                              hba_private);
    } else {
        return scsi_req_alloc(&scsi_block_dma_reqops, &s->qdev, tag, lun,
                              hba_private);
    }
}

static int scsi_block_parse_cdb(SCSIDevice *d, SCSICommand *cmd,
                                  uint8_t *buf, size_t buf_len,
                                  void *hba_private)
{
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, d);

    if (scsi_block_is_passthrough(s, buf)) {
        return scsi_bus_parse_cdb(&s->qdev, cmd, buf, buf_len, hba_private);
    } else {
        return scsi_req_parse_cdb(&s->qdev, cmd, buf, buf_len);
    }
}

static void scsi_block_update_sense(SCSIRequest *req)
{
    SCSIDiskReq *r = DO_UPCAST(SCSIDiskReq, req, req);
    SCSIBlockReq *br = DO_UPCAST(SCSIBlockReq, req, r);
    r->req.sense_len = MIN(br->io_header.sb_len_wr, sizeof(r->req.sense));
}
#endif

static
BlockAIOCB *scsi_dma_readv(int64_t offset, QEMUIOVector *iov,
                           BlockCompletionFunc *cb, void *cb_opaque,
                           void *opaque)
{
    SCSIDiskReq *r = opaque;
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, r->req.dev);
    return blk_aio_preadv(s->qdev.conf.blk, offset, iov, 0, cb, cb_opaque);
}

static
BlockAIOCB *scsi_dma_writev(int64_t offset, QEMUIOVector *iov,
                            BlockCompletionFunc *cb, void *cb_opaque,
                            void *opaque)
{
    SCSIDiskReq *r = opaque;
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, r->req.dev);
    return blk_aio_pwritev(s->qdev.conf.blk, offset, iov, 0, cb, cb_opaque);
}

static void scsi_disk_base_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SCSIDiskClass *sdc = SCSI_DISK_BASE_CLASS(klass);

    dc->fw_name = "disk";
    dc->reset = scsi_disk_reset;
    sdc->dma_readv = scsi_dma_readv;
    sdc->dma_writev = scsi_dma_writev;
    sdc->need_fua_emulation = scsi_is_cmd_fua;
}

static const TypeInfo scsi_disk_base_info = {
    .name          = TYPE_SCSI_DISK_BASE,
    .parent        = TYPE_SCSI_DEVICE,
    .class_init    = scsi_disk_base_class_initfn,
    .instance_size = sizeof(SCSIDiskState),
    .class_size    = sizeof(SCSIDiskClass),
    .abstract      = true,
};

#define DEFINE_SCSI_DISK_PROPERTIES()                                   \
    DEFINE_PROP_DRIVE_IOTHREAD("drive", SCSIDiskState, qdev.conf.blk),  \
    DEFINE_BLOCK_PROPERTIES_BASE(SCSIDiskState, qdev.conf),             \
    DEFINE_BLOCK_ERROR_PROPERTIES(SCSIDiskState, qdev.conf),            \
    DEFINE_PROP_STRING("ver", SCSIDiskState, version),                  \
    DEFINE_PROP_STRING("serial", SCSIDiskState, serial),                \
    DEFINE_PROP_STRING("vendor", SCSIDiskState, vendor),                \
    DEFINE_PROP_STRING("product", SCSIDiskState, product),              \
    DEFINE_PROP_STRING("device_id", SCSIDiskState, device_id)


static Property scsi_hd_properties[] = {
    DEFINE_SCSI_DISK_PROPERTIES(),
    DEFINE_PROP_BIT("removable", SCSIDiskState, features,
                    SCSI_DISK_F_REMOVABLE, false),
    DEFINE_PROP_BIT("dpofua", SCSIDiskState, features,
                    SCSI_DISK_F_DPOFUA, false),
    DEFINE_PROP_UINT64("wwn", SCSIDiskState, qdev.wwn, 0),
    DEFINE_PROP_UINT64("port_wwn", SCSIDiskState, qdev.port_wwn, 0),
    DEFINE_PROP_UINT16("port_index", SCSIDiskState, port_index, 0),
    DEFINE_PROP_UINT64("max_unmap_size", SCSIDiskState, max_unmap_size,
                       DEFAULT_MAX_UNMAP_SIZE),
    DEFINE_PROP_UINT64("max_io_size", SCSIDiskState, max_io_size,
                       DEFAULT_MAX_IO_SIZE),
    DEFINE_PROP_UINT16("rotation_rate", SCSIDiskState, rotation_rate, 0),
    DEFINE_PROP_INT32("scsi_version", SCSIDiskState, qdev.default_scsi_version,
                      5),
    DEFINE_PROP_BIT("quirk_mode_page_vendor_specific_apple", SCSIDiskState,
                    quirks, SCSI_DISK_QUIRK_MODE_PAGE_VENDOR_SPECIFIC_APPLE,
                    0),
    DEFINE_BLOCK_CHS_PROPERTIES(SCSIDiskState, qdev.conf),
    DEFINE_PROP_END_OF_LIST(),
};

static const VMStateDescription vmstate_scsi_disk_state = {
    .name = "scsi-disk",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_SCSI_DEVICE(qdev, SCSIDiskState),
        VMSTATE_BOOL(media_changed, SCSIDiskState),
        VMSTATE_BOOL(media_event, SCSIDiskState),
        VMSTATE_BOOL(eject_request, SCSIDiskState),
        VMSTATE_BOOL(tray_open, SCSIDiskState),
        VMSTATE_BOOL(tray_locked, SCSIDiskState),
        VMSTATE_END_OF_LIST()
    }
};

static void scsi_hd_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SCSIDeviceClass *sc = SCSI_DEVICE_CLASS(klass);

    sc->realize      = scsi_hd_realize;
    sc->unrealize    = scsi_unrealize;
    sc->alloc_req    = scsi_new_request;
    sc->unit_attention_reported = scsi_disk_unit_attention_reported;
    dc->desc = "virtual SCSI disk";
    device_class_set_props(dc, scsi_hd_properties);
    dc->vmsd  = &vmstate_scsi_disk_state;
}

static const TypeInfo scsi_hd_info = {
    .name          = "scsi-hd",
    .parent        = TYPE_SCSI_DISK_BASE,
    .class_init    = scsi_hd_class_initfn,
};

static Property scsi_cd_properties[] = {
    DEFINE_SCSI_DISK_PROPERTIES(),
    DEFINE_PROP_UINT64("wwn", SCSIDiskState, qdev.wwn, 0),
    DEFINE_PROP_UINT64("port_wwn", SCSIDiskState, qdev.port_wwn, 0),
    DEFINE_PROP_UINT16("port_index", SCSIDiskState, port_index, 0),
    DEFINE_PROP_UINT64("max_io_size", SCSIDiskState, max_io_size,
                       DEFAULT_MAX_IO_SIZE),
    DEFINE_PROP_INT32("scsi_version", SCSIDiskState, qdev.default_scsi_version,
                      5),
    DEFINE_PROP_BIT("quirk_mode_page_apple_vendor", SCSIDiskState, quirks,
                    SCSI_DISK_QUIRK_MODE_PAGE_APPLE_VENDOR, 0),
    DEFINE_PROP_BIT("quirk_mode_sense_rom_use_dbd", SCSIDiskState, quirks,
                    SCSI_DISK_QUIRK_MODE_SENSE_ROM_USE_DBD, 0),
    DEFINE_PROP_BIT("quirk_mode_page_vendor_specific_apple", SCSIDiskState,
                    quirks, SCSI_DISK_QUIRK_MODE_PAGE_VENDOR_SPECIFIC_APPLE,
                    0),
    DEFINE_PROP_BIT("quirk_mode_page_truncated", SCSIDiskState, quirks,
                    SCSI_DISK_QUIRK_MODE_PAGE_TRUNCATED, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void scsi_cd_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SCSIDeviceClass *sc = SCSI_DEVICE_CLASS(klass);

    sc->realize      = scsi_cd_realize;
    sc->alloc_req    = scsi_new_request;
    sc->unit_attention_reported = scsi_disk_unit_attention_reported;
    dc->desc = "virtual SCSI CD-ROM";
    device_class_set_props(dc, scsi_cd_properties);
    dc->vmsd  = &vmstate_scsi_disk_state;
}

static const TypeInfo scsi_cd_info = {
    .name          = "scsi-cd",
    .parent        = TYPE_SCSI_DISK_BASE,
    .class_init    = scsi_cd_class_initfn,
};

#ifdef __linux__
static Property scsi_block_properties[] = {
    DEFINE_BLOCK_ERROR_PROPERTIES(SCSIDiskState, qdev.conf),
    DEFINE_PROP_DRIVE("drive", SCSIDiskState, qdev.conf.blk),
    DEFINE_PROP_BOOL("share-rw", SCSIDiskState, qdev.conf.share_rw, false),
    DEFINE_PROP_UINT16("rotation_rate", SCSIDiskState, rotation_rate, 0),
    DEFINE_PROP_UINT64("max_unmap_size", SCSIDiskState, max_unmap_size,
                       DEFAULT_MAX_UNMAP_SIZE),
    DEFINE_PROP_UINT64("max_io_size", SCSIDiskState, max_io_size,
                       DEFAULT_MAX_IO_SIZE),
    DEFINE_PROP_INT32("scsi_version", SCSIDiskState, qdev.default_scsi_version,
                      -1),
    DEFINE_PROP_UINT32("io_timeout", SCSIDiskState, qdev.io_timeout,
                       DEFAULT_IO_TIMEOUT),
    DEFINE_PROP_END_OF_LIST(),
};

static void scsi_block_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SCSIDeviceClass *sc = SCSI_DEVICE_CLASS(klass);
    SCSIDiskClass *sdc = SCSI_DISK_BASE_CLASS(klass);

    sc->realize      = scsi_block_realize;
    sc->alloc_req    = scsi_block_new_request;
    sc->parse_cdb    = scsi_block_parse_cdb;
    sdc->dma_readv   = scsi_block_dma_readv;
    sdc->dma_writev  = scsi_block_dma_writev;
    sdc->update_sense = scsi_block_update_sense;
    sdc->need_fua_emulation = scsi_block_no_fua;
    dc->desc = "SCSI block device passthrough";
    device_class_set_props(dc, scsi_block_properties);
    dc->vmsd  = &vmstate_scsi_disk_state;
}

static const TypeInfo scsi_block_info = {
    .name          = "scsi-block",
    .parent        = TYPE_SCSI_DISK_BASE,
    .class_init    = scsi_block_class_initfn,
};
#endif

static void scsi_disk_register_types(void)
{
    type_register_static(&scsi_disk_base_info);
    type_register_static(&scsi_hd_info);
    type_register_static(&scsi_cd_info);
#ifdef __linux__
    type_register_static(&scsi_block_info);
#endif
}

type_init(scsi_disk_register_types)
