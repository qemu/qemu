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
 * This code is licenced under the LGPL.
 *
 * Note that this file only handles the SCSI architecture model and device
 * commands.  Emulation of interface/link layer protocols is handled by
 * the host adapter emulator.
 */

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
#include "qemu-error.h"
#include "scsi.h"
#include "scsi-defs.h"
#include "sysemu.h"
#include "blockdev.h"

#define SCSI_DMA_BUF_SIZE    131072
#define SCSI_MAX_INQUIRY_LEN 256

#define SCSI_REQ_STATUS_RETRY           0x01
#define SCSI_REQ_STATUS_RETRY_TYPE_MASK 0x06
#define SCSI_REQ_STATUS_RETRY_READ      0x00
#define SCSI_REQ_STATUS_RETRY_WRITE     0x02
#define SCSI_REQ_STATUS_RETRY_FLUSH     0x04

typedef struct SCSIDiskState SCSIDiskState;

typedef struct SCSIDiskReq {
    SCSIRequest req;
    /* Both sector and sector_count are in terms of qemu 512 byte blocks.  */
    uint64_t sector;
    uint32_t sector_count;
    struct iovec iov;
    QEMUIOVector qiov;
    uint32_t status;
} SCSIDiskReq;

typedef enum { SCSI_HD, SCSI_CD } SCSIDriveKind;

struct SCSIDiskState
{
    SCSIDevice qdev;
    BlockDriverState *bs;
    /* The qemu block layer uses a fixed 512 byte sector size.
       This is the number of 512 byte blocks in a single scsi sector.  */
    int cluster_size;
    uint32_t removable;
    uint64_t max_lba;
    QEMUBH *bh;
    char *version;
    char *serial;
    SCSISense sense;
    SCSIDriveKind drive_kind;
};

static int scsi_handle_rw_error(SCSIDiskReq *r, int error, int type);
static int scsi_disk_emulate_command(SCSIDiskReq *r, uint8_t *outbuf);

static SCSIRequest *scsi_new_request(SCSIDevice *d, uint32_t tag,
                                     uint32_t lun, void *hba_private)
{
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, d);
    SCSIRequest *req;
    SCSIDiskReq *r;

    req = scsi_req_alloc(sizeof(SCSIDiskReq), &s->qdev, tag, lun, hba_private);
    r = DO_UPCAST(SCSIDiskReq, req, req);
    r->iov.iov_base = qemu_blockalign(s->bs, SCSI_DMA_BUF_SIZE);
    return req;
}

static void scsi_free_request(SCSIRequest *req)
{
    SCSIDiskReq *r = DO_UPCAST(SCSIDiskReq, req, req);

    qemu_vfree(r->iov.iov_base);
}

static void scsi_disk_clear_sense(SCSIDiskState *s)
{
    memset(&s->sense, 0, sizeof(s->sense));
}

static void scsi_req_set_status(SCSIDiskReq *r, int status, SCSISense sense)
{
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, r->req.dev);

    r->req.status = status;
    s->sense = sense;
}

/* Helper function for command completion.  */
static void scsi_command_complete(SCSIDiskReq *r, int status, SCSISense sense)
{
    DPRINTF("Command complete tag=0x%x status=%d sense=%d/%d/%d\n",
            r->req.tag, status, sense.key, sense.asc, sense.ascq);
    scsi_req_set_status(r, status, sense);
    scsi_req_complete(&r->req);
}

/* Cancel a pending data transfer.  */
static void scsi_cancel_io(SCSIRequest *req)
{
    SCSIDiskReq *r = DO_UPCAST(SCSIDiskReq, req, req);

    DPRINTF("Cancel tag=0x%x\n", req->tag);
    if (r->req.aiocb) {
        bdrv_aio_cancel(r->req.aiocb);
    }
    r->req.aiocb = NULL;
}

static void scsi_read_complete(void * opaque, int ret)
{
    SCSIDiskReq *r = (SCSIDiskReq *)opaque;
    int n;

    r->req.aiocb = NULL;

    if (ret) {
        if (scsi_handle_rw_error(r, -ret, SCSI_REQ_STATUS_RETRY_READ)) {
            return;
        }
    }

    DPRINTF("Data ready tag=0x%x len=%zd\n", r->req.tag, r->iov.iov_len);

    n = r->iov.iov_len / 512;
    r->sector += n;
    r->sector_count -= n;
    scsi_req_data(&r->req, r->iov.iov_len);
}


/* Read more data from scsi device into buffer.  */
static void scsi_read_data(SCSIRequest *req)
{
    SCSIDiskReq *r = DO_UPCAST(SCSIDiskReq, req, req);
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, r->req.dev);
    uint32_t n;

    if (r->sector_count == (uint32_t)-1) {
        DPRINTF("Read buf_len=%zd\n", r->iov.iov_len);
        r->sector_count = 0;
        scsi_req_data(&r->req, r->iov.iov_len);
        return;
    }
    DPRINTF("Read sector_count=%d\n", r->sector_count);
    if (r->sector_count == 0) {
        scsi_command_complete(r, GOOD, SENSE_CODE(NO_SENSE));
        return;
    }

    /* No data transfer may already be in progress */
    assert(r->req.aiocb == NULL);

    if (r->req.cmd.mode == SCSI_XFER_TO_DEV) {
        DPRINTF("Data transfer direction invalid\n");
        scsi_read_complete(r, -EINVAL);
        return;
    }

    n = r->sector_count;
    if (n > SCSI_DMA_BUF_SIZE / 512)
        n = SCSI_DMA_BUF_SIZE / 512;

    r->iov.iov_len = n * 512;
    qemu_iovec_init_external(&r->qiov, &r->iov, 1);
    r->req.aiocb = bdrv_aio_readv(s->bs, r->sector, &r->qiov, n,
                              scsi_read_complete, r);
    if (r->req.aiocb == NULL) {
        scsi_read_complete(r, -EIO);
    }
}

static int scsi_handle_rw_error(SCSIDiskReq *r, int error, int type)
{
    int is_read = (type == SCSI_REQ_STATUS_RETRY_READ);
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, r->req.dev);
    BlockErrorAction action = bdrv_get_on_error(s->bs, is_read);

    if (action == BLOCK_ERR_IGNORE) {
        bdrv_mon_event(s->bs, BDRV_ACTION_IGNORE, is_read);
        return 0;
    }

    if ((error == ENOSPC && action == BLOCK_ERR_STOP_ENOSPC)
            || action == BLOCK_ERR_STOP_ANY) {

        type &= SCSI_REQ_STATUS_RETRY_TYPE_MASK;
        r->status |= SCSI_REQ_STATUS_RETRY | type;

        bdrv_mon_event(s->bs, BDRV_ACTION_STOP, is_read);
        vm_stop(VMSTOP_DISKFULL);
    } else {
        if (type == SCSI_REQ_STATUS_RETRY_READ) {
            scsi_req_data(&r->req, 0);
        }
        switch (error) {
        case ENOMEM:
            scsi_command_complete(r, CHECK_CONDITION,
                                  SENSE_CODE(TARGET_FAILURE));
            break;
        case EINVAL:
            scsi_command_complete(r, CHECK_CONDITION,
                                  SENSE_CODE(INVALID_FIELD));
            break;
        default:
            scsi_command_complete(r, CHECK_CONDITION,
                                  SENSE_CODE(IO_ERROR));
            break;
        }
        bdrv_mon_event(s->bs, BDRV_ACTION_REPORT, is_read);
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
        if (scsi_handle_rw_error(r, -ret, SCSI_REQ_STATUS_RETRY_WRITE)) {
            return;
        }
    }

    n = r->iov.iov_len / 512;
    r->sector += n;
    r->sector_count -= n;
    if (r->sector_count == 0) {
        scsi_command_complete(r, GOOD, SENSE_CODE(NO_SENSE));
    } else {
        len = r->sector_count * 512;
        if (len > SCSI_DMA_BUF_SIZE) {
            len = SCSI_DMA_BUF_SIZE;
        }
        r->iov.iov_len = len;
        DPRINTF("Write complete tag=0x%x more=%d\n", r->req.tag, len);
        scsi_req_data(&r->req, len);
    }
}

static void scsi_write_data(SCSIRequest *req)
{
    SCSIDiskReq *r = DO_UPCAST(SCSIDiskReq, req, req);
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, r->req.dev);
    uint32_t n;

    /* No data transfer may already be in progress */
    assert(r->req.aiocb == NULL);

    if (r->req.cmd.mode != SCSI_XFER_TO_DEV) {
        DPRINTF("Data transfer direction invalid\n");
        scsi_write_complete(r, -EINVAL);
        return;
    }

    n = r->iov.iov_len / 512;
    if (n) {
        qemu_iovec_init_external(&r->qiov, &r->iov, 1);
        r->req.aiocb = bdrv_aio_writev(s->bs, r->sector, &r->qiov, n,
                                   scsi_write_complete, r);
        if (r->req.aiocb == NULL) {
            scsi_write_complete(r, -ENOMEM);
        }
    } else {
        /* Invoke completion routine to fetch data from host.  */
        scsi_write_complete(r, 0);
    }
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
            int status = r->status;
            int ret;

            r->status &=
                ~(SCSI_REQ_STATUS_RETRY | SCSI_REQ_STATUS_RETRY_TYPE_MASK);

            switch (status & SCSI_REQ_STATUS_RETRY_TYPE_MASK) {
            case SCSI_REQ_STATUS_RETRY_READ:
                scsi_read_data(&r->req);
                break;
            case SCSI_REQ_STATUS_RETRY_WRITE:
                scsi_write_data(&r->req);
                break;
            case SCSI_REQ_STATUS_RETRY_FLUSH:
                ret = scsi_disk_emulate_command(r, r->iov.iov_base);
                if (ret == 0) {
                    scsi_command_complete(r, GOOD, SENSE_CODE(NO_SENSE));
                }
            }
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
static uint8_t *scsi_get_buf(SCSIRequest *req)
{
    SCSIDiskReq *r = DO_UPCAST(SCSIDiskReq, req, req);

    return (uint8_t *)r->iov.iov_base;
}

/* Copy sense information into the provided buffer */
static int scsi_get_sense(SCSIRequest *req, uint8_t *outbuf, int len)
{
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, req->dev);

    return scsi_build_sense(s->sense, outbuf, len, len > 14);
}

static int scsi_disk_emulate_inquiry(SCSIRequest *req, uint8_t *outbuf)
{
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, req->dev);
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

        if (s->drive_kind == SCSI_CD) {
            outbuf[buflen++] = 5;
        } else {
            outbuf[buflen++] = 0;
        }
        outbuf[buflen++] = page_code ; // this page
        outbuf[buflen++] = 0x00;

        switch (page_code) {
        case 0x00: /* Supported page codes, mandatory */
        {
            int pages;
            DPRINTF("Inquiry EVPD[Supported pages] "
                    "buffer size %zd\n", req->cmd.xfer);
            pages = buflen++;
            outbuf[buflen++] = 0x00; // list of supported pages (this page)
            outbuf[buflen++] = 0x80; // unit serial number
            outbuf[buflen++] = 0x83; // device identification
            if (s->drive_kind == SCSI_HD) {
                outbuf[buflen++] = 0xb0; // block limits
                outbuf[buflen++] = 0xb2; // thin provisioning
            }
            outbuf[pages] = buflen - pages - 1; // number of pages
            break;
        }
        case 0x80: /* Device serial number, optional */
        {
            int l = strlen(s->serial);

            if (l > req->cmd.xfer)
                l = req->cmd.xfer;
            if (l > 20)
                l = 20;

            DPRINTF("Inquiry EVPD[Serial number] "
                    "buffer size %zd\n", req->cmd.xfer);
            outbuf[buflen++] = l;
            memcpy(outbuf+buflen, s->serial, l);
            buflen += l;
            break;
        }

        case 0x83: /* Device identification page, mandatory */
        {
            int max_len = 255 - 8;
            int id_len = strlen(bdrv_get_device_name(s->bs));

            if (id_len > max_len)
                id_len = max_len;
            DPRINTF("Inquiry EVPD[Device identification] "
                    "buffer size %zd\n", req->cmd.xfer);

            outbuf[buflen++] = 4 + id_len;
            outbuf[buflen++] = 0x2; // ASCII
            outbuf[buflen++] = 0;   // not officially assigned
            outbuf[buflen++] = 0;   // reserved
            outbuf[buflen++] = id_len; // length of data following

            memcpy(outbuf+buflen, bdrv_get_device_name(s->bs), id_len);
            buflen += id_len;
            break;
        }
        case 0xb0: /* block limits */
        {
            unsigned int unmap_sectors =
                    s->qdev.conf.discard_granularity / s->qdev.blocksize;
            unsigned int min_io_size =
                    s->qdev.conf.min_io_size / s->qdev.blocksize;
            unsigned int opt_io_size =
                    s->qdev.conf.opt_io_size / s->qdev.blocksize;

            if (s->drive_kind == SCSI_CD) {
                DPRINTF("Inquiry (EVPD[%02X] not supported for CDROM\n",
                        page_code);
                return -1;
            }
            /* required VPD size with unmap support */
            outbuf[3] = buflen = 0x3c;

            memset(outbuf + 4, 0, buflen - 4);

            /* optimal transfer length granularity */
            outbuf[6] = (min_io_size >> 8) & 0xff;
            outbuf[7] = min_io_size & 0xff;

            /* optimal transfer length */
            outbuf[12] = (opt_io_size >> 24) & 0xff;
            outbuf[13] = (opt_io_size >> 16) & 0xff;
            outbuf[14] = (opt_io_size >> 8) & 0xff;
            outbuf[15] = opt_io_size & 0xff;

            /* optimal unmap granularity */
            outbuf[28] = (unmap_sectors >> 24) & 0xff;
            outbuf[29] = (unmap_sectors >> 16) & 0xff;
            outbuf[30] = (unmap_sectors >> 8) & 0xff;
            outbuf[31] = unmap_sectors & 0xff;
            break;
        }
        case 0xb2: /* thin provisioning */
        {
            outbuf[3] = buflen = 8;
            outbuf[4] = 0;
            outbuf[5] = 0x40; /* write same with unmap supported */
            outbuf[6] = 0;
            outbuf[7] = 0;
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

    buflen = req->cmd.xfer;
    if (buflen > SCSI_MAX_INQUIRY_LEN)
        buflen = SCSI_MAX_INQUIRY_LEN;

    memset(outbuf, 0, buflen);

    if (req->lun) {
        outbuf[0] = 0x7f;	/* LUN not supported */
        return buflen;
    }

    if (s->drive_kind == SCSI_CD) {
        outbuf[0] = 5;
        outbuf[1] = 0x80;
        memcpy(&outbuf[16], "QEMU CD-ROM     ", 16);
    } else {
        outbuf[0] = 0;
        outbuf[1] = s->removable ? 0x80 : 0;
        memcpy(&outbuf[16], "QEMU HARDDISK   ", 16);
    }
    memcpy(&outbuf[8], "QEMU    ", 8);
    memset(&outbuf[32], 0, 4);
    memcpy(&outbuf[32], s->version, MIN(4, strlen(s->version)));
    /*
     * We claim conformance to SPC-3, which is required for guests
     * to ask for modern features like READ CAPACITY(16) or the
     * block characteristics VPD page by default.  Not all of SPC-3
     * is actually implemented, but we're good enough.
     */
    outbuf[2] = 5;
    outbuf[3] = 2; /* Format 2 */

    if (buflen > 36) {
        outbuf[4] = buflen - 5; /* Additional Length = (Len - 1) - 4 */
    } else {
        /* If the allocation length of CDB is too small,
               the additional length is not adjusted */
        outbuf[4] = 36 - 5;
    }

    /* Sync data transfer and TCQ.  */
    outbuf[7] = 0x10 | (req->bus->tcq ? 0x02 : 0);
    return buflen;
}

static int mode_sense_page(SCSIRequest *req, int page, uint8_t *p,
                           int page_control)
{
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, req->dev);
    BlockDriverState *bdrv = s->bs;
    int cylinders, heads, secs;

    /*
     * If Changeable Values are requested, a mask denoting those mode parameters
     * that are changeable shall be returned. As we currently don't support
     * parameter changes via MODE_SELECT all bits are returned set to zero.
     * The buffer was already menset to zero by the caller of this function.
     */
    switch (page) {
    case 4: /* Rigid disk device geometry page. */
        p[0] = 4;
        p[1] = 0x16;
        if (page_control == 1) { /* Changeable Values */
            return p[1] + 2;
        }
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
        return p[1] + 2;

    case 5: /* Flexible disk device geometry page. */
        p[0] = 5;
        p[1] = 0x1e;
        if (page_control == 1) { /* Changeable Values */
            return p[1] + 2;
        }
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
        return p[1] + 2;

    case 8: /* Caching page.  */
        p[0] = 8;
        p[1] = 0x12;
        if (page_control == 1) { /* Changeable Values */
            return p[1] + 2;
        }
        if (bdrv_enable_write_cache(s->bs)) {
            p[2] = 4; /* WCE */
        }
        return p[1] + 2;

    case 0x2a: /* CD Capabilities and Mechanical Status page. */
        if (s->drive_kind != SCSI_CD)
            return 0;
        p[0] = 0x2a;
        p[1] = 0x14;
        if (page_control == 1) { /* Changeable Values */
            return p[1] + 2;
        }
        p[2] = 3; // CD-R & CD-RW read
        p[3] = 0; // Writing not supported
        p[4] = 0x7f; /* Audio, composite, digital out,
                        mode 2 form 1&2, multi session */
        p[5] = 0xff; /* CD DA, DA accurate, RW supported,
                        RW corrected, C2 errors, ISRC,
                        UPC, Bar code */
        p[6] = 0x2d | (bdrv_is_locked(s->bs)? 2 : 0);
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
        return p[1] + 2;

    default:
        return 0;
    }
}

static int scsi_disk_emulate_mode_sense(SCSIRequest *req, uint8_t *outbuf)
{
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, req->dev);
    uint64_t nb_sectors;
    int page, dbd, buflen, page_control;
    uint8_t *p;
    uint8_t dev_specific_param;

    dbd = req->cmd.buf[1]  & 0x8;
    page = req->cmd.buf[2] & 0x3f;
    page_control = (req->cmd.buf[2] & 0xc0) >> 6;
    DPRINTF("Mode Sense(%d) (page %d, xfer %zd, page_control %d)\n",
        (req->cmd.buf[0] == MODE_SENSE) ? 6 : 10, page, req->cmd.xfer, page_control);
    memset(outbuf, 0, req->cmd.xfer);
    p = outbuf;

    if (bdrv_is_read_only(s->bs)) {
        dev_specific_param = 0x80; /* Readonly.  */
    } else {
        dev_specific_param = 0x00;
    }

    if (req->cmd.buf[0] == MODE_SENSE) {
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

    bdrv_get_geometry(s->bs, &nb_sectors);
    if (!dbd && nb_sectors) {
        if (req->cmd.buf[0] == MODE_SENSE) {
            outbuf[3] = 8; /* Block descriptor length  */
        } else { /* MODE_SENSE_10 */
            outbuf[7] = 8; /* Block descriptor length  */
        }
        nb_sectors /= s->cluster_size;
        if (nb_sectors > 0xffffff)
            nb_sectors = 0;
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

    if (page_control == 3) { /* Saved Values */
        return -1; /* ILLEGAL_REQUEST */
    }

    switch (page) {
    case 0x04:
    case 0x05:
    case 0x08:
    case 0x2a:
        p += mode_sense_page(req, page, p, page_control);
        break;
    case 0x3f:
        p += mode_sense_page(req, 0x08, p, page_control);
        p += mode_sense_page(req, 0x2a, p, page_control);
        break;
    default:
        return -1; /* ILLEGAL_REQUEST */
    }

    buflen = p - outbuf;
    /*
     * The mode data length field specifies the length in bytes of the
     * following data that is available to be transferred. The mode data
     * length does not include itself.
     */
    if (req->cmd.buf[0] == MODE_SENSE) {
        outbuf[0] = buflen - 1;
    } else { /* MODE_SENSE_10 */
        outbuf[0] = ((buflen - 2) >> 8) & 0xff;
        outbuf[1] = (buflen - 2) & 0xff;
    }
    if (buflen > req->cmd.xfer)
        buflen = req->cmd.xfer;
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
    bdrv_get_geometry(s->bs, &nb_sectors);
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

static int scsi_disk_emulate_command(SCSIDiskReq *r, uint8_t *outbuf)
{
    SCSIRequest *req = &r->req;
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, req->dev);
    uint64_t nb_sectors;
    int buflen = 0;
    int ret;

    switch (req->cmd.buf[0]) {
    case TEST_UNIT_READY:
        if (!bdrv_is_inserted(s->bs))
            goto not_ready;
	break;
    case REQUEST_SENSE:
        if (req->cmd.xfer < 4)
            goto illegal_request;
        buflen = scsi_build_sense(s->sense, outbuf, req->cmd.xfer,
                                  req->cmd.xfer > 13);
        scsi_disk_clear_sense(s);
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
        if (s->drive_kind == SCSI_CD && (req->cmd.buf[4] & 2)) {
            /* load/eject medium */
            bdrv_eject(s->bs, !(req->cmd.buf[4] & 1));
        }
	break;
    case ALLOW_MEDIUM_REMOVAL:
        bdrv_set_locked(s->bs, req->cmd.buf[4] & 1);
	break;
    case READ_CAPACITY:
        /* The normal LEN field for this command is zero.  */
	memset(outbuf, 0, 8);
	bdrv_get_geometry(s->bs, &nb_sectors);
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
        ret = bdrv_flush(s->bs);
        if (ret < 0) {
            if (scsi_handle_rw_error(r, -ret, SCSI_REQ_STATUS_RETRY_FLUSH)) {
                return -1;
            }
        }
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
            bdrv_get_geometry(s->bs, &nb_sectors);
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
            outbuf[12] = 0;
            outbuf[13] = get_physical_block_exp(&s->qdev.conf);

            /* set TPE bit if the format supports discard */
            if (s->qdev.conf.discard_granularity) {
                outbuf[14] = 0x80;
            }

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
    case REZERO_UNIT:
        DPRINTF("Rezero Unit\n");
        if (!bdrv_is_inserted(s->bs)) {
            goto not_ready;
        }
        break;
    default:
        scsi_command_complete(r, CHECK_CONDITION, SENSE_CODE(INVALID_OPCODE));
        return -1;
    }
    scsi_req_set_status(r, GOOD, SENSE_CODE(NO_SENSE));
    return buflen;

not_ready:
    if (!bdrv_is_inserted(s->bs)) {
        scsi_command_complete(r, CHECK_CONDITION, SENSE_CODE(NO_MEDIUM));
    } else {
        scsi_command_complete(r, CHECK_CONDITION, SENSE_CODE(LUN_NOT_READY));
    }
    return -1;

illegal_request:
    scsi_command_complete(r, CHECK_CONDITION, SENSE_CODE(INVALID_FIELD));
    return -1;
}

/* Execute a scsi command.  Returns the length of the data expected by the
   command.  This will be Positive for data transfers from the device
   (eg. disk reads), negative for transfers to the device (eg. disk writes),
   and zero if the command does not transfer any data.  */

static int32_t scsi_send_command(SCSIRequest *req, uint8_t *buf)
{
    SCSIDiskReq *r = DO_UPCAST(SCSIDiskReq, req, req);
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, req->dev);
    int32_t len;
    uint8_t command;
    uint8_t *outbuf;
    int rc;

    command = buf[0];
    outbuf = (uint8_t *)r->iov.iov_base;
    DPRINTF("Command: lun=%d tag=0x%x data=0x%02x", req->lun, req->tag, buf[0]);

    if (scsi_req_parse(&r->req, buf) != 0) {
        BADF("Unsupported command length, command %x\n", command);
        scsi_command_complete(r, CHECK_CONDITION, SENSE_CODE(INVALID_OPCODE));
        return 0;
    }
#ifdef DEBUG_SCSI
    {
        int i;
        for (i = 1; i < r->req.cmd.len; i++) {
            printf(" 0x%02x", buf[i]);
        }
        printf("\n");
    }
#endif

    if (req->lun) {
        /* Only LUN 0 supported.  */
        DPRINTF("Unimplemented LUN %d\n", req->lun);
        if (command != REQUEST_SENSE && command != INQUIRY) {
            scsi_command_complete(r, CHECK_CONDITION,
                                  SENSE_CODE(LUN_NOT_SUPPORTED));
            return 0;
        }
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
    case REZERO_UNIT:
        rc = scsi_disk_emulate_command(r, outbuf);
        if (rc < 0) {
            return 0;
        }

        r->iov.iov_len = rc;
        break;
    case READ_6:
    case READ_10:
    case READ_12:
    case READ_16:
        len = r->req.cmd.xfer / s->qdev.blocksize;
        DPRINTF("Read (sector %" PRId64 ", count %d)\n", r->req.cmd.lba, len);
        if (r->req.cmd.lba > s->max_lba)
            goto illegal_lba;
        r->sector = r->req.cmd.lba * s->cluster_size;
        r->sector_count = len * s->cluster_size;
        break;
    case WRITE_6:
    case WRITE_10:
    case WRITE_12:
    case WRITE_16:
    case WRITE_VERIFY:
    case WRITE_VERIFY_12:
    case WRITE_VERIFY_16:
        len = r->req.cmd.xfer / s->qdev.blocksize;
        DPRINTF("Write %s(sector %" PRId64 ", count %d)\n",
                (command & 0xe) == 0xe ? "And Verify " : "",
                r->req.cmd.lba, len);
        if (r->req.cmd.lba > s->max_lba)
            goto illegal_lba;
        r->sector = r->req.cmd.lba * s->cluster_size;
        r->sector_count = len * s->cluster_size;
        break;
    case MODE_SELECT:
        DPRINTF("Mode Select(6) (len %lu)\n", (long)r->req.cmd.xfer);
        /* We don't support mode parameter changes.
           Allow the mode parameter header + block descriptors only. */
        if (r->req.cmd.xfer > 12) {
            goto fail;
        }
        break;
    case MODE_SELECT_10:
        DPRINTF("Mode Select(10) (len %lu)\n", (long)r->req.cmd.xfer);
        /* We don't support mode parameter changes.
           Allow the mode parameter header + block descriptors only. */
        if (r->req.cmd.xfer > 16) {
            goto fail;
        }
        break;
    case SEEK_6:
    case SEEK_10:
        DPRINTF("Seek(%d) (sector %" PRId64 ")\n", command == SEEK_6 ? 6 : 10,
                r->req.cmd.lba);
        if (r->req.cmd.lba > s->max_lba) {
            goto illegal_lba;
        }
        break;
    case WRITE_SAME_16:
        len = r->req.cmd.xfer / s->qdev.blocksize;

        DPRINTF("WRITE SAME(16) (sector %" PRId64 ", count %d)\n",
                r->req.cmd.lba, len);

        if (r->req.cmd.lba > s->max_lba) {
            goto illegal_lba;
        }

        /*
         * We only support WRITE SAME with the unmap bit set for now.
         */
        if (!(buf[1] & 0x8)) {
            goto fail;
        }

        rc = bdrv_discard(s->bs, r->req.cmd.lba * s->cluster_size,
                          len * s->cluster_size);
        if (rc < 0) {
            /* XXX: better error code ?*/
            goto fail;
        }

        break;
    default:
        DPRINTF("Unknown SCSI command (%2.2x)\n", buf[0]);
        scsi_command_complete(r, CHECK_CONDITION, SENSE_CODE(INVALID_OPCODE));
        return 0;
    fail:
        scsi_command_complete(r, CHECK_CONDITION, SENSE_CODE(INVALID_FIELD));
        return 0;
    illegal_lba:
        scsi_command_complete(r, CHECK_CONDITION, SENSE_CODE(LBA_OUT_OF_RANGE));
        return 0;
    }
    if (r->sector_count == 0 && r->iov.iov_len == 0) {
        scsi_command_complete(r, GOOD, SENSE_CODE(NO_SENSE));
    }
    len = r->sector_count * 512 + r->iov.iov_len;
    if (r->req.cmd.mode == SCSI_XFER_TO_DEV) {
        return -len;
    } else {
        if (!r->sector_count)
            r->sector_count = -1;
        return len;
    }
}

static void scsi_disk_reset(DeviceState *dev)
{
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev.qdev, dev);
    uint64_t nb_sectors;

    scsi_device_purge_requests(&s->qdev);

    bdrv_get_geometry(s->bs, &nb_sectors);
    nb_sectors /= s->cluster_size;
    if (nb_sectors) {
        nb_sectors--;
    }
    s->max_lba = nb_sectors;
}

static void scsi_destroy(SCSIDevice *dev)
{
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, dev);

    scsi_device_purge_requests(&s->qdev);
    blockdev_mark_auto_del(s->qdev.conf.bs);
}

static int scsi_initfn(SCSIDevice *dev, SCSIDriveKind kind)
{
    SCSIDiskState *s = DO_UPCAST(SCSIDiskState, qdev, dev);
    DriveInfo *dinfo;

    if (!s->qdev.conf.bs) {
        error_report("scsi-disk: drive property not set");
        return -1;
    }
    s->bs = s->qdev.conf.bs;
    s->drive_kind = kind;

    if (kind == SCSI_HD && !bdrv_is_inserted(s->bs)) {
        error_report("Device needs media, but drive is empty");
        return -1;
    }

    if (!s->serial) {
        /* try to fall back to value set with legacy -drive serial=... */
        dinfo = drive_get_by_blockdev(s->bs);
        s->serial = qemu_strdup(*dinfo->serial ? dinfo->serial : "0");
    }

    if (!s->version) {
        s->version = qemu_strdup(QEMU_VERSION);
    }

    if (bdrv_is_sg(s->bs)) {
        error_report("scsi-disk: unwanted /dev/sg*");
        return -1;
    }

    if (kind == SCSI_CD) {
        s->qdev.blocksize = 2048;
    } else {
        s->qdev.blocksize = s->qdev.conf.logical_block_size;
    }
    s->cluster_size = s->qdev.blocksize / 512;
    s->bs->buffer_alignment = s->qdev.blocksize;

    s->qdev.type = TYPE_DISK;
    qemu_add_vm_change_state_handler(scsi_dma_restart_cb, s);
    bdrv_set_removable(s->bs, kind == SCSI_CD);
    add_boot_device_path(s->qdev.conf.bootindex, &dev->qdev, ",0");
    return 0;
}

static int scsi_hd_initfn(SCSIDevice *dev)
{
    return scsi_initfn(dev, SCSI_HD);
}

static int scsi_cd_initfn(SCSIDevice *dev)
{
    return scsi_initfn(dev, SCSI_CD);
}

static int scsi_disk_initfn(SCSIDevice *dev)
{
    SCSIDriveKind kind;
    DriveInfo *dinfo;

    if (!dev->conf.bs) {
        kind = SCSI_HD;         /* will die in scsi_initfn() */
    } else {
        dinfo = drive_get_by_blockdev(dev->conf.bs);
        kind = dinfo->media_cd ? SCSI_CD : SCSI_HD;
    }

    return scsi_initfn(dev, kind);
}

#define DEFINE_SCSI_DISK_PROPERTIES()                           \
    DEFINE_BLOCK_PROPERTIES(SCSIDiskState, qdev.conf),          \
    DEFINE_PROP_STRING("ver",  SCSIDiskState, version),         \
    DEFINE_PROP_STRING("serial",  SCSIDiskState, serial)

static SCSIDeviceInfo scsi_disk_info[] = {
    {
        .qdev.name    = "scsi-hd",
        .qdev.fw_name = "disk",
        .qdev.desc    = "virtual SCSI disk",
        .qdev.size    = sizeof(SCSIDiskState),
        .qdev.reset   = scsi_disk_reset,
        .init         = scsi_hd_initfn,
        .destroy      = scsi_destroy,
        .alloc_req    = scsi_new_request,
        .free_req     = scsi_free_request,
        .send_command = scsi_send_command,
        .read_data    = scsi_read_data,
        .write_data   = scsi_write_data,
        .cancel_io    = scsi_cancel_io,
        .get_buf      = scsi_get_buf,
        .get_sense    = scsi_get_sense,
        .qdev.props   = (Property[]) {
            DEFINE_SCSI_DISK_PROPERTIES(),
            DEFINE_PROP_BIT("removable", SCSIDiskState, removable, 0, false),
            DEFINE_PROP_END_OF_LIST(),
        }
    },{
        .qdev.name    = "scsi-cd",
        .qdev.fw_name = "disk",
        .qdev.desc    = "virtual SCSI CD-ROM",
        .qdev.size    = sizeof(SCSIDiskState),
        .qdev.reset   = scsi_disk_reset,
        .init         = scsi_cd_initfn,
        .destroy      = scsi_destroy,
        .alloc_req    = scsi_new_request,
        .free_req     = scsi_free_request,
        .send_command = scsi_send_command,
        .read_data    = scsi_read_data,
        .write_data   = scsi_write_data,
        .cancel_io    = scsi_cancel_io,
        .get_buf      = scsi_get_buf,
        .get_sense    = scsi_get_sense,
        .qdev.props   = (Property[]) {
            DEFINE_SCSI_DISK_PROPERTIES(),
            DEFINE_PROP_END_OF_LIST(),
        },
    },{
        .qdev.name    = "scsi-disk", /* legacy -device scsi-disk */
        .qdev.fw_name = "disk",
        .qdev.desc    = "virtual SCSI disk or CD-ROM (legacy)",
        .qdev.size    = sizeof(SCSIDiskState),
        .qdev.reset   = scsi_disk_reset,
        .init         = scsi_disk_initfn,
        .destroy      = scsi_destroy,
        .alloc_req    = scsi_new_request,
        .free_req     = scsi_free_request,
        .send_command = scsi_send_command,
        .read_data    = scsi_read_data,
        .write_data   = scsi_write_data,
        .cancel_io    = scsi_cancel_io,
        .get_buf      = scsi_get_buf,
        .get_sense    = scsi_get_sense,
        .qdev.props   = (Property[]) {
            DEFINE_SCSI_DISK_PROPERTIES(),
            DEFINE_PROP_BIT("removable", SCSIDiskState, removable, 0, false),
            DEFINE_PROP_END_OF_LIST(),
        }
    }
};

static void scsi_disk_register_devices(void)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(scsi_disk_info); i++) {
        scsi_qdev_register(&scsi_disk_info[i]);
    }
}
device_init(scsi_disk_register_devices)
