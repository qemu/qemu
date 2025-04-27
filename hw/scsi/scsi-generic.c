/*
 * Generic SCSI Device support
 *
 * Copyright (c) 2007 Bull S.A.S.
 * Based on code by Paul Brook
 * Based on code by Fabrice Bellard
 *
 * Written by Laurent Vivier <Laurent.Vivier@bull.net>
 *
 * This code is licensed under the LGPL.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/ctype.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "hw/scsi/scsi.h"
#include "migration/qemu-file-types.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "hw/scsi/emulation.h"
#include "system/block-backend.h"
#include "trace.h"

#ifdef __linux__

#include <scsi/sg.h>
#include "scsi/constants.h"

#ifndef MAX_UINT
#define MAX_UINT ((unsigned int)-1)
#endif

typedef struct SCSIGenericReq {
    SCSIRequest req;
    uint8_t *buf;
    int buflen;
    int len;
    sg_io_hdr_t io_header;
} SCSIGenericReq;

static void scsi_generic_save_request(QEMUFile *f, SCSIRequest *req)
{
    SCSIGenericReq *r = DO_UPCAST(SCSIGenericReq, req, req);

    qemu_put_sbe32s(f, &r->buflen);
    if (r->buflen && r->req.cmd.mode == SCSI_XFER_TO_DEV) {
        assert(!r->req.sg);
        qemu_put_buffer(f, r->buf, r->req.cmd.xfer);
    }
}

static void scsi_generic_load_request(QEMUFile *f, SCSIRequest *req)
{
    SCSIGenericReq *r = DO_UPCAST(SCSIGenericReq, req, req);

    qemu_get_sbe32s(f, &r->buflen);
    if (r->buflen && r->req.cmd.mode == SCSI_XFER_TO_DEV) {
        assert(!r->req.sg);
        qemu_get_buffer(f, r->buf, r->req.cmd.xfer);
    }
}

static void scsi_free_request(SCSIRequest *req)
{
    SCSIGenericReq *r = DO_UPCAST(SCSIGenericReq, req, req);

    g_free(r->buf);
}

/* Helper function for command completion.  */
static void scsi_command_complete_noio(SCSIGenericReq *r, int ret)
{
    int status;
    SCSISense sense;
    sg_io_hdr_t *io_hdr = &r->io_header;

    assert(r->req.aiocb == NULL);

    if (r->req.io_canceled) {
        scsi_req_cancel_complete(&r->req);
        goto done;
    }
    if (ret < 0) {
        status = scsi_sense_from_errno(-ret, &sense);
        if (status == CHECK_CONDITION) {
            scsi_req_build_sense(&r->req, sense);
        }
    } else if (io_hdr->host_status != SCSI_HOST_OK) {
        scsi_req_complete_failed(&r->req, io_hdr->host_status);
        goto done;
    } else if (io_hdr->driver_status & SG_ERR_DRIVER_TIMEOUT) {
        status = BUSY;
    } else {
        status = io_hdr->status;
        if (io_hdr->driver_status & SG_ERR_DRIVER_SENSE) {
            r->req.sense_len = io_hdr->sb_len_wr;
        }
    }
    trace_scsi_generic_command_complete_noio(r, r->req.tag, status);

    scsi_req_complete(&r->req, status);
done:
    scsi_req_unref(&r->req);
}

static void scsi_command_complete(void *opaque, int ret)
{
    SCSIGenericReq *r = (SCSIGenericReq *)opaque;

    assert(r->req.aiocb != NULL);
    r->req.aiocb = NULL;

    scsi_command_complete_noio(r, ret);
}

static int execute_command(BlockBackend *blk,
                           SCSIGenericReq *r, int direction,
                           BlockCompletionFunc *complete)
{
    SCSIDevice *s = r->req.dev;

    r->io_header.interface_id = 'S';
    r->io_header.dxfer_direction = direction;
    r->io_header.dxferp = r->buf;
    r->io_header.dxfer_len = r->buflen;
    r->io_header.cmdp = r->req.cmd.buf;
    r->io_header.cmd_len = r->req.cmd.len;
    r->io_header.mx_sb_len = sizeof(r->req.sense);
    r->io_header.sbp = r->req.sense;
    r->io_header.timeout = s->io_timeout * 1000;
    r->io_header.usr_ptr = r;
    r->io_header.flags |= SG_FLAG_DIRECT_IO;

    trace_scsi_generic_aio_sgio_command(r->req.tag, r->req.cmd.buf[0],
                                        r->io_header.timeout);
    r->req.aiocb = blk_aio_ioctl(blk, SG_IO, &r->io_header, complete, r);
    if (r->req.aiocb == NULL) {
        return -EIO;
    }

    return 0;
}

static uint64_t calculate_max_transfer(SCSIDevice *s)
{
    uint64_t max_transfer = blk_get_max_hw_transfer(s->conf.blk);
    uint32_t max_iov = blk_get_max_hw_iov(s->conf.blk);

    assert(max_transfer);
    max_transfer = MIN_NON_ZERO(max_transfer,
                                max_iov * qemu_real_host_page_size());

    return max_transfer / s->blocksize;
}

static int scsi_handle_inquiry_reply(SCSIGenericReq *r, SCSIDevice *s, int len)
{
    uint8_t page, page_idx;

    /*
     *  EVPD set to zero returns the standard INQUIRY data.
     *
     *  Check if scsi_version is unset (-1) to avoid re-defining it
     *  each time an INQUIRY with standard data is received.
     *  scsi_version is initialized with -1 in scsi_generic_reset
     *  and scsi_disk_reset, making sure that we'll set the
     *  scsi_version after a reset. If the version field of the
     *  INQUIRY response somehow changes after a guest reboot,
     *  we'll be able to keep track of it.
     *
     *  On SCSI-2 and older, first 3 bits of byte 2 is the
     *  ANSI-approved version, while on later versions the
     *  whole byte 2 contains the version. Check if we're dealing
     *  with a newer version and, in that case, assign the
     *  whole byte.
     */
    if (s->scsi_version == -1 && !(r->req.cmd.buf[1] & 0x01)) {
        s->scsi_version = r->buf[2] & 0x07;
        if (s->scsi_version > 2) {
            s->scsi_version = r->buf[2];
        }
    }

    if ((s->type == TYPE_DISK || s->type == TYPE_ZBC) &&
        (r->req.cmd.buf[1] & 0x01)) {
        page = r->req.cmd.buf[2];
        if (page == 0xb0 && r->buflen >= 8) {
            uint8_t buf[16] = {};
            uint8_t buf_used = MIN(r->buflen, 16);
            uint64_t max_transfer = calculate_max_transfer(s);

            memcpy(buf, r->buf, buf_used);
            stl_be_p(&buf[8], max_transfer);
            stl_be_p(&buf[12], MIN_NON_ZERO(max_transfer, ldl_be_p(&buf[12])));
            memcpy(r->buf + 8, buf + 8, buf_used - 8);

        } else if (s->needs_vpd_bl_emulation && page == 0x00 && r->buflen >= 4) {
            /*
             * Now we're capable of supplying the VPD Block Limits
             * response if the hardware can't. Add it in the INQUIRY
             * Supported VPD pages response in case we are using the
             * emulation for this device.
             *
             * This way, the guest kernel will be aware of the support
             * and will use it to proper setup the SCSI device.
             *
             * VPD page numbers must be sorted, so insert 0xb0 at the
             * right place with an in-place insert.  When the while loop
             * begins the device response is at r[0] to r[page_idx - 1].
             */
            page_idx = lduw_be_p(r->buf + 2) + 4;
            page_idx = MIN(page_idx, r->buflen);
            while (page_idx > 4 && r->buf[page_idx - 1] >= 0xb0) {
                if (page_idx < r->buflen) {
                    r->buf[page_idx] = r->buf[page_idx - 1];
                }
                page_idx--;
            }
            if (page_idx < r->buflen) {
                r->buf[page_idx] = 0xb0;
            }
            stw_be_p(r->buf + 2, lduw_be_p(r->buf + 2) + 1);

            if (len < r->buflen) {
                len++;
            }
        }
    }
    return len;
}

static int scsi_generic_emulate_block_limits(SCSIGenericReq *r, SCSIDevice *s)
{
    int len;
    uint8_t buf[64];

    SCSIBlockLimits bl = {
        .max_io_sectors = calculate_max_transfer(s),
    };

    memset(r->buf, 0, r->buflen);
    stb_p(buf, s->type);
    stb_p(buf + 1, 0xb0);
    len = scsi_emulate_block_limits(buf + 4, &bl);
    assert(len <= sizeof(buf) - 4);
    stw_be_p(buf + 2, len);

    memcpy(r->buf, buf, MIN(r->buflen, len + 4));

    r->io_header.sb_len_wr = 0;

    /*
    * We have valid contents in the reply buffer but the
    * io_header can report a sense error coming from
    * the hardware in scsi_command_complete_noio. Clean
    * up the io_header to avoid reporting it.
    */
    r->io_header.driver_status = 0;
    r->io_header.status = 0;

    return r->buflen;
}

static void scsi_read_complete(void * opaque, int ret)
{
    SCSIGenericReq *r = (SCSIGenericReq *)opaque;
    SCSIDevice *s = r->req.dev;
    int len;

    assert(r->req.aiocb != NULL);
    r->req.aiocb = NULL;

    if (ret || r->req.io_canceled) {
        scsi_command_complete_noio(r, ret);
        return;
    }

    len = r->io_header.dxfer_len - r->io_header.resid;
    trace_scsi_generic_read_complete(r->req.tag, len);

    r->len = -1;

    if (r->io_header.driver_status & SG_ERR_DRIVER_SENSE) {
        SCSISense sense =
            scsi_parse_sense_buf(r->req.sense, r->io_header.sb_len_wr);

        /*
         * Check if this is a VPD Block Limits request that
         * resulted in sense error but would need emulation.
         * In this case, emulate a valid VPD response.
         */
        if (sense.key == ILLEGAL_REQUEST &&
            s->needs_vpd_bl_emulation &&
            r->req.cmd.buf[0] == INQUIRY &&
            (r->req.cmd.buf[1] & 0x01) &&
            r->req.cmd.buf[2] == 0xb0) {
            len = scsi_generic_emulate_block_limits(r, s);
            /*
             * It's okay to jup to req_complete: no need to
             * let scsi_handle_inquiry_reply handle an
             * INQUIRY VPD BL request we created manually.
             */
        }
        if (sense.key) {
            goto req_complete;
        }
    }

    if (r->io_header.host_status != SCSI_HOST_OK ||
        (r->io_header.driver_status & SG_ERR_DRIVER_TIMEOUT) ||
        r->io_header.status != GOOD ||
        len == 0) {
        scsi_command_complete_noio(r, 0);
        return;
    }

    /* Snoop READ CAPACITY output to set the blocksize.  */
    if (r->req.cmd.buf[0] == READ_CAPACITY_10 &&
        (ldl_be_p(&r->buf[0]) != 0xffffffffU || s->max_lba == 0)) {
        s->blocksize = ldl_be_p(&r->buf[4]);
        s->max_lba = ldl_be_p(&r->buf[0]) & 0xffffffffULL;
    } else if (r->req.cmd.buf[0] == SERVICE_ACTION_IN_16 &&
               (r->req.cmd.buf[1] & 31) == SAI_READ_CAPACITY_16) {
        s->blocksize = ldl_be_p(&r->buf[8]);
        s->max_lba = ldq_be_p(&r->buf[0]);
    }

    /*
     * Patch MODE SENSE device specific parameters if the BDS is opened
     * readonly.
     */
    if ((s->type == TYPE_DISK || s->type == TYPE_TAPE || s->type == TYPE_ZBC) &&
        !blk_is_writable(s->conf.blk) &&
        (r->req.cmd.buf[0] == MODE_SENSE ||
         r->req.cmd.buf[0] == MODE_SENSE_10) &&
        (r->req.cmd.buf[1] & 0x8) == 0) {
        if (r->req.cmd.buf[0] == MODE_SENSE) {
            r->buf[2] |= 0x80;
        } else  {
            r->buf[3] |= 0x80;
        }
    }
    if (r->req.cmd.buf[0] == INQUIRY) {
        len = scsi_handle_inquiry_reply(r, s, len);
    }

req_complete:
    scsi_req_data(&r->req, len);
    scsi_req_unref(&r->req);
}

/* Read more data from scsi device into buffer.  */
static void scsi_read_data(SCSIRequest *req)
{
    SCSIGenericReq *r = DO_UPCAST(SCSIGenericReq, req, req);
    SCSIDevice *s = r->req.dev;
    int ret;

    trace_scsi_generic_read_data(req->tag);

    /* The request is used as the AIO opaque value, so add a ref.  */
    scsi_req_ref(&r->req);
    if (r->len == -1) {
        scsi_command_complete_noio(r, 0);
        return;
    }

    ret = execute_command(s->conf.blk, r, SG_DXFER_FROM_DEV,
                          scsi_read_complete);
    if (ret < 0) {
        scsi_command_complete_noio(r, ret);
    }
}

static void scsi_write_complete(void * opaque, int ret)
{
    SCSIGenericReq *r = (SCSIGenericReq *)opaque;
    SCSIDevice *s = r->req.dev;

    trace_scsi_generic_write_complete(ret);

    assert(r->req.aiocb != NULL);
    r->req.aiocb = NULL;

    if (ret || r->req.io_canceled) {
        scsi_command_complete_noio(r, ret);
        return;
    }

    if (r->req.cmd.buf[0] == MODE_SELECT && r->req.cmd.buf[4] == 12 &&
        s->type == TYPE_TAPE) {
        s->blocksize = (r->buf[9] << 16) | (r->buf[10] << 8) | r->buf[11];
        trace_scsi_generic_write_complete_blocksize(s->blocksize);
    }

    scsi_command_complete_noio(r, ret);
}

/* Write data to a scsi device.  Returns nonzero on failure.
   The transfer may complete asynchronously.  */
static void scsi_write_data(SCSIRequest *req)
{
    SCSIGenericReq *r = DO_UPCAST(SCSIGenericReq, req, req);
    SCSIDevice *s = r->req.dev;
    int ret;

    trace_scsi_generic_write_data(req->tag);
    if (r->len == 0) {
        r->len = r->buflen;
        scsi_req_data(&r->req, r->len);
        return;
    }

    /* The request is used as the AIO opaque value, so add a ref.  */
    scsi_req_ref(&r->req);
    ret = execute_command(s->conf.blk, r, SG_DXFER_TO_DEV, scsi_write_complete);
    if (ret < 0) {
        scsi_command_complete_noio(r, ret);
    }
}

/* Return a pointer to the data buffer.  */
static uint8_t *scsi_get_buf(SCSIRequest *req)
{
    SCSIGenericReq *r = DO_UPCAST(SCSIGenericReq, req, req);

    return r->buf;
}

static void scsi_generic_command_dump(uint8_t *cmd, int len)
{
    int i;
    char *line_buffer, *p;

    line_buffer = g_malloc(len * 5 + 1);

    for (i = 0, p = line_buffer; i < len; i++) {
        p += sprintf(p, " 0x%02x", cmd[i]);
    }
    trace_scsi_generic_send_command(line_buffer);

    g_free(line_buffer);
}

/* Execute a scsi command.  Returns the length of the data expected by the
   command.  This will be Positive for data transfers from the device
   (eg. disk reads), negative for transfers to the device (eg. disk writes),
   and zero if the command does not transfer any data.  */

static int32_t scsi_send_command(SCSIRequest *req, uint8_t *cmd)
{
    SCSIGenericReq *r = DO_UPCAST(SCSIGenericReq, req, req);
    SCSIDevice *s = r->req.dev;
    int ret;

    if (trace_event_get_state_backends(TRACE_SCSI_GENERIC_SEND_COMMAND)) {
        scsi_generic_command_dump(cmd, r->req.cmd.len);
    }

    if (r->req.cmd.xfer == 0) {
        g_free(r->buf);
        r->buflen = 0;
        r->buf = NULL;
        /* The request is used as the AIO opaque value, so add a ref.  */
        scsi_req_ref(&r->req);
        ret = execute_command(s->conf.blk, r, SG_DXFER_NONE,
                              scsi_command_complete);
        if (ret < 0) {
            scsi_command_complete_noio(r, ret);
            return 0;
        }
        return 0;
    }

    if (r->buflen != r->req.cmd.xfer) {
        g_free(r->buf);
        r->buf = g_malloc(r->req.cmd.xfer);
        r->buflen = r->req.cmd.xfer;
    }

    memset(r->buf, 0, r->buflen);
    r->len = r->req.cmd.xfer;
    if (r->req.cmd.mode == SCSI_XFER_TO_DEV) {
        r->len = 0;
        return -r->req.cmd.xfer;
    } else {
        return r->req.cmd.xfer;
    }
}

static int read_naa_id(const uint8_t *p, uint64_t *p_wwn)
{
    int i;

    if ((p[1] & 0xF) == 3) {
        /* NAA designator type */
        if (p[3] != 8) {
            return -EINVAL;
        }
        *p_wwn = ldq_be_p(p + 4);
        return 0;
    }

    if ((p[1] & 0xF) == 8) {
        /* SCSI name string designator type */
        if (p[3] < 20 || memcmp(&p[4], "naa.", 4)) {
            return -EINVAL;
        }
        if (p[3] > 20 && p[24] != ',') {
            return -EINVAL;
        }
        *p_wwn = 0;
        for (i = 8; i < 24; i++) {
            char c = qemu_toupper(p[i]);
            c -= (c >= '0' && c <= '9' ? '0' : 'A' - 10);
            *p_wwn = (*p_wwn << 4) | c;
        }
        return 0;
    }

    return -EINVAL;
}

int scsi_SG_IO_FROM_DEV(BlockBackend *blk, uint8_t *cmd, uint8_t cmd_size,
                        uint8_t *buf, uint8_t buf_size, uint32_t timeout)
{
    sg_io_hdr_t io_header;
    uint8_t sensebuf[8];
    int ret;

    memset(&io_header, 0, sizeof(io_header));
    io_header.interface_id = 'S';
    io_header.dxfer_direction = SG_DXFER_FROM_DEV;
    io_header.dxfer_len = buf_size;
    io_header.dxferp = buf;
    io_header.cmdp = cmd;
    io_header.cmd_len = cmd_size;
    io_header.mx_sb_len = sizeof(sensebuf);
    io_header.sbp = sensebuf;
    io_header.timeout = timeout * 1000;

    trace_scsi_generic_ioctl_sgio_command(cmd[0], io_header.timeout);
    ret = blk_ioctl(blk, SG_IO, &io_header);
    if (ret < 0 || io_header.status ||
        io_header.driver_status || io_header.host_status) {
        trace_scsi_generic_ioctl_sgio_done(cmd[0], ret, io_header.status,
                                           io_header.host_status);
        return -1;
    }
    return 0;
}

/*
 * Executes an INQUIRY request with EVPD set to retrieve the
 * available VPD pages of the device. If the device does
 * not support the Block Limits page (page 0xb0), set
 * the needs_vpd_bl_emulation flag for future use.
 */
static void scsi_generic_set_vpd_bl_emulation(SCSIDevice *s)
{
    uint8_t cmd[6];
    uint8_t buf[250];
    uint8_t page_len;
    int ret, i;

    memset(cmd, 0, sizeof(cmd));
    memset(buf, 0, sizeof(buf));
    cmd[0] = INQUIRY;
    cmd[1] = 1;
    cmd[2] = 0x00;
    cmd[4] = sizeof(buf);

    ret = scsi_SG_IO_FROM_DEV(s->conf.blk, cmd, sizeof(cmd),
                              buf, sizeof(buf), s->io_timeout);
    if (ret < 0) {
        /*
         * Do not assume anything if we can't retrieve the
         * INQUIRY response to assert the VPD Block Limits
         * support.
         */
        s->needs_vpd_bl_emulation = false;
        return;
    }

    page_len = buf[3];
    for (i = 4; i < MIN(sizeof(buf), page_len + 4); i++) {
        if (buf[i] == 0xb0) {
            s->needs_vpd_bl_emulation = false;
            return;
        }
    }
    s->needs_vpd_bl_emulation = true;
}

static void scsi_generic_read_device_identification(SCSIDevice *s)
{
    uint8_t cmd[6];
    uint8_t buf[250];
    int ret;
    int i, len;

    memset(cmd, 0, sizeof(cmd));
    memset(buf, 0, sizeof(buf));
    cmd[0] = INQUIRY;
    cmd[1] = 1;
    cmd[2] = 0x83;
    cmd[4] = sizeof(buf);

    ret = scsi_SG_IO_FROM_DEV(s->conf.blk, cmd, sizeof(cmd),
                              buf, sizeof(buf), s->io_timeout);
    if (ret < 0) {
        return;
    }

    len = MIN((buf[2] << 8) | buf[3], sizeof(buf) - 4);
    for (i = 0; i + 3 <= len; ) {
        const uint8_t *p = &buf[i + 4];
        uint64_t wwn;

        if (i + (p[3] + 4) > len) {
            break;
        }

        if ((p[1] & 0x10) == 0) {
            /* Associated with the logical unit */
            if (read_naa_id(p, &wwn) == 0) {
                s->wwn = wwn;
            }
        } else if ((p[1] & 0x10) == 0x10) {
            /* Associated with the target port */
            if (read_naa_id(p, &wwn) == 0) {
                s->port_wwn = wwn;
            }
        }

        i += p[3] + 4;
    }
}

void scsi_generic_read_device_inquiry(SCSIDevice *s)
{
    scsi_generic_read_device_identification(s);
    if (s->type == TYPE_DISK || s->type == TYPE_ZBC) {
        scsi_generic_set_vpd_bl_emulation(s);
    } else {
        s->needs_vpd_bl_emulation = false;
    }
}

static int get_stream_blocksize(BlockBackend *blk)
{
    uint8_t cmd[6];
    uint8_t buf[12];
    int ret;

    memset(cmd, 0, sizeof(cmd));
    memset(buf, 0, sizeof(buf));
    cmd[0] = MODE_SENSE;
    cmd[4] = sizeof(buf);

    ret = scsi_SG_IO_FROM_DEV(blk, cmd, sizeof(cmd), buf, sizeof(buf), 6);
    if (ret < 0) {
        return -1;
    }

    return (buf[9] << 16) | (buf[10] << 8) | buf[11];
}

static void scsi_generic_reset(DeviceState *dev)
{
    SCSIDevice *s = SCSI_DEVICE(dev);

    s->scsi_version = s->default_scsi_version;
    scsi_device_purge_requests(s, SENSE_CODE(RESET));
}

static void scsi_generic_realize(SCSIDevice *s, Error **errp)
{
    int rc;
    int sg_version;
    struct sg_scsi_id scsiid;

    if (!s->conf.blk) {
        error_setg(errp, "drive property not set");
        return;
    }

    if (blk_get_on_error(s->conf.blk, 0) != BLOCKDEV_ON_ERROR_ENOSPC &&
        blk_get_on_error(s->conf.blk, 0) != BLOCKDEV_ON_ERROR_REPORT) {
        error_setg(errp, "Device doesn't support drive option werror");
        return;
    }
    if (blk_get_on_error(s->conf.blk, 1) != BLOCKDEV_ON_ERROR_REPORT) {
        error_setg(errp, "Device doesn't support drive option rerror");
        return;
    }

    /* check we are using a driver managing SG_IO (version 3 and after */
    rc = blk_ioctl(s->conf.blk, SG_GET_VERSION_NUM, &sg_version);
    if (rc < 0) {
        error_setg_errno(errp, -rc, "cannot get SG_IO version number");
        if (rc != -EPERM) {
            error_append_hint(errp, "Is this a SCSI device?\n");
        }
        return;
    }
    if (sg_version < 30000) {
        error_setg(errp, "scsi generic interface too old");
        return;
    }

    /* get LUN of the /dev/sg? */
    if (blk_ioctl(s->conf.blk, SG_GET_SCSI_ID, &scsiid)) {
        error_setg(errp, "SG_GET_SCSI_ID ioctl failed");
        return;
    }
    if (!blkconf_apply_backend_options(&s->conf,
                                       !blk_supports_write_perm(s->conf.blk),
                                       true, errp)) {
        return;
    }

    /* define device state */
    s->type = scsiid.scsi_type;
    trace_scsi_generic_realize_type(s->type);

    switch (s->type) {
    case TYPE_TAPE:
        s->blocksize = get_stream_blocksize(s->conf.blk);
        if (s->blocksize == -1) {
            s->blocksize = 0;
        }
        break;

        /* Make a guess for block devices, we'll fix it when the guest sends.
         * READ CAPACITY.  If they don't, they likely would assume these sizes
         * anyway. (TODO: they could also send MODE SENSE).
         */
    case TYPE_ROM:
    case TYPE_WORM:
        s->blocksize = 2048;
        break;
    default:
        s->blocksize = 512;
        break;
    }

    trace_scsi_generic_realize_blocksize(s->blocksize);

    /* Only used by scsi-block, but initialize it nevertheless to be clean.  */
    s->default_scsi_version = -1;
    scsi_generic_read_device_inquiry(s);
}

const SCSIReqOps scsi_generic_req_ops = {
    .size         = sizeof(SCSIGenericReq),
    .free_req     = scsi_free_request,
    .send_command = scsi_send_command,
    .read_data    = scsi_read_data,
    .write_data   = scsi_write_data,
    .get_buf      = scsi_get_buf,
    .load_request = scsi_generic_load_request,
    .save_request = scsi_generic_save_request,
};

static SCSIRequest *scsi_new_request(SCSIDevice *d, uint32_t tag, uint32_t lun,
                                     uint8_t *buf, void *hba_private)
{
    return scsi_req_alloc(&scsi_generic_req_ops, d, tag, lun, hba_private);
}

static const Property scsi_generic_properties[] = {
    DEFINE_PROP_DRIVE("drive", SCSIDevice, conf.blk),
    DEFINE_PROP_BOOL("share-rw", SCSIDevice, conf.share_rw, false),
    DEFINE_PROP_UINT32("io_timeout", SCSIDevice, io_timeout,
                       DEFAULT_IO_TIMEOUT),
};

static int scsi_generic_parse_cdb(SCSIDevice *dev, SCSICommand *cmd,
                                  uint8_t *buf, size_t buf_len,
                                  void *hba_private)
{
    return scsi_bus_parse_cdb(dev, cmd, buf, buf_len, hba_private);
}

static void scsi_generic_class_initfn(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SCSIDeviceClass *sc = SCSI_DEVICE_CLASS(klass);

    sc->realize      = scsi_generic_realize;
    sc->alloc_req    = scsi_new_request;
    sc->parse_cdb    = scsi_generic_parse_cdb;
    dc->fw_name = "disk";
    dc->desc = "pass through generic scsi device (/dev/sg*)";
    device_class_set_legacy_reset(dc, scsi_generic_reset);
    device_class_set_props(dc, scsi_generic_properties);
    dc->vmsd  = &vmstate_scsi_device;
}

static const TypeInfo scsi_generic_info = {
    .name          = "scsi-generic",
    .parent        = TYPE_SCSI_DEVICE,
    .instance_size = sizeof(SCSIDevice),
    .class_init    = scsi_generic_class_initfn,
};

static void scsi_generic_register_types(void)
{
    type_register_static(&scsi_generic_info);
}

type_init(scsi_generic_register_types)

#endif /* __linux__ */
