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
#include "scsi-disk.h"

#ifndef __linux__

SCSIDevice *scsi_generic_init(BlockDriverState *bdrv, int tcq,
                              scsi_completionfn completion, void *opaque)
{
    return NULL;
}

#else /* __linux__ */

//#define DEBUG_SCSI

#ifdef DEBUG_SCSI
#define DPRINTF(fmt, args...) \
do { printf("scsi-generic: " fmt , ##args); } while (0)
#else
#define DPRINTF(fmt, args...) do {} while(0)
#endif

#define BADF(fmt, args...) \
do { fprintf(stderr, "scsi-generic: " fmt , ##args); } while (0)

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <scsi/sg.h>
#include <scsi/scsi.h>

#define LOAD_UNLOAD 0xa6
#define SET_CD_SPEED 0xbb
#define BLANK 0xa1

#define SCSI_CMD_BUF_SIZE     16
#define SCSI_SENSE_BUF_SIZE 32

#define SG_ERR_DRIVER_TIMEOUT 0x06
#define SG_ERR_DRIVER_SENSE 0x08

#ifndef MAX_UINT
#define MAX_UINT ((unsigned int)-1)
#endif

typedef struct SCSIRequest {
    BlockDriverAIOCB *aiocb;
    struct SCSIRequest *next;
    SCSIDeviceState *dev;
    uint32_t tag;
    uint8_t cmd[SCSI_CMD_BUF_SIZE];
    int cmdlen;
    uint8_t *buf;
    int buflen;
    int len;
    sg_io_hdr_t io_header;
} SCSIRequest;

struct SCSIDeviceState
{
    SCSIRequest *requests;
    BlockDriverState *bdrv;
    int blocksize;
    int lun;
    scsi_completionfn completion;
    void *opaque;
    int driver_status;
    uint8_t sensebuf[SCSI_SENSE_BUF_SIZE];
};

/* Global pool of SCSIRequest structures.  */
static SCSIRequest *free_requests = NULL;

static SCSIRequest *scsi_new_request(SCSIDeviceState *s, uint32_t tag)
{
    SCSIRequest *r;

    if (free_requests) {
        r = free_requests;
        free_requests = r->next;
    } else {
        r = qemu_malloc(sizeof(SCSIRequest));
        r->buf = NULL;
        r->buflen = 0;
    }
    r->dev = s;
    r->tag = tag;
    memset(r->cmd, 0, sizeof(r->cmd));
    memset(&r->io_header, 0, sizeof(r->io_header));
    r->cmdlen = 0;
    r->len = 0;
    r->aiocb = NULL;

    /* link */

    r->next = s->requests;
    s->requests = r;
    return r;
}

static void scsi_remove_request(SCSIRequest *r)
{
    SCSIRequest *last;
    SCSIDeviceState *s = r->dev;

    if (s->requests == r) {
        s->requests = r->next;
    } else {
        last = s->requests;
        while (last && last->next != r)
            last = last->next;
        if (last) {
            last->next = r->next;
        } else {
            BADF("Orphaned request\n");
        }
    }
    r->next = free_requests;
    free_requests = r;
}

static SCSIRequest *scsi_find_request(SCSIDeviceState *s, uint32_t tag)
{
    SCSIRequest *r;

    r = s->requests;
    while (r && r->tag != tag)
        r = r->next;

    return r;
}

/* Helper function for command completion.  */
static void scsi_command_complete(void *opaque, int ret)
{
    SCSIRequest *r = (SCSIRequest *)opaque;
    SCSIDeviceState *s = r->dev;
    uint32_t tag;
    int sense;

    s->driver_status = r->io_header.driver_status;
    if (ret != 0)
        sense = HARDWARE_ERROR;
    else {
        if (s->driver_status & SG_ERR_DRIVER_TIMEOUT) {
            sense = HARDWARE_ERROR;
            BADF("Driver Timeout\n");
        } else if ((s->driver_status & SG_ERR_DRIVER_SENSE) == 0)
            sense = NO_SENSE;
        else
            sense = s->sensebuf[2] & 0x0f;
    }

    DPRINTF("Command complete 0x%p tag=0x%x sense=%d\n", r, r->tag, sense);
    tag = r->tag;
    scsi_remove_request(r);
    s->completion(s->opaque, SCSI_REASON_DONE, tag, sense);
}

/* Cancel a pending data transfer.  */
static void scsi_cancel_io(SCSIDevice *d, uint32_t tag)
{
    DPRINTF("scsi_cancel_io 0x%x\n", tag);
    SCSIDeviceState *s = d->state;
    SCSIRequest *r;
    DPRINTF("Cancel tag=0x%x\n", tag);
    r = scsi_find_request(s, tag);
    if (r) {
        if (r->aiocb)
            bdrv_aio_cancel(r->aiocb);
        r->aiocb = NULL;
        scsi_remove_request(r);
    }
}

static int execute_command(BlockDriverState *bdrv,
                           SCSIRequest *r, int direction,
			   BlockDriverCompletionFunc *complete)
{

    r->io_header.interface_id = 'S';
    r->io_header.dxfer_direction = direction;
    r->io_header.dxferp = r->buf;
    r->io_header.dxfer_len = r->buflen;
    r->io_header.cmdp = r->cmd;
    r->io_header.cmd_len = r->cmdlen;
    r->io_header.mx_sb_len = sizeof(r->dev->sensebuf);
    r->io_header.sbp = r->dev->sensebuf;
    r->io_header.timeout = MAX_UINT;
    r->io_header.usr_ptr = r;
    r->io_header.flags |= SG_FLAG_DIRECT_IO;

    if (bdrv_pwrite(bdrv, -1, &r->io_header, sizeof(r->io_header)) == -1) {
        BADF("execute_command: write failed ! (%d)\n", errno);
        return -1;
    }
    if (complete == NULL) {
        int ret;
        r->aiocb = NULL;
        while ((ret = bdrv_pread(bdrv, -1, &r->io_header,
                                           sizeof(r->io_header))) == -1 &&
                      errno == EINTR);
        if (ret == -1) {
            BADF("execute_command: read failed !\n");
            return -1;
        }
        return 0;
    }

    r->aiocb = bdrv_aio_read(bdrv, 0, (uint8_t*)&r->io_header,
                          -(int64_t)sizeof(r->io_header), complete, r);
    if (r->aiocb == NULL) {
        BADF("execute_command: read failed !\n");
        return -1;
    }

    return 0;
}

static void scsi_read_complete(void * opaque, int ret)
{
    SCSIRequest *r = (SCSIRequest *)opaque;
    SCSIDeviceState *s = r->dev;
    int len;

    if (ret) {
        DPRINTF("IO error\n");
        scsi_command_complete(r, ret);
        return;
    }
    len = r->io_header.dxfer_len - r->io_header.resid;
    DPRINTF("Data ready tag=0x%x len=%d\n", r->tag, len);

    r->len = -1;
    s->completion(s->opaque, SCSI_REASON_DATA, r->tag, len);
}

/* Read more data from scsi device into buffer.  */
static void scsi_read_data(SCSIDevice *d, uint32_t tag)
{
    SCSIDeviceState *s = d->state;
    SCSIRequest *r;
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

    if (r->cmd[0] == REQUEST_SENSE && s->driver_status & SG_ERR_DRIVER_SENSE)
    {
        memcpy(r->buf, s->sensebuf, 16);
        r->io_header.driver_status = 0;
        r->len = -1;
        s->completion(s->opaque, SCSI_REASON_DATA, r->tag, 16);
        return;
    }

    ret = execute_command(s->bdrv, r, SG_DXFER_FROM_DEV, scsi_read_complete);
    if (ret == -1) {
        scsi_command_complete(r, -EINVAL);
        return;
    }
}

static void scsi_write_complete(void * opaque, int ret)
{
    SCSIRequest *r = (SCSIRequest *)opaque;

    DPRINTF("scsi_write_complete() ret = %d\n", ret);
    if (ret) {
        DPRINTF("IO error\n");
        scsi_command_complete(r, ret);
        return;
    }

    scsi_command_complete(r, ret);
}

/* Write data to a scsi device.  Returns nonzero on failure.
   The transfer may complete asynchronously.  */
static int scsi_write_data(SCSIDevice *d, uint32_t tag)
{
    SCSIDeviceState *s = d->state;
    SCSIRequest *r;
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
        s->completion(s->opaque, SCSI_REASON_DATA, r->tag, r->len);
        return 0;
    }

    ret = execute_command(s->bdrv, r, SG_DXFER_TO_DEV, scsi_write_complete);
    if (ret == -1) {
        scsi_command_complete(r, -EINVAL);
        return 1;
    }

    return 0;
}

/* Return a pointer to the data buffer.  */
static uint8_t *scsi_get_buf(SCSIDevice *d, uint32_t tag)
{
    SCSIDeviceState *s = d->state;
    SCSIRequest *r;
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
    SCSIDeviceState *s = d->state;
    uint32_t len=0;
    int cmdlen=0;
    SCSIRequest *r;
    int ret;

    /* ??? Tags are not unique for different luns.  We only implement a
       single lun, so this should not matter.  */

    if (lun != s->lun || (cmd[1] >> 5) != s->lun) {
        DPRINTF("Unimplemented LUN %d\n", lun ? lun : cmd[1] >> 5);
        s->completion(s->opaque, SCSI_REASON_DONE, tag, ILLEGAL_REQUEST);
        return 0;
    }

    if (scsi_length(cmd, s->blocksize, &cmdlen, &len) == -1) {
        BADF("Unsupported command length, command %x\n", cmd[0]);
        return 0;
    }

    DPRINTF("Command: lun=%d tag=0x%x data=0x%02x len %d\n", lun, tag,
            cmd[0], len);

    r = scsi_find_request(s, tag);
    if (r) {
        BADF("Tag 0x%x already in use %p\n", tag, r);
        scsi_cancel_io(d, tag);
    }
    r = scsi_new_request(s, tag);

    memcpy(r->cmd, cmd, cmdlen);
    r->cmdlen = cmdlen;

    if (len == 0) {
        if (r->buf != NULL)
            free(r->buf);
        r->buflen = 0;
        r->buf = NULL;
        ret = execute_command(s->bdrv, r, SG_DXFER_NONE, scsi_command_complete);
        if (ret == -1) {
            scsi_command_complete(r, -EINVAL);
            return 0;
        }
        return 0;
    }

    if (r->buflen != len) {
        if (r->buf != NULL)
            free(r->buf);
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

    memset(cmd, sizeof(cmd), 0);
    memset(buf, sizeof(buf), 0);
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

    ret = bdrv_pwrite(bdrv, -1, &io_header, sizeof(io_header));
    if (ret == -1)
        return -1;

    while ((ret = bdrv_pread(bdrv, -1, &io_header, sizeof(io_header))) == -1 &&
           errno == EINTR);

    if (ret == -1)
        return -1;

    return (buf[4] << 24) | (buf[5] << 16) | (buf[6] << 8) | buf[7];
}

static void scsi_destroy(SCSIDevice *d)
{
    SCSIRequest *r, *n;

    r = d->state->requests;
    while (r) {
        n = r->next;
        qemu_free(r);
        r = n;
    }

    r = free_requests;
    while (r) {
        n = r->next;
        qemu_free(r);
        r = n;
    }

    qemu_free(d->state);
    qemu_free(d);
}

SCSIDevice *scsi_generic_init(BlockDriverState *bdrv, int tcq,
                              scsi_completionfn completion, void *opaque)
{
    int sg_version;
    SCSIDevice *d;
    SCSIDeviceState *s;
    struct sg_scsi_id scsiid;

    /* check we are really using a /dev/sg* file */

    if (!bdrv_is_sg(bdrv))
        return NULL;

    /* check we are using a driver managing SG_IO (version 3 and after */

    if (bdrv_ioctl(bdrv, SG_GET_VERSION_NUM, &sg_version) < 0 ||
        sg_version < 30000)
        return NULL;

    /* get LUN of the /dev/sg? */

    if (bdrv_ioctl(bdrv, SG_GET_SCSI_ID, &scsiid))
        return NULL;

    /* define device state */

    s = (SCSIDeviceState *)qemu_mallocz(sizeof(SCSIDeviceState));
    s->bdrv = bdrv;
    s->requests = NULL;
    s->completion = completion;
    s->opaque = opaque;
    s->lun = scsiid.lun;
    s->blocksize = get_blocksize(s->bdrv);
    s->driver_status = 0;
    memset(s->sensebuf, 0, sizeof(s->sensebuf));
    /* removable media returns 0 if not present */
    if (s->blocksize <= 0)
        s->blocksize = 2048;

    /* define function to manage device */

    d = (SCSIDevice *)qemu_mallocz(sizeof(SCSIDevice));
    d->state = s;
    d->destroy = scsi_destroy;
    d->send_command = scsi_send_command;
    d->read_data = scsi_read_data;
    d->write_data = scsi_write_data;
    d->cancel_io = scsi_cancel_io;
    d->get_buf = scsi_get_buf;

    return d;
}
#endif /* __linux__ */
