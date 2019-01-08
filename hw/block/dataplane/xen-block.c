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
    XenBlockDataPlane *dataplane;
    QLIST_ENTRY(ioreq) list;
    BlockAcctCookie acct;
};

struct XenBlockDataPlane {
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

    ioreq->dataplane = NULL;
    memset(&ioreq->list, 0, sizeof(ioreq->list));
    memset(&ioreq->acct, 0, sizeof(ioreq->acct));

    qemu_iovec_reset(&ioreq->v);
}

static struct ioreq *ioreq_start(XenBlockDataPlane *dataplane)
{
    struct ioreq *ioreq = NULL;

    if (QLIST_EMPTY(&dataplane->freelist)) {
        if (dataplane->requests_total >= dataplane->max_requests) {
            goto out;
        }
        /* allocate new struct */
        ioreq = g_malloc0(sizeof(*ioreq));
        ioreq->dataplane = dataplane;
        dataplane->requests_total++;
        qemu_iovec_init(&ioreq->v, 1);
    } else {
        /* get one from freelist */
        ioreq = QLIST_FIRST(&dataplane->freelist);
        QLIST_REMOVE(ioreq, list);
    }
    QLIST_INSERT_HEAD(&dataplane->inflight, ioreq, list);
    dataplane->requests_inflight++;

out:
    return ioreq;
}

static void ioreq_finish(struct ioreq *ioreq)
{
    XenBlockDataPlane *dataplane = ioreq->dataplane;

    QLIST_REMOVE(ioreq, list);
    QLIST_INSERT_HEAD(&dataplane->finished, ioreq, list);
    dataplane->requests_inflight--;
    dataplane->requests_finished++;
}

static void ioreq_release(struct ioreq *ioreq, bool finish)
{
    XenBlockDataPlane *dataplane = ioreq->dataplane;

    QLIST_REMOVE(ioreq, list);
    ioreq_reset(ioreq);
    ioreq->dataplane = dataplane;
    QLIST_INSERT_HEAD(&dataplane->freelist, ioreq, list);
    if (finish) {
        dataplane->requests_finished--;
    } else {
        dataplane->requests_inflight--;
    }
}

/*
 * translate request into iovec + start offset
 * do sanity checks along the way
 */
static int ioreq_parse(struct ioreq *ioreq)
{
    XenBlockDataPlane *dataplane = ioreq->dataplane;
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
        blk_is_read_only(dataplane->blk)) {
        error_report("error: write req for ro device");
        goto err;
    }

    ioreq->start = ioreq->req.sector_number * dataplane->file_blk;
    for (i = 0; i < ioreq->req.nr_segments; i++) {
        if (i == BLKIF_MAX_SEGMENTS_PER_REQUEST) {
            error_report("error: nr_segments too big");
            goto err;
        }
        if (ioreq->req.seg[i].first_sect > ioreq->req.seg[i].last_sect) {
            error_report("error: first > last sector");
            goto err;
        }
        if (ioreq->req.seg[i].last_sect * dataplane->file_blk >= XC_PAGE_SIZE) {
            error_report("error: page crossing");
            goto err;
        }

        len = (ioreq->req.seg[i].last_sect -
               ioreq->req.seg[i].first_sect + 1) * dataplane->file_blk;
        ioreq->size += len;
    }
    if (ioreq->start + ioreq->size > dataplane->file_size) {
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
    XenBlockDataPlane *dataplane = ioreq->dataplane;
    XenDevice *xendev = dataplane->xendev;
    XenDeviceGrantCopySegment segs[BLKIF_MAX_SEGMENTS_PER_REQUEST];
    int i, count;
    int64_t file_blk = dataplane->file_blk;
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
    XenBlockDataPlane *dataplane = ioreq->dataplane;

    aio_context_acquire(dataplane->ctx);

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
            block_acct_done(blk_get_stats(dataplane->blk), &ioreq->acct);
        } else {
            block_acct_failed(blk_get_stats(dataplane->blk), &ioreq->acct);
        }
        break;
    case BLKIF_OP_DISCARD:
    default:
        break;
    }
    qemu_bh_schedule(dataplane->bh);

done:
    aio_context_release(dataplane->ctx);
}

static bool blk_split_discard(struct ioreq *ioreq, blkif_sector_t sector_number,
                              uint64_t nr_sectors)
{
    XenBlockDataPlane *dataplane = ioreq->dataplane;
    int64_t byte_offset;
    int byte_chunk;
    uint64_t byte_remaining, limit;
    uint64_t sec_start = sector_number;
    uint64_t sec_count = nr_sectors;

    /* Wrap around, or overflowing byte limit? */
    if (sec_start + sec_count < sec_count ||
        sec_start + sec_count > INT64_MAX / dataplane->file_blk) {
        return false;
    }

    limit = BDRV_REQUEST_MAX_SECTORS * dataplane->file_blk;
    byte_offset = sec_start * dataplane->file_blk;
    byte_remaining = sec_count * dataplane->file_blk;

    do {
        byte_chunk = byte_remaining > limit ? limit : byte_remaining;
        ioreq->aio_inflight++;
        blk_aio_pdiscard(dataplane->blk, byte_offset, byte_chunk,
                         qemu_aio_complete, ioreq);
        byte_remaining -= byte_chunk;
        byte_offset += byte_chunk;
    } while (byte_remaining > 0);

    return true;
}

static int ioreq_runio_qemu_aio(struct ioreq *ioreq)
{
    XenBlockDataPlane *dataplane = ioreq->dataplane;

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
        blk_aio_flush(ioreq->dataplane->blk, qemu_aio_complete, ioreq);
        return 0;
    }

    switch (ioreq->req.operation) {
    case BLKIF_OP_READ:
        qemu_iovec_add(&ioreq->v, ioreq->buf, ioreq->size);
        block_acct_start(blk_get_stats(dataplane->blk), &ioreq->acct,
                         ioreq->v.size, BLOCK_ACCT_READ);
        ioreq->aio_inflight++;
        blk_aio_preadv(dataplane->blk, ioreq->start, &ioreq->v, 0,
                       qemu_aio_complete, ioreq);
        break;
    case BLKIF_OP_WRITE:
    case BLKIF_OP_FLUSH_DISKCACHE:
        if (!ioreq->req.nr_segments) {
            break;
        }

        qemu_iovec_add(&ioreq->v, ioreq->buf, ioreq->size);
        block_acct_start(blk_get_stats(dataplane->blk), &ioreq->acct,
                         ioreq->v.size,
                         ioreq->req.operation == BLKIF_OP_WRITE ?
                         BLOCK_ACCT_WRITE : BLOCK_ACCT_FLUSH);
        ioreq->aio_inflight++;
        blk_aio_pwritev(dataplane->blk, ioreq->start, &ioreq->v, 0,
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
    XenBlockDataPlane *dataplane = ioreq->dataplane;
    int send_notify = 0;
    int have_requests = 0;
    blkif_response_t *resp;

    /* Place on the response ring for the relevant domain. */
    switch (dataplane->protocol) {
    case BLKIF_PROTOCOL_NATIVE:
        resp = (blkif_response_t *)RING_GET_RESPONSE(
            &dataplane->rings.native,
            dataplane->rings.native.rsp_prod_pvt);
        break;
    case BLKIF_PROTOCOL_X86_32:
        resp = (blkif_response_t *)RING_GET_RESPONSE(
            &dataplane->rings.x86_32_part,
            dataplane->rings.x86_32_part.rsp_prod_pvt);
        break;
    case BLKIF_PROTOCOL_X86_64:
        resp = (blkif_response_t *)RING_GET_RESPONSE(
            &dataplane->rings.x86_64_part,
            dataplane->rings.x86_64_part.rsp_prod_pvt);
        break;
    default:
        return 0;
    }

    resp->id = ioreq->req.id;
    resp->operation = ioreq->req.operation;
    resp->status = ioreq->status;

    dataplane->rings.common.rsp_prod_pvt++;

    RING_PUSH_RESPONSES_AND_CHECK_NOTIFY(&dataplane->rings.common,
                                         send_notify);
    if (dataplane->rings.common.rsp_prod_pvt ==
        dataplane->rings.common.req_cons) {
        /*
         * Tail check for pending requests. Allows frontend to avoid
         * notifications if requests are already in flight (lower
         * overheads and promotes batching).
         */
        RING_FINAL_CHECK_FOR_REQUESTS(&dataplane->rings.common,
                                      have_requests);
    } else if (RING_HAS_UNCONSUMED_REQUESTS(&dataplane->rings.common)) {
        have_requests = 1;
    }

    if (have_requests) {
        dataplane->more_work++;
    }
    return send_notify;
}

/* walk finished list, send outstanding responses, free requests */
static void blk_send_response_all(XenBlockDataPlane *dataplane)
{
    struct ioreq *ioreq;
    int send_notify = 0;

    while (!QLIST_EMPTY(&dataplane->finished)) {
        ioreq = QLIST_FIRST(&dataplane->finished);
        send_notify += blk_send_response_one(ioreq);
        ioreq_release(ioreq, true);
    }
    if (send_notify) {
        Error *local_err = NULL;

        xen_device_notify_event_channel(dataplane->xendev,
                                        dataplane->event_channel,
                                        &local_err);
        if (local_err) {
            error_report_err(local_err);
        }
    }
}

static int blk_get_request(XenBlockDataPlane *dataplane, struct ioreq *ioreq,
                           RING_IDX rc)
{
    switch (dataplane->protocol) {
    case BLKIF_PROTOCOL_NATIVE: {
        blkif_request_t *req =
            RING_GET_REQUEST(&dataplane->rings.native, rc);

        memcpy(&ioreq->req, req, sizeof(ioreq->req));
        break;
    }
    case BLKIF_PROTOCOL_X86_32: {
        blkif_x86_32_request_t *req =
            RING_GET_REQUEST(&dataplane->rings.x86_32_part, rc);

        blkif_get_x86_32_req(&ioreq->req, req);
        break;
    }
    case BLKIF_PROTOCOL_X86_64: {
        blkif_x86_64_request_t *req =
            RING_GET_REQUEST(&dataplane->rings.x86_64_part, rc);

        blkif_get_x86_64_req(&ioreq->req, req);
        break;
    }
    }
    /* Prevent the compiler from accessing the on-ring fields instead. */
    barrier();
    return 0;
}

static void blk_handle_requests(XenBlockDataPlane *dataplane)
{
    RING_IDX rc, rp;
    struct ioreq *ioreq;

    dataplane->more_work = 0;

    rc = dataplane->rings.common.req_cons;
    rp = dataplane->rings.common.sring->req_prod;
    xen_rmb(); /* Ensure we see queued requests up to 'rp'. */

    blk_send_response_all(dataplane);
    while (rc != rp) {
        /* pull request from ring */
        if (RING_REQUEST_CONS_OVERFLOW(&dataplane->rings.common, rc)) {
            break;
        }
        ioreq = ioreq_start(dataplane);
        if (ioreq == NULL) {
            dataplane->more_work++;
            break;
        }
        blk_get_request(dataplane, ioreq, rc);
        dataplane->rings.common.req_cons = ++rc;

        /* parse them */
        if (ioreq_parse(ioreq) != 0) {

            switch (ioreq->req.operation) {
            case BLKIF_OP_READ:
                block_acct_invalid(blk_get_stats(dataplane->blk),
                                   BLOCK_ACCT_READ);
                break;
            case BLKIF_OP_WRITE:
                block_acct_invalid(blk_get_stats(dataplane->blk),
                                   BLOCK_ACCT_WRITE);
                break;
            case BLKIF_OP_FLUSH_DISKCACHE:
                block_acct_invalid(blk_get_stats(dataplane->blk),
                                   BLOCK_ACCT_FLUSH);
            default:
                break;
            };

            if (blk_send_response_one(ioreq)) {
                Error *local_err = NULL;

                xen_device_notify_event_channel(dataplane->xendev,
                                                dataplane->event_channel,
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

    if (dataplane->more_work &&
        dataplane->requests_inflight < dataplane->max_requests) {
        qemu_bh_schedule(dataplane->bh);
    }
}

static void blk_bh(void *opaque)
{
    XenBlockDataPlane *dataplane = opaque;

    aio_context_acquire(dataplane->ctx);
    blk_handle_requests(dataplane);
    aio_context_release(dataplane->ctx);
}

static void blk_event(void *opaque)
{
    XenBlockDataPlane *dataplane = opaque;

    qemu_bh_schedule(dataplane->bh);
}

XenBlockDataPlane *xen_block_dataplane_create(XenDevice *xendev,
                                              BlockConf *conf,
                                              IOThread *iothread)
{
    XenBlockDataPlane *dataplane = g_new0(XenBlockDataPlane, 1);

    dataplane->xendev = xendev;
    dataplane->file_blk = conf->logical_block_size;
    dataplane->blk = conf->blk;
    dataplane->file_size = blk_getlength(dataplane->blk);

    QLIST_INIT(&dataplane->inflight);
    QLIST_INIT(&dataplane->finished);
    QLIST_INIT(&dataplane->freelist);

    if (iothread) {
        dataplane->iothread = iothread;
        object_ref(OBJECT(dataplane->iothread));
        dataplane->ctx = iothread_get_aio_context(dataplane->iothread);
    } else {
        dataplane->ctx = qemu_get_aio_context();
    }
    dataplane->bh = aio_bh_new(dataplane->ctx, blk_bh, dataplane);

    return dataplane;
}

void xen_block_dataplane_destroy(XenBlockDataPlane *dataplane)
{
    struct ioreq *ioreq;

    if (!dataplane) {
        return;
    }

    while (!QLIST_EMPTY(&dataplane->freelist)) {
        ioreq = QLIST_FIRST(&dataplane->freelist);
        QLIST_REMOVE(ioreq, list);
        qemu_iovec_destroy(&ioreq->v);
        g_free(ioreq);
    }

    qemu_bh_delete(dataplane->bh);
    if (dataplane->iothread) {
        object_unref(OBJECT(dataplane->iothread));
    }

    g_free(dataplane);
}

void xen_block_dataplane_stop(XenBlockDataPlane *dataplane)
{
    XenDevice *xendev;

    if (!dataplane) {
        return;
    }

    aio_context_acquire(dataplane->ctx);
    blk_set_aio_context(dataplane->blk, qemu_get_aio_context());
    aio_context_release(dataplane->ctx);

    xendev = dataplane->xendev;

    if (dataplane->event_channel) {
        Error *local_err = NULL;

        xen_device_unbind_event_channel(xendev, dataplane->event_channel,
                                        &local_err);
        dataplane->event_channel = NULL;

        if (local_err) {
            error_report_err(local_err);
        }
    }

    if (dataplane->sring) {
        Error *local_err = NULL;

        xen_device_unmap_grant_refs(xendev, dataplane->sring,
                                    dataplane->nr_ring_ref, &local_err);
        dataplane->sring = NULL;

        if (local_err) {
            error_report_err(local_err);
        }
    }

    g_free(dataplane->ring_ref);
    dataplane->ring_ref = NULL;
}

void xen_block_dataplane_start(XenBlockDataPlane *dataplane,
                               const unsigned int ring_ref[],
                               unsigned int nr_ring_ref,
                               unsigned int event_channel,
                               unsigned int protocol,
                               Error **errp)
{
    XenDevice *xendev = dataplane->xendev;
    Error *local_err = NULL;
    unsigned int ring_size;
    unsigned int i;

    dataplane->nr_ring_ref = nr_ring_ref;
    dataplane->ring_ref = g_new(unsigned int, nr_ring_ref);

    for (i = 0; i < nr_ring_ref; i++) {
        dataplane->ring_ref[i] = ring_ref[i];
    }

    dataplane->protocol = protocol;

    ring_size = XC_PAGE_SIZE * dataplane->nr_ring_ref;
    switch (dataplane->protocol) {
    case BLKIF_PROTOCOL_NATIVE:
    {
        dataplane->max_requests = __CONST_RING_SIZE(blkif, ring_size);
        break;
    }
    case BLKIF_PROTOCOL_X86_32:
    {
        dataplane->max_requests = __CONST_RING_SIZE(blkif_x86_32, ring_size);
        break;
    }
    case BLKIF_PROTOCOL_X86_64:
    {
        dataplane->max_requests = __CONST_RING_SIZE(blkif_x86_64, ring_size);
        break;
    }
    default:
        error_setg(errp, "unknown protocol %u", dataplane->protocol);
        return;
    }

    xen_device_set_max_grant_refs(xendev, dataplane->nr_ring_ref,
                                  &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        goto stop;
    }

    dataplane->sring = xen_device_map_grant_refs(xendev,
                                              dataplane->ring_ref,
                                              dataplane->nr_ring_ref,
                                              PROT_READ | PROT_WRITE,
                                              &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        goto stop;
    }

    switch (dataplane->protocol) {
    case BLKIF_PROTOCOL_NATIVE:
    {
        blkif_sring_t *sring_native = dataplane->sring;

        BACK_RING_INIT(&dataplane->rings.native, sring_native, ring_size);
        break;
    }
    case BLKIF_PROTOCOL_X86_32:
    {
        blkif_x86_32_sring_t *sring_x86_32 = dataplane->sring;

        BACK_RING_INIT(&dataplane->rings.x86_32_part, sring_x86_32,
                       ring_size);
        break;
    }
    case BLKIF_PROTOCOL_X86_64:
    {
        blkif_x86_64_sring_t *sring_x86_64 = dataplane->sring;

        BACK_RING_INIT(&dataplane->rings.x86_64_part, sring_x86_64,
                       ring_size);
        break;
    }
    }

    dataplane->event_channel =
        xen_device_bind_event_channel(xendev, event_channel,
                                      blk_event, dataplane,
                                      &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        goto stop;
    }

    aio_context_acquire(dataplane->ctx);
    blk_set_aio_context(dataplane->blk, dataplane->ctx);
    aio_context_release(dataplane->ctx);
    return;

stop:
    xen_block_dataplane_stop(dataplane);
}
