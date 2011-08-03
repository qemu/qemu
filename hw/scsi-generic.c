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
#include "qemu-error.h"
#include "scsi.h"
#include "blockdev.h"

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
#include "scsi-defs.h"

#define SCSI_SENSE_BUF_SIZE 96

#define SG_ERR_DRIVER_TIMEOUT 0x06
#define SG_ERR_DRIVER_SENSE 0x08

#ifndef MAX_UINT
#define MAX_UINT ((unsigned int)-1)
#endif

typedef struct SCSIGenericState SCSIGenericState;

typedef struct SCSIGenericReq {
    SCSIRequest req;
    uint8_t *buf;
    int buflen;
    int len;
    sg_io_hdr_t io_header;
} SCSIGenericReq;

struct SCSIGenericState
{
    SCSIDevice qdev;
    BlockDriverState *bs;
};

static void scsi_free_request(SCSIRequest *req)
{
    SCSIGenericReq *r = DO_UPCAST(SCSIGenericReq, req, req);

    qemu_free(r->buf);
}

/* Helper function for command completion.  */
static void scsi_command_complete(void *opaque, int ret)
{
    int status;
    SCSIGenericReq *r = (SCSIGenericReq *)opaque;

    r->req.aiocb = NULL;
    if (r->io_header.driver_status & SG_ERR_DRIVER_SENSE)
        r->req.sense_len = r->io_header.sb_len_wr;

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
        if (r->io_header.driver_status & SG_ERR_DRIVER_TIMEOUT) {
            status = BUSY;
            BADF("Driver Timeout\n");
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
}

/* Cancel a pending data transfer.  */
static void scsi_cancel_io(SCSIRequest *req)
{
    SCSIGenericReq *r = DO_UPCAST(SCSIGenericReq, req, req);

    DPRINTF("Cancel tag=0x%x\n", req->tag);
    if (r->req.aiocb) {
        bdrv_aio_cancel(r->req.aiocb);
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
        BADF("execute_command: read failed !\n");
        return -ENOMEM;
    }

    return 0;
}

static void scsi_read_complete(void * opaque, int ret)
{
    SCSIGenericReq *r = (SCSIGenericReq *)opaque;
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
        scsi_req_data(&r->req, len);
    }
}

/* Read more data from scsi device into buffer.  */
static void scsi_read_data(SCSIRequest *req)
{
    SCSIGenericReq *r = DO_UPCAST(SCSIGenericReq, req, req);
    SCSIGenericState *s = DO_UPCAST(SCSIGenericState, qdev, r->req.dev);
    int ret;

    DPRINTF("scsi_read_data 0x%x\n", req->tag);
    if (r->len == -1) {
        scsi_command_complete(r, 0);
        return;
    }

    if (r->req.cmd.buf[0] == REQUEST_SENSE) {
        r->io_header.driver_status = 0;
        r->io_header.status = 0;
        r->io_header.dxfer_len =
            scsi_device_get_sense(&s->qdev, r->buf, r->req.cmd.xfer,
                                  (r->req.cmd.buf[1] & 1) == 0);
        r->len = -1;
        DPRINTF("Data ready tag=0x%x len=%d\n", r->req.tag, r->io_header.dxfer_len);
        DPRINTF("Sense: %d %d %d %d %d %d %d %d\n",
                r->buf[0], r->buf[1], r->buf[2], r->buf[3],
                r->buf[4], r->buf[5], r->buf[6], r->buf[7]);
        scsi_req_data(&r->req, r->io_header.dxfer_len);
        /* The sense buffer is cleared when we return GOOD */
        return;
    }

    ret = execute_command(s->bs, r, SG_DXFER_FROM_DEV, scsi_read_complete);
    if (ret < 0) {
        scsi_command_complete(r, ret);
        return;
    }
}

static void scsi_write_complete(void * opaque, int ret)
{
    SCSIGenericReq *r = (SCSIGenericReq *)opaque;
    SCSIGenericState *s = DO_UPCAST(SCSIGenericState, qdev, r->req.dev);

    DPRINTF("scsi_write_complete() ret = %d\n", ret);
    r->req.aiocb = NULL;
    if (ret) {
        DPRINTF("IO error\n");
        scsi_command_complete(r, ret);
        return;
    }

    if (r->req.cmd.buf[0] == MODE_SELECT && r->req.cmd.buf[4] == 12 &&
        s->qdev.type == TYPE_TAPE) {
        s->qdev.blocksize = (r->buf[9] << 16) | (r->buf[10] << 8) | r->buf[11];
        DPRINTF("block size %d\n", s->qdev.blocksize);
    }

    scsi_command_complete(r, ret);
}

/* Write data to a scsi device.  Returns nonzero on failure.
   The transfer may complete asynchronously.  */
static void scsi_write_data(SCSIRequest *req)
{
    SCSIGenericState *s = DO_UPCAST(SCSIGenericState, qdev, req->dev);
    SCSIGenericReq *r = DO_UPCAST(SCSIGenericReq, req, req);
    int ret;

    DPRINTF("scsi_write_data 0x%x\n", req->tag);
    if (r->len == 0) {
        r->len = r->buflen;
        scsi_req_data(&r->req, r->len);
        return;
    }

    ret = execute_command(s->bs, r, SG_DXFER_TO_DEV, scsi_write_complete);
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

static void scsi_req_fixup(SCSIRequest *req)
{
    switch(req->cmd.buf[0]) {
    case WRITE_10:
        req->cmd.buf[1] &= ~0x08;	/* disable FUA */
        break;
    case READ_10:
        req->cmd.buf[1] &= ~0x08;	/* disable FUA */
        break;
    case REWIND:
    case START_STOP:
        if (req->dev->type == TYPE_TAPE) {
            /* force IMMED, otherwise qemu waits end of command */
            req->cmd.buf[1] = 0x01;
        }
        break;
    }
}

/* Execute a scsi command.  Returns the length of the data expected by the
   command.  This will be Positive for data transfers from the device
   (eg. disk reads), negative for transfers to the device (eg. disk writes),
   and zero if the command does not transfer any data.  */

static int32_t scsi_send_command(SCSIRequest *req, uint8_t *cmd)
{
    SCSIGenericState *s = DO_UPCAST(SCSIGenericState, qdev, req->dev);
    SCSIGenericReq *r = DO_UPCAST(SCSIGenericReq, req, req);
    int ret;

    if (cmd[0] != REQUEST_SENSE && req->lun != s->qdev.lun) {
        DPRINTF("Unimplemented LUN %d\n", req->lun);
        scsi_req_build_sense(&r->req, SENSE_CODE(LUN_NOT_SUPPORTED));
        scsi_req_complete(&r->req, CHECK_CONDITION);
        return 0;
    }

    scsi_req_fixup(&r->req);

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
            qemu_free(r->buf);
        r->buflen = 0;
        r->buf = NULL;
        ret = execute_command(s->bs, r, SG_DXFER_NONE, scsi_command_complete);
        if (ret < 0) {
            scsi_command_complete(r, ret);
            return 0;
        }
        return 0;
    }

    if (r->buflen != r->req.cmd.xfer) {
        if (r->buf != NULL)
            qemu_free(r->buf);
        r->buf = qemu_malloc(r->req.cmd.xfer);
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

static int get_blocksize(BlockDriverState *bdrv)
{
    uint8_t cmd[10];
    uint8_t buf[8];
    uint8_t sensebuf[8];
    sg_io_hdr_t io_header;
    int ret;

    memset(cmd, 0, sizeof(cmd));
    memset(buf, 0, sizeof(buf));
    cmd[0] = READ_CAPACITY_10;

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
    if (ret < 0)
        return -1;

    return (buf[4] << 24) | (buf[5] << 16) | (buf[6] << 8) | buf[7];
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
    if (ret < 0)
        return -1;

    return (buf[9] << 16) | (buf[10] << 8) | buf[11];
}

static void scsi_generic_reset(DeviceState *dev)
{
    SCSIGenericState *s = DO_UPCAST(SCSIGenericState, qdev.qdev, dev);

    scsi_device_purge_requests(&s->qdev);
}

static void scsi_destroy(SCSIDevice *d)
{
    SCSIGenericState *s = DO_UPCAST(SCSIGenericState, qdev, d);

    scsi_device_purge_requests(&s->qdev);
    blockdev_mark_auto_del(s->qdev.conf.bs);
}

static int scsi_generic_initfn(SCSIDevice *dev)
{
    SCSIGenericState *s = DO_UPCAST(SCSIGenericState, qdev, dev);
    int sg_version;
    struct sg_scsi_id scsiid;

    if (!s->qdev.conf.bs) {
        error_report("scsi-generic: drive property not set");
        return -1;
    }
    s->bs = s->qdev.conf.bs;

    /* check we are really using a /dev/sg* file */
    if (!bdrv_is_sg(s->bs)) {
        error_report("scsi-generic: not /dev/sg*");
        return -1;
    }

    if (bdrv_get_on_error(s->bs, 0) != BLOCK_ERR_STOP_ENOSPC) {
        error_report("Device doesn't support drive option werror");
        return -1;
    }
    if (bdrv_get_on_error(s->bs, 1) != BLOCK_ERR_REPORT) {
        error_report("Device doesn't support drive option rerror");
        return -1;
    }

    /* check we are using a driver managing SG_IO (version 3 and after */
    if (bdrv_ioctl(s->bs, SG_GET_VERSION_NUM, &sg_version) < 0 ||
        sg_version < 30000) {
        error_report("scsi-generic: scsi generic interface too old");
        return -1;
    }

    /* get LUN of the /dev/sg? */
    if (bdrv_ioctl(s->bs, SG_GET_SCSI_ID, &scsiid)) {
        error_report("scsi-generic: SG_GET_SCSI_ID ioctl failed");
        return -1;
    }

    /* define device state */
    s->qdev.type = scsiid.scsi_type;
    DPRINTF("device type %d\n", s->qdev.type);
    if (s->qdev.type == TYPE_TAPE) {
        s->qdev.blocksize = get_stream_blocksize(s->bs);
        if (s->qdev.blocksize == -1)
            s->qdev.blocksize = 0;
    } else {
        s->qdev.blocksize = get_blocksize(s->bs);
        /* removable media returns 0 if not present */
        if (s->qdev.blocksize <= 0) {
            if (s->qdev.type == TYPE_ROM || s->qdev.type  == TYPE_WORM)
                s->qdev.blocksize = 2048;
            else
                s->qdev.blocksize = 512;
        }
    }
    DPRINTF("block size %d\n", s->qdev.blocksize);
    bdrv_set_removable(s->bs, 0);
    return 0;
}

static SCSIReqOps scsi_generic_req_ops = {
    .size         = sizeof(SCSIGenericReq),
    .free_req     = scsi_free_request,
    .send_command = scsi_send_command,
    .read_data    = scsi_read_data,
    .write_data   = scsi_write_data,
    .cancel_io    = scsi_cancel_io,
    .get_buf      = scsi_get_buf,
};

static SCSIRequest *scsi_new_request(SCSIDevice *d, uint32_t tag, uint32_t lun,
                                     void *hba_private)
{
    SCSIRequest *req;

    req = scsi_req_alloc(&scsi_generic_req_ops, d, tag, lun, hba_private);
    return req;
}

static SCSIDeviceInfo scsi_generic_info = {
    .qdev.name    = "scsi-generic",
    .qdev.desc    = "pass through generic scsi device (/dev/sg*)",
    .qdev.size    = sizeof(SCSIGenericState),
    .qdev.reset   = scsi_generic_reset,
    .init         = scsi_generic_initfn,
    .destroy      = scsi_destroy,
    .alloc_req    = scsi_new_request,
    .qdev.props   = (Property[]) {
        DEFINE_BLOCK_PROPERTIES(SCSIGenericState, qdev.conf),
        DEFINE_PROP_END_OF_LIST(),
    },
};

static void scsi_generic_register_devices(void)
{
    scsi_qdev_register(&scsi_generic_info);
}
device_init(scsi_generic_register_devices)

#endif /* __linux__ */
