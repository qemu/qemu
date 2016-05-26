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
#include "qemu-common.h"
#include "qemu/error-report.h"
#include "hw/scsi/scsi.h"
#include "sysemu/block-backend.h"
#include "sysemu/blockdev.h"

#ifdef __linux__

//#define DEBUG_SCSI

#ifdef DEBUG_SCSI
#define DPRINTF(fmt, ...) \
do { printf("scsi-generic: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) do {} while(0)
#endif

#define BADF(fmt, ...) \
do { fprintf(stderr, "scsi-generic: " fmt , ## __VA_ARGS__); } while (0)

#include <scsi/sg.h>
#include "block/scsi.h"

#define SG_ERR_DRIVER_TIMEOUT  0x06
#define SG_ERR_DRIVER_SENSE    0x08

#define SG_ERR_DID_OK          0x00
#define SG_ERR_DID_NO_CONNECT  0x01
#define SG_ERR_DID_BUS_BUSY    0x02
#define SG_ERR_DID_TIME_OUT    0x03

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

    assert(r->req.aiocb == NULL);

    if (r->req.io_canceled) {
        scsi_req_cancel_complete(&r->req);
        goto done;
    }
    if (r->io_header.driver_status & SG_ERR_DRIVER_SENSE) {
        r->req.sense_len = r->io_header.sb_len_wr;
    }

    if (ret != 0) {
        switch (ret) {
        case -EDOM:
            status = TASK_SET_FULL;
            break;
        case -ENOMEM:
            status = CHECK_CONDITION;
            scsi_req_build_sense(&r->req, SENSE_CODE(TARGET_FAILURE));
            break;
        default:
            status = CHECK_CONDITION;
            scsi_req_build_sense(&r->req, SENSE_CODE(IO_ERROR));
            break;
        }
    } else {
        if (r->io_header.host_status == SG_ERR_DID_NO_CONNECT ||
            r->io_header.host_status == SG_ERR_DID_BUS_BUSY ||
            r->io_header.host_status == SG_ERR_DID_TIME_OUT ||
            (r->io_header.driver_status & SG_ERR_DRIVER_TIMEOUT)) {
            status = BUSY;
            BADF("Driver Timeout\n");
        } else if (r->io_header.host_status) {
            status = CHECK_CONDITION;
            scsi_req_build_sense(&r->req, SENSE_CODE(I_T_NEXUS_LOSS));
        } else if (r->io_header.status) {
            status = r->io_header.status;
        } else if (r->io_header.driver_status & SG_ERR_DRIVER_SENSE) {
            status = CHECK_CONDITION;
        } else {
            status = GOOD;
        }
    }
    DPRINTF("Command complete 0x%p tag=0x%x status=%d\n",
            r, r->req.tag, status);

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
    r->io_header.interface_id = 'S';
    r->io_header.dxfer_direction = direction;
    r->io_header.dxferp = r->buf;
    r->io_header.dxfer_len = r->buflen;
    r->io_header.cmdp = r->req.cmd.buf;
    r->io_header.cmd_len = r->req.cmd.len;
    r->io_header.mx_sb_len = sizeof(r->req.sense);
    r->io_header.sbp = r->req.sense;
    r->io_header.timeout = MAX_UINT;
    r->io_header.usr_ptr = r;
    r->io_header.flags |= SG_FLAG_DIRECT_IO;

    r->req.aiocb = blk_aio_ioctl(blk, SG_IO, &r->io_header, complete, r);
    if (r->req.aiocb == NULL) {
        return -EIO;
    }

    return 0;
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
    DPRINTF("Data ready tag=0x%x len=%d\n", r->req.tag, len);

    r->len = -1;
    if (len == 0) {
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
    blk_set_guest_block_size(s->conf.blk, s->blocksize);

    /* Patch MODE SENSE device specific parameters if the BDS is opened
     * readonly.
     */
    if ((s->type == TYPE_DISK || s->type == TYPE_TAPE) &&
        blk_is_read_only(s->conf.blk) &&
        (r->req.cmd.buf[0] == MODE_SENSE ||
         r->req.cmd.buf[0] == MODE_SENSE_10) &&
        (r->req.cmd.buf[1] & 0x8) == 0) {
        if (r->req.cmd.buf[0] == MODE_SENSE) {
            r->buf[2] |= 0x80;
        } else  {
            r->buf[3] |= 0x80;
        }
    }
    if (s->type == TYPE_DISK &&
        r->req.cmd.buf[0] == INQUIRY &&
        r->req.cmd.buf[2] == 0xb0) {
        uint32_t max_xfer_len = blk_get_max_transfer_length(s->conf.blk);
        if (max_xfer_len) {
            stl_be_p(&r->buf[8], max_xfer_len);
            /* Also take care of the opt xfer len. */
            if (ldl_be_p(&r->buf[12]) > max_xfer_len) {
                stl_be_p(&r->buf[12], max_xfer_len);
            }
        }
    }
    scsi_req_data(&r->req, len);
    scsi_req_unref(&r->req);
}

/* Read more data from scsi device into buffer.  */
static void scsi_read_data(SCSIRequest *req)
{
    SCSIGenericReq *r = DO_UPCAST(SCSIGenericReq, req, req);
    SCSIDevice *s = r->req.dev;
    int ret;

    DPRINTF("scsi_read_data 0x%x\n", req->tag);

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

    DPRINTF("scsi_write_complete() ret = %d\n", ret);

    assert(r->req.aiocb != NULL);
    r->req.aiocb = NULL;

    if (ret || r->req.io_canceled) {
        scsi_command_complete_noio(r, ret);
        return;
    }

    if (r->req.cmd.buf[0] == MODE_SELECT && r->req.cmd.buf[4] == 12 &&
        s->type == TYPE_TAPE) {
        s->blocksize = (r->buf[9] << 16) | (r->buf[10] << 8) | r->buf[11];
        DPRINTF("block size %d\n", s->blocksize);
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

    DPRINTF("scsi_write_data 0x%x\n", req->tag);
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

/* Execute a scsi command.  Returns the length of the data expected by the
   command.  This will be Positive for data transfers from the device
   (eg. disk reads), negative for transfers to the device (eg. disk writes),
   and zero if the command does not transfer any data.  */

static int32_t scsi_send_command(SCSIRequest *req, uint8_t *cmd)
{
    SCSIGenericReq *r = DO_UPCAST(SCSIGenericReq, req, req);
    SCSIDevice *s = r->req.dev;
    int ret;

#ifdef DEBUG_SCSI
    {
        int i;
        for (i = 1; i < r->req.cmd.len; i++) {
            printf(" 0x%02x", cmd[i]);
        }
        printf("\n");
    }
#endif

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
            char c = toupper(p[i]);
            c -= (c >= '0' && c <= '9' ? '0' : 'A' - 10);
            *p_wwn = (*p_wwn << 4) | c;
        }
        return 0;
    }

    return -EINVAL;
}

void scsi_generic_read_device_identification(SCSIDevice *s)
{
    uint8_t cmd[6];
    uint8_t buf[250];
    uint8_t sensebuf[8];
    sg_io_hdr_t io_header;
    int ret;
    int i, len;

    memset(cmd, 0, sizeof(cmd));
    memset(buf, 0, sizeof(buf));
    cmd[0] = INQUIRY;
    cmd[1] = 1;
    cmd[2] = 0x83;
    cmd[4] = sizeof(buf);

    memset(&io_header, 0, sizeof(io_header));
    io_header.interface_id = 'S';
    io_header.dxfer_direction = SG_DXFER_FROM_DEV;
    io_header.dxfer_len = sizeof(buf);
    io_header.dxferp = buf;
    io_header.cmdp = cmd;
    io_header.cmd_len = sizeof(cmd);
    io_header.mx_sb_len = sizeof(sensebuf);
    io_header.sbp = sensebuf;
    io_header.timeout = 6000; /* XXX */

    ret = blk_ioctl(s->conf.blk, SG_IO, &io_header);
    if (ret < 0 || io_header.driver_status || io_header.host_status) {
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

static int get_stream_blocksize(BlockBackend *blk)
{
    uint8_t cmd[6];
    uint8_t buf[12];
    uint8_t sensebuf[8];
    sg_io_hdr_t io_header;
    int ret;

    memset(cmd, 0, sizeof(cmd));
    memset(buf, 0, sizeof(buf));
    cmd[0] = MODE_SENSE;
    cmd[4] = sizeof(buf);

    memset(&io_header, 0, sizeof(io_header));
    io_header.interface_id = 'S';
    io_header.dxfer_direction = SG_DXFER_FROM_DEV;
    io_header.dxfer_len = sizeof(buf);
    io_header.dxferp = buf;
    io_header.cmdp = cmd;
    io_header.cmd_len = sizeof(cmd);
    io_header.mx_sb_len = sizeof(sensebuf);
    io_header.sbp = sensebuf;
    io_header.timeout = 6000; /* XXX */

    ret = blk_ioctl(blk, SG_IO, &io_header);
    if (ret < 0 || io_header.driver_status || io_header.host_status) {
        return -1;
    }
    return (buf[9] << 16) | (buf[10] << 8) | buf[11];
}

static void scsi_generic_reset(DeviceState *dev)
{
    SCSIDevice *s = SCSI_DEVICE(dev);

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

    if (blk_get_on_error(s->conf.blk, 0) != BLOCKDEV_ON_ERROR_ENOSPC) {
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
        error_setg(errp, "cannot get SG_IO version number: %s.  "
                         "Is this a SCSI device?",
                         strerror(-rc));
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

    /* define device state */
    s->type = scsiid.scsi_type;
    DPRINTF("device type %d\n", s->type);

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

    DPRINTF("block size %d\n", s->blocksize);

    scsi_generic_read_device_identification(s);
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
    SCSIRequest *req;

    req = scsi_req_alloc(&scsi_generic_req_ops, d, tag, lun, hba_private);
    return req;
}

static Property scsi_generic_properties[] = {
    DEFINE_PROP_DRIVE("drive", SCSIDevice, conf.blk),
    DEFINE_PROP_END_OF_LIST(),
};

static int scsi_generic_parse_cdb(SCSIDevice *dev, SCSICommand *cmd,
                                  uint8_t *buf, void *hba_private)
{
    return scsi_bus_parse_cdb(dev, cmd, buf, hba_private);
}

static void scsi_generic_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SCSIDeviceClass *sc = SCSI_DEVICE_CLASS(klass);

    sc->realize      = scsi_generic_realize;
    sc->alloc_req    = scsi_new_request;
    sc->parse_cdb    = scsi_generic_parse_cdb;
    dc->fw_name = "disk";
    dc->desc = "pass through generic scsi device (/dev/sg*)";
    dc->reset = scsi_generic_reset;
    dc->props = scsi_generic_properties;
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
