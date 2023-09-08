/*
 * QEMU UFS Logical Unit
 *
 * Copyright (c) 2023 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Written by Jeuk Kim <jeuk20.kim@samsung.com>
 *
 * This code is licensed under the GNU GPL v2 or later.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "qemu/memalign.h"
#include "hw/scsi/scsi.h"
#include "scsi/constants.h"
#include "sysemu/block-backend.h"
#include "qemu/cutils.h"
#include "trace.h"
#include "ufs.h"

/*
 * The code below handling SCSI commands is copied from hw/scsi/scsi-disk.c,
 * with minor adjustments to make it work for UFS.
 */

#define SCSI_DMA_BUF_SIZE (128 * KiB)
#define SCSI_MAX_INQUIRY_LEN 256
#define SCSI_INQUIRY_DATA_SIZE 36
#define SCSI_MAX_MODE_LEN 256

typedef struct UfsSCSIReq {
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
} UfsSCSIReq;

static void ufs_scsi_free_request(SCSIRequest *req)
{
    UfsSCSIReq *r = DO_UPCAST(UfsSCSIReq, req, req);

    qemu_vfree(r->iov.iov_base);
}

static void scsi_check_condition(UfsSCSIReq *r, SCSISense sense)
{
    trace_ufs_scsi_check_condition(r->req.tag, sense.key, sense.asc,
                                   sense.ascq);
    scsi_req_build_sense(&r->req, sense);
    scsi_req_complete(&r->req, CHECK_CONDITION);
}

static int ufs_scsi_emulate_vpd_page(SCSIRequest *req, uint8_t *outbuf,
                                     uint32_t outbuf_len)
{
    UfsHc *u = UFS(req->bus->qbus.parent);
    UfsLu *lu = DO_UPCAST(UfsLu, qdev, req->dev);
    uint8_t page_code = req->cmd.buf[2];
    int start, buflen = 0;

    if (outbuf_len < SCSI_INQUIRY_DATA_SIZE) {
        return -1;
    }

    outbuf[buflen++] = lu->qdev.type & 0x1f;
    outbuf[buflen++] = page_code;
    outbuf[buflen++] = 0x00;
    outbuf[buflen++] = 0x00;
    start = buflen;

    switch (page_code) {
    case 0x00: /* Supported page codes, mandatory */
    {
        trace_ufs_scsi_emulate_vpd_page_00(req->cmd.xfer);
        outbuf[buflen++] = 0x00; /* list of supported pages (this page) */
        if (u->params.serial) {
            outbuf[buflen++] = 0x80; /* unit serial number */
        }
        outbuf[buflen++] = 0x87; /* mode page policy */
        break;
    }
    case 0x80: /* Device serial number, optional */
    {
        int l;

        if (!u->params.serial) {
            trace_ufs_scsi_emulate_vpd_page_80_not_supported();
            return -1;
        }

        l = strlen(u->params.serial);
        if (l > SCSI_INQUIRY_DATA_SIZE) {
            l = SCSI_INQUIRY_DATA_SIZE;
        }

        trace_ufs_scsi_emulate_vpd_page_80(req->cmd.xfer);
        memcpy(outbuf + buflen, u->params.serial, l);
        buflen += l;
        break;
    }
    case 0x87: /* Mode Page Policy, mandatory */
    {
        trace_ufs_scsi_emulate_vpd_page_87(req->cmd.xfer);
        outbuf[buflen++] = 0x3f; /* apply to all mode pages and subpages */
        outbuf[buflen++] = 0xff;
        outbuf[buflen++] = 0; /* shared */
        outbuf[buflen++] = 0;
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

static int ufs_scsi_emulate_inquiry(SCSIRequest *req, uint8_t *outbuf,
                                    uint32_t outbuf_len)
{
    int buflen = 0;

    if (outbuf_len < SCSI_INQUIRY_DATA_SIZE) {
        return -1;
    }

    if (req->cmd.buf[1] & 0x1) {
        /* Vital product data */
        return ufs_scsi_emulate_vpd_page(req, outbuf, outbuf_len);
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

    if (is_wlun(req->lun)) {
        outbuf[0] = TYPE_WLUN;
    } else {
        outbuf[0] = 0;
    }
    outbuf[1] = 0;

    strpadcpy((char *)&outbuf[16], 16, "QEMU UFS", ' ');
    strpadcpy((char *)&outbuf[8], 8, "QEMU", ' ');

    memset(&outbuf[32], 0, 4);

    outbuf[2] = 0x06; /* SPC-4 */
    outbuf[3] = 0x2;

    if (buflen > SCSI_INQUIRY_DATA_SIZE) {
        outbuf[4] = buflen - 5; /* Additional Length = (Len - 1) - 4 */
    } else {
        /*
         * If the allocation length of CDB is too small, the additional
         * length is not adjusted
         */
        outbuf[4] = SCSI_INQUIRY_DATA_SIZE - 5;
    }

    /* Support TCQ.  */
    outbuf[7] = req->bus->info->tcq ? 0x02 : 0;
    return buflen;
}

static int mode_sense_page(UfsLu *lu, int page, uint8_t **p_outbuf,
                           int page_control)
{
    static const int mode_sense_valid[0x3f] = {
        [MODE_PAGE_CACHING] = 1,
        [MODE_PAGE_R_W_ERROR] = 1,
        [MODE_PAGE_CONTROL] = 1,
    };

    uint8_t *p = *p_outbuf + 2;
    int length;

    assert(page < ARRAY_SIZE(mode_sense_valid));
    if ((mode_sense_valid[page]) == 0) {
        return -1;
    }

    /*
     * If Changeable Values are requested, a mask denoting those mode parameters
     * that are changeable shall be returned. As we currently don't support
     * parameter changes via MODE_SELECT all bits are returned set to zero.
     * The buffer was already memset to zero by the caller of this function.
     */
    switch (page) {
    case MODE_PAGE_CACHING:
        length = 0x12;
        if (page_control == 1 || /* Changeable Values */
            blk_enable_write_cache(lu->qdev.conf.blk)) {
            p[0] = 4; /* WCE */
        }
        break;

    case MODE_PAGE_R_W_ERROR:
        length = 10;
        if (page_control == 1) { /* Changeable Values */
            break;
        }
        p[0] = 0x80; /* Automatic Write Reallocation Enabled */
        break;

    case MODE_PAGE_CONTROL:
        length = 10;
        if (page_control == 1) { /* Changeable Values */
            break;
        }
        p[1] = 0x10; /* Queue Algorithm modifier */
        p[8] = 0xff; /* Busy Timeout Period */
        p[9] = 0xff;
        break;

    default:
        return -1;
    }

    assert(length < 256);
    (*p_outbuf)[0] = page;
    (*p_outbuf)[1] = length;
    *p_outbuf += length + 2;
    return length + 2;
}

static int ufs_scsi_emulate_mode_sense(UfsSCSIReq *r, uint8_t *outbuf)
{
    UfsLu *lu = DO_UPCAST(UfsLu, qdev, r->req.dev);
    bool dbd;
    int page, buflen, ret, page_control;
    uint8_t *p;
    uint8_t dev_specific_param = 0;

    dbd = (r->req.cmd.buf[1] & 0x8) != 0;
    if (!dbd) {
        return -1;
    }

    page = r->req.cmd.buf[2] & 0x3f;
    page_control = (r->req.cmd.buf[2] & 0xc0) >> 6;

    trace_ufs_scsi_emulate_mode_sense((r->req.cmd.buf[0] == MODE_SENSE) ? 6 :
                                                                          10,
                                      page, r->req.cmd.xfer, page_control);
    memset(outbuf, 0, r->req.cmd.xfer);
    p = outbuf;

    if (!blk_is_writable(lu->qdev.conf.blk)) {
        dev_specific_param |= 0x80; /* Readonly.  */
    }

    p[2] = 0; /* Medium type.  */
    p[3] = dev_specific_param;
    p[6] = p[7] = 0; /* Block descriptor length.  */
    p += 8;

    if (page_control == 3) {
        /* Saved Values */
        scsi_check_condition(r, SENSE_CODE(SAVING_PARAMS_NOT_SUPPORTED));
        return -1;
    }

    if (page == 0x3f) {
        for (page = 0; page <= 0x3e; page++) {
            mode_sense_page(lu, page, &p, page_control);
        }
    } else {
        ret = mode_sense_page(lu, page, &p, page_control);
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
    outbuf[0] = ((buflen - 2) >> 8) & 0xff;
    outbuf[1] = (buflen - 2) & 0xff;
    return buflen;
}

/*
 * scsi_handle_rw_error has two return values.  False means that the error
 * must be ignored, true means that the error has been processed and the
 * caller should not do anything else for this request.  Note that
 * scsi_handle_rw_error always manages its reference counts, independent
 * of the return value.
 */
static bool scsi_handle_rw_error(UfsSCSIReq *r, int ret, bool acct_failed)
{
    bool is_read = (r->req.cmd.mode == SCSI_XFER_FROM_DEV);
    UfsLu *lu = DO_UPCAST(UfsLu, qdev, r->req.dev);
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
    if (req_has_sense && scsi_sense_buf_is_guest_recoverable(
                             r->req.sense, sizeof(r->req.sense))) {
        action = BLOCK_ERROR_ACTION_REPORT;
        acct_failed = false;
    } else {
        action = blk_get_error_action(lu->qdev.conf.blk, is_read, error);
        blk_error_action(lu->qdev.conf.blk, action, is_read, error);
    }

    switch (action) {
    case BLOCK_ERROR_ACTION_REPORT:
        if (acct_failed) {
            block_acct_failed(blk_get_stats(lu->qdev.conf.blk), &r->acct);
        }
        if (!req_has_sense && status == CHECK_CONDITION) {
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

static bool ufs_scsi_req_check_error(UfsSCSIReq *r, int ret, bool acct_failed)
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
    UfsSCSIReq *r = (UfsSCSIReq *)opaque;
    UfsLu *lu = DO_UPCAST(UfsLu, qdev, r->req.dev);

    assert(r->req.aiocb != NULL);
    r->req.aiocb = NULL;
    aio_context_acquire(blk_get_aio_context(lu->qdev.conf.blk));
    if (ufs_scsi_req_check_error(r, ret, true)) {
        goto done;
    }

    block_acct_done(blk_get_stats(lu->qdev.conf.blk), &r->acct);
    scsi_req_complete(&r->req, GOOD);

done:
    aio_context_release(blk_get_aio_context(lu->qdev.conf.blk));
    scsi_req_unref(&r->req);
}

static int32_t ufs_scsi_emulate_command(SCSIRequest *req, uint8_t *buf)
{
    UfsSCSIReq *r = DO_UPCAST(UfsSCSIReq, req, req);
    UfsLu *lu = DO_UPCAST(UfsLu, qdev, req->dev);
    uint32_t last_block = 0;
    uint8_t *outbuf;
    int buflen;

    switch (req->cmd.buf[0]) {
    case INQUIRY:
    case MODE_SENSE_10:
    case START_STOP:
    case REQUEST_SENSE:
        break;

    default:
        if (!blk_is_available(lu->qdev.conf.blk)) {
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
        r->iov.iov_base = blk_blockalign(lu->qdev.conf.blk, r->buflen);
    }

    outbuf = r->iov.iov_base;
    memset(outbuf, 0, r->buflen);
    switch (req->cmd.buf[0]) {
    case TEST_UNIT_READY:
        assert(blk_is_available(lu->qdev.conf.blk));
        break;
    case INQUIRY:
        buflen = ufs_scsi_emulate_inquiry(req, outbuf, r->buflen);
        if (buflen < 0) {
            goto illegal_request;
        }
        break;
    case MODE_SENSE_10:
        buflen = ufs_scsi_emulate_mode_sense(r, outbuf);
        if (buflen < 0) {
            goto illegal_request;
        }
        break;
    case READ_CAPACITY_10:
        /* The normal LEN field for this command is zero.  */
        memset(outbuf, 0, 8);
        if (lu->qdev.max_lba > 0) {
            last_block = lu->qdev.max_lba - 1;
        };
        outbuf[0] = (last_block >> 24) & 0xff;
        outbuf[1] = (last_block >> 16) & 0xff;
        outbuf[2] = (last_block >> 8) & 0xff;
        outbuf[3] = last_block & 0xff;
        outbuf[4] = (lu->qdev.blocksize >> 24) & 0xff;
        outbuf[5] = (lu->qdev.blocksize >> 16) & 0xff;
        outbuf[6] = (lu->qdev.blocksize >> 8) & 0xff;
        outbuf[7] = lu->qdev.blocksize & 0xff;
        break;
    case REQUEST_SENSE:
        /* Just return "NO SENSE".  */
        buflen = scsi_convert_sense(NULL, 0, outbuf, r->buflen,
                                    (req->cmd.buf[1] & 1) == 0);
        if (buflen < 0) {
            goto illegal_request;
        }
        break;
    case SYNCHRONIZE_CACHE:
        /* The request is used as the AIO opaque value, so add a ref.  */
        scsi_req_ref(&r->req);
        block_acct_start(blk_get_stats(lu->qdev.conf.blk), &r->acct, 0,
                         BLOCK_ACCT_FLUSH);
        r->req.aiocb = blk_aio_flush(lu->qdev.conf.blk, scsi_aio_complete, r);
        return 0;
    case VERIFY_10:
        trace_ufs_scsi_emulate_command_VERIFY((req->cmd.buf[1] >> 1) & 3);
        if (req->cmd.buf[1] & 6) {
            goto illegal_request;
        }
        break;
    case SERVICE_ACTION_IN_16:
        /* Service Action In subcommands. */
        if ((req->cmd.buf[1] & 31) == SAI_READ_CAPACITY_16) {
            trace_ufs_scsi_emulate_command_SAI_16();
            memset(outbuf, 0, req->cmd.xfer);

            if (lu->qdev.max_lba > 0) {
                last_block = lu->qdev.max_lba - 1;
            };
            outbuf[0] = 0;
            outbuf[1] = 0;
            outbuf[2] = 0;
            outbuf[3] = 0;
            outbuf[4] = (last_block >> 24) & 0xff;
            outbuf[5] = (last_block >> 16) & 0xff;
            outbuf[6] = (last_block >> 8) & 0xff;
            outbuf[7] = last_block & 0xff;
            outbuf[8] = (lu->qdev.blocksize >> 24) & 0xff;
            outbuf[9] = (lu->qdev.blocksize >> 16) & 0xff;
            outbuf[10] = (lu->qdev.blocksize >> 8) & 0xff;
            outbuf[11] = lu->qdev.blocksize & 0xff;
            outbuf[12] = 0;
            outbuf[13] = get_physical_block_exp(&lu->qdev.conf);

            if (lu->unit_desc.provisioning_type == 2 ||
                lu->unit_desc.provisioning_type == 3) {
                outbuf[14] = 0x80;
            }
            /* Protection, exponent and lowest lba field left blank. */
            break;
        }
        trace_ufs_scsi_emulate_command_SAI_unsupported();
        goto illegal_request;
    case MODE_SELECT_10:
        trace_ufs_scsi_emulate_command_MODE_SELECT_10(r->req.cmd.xfer);
        break;
    case START_STOP:
        /*
         * TODO: START_STOP is not yet implemented. It always returns success.
         * Revisit it when ufs power management is implemented.
         */
        trace_ufs_scsi_emulate_command_START_STOP();
        break;
    case FORMAT_UNIT:
        trace_ufs_scsi_emulate_command_FORMAT_UNIT();
        break;
    case SEND_DIAGNOSTIC:
        trace_ufs_scsi_emulate_command_SEND_DIAGNOSTIC();
        break;
    default:
        trace_ufs_scsi_emulate_command_UNKNOWN(buf[0],
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
}

static void ufs_scsi_emulate_read_data(SCSIRequest *req)
{
    UfsSCSIReq *r = DO_UPCAST(UfsSCSIReq, req, req);
    int buflen = r->iov.iov_len;

    if (buflen) {
        trace_ufs_scsi_emulate_read_data(buflen);
        r->iov.iov_len = 0;
        r->started = true;
        scsi_req_data(&r->req, buflen);
        return;
    }

    /* This also clears the sense buffer for REQUEST SENSE.  */
    scsi_req_complete(&r->req, GOOD);
}

static int ufs_scsi_check_mode_select(UfsLu *lu, int page, uint8_t *inbuf,
                                      int inlen)
{
    uint8_t mode_current[SCSI_MAX_MODE_LEN];
    uint8_t mode_changeable[SCSI_MAX_MODE_LEN];
    uint8_t *p;
    int len, expected_len, changeable_len, i;

    /*
     * The input buffer does not include the page header, so it is
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
    len = mode_sense_page(lu, page, &p, 0);
    if (len < 0 || len != expected_len) {
        return -1;
    }

    p = mode_changeable;
    memset(mode_changeable, 0, inlen + 2);
    changeable_len = mode_sense_page(lu, page, &p, 1);
    assert(changeable_len == len);

    /*
     * Check that unchangeable bits are the same as what MODE SENSE
     * would return.
     */
    for (i = 2; i < len; i++) {
        if (((mode_current[i] ^ inbuf[i - 2]) & ~mode_changeable[i]) != 0) {
            return -1;
        }
    }
    return 0;
}

static void ufs_scsi_apply_mode_select(UfsLu *lu, int page, uint8_t *p)
{
    switch (page) {
    case MODE_PAGE_CACHING:
        blk_set_enable_write_cache(lu->qdev.conf.blk, (p[0] & 4) != 0);
        break;

    default:
        break;
    }
}

static int mode_select_pages(UfsSCSIReq *r, uint8_t *p, int len, bool change)
{
    UfsLu *lu = DO_UPCAST(UfsLu, qdev, r->req.dev);

    while (len > 0) {
        int page, page_len;

        page = p[0] & 0x3f;
        if (p[0] & 0x40) {
            goto invalid_param;
        } else {
            if (len < 2) {
                goto invalid_param_len;
            }
            page_len = p[1];
            p += 2;
            len -= 2;
        }

        if (page_len > len) {
            goto invalid_param_len;
        }

        if (!change) {
            if (ufs_scsi_check_mode_select(lu, page, p, page_len) < 0) {
                goto invalid_param;
            }
        } else {
            ufs_scsi_apply_mode_select(lu, page, p);
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

static void ufs_scsi_emulate_mode_select(UfsSCSIReq *r, uint8_t *inbuf)
{
    UfsLu *lu = DO_UPCAST(UfsLu, qdev, r->req.dev);
    uint8_t *p = inbuf;
    int len = r->req.cmd.xfer;
    int hdr_len = 8;
    int bd_len;
    int pass;

    /* We only support PF=1, SP=0.  */
    if ((r->req.cmd.buf[1] & 0x11) != 0x10) {
        goto invalid_field;
    }

    if (len < hdr_len) {
        goto invalid_param_len;
    }

    bd_len = lduw_be_p(&p[6]);
    if (bd_len != 0) {
        goto invalid_param;
    }

    len -= hdr_len;
    p += hdr_len;

    /* Ensure no change is made if there is an error!  */
    for (pass = 0; pass < 2; pass++) {
        if (mode_select_pages(r, p, len, pass == 1) < 0) {
            assert(pass == 0);
            return;
        }
    }

    if (!blk_enable_write_cache(lu->qdev.conf.blk)) {
        /* The request is used as the AIO opaque value, so add a ref.  */
        scsi_req_ref(&r->req);
        block_acct_start(blk_get_stats(lu->qdev.conf.blk), &r->acct, 0,
                         BLOCK_ACCT_FLUSH);
        r->req.aiocb = blk_aio_flush(lu->qdev.conf.blk, scsi_aio_complete, r);
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

/* block_num and nb_blocks expected to be in qdev blocksize */
static inline bool check_lba_range(UfsLu *lu, uint64_t block_num,
                                   uint32_t nb_blocks)
{
    /*
     * The first line tests that no overflow happens when computing the last
     * block.  The second line tests that the last accessed block is in
     * range.
     *
     * Careful, the computations should not underflow for nb_blocks == 0,
     * and a 0-block read to the first LBA beyond the end of device is
     * valid.
     */
    return (block_num <= block_num + nb_blocks &&
            block_num + nb_blocks <= lu->qdev.max_lba + 1);
}

static void ufs_scsi_emulate_write_data(SCSIRequest *req)
{
    UfsSCSIReq *r = DO_UPCAST(UfsSCSIReq, req, req);

    if (r->iov.iov_len) {
        int buflen = r->iov.iov_len;
        trace_ufs_scsi_emulate_write_data(buflen);
        r->iov.iov_len = 0;
        scsi_req_data(&r->req, buflen);
        return;
    }

    switch (req->cmd.buf[0]) {
    case MODE_SELECT_10:
        /* This also clears the sense buffer for REQUEST SENSE.  */
        ufs_scsi_emulate_mode_select(r, r->iov.iov_base);
        break;
    default:
        abort();
    }
}

/* Return a pointer to the data buffer.  */
static uint8_t *ufs_scsi_get_buf(SCSIRequest *req)
{
    UfsSCSIReq *r = DO_UPCAST(UfsSCSIReq, req, req);

    return (uint8_t *)r->iov.iov_base;
}

static int32_t ufs_scsi_dma_command(SCSIRequest *req, uint8_t *buf)
{
    UfsSCSIReq *r = DO_UPCAST(UfsSCSIReq, req, req);
    UfsLu *lu = DO_UPCAST(UfsLu, qdev, req->dev);
    uint32_t len;
    uint8_t command;

    command = buf[0];

    if (!blk_is_available(lu->qdev.conf.blk)) {
        scsi_check_condition(r, SENSE_CODE(NO_MEDIUM));
        return 0;
    }

    len = scsi_data_cdb_xfer(r->req.cmd.buf);
    switch (command) {
    case READ_6:
    case READ_10:
        trace_ufs_scsi_dma_command_READ(r->req.cmd.lba, len);
        if (r->req.cmd.buf[1] & 0xe0) {
            goto illegal_request;
        }
        if (!check_lba_range(lu, r->req.cmd.lba, len)) {
            goto illegal_lba;
        }
        r->sector = r->req.cmd.lba * (lu->qdev.blocksize / BDRV_SECTOR_SIZE);
        r->sector_count = len * (lu->qdev.blocksize / BDRV_SECTOR_SIZE);
        break;
    case WRITE_6:
    case WRITE_10:
        trace_ufs_scsi_dma_command_WRITE(r->req.cmd.lba, len);
        if (!blk_is_writable(lu->qdev.conf.blk)) {
            scsi_check_condition(r, SENSE_CODE(WRITE_PROTECTED));
            return 0;
        }
        if (r->req.cmd.buf[1] & 0xe0) {
            goto illegal_request;
        }
        if (!check_lba_range(lu, r->req.cmd.lba, len)) {
            goto illegal_lba;
        }
        r->sector = r->req.cmd.lba * (lu->qdev.blocksize / BDRV_SECTOR_SIZE);
        r->sector_count = len * (lu->qdev.blocksize / BDRV_SECTOR_SIZE);
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
    r->need_fua_emulation = ((r->req.cmd.buf[1] & 8) != 0);
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

static void scsi_write_do_fua(UfsSCSIReq *r)
{
    UfsLu *lu = DO_UPCAST(UfsLu, qdev, r->req.dev);

    assert(r->req.aiocb == NULL);
    assert(!r->req.io_canceled);

    if (r->need_fua_emulation) {
        block_acct_start(blk_get_stats(lu->qdev.conf.blk), &r->acct, 0,
                         BLOCK_ACCT_FLUSH);
        r->req.aiocb = blk_aio_flush(lu->qdev.conf.blk, scsi_aio_complete, r);
        return;
    }

    scsi_req_complete(&r->req, GOOD);
    scsi_req_unref(&r->req);
}

static void scsi_dma_complete_noio(UfsSCSIReq *r, int ret)
{
    assert(r->req.aiocb == NULL);
    if (ufs_scsi_req_check_error(r, ret, false)) {
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
    UfsSCSIReq *r = (UfsSCSIReq *)opaque;
    UfsLu *lu = DO_UPCAST(UfsLu, qdev, r->req.dev);

    assert(r->req.aiocb != NULL);
    r->req.aiocb = NULL;

    aio_context_acquire(blk_get_aio_context(lu->qdev.conf.blk));
    if (ret < 0) {
        block_acct_failed(blk_get_stats(lu->qdev.conf.blk), &r->acct);
    } else {
        block_acct_done(blk_get_stats(lu->qdev.conf.blk), &r->acct);
    }
    scsi_dma_complete_noio(r, ret);
    aio_context_release(blk_get_aio_context(lu->qdev.conf.blk));
}

static BlockAIOCB *scsi_dma_readv(int64_t offset, QEMUIOVector *iov,
                                  BlockCompletionFunc *cb, void *cb_opaque,
                                  void *opaque)
{
    UfsSCSIReq *r = opaque;
    UfsLu *lu = DO_UPCAST(UfsLu, qdev, r->req.dev);
    return blk_aio_preadv(lu->qdev.conf.blk, offset, iov, 0, cb, cb_opaque);
}

static void scsi_init_iovec(UfsSCSIReq *r, size_t size)
{
    UfsLu *lu = DO_UPCAST(UfsLu, qdev, r->req.dev);

    if (!r->iov.iov_base) {
        r->buflen = size;
        r->iov.iov_base = blk_blockalign(lu->qdev.conf.blk, r->buflen);
    }
    r->iov.iov_len = MIN(r->sector_count * BDRV_SECTOR_SIZE, r->buflen);
    qemu_iovec_init_external(&r->qiov, &r->iov, 1);
}

static void scsi_read_complete_noio(UfsSCSIReq *r, int ret)
{
    uint32_t n;

    assert(r->req.aiocb == NULL);
    if (ufs_scsi_req_check_error(r, ret, false)) {
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
    UfsSCSIReq *r = (UfsSCSIReq *)opaque;
    UfsLu *lu = DO_UPCAST(UfsLu, qdev, r->req.dev);

    assert(r->req.aiocb != NULL);
    r->req.aiocb = NULL;
    trace_ufs_scsi_read_data_count(r->sector_count);
    aio_context_acquire(blk_get_aio_context(lu->qdev.conf.blk));
    if (ret < 0) {
        block_acct_failed(blk_get_stats(lu->qdev.conf.blk), &r->acct);
    } else {
        block_acct_done(blk_get_stats(lu->qdev.conf.blk), &r->acct);
        trace_ufs_scsi_read_complete(r->req.tag, r->qiov.size);
    }
    scsi_read_complete_noio(r, ret);
    aio_context_release(blk_get_aio_context(lu->qdev.conf.blk));
}

/* Actually issue a read to the block device.  */
static void scsi_do_read(UfsSCSIReq *r, int ret)
{
    UfsLu *lu = DO_UPCAST(UfsLu, qdev, r->req.dev);

    assert(r->req.aiocb == NULL);
    if (ufs_scsi_req_check_error(r, ret, false)) {
        goto done;
    }

    /* The request is used as the AIO opaque value, so add a ref.  */
    scsi_req_ref(&r->req);

    if (r->req.sg) {
        dma_acct_start(lu->qdev.conf.blk, &r->acct, r->req.sg, BLOCK_ACCT_READ);
        r->req.residual -= r->req.sg->size;
        r->req.aiocb = dma_blk_io(
            blk_get_aio_context(lu->qdev.conf.blk), r->req.sg,
            r->sector << BDRV_SECTOR_BITS, BDRV_SECTOR_SIZE, scsi_dma_readv, r,
            scsi_dma_complete, r, DMA_DIRECTION_FROM_DEVICE);
    } else {
        scsi_init_iovec(r, SCSI_DMA_BUF_SIZE);
        block_acct_start(blk_get_stats(lu->qdev.conf.blk), &r->acct,
                         r->qiov.size, BLOCK_ACCT_READ);
        r->req.aiocb = scsi_dma_readv(r->sector << BDRV_SECTOR_BITS, &r->qiov,
                                      scsi_read_complete, r, r);
    }

done:
    scsi_req_unref(&r->req);
}

static void scsi_do_read_cb(void *opaque, int ret)
{
    UfsSCSIReq *r = (UfsSCSIReq *)opaque;
    UfsLu *lu = DO_UPCAST(UfsLu, qdev, r->req.dev);

    assert(r->req.aiocb != NULL);
    r->req.aiocb = NULL;

    aio_context_acquire(blk_get_aio_context(lu->qdev.conf.blk));
    if (ret < 0) {
        block_acct_failed(blk_get_stats(lu->qdev.conf.blk), &r->acct);
    } else {
        block_acct_done(blk_get_stats(lu->qdev.conf.blk), &r->acct);
    }
    scsi_do_read(opaque, ret);
    aio_context_release(blk_get_aio_context(lu->qdev.conf.blk));
}

/* Read more data from scsi device into buffer.  */
static void scsi_read_data(SCSIRequest *req)
{
    UfsSCSIReq *r = DO_UPCAST(UfsSCSIReq, req, req);
    UfsLu *lu = DO_UPCAST(UfsLu, qdev, r->req.dev);
    bool first;

    trace_ufs_scsi_read_data_count(r->sector_count);
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
        trace_ufs_scsi_read_data_invalid();
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
        block_acct_start(blk_get_stats(lu->qdev.conf.blk), &r->acct, 0,
                         BLOCK_ACCT_FLUSH);
        r->req.aiocb = blk_aio_flush(lu->qdev.conf.blk, scsi_do_read_cb, r);
    } else {
        scsi_do_read(r, 0);
    }
}

static void scsi_write_complete_noio(UfsSCSIReq *r, int ret)
{
    uint32_t n;

    assert(r->req.aiocb == NULL);
    if (ufs_scsi_req_check_error(r, ret, false)) {
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
        trace_ufs_scsi_write_complete_noio(r->req.tag, r->qiov.size);
        scsi_req_data(&r->req, r->qiov.size);
    }

done:
    scsi_req_unref(&r->req);
}

static void scsi_write_complete(void *opaque, int ret)
{
    UfsSCSIReq *r = (UfsSCSIReq *)opaque;
    UfsLu *lu = DO_UPCAST(UfsLu, qdev, r->req.dev);

    assert(r->req.aiocb != NULL);
    r->req.aiocb = NULL;

    aio_context_acquire(blk_get_aio_context(lu->qdev.conf.blk));
    if (ret < 0) {
        block_acct_failed(blk_get_stats(lu->qdev.conf.blk), &r->acct);
    } else {
        block_acct_done(blk_get_stats(lu->qdev.conf.blk), &r->acct);
    }
    scsi_write_complete_noio(r, ret);
    aio_context_release(blk_get_aio_context(lu->qdev.conf.blk));
}

static BlockAIOCB *scsi_dma_writev(int64_t offset, QEMUIOVector *iov,
                                   BlockCompletionFunc *cb, void *cb_opaque,
                                   void *opaque)
{
    UfsSCSIReq *r = opaque;
    UfsLu *lu = DO_UPCAST(UfsLu, qdev, r->req.dev);
    return blk_aio_pwritev(lu->qdev.conf.blk, offset, iov, 0, cb, cb_opaque);
}

static void scsi_write_data(SCSIRequest *req)
{
    UfsSCSIReq *r = DO_UPCAST(UfsSCSIReq, req, req);
    UfsLu *lu = DO_UPCAST(UfsLu, qdev, r->req.dev);

    /* No data transfer may already be in progress */
    assert(r->req.aiocb == NULL);

    /* The request is used as the AIO opaque value, so add a ref.  */
    scsi_req_ref(&r->req);
    if (r->req.cmd.mode != SCSI_XFER_TO_DEV) {
        trace_ufs_scsi_write_data_invalid();
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

    if (r->req.sg) {
        dma_acct_start(lu->qdev.conf.blk, &r->acct, r->req.sg,
                       BLOCK_ACCT_WRITE);
        r->req.residual -= r->req.sg->size;
        r->req.aiocb = dma_blk_io(
            blk_get_aio_context(lu->qdev.conf.blk), r->req.sg,
            r->sector << BDRV_SECTOR_BITS, BDRV_SECTOR_SIZE, scsi_dma_writev, r,
            scsi_dma_complete, r, DMA_DIRECTION_TO_DEVICE);
    } else {
        block_acct_start(blk_get_stats(lu->qdev.conf.blk), &r->acct,
                         r->qiov.size, BLOCK_ACCT_WRITE);
        r->req.aiocb = scsi_dma_writev(r->sector << BDRV_SECTOR_BITS, &r->qiov,
                                       scsi_write_complete, r, r);
    }
}

static const SCSIReqOps ufs_scsi_emulate_reqops = {
    .size = sizeof(UfsSCSIReq),
    .free_req = ufs_scsi_free_request,
    .send_command = ufs_scsi_emulate_command,
    .read_data = ufs_scsi_emulate_read_data,
    .write_data = ufs_scsi_emulate_write_data,
    .get_buf = ufs_scsi_get_buf,
};

static const SCSIReqOps ufs_scsi_dma_reqops = {
    .size = sizeof(UfsSCSIReq),
    .free_req = ufs_scsi_free_request,
    .send_command = ufs_scsi_dma_command,
    .read_data = scsi_read_data,
    .write_data = scsi_write_data,
    .get_buf = ufs_scsi_get_buf,
};

/*
 * Following commands are not yet supported
 * PRE_FETCH(10),
 * UNMAP,
 * WRITE_BUFFER, READ_BUFFER,
 * SECURITY_PROTOCOL_IN, SECURITY_PROTOCOL_OUT
 */
static const SCSIReqOps *const ufs_scsi_reqops_dispatch[256] = {
    [TEST_UNIT_READY] = &ufs_scsi_emulate_reqops,
    [INQUIRY] = &ufs_scsi_emulate_reqops,
    [MODE_SENSE_10] = &ufs_scsi_emulate_reqops,
    [START_STOP] = &ufs_scsi_emulate_reqops,
    [READ_CAPACITY_10] = &ufs_scsi_emulate_reqops,
    [REQUEST_SENSE] = &ufs_scsi_emulate_reqops,
    [SYNCHRONIZE_CACHE] = &ufs_scsi_emulate_reqops,
    [MODE_SELECT_10] = &ufs_scsi_emulate_reqops,
    [VERIFY_10] = &ufs_scsi_emulate_reqops,
    [FORMAT_UNIT] = &ufs_scsi_emulate_reqops,
    [SERVICE_ACTION_IN_16] = &ufs_scsi_emulate_reqops,
    [SEND_DIAGNOSTIC] = &ufs_scsi_emulate_reqops,

    [READ_6] = &ufs_scsi_dma_reqops,
    [READ_10] = &ufs_scsi_dma_reqops,
    [WRITE_6] = &ufs_scsi_dma_reqops,
    [WRITE_10] = &ufs_scsi_dma_reqops,
};

static SCSIRequest *scsi_new_request(SCSIDevice *dev, uint32_t tag,
                                     uint32_t lun, uint8_t *buf,
                                     void *hba_private)
{
    UfsLu *lu = DO_UPCAST(UfsLu, qdev, dev);
    SCSIRequest *req;
    const SCSIReqOps *ops;
    uint8_t command;

    command = buf[0];
    ops = ufs_scsi_reqops_dispatch[command];
    if (!ops) {
        ops = &ufs_scsi_emulate_reqops;
    }
    req = scsi_req_alloc(ops, &lu->qdev, tag, lun, hba_private);

    return req;
}

static Property ufs_lu_props[] = {
    DEFINE_PROP_DRIVE("drive", UfsLu, qdev.conf.blk),
    DEFINE_PROP_END_OF_LIST(),
};

static bool ufs_lu_brdv_init(UfsLu *lu, Error **errp)
{
    SCSIDevice *dev = &lu->qdev;
    bool read_only;

    if (!lu->qdev.conf.blk) {
        error_setg(errp, "drive property not set");
        return false;
    }

    if (!blkconf_blocksizes(&lu->qdev.conf, errp)) {
        return false;
    }

    if (blk_get_aio_context(lu->qdev.conf.blk) != qemu_get_aio_context() &&
        !lu->qdev.hba_supports_iothread) {
        error_setg(errp, "HBA does not support iothreads");
        return false;
    }

    read_only = !blk_supports_write_perm(lu->qdev.conf.blk);

    if (!blkconf_apply_backend_options(&dev->conf, read_only,
                                       dev->type == TYPE_DISK, errp)) {
        return false;
    }

    if (blk_is_sg(lu->qdev.conf.blk)) {
        error_setg(errp, "unwanted /dev/sg*");
        return false;
    }

    blk_iostatus_enable(lu->qdev.conf.blk);
    return true;
}

static bool ufs_add_lu(UfsHc *u, UfsLu *lu, Error **errp)
{
    BlockBackend *blk = lu->qdev.conf.blk;
    int64_t brdv_len = blk_getlength(blk);
    uint64_t raw_dev_cap =
        be64_to_cpu(u->geometry_desc.total_raw_device_capacity);

    if (u->device_desc.number_lu >= UFS_MAX_LUS) {
        error_setg(errp, "ufs host controller has too many logical units.");
        return false;
    }

    if (u->lus[lu->lun] != NULL) {
        error_setg(errp, "ufs logical unit %d already exists.", lu->lun);
        return false;
    }

    u->lus[lu->lun] = lu;
    u->device_desc.number_lu++;
    raw_dev_cap += (brdv_len >> UFS_GEOMETRY_CAPACITY_SHIFT);
    u->geometry_desc.total_raw_device_capacity = cpu_to_be64(raw_dev_cap);
    return true;
}

static inline uint8_t ufs_log2(uint64_t input)
{
    int log = 0;
    while (input >>= 1) {
        log++;
    }
    return log;
}

static void ufs_init_lu(UfsLu *lu)
{
    BlockBackend *blk = lu->qdev.conf.blk;
    int64_t brdv_len = blk_getlength(blk);

    lu->lun = lu->qdev.lun;
    memset(&lu->unit_desc, 0, sizeof(lu->unit_desc));
    lu->unit_desc.length = sizeof(UnitDescriptor);
    lu->unit_desc.descriptor_idn = UFS_QUERY_DESC_IDN_UNIT;
    lu->unit_desc.lu_enable = 0x01;
    lu->unit_desc.logical_block_size = ufs_log2(lu->qdev.blocksize);
    lu->unit_desc.unit_index = lu->qdev.lun;
    lu->unit_desc.logical_block_count =
        cpu_to_be64(brdv_len / (1 << lu->unit_desc.logical_block_size));
}

static bool ufs_lu_check_constraints(UfsLu *lu, Error **errp)
{
    if (!lu->qdev.conf.blk) {
        error_setg(errp, "drive property not set");
        return false;
    }

    if (lu->qdev.channel != 0) {
        error_setg(errp, "ufs logical unit does not support channel");
        return false;
    }

    if (lu->qdev.lun >= UFS_MAX_LUS) {
        error_setg(errp, "lun must be between 1 and %d", UFS_MAX_LUS - 1);
        return false;
    }

    return true;
}

static void ufs_lu_realize(SCSIDevice *dev, Error **errp)
{
    UfsLu *lu = DO_UPCAST(UfsLu, qdev, dev);
    BusState *s = qdev_get_parent_bus(&dev->qdev);
    UfsHc *u = UFS(s->parent);
    AioContext *ctx = NULL;
    uint64_t nb_sectors, nb_blocks;

    if (!ufs_lu_check_constraints(lu, errp)) {
        return;
    }

    if (lu->qdev.conf.blk) {
        ctx = blk_get_aio_context(lu->qdev.conf.blk);
        aio_context_acquire(ctx);
        if (!blkconf_blocksizes(&lu->qdev.conf, errp)) {
            goto out;
        }
    }
    lu->qdev.blocksize = UFS_BLOCK_SIZE;
    blk_get_geometry(lu->qdev.conf.blk, &nb_sectors);
    nb_blocks = nb_sectors / (lu->qdev.blocksize / BDRV_SECTOR_SIZE);
    if (nb_blocks > UINT32_MAX) {
        nb_blocks = UINT32_MAX;
    }
    lu->qdev.max_lba = nb_blocks;
    lu->qdev.type = TYPE_DISK;

    ufs_init_lu(lu);
    if (!ufs_add_lu(u, lu, errp)) {
        goto out;
    }

    ufs_lu_brdv_init(lu, errp);
out:
    if (ctx) {
        aio_context_release(ctx);
    }
}

static void ufs_lu_unrealize(SCSIDevice *dev)
{
    UfsLu *lu = DO_UPCAST(UfsLu, qdev, dev);

    blk_drain(lu->qdev.conf.blk);
}

static void ufs_wlu_realize(DeviceState *qdev, Error **errp)
{
    UfsWLu *wlu = UFSWLU(qdev);
    SCSIDevice *dev = &wlu->qdev;

    if (!is_wlun(dev->lun)) {
        error_setg(errp, "not well-known logical unit number");
        return;
    }

    QTAILQ_INIT(&dev->requests);
}

static void ufs_lu_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    SCSIDeviceClass *sc = SCSI_DEVICE_CLASS(oc);

    sc->realize = ufs_lu_realize;
    sc->unrealize = ufs_lu_unrealize;
    sc->alloc_req = scsi_new_request;
    dc->bus_type = TYPE_UFS_BUS;
    device_class_set_props(dc, ufs_lu_props);
    dc->desc = "Virtual UFS logical unit";
}

static void ufs_wlu_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    SCSIDeviceClass *sc = SCSI_DEVICE_CLASS(oc);

    /*
     * The realize() function of TYPE_SCSI_DEVICE causes a segmentation fault
     * if a block drive does not exist. Define a new realize function for
     * well-known LUs that do not have a block drive.
     */
    dc->realize = ufs_wlu_realize;
    sc->alloc_req = scsi_new_request;
    dc->bus_type = TYPE_UFS_BUS;
    dc->desc = "Virtual UFS well-known logical unit";
}

static const TypeInfo ufs_lu_info = {
    .name = TYPE_UFS_LU,
    .parent = TYPE_SCSI_DEVICE,
    .class_init = ufs_lu_class_init,
    .instance_size = sizeof(UfsLu),
};

static const TypeInfo ufs_wlu_info = {
    .name = TYPE_UFS_WLU,
    .parent = TYPE_SCSI_DEVICE,
    .class_init = ufs_wlu_class_init,
    .instance_size = sizeof(UfsWLu),
};

static void ufs_lu_register_types(void)
{
    type_register_static(&ufs_lu_info);
    type_register_static(&ufs_wlu_info);
}

type_init(ufs_lu_register_types)
