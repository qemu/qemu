/*
 *  xen paravirt block device backend
 *
 *  (c) Gerd Hoffmann <kraxel@redhat.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; under version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <inttypes.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/uio.h>

#include <xs.h>
#include <xenctrl.h>
#include <xen/io/xenbus.h>

#include "hw.h"
#include "block_int.h"
#include "qemu-char.h"
#include "xen_blkif.h"
#include "xen_backend.h"
#include "blockdev.h"

/* ------------------------------------------------------------- */

static int syncwrite    = 0;
static int batch_maps   = 0;

static int max_requests = 32;
static int use_aio      = 1;

/* ------------------------------------------------------------- */

#define BLOCK_SIZE  512
#define IOCB_COUNT  (BLKIF_MAX_SEGMENTS_PER_REQUEST + 2)

struct ioreq {
    blkif_request_t     req;
    int16_t             status;

    /* parsed request */
    off_t               start;
    QEMUIOVector        v;
    int                 presync;
    int                 postsync;

    /* grant mapping */
    uint32_t            domids[BLKIF_MAX_SEGMENTS_PER_REQUEST];
    uint32_t            refs[BLKIF_MAX_SEGMENTS_PER_REQUEST];
    int                 prot;
    void                *page[BLKIF_MAX_SEGMENTS_PER_REQUEST];
    void                *pages;

    /* aio status */
    int                 aio_inflight;
    int                 aio_errors;

    struct XenBlkDev    *blkdev;
    QLIST_ENTRY(ioreq)   list;
    BlockAcctCookie     acct;
};

struct XenBlkDev {
    struct XenDevice    xendev;  /* must be first */
    char                *params;
    char                *mode;
    char                *type;
    char                *dev;
    char                *devtype;
    const char          *fileproto;
    const char          *filename;
    int                 ring_ref;
    void                *sring;
    int64_t             file_blk;
    int64_t             file_size;
    int                 protocol;
    blkif_back_rings_t  rings;
    int                 more_work;
    int                 cnt_map;

    /* request lists */
    QLIST_HEAD(inflight_head, ioreq) inflight;
    QLIST_HEAD(finished_head, ioreq) finished;
    QLIST_HEAD(freelist_head, ioreq) freelist;
    int                 requests_total;
    int                 requests_inflight;
    int                 requests_finished;

    /* qemu block driver */
    DriveInfo           *dinfo;
    BlockDriverState    *bs;
    QEMUBH              *bh;
};

/* ------------------------------------------------------------- */

static struct ioreq *ioreq_start(struct XenBlkDev *blkdev)
{
    struct ioreq *ioreq = NULL;

    if (QLIST_EMPTY(&blkdev->freelist)) {
        if (blkdev->requests_total >= max_requests) {
            goto out;
        }
        /* allocate new struct */
        ioreq = g_malloc0(sizeof(*ioreq));
        ioreq->blkdev = blkdev;
        blkdev->requests_total++;
        qemu_iovec_init(&ioreq->v, BLKIF_MAX_SEGMENTS_PER_REQUEST);
    } else {
        /* get one from freelist */
        ioreq = QLIST_FIRST(&blkdev->freelist);
        QLIST_REMOVE(ioreq, list);
        qemu_iovec_reset(&ioreq->v);
    }
    QLIST_INSERT_HEAD(&blkdev->inflight, ioreq, list);
    blkdev->requests_inflight++;

out:
    return ioreq;
}

static void ioreq_finish(struct ioreq *ioreq)
{
    struct XenBlkDev *blkdev = ioreq->blkdev;

    QLIST_REMOVE(ioreq, list);
    QLIST_INSERT_HEAD(&blkdev->finished, ioreq, list);
    blkdev->requests_inflight--;
    blkdev->requests_finished++;
}

static void ioreq_release(struct ioreq *ioreq)
{
    struct XenBlkDev *blkdev = ioreq->blkdev;

    QLIST_REMOVE(ioreq, list);
    memset(ioreq, 0, sizeof(*ioreq));
    ioreq->blkdev = blkdev;
    QLIST_INSERT_HEAD(&blkdev->freelist, ioreq, list);
    blkdev->requests_finished--;
}

/*
 * translate request into iovec + start offset
 * do sanity checks along the way
 */
static int ioreq_parse(struct ioreq *ioreq)
{
    struct XenBlkDev *blkdev = ioreq->blkdev;
    uintptr_t mem;
    size_t len;
    int i;

    xen_be_printf(&blkdev->xendev, 3,
                  "op %d, nr %d, handle %d, id %" PRId64 ", sector %" PRId64 "\n",
                  ioreq->req.operation, ioreq->req.nr_segments,
                  ioreq->req.handle, ioreq->req.id, ioreq->req.sector_number);
    switch (ioreq->req.operation) {
    case BLKIF_OP_READ:
        ioreq->prot = PROT_WRITE; /* to memory */
        break;
    case BLKIF_OP_WRITE_BARRIER:
        if (!ioreq->req.nr_segments) {
            ioreq->presync = 1;
            return 0;
        }
        if (!syncwrite) {
            ioreq->presync = ioreq->postsync = 1;
        }
        /* fall through */
    case BLKIF_OP_WRITE:
        ioreq->prot = PROT_READ; /* from memory */
        if (syncwrite) {
            ioreq->postsync = 1;
        }
        break;
    default:
        xen_be_printf(&blkdev->xendev, 0, "error: unknown operation (%d)\n",
                      ioreq->req.operation);
        goto err;
    };

    if (ioreq->req.operation != BLKIF_OP_READ && blkdev->mode[0] != 'w') {
        xen_be_printf(&blkdev->xendev, 0, "error: write req for ro device\n");
        goto err;
    }

    ioreq->start = ioreq->req.sector_number * blkdev->file_blk;
    for (i = 0; i < ioreq->req.nr_segments; i++) {
        if (i == BLKIF_MAX_SEGMENTS_PER_REQUEST) {
            xen_be_printf(&blkdev->xendev, 0, "error: nr_segments too big\n");
            goto err;
        }
        if (ioreq->req.seg[i].first_sect > ioreq->req.seg[i].last_sect) {
            xen_be_printf(&blkdev->xendev, 0, "error: first > last sector\n");
            goto err;
        }
        if (ioreq->req.seg[i].last_sect * BLOCK_SIZE >= XC_PAGE_SIZE) {
            xen_be_printf(&blkdev->xendev, 0, "error: page crossing\n");
            goto err;
        }

        ioreq->domids[i] = blkdev->xendev.dom;
        ioreq->refs[i]   = ioreq->req.seg[i].gref;

        mem = ioreq->req.seg[i].first_sect * blkdev->file_blk;
        len = (ioreq->req.seg[i].last_sect - ioreq->req.seg[i].first_sect + 1) * blkdev->file_blk;
        qemu_iovec_add(&ioreq->v, (void*)mem, len);
    }
    if (ioreq->start + ioreq->v.size > blkdev->file_size) {
        xen_be_printf(&blkdev->xendev, 0, "error: access beyond end of file\n");
        goto err;
    }
    return 0;

err:
    ioreq->status = BLKIF_RSP_ERROR;
    return -1;
}

static void ioreq_unmap(struct ioreq *ioreq)
{
    XenGnttab gnt = ioreq->blkdev->xendev.gnttabdev;
    int i;

    if (ioreq->v.niov == 0) {
        return;
    }
    if (batch_maps) {
        if (!ioreq->pages) {
            return;
        }
        if (xc_gnttab_munmap(gnt, ioreq->pages, ioreq->v.niov) != 0) {
            xen_be_printf(&ioreq->blkdev->xendev, 0, "xc_gnttab_munmap failed: %s\n",
                          strerror(errno));
        }
        ioreq->blkdev->cnt_map -= ioreq->v.niov;
        ioreq->pages = NULL;
    } else {
        for (i = 0; i < ioreq->v.niov; i++) {
            if (!ioreq->page[i]) {
                continue;
            }
            if (xc_gnttab_munmap(gnt, ioreq->page[i], 1) != 0) {
                xen_be_printf(&ioreq->blkdev->xendev, 0, "xc_gnttab_munmap failed: %s\n",
                              strerror(errno));
            }
            ioreq->blkdev->cnt_map--;
            ioreq->page[i] = NULL;
        }
    }
}

static int ioreq_map(struct ioreq *ioreq)
{
    XenGnttab gnt = ioreq->blkdev->xendev.gnttabdev;
    int i;

    if (ioreq->v.niov == 0) {
        return 0;
    }
    if (batch_maps) {
        ioreq->pages = xc_gnttab_map_grant_refs
            (gnt, ioreq->v.niov, ioreq->domids, ioreq->refs, ioreq->prot);
        if (ioreq->pages == NULL) {
            xen_be_printf(&ioreq->blkdev->xendev, 0,
                          "can't map %d grant refs (%s, %d maps)\n",
                          ioreq->v.niov, strerror(errno), ioreq->blkdev->cnt_map);
            return -1;
        }
        for (i = 0; i < ioreq->v.niov; i++) {
            ioreq->v.iov[i].iov_base = ioreq->pages + i * XC_PAGE_SIZE +
                (uintptr_t)ioreq->v.iov[i].iov_base;
        }
        ioreq->blkdev->cnt_map += ioreq->v.niov;
    } else  {
        for (i = 0; i < ioreq->v.niov; i++) {
            ioreq->page[i] = xc_gnttab_map_grant_ref
                (gnt, ioreq->domids[i], ioreq->refs[i], ioreq->prot);
            if (ioreq->page[i] == NULL) {
                xen_be_printf(&ioreq->blkdev->xendev, 0,
                              "can't map grant ref %d (%s, %d maps)\n",
                              ioreq->refs[i], strerror(errno), ioreq->blkdev->cnt_map);
                ioreq_unmap(ioreq);
                return -1;
            }
            ioreq->v.iov[i].iov_base = ioreq->page[i] + (uintptr_t)ioreq->v.iov[i].iov_base;
            ioreq->blkdev->cnt_map++;
        }
    }
    return 0;
}

static int ioreq_runio_qemu_sync(struct ioreq *ioreq)
{
    struct XenBlkDev *blkdev = ioreq->blkdev;
    int i, rc;
    off_t pos;

    if (ioreq->req.nr_segments && ioreq_map(ioreq) == -1) {
        goto err_no_map;
    }
    if (ioreq->presync) {
        bdrv_flush(blkdev->bs);
    }

    switch (ioreq->req.operation) {
    case BLKIF_OP_READ:
        pos = ioreq->start;
        for (i = 0; i < ioreq->v.niov; i++) {
            rc = bdrv_read(blkdev->bs, pos / BLOCK_SIZE,
                           ioreq->v.iov[i].iov_base,
                           ioreq->v.iov[i].iov_len / BLOCK_SIZE);
            if (rc != 0) {
                xen_be_printf(&blkdev->xendev, 0, "rd I/O error (%p, len %zd)\n",
                              ioreq->v.iov[i].iov_base,
                              ioreq->v.iov[i].iov_len);
                goto err;
            }
            pos += ioreq->v.iov[i].iov_len;
        }
        break;
    case BLKIF_OP_WRITE:
    case BLKIF_OP_WRITE_BARRIER:
        if (!ioreq->req.nr_segments) {
            break;
        }
        pos = ioreq->start;
        for (i = 0; i < ioreq->v.niov; i++) {
            rc = bdrv_write(blkdev->bs, pos / BLOCK_SIZE,
                            ioreq->v.iov[i].iov_base,
                            ioreq->v.iov[i].iov_len / BLOCK_SIZE);
            if (rc != 0) {
                xen_be_printf(&blkdev->xendev, 0, "wr I/O error (%p, len %zd)\n",
                              ioreq->v.iov[i].iov_base,
                              ioreq->v.iov[i].iov_len);
                goto err;
            }
            pos += ioreq->v.iov[i].iov_len;
        }
        break;
    default:
        /* unknown operation (shouldn't happen -- parse catches this) */
        goto err;
    }

    if (ioreq->postsync) {
        bdrv_flush(blkdev->bs);
    }
    ioreq->status = BLKIF_RSP_OKAY;

    ioreq_unmap(ioreq);
    ioreq_finish(ioreq);
    return 0;

err:
    ioreq_unmap(ioreq);
err_no_map:
    ioreq_finish(ioreq);
    ioreq->status = BLKIF_RSP_ERROR;
    return -1;
}

static void qemu_aio_complete(void *opaque, int ret)
{
    struct ioreq *ioreq = opaque;

    if (ret != 0) {
        xen_be_printf(&ioreq->blkdev->xendev, 0, "%s I/O error\n",
                      ioreq->req.operation == BLKIF_OP_READ ? "read" : "write");
        ioreq->aio_errors++;
    }

    ioreq->aio_inflight--;
    if (ioreq->aio_inflight > 0) {
        return;
    }

    ioreq->status = ioreq->aio_errors ? BLKIF_RSP_ERROR : BLKIF_RSP_OKAY;
    ioreq_unmap(ioreq);
    ioreq_finish(ioreq);
    bdrv_acct_done(ioreq->blkdev->bs, &ioreq->acct);
    qemu_bh_schedule(ioreq->blkdev->bh);
}

static int ioreq_runio_qemu_aio(struct ioreq *ioreq)
{
    struct XenBlkDev *blkdev = ioreq->blkdev;

    if (ioreq->req.nr_segments && ioreq_map(ioreq) == -1) {
        goto err_no_map;
    }

    ioreq->aio_inflight++;
    if (ioreq->presync) {
        bdrv_flush(blkdev->bs); /* FIXME: aio_flush() ??? */
    }

    switch (ioreq->req.operation) {
    case BLKIF_OP_READ:
        bdrv_acct_start(blkdev->bs, &ioreq->acct, ioreq->v.size, BDRV_ACCT_READ);
        ioreq->aio_inflight++;
        bdrv_aio_readv(blkdev->bs, ioreq->start / BLOCK_SIZE,
                       &ioreq->v, ioreq->v.size / BLOCK_SIZE,
                       qemu_aio_complete, ioreq);
        break;
    case BLKIF_OP_WRITE:
    case BLKIF_OP_WRITE_BARRIER:
        if (!ioreq->req.nr_segments) {
            break;
        }

        bdrv_acct_start(blkdev->bs, &ioreq->acct, ioreq->v.size, BDRV_ACCT_WRITE);
        ioreq->aio_inflight++;
        bdrv_aio_writev(blkdev->bs, ioreq->start / BLOCK_SIZE,
                        &ioreq->v, ioreq->v.size / BLOCK_SIZE,
                        qemu_aio_complete, ioreq);
        break;
    default:
        /* unknown operation (shouldn't happen -- parse catches this) */
        goto err;
    }

    if (ioreq->postsync) {
        bdrv_flush(blkdev->bs); /* FIXME: aio_flush() ??? */
    }
    qemu_aio_complete(ioreq, 0);

    return 0;

err:
    ioreq_unmap(ioreq);
err_no_map:
    ioreq_finish(ioreq);
    ioreq->status = BLKIF_RSP_ERROR;
    return -1;
}

static int blk_send_response_one(struct ioreq *ioreq)
{
    struct XenBlkDev  *blkdev = ioreq->blkdev;
    int               send_notify   = 0;
    int               have_requests = 0;
    blkif_response_t  resp;
    void              *dst;

    resp.id        = ioreq->req.id;
    resp.operation = ioreq->req.operation;
    resp.status    = ioreq->status;

    /* Place on the response ring for the relevant domain. */
    switch (blkdev->protocol) {
    case BLKIF_PROTOCOL_NATIVE:
        dst = RING_GET_RESPONSE(&blkdev->rings.native, blkdev->rings.native.rsp_prod_pvt);
        break;
    case BLKIF_PROTOCOL_X86_32:
        dst = RING_GET_RESPONSE(&blkdev->rings.x86_32_part,
                                blkdev->rings.x86_32_part.rsp_prod_pvt);
        break;
    case BLKIF_PROTOCOL_X86_64:
        dst = RING_GET_RESPONSE(&blkdev->rings.x86_64_part,
                                blkdev->rings.x86_64_part.rsp_prod_pvt);
        break;
    default:
        dst = NULL;
    }
    memcpy(dst, &resp, sizeof(resp));
    blkdev->rings.common.rsp_prod_pvt++;

    RING_PUSH_RESPONSES_AND_CHECK_NOTIFY(&blkdev->rings.common, send_notify);
    if (blkdev->rings.common.rsp_prod_pvt == blkdev->rings.common.req_cons) {
        /*
         * Tail check for pending requests. Allows frontend to avoid
         * notifications if requests are already in flight (lower
         * overheads and promotes batching).
         */
        RING_FINAL_CHECK_FOR_REQUESTS(&blkdev->rings.common, have_requests);
    } else if (RING_HAS_UNCONSUMED_REQUESTS(&blkdev->rings.common)) {
        have_requests = 1;
    }

    if (have_requests) {
        blkdev->more_work++;
    }
    return send_notify;
}

/* walk finished list, send outstanding responses, free requests */
static void blk_send_response_all(struct XenBlkDev *blkdev)
{
    struct ioreq *ioreq;
    int send_notify = 0;

    while (!QLIST_EMPTY(&blkdev->finished)) {
        ioreq = QLIST_FIRST(&blkdev->finished);
        send_notify += blk_send_response_one(ioreq);
        ioreq_release(ioreq);
    }
    if (send_notify) {
        xen_be_send_notify(&blkdev->xendev);
    }
}

static int blk_get_request(struct XenBlkDev *blkdev, struct ioreq *ioreq, RING_IDX rc)
{
    switch (blkdev->protocol) {
    case BLKIF_PROTOCOL_NATIVE:
        memcpy(&ioreq->req, RING_GET_REQUEST(&blkdev->rings.native, rc),
               sizeof(ioreq->req));
        break;
    case BLKIF_PROTOCOL_X86_32:
        blkif_get_x86_32_req(&ioreq->req,
                             RING_GET_REQUEST(&blkdev->rings.x86_32_part, rc));
        break;
    case BLKIF_PROTOCOL_X86_64:
        blkif_get_x86_64_req(&ioreq->req,
                             RING_GET_REQUEST(&blkdev->rings.x86_64_part, rc));
        break;
    }
    return 0;
}

static void blk_handle_requests(struct XenBlkDev *blkdev)
{
    RING_IDX rc, rp;
    struct ioreq *ioreq;

    blkdev->more_work = 0;

    rc = blkdev->rings.common.req_cons;
    rp = blkdev->rings.common.sring->req_prod;
    xen_rmb(); /* Ensure we see queued requests up to 'rp'. */

    if (use_aio) {
        blk_send_response_all(blkdev);
    }
    while (rc != rp) {
        /* pull request from ring */
        if (RING_REQUEST_CONS_OVERFLOW(&blkdev->rings.common, rc)) {
            break;
        }
        ioreq = ioreq_start(blkdev);
        if (ioreq == NULL) {
            blkdev->more_work++;
            break;
        }
        blk_get_request(blkdev, ioreq, rc);
        blkdev->rings.common.req_cons = ++rc;

        /* parse them */
        if (ioreq_parse(ioreq) != 0) {
            if (blk_send_response_one(ioreq)) {
                xen_be_send_notify(&blkdev->xendev);
            }
            ioreq_release(ioreq);
            continue;
        }

        if (use_aio) {
            /* run i/o in aio mode */
            ioreq_runio_qemu_aio(ioreq);
        } else {
            /* run i/o in sync mode */
            ioreq_runio_qemu_sync(ioreq);
        }
    }
    if (!use_aio) {
        blk_send_response_all(blkdev);
    }

    if (blkdev->more_work && blkdev->requests_inflight < max_requests) {
        qemu_bh_schedule(blkdev->bh);
    }
}

/* ------------------------------------------------------------- */

static void blk_bh(void *opaque)
{
    struct XenBlkDev *blkdev = opaque;
    blk_handle_requests(blkdev);
}

static void blk_alloc(struct XenDevice *xendev)
{
    struct XenBlkDev *blkdev = container_of(xendev, struct XenBlkDev, xendev);

    QLIST_INIT(&blkdev->inflight);
    QLIST_INIT(&blkdev->finished);
    QLIST_INIT(&blkdev->freelist);
    blkdev->bh = qemu_bh_new(blk_bh, blkdev);
    if (xen_mode != XEN_EMULATE) {
        batch_maps = 1;
    }
}

static int blk_init(struct XenDevice *xendev)
{
    struct XenBlkDev *blkdev = container_of(xendev, struct XenBlkDev, xendev);
    int index, qflags, have_barriers, info = 0;

    /* read xenstore entries */
    if (blkdev->params == NULL) {
        char *h = NULL;
        blkdev->params = xenstore_read_be_str(&blkdev->xendev, "params");
        if (blkdev->params != NULL) {
            h = strchr(blkdev->params, ':');
        }
        if (h != NULL) {
            blkdev->fileproto = blkdev->params;
            blkdev->filename  = h+1;
            *h = 0;
        } else {
            blkdev->fileproto = "<unset>";
            blkdev->filename  = blkdev->params;
        }
    }
    if (!strcmp("aio", blkdev->fileproto)) {
        blkdev->fileproto = "raw";
    }
    if (blkdev->mode == NULL) {
        blkdev->mode = xenstore_read_be_str(&blkdev->xendev, "mode");
    }
    if (blkdev->type == NULL) {
        blkdev->type = xenstore_read_be_str(&blkdev->xendev, "type");
    }
    if (blkdev->dev == NULL) {
        blkdev->dev = xenstore_read_be_str(&blkdev->xendev, "dev");
    }
    if (blkdev->devtype == NULL) {
        blkdev->devtype = xenstore_read_be_str(&blkdev->xendev, "device-type");
    }

    /* do we have all we need? */
    if (blkdev->params == NULL ||
        blkdev->mode == NULL   ||
        blkdev->type == NULL   ||
        blkdev->dev == NULL) {
        goto out_error;
    }

    /* read-only ? */
    if (strcmp(blkdev->mode, "w") == 0) {
        qflags = BDRV_O_RDWR;
    } else {
        qflags = 0;
        info  |= VDISK_READONLY;
    }

    /* cdrom ? */
    if (blkdev->devtype && !strcmp(blkdev->devtype, "cdrom")) {
        info  |= VDISK_CDROM;
    }

    /* init qemu block driver */
    index = (blkdev->xendev.dev - 202 * 256) / 16;
    blkdev->dinfo = drive_get(IF_XEN, 0, index);
    if (!blkdev->dinfo) {
        /* setup via xenbus -> create new block driver instance */
        xen_be_printf(&blkdev->xendev, 2, "create new bdrv (xenbus setup)\n");
        blkdev->bs = bdrv_new(blkdev->dev);
        if (blkdev->bs) {
            if (bdrv_open(blkdev->bs, blkdev->filename, qflags,
                        bdrv_find_whitelisted_format(blkdev->fileproto)) != 0) {
                bdrv_delete(blkdev->bs);
                blkdev->bs = NULL;
            }
        }
        if (!blkdev->bs) {
            goto out_error;
        }
    } else {
        /* setup via qemu cmdline -> already setup for us */
        xen_be_printf(&blkdev->xendev, 2, "get configured bdrv (cmdline setup)\n");
        blkdev->bs = blkdev->dinfo->bdrv;
    }
    blkdev->file_blk  = BLOCK_SIZE;
    blkdev->file_size = bdrv_getlength(blkdev->bs);
    if (blkdev->file_size < 0) {
        xen_be_printf(&blkdev->xendev, 1, "bdrv_getlength: %d (%s) | drv %s\n",
                      (int)blkdev->file_size, strerror(-blkdev->file_size),
                      blkdev->bs->drv ? blkdev->bs->drv->format_name : "-");
        blkdev->file_size = 0;
    }
    have_barriers = blkdev->bs->drv && blkdev->bs->drv->bdrv_flush ? 1 : 0;

    xen_be_printf(xendev, 1, "type \"%s\", fileproto \"%s\", filename \"%s\","
                  " size %" PRId64 " (%" PRId64 " MB)\n",
                  blkdev->type, blkdev->fileproto, blkdev->filename,
                  blkdev->file_size, blkdev->file_size >> 20);

    /* fill info */
    xenstore_write_be_int(&blkdev->xendev, "feature-barrier", have_barriers);
    xenstore_write_be_int(&blkdev->xendev, "info",            info);
    xenstore_write_be_int(&blkdev->xendev, "sector-size",     blkdev->file_blk);
    xenstore_write_be_int(&blkdev->xendev, "sectors",
                          blkdev->file_size / blkdev->file_blk);
    return 0;

out_error:
    g_free(blkdev->params);
    blkdev->params = NULL;
    g_free(blkdev->mode);
    blkdev->mode = NULL;
    g_free(blkdev->type);
    blkdev->type = NULL;
    g_free(blkdev->dev);
    blkdev->dev = NULL;
    g_free(blkdev->devtype);
    blkdev->devtype = NULL;
    return -1;
}

static int blk_connect(struct XenDevice *xendev)
{
    struct XenBlkDev *blkdev = container_of(xendev, struct XenBlkDev, xendev);

    if (xenstore_read_fe_int(&blkdev->xendev, "ring-ref", &blkdev->ring_ref) == -1) {
        return -1;
    }
    if (xenstore_read_fe_int(&blkdev->xendev, "event-channel",
                             &blkdev->xendev.remote_port) == -1) {
        return -1;
    }

    blkdev->protocol = BLKIF_PROTOCOL_NATIVE;
    if (blkdev->xendev.protocol) {
        if (strcmp(blkdev->xendev.protocol, XEN_IO_PROTO_ABI_X86_32) == 0) {
            blkdev->protocol = BLKIF_PROTOCOL_X86_32;
        }
        if (strcmp(blkdev->xendev.protocol, XEN_IO_PROTO_ABI_X86_64) == 0) {
            blkdev->protocol = BLKIF_PROTOCOL_X86_64;
        }
    }

    blkdev->sring = xc_gnttab_map_grant_ref(blkdev->xendev.gnttabdev,
                                            blkdev->xendev.dom,
                                            blkdev->ring_ref,
                                            PROT_READ | PROT_WRITE);
    if (!blkdev->sring) {
        return -1;
    }
    blkdev->cnt_map++;

    switch (blkdev->protocol) {
    case BLKIF_PROTOCOL_NATIVE:
    {
        blkif_sring_t *sring_native = blkdev->sring;
        BACK_RING_INIT(&blkdev->rings.native, sring_native, XC_PAGE_SIZE);
        break;
    }
    case BLKIF_PROTOCOL_X86_32:
    {
        blkif_x86_32_sring_t *sring_x86_32 = blkdev->sring;

        BACK_RING_INIT(&blkdev->rings.x86_32_part, sring_x86_32, XC_PAGE_SIZE);
        break;
    }
    case BLKIF_PROTOCOL_X86_64:
    {
        blkif_x86_64_sring_t *sring_x86_64 = blkdev->sring;

        BACK_RING_INIT(&blkdev->rings.x86_64_part, sring_x86_64, XC_PAGE_SIZE);
        break;
    }
    }

    xen_be_bind_evtchn(&blkdev->xendev);

    xen_be_printf(&blkdev->xendev, 1, "ok: proto %s, ring-ref %d, "
                  "remote port %d, local port %d\n",
                  blkdev->xendev.protocol, blkdev->ring_ref,
                  blkdev->xendev.remote_port, blkdev->xendev.local_port);
    return 0;
}

static void blk_disconnect(struct XenDevice *xendev)
{
    struct XenBlkDev *blkdev = container_of(xendev, struct XenBlkDev, xendev);

    if (blkdev->bs) {
        if (!blkdev->dinfo) {
            /* close/delete only if we created it ourself */
            bdrv_close(blkdev->bs);
            bdrv_delete(blkdev->bs);
        }
        blkdev->bs = NULL;
    }
    xen_be_unbind_evtchn(&blkdev->xendev);

    if (blkdev->sring) {
        xc_gnttab_munmap(blkdev->xendev.gnttabdev, blkdev->sring, 1);
        blkdev->cnt_map--;
        blkdev->sring = NULL;
    }
}

static int blk_free(struct XenDevice *xendev)
{
    struct XenBlkDev *blkdev = container_of(xendev, struct XenBlkDev, xendev);
    struct ioreq *ioreq;

    while (!QLIST_EMPTY(&blkdev->freelist)) {
        ioreq = QLIST_FIRST(&blkdev->freelist);
        QLIST_REMOVE(ioreq, list);
        qemu_iovec_destroy(&ioreq->v);
        g_free(ioreq);
    }

    g_free(blkdev->params);
    g_free(blkdev->mode);
    g_free(blkdev->type);
    g_free(blkdev->dev);
    g_free(blkdev->devtype);
    qemu_bh_delete(blkdev->bh);
    return 0;
}

static void blk_event(struct XenDevice *xendev)
{
    struct XenBlkDev *blkdev = container_of(xendev, struct XenBlkDev, xendev);

    qemu_bh_schedule(blkdev->bh);
}

struct XenDevOps xen_blkdev_ops = {
    .size       = sizeof(struct XenBlkDev),
    .flags      = DEVOPS_FLAG_NEED_GNTDEV,
    .alloc      = blk_alloc,
    .init       = blk_init,
    .connect    = blk_connect,
    .disconnect = blk_disconnect,
    .event      = blk_event,
    .free       = blk_free,
};
