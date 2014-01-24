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

#include "qemu-common.h"
#include "qemu/error-report.h"
#include "hw/scsi/scsi.h"
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

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
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
static void scsi_command_complete(void *opaque, int ret)
{
    int status;
    SCSIGenericReq *r = (SCSIGenericReq *)opaque;

    r->req.aiocb = NULL;
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
    if (!r->req.io_canceled) {
        scsi_req_unref(&r->req);
    }
}

/* Cancel a pending data transfer.  */
static void scsi_cancel_io(SCSIRequest *req)
{
    SCSIGenericReq *r = DO_UPCAST(SCSIGenericReq, req, req);

    DPRINTF("Cancel tag=0x%x\n", req->tag);
    if (r->req.aiocb) {
        bdrv_aio_cancel(r->req.aiocb);

        /* This reference was left in by scsi_*_data.  We take ownership of
         * it independent of whether bdrv_aio_cancel completes the request
         * or not.  */
        scsi_req_unref(&r->req);
    }
    r->req.aiocb = NULL;
}

static int execute_command(BlockDriverState *bdrv,
                           SCSIGenericReq *r, int direction,
			   BlockDriverCompletionFunc *complete)
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

    r->req.aiocb = bdrv_aio_ioctl(bdrv, SG_IO, &r->io_header, complete, r);
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

    r->req.aiocb = NULL;
    if (ret) {
        DPRINTF("IO error ret %d\n", ret);
        scsi_command_complete(r, ret);
        return;
    }
    len = r->io_header.dxfer_len - r->io_header.resid;
    DPRINTF("Data ready tag=0x%x len=%d\n", r->req.tag, len);

    r->len = -1;
    if (len == 0) {
        scsi_command_complete(r, 0);
    } else {
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
        bdrv_set_guest_block_size(s->conf.bs, s->blocksize);

        scsi_req_data(&r->req, len);
        if (!r->req.io_canceled) {
            scsi_req_unref(&r->req);
        }
    }
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
        scsi_command_complete(r, 0);
        return;
    }

    ret = execute_command(s->conf.bs, r, SG_DXFER_FROM_DEV, scsi_read_complete);
    if (ret < 0) {
        scsi_command_complete(r, ret);
    }
}

static void scsi_write_complete(void * opaque, int ret)
{
    SCSIGenericReq *r = (SCSIGenericReq *)opaque;
    SCSIDevice *s = r->req.dev;

    DPRINTF("scsi_write_complete() ret = %d\n", ret);
    r->req.aiocb = NULL;
    if (ret) {
        DPRINTF("IO error\n");
        scsi_command_complete(r, ret);
        return;
    }

    if (r->req.cmd.buf[0] == MODE_SELECT && r->req.cmd.buf[4] == 12 &&
        s->type == TYPE_TAPE) {
        s->blocksize = (r->buf[9] << 16) | (r->buf[10] << 8) | r->buf[11];
        DPRINTF("block size %d\n", s->blocksize);
    }

    scsi_command_complete(r, ret);
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
    ret = execute_command(s->conf.bs, r, SG_DXFER_TO_DEV, scsi_write_complete);
    if (ret < 0) {
        scsi_command_complete(r, ret);
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

    DPRINTF("Command: lun=%d tag=0x%x len %zd data=0x%02x", lun, tag,
            r->req.cmd.xfer, cmd[0]);

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
        if (r->buf != NULL)
            g_free(r->buf);
        r->buflen = 0;
        r->buf = NULL;
        /* The request is used as the AIO opaque value, so add a ref.  */
        scsi_req_ref(&r->req);
        ret = execute_command(s->conf.bs, r, SG_DXFER_NONE, scsi_command_complete);
        if (ret < 0) {
            scsi_command_complete(r, ret);
            return 0;
        }
        return 0;
    }

    if (r->buflen != r->req.cmd.xfer) {
        if (r->buf != NULL)
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

static int get_stream_blocksize(BlockDriverState *bdrv)
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

    ret = bdrv_ioctl(bdrv, SG_IO, &io_header);
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

static void scsi_destroy(SCSIDevice *s)
{
    scsi_device_purge_requests(s, SENSE_CODE(NO_SENSE));
    blockdev_mark_auto_del(s->conf.bs);
}

static int scsi_generic_initfn(SCSIDevice *s)
{
    int sg_version;
    struct sg_scsi_id scsiid;

    if (!s->conf.bs) {
        error_report("drive property not set");
        return -1;
    }

    if (bdrv_get_on_error(s->conf.bs, 0) != BLOCKDEV_ON_ERROR_ENOSPC) {
        error_report("Device doesn't support drive option werror");
        return -1;
    }
    if (bdrv_get_on_error(s->conf.bs, 1) != BLOCKDEV_ON_ERROR_REPORT) {
        error_report("Device doesn't support drive option rerror");
        return -1;
    }

    /* check we are using a driver managing SG_IO (version 3 and after */
    if (bdrv_ioctl(s->conf.bs, SG_GET_VERSION_NUM, &sg_version) < 0) {
        error_report("scsi generic interface not supported");
        return -1;
    }
    if (sg_version < 30000) {
        error_report("scsi generic interface too old");
        return -1;
    }

    /* get LUN of the /dev/sg? */
    if (bdrv_ioctl(s->conf.bs, SG_GET_SCSI_ID, &scsiid)) {
        error_report("SG_GET_SCSI_ID ioctl failed");
        return -1;
    }

    /* define device state */
    s->type = scsiid.scsi_type;
    DPRINTF("device type %d\n", s->type);
    if (s->type == TYPE_DISK || s->type == TYPE_ROM) {
        add_boot_device_path(s->conf.bootindex, &s->qdev, NULL);
    }

    switch (s->type) {
    case TYPE_TAPE:
        s->blocksize = get_stream_blocksize(s->conf.bs);
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
    return 0;
}

const SCSIReqOps scsi_generic_req_ops = {
    .size         = sizeof(SCSIGenericReq),
    .free_req     = scsi_free_request,
    .send_command = scsi_send_command,
    .read_data    = scsi_read_data,
    .write_data   = scsi_write_data,
    .cancel_io    = scsi_cancel_io,
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
    DEFINE_PROP_DRIVE("drive", SCSIDevice, conf.bs),
    DEFINE_PROP_INT32("bootindex", SCSIDevice, conf.bootindex, -1),
    DEFINE_PROP_END_OF_LIST(),
};

static void scsi_generic_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SCSIDeviceClass *sc = SCSI_DEVICE_CLASS(klass);

    sc->init         = scsi_generic_initfn;
    sc->destroy      = scsi_destroy;
    sc->alloc_req    = scsi_new_request;
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
