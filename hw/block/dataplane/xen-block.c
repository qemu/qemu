/*
 * Copyright (c) 2018  Citrix Systems Inc.
 * (c) Gerd Hoffmann <kraxel@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; under version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/xen/xen_common.h"
#include "hw/block/xen_blkif.h"
#include "sysemu/block-backend.h"
#include "sysemu/iothread.h"
#include "xen-block.h"

struct ioreq {
    blkif_request_t req;
    int16_t status;
    off_t start;
    QEMUIOVector v;
    void *buf;
    size_t size;
    int presync;
    int aio_inflight;
    int aio_errors;
    struct XenBlkDev *blkdev;
    QLIST_ENTRY(ioreq) list;
    BlockAcctCookie acct;
};

struct XenBlkDev {
    XenDevice *xendev;
    XenEventChannel *event_channel;
    unsigned int *ring_ref;
    unsigned int nr_ring_ref;
    void *sring;
    int64_t file_blk;
    int64_t file_size;
    int protocol;
    blkif_back_rings_t rings;
    int more_work;
    QLIST_HEAD(inflight_head, ioreq) inflight;
    QLIST_HEAD(finished_head, ioreq) finished;
    QLIST_HEAD(freelist_head, ioreq) freelist;
    int requests_total;
    int requests_inflight;
    int requests_finished;
    unsigned int max_requests;
    BlockBackend *blk;
    QEMUBH *bh;
    IOThread *iothread;
    AioContext *ctx;
};

static void ioreq_reset(struct ioreq *ioreq)
{
    memset(&ioreq->req, 0, sizeof(ioreq->req));
    ioreq->status = 0;
    ioreq->start = 0;
    ioreq->buf = NULL;
    ioreq->size = 0;
    ioreq->presync = 0;

    ioreq->aio_inflight = 0;
    ioreq->aio_errors = 0;

    ioreq->blkdev = NULL;
    memset(&ioreq->list, 0, sizeof(ioreq->list));
    memset(&ioreq->acct, 0, sizeof(ioreq->acct));

    qemu_iovec_reset(&ioreq->v);
}

static struct ioreq *ioreq_start(struct XenBlkDev *blkdev)
{
    struct ioreq *ioreq = NULL;

    if (QLIST_EMPTY(&blkdev->freelist)) {
        if (blkdev->requests_total >= blkdev->max_requests) {
            goto out;
        }
        /* allocate new struct */
        ioreq = g_malloc0(sizeof(*ioreq));
        ioreq->blkdev = blkdev;
        blkdev->requests_total++;
        qemu_iovec_init(&ioreq->v, 1);
    } else {
        /* get one from freelist */
        ioreq = QLIST_FIRST(&blkdev->freelist);
        QLIST_REMOVE(ioreq, list);
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

static void ioreq_release(struct ioreq *ioreq, bool finish)
{
    struct XenBlkDev *blkdev = ioreq->blkdev;

    QLIST_REMOVE(ioreq, list);
    ioreq_reset(ioreq);
    ioreq->blkdev = blkdev;
    QLIST_INSERT_HEAD(&blkdev->freelist, ioreq, list);
    if (finish) {
        blkdev->requests_finished--;
    } else {
        blkdev->requests_inflight--;
    }
}

/*
 * translate request into iovec + start offset
 * do sanity checks along the way
 */
static int ioreq_parse(struct ioreq *ioreq)
{
    struct XenBlkDev *blkdev = ioreq->blkdev;
    size_t len;
    int i;

    switch (ioreq->req.operation) {
    case BLKIF_OP_READ:
        break;
    case BLKIF_OP_FLUSH_DISKCACHE:
        ioreq->presync = 1;
        if (!ioreq->req.nr_segments) {
            return 0;
        }
        /* fall through */
    case BLKIF_OP_WRITE:
        break;
    case BLKIF_OP_DISCARD:
        return 0;
    default:
        error_report("error: unknown operation (%d)", ioreq->req.operation);
        goto err;
    };

    if (ioreq->req.operation != BLKIF_OP_READ &&
        blk_is_read_only(blkdev->blk)) {
        error_report("error: write req for ro device");
        goto err;
    }

    ioreq->start = ioreq->req.sector_number * blkdev->file_blk;
    for (i = 0; i < ioreq->req.nr_segments; i++) {
        if (i == BLKIF_MAX_SEGMENTS_PER_REQUEST) {
            error_report("error: nr_segments too big");
            goto err;
        }
        if (ioreq->req.seg[i].first_sect > ioreq->req.seg[i].last_sect) {
            error_report("error: first > last sector");
            goto err;
        }
        if (ioreq->req.seg[i].last_sect * blkdev->file_blk >= XC_PAGE_SIZE) {
            error_report("error: page crossing");
            goto err;
        }

        len = (ioreq->req.seg[i].last_sect -
               ioreq->req.seg[i].first_sect + 1) * blkdev->file_blk;
        ioreq->size += len;
    }
    if (ioreq->start + ioreq->size > blkdev->file_size) {
        error_report("error: access beyond end of file");
        goto err;
    }
    return 0;

err:
    ioreq->status = BLKIF_RSP_ERROR;
    return -1;
}

static int ioreq_grant_copy(struct ioreq *ioreq)
{
    struct XenBlkDev *blkdev = ioreq->blkdev;
    XenDevice *xendev = blkdev->xendev;
    XenDeviceGrantCopySegment segs[BLKIF_MAX_SEGMENTS_PER_REQUEST];
    int i, count;
    int64_t file_blk = blkdev->file_blk;
    bool to_domain = (ioreq->req.operation == BLKIF_OP_READ);
    void *virt = ioreq->buf;
    Error *local_err = NULL;

    if (ioreq->req.nr_segments == 0) {
        return 0;
    }

    count = ioreq->req.nr_segments;

    for (i = 0; i < count; i++) {
        if (to_domain) {
            segs[i].dest.foreign.ref = ioreq->req.seg[i].gref;
            segs[i].dest.foreign.offset = ioreq->req.seg[i].first_sect *
                file_blk;
            segs[i].source.virt = virt;
        } else {
            segs[i].source.foreign.ref = ioreq->req.seg[i].gref;
            segs[i].source.foreign.offset = ioreq->req.seg[i].first_sect *
                file_blk;
            segs[i].dest.virt = virt;
        }
        segs[i].len = (ioreq->req.seg[i].last_sect -
                       ioreq->req.seg[i].first_sect + 1) * file_blk;
        virt += segs[i].len;
    }

    xen_device_copy_grant_refs(xendev, to_domain, segs, count, &local_err);

    if (local_err) {
        error_reportf_err(local_err, "failed to copy data: ");

        ioreq->aio_errors++;
        return -1;
    }

    return 0;
}

static int ioreq_runio_qemu_aio(struct ioreq *ioreq);

static void qemu_aio_complete(void *opaque, int ret)
{
    struct ioreq *ioreq = opaque;
    struct XenBlkDev *blkdev = ioreq->blkdev;

    aio_context_acquire(blkdev->ctx);

    if (ret != 0) {
        error_report("%s I/O error",
                     ioreq->req.operation == BLKIF_OP_READ ?
                     "read" : "write");
        ioreq->aio_errors++;
    }

    ioreq->aio_inflight--;
    if (ioreq->presync) {
        ioreq->presync = 0;
        ioreq_runio_qemu_aio(ioreq);
        goto done;
    }
    if (ioreq->aio_inflight > 0) {
        goto done;
    }

    switch (ioreq->req.operation) {
    case BLKIF_OP_READ:
        /* in case of failure ioreq->aio_errors is increased */
        if (ret == 0) {
            ioreq_grant_copy(ioreq);
        }
        qemu_vfree(ioreq->buf);
        break;
    case BLKIF_OP_WRITE:
    case BLKIF_OP_FLUSH_DISKCACHE:
        if (!ioreq->req.nr_segments) {
            break;
        }
        qemu_vfree(ioreq->buf);
        break;
    default:
        break;
    }

    ioreq->status = ioreq->aio_errors ? BLKIF_RSP_ERROR : BLKIF_RSP_OKAY;
    ioreq_finish(ioreq);

    switch (ioreq->req.operation) {
    case BLKIF_OP_WRITE:
    case BLKIF_OP_FLUSH_DISKCACHE:
        if (!ioreq->req.nr_segments) {
            break;
        }
    case BLKIF_OP_READ:
        if (ioreq->status == BLKIF_RSP_OKAY) {
            block_acct_done(blk_get_stats(blkdev->blk), &ioreq->acct);
        } else {
            block_acct_failed(blk_get_stats(blkdev->blk), &ioreq->acct);
        }
        break;
    case BLKIF_OP_DISCARD:
    default:
        break;
    }
    qemu_bh_schedule(blkdev->bh);

done:
    aio_context_release(blkdev->ctx);
}

static bool blk_split_discard(struct ioreq *ioreq, blkif_sector_t sector_number,
                              uint64_t nr_sectors)
{
    struct XenBlkDev *blkdev = ioreq->blkdev;
    int64_t byte_offset;
    int byte_chunk;
    uint64_t byte_remaining, limit;
    uint64_t sec_start = sector_number;
    uint64_t sec_count = nr_sectors;

    /* Wrap around, or overflowing byte limit? */
    if (sec_start + sec_count < sec_count ||
        sec_start + sec_count > INT64_MAX / blkdev->file_blk) {
        return false;
    }

    limit = BDRV_REQUEST_MAX_SECTORS * blkdev->file_blk;
    byte_offset = sec_start * blkdev->file_blk;
    byte_remaining = sec_count * blkdev->file_blk;

    do {
        byte_chunk = byte_remaining > limit ? limit : byte_remaining;
        ioreq->aio_inflight++;
        blk_aio_pdiscard(blkdev->blk, byte_offset, byte_chunk,
                         qemu_aio_complete, ioreq);
        byte_remaining -= byte_chunk;
        byte_offset += byte_chunk;
    } while (byte_remaining > 0);

    return true;
}

static int ioreq_runio_qemu_aio(struct ioreq *ioreq)
{
    struct XenBlkDev *blkdev = ioreq->blkdev;

    ioreq->buf = qemu_memalign(XC_PAGE_SIZE, ioreq->size);
    if (ioreq->req.nr_segments &&
        (ioreq->req.operation == BLKIF_OP_WRITE ||
         ioreq->req.operation == BLKIF_OP_FLUSH_DISKCACHE) &&
        ioreq_grant_copy(ioreq)) {
        qemu_vfree(ioreq->buf);
        goto err;
    }

    ioreq->aio_inflight++;
    if (ioreq->presync) {
        blk_aio_flush(ioreq->blkdev->blk, qemu_aio_complete, ioreq);
        return 0;
    }

    switch (ioreq->req.operation) {
    case BLKIF_OP_READ:
        qemu_iovec_add(&ioreq->v, ioreq->buf, ioreq->size);
        block_acct_start(blk_get_stats(blkdev->blk), &ioreq->acct,
                         ioreq->v.size, BLOCK_ACCT_READ);
        ioreq->aio_inflight++;
        blk_aio_preadv(blkdev->blk, ioreq->start, &ioreq->v, 0,
                       qemu_aio_complete, ioreq);
        break;
    case BLKIF_OP_WRITE:
    case BLKIF_OP_FLUSH_DISKCACHE:
        if (!ioreq->req.nr_segments) {
            break;
        }

        qemu_iovec_add(&ioreq->v, ioreq->buf, ioreq->size);
        block_acct_start(blk_get_stats(blkdev->blk), &ioreq->acct,
                         ioreq->v.size,
                         ioreq->req.operation == BLKIF_OP_WRITE ?
                         BLOCK_ACCT_WRITE : BLOCK_ACCT_FLUSH);
        ioreq->aio_inflight++;
        blk_aio_pwritev(blkdev->blk, ioreq->start, &ioreq->v, 0,
                        qemu_aio_complete, ioreq);
        break;
    case BLKIF_OP_DISCARD:
    {
        struct blkif_request_discard *req = (void *)&ioreq->req;
        if (!blk_split_discard(ioreq, req->sector_number, req->nr_sectors)) {
            goto err;
        }
        break;
    }
    default:
        /* unknown operation (shouldn't happen -- parse catches this) */
        goto err;
    }

    qemu_aio_complete(ioreq, 0);

    return 0;

err:
    ioreq_finish(ioreq);
    ioreq->status = BLKIF_RSP_ERROR;
    return -1;
}

static int blk_send_response_one(struct ioreq *ioreq)
{
    struct XenBlkDev *blkdev = ioreq->blkdev;
    int send_notify = 0;
    int have_requests = 0;
    blkif_response_t *resp;

    /* Place on the response ring for the relevant domain. */
    switch (blkdev->protocol) {
    case BLKIF_PROTOCOL_NATIVE:
        resp = (blkif_response_t *)RING_GET_RESPONSE(
            &blkdev->rings.native,
            blkdev->rings.native.rsp_prod_pvt);
        break;
    case BLKIF_PROTOCOL_X86_32:
        resp = (blkif_response_t *)RING_GET_RESPONSE(
            &blkdev->rings.x86_32_part,
            blkdev->rings.x86_32_part.rsp_prod_pvt);
        break;
    case BLKIF_PROTOCOL_X86_64:
        resp = (blkif_response_t *)RING_GET_RESPONSE(
            &blkdev->rings.x86_64_part,
            blkdev->rings.x86_64_part.rsp_prod_pvt);
        break;
    default:
        return 0;
    }

    resp->id = ioreq->req.id;
    resp->operation = ioreq->req.operation;
    resp->status = ioreq->status;

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
        ioreq_release(ioreq, true);
    }
    if (send_notify) {
        Error *local_err = NULL;

        xen_device_notify_event_channel(blkdev->xendev,
                                        blkdev->event_channel,
                                        &local_err);
        if (local_err) {
            error_report_err(local_err);
        }
    }
}

static int blk_get_request(struct XenBlkDev *blkdev, struct ioreq *ioreq,
                           RING_IDX rc)
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
    /* Prevent the compiler from accessing the on-ring fields instead. */
    barrier();
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

    blk_send_response_all(blkdev);
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

            switch (ioreq->req.operation) {
            case BLKIF_OP_READ:
                block_acct_invalid(blk_get_stats(blkdev->blk),
                                   BLOCK_ACCT_READ);
                break;
            case BLKIF_OP_WRITE:
                block_acct_invalid(blk_get_stats(blkdev->blk),
                                   BLOCK_ACCT_WRITE);
                break;
            case BLKIF_OP_FLUSH_DISKCACHE:
                block_acct_invalid(blk_get_stats(blkdev->blk),
                                   BLOCK_ACCT_FLUSH);
            default:
                break;
            };

            if (blk_send_response_one(ioreq)) {
                Error *local_err = NULL;

                xen_device_notify_event_channel(blkdev->xendev,
                                                blkdev->event_channel,
                                                &local_err);
                if (local_err) {
                    error_report_err(local_err);
                }
            }
            ioreq_release(ioreq, false);
            continue;
        }

        ioreq_runio_qemu_aio(ioreq);
    }

    if (blkdev->more_work && blkdev->requests_inflight < blkdev->max_requests) {
        qemu_bh_schedule(blkdev->bh);
    }
}

static void blk_bh(void *opaque)
{
    struct XenBlkDev *blkdev = opaque;

    aio_context_acquire(blkdev->ctx);
    blk_handle_requests(blkdev);
    aio_context_release(blkdev->ctx);
}

static void blk_event(void *opaque)
{
    struct XenBlkDev *blkdev = opaque;

    qemu_bh_schedule(blkdev->bh);
}

struct XenBlkDev *xen_block_dataplane_create(XenDevice *xendev,
                                             BlockConf *conf,
                                             IOThread *iothread)
{
    struct XenBlkDev *blkdev = g_new0(struct XenBlkDev, 1);

    blkdev->xendev = xendev;
    blkdev->file_blk = conf->logical_block_size;
    blkdev->blk = conf->blk;
    blkdev->file_size = blk_getlength(blkdev->blk);

    QLIST_INIT(&blkdev->inflight);
    QLIST_INIT(&blkdev->finished);
    QLIST_INIT(&blkdev->freelist);

    if (iothread) {
        blkdev->iothread = iothread;
        object_ref(OBJECT(blkdev->iothread));
        blkdev->ctx = iothread_get_aio_context(blkdev->iothread);
    } else {
        blkdev->ctx = qemu_get_aio_context();
    }
    blkdev->bh = aio_bh_new(blkdev->ctx, blk_bh, blkdev);

    return blkdev;
}

void xen_block_dataplane_destroy(struct XenBlkDev *blkdev)
{
    struct ioreq *ioreq;

    if (!blkdev) {
        return;
    }

    while (!QLIST_EMPTY(&blkdev->freelist)) {
        ioreq = QLIST_FIRST(&blkdev->freelist);
        QLIST_REMOVE(ioreq, list);
        qemu_iovec_destroy(&ioreq->v);
        g_free(ioreq);
    }

    qemu_bh_delete(blkdev->bh);
    if (blkdev->iothread) {
        object_unref(OBJECT(blkdev->iothread));
    }

    g_free(blkdev);
}


void xen_block_dataplane_stop(struct XenBlkDev *blkdev)
{
    XenDevice *xendev;

    if (!blkdev) {
        return;
    }

    aio_context_acquire(blkdev->ctx);
    blk_set_aio_context(blkdev->blk, qemu_get_aio_context());
    aio_context_release(blkdev->ctx);

    xendev = blkdev->xendev;

    if (blkdev->event_channel) {
        Error *local_err = NULL;

        xen_device_unbind_event_channel(xendev, blkdev->event_channel,
                                        &local_err);
        blkdev->event_channel = NULL;

        if (local_err) {
            error_report_err(local_err);
        }
    }

    if (blkdev->sring) {
        Error *local_err = NULL;

        xen_device_unmap_grant_refs(xendev, blkdev->sring,
                                    blkdev->nr_ring_ref, &local_err);
        blkdev->sring = NULL;

        if (local_err) {
            error_report_err(local_err);
        }
    }

    g_free(blkdev->ring_ref);
    blkdev->ring_ref = NULL;
}

void xen_block_dataplane_start(struct XenBlkDev *blkdev,
                               const unsigned int ring_ref[],
                               unsigned int nr_ring_ref,
                               unsigned int event_channel,
                               unsigned int protocol,
                               Error **errp)
{
    XenDevice *xendev = blkdev->xendev;
    Error *local_err = NULL;
    unsigned int ring_size;
    unsigned int i;

    blkdev->nr_ring_ref = nr_ring_ref;
    blkdev->ring_ref = g_new(unsigned int, nr_ring_ref);

    for (i = 0; i < nr_ring_ref; i++) {
        blkdev->ring_ref[i] = ring_ref[i];
    }

    blkdev->protocol = protocol;

    ring_size = XC_PAGE_SIZE * blkdev->nr_ring_ref;
    switch (blkdev->protocol) {
    case BLKIF_PROTOCOL_NATIVE:
    {
        blkdev->max_requests = __CONST_RING_SIZE(blkif, ring_size);
        break;
    }
    case BLKIF_PROTOCOL_X86_32:
    {
        blkdev->max_requests = __CONST_RING_SIZE(blkif_x86_32, ring_size);
        break;
    }
    case BLKIF_PROTOCOL_X86_64:
    {
        blkdev->max_requests = __CONST_RING_SIZE(blkif_x86_64, ring_size);
        break;
    }
    default:
        error_setg(errp, "unknown protocol %u", blkdev->protocol);
        return;
    }

    xen_device_set_max_grant_refs(xendev, blkdev->nr_ring_ref,
                                  &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        goto stop;
    }

    blkdev->sring = xen_device_map_grant_refs(xendev,
                                              blkdev->ring_ref,
                                              blkdev->nr_ring_ref,
                                              PROT_READ | PROT_WRITE,
                                              &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        goto stop;
    }

    switch (blkdev->protocol) {
    case BLKIF_PROTOCOL_NATIVE:
    {
        blkif_sring_t *sring_native = blkdev->sring;

        BACK_RING_INIT(&blkdev->rings.native, sring_native, ring_size);
        break;
    }
    case BLKIF_PROTOCOL_X86_32:
    {
        blkif_x86_32_sring_t *sring_x86_32 = blkdev->sring;

        BACK_RING_INIT(&blkdev->rings.x86_32_part, sring_x86_32,
                       ring_size);
        break;
    }
    case BLKIF_PROTOCOL_X86_64:
    {
        blkif_x86_64_sring_t *sring_x86_64 = blkdev->sring;

        BACK_RING_INIT(&blkdev->rings.x86_64_part, sring_x86_64,
                       ring_size);
        break;
    }
    }

    blkdev->event_channel =
        xen_device_bind_event_channel(xendev, event_channel,
                                      blk_event, blkdev,
                                      &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        goto stop;
    }

    aio_context_acquire(blkdev->ctx);
    blk_set_aio_context(blkdev->blk, blkdev->ctx);
    aio_context_release(blkdev->ctx);
    return;

stop:
    xen_block_dataplane_stop(blkdev);
}
