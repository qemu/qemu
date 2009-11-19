/*
 * SCSI Device emulation
 *
 * Copyright (c) 2006 CodeSourcery.
 * Based on code by Fabrice Bellard
 *
 * Written by Paul Brook
 *
 * This code is licenced under the LGPL.
 *
 * Note that this file only handles the SCSI architecture model and device
 * commands.  Emulation of interface/link layer protocols is handled by
 * the host adapter emulator.
 */

#include <qemu-common.h>
#include <sysemu.h>
//#define DEBUG_SCSI

#ifdef DEBUG_SCSI
#define DPRINTF(fmt, ...) \
do { printf("scsi-disk: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) do {} while(0)
#endif

#define BADF(fmt, ...) \
do { fprintf(stderr, "scsi-disk: " fmt , ## __VA_ARGS__); } while (0)

#include "qemu-common.h"
#include "block.h"
#include "scsi.h"
#include "scsi-defs.h"

#define SCSI_DMA_BUF_SIZE    131072
#define SCSI_MAX_INQUIRY_LEN 256

#define SCSI_REQ_STATUS_RETRY 0x01

typedef struct SCSIDiskState SCSIDiskState;

typedef struct SCSIDiskReq {
    SCSIRequest req;
    /* ??? We should probably keep track of whether the data transfer is
       a read or a write.  Currently we rely on the host getting it right.  */
    /* Both sector and sector_count are in terms of qemu 512 byte blocks.  */
    uint64_t sector;
    uint32_t sector_count;
    struct iovec iov;
    QEMUIOVector qiov;
    uint32_t status;
} SCSIDiskReq;

struct SCSIDiskState
{
    SCSIDevice qdev;
    /* The qemu block layer uses a fixed 512 byte sector size.
       This is the number of 512 byte blocks in a single scsi sector.  */
    int cluster_size;
    uint64_t max_lba;
    QEMUBH *bh;
};

static SCSIDiskReq *scsi_new_request(SCSIDevice *d, uint32_t tag, uint32_t lun)
{
    SCSIRequest *req;
    SCSIDiskReq *r;

    req = scsi_req_alloc(sizeof(SCSIDiskReq), d, tag, lun);
    r = DO_UPCAST(SCSIDiskReq, req, req);
    r->iov.iov_base = qemu_memalign(512, SCSI_DMA_BUF_SIZE);
    return r;
}

static void scsi_remove_request(SCSIDiskReq *r)
{
    qemu_free(r->iov.iov_base);
    scsi_req_free(&r->req);
}

static SCSIDiskReq *scsi_find_request(SCSIDiskState *s, uint32_t tag)
{
    return DO_UPCAST(SCSIDiskReq, req, scsi_req_find(&s->qdev, tag));
}

static void scsi_req_set_status(SCSIRequest *req, int status, int sense_code)
{
    req->status = status;
    scsi_dev_set_sense(req->dev, sense_code);
}

/* Helper function for command completion.  */
static void scsi_command_complete(SCSIDiskReq *r, int status, int sense)
{
    DPRINTF("Command complete tag=0x%x status=%d sense=%d\n",
            r->req.tag, status, sense);
    scsi_req_set_status(&r->req, status, sense);
    scsi_req_complete(&r->req);
    scsi_remove_request(r);
}

/* Cancel a pending data transfer.  */
static void scsi_cancel_io(SCSIDevice *d, uint32_t tag)
{
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, d);
    SCSIDiskReq *r;
    DPRINTF("Cancel tag=0x%x\n", tag);
    r = scsi_find_request(s, tag);
    if (r) {
        if (r->req.aiocb)
            bdrv_aio_cancel(r->req.aiocb);
        r->req.aiocb = NULL;
        scsi_remove_request(r);
    }
}

static void scsi_read_complete(void * opaque, int ret)
{
    SCSIDiskReq *r = (SCSIDiskReq *)opaque;

    if (ret) {
        DPRINTF("IO error\n");
        r->req.bus->complete(r->req.bus, SCSI_REASON_DATA, r->req.tag, 0);
        scsi_command_complete(r, CHECK_CONDITION, NO_SENSE);
        return;
    }
    DPRINTF("Data ready tag=0x%x len=%" PRId64 "\n", r->req.tag, r->iov.iov_len);

    r->req.bus->complete(r->req.bus, SCSI_REASON_DATA, r->req.tag, r->iov.iov_len);
}

/* Read more data from scsi device into buffer.  */
static void scsi_read_data(SCSIDevice *d, uint32_t tag)
{
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, d);
    SCSIDiskReq *r;
    uint32_t n;

    r = scsi_find_request(s, tag);
    if (!r) {
        BADF("Bad read tag 0x%x\n", tag);
        /* ??? This is the wrong error.  */
        scsi_command_complete(r, CHECK_CONDITION, HARDWARE_ERROR);
        return;
    }
    if (r->sector_count == (uint32_t)-1) {
        DPRINTF("Read buf_len=%" PRId64 "\n", r->iov.iov_len);
        r->sector_count = 0;
        r->req.bus->complete(r->req.bus, SCSI_REASON_DATA, r->req.tag, r->iov.iov_len);
        return;
    }
    DPRINTF("Read sector_count=%d\n", r->sector_count);
    if (r->sector_count == 0) {
        scsi_command_complete(r, GOOD, NO_SENSE);
        return;
    }

    n = r->sector_count;
    if (n > SCSI_DMA_BUF_SIZE / 512)
        n = SCSI_DMA_BUF_SIZE / 512;

    r->iov.iov_len = n * 512;
    qemu_iovec_init_external(&r->qiov, &r->iov, 1);
    r->req.aiocb = bdrv_aio_readv(s->qdev.dinfo->bdrv, r->sector, &r->qiov, n,
                              scsi_read_complete, r);
    if (r->req.aiocb == NULL)
        scsi_command_complete(r, CHECK_CONDITION, HARDWARE_ERROR);
    r->sector += n;
    r->sector_count -= n;
}

static int scsi_handle_write_error(SCSIDiskReq *r, int error)
{
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, r->req.dev);
    BlockInterfaceErrorAction action =
        drive_get_on_error(s->qdev.dinfo->bdrv, 0);

    if (action == BLOCK_ERR_IGNORE)
        return 0;

    if ((error == ENOSPC && action == BLOCK_ERR_STOP_ENOSPC)
            || action == BLOCK_ERR_STOP_ANY) {
        r->status |= SCSI_REQ_STATUS_RETRY;
        vm_stop(0);
    } else {
        scsi_command_complete(r, CHECK_CONDITION,
                HARDWARE_ERROR);
    }

    return 1;
}

static void scsi_write_complete(void * opaque, int ret)
{
    SCSIDiskReq *r = (SCSIDiskReq *)opaque;
    uint32_t len;
    uint32_t n;

    r->req.aiocb = NULL;

    if (ret) {
        if (scsi_handle_write_error(r, -ret))
            return;
    }

    n = r->iov.iov_len / 512;
    r->sector += n;
    r->sector_count -= n;
    if (r->sector_count == 0) {
        scsi_command_complete(r, GOOD, NO_SENSE);
    } else {
        len = r->sector_count * 512;
        if (len > SCSI_DMA_BUF_SIZE) {
            len = SCSI_DMA_BUF_SIZE;
        }
        r->iov.iov_len = len;
        DPRINTF("Write complete tag=0x%x more=%d\n", r->req.tag, len);
        r->req.bus->complete(r->req.bus, SCSI_REASON_DATA, r->req.tag, len);
    }
}

static void scsi_write_request(SCSIDiskReq *r)
{
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, r->req.dev);
    uint32_t n;

    n = r->iov.iov_len / 512;
    if (n) {
        qemu_iovec_init_external(&r->qiov, &r->iov, 1);
        r->req.aiocb = bdrv_aio_writev(s->qdev.dinfo->bdrv, r->sector, &r->qiov, n,
                                   scsi_write_complete, r);
        if (r->req.aiocb == NULL)
            scsi_command_complete(r, CHECK_CONDITION,
                                  HARDWARE_ERROR);
    } else {
        /* Invoke completion routine to fetch data from host.  */
        scsi_write_complete(r, 0);
    }
}

/* Write data to a scsi device.  Returns nonzero on failure.
   The transfer may complete asynchronously.  */
static int scsi_write_data(SCSIDevice *d, uint32_t tag)
{
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, d);
    SCSIDiskReq *r;

    DPRINTF("Write data tag=0x%x\n", tag);
    r = scsi_find_request(s, tag);
    if (!r) {
        BADF("Bad write tag 0x%x\n", tag);
        scsi_command_complete(r, CHECK_CONDITION, HARDWARE_ERROR);
        return 1;
    }

    if (r->req.aiocb)
        BADF("Data transfer already in progress\n");

    scsi_write_request(r);

    return 0;
}

static void scsi_dma_restart_bh(void *opaque)
{
    SCSIDiskState *s = opaque;
    SCSIRequest *req;
    SCSIDiskReq *r;

    qemu_bh_delete(s->bh);
    s->bh = NULL;

    QTAILQ_FOREACH(req, &s->qdev.requests, next) {
        r = DO_UPCAST(SCSIDiskReq, req, req);
        if (r->status & SCSI_REQ_STATUS_RETRY) {
            r->status &= ~SCSI_REQ_STATUS_RETRY;
            scsi_write_request(r); 
        }
    }
}

static void scsi_dma_restart_cb(void *opaque, int running, int reason)
{
    SCSIDiskState *s = opaque;

    if (!running)
        return;

    if (!s->bh) {
        s->bh = qemu_bh_new(scsi_dma_restart_bh, s);
        qemu_bh_schedule(s->bh);
    }
}

/* Return a pointer to the data buffer.  */
static uint8_t *scsi_get_buf(SCSIDevice *d, uint32_t tag)
{
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, d);
    SCSIDiskReq *r;

    r = scsi_find_request(s, tag);
    if (!r) {
        BADF("Bad buffer tag 0x%x\n", tag);
        return NULL;
    }
    return (uint8_t *)r->iov.iov_base;
}

static int scsi_disk_emulate_inquiry(SCSIRequest *req, uint8_t *outbuf)
{
    BlockDriverState *bdrv = req->dev->dinfo->bdrv;
    int buflen = 0;

    if (req->cmd.buf[1] & 0x2) {
        /* Command support data - optional, not implemented */
        BADF("optional INQUIRY command support request not implemented\n");
        return -1;
    }

    if (req->cmd.buf[1] & 0x1) {
        /* Vital product data */
        uint8_t page_code = req->cmd.buf[2];
        if (req->cmd.xfer < 4) {
            BADF("Error: Inquiry (EVPD[%02X]) buffer size %zd is "
                 "less than 4\n", page_code, req->cmd.xfer);
            return -1;
        }

        if (bdrv_get_type_hint(bdrv) == BDRV_TYPE_CDROM) {
            outbuf[buflen++] = 5;
        } else {
            outbuf[buflen++] = 0;
        }
        outbuf[buflen++] = page_code ; // this page
        outbuf[buflen++] = 0x00;

        switch (page_code) {
        case 0x00: /* Supported page codes, mandatory */
            DPRINTF("Inquiry EVPD[Supported pages] "
                    "buffer size %zd\n", req->cmd.xfer);
            outbuf[buflen++] = 3;    // number of pages
            outbuf[buflen++] = 0x00; // list of supported pages (this page)
            outbuf[buflen++] = 0x80; // unit serial number
            outbuf[buflen++] = 0x83; // device identification
            break;

        case 0x80: /* Device serial number, optional */
        {
            const char *serial = req->dev->dinfo->serial ?: "0";
            int l = strlen(serial);

            if (l > req->cmd.xfer)
                l = req->cmd.xfer;
            if (l > 20)
                l = 20;

            DPRINTF("Inquiry EVPD[Serial number] "
                    "buffer size %zd\n", req->cmd.xfer);
            outbuf[buflen++] = l;
            memcpy(outbuf+buflen, serial, l);
            buflen += l;
            break;
        }

        case 0x83: /* Device identification page, mandatory */
        {
            int max_len = 255 - 8;
            int id_len = strlen(bdrv_get_device_name(bdrv));

            if (id_len > max_len)
                id_len = max_len;
            DPRINTF("Inquiry EVPD[Device identification] "
                    "buffer size %zd\n", req->cmd.xfer);

            outbuf[buflen++] = 3 + id_len;
            outbuf[buflen++] = 0x2; // ASCII
            outbuf[buflen++] = 0;   // not officially assigned
            outbuf[buflen++] = 0;   // reserved
            outbuf[buflen++] = id_len; // length of data following

            memcpy(outbuf+buflen, bdrv_get_device_name(bdrv), id_len);
            buflen += id_len;
            break;
        }
        default:
            BADF("Error: unsupported Inquiry (EVPD[%02X]) "
                 "buffer size %zd\n", page_code, req->cmd.xfer);
            return -1;
        }
        /* done with EVPD */
        return buflen;
    }

    /* Standard INQUIRY data */
    if (req->cmd.buf[2] != 0) {
        BADF("Error: Inquiry (STANDARD) page or code "
             "is non-zero [%02X]\n", req->cmd.buf[2]);
        return -1;
    }

    /* PAGE CODE == 0 */
    if (req->cmd.xfer < 5) {
        BADF("Error: Inquiry (STANDARD) buffer size %zd "
             "is less than 5\n", req->cmd.xfer);
        return -1;
    }

    if (req->cmd.xfer < 36) {
        BADF("Error: Inquiry (STANDARD) buffer size %zd "
             "is less than 36 (TODO: only 5 required)\n", req->cmd.xfer);
    }

    buflen = req->cmd.xfer;
    if (buflen > SCSI_MAX_INQUIRY_LEN)
        buflen = SCSI_MAX_INQUIRY_LEN;

    memset(outbuf, 0, buflen);

    if (req->lun || req->cmd.buf[1] >> 5) {
        outbuf[0] = 0x7f;	/* LUN not supported */
        return buflen;
    }

    if (bdrv_get_type_hint(bdrv) == BDRV_TYPE_CDROM) {
        outbuf[0] = 5;
        outbuf[1] = 0x80;
        memcpy(&outbuf[16], "QEMU CD-ROM     ", 16);
    } else {
        outbuf[0] = 0;
        memcpy(&outbuf[16], "QEMU HARDDISK   ", 16);
    }
    memcpy(&outbuf[8], "QEMU    ", 8);
    memcpy(&outbuf[32], QEMU_VERSION, 4);
    /* Identify device as SCSI-3 rev 1.
       Some later commands are also implemented. */
    outbuf[2] = 3;
    outbuf[3] = 2; /* Format 2 */
    outbuf[4] = buflen - 5; /* Additional Length = (Len - 1) - 4 */
    /* Sync data transfer and TCQ.  */
    outbuf[7] = 0x10 | (req->bus->tcq ? 0x02 : 0);
    return buflen;
}

static int mode_sense_page(SCSIRequest *req, int page, uint8_t *p)
{
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, req->dev);
    BlockDriverState *bdrv = req->dev->dinfo->bdrv;
    int cylinders, heads, secs;

    switch (page) {
    case 4: /* Rigid disk device geometry page. */
        p[0] = 4;
        p[1] = 0x16;
        /* if a geometry hint is available, use it */
        bdrv_get_geometry_hint(bdrv, &cylinders, &heads, &secs);
        p[2] = (cylinders >> 16) & 0xff;
        p[3] = (cylinders >> 8) & 0xff;
        p[4] = cylinders & 0xff;
        p[5] = heads & 0xff;
        /* Write precomp start cylinder, disabled */
        p[6] = (cylinders >> 16) & 0xff;
        p[7] = (cylinders >> 8) & 0xff;
        p[8] = cylinders & 0xff;
        /* Reduced current start cylinder, disabled */
        p[9] = (cylinders >> 16) & 0xff;
        p[10] = (cylinders >> 8) & 0xff;
        p[11] = cylinders & 0xff;
        /* Device step rate [ns], 200ns */
        p[12] = 0;
        p[13] = 200;
        /* Landing zone cylinder */
        p[14] = 0xff;
        p[15] =  0xff;
        p[16] = 0xff;
        /* Medium rotation rate [rpm], 5400 rpm */
        p[20] = (5400 >> 8) & 0xff;
        p[21] = 5400 & 0xff;
        return 0x16;

    case 5: /* Flexible disk device geometry page. */
        p[0] = 5;
        p[1] = 0x1e;
        /* Transfer rate [kbit/s], 5Mbit/s */
        p[2] = 5000 >> 8;
        p[3] = 5000 & 0xff;
        /* if a geometry hint is available, use it */
        bdrv_get_geometry_hint(bdrv, &cylinders, &heads, &secs);
        p[4] = heads & 0xff;
        p[5] = secs & 0xff;
        p[6] = s->cluster_size * 2;
        p[8] = (cylinders >> 8) & 0xff;
        p[9] = cylinders & 0xff;
        /* Write precomp start cylinder, disabled */
        p[10] = (cylinders >> 8) & 0xff;
        p[11] = cylinders & 0xff;
        /* Reduced current start cylinder, disabled */
        p[12] = (cylinders >> 8) & 0xff;
        p[13] = cylinders & 0xff;
        /* Device step rate [100us], 100us */
        p[14] = 0;
        p[15] = 1;
        /* Device step pulse width [us], 1us */
        p[16] = 1;
        /* Device head settle delay [100us], 100us */
        p[17] = 0;
        p[18] = 1;
        /* Motor on delay [0.1s], 0.1s */
        p[19] = 1;
        /* Motor off delay [0.1s], 0.1s */
        p[20] = 1;
        /* Medium rotation rate [rpm], 5400 rpm */
        p[28] = (5400 >> 8) & 0xff;
        p[29] = 5400 & 0xff;
        return 0x1e;

    case 8: /* Caching page.  */
        p[0] = 8;
        p[1] = 0x12;
        if (bdrv_enable_write_cache(s->qdev.dinfo->bdrv)) {
            p[2] = 4; /* WCE */
        }
        return 20;

    case 0x2a: /* CD Capabilities and Mechanical Status page. */
        if (bdrv_get_type_hint(bdrv) != BDRV_TYPE_CDROM)
            return 0;
        p[0] = 0x2a;
        p[1] = 0x14;
        p[2] = 3; // CD-R & CD-RW read
        p[3] = 0; // Writing not supported
        p[4] = 0x7f; /* Audio, composite, digital out,
                        mode 2 form 1&2, multi session */
        p[5] = 0xff; /* CD DA, DA accurate, RW supported,
                        RW corrected, C2 errors, ISRC,
                        UPC, Bar code */
        p[6] = 0x2d | (bdrv_is_locked(s->qdev.dinfo->bdrv)? 2 : 0);
        /* Locking supported, jumper present, eject, tray */
        p[7] = 0; /* no volume & mute control, no
                     changer */
        p[8] = (50 * 176) >> 8; // 50x read speed
        p[9] = (50 * 176) & 0xff;
        p[10] = 0 >> 8; // No volume
        p[11] = 0 & 0xff;
        p[12] = 2048 >> 8; // 2M buffer
        p[13] = 2048 & 0xff;
        p[14] = (16 * 176) >> 8; // 16x read speed current
        p[15] = (16 * 176) & 0xff;
        p[18] = (16 * 176) >> 8; // 16x write speed
        p[19] = (16 * 176) & 0xff;
        p[20] = (16 * 176) >> 8; // 16x write speed current
        p[21] = (16 * 176) & 0xff;
        return 22;

    default:
        return 0;
    }
}

static int scsi_disk_emulate_mode_sense(SCSIRequest *req, uint8_t *outbuf)
{
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, req->dev);
    BlockDriverState *bdrv = req->dev->dinfo->bdrv;
    uint64_t nb_sectors;
    int page, dbd, buflen;
    uint8_t *p;

    dbd = req->cmd.buf[1]  & 0x8;
    page = req->cmd.buf[2] & 0x3f;
    DPRINTF("Mode Sense (page %d, len %zd)\n", page, req->cmd.xfer);
    memset(outbuf, 0, req->cmd.xfer);
    p = outbuf;

    p[1] = 0; /* Default media type.  */
    p[3] = 0; /* Block descriptor length.  */
    if (bdrv_get_type_hint(bdrv) == BDRV_TYPE_CDROM ||
        bdrv_is_read_only(bdrv)) {
        p[2] = 0x80; /* Readonly.  */
    }
    p += 4;

    bdrv_get_geometry(bdrv, &nb_sectors);
    if ((~dbd) & nb_sectors) {
        outbuf[3] = 8; /* Block descriptor length  */
        nb_sectors /= s->cluster_size;
        nb_sectors--;
        if (nb_sectors > 0xffffff)
            nb_sectors = 0xffffff;
        p[0] = 0; /* media density code */
        p[1] = (nb_sectors >> 16) & 0xff;
        p[2] = (nb_sectors >> 8) & 0xff;
        p[3] = nb_sectors & 0xff;
        p[4] = 0; /* reserved */
        p[5] = 0; /* bytes 5-7 are the sector size in bytes */
        p[6] = s->cluster_size * 2;
        p[7] = 0;
        p += 8;
    }

    switch (page) {
    case 0x04:
    case 0x05:
    case 0x08:
    case 0x2a:
        p += mode_sense_page(req, page, p);
        break;
    case 0x3f:
        p += mode_sense_page(req, 0x08, p);
        p += mode_sense_page(req, 0x2a, p);
        break;
    }

    buflen = p - outbuf;
    outbuf[0] = buflen - 4;
    if (buflen > req->cmd.xfer)
        buflen = req->cmd.xfer;
    return buflen;
}

static int scsi_disk_emulate_read_toc(SCSIRequest *req, uint8_t *outbuf)
{
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, req->dev);
    BlockDriverState *bdrv = req->dev->dinfo->bdrv;
    int start_track, format, msf, toclen;
    uint64_t nb_sectors;

    msf = req->cmd.buf[1] & 2;
    format = req->cmd.buf[2] & 0xf;
    start_track = req->cmd.buf[6];
    bdrv_get_geometry(bdrv, &nb_sectors);
    DPRINTF("Read TOC (track %d format %d msf %d)\n", start_track, format, msf >> 1);
    nb_sectors /= s->cluster_size;
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
    if (toclen > req->cmd.xfer)
        toclen = req->cmd.xfer;
    return toclen;
}

static int scsi_disk_emulate_command(SCSIRequest *req, uint8_t *outbuf)
{
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, req->dev);
    BlockDriverState *bdrv = req->dev->dinfo->bdrv;
    uint64_t nb_sectors;
    int buflen = 0;

    switch (req->cmd.buf[0]) {
    case TEST_UNIT_READY:
        if (!bdrv_is_inserted(bdrv))
            goto not_ready;
	break;
    case REQUEST_SENSE:
        if (req->cmd.xfer < 4)
            goto illegal_request;
        memset(outbuf, 0, 4);
        buflen = 4;
        if (req->dev->sense.key == NOT_READY && req->cmd.xfer >= 18) {
            memset(outbuf, 0, 18);
            buflen = 18;
            outbuf[7] = 10;
            /* asc 0x3a, ascq 0: Medium not present */
            outbuf[12] = 0x3a;
            outbuf[13] = 0;
        }
        outbuf[0] = 0xf0;
        outbuf[1] = 0;
        outbuf[2] = req->dev->sense.key;
        scsi_dev_clear_sense(req->dev);
        break;
    case INQUIRY:
        buflen = scsi_disk_emulate_inquiry(req, outbuf);
        if (buflen < 0)
            goto illegal_request;
	break;
    case MODE_SENSE:
    case MODE_SENSE_10:
        buflen = scsi_disk_emulate_mode_sense(req, outbuf);
        if (buflen < 0)
            goto illegal_request;
        break;
    case READ_TOC:
        buflen = scsi_disk_emulate_read_toc(req, outbuf);
        if (buflen < 0)
            goto illegal_request;
        break;
    case RESERVE:
        if (req->cmd.buf[1] & 1)
            goto illegal_request;
        break;
    case RESERVE_10:
        if (req->cmd.buf[1] & 3)
            goto illegal_request;
        break;
    case RELEASE:
        if (req->cmd.buf[1] & 1)
            goto illegal_request;
        break;
    case RELEASE_10:
        if (req->cmd.buf[1] & 3)
            goto illegal_request;
        break;
    case START_STOP:
        if (bdrv_get_type_hint(bdrv) == BDRV_TYPE_CDROM && (req->cmd.buf[4] & 2)) {
            /* load/eject medium */
            bdrv_eject(bdrv, !(req->cmd.buf[4] & 1));
        }
	break;
    case ALLOW_MEDIUM_REMOVAL:
        bdrv_set_locked(bdrv, req->cmd.buf[4] & 1);
	break;
    case READ_CAPACITY:
        /* The normal LEN field for this command is zero.  */
	memset(outbuf, 0, 8);
	bdrv_get_geometry(bdrv, &nb_sectors);
        if (!nb_sectors)
            goto not_ready;
        nb_sectors /= s->cluster_size;
        /* Returned value is the address of the last sector.  */
        nb_sectors--;
        /* Remember the new size for read/write sanity checking. */
        s->max_lba = nb_sectors;
        /* Clip to 2TB, instead of returning capacity modulo 2TB. */
        if (nb_sectors > UINT32_MAX)
            nb_sectors = UINT32_MAX;
        outbuf[0] = (nb_sectors >> 24) & 0xff;
        outbuf[1] = (nb_sectors >> 16) & 0xff;
        outbuf[2] = (nb_sectors >> 8) & 0xff;
        outbuf[3] = nb_sectors & 0xff;
        outbuf[4] = 0;
        outbuf[5] = 0;
        outbuf[6] = s->cluster_size * 2;
        outbuf[7] = 0;
        buflen = 8;
	break;
    case SYNCHRONIZE_CACHE:
        bdrv_flush(bdrv);
        break;
    case GET_CONFIGURATION:
        memset(outbuf, 0, 8);
        /* ??? This should probably return much more information.  For now
           just return the basic header indicating the CD-ROM profile.  */
        outbuf[7] = 8; // CD-ROM
        buflen = 8;
        break;
    case SERVICE_ACTION_IN:
        /* Service Action In subcommands. */
        if ((req->cmd.buf[1] & 31) == 0x10) {
            DPRINTF("SAI READ CAPACITY(16)\n");
            memset(outbuf, 0, req->cmd.xfer);
            bdrv_get_geometry(bdrv, &nb_sectors);
            if (!nb_sectors)
                goto not_ready;
            nb_sectors /= s->cluster_size;
            /* Returned value is the address of the last sector.  */
            nb_sectors--;
            /* Remember the new size for read/write sanity checking. */
            s->max_lba = nb_sectors;
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
            outbuf[10] = s->cluster_size * 2;
            outbuf[11] = 0;
            /* Protection, exponent and lowest lba field left blank. */
            buflen = req->cmd.xfer;
            break;
        }
        DPRINTF("Unsupported Service Action In\n");
        goto illegal_request;
    case REPORT_LUNS:
        if (req->cmd.xfer < 16)
            goto illegal_request;
        memset(outbuf, 0, 16);
        outbuf[3] = 8;
        buflen = 16;
        break;
    case VERIFY:
        break;
    default:
        goto illegal_request;
    }
    scsi_req_set_status(req, GOOD, NO_SENSE);
    return buflen;

not_ready:
    scsi_req_set_status(req, CHECK_CONDITION, NOT_READY);
    return 0;

illegal_request:
    scsi_req_set_status(req, CHECK_CONDITION, ILLEGAL_REQUEST);
    return 0;
}

/* Execute a scsi command.  Returns the length of the data expected by the
   command.  This will be Positive for data transfers from the device
   (eg. disk reads), negative for transfers to the device (eg. disk writes),
   and zero if the command does not transfer any data.  */

static int32_t scsi_send_command(SCSIDevice *d, uint32_t tag,
                                 uint8_t *buf, int lun)
{
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, d);
    uint64_t lba;
    uint32_t len;
    int cmdlen;
    int is_write;
    uint8_t command;
    uint8_t *outbuf;
    SCSIDiskReq *r;
    int rc;

    command = buf[0];
    r = scsi_find_request(s, tag);
    if (r) {
        BADF("Tag 0x%x already in use\n", tag);
        scsi_cancel_io(d, tag);
    }
    /* ??? Tags are not unique for different luns.  We only implement a
       single lun, so this should not matter.  */
    r = scsi_new_request(d, tag, lun);
    outbuf = (uint8_t *)r->iov.iov_base;
    is_write = 0;
    DPRINTF("Command: lun=%d tag=0x%x data=0x%02x", lun, tag, buf[0]);
    switch (command >> 5) {
    case 0:
        lba = (uint64_t) buf[3] | ((uint64_t) buf[2] << 8) |
              (((uint64_t) buf[1] & 0x1f) << 16);
        len = buf[4];
        cmdlen = 6;
        break;
    case 1:
    case 2:
        lba = (uint64_t) buf[5] | ((uint64_t) buf[4] << 8) |
              ((uint64_t) buf[3] << 16) | ((uint64_t) buf[2] << 24);
        len = buf[8] | (buf[7] << 8);
        cmdlen = 10;
        break;
    case 4:
        lba = (uint64_t) buf[9] | ((uint64_t) buf[8] << 8) |
              ((uint64_t) buf[7] << 16) | ((uint64_t) buf[6] << 24) |
              ((uint64_t) buf[5] << 32) | ((uint64_t) buf[4] << 40) |
              ((uint64_t) buf[3] << 48) | ((uint64_t) buf[2] << 56);
        len = buf[13] | (buf[12] << 8) | (buf[11] << 16) | (buf[10] << 24);
        cmdlen = 16;
        break;
    case 5:
        lba = (uint64_t) buf[5] | ((uint64_t) buf[4] << 8) |
              ((uint64_t) buf[3] << 16) | ((uint64_t) buf[2] << 24);
        len = buf[9] | (buf[8] << 8) | (buf[7] << 16) | (buf[6] << 24);
        cmdlen = 12;
        break;
    default:
        BADF("Unsupported command length, command %x\n", command);
        goto fail;
    }
#ifdef DEBUG_SCSI
    {
        int i;
        for (i = 1; i < cmdlen; i++) {
            printf(" 0x%02x", buf[i]);
        }
        printf("\n");
    }
#endif

    if (scsi_req_parse(&r->req, buf) != 0) {
        BADF("Unsupported command length, command %x\n", command);
        goto fail;
    }
    assert(r->req.cmd.len == cmdlen);
    assert(r->req.cmd.lba == lba);

    if (lun || buf[1] >> 5) {
        /* Only LUN 0 supported.  */
        DPRINTF("Unimplemented LUN %d\n", lun ? lun : buf[1] >> 5);
        if (command != REQUEST_SENSE && command != INQUIRY)
            goto fail;
    }
    switch (command) {
    case TEST_UNIT_READY:
    case REQUEST_SENSE:
    case INQUIRY:
    case MODE_SENSE:
    case MODE_SENSE_10:
    case RESERVE:
    case RESERVE_10:
    case RELEASE:
    case RELEASE_10:
    case START_STOP:
    case ALLOW_MEDIUM_REMOVAL:
    case READ_CAPACITY:
    case SYNCHRONIZE_CACHE:
    case READ_TOC:
    case GET_CONFIGURATION:
    case SERVICE_ACTION_IN:
    case REPORT_LUNS:
    case VERIFY:
        rc = scsi_disk_emulate_command(&r->req, outbuf);
        if (rc > 0) {
            r->iov.iov_len = rc;
        } else {
            scsi_req_complete(&r->req);
            scsi_remove_request(r);
            return 0;
        }
        break;
    case READ_6:
    case READ_10:
    case READ_12:
    case READ_16:
        DPRINTF("Read (sector %" PRId64 ", count %d)\n", lba, len);
        if (lba > s->max_lba)
            goto illegal_lba;
        r->sector = lba * s->cluster_size;
        r->sector_count = len * s->cluster_size;
        break;
    case WRITE_6:
    case WRITE_10:
    case WRITE_12:
    case WRITE_16:
        DPRINTF("Write (sector %" PRId64 ", count %d)\n", lba, len);
        if (lba > s->max_lba)
            goto illegal_lba;
        r->sector = lba * s->cluster_size;
        r->sector_count = len * s->cluster_size;
        is_write = 1;
        break;
    default:
	DPRINTF("Unknown SCSI command (%2.2x)\n", buf[0]);
    fail:
        scsi_command_complete(r, CHECK_CONDITION, ILLEGAL_REQUEST);
	return 0;
    illegal_lba:
        scsi_command_complete(r, CHECK_CONDITION, HARDWARE_ERROR);
        return 0;
    }
    if (r->sector_count == 0 && r->iov.iov_len == 0) {
        scsi_command_complete(r, GOOD, NO_SENSE);
    }
    len = r->sector_count * 512 + r->iov.iov_len;
    if (is_write) {
        return -len;
    } else {
        if (!r->sector_count)
            r->sector_count = -1;
        return len;
    }
}

static void scsi_destroy(SCSIDevice *dev)
{
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, dev);
    SCSIDiskReq *r;

    while (!QTAILQ_EMPTY(&s->qdev.requests)) {
        r = DO_UPCAST(SCSIDiskReq, req, QTAILQ_FIRST(&s->qdev.requests));
        scsi_remove_request(r);
    }
    drive_uninit(s->qdev.dinfo);
}

static int scsi_disk_initfn(SCSIDevice *dev)
{
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, dev);
    uint64_t nb_sectors;

    if (!s->qdev.dinfo || !s->qdev.dinfo->bdrv) {
        qemu_error("scsi-disk: drive property not set\n");
        return -1;
    }

    if (bdrv_get_type_hint(s->qdev.dinfo->bdrv) == BDRV_TYPE_CDROM) {
        s->cluster_size = 4;
    } else {
        s->cluster_size = 1;
    }
    s->qdev.blocksize = 512 * s->cluster_size;
    s->qdev.type = TYPE_DISK;
    bdrv_get_geometry(s->qdev.dinfo->bdrv, &nb_sectors);
    nb_sectors /= s->cluster_size;
    if (nb_sectors)
        nb_sectors--;
    s->max_lba = nb_sectors;
    qemu_add_vm_change_state_handler(scsi_dma_restart_cb, s);
    return 0;
}

static SCSIDeviceInfo scsi_disk_info = {
    .qdev.name    = "scsi-disk",
    .qdev.desc    = "virtual scsi disk or cdrom",
    .qdev.size    = sizeof(SCSIDiskState),
    .init         = scsi_disk_initfn,
    .destroy      = scsi_destroy,
    .send_command = scsi_send_command,
    .read_data    = scsi_read_data,
    .write_data   = scsi_write_data,
    .cancel_io    = scsi_cancel_io,
    .get_buf      = scsi_get_buf,
    .qdev.props   = (Property[]) {
        DEFINE_PROP_DRIVE("drive", SCSIDiskState, qdev.dinfo),
        DEFINE_PROP_END_OF_LIST(),
    },
};

static void scsi_disk_register_devices(void)
{
    scsi_qdev_register(&scsi_disk_info);
}
device_init(scsi_disk_register_devices)
