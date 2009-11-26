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
#include "block.h"
#include "scsi.h"

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
#include <scsi/scsi.h>

#define REWIND 0x01
#define REPORT_DENSITY_SUPPORT 0x44
#define LOAD_UNLOAD 0xa6
#define SET_CD_SPEED 0xbb
#define BLANK 0xa1

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
    DriveInfo *dinfo;
    int type;
    int blocksize;
    int lun;
    int driver_status;
    uint8_t sensebuf[SCSI_SENSE_BUF_SIZE];
    uint8_t senselen;
};

static SCSIGenericReq *scsi_new_request(SCSIDevice *d, uint32_t tag, uint32_t lun)
{
    SCSIRequest *req;

    req = scsi_req_alloc(sizeof(SCSIGenericReq), d, tag, lun);
    return DO_UPCAST(SCSIGenericReq, req, req);
}

static void scsi_remove_request(SCSIGenericReq *r)
{
    qemu_free(r->buf);
    scsi_req_free(&r->req);
}

static SCSIGenericReq *scsi_find_request(SCSIGenericState *s, uint32_t tag)
{
    return DO_UPCAST(SCSIGenericReq, req, scsi_req_find(&s->qdev, tag));
}

/* Helper function for command completion.  */
static void scsi_command_complete(void *opaque, int ret)
{
    SCSIGenericReq *r = (SCSIGenericReq *)opaque;
    SCSIGenericState *s = DO_UPCAST(SCSIGenericState, qdev, r->req.dev);
    uint32_t tag;
    int status;

    s->driver_status = r->io_header.driver_status;
    if (s->driver_status & SG_ERR_DRIVER_SENSE)
        s->senselen = r->io_header.sb_len_wr;

    if (ret != 0)
        status = BUSY << 1;
    else {
        if (s->driver_status & SG_ERR_DRIVER_TIMEOUT) {
            status = BUSY << 1;
            BADF("Driver Timeout\n");
        } else if (r->io_header.status)
            status = r->io_header.status;
        else if (s->driver_status & SG_ERR_DRIVER_SENSE)
            status = CHECK_CONDITION << 1;
        else
            status = GOOD << 1;
    }
    DPRINTF("Command complete 0x%p tag=0x%x status=%d\n",
            r, r->req.tag, status);
    tag = r->req.tag;
    r->req.bus->complete(r->req.bus, SCSI_REASON_DONE, tag, status);
    scsi_remove_request(r);
}

/* Cancel a pending data transfer.  */
static void scsi_cancel_io(SCSIDevice *d, uint32_t tag)
{
    DPRINTF("scsi_cancel_io 0x%x\n", tag);
    SCSIGenericState *s = DO_UPCAST(SCSIGenericState, qdev, d);
    SCSIGenericReq *r;
    DPRINTF("Cancel tag=0x%x\n", tag);
    r = scsi_find_request(s, tag);
    if (r) {
        if (r->req.aiocb)
            bdrv_aio_cancel(r->req.aiocb);
        r->req.aiocb = NULL;
        scsi_remove_request(r);
    }
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

    if (ret) {
        DPRINTF("IO error\n");
        scsi_command_complete(r, ret);
        return;
    }
    len = r->io_header.dxfer_len - r->io_header.resid;
    DPRINTF("Data ready tag=0x%x len=%d\n", r->req.tag, len);

    r->len = -1;
    r->req.bus->complete(r->req.bus, SCSI_REASON_DATA, r->req.tag, len);
    if (len == 0)
        scsi_command_complete(r, 0);
}

/* Read more data from scsi device into buffer.  */
static void scsi_read_data(SCSIDevice *d, uint32_t tag)
{
    SCSIGenericState *s = DO_UPCAST(SCSIGenericState, qdev, d);
    SCSIGenericReq *r;
    int ret;

    DPRINTF("scsi_read_data 0x%x\n", tag);
    r = scsi_find_request(s, tag);
    if (!r) {
        BADF("Bad read tag 0x%x\n", tag);
        /* ??? This is the wrong error.  */
        scsi_command_complete(r, -EINVAL);
        return;
    }

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
        r->req.bus->complete(r->req.bus, SCSI_REASON_DATA, r->req.tag, s->senselen);
        return;
    }

    ret = execute_command(s->dinfo->bdrv, r, SG_DXFER_FROM_DEV, scsi_read_complete);
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
static int scsi_write_data(SCSIDevice *d, uint32_t tag)
{
    SCSIGenericState *s = DO_UPCAST(SCSIGenericState, qdev, d);
    SCSIGenericReq *r;
    int ret;

    DPRINTF("scsi_write_data 0x%x\n", tag);
    r = scsi_find_request(s, tag);
    if (!r) {
        BADF("Bad write tag 0x%x\n", tag);
        /* ??? This is the wrong error.  */
        scsi_command_complete(r, -EINVAL);
        return 0;
    }

    if (r->len == 0) {
        r->len = r->buflen;
        r->req.bus->complete(r->req.bus, SCSI_REASON_DATA, r->req.tag, r->len);
        return 0;
    }

    ret = execute_command(s->dinfo->bdrv, r, SG_DXFER_TO_DEV, scsi_write_complete);
    if (ret == -1) {
        scsi_command_complete(r, -EINVAL);
        return 1;
    }

    return 0;
}

/* Return a pointer to the data buffer.  */
static uint8_t *scsi_get_buf(SCSIDevice *d, uint32_t tag)
{
    SCSIGenericState *s = DO_UPCAST(SCSIGenericState, qdev, d);
    SCSIGenericReq *r;
    r = scsi_find_request(s, tag);
    if (!r) {
        BADF("Bad buffer tag 0x%x\n", tag);
        return NULL;
    }
    return r->buf;
}

static int scsi_length(uint8_t *cmd, int blocksize, int *cmdlen, uint32_t *len)
{
    switch (cmd[0] >> 5) {
    case 0:
        *len = cmd[4];
        *cmdlen = 6;
        /* length 0 means 256 blocks */
        if (*len == 0)
            *len = 256;
        break;
    case 1:
    case 2:
        *len = cmd[8] | (cmd[7] << 8);
        *cmdlen = 10;
        break;
    case 4:
        *len = cmd[13] | (cmd[12] << 8) | (cmd[11] << 16) | (cmd[10] << 24);
        *cmdlen = 16;
        break;
    case 5:
        *len = cmd[9] | (cmd[8] << 8) | (cmd[7] << 16) | (cmd[6] << 24);
        *cmdlen = 12;
        break;
    default:
        return -1;
    }

    switch(cmd[0]) {
    case TEST_UNIT_READY:
    case REZERO_UNIT:
    case START_STOP:
    case SEEK_6:
    case WRITE_FILEMARKS:
    case SPACE:
    case ERASE:
    case ALLOW_MEDIUM_REMOVAL:
    case VERIFY:
    case SEEK_10:
    case SYNCHRONIZE_CACHE:
    case LOCK_UNLOCK_CACHE:
    case LOAD_UNLOAD:
    case SET_CD_SPEED:
    case SET_LIMITS:
    case WRITE_LONG:
    case MOVE_MEDIUM:
    case UPDATE_BLOCK:
        *len = 0;
        break;
    case MODE_SENSE:
        break;
    case WRITE_SAME:
        *len = 1;
        break;
    case READ_CAPACITY:
        *len = 8;
        break;
    case READ_BLOCK_LIMITS:
        *len = 6;
        break;
    case READ_POSITION:
        *len = 20;
        break;
    case SEND_VOLUME_TAG:
        *len *= 40;
        break;
    case MEDIUM_SCAN:
        *len *= 8;
        break;
    case WRITE_10:
        cmd[1] &= ~0x08;	/* disable FUA */
    case WRITE_VERIFY:
    case WRITE_6:
    case WRITE_12:
    case WRITE_VERIFY_12:
        *len *= blocksize;
        break;
    case READ_10:
        cmd[1] &= ~0x08;	/* disable FUA */
    case READ_6:
    case READ_REVERSE:
    case RECOVER_BUFFERED_DATA:
    case READ_12:
        *len *= blocksize;
        break;
    case INQUIRY:
        *len = cmd[4] | (cmd[3] << 8);
        break;
    }
    return 0;
}

static int scsi_stream_length(uint8_t *cmd, int blocksize, int *cmdlen, uint32_t *len)
{
    switch(cmd[0]) {
    /* stream commands */
    case READ_6:
    case READ_REVERSE:
    case RECOVER_BUFFERED_DATA:
    case WRITE_6:
        *cmdlen = 6;
        *len = cmd[4] | (cmd[3] << 8) | (cmd[2] << 16);
        if (cmd[1] & 0x01) /* fixed */
            *len *= blocksize;
        break;
    case REWIND:
    case START_STOP:
        *cmdlen = 6;
        *len = 0;
        cmd[1] = 0x01;	/* force IMMED, otherwise qemu waits end of command */
        break;
    /* generic commands */
    default:
        return scsi_length(cmd, blocksize, cmdlen, len);
    }
    return 0;
}

static int is_write(int command)
{
    switch (command) {
    case COPY:
    case COPY_VERIFY:
    case COMPARE:
    case CHANGE_DEFINITION:
    case LOG_SELECT:
    case MODE_SELECT:
    case MODE_SELECT_10:
    case SEND_DIAGNOSTIC:
    case WRITE_BUFFER:
    case FORMAT_UNIT:
    case REASSIGN_BLOCKS:
    case RESERVE:
    case SEARCH_EQUAL:
    case SEARCH_HIGH:
    case SEARCH_LOW:
    case WRITE_6:
    case WRITE_10:
    case WRITE_VERIFY:
    case UPDATE_BLOCK:
    case WRITE_LONG:
    case WRITE_SAME:
    case SEARCH_HIGH_12:
    case SEARCH_EQUAL_12:
    case SEARCH_LOW_12:
    case WRITE_12:
    case WRITE_VERIFY_12:
    case SET_WINDOW:
    case MEDIUM_SCAN:
    case SEND_VOLUME_TAG:
    case WRITE_LONG_2:
        return 1;
    }
    return 0;
}

/* Execute a scsi command.  Returns the length of the data expected by the
   command.  This will be Positive for data transfers from the device
   (eg. disk reads), negative for transfers to the device (eg. disk writes),
   and zero if the command does not transfer any data.  */

static int32_t scsi_send_command(SCSIDevice *d, uint32_t tag,
                                 uint8_t *cmd, int lun)
{
    SCSIGenericState *s = DO_UPCAST(SCSIGenericState, qdev, d);
    uint32_t len=0;
    int cmdlen=0;
    SCSIGenericReq *r;
    SCSIBus *bus;
    int ret;

    if (s->type == TYPE_TAPE) {
        if (scsi_stream_length(cmd, s->blocksize, &cmdlen, &len) == -1) {
            BADF("Unsupported command length, command %x\n", cmd[0]);
            return 0;
        }
     } else {
        if (scsi_length(cmd, s->blocksize, &cmdlen, &len) == -1) {
            BADF("Unsupported command length, command %x\n", cmd[0]);
            return 0;
        }
    }

    DPRINTF("Command: lun=%d tag=0x%x data=0x%02x len %d\n", lun, tag,
            cmd[0], len);

    if (cmd[0] != REQUEST_SENSE &&
        (lun != s->lun || (cmd[1] >> 5) != s->lun)) {
        DPRINTF("Unimplemented LUN %d\n", lun ? lun : cmd[1] >> 5);

        s->sensebuf[0] = 0x70;
        s->sensebuf[1] = 0x00;
        s->sensebuf[2] = ILLEGAL_REQUEST;
        s->sensebuf[3] = 0x00;
        s->sensebuf[4] = 0x00;
        s->sensebuf[5] = 0x00;
        s->sensebuf[6] = 0x00;
        s->senselen = 7;
        s->driver_status = SG_ERR_DRIVER_SENSE;
        bus = scsi_bus_from_device(d);
        bus->complete(bus, SCSI_REASON_DONE, tag, CHECK_CONDITION << 1);
        return 0;
    }

    r = scsi_find_request(s, tag);
    if (r) {
        BADF("Tag 0x%x already in use %p\n", tag, r);
        scsi_cancel_io(d, tag);
    }
    r = scsi_new_request(d, tag, lun);

    memcpy(r->req.cmd.buf, cmd, cmdlen);
    r->req.cmd.len = cmdlen;

    if (len == 0) {
        if (r->buf != NULL)
            qemu_free(r->buf);
        r->buflen = 0;
        r->buf = NULL;
        ret = execute_command(s->dinfo->bdrv, r, SG_DXFER_NONE, scsi_command_complete);
        if (ret == -1) {
            scsi_command_complete(r, -EINVAL);
            return 0;
        }
        return 0;
    }

    if (r->buflen != len) {
        if (r->buf != NULL)
            qemu_free(r->buf);
        r->buf = qemu_malloc(len);
        r->buflen = len;
    }

    memset(r->buf, 0, r->buflen);
    r->len = len;
    if (is_write(cmd[0])) {
        r->len = 0;
        return -len;
    }

    return len;
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

static void scsi_destroy(SCSIDevice *d)
{
    SCSIGenericState *s = DO_UPCAST(SCSIGenericState, qdev, d);
    SCSIGenericReq *r;

    while (!QTAILQ_EMPTY(&s->qdev.requests)) {
        r = DO_UPCAST(SCSIGenericReq, req, QTAILQ_FIRST(&s->qdev.requests));
        scsi_remove_request(r);
    }
    drive_uninit(s->dinfo);
}

static int scsi_generic_initfn(SCSIDevice *dev)
{
    SCSIGenericState *s = DO_UPCAST(SCSIGenericState, qdev, dev);
    int sg_version;
    struct sg_scsi_id scsiid;

    if (!s->dinfo || !s->dinfo->bdrv) {
        qemu_error("scsi-generic: drive property not set\n");
        return -1;
    }

    /* check we are really using a /dev/sg* file */
    if (!bdrv_is_sg(s->dinfo->bdrv)) {
        qemu_error("scsi-generic: not /dev/sg*\n");
        return -1;
    }

    /* check we are using a driver managing SG_IO (version 3 and after */
    if (bdrv_ioctl(s->dinfo->bdrv, SG_GET_VERSION_NUM, &sg_version) < 0 ||
        sg_version < 30000) {
        qemu_error("scsi-generic: scsi generic interface too old\n");
        return -1;
    }

    /* get LUN of the /dev/sg? */
    if (bdrv_ioctl(s->dinfo->bdrv, SG_GET_SCSI_ID, &scsiid)) {
        qemu_error("scsi-generic: SG_GET_SCSI_ID ioctl failed\n");
        return -1;
    }

    /* define device state */
    s->lun = scsiid.lun;
    DPRINTF("LUN %d\n", s->lun);
    s->type = scsiid.scsi_type;
    DPRINTF("device type %d\n", s->type);
    if (s->type == TYPE_TAPE) {
        s->blocksize = get_stream_blocksize(s->dinfo->bdrv);
        if (s->blocksize == -1)
            s->blocksize = 0;
    } else {
        s->blocksize = get_blocksize(s->dinfo->bdrv);
        /* removable media returns 0 if not present */
        if (s->blocksize <= 0) {
            if (s->type == TYPE_ROM || s->type  == TYPE_WORM)
                s->blocksize = 2048;
            else
                s->blocksize = 512;
        }
    }
    DPRINTF("block size %d\n", s->blocksize);
    s->driver_status = 0;
    memset(s->sensebuf, 0, sizeof(s->sensebuf));
    return 0;
}

static SCSIDeviceInfo scsi_generic_info = {
    .qdev.name    = "scsi-generic",
    .qdev.desc    = "pass through generic scsi device (/dev/sg*)",
    .qdev.size    = sizeof(SCSIGenericState),
    .init         = scsi_generic_initfn,
    .destroy      = scsi_destroy,
    .send_command = scsi_send_command,
    .read_data    = scsi_read_data,
    .write_data   = scsi_write_data,
    .cancel_io    = scsi_cancel_io,
    .get_buf      = scsi_get_buf,
    .qdev.props   = (Property[]) {
        DEFINE_PROP_DRIVE("drive", SCSIGenericState, dinfo),
        DEFINE_PROP_END_OF_LIST(),
    },
};

static void scsi_generic_register_devices(void)
{
    scsi_qdev_register(&scsi_generic_info);
}
device_init(scsi_generic_register_devices)

#endif /* __linux__ */
