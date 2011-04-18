/*
 * Generic SCSI Device support
 *
 * Copyright (c) 2007 Bull S.A.S.
 * Based on code by Paul Brook
 * Based on code by Fabrice Bellard
 *
 * Written by Laurent Vivier <Laurent.Vivier@bull.net>
 *
 * This code is licenced under the LGPL.
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
    int lun;
    int driver_status;
    uint8_t sensebuf[SCSI_SENSE_BUF_SIZE];
    uint8_t senselen;
};

static SCSIRequest *scsi_new_request(SCSIDevice *d, uint32_t tag, uint32_t lun)
{
    SCSIRequest *req;

    req = scsi_req_alloc(sizeof(SCSIGenericReq), d, tag, lun);
    return req;
}

static void scsi_free_request(SCSIRequest *req)
{
    SCSIGenericReq *r = DO_UPCAST(SCSIGenericReq, req, req);

    qemu_free(r->buf);
}

/* Helper function for command completion.  */
static void scsi_command_complete(void *opaque, int ret)
{
    SCSIGenericReq *r = (SCSIGenericReq *)opaque;
    SCSIGenericState *s = DO_UPCAST(SCSIGenericState, qdev, r->req.dev);

    r->req.aiocb = NULL;
    s->driver_status = r->io_header.driver_status;
    if (s->driver_status & SG_ERR_DRIVER_SENSE)
        s->senselen = r->io_header.sb_len_wr;

    if (ret != 0)
        r->req.status = BUSY;
    else {
        if (s->driver_status & SG_ERR_DRIVER_TIMEOUT) {
            r->req.status = BUSY;
            BADF("Driver Timeout\n");
        } else if (r->io_header.status)
            r->req.status = r->io_header.status;
        else if (s->driver_status & SG_ERR_DRIVER_SENSE)
            r->req.status = CHECK_CONDITION;
        else
            r->req.status = GOOD;
    }
    DPRINTF("Command complete 0x%p tag=0x%x status=%d\n",
            r, r->req.tag, r->req.status);

    scsi_req_complete(&r->req);
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
    scsi_req_dequeue(&r->req);
}

static int execute_command(BlockDriverState *bdrv,
                           SCSIGenericReq *r, int direction,
			   BlockDriverCompletionFunc *complete)
{
    SCSIGenericState *s = DO_UPCAST(SCSIGenericState, qdev, r->req.dev);

    r->io_header.interface_id = 'S';
    r->io_header.dxfer_direction = direction;
    r->io_header.dxferp = r->buf;
    r->io_header.dxfer_len = r->buflen;
    r->io_header.cmdp = r->req.cmd.buf;
    r->io_header.cmd_len = r->req.cmd.len;
    r->io_header.mx_sb_len = sizeof(s->sensebuf);
    r->io_header.sbp = s->sensebuf;
    r->io_header.timeout = MAX_UINT;
    r->io_header.usr_ptr = r;
    r->io_header.flags |= SG_FLAG_DIRECT_IO;

    r->req.aiocb = bdrv_aio_ioctl(bdrv, SG_IO, &r->io_header, complete, r);
    if (r->req.aiocb == NULL) {
        BADF("execute_command: read failed !\n");
        return -1;
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

    if (r->req.cmd.buf[0] == REQUEST_SENSE && s->driver_status & SG_ERR_DRIVER_SENSE)
    {
        s->senselen = MIN(r->len, s->senselen);
        memcpy(r->buf, s->sensebuf, s->senselen);
        r->io_header.driver_status = 0;
        r->io_header.status = 0;
        r->io_header.dxfer_len  = s->senselen;
        r->len = -1;
        DPRINTF("Data ready tag=0x%x len=%d\n", r->req.tag, s->senselen);
        DPRINTF("Sense: %d %d %d %d %d %d %d %d\n",
                r->buf[0], r->buf[1], r->buf[2], r->buf[3],
                r->buf[4], r->buf[5], r->buf[6], r->buf[7]);
        scsi_req_data(&r->req, s->senselen);
        return;
    }

    ret = execute_command(s->bs, r, SG_DXFER_FROM_DEV, scsi_read_complete);
    if (ret == -1) {
        scsi_command_complete(r, -EINVAL);
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
static int scsi_write_data(SCSIRequest *req)
{
    SCSIGenericState *s = DO_UPCAST(SCSIGenericState, qdev, req->dev);
    SCSIGenericReq *r = DO_UPCAST(SCSIGenericReq, req, req);
    int ret;

    DPRINTF("scsi_write_data 0x%x\n", req->tag);
    if (r->len == 0) {
        r->len = r->buflen;
        scsi_req_data(&r->req, r->len);
        return 0;
    }

    ret = execute_command(s->bs, r, SG_DXFER_TO_DEV, scsi_write_complete);
    if (ret == -1) {
        scsi_command_complete(r, -EINVAL);
        return 1;
    }

    return 0;
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
    SCSIBus *bus;
    int ret;

    scsi_req_enqueue(req);
    if (cmd[0] != REQUEST_SENSE &&
        (req->lun != s->lun || (cmd[1] >> 5) != s->lun)) {
        DPRINTF("Unimplemented LUN %d\n", req->lun ? req->lun : cmd[1] >> 5);

        s->sensebuf[0] = 0x70;
        s->sensebuf[1] = 0x00;
        s->sensebuf[2] = ILLEGAL_REQUEST;
        s->sensebuf[3] = 0x00;
        s->sensebuf[4] = 0x00;
        s->sensebuf[5] = 0x00;
        s->sensebuf[6] = 0x00;
        s->senselen = 7;
        s->driver_status = SG_ERR_DRIVER_SENSE;
        bus = scsi_bus_from_device(&s->qdev);
        bus->ops->complete(req, SCSI_REASON_DONE, CHECK_CONDITION);
        return 0;
    }

    if (-1 == scsi_req_parse(&r->req, cmd)) {
        BADF("Unsupported command length, command %x\n", cmd[0]);
        scsi_req_dequeue(&r->req);
        scsi_req_unref(&r->req);
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
        if (ret == -1) {
            scsi_command_complete(r, -EINVAL);
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
    cmd[0] = READ_CAPACITY;

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

static void scsi_generic_purge_requests(SCSIGenericState *s)
{
    SCSIGenericReq *r;

    while (!QTAILQ_EMPTY(&s->qdev.requests)) {
        r = DO_UPCAST(SCSIGenericReq, req, QTAILQ_FIRST(&s->qdev.requests));
        if (r->req.aiocb) {
            bdrv_aio_cancel(r->req.aiocb);
        }
        scsi_req_dequeue(&r->req);
        scsi_req_unref(&r->req);
    }
}

static void scsi_generic_reset(DeviceState *dev)
{
    SCSIGenericState *s = DO_UPCAST(SCSIGenericState, qdev.qdev, dev);

    scsi_generic_purge_requests(s);
}

static void scsi_destroy(SCSIDevice *d)
{
    SCSIGenericState *s = DO_UPCAST(SCSIGenericState, qdev, d);

    scsi_generic_purge_requests(s);
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
    s->lun = scsiid.lun;
    DPRINTF("LUN %d\n", s->lun);
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
    s->driver_status = 0;
    memset(s->sensebuf, 0, sizeof(s->sensebuf));
    bdrv_set_removable(s->bs, 0);
    return 0;
}

static SCSIDeviceInfo scsi_generic_info = {
    .qdev.name    = "scsi-generic",
    .qdev.desc    = "pass through generic scsi device (/dev/sg*)",
    .qdev.size    = sizeof(SCSIGenericState),
    .qdev.reset   = scsi_generic_reset,
    .init         = scsi_generic_initfn,
    .destroy      = scsi_destroy,
    .alloc_req    = scsi_new_request,
    .free_req     = scsi_free_request,
    .send_command = scsi_send_command,
    .read_data    = scsi_read_data,
    .write_data   = scsi_write_data,
    .cancel_io    = scsi_cancel_io,
    .get_buf      = scsi_get_buf,
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
