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
#include "qemu/defer-call.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qemu/memalign.h"
#include "qapi/error.h"
#include "hw/xen/xen.h"
#include "hw/block/xen_blkif.h"
#include "hw/xen/interface/io/ring.h"
#include "sysemu/block-backend.h"
#include "sysemu/iothread.h"
#include "xen-block.h"

typedef struct XenBlockRequest {
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
    QLIST_ENTRY(XenBlockRequest) list;
    BlockAcctCookie acct;
} XenBlockRequest;

struct XenBlockDataPlane {
    XenDevice *xendev;
    XenEventChannel *event_channel;
    unsigned int *ring_ref;
    unsigned int nr_ring_ref;
    void *sring;
    int protocol;
    blkif_back_rings_t rings;
    int more_work;
    QLIST_HEAD(inflight_head, XenBlockRequest) inflight;
    QLIST_HEAD(freelist_head, XenBlockRequest) freelist;
    int requests_total;
    int requests_inflight;
    unsigned int max_requests;
    BlockBackend *blk;
    unsigned int sector_size;
    QEMUBH *bh;
    IOThread *iothread;
    AioContext *ctx;
};

static int xen_block_send_response(XenBlockRequest *request);

static void reset_request(XenBlockRequest *request)
{
    memset(&request->req, 0, sizeof(request->req));
    request->status = 0;
    request->start = 0;
    request->size = 0;
    request->presync = 0;

    request->aio_inflight = 0;
    request->aio_errors = 0;

    request->dataplane = NULL;
    memset(&request->list, 0, sizeof(request->list));
    memset(&request->acct, 0, sizeof(request->acct));

    qemu_iovec_reset(&request->v);
}

static XenBlockRequest *xen_block_start_request(XenBlockDataPlane *dataplane)
{
    XenBlockRequest *request = NULL;

    if (QLIST_EMPTY(&dataplane->freelist)) {
        if (dataplane->requests_total >= dataplane->max_requests) {
            goto out;
        }
        /* allocate new struct */
        request = g_malloc0(sizeof(*request));
        request->dataplane = dataplane;
        /*
         * We cannot need more pages per requests than this, and since we
         * re-use requests, allocate the memory once here. It will be freed
         * xen_block_dataplane_destroy() when the request list is freed.
         */
        request->buf = qemu_memalign(XEN_PAGE_SIZE,
                                     BLKIF_MAX_SEGMENTS_PER_REQUEST *
                                     XEN_PAGE_SIZE);
        dataplane->requests_total++;
        qemu_iovec_init(&request->v, 1);
    } else {
        /* get one from freelist */
        request = QLIST_FIRST(&dataplane->freelist);
        QLIST_REMOVE(request, list);
    }
    QLIST_INSERT_HEAD(&dataplane->inflight, request, list);
    dataplane->requests_inflight++;

out:
    return request;
}

static void xen_block_complete_request(XenBlockRequest *request)
{
    XenBlockDataPlane *dataplane = request->dataplane;

    if (xen_block_send_response(request)) {
        Error *local_err = NULL;

        xen_device_notify_event_channel(dataplane->xendev,
                                        dataplane->event_channel,
                                        &local_err);
        if (local_err) {
            error_report_err(local_err);
        }
    }

    QLIST_REMOVE(request, list);
    dataplane->requests_inflight--;
    reset_request(request);
    request->dataplane = dataplane;
    QLIST_INSERT_HEAD(&dataplane->freelist, request, list);
}

/*
 * translate request into iovec + start offset
 * do sanity checks along the way
 */
static int xen_block_parse_request(XenBlockRequest *request)
{
    XenBlockDataPlane *dataplane = request->dataplane;
    size_t len;
    int i;

    switch (request->req.operation) {
    case BLKIF_OP_READ:
        break;
    case BLKIF_OP_FLUSH_DISKCACHE:
        request->presync = 1;
        if (!request->req.nr_segments) {
            return 0;
        }
        /* fall through */
    case BLKIF_OP_WRITE:
        break;
    case BLKIF_OP_DISCARD:
        return 0;
    default:
        error_report("error: unknown operation (%d)", request->req.operation);
        goto err;
    };

    if (request->req.operation != BLKIF_OP_READ &&
        !blk_is_writable(dataplane->blk)) {
        error_report("error: write req for ro device");
        goto err;
    }

    request->start = request->req.sector_number * dataplane->sector_size;
    for (i = 0; i < request->req.nr_segments; i++) {
        if (i == BLKIF_MAX_SEGMENTS_PER_REQUEST) {
            error_report("error: nr_segments too big");
            goto err;
        }
        if (request->req.seg[i].first_sect > request->req.seg[i].last_sect) {
            error_report("error: first > last sector");
            goto err;
        }
        if (request->req.seg[i].last_sect * dataplane->sector_size >=
            XEN_PAGE_SIZE) {
            error_report("error: page crossing");
            goto err;
        }

        len = (request->req.seg[i].last_sect -
               request->req.seg[i].first_sect + 1) * dataplane->sector_size;
        request->size += len;
    }
    if (request->start + request->size > blk_getlength(dataplane->blk)) {
        error_report("error: access beyond end of file");
        goto err;
    }
    return 0;

err:
    request->status = BLKIF_RSP_ERROR;
    return -1;
}

static int xen_block_copy_request(XenBlockRequest *request)
{
    XenBlockDataPlane *dataplane = request->dataplane;
    XenDevice *xendev = dataplane->xendev;
    XenDeviceGrantCopySegment segs[BLKIF_MAX_SEGMENTS_PER_REQUEST];
    int i, count;
    bool to_domain = (request->req.operation == BLKIF_OP_READ);
    void *virt = request->buf;
    Error *local_err = NULL;

    if (request->req.nr_segments == 0) {
        return 0;
    }

    count = request->req.nr_segments;

    for (i = 0; i < count; i++) {
        if (to_domain) {
            segs[i].dest.foreign.ref = request->req.seg[i].gref;
            segs[i].dest.foreign.offset = request->req.seg[i].first_sect *
                dataplane->sector_size;
            segs[i].source.virt = virt;
        } else {
            segs[i].source.foreign.ref = request->req.seg[i].gref;
            segs[i].source.foreign.offset = request->req.seg[i].first_sect *
                dataplane->sector_size;
            segs[i].dest.virt = virt;
        }
        segs[i].len = (request->req.seg[i].last_sect -
                       request->req.seg[i].first_sect + 1) *
                      dataplane->sector_size;
        virt += segs[i].len;
    }

    xen_device_copy_grant_refs(xendev, to_domain, segs, count, &local_err);

    if (local_err) {
        error_reportf_err(local_err, "failed to copy data: ");

        request->aio_errors++;
        return -1;
    }

    return 0;
}

static int xen_block_do_aio(XenBlockRequest *request);

static void xen_block_complete_aio(void *opaque, int ret)
{
    XenBlockRequest *request = opaque;
    XenBlockDataPlane *dataplane = request->dataplane;

    if (ret != 0) {
        error_report("%s I/O error",
                     request->req.operation == BLKIF_OP_READ ?
                     "read" : "write");
        request->aio_errors++;
    }

    request->aio_inflight--;
    if (request->presync) {
        request->presync = 0;
        xen_block_do_aio(request);
        return;
    }
    if (request->aio_inflight > 0) {
        return;
    }

    switch (request->req.operation) {
    case BLKIF_OP_READ:
        /* in case of failure request->aio_errors is increased */
        if (ret == 0) {
            xen_block_copy_request(request);
        }
        break;
    case BLKIF_OP_WRITE:
    case BLKIF_OP_FLUSH_DISKCACHE:
    default:
        break;
    }

    request->status = request->aio_errors ? BLKIF_RSP_ERROR : BLKIF_RSP_OKAY;

    switch (request->req.operation) {
    case BLKIF_OP_WRITE:
    case BLKIF_OP_FLUSH_DISKCACHE:
        if (!request->req.nr_segments) {
            break;
        }
        /* fall through */
    case BLKIF_OP_READ:
        if (request->status == BLKIF_RSP_OKAY) {
            block_acct_done(blk_get_stats(dataplane->blk), &request->acct);
        } else {
            block_acct_failed(blk_get_stats(dataplane->blk), &request->acct);
        }
        break;
    case BLKIF_OP_DISCARD:
    default:
        break;
    }

    xen_block_complete_request(request);

    if (dataplane->more_work) {
        qemu_bh_schedule(dataplane->bh);
    }
}

static bool xen_block_split_discard(XenBlockRequest *request,
                                    blkif_sector_t sector_number,
                                    uint64_t nr_sectors)
{
    XenBlockDataPlane *dataplane = request->dataplane;
    int64_t byte_offset;
    int byte_chunk;
    uint64_t byte_remaining;
    uint64_t sec_start = sector_number;
    uint64_t sec_count = nr_sectors;

    /* Wrap around, or overflowing byte limit? */
    if (sec_start + sec_count < sec_count ||
        sec_start + sec_count > INT64_MAX / dataplane->sector_size) {
        return false;
    }

    byte_offset = sec_start * dataplane->sector_size;
    byte_remaining = sec_count * dataplane->sector_size;

    do {
        byte_chunk = byte_remaining > BDRV_REQUEST_MAX_BYTES ?
            BDRV_REQUEST_MAX_BYTES : byte_remaining;
        request->aio_inflight++;
        blk_aio_pdiscard(dataplane->blk, byte_offset, byte_chunk,
                         xen_block_complete_aio, request);
        byte_remaining -= byte_chunk;
        byte_offset += byte_chunk;
    } while (byte_remaining > 0);

    return true;
}

static int xen_block_do_aio(XenBlockRequest *request)
{
    XenBlockDataPlane *dataplane = request->dataplane;

    if (request->req.nr_segments &&
        (request->req.operation == BLKIF_OP_WRITE ||
         request->req.operation == BLKIF_OP_FLUSH_DISKCACHE) &&
        xen_block_copy_request(request)) {
        goto err;
    }

    request->aio_inflight++;
    if (request->presync) {
        blk_aio_flush(request->dataplane->blk, xen_block_complete_aio,
                      request);
        return 0;
    }

    switch (request->req.operation) {
    case BLKIF_OP_READ:
        qemu_iovec_add(&request->v, request->buf, request->size);
        block_acct_start(blk_get_stats(dataplane->blk), &request->acct,
                         request->v.size, BLOCK_ACCT_READ);
        request->aio_inflight++;
        blk_aio_preadv(dataplane->blk, request->start, &request->v, 0,
                       xen_block_complete_aio, request);
        break;
    case BLKIF_OP_WRITE:
    case BLKIF_OP_FLUSH_DISKCACHE:
        if (!request->req.nr_segments) {
            break;
        }

        qemu_iovec_add(&request->v, request->buf, request->size);
        block_acct_start(blk_get_stats(dataplane->blk), &request->acct,
                         request->v.size,
                         request->req.operation == BLKIF_OP_WRITE ?
                         BLOCK_ACCT_WRITE : BLOCK_ACCT_FLUSH);
        request->aio_inflight++;
        blk_aio_pwritev(dataplane->blk, request->start, &request->v, 0,
                        xen_block_complete_aio, request);
        break;
    case BLKIF_OP_DISCARD:
    {
        struct blkif_request_discard *req = (void *)&request->req;
        if (!xen_block_split_discard(request, req->sector_number,
                                     req->nr_sectors)) {
            goto err;
        }
        break;
    }
    default:
        /* unknown operation (shouldn't happen -- parse catches this) */
        goto err;
    }

    xen_block_complete_aio(request, 0);

    return 0;

err:
    request->status = BLKIF_RSP_ERROR;
    xen_block_complete_request(request);
    return -1;
}

static int xen_block_send_response(XenBlockRequest *request)
{
    XenBlockDataPlane *dataplane = request->dataplane;
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

    resp->id = request->req.id;
    resp->operation = request->req.operation;
    resp->status = request->status;

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

static int xen_block_get_request(XenBlockDataPlane *dataplane,
                                 XenBlockRequest *request, RING_IDX rc)
{
    switch (dataplane->protocol) {
    case BLKIF_PROTOCOL_NATIVE: {
        blkif_request_t *req =
            RING_GET_REQUEST(&dataplane->rings.native, rc);

        memcpy(&request->req, req, sizeof(request->req));
        break;
    }
    case BLKIF_PROTOCOL_X86_32: {
        blkif_x86_32_request_t *req =
            RING_GET_REQUEST(&dataplane->rings.x86_32_part, rc);

        blkif_get_x86_32_req(&request->req, req);
        break;
    }
    case BLKIF_PROTOCOL_X86_64: {
        blkif_x86_64_request_t *req =
            RING_GET_REQUEST(&dataplane->rings.x86_64_part, rc);

        blkif_get_x86_64_req(&request->req, req);
        break;
    }
    }
    /* Prevent the compiler from accessing the on-ring fields instead. */
    barrier();
    return 0;
}

/*
 * Threshold of in-flight requests above which we will start using
 * defer_call_begin()/defer_call_end() to batch requests.
 */
#define IO_PLUG_THRESHOLD 1

static bool xen_block_handle_requests(XenBlockDataPlane *dataplane)
{
    RING_IDX rc, rp;
    XenBlockRequest *request;
    int inflight_atstart = dataplane->requests_inflight;
    int batched = 0;
    bool done_something = false;

    dataplane->more_work = 0;

    rc = dataplane->rings.common.req_cons;
    rp = dataplane->rings.common.sring->req_prod;
    xen_rmb(); /* Ensure we see queued requests up to 'rp'. */

    /*
     * If there was more than IO_PLUG_THRESHOLD requests in flight
     * when we got here, this is an indication that there the bottleneck
     * is below us, so it's worth beginning to batch up I/O requests
     * rather than submitting them immediately. The maximum number
     * of requests we're willing to batch is the number already in
     * flight, so it can grow up to max_requests when the bottleneck
     * is below us.
     */
    if (inflight_atstart > IO_PLUG_THRESHOLD) {
        defer_call_begin();
    }
    while (rc != rp) {
        /* pull request from ring */
        if (RING_REQUEST_CONS_OVERFLOW(&dataplane->rings.common, rc)) {
            break;
        }
        request = xen_block_start_request(dataplane);
        if (request == NULL) {
            dataplane->more_work++;
            break;
        }
        xen_block_get_request(dataplane, request, rc);
        dataplane->rings.common.req_cons = ++rc;
        done_something = true;

        /* parse them */
        if (xen_block_parse_request(request) != 0) {
            switch (request->req.operation) {
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

            xen_block_complete_request(request);
            continue;
        }

        if (inflight_atstart > IO_PLUG_THRESHOLD &&
            batched >= inflight_atstart) {
            defer_call_end();
        }
        xen_block_do_aio(request);
        if (inflight_atstart > IO_PLUG_THRESHOLD) {
            if (batched >= inflight_atstart) {
                defer_call_begin();
                batched = 0;
            } else {
                batched++;
            }
        }
    }
    if (inflight_atstart > IO_PLUG_THRESHOLD) {
        defer_call_end();
    }

    return done_something;
}

static void xen_block_dataplane_bh(void *opaque)
{
    XenBlockDataPlane *dataplane = opaque;

    xen_block_handle_requests(dataplane);
}

static bool xen_block_dataplane_event(void *opaque)
{
    XenBlockDataPlane *dataplane = opaque;

    return xen_block_handle_requests(dataplane);
}

XenBlockDataPlane *xen_block_dataplane_create(XenDevice *xendev,
                                              BlockBackend *blk,
                                              unsigned int sector_size,
                                              IOThread *iothread)
{
    XenBlockDataPlane *dataplane = g_new0(XenBlockDataPlane, 1);

    dataplane->xendev = xendev;
    dataplane->blk = blk;
    dataplane->sector_size = sector_size;

    QLIST_INIT(&dataplane->inflight);
    QLIST_INIT(&dataplane->freelist);

    if (iothread) {
        dataplane->iothread = iothread;
        object_ref(OBJECT(dataplane->iothread));
        dataplane->ctx = iothread_get_aio_context(dataplane->iothread);
    } else {
        dataplane->ctx = qemu_get_aio_context();
    }
    dataplane->bh = aio_bh_new_guarded(dataplane->ctx, xen_block_dataplane_bh,
                                       dataplane,
                                       &DEVICE(xendev)->mem_reentrancy_guard);

    return dataplane;
}

void xen_block_dataplane_destroy(XenBlockDataPlane *dataplane)
{
    XenBlockRequest *request;

    if (!dataplane) {
        return;
    }

    while (!QLIST_EMPTY(&dataplane->freelist)) {
        request = QLIST_FIRST(&dataplane->freelist);
        QLIST_REMOVE(request, list);
        qemu_iovec_destroy(&request->v);
        qemu_vfree(request->buf);
        g_free(request);
    }

    qemu_bh_delete(dataplane->bh);
    if (dataplane->iothread) {
        object_unref(OBJECT(dataplane->iothread));
    }

    g_free(dataplane);
}

void xen_block_dataplane_detach(XenBlockDataPlane *dataplane)
{
    if (!dataplane || !dataplane->event_channel) {
        return;
    }

    /* Only reason for failure is a NULL channel */
    xen_device_set_event_channel_context(dataplane->xendev,
                                         dataplane->event_channel,
                                         NULL, &error_abort);
}

void xen_block_dataplane_attach(XenBlockDataPlane *dataplane)
{
    if (!dataplane || !dataplane->event_channel) {
        return;
    }

    /* Only reason for failure is a NULL channel */
    xen_device_set_event_channel_context(dataplane->xendev,
                                         dataplane->event_channel,
                                         dataplane->ctx, &error_abort);
}

void xen_block_dataplane_stop(XenBlockDataPlane *dataplane)
{
    XenDevice *xendev;

    if (!dataplane) {
        return;
    }

    xendev = dataplane->xendev;

    if (!blk_in_drain(dataplane->blk)) {
        xen_block_dataplane_detach(dataplane);
    }

    /* Xen doesn't have multiple users for nodes, so this can't fail */
    blk_set_aio_context(dataplane->blk, qemu_get_aio_context(), &error_abort);

    /*
     * Now that the context has been moved onto the main thread, cancel
     * further processing.
     */
    qemu_bh_cancel(dataplane->bh);

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
                                    dataplane->ring_ref,
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
    ERRP_GUARD();
    XenDevice *xendev = dataplane->xendev;
    unsigned int ring_size;
    unsigned int i;

    dataplane->nr_ring_ref = nr_ring_ref;
    dataplane->ring_ref = g_new(unsigned int, nr_ring_ref);

    for (i = 0; i < nr_ring_ref; i++) {
        dataplane->ring_ref[i] = ring_ref[i];
    }

    dataplane->protocol = protocol;

    ring_size = XEN_PAGE_SIZE * dataplane->nr_ring_ref;
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
                                  errp);
    if (*errp) {
        goto stop;
    }

    dataplane->sring = xen_device_map_grant_refs(xendev,
                                              dataplane->ring_ref,
                                              dataplane->nr_ring_ref,
                                              PROT_READ | PROT_WRITE,
                                              errp);
    if (*errp) {
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
                                      xen_block_dataplane_event, dataplane,
                                      errp);
    if (*errp) {
        goto stop;
    }

    /* If other users keep the BlockBackend in the iothread, that's ok */
    blk_set_aio_context(dataplane->blk, dataplane->ctx, NULL);

    if (!blk_in_drain(dataplane->blk)) {
        xen_block_dataplane_attach(dataplane);
    }

    return;

stop:
    xen_block_dataplane_stop(dataplane);
}
