/*
 * Virtio SCSI HBA
 *
 * Copyright IBM, Corp. 2010
 * Copyright Red Hat, Inc. 2011
 *
 * Authors:
 *   Stefan Hajnoczi    <stefanha@linux.vnet.ibm.com>
 *   Paolo Bonzini      <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "standard-headers/linux/virtio_ids.h"
#include "hw/virtio/virtio-scsi.h"
#include "migration/qemu-file-types.h"
#include "qemu/defer-call.h"
#include "qemu/error-report.h"
#include "qemu/iov.h"
#include "qemu/module.h"
#include "system/block-backend.h"
#include "system/dma.h"
#include "hw/qdev-properties.h"
#include "hw/scsi/scsi.h"
#include "scsi/constants.h"
#include "hw/virtio/iothread-vq-mapping.h"
#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/virtio-access.h"
#include "trace.h"

typedef struct VirtIOSCSIReq {
    /*
     * Note:
     * - fields up to resp_iov are initialized by virtio_scsi_init_req;
     * - fields starting at vring are zeroed by virtio_scsi_init_req.
     */
    VirtQueueElement elem;

    VirtIOSCSI *dev;
    VirtQueue *vq;
    QEMUSGList qsgl;
    QEMUIOVector resp_iov;

    /* Used for two-stage request submission and TMFs deferred to BH */
    QTAILQ_ENTRY(VirtIOSCSIReq) next;

    /* Used for cancellation of request during TMFs. Atomic. */
    int remaining;

    SCSIRequest *sreq;
    size_t resp_size;
    enum SCSIXferMode mode;
    union {
        VirtIOSCSICmdResp     cmd;
        VirtIOSCSICtrlTMFResp tmf;
        VirtIOSCSICtrlANResp  an;
        VirtIOSCSIEvent       event;
    } resp;
    union {
        VirtIOSCSICmdReq      cmd;
        VirtIOSCSICtrlTMFReq  tmf;
        VirtIOSCSICtrlANReq   an;
    } req;
} VirtIOSCSIReq;

static inline int virtio_scsi_get_lun(uint8_t *lun)
{
    return ((lun[2] << 8) | lun[3]) & 0x3FFF;
}

static inline SCSIDevice *virtio_scsi_device_get(VirtIOSCSI *s, uint8_t *lun)
{
    if (lun[0] != 1) {
        return NULL;
    }
    if (lun[2] != 0 && !(lun[2] >= 0x40 && lun[2] < 0x80)) {
        return NULL;
    }
    return scsi_device_get(&s->bus, 0, lun[1], virtio_scsi_get_lun(lun));
}

static void virtio_scsi_init_req(VirtIOSCSI *s, VirtQueue *vq, VirtIOSCSIReq *req)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(s);
    const size_t zero_skip =
        offsetof(VirtIOSCSIReq, resp_iov) + sizeof(req->resp_iov);

    req->vq = vq;
    req->dev = s;
    qemu_sglist_init(&req->qsgl, DEVICE(s), 8, vdev->dma_as);
    qemu_iovec_init(&req->resp_iov, 1);
    memset((uint8_t *)req + zero_skip, 0, sizeof(*req) - zero_skip);
}

static void virtio_scsi_free_req(VirtIOSCSIReq *req)
{
    qemu_iovec_destroy(&req->resp_iov);
    qemu_sglist_destroy(&req->qsgl);
    g_free(req);
}

static void virtio_scsi_complete_req(VirtIOSCSIReq *req, QemuMutex *vq_lock)
{
    VirtIOSCSI *s = req->dev;
    VirtQueue *vq = req->vq;
    VirtIODevice *vdev = VIRTIO_DEVICE(s);

    qemu_iovec_from_buf(&req->resp_iov, 0, &req->resp, req->resp_size);

    if (vq_lock) {
        qemu_mutex_lock(vq_lock);
    }

    virtqueue_push(vq, &req->elem, req->qsgl.size + req->resp_iov.size);
    if (s->dataplane_started && !s->dataplane_fenced) {
        virtio_notify_irqfd(vdev, vq);
    } else {
        virtio_notify(vdev, vq);
    }

    if (vq_lock) {
        qemu_mutex_unlock(vq_lock);
    }

    if (req->sreq) {
        req->sreq->hba_private = NULL;
        scsi_req_unref(req->sreq);
    }
    virtio_scsi_free_req(req);
}

static void virtio_scsi_bad_req(VirtIOSCSIReq *req, QemuMutex *vq_lock)
{
    virtio_error(VIRTIO_DEVICE(req->dev), "wrong size for virtio-scsi headers");

    if (vq_lock) {
        qemu_mutex_lock(vq_lock);
    }

    virtqueue_detach_element(req->vq, &req->elem, 0);

    if (vq_lock) {
        qemu_mutex_unlock(vq_lock);
    }

    virtio_scsi_free_req(req);
}

static size_t qemu_sgl_concat(VirtIOSCSIReq *req, struct iovec *iov,
                              hwaddr *addr, int num, size_t skip)
{
    QEMUSGList *qsgl = &req->qsgl;
    size_t copied = 0;

    while (num) {
        if (skip >= iov->iov_len) {
            skip -= iov->iov_len;
        } else {
            qemu_sglist_add(qsgl, *addr + skip, iov->iov_len - skip);
            copied += iov->iov_len - skip;
            skip = 0;
        }
        iov++;
        addr++;
        num--;
    }

    assert(skip == 0);
    return copied;
}

static int virtio_scsi_parse_req(VirtIOSCSIReq *req,
                                 unsigned req_size, unsigned resp_size)
{
    VirtIODevice *vdev = (VirtIODevice *) req->dev;
    size_t in_size, out_size;

    if (iov_to_buf(req->elem.out_sg, req->elem.out_num, 0,
                   &req->req, req_size) < req_size) {
        return -EINVAL;
    }

    if (qemu_iovec_concat_iov(&req->resp_iov,
                              req->elem.in_sg, req->elem.in_num, 0,
                              resp_size) < resp_size) {
        return -EINVAL;
    }

    req->resp_size = resp_size;

    /* Old BIOSes left some padding by mistake after the req_size/resp_size.
     * As a workaround, always consider the first buffer as the virtio-scsi
     * request/response, making the payload start at the second element
     * of the iovec.
     *
     * The actual length of the response header, stored in req->resp_size,
     * does not change.
     *
     * TODO: always disable this workaround for virtio 1.0 devices.
     */
    if (!virtio_vdev_has_feature(vdev, VIRTIO_F_ANY_LAYOUT)) {
        if (req->elem.out_num) {
            req_size = req->elem.out_sg[0].iov_len;
        }
        if (req->elem.in_num) {
            resp_size = req->elem.in_sg[0].iov_len;
        }
    }

    out_size = qemu_sgl_concat(req, req->elem.out_sg,
                               &req->elem.out_addr[0], req->elem.out_num,
                               req_size);
    in_size = qemu_sgl_concat(req, req->elem.in_sg,
                              &req->elem.in_addr[0], req->elem.in_num,
                              resp_size);

    if (out_size && in_size) {
        return -ENOTSUP;
    }

    if (out_size) {
        req->mode = SCSI_XFER_TO_DEV;
    } else if (in_size) {
        req->mode = SCSI_XFER_FROM_DEV;
    }

    return 0;
}

static VirtIOSCSIReq *virtio_scsi_pop_req(VirtIOSCSI *s, VirtQueue *vq, QemuMutex *vq_lock)
{
    VirtIOSCSICommon *vs = (VirtIOSCSICommon *)s;
    VirtIOSCSIReq *req;

    if (vq_lock) {
        qemu_mutex_lock(vq_lock);
    }

    req = virtqueue_pop(vq, sizeof(VirtIOSCSIReq) + vs->cdb_size);

    if (vq_lock) {
        qemu_mutex_unlock(vq_lock);
    }

    if (!req) {
        return NULL;
    }
    virtio_scsi_init_req(s, vq, req);
    return req;
}

static void virtio_scsi_save_request(QEMUFile *f, SCSIRequest *sreq)
{
    VirtIOSCSIReq *req = sreq->hba_private;
    VirtIOSCSICommon *vs = VIRTIO_SCSI_COMMON(req->dev);
    VirtIODevice *vdev = VIRTIO_DEVICE(req->dev);
    uint32_t n = virtio_get_queue_index(req->vq) - VIRTIO_SCSI_VQ_NUM_FIXED;

    assert(n < vs->conf.num_queues);
    qemu_put_be32s(f, &n);
    qemu_put_virtqueue_element(vdev, f, &req->elem);
}

static void *virtio_scsi_load_request(QEMUFile *f, SCSIRequest *sreq)
{
    SCSIBus *bus = sreq->bus;
    VirtIOSCSI *s = container_of(bus, VirtIOSCSI, bus);
    VirtIOSCSICommon *vs = VIRTIO_SCSI_COMMON(s);
    VirtIODevice *vdev = VIRTIO_DEVICE(s);
    VirtIOSCSIReq *req;
    uint32_t n;

    qemu_get_be32s(f, &n);
    assert(n < vs->conf.num_queues);
    req = qemu_get_virtqueue_element(vdev, f,
                                     sizeof(VirtIOSCSIReq) + vs->cdb_size);
    virtio_scsi_init_req(s, vs->cmd_vqs[n], req);

    if (virtio_scsi_parse_req(req, sizeof(VirtIOSCSICmdReq) + vs->cdb_size,
                              sizeof(VirtIOSCSICmdResp) + vs->sense_size) < 0) {
        error_report("invalid SCSI request migration data");
        exit(1);
    }

    scsi_req_ref(sreq);
    req->sreq = sreq;
    if (req->sreq->cmd.mode != SCSI_XFER_NONE) {
        assert(req->sreq->cmd.mode == req->mode);
    }
    return req;
}

typedef struct {
    Notifier        notifier;
    VirtIOSCSIReq  *tmf_req;
} VirtIOSCSICancelNotifier;

static void virtio_scsi_tmf_dec_remaining(VirtIOSCSIReq *tmf)
{
    if (qatomic_fetch_dec(&tmf->remaining) == 1) {
        trace_virtio_scsi_tmf_resp(virtio_scsi_get_lun(tmf->req.tmf.lun),
                                   tmf->req.tmf.tag, tmf->resp.tmf.response);

        virtio_scsi_complete_req(tmf, &tmf->dev->ctrl_lock);
    }
}

static void virtio_scsi_cancel_notify(Notifier *notifier, void *data)
{
    VirtIOSCSICancelNotifier *n = container_of(notifier,
                                               VirtIOSCSICancelNotifier,
                                               notifier);

    virtio_scsi_tmf_dec_remaining(n->tmf_req);
    g_free(n);
}

static void virtio_scsi_tmf_cancel_req(VirtIOSCSIReq *tmf, SCSIRequest *r)
{
    VirtIOSCSICancelNotifier *notifier;

    assert(r->ctx == qemu_get_current_aio_context());

    /* Decremented in virtio_scsi_cancel_notify() */
    qatomic_inc(&tmf->remaining);

    notifier = g_new(VirtIOSCSICancelNotifier, 1);
    notifier->notifier.notify = virtio_scsi_cancel_notify;
    notifier->tmf_req = tmf;
    scsi_req_cancel_async(r, &notifier->notifier);
}

/* Execute a TMF on the requests in the current AioContext */
static void virtio_scsi_do_tmf_aio_context(void *opaque)
{
    AioContext *ctx = qemu_get_current_aio_context();
    VirtIOSCSIReq *tmf = opaque;
    VirtIOSCSI *s = tmf->dev;
    SCSIDevice *d = virtio_scsi_device_get(s, tmf->req.tmf.lun);
    SCSIRequest *r;
    bool match_tag;

    if (!d) {
        tmf->resp.tmf.response = VIRTIO_SCSI_S_BAD_TARGET;
        virtio_scsi_tmf_dec_remaining(tmf);
        return;
    }

    /*
     * This function could handle other subtypes that need to be processed in
     * the request's AioContext in the future, but for now only request
     * cancelation subtypes are performed here.
     */
    switch (tmf->req.tmf.subtype) {
    case VIRTIO_SCSI_T_TMF_ABORT_TASK:
        match_tag = true;
        break;
    case VIRTIO_SCSI_T_TMF_ABORT_TASK_SET:
    case VIRTIO_SCSI_T_TMF_CLEAR_TASK_SET:
        match_tag = false;
        break;
    default:
        g_assert_not_reached();
    }

    WITH_QEMU_LOCK_GUARD(&d->requests_lock) {
        QTAILQ_FOREACH(r, &d->requests, next) {
            VirtIOSCSIReq *cmd_req = r->hba_private;
            assert(cmd_req); /* request has hba_private while enqueued */

            if (r->ctx != ctx) {
                continue;
            }
            if (match_tag && cmd_req->req.cmd.tag != tmf->req.tmf.tag) {
                continue;
            }
            virtio_scsi_tmf_cancel_req(tmf, r);
        }
    }

    /* Incremented by virtio_scsi_do_tmf() */
    virtio_scsi_tmf_dec_remaining(tmf);

    object_unref(d);
}

static void dummy_bh(void *opaque)
{
    /* Do nothing */
}

/*
 * Wait for pending virtio_scsi_defer_tmf_to_aio_context() BHs.
 */
static void virtio_scsi_flush_defer_tmf_to_aio_context(VirtIOSCSI *s)
{
    GLOBAL_STATE_CODE();

    assert(!s->dataplane_started);

    for (uint32_t i = 0; i < s->parent_obj.conf.num_queues; i++) {
        AioContext *ctx = s->vq_aio_context[VIRTIO_SCSI_VQ_NUM_FIXED + i];

        /* Our BH only runs after previously scheduled BHs */
        aio_wait_bh_oneshot(ctx, dummy_bh, NULL);
    }
}

/*
 * Run the TMF in a specific AioContext, handling only requests in that
 * AioContext. This is necessary because requests can run in different
 * AioContext and it is only possible to cancel them from the AioContext where
 * they are running.
 */
static void virtio_scsi_defer_tmf_to_aio_context(VirtIOSCSIReq *tmf,
                                                 AioContext *ctx)
{
    /* Decremented in virtio_scsi_do_tmf_aio_context() */
    qatomic_inc(&tmf->remaining);

    /* See virtio_scsi_flush_defer_tmf_to_aio_context() cleanup during reset */
    aio_bh_schedule_oneshot(ctx, virtio_scsi_do_tmf_aio_context, tmf);
}

/*
 * Returns the AioContext for a given TMF's tag field or NULL. Note that the
 * request identified by the tag may have completed by the time you can execute
 * a BH in the AioContext, so don't assume the request still exists in your BH.
 */
static AioContext *find_aio_context_for_tmf_tag(SCSIDevice *d,
                                                VirtIOSCSIReq *tmf)
{
    WITH_QEMU_LOCK_GUARD(&d->requests_lock) {
        SCSIRequest *r;
        SCSIRequest *next;

        QTAILQ_FOREACH_SAFE(r, &d->requests, next, next) {
            VirtIOSCSIReq *cmd_req = r->hba_private;

            /* hba_private is non-NULL while the request is enqueued */
            assert(cmd_req);

            if (cmd_req->req.cmd.tag == tmf->req.tmf.tag) {
                return r->ctx;
            }
        }
    }
    return NULL;
}

/* Return 0 if the request is ready to be completed and return to guest;
 * -EINPROGRESS if the request is submitted and will be completed later, in the
 *  case of async cancellation. */
static int virtio_scsi_do_tmf(VirtIOSCSI *s, VirtIOSCSIReq *req)
{
    SCSIDevice *d = virtio_scsi_device_get(s, req->req.tmf.lun);
    SCSIRequest *r, *next;
    AioContext *ctx;
    int ret = 0;

    /* Here VIRTIO_SCSI_S_OK means "FUNCTION COMPLETE".  */
    req->resp.tmf.response = VIRTIO_SCSI_S_OK;

    /*
     * req->req.tmf has the QEMU_PACKED attribute. Don't use virtio_tswap32s()
     * to avoid compiler errors.
     */
    req->req.tmf.subtype =
        virtio_tswap32(VIRTIO_DEVICE(s), req->req.tmf.subtype);

    trace_virtio_scsi_tmf_req(virtio_scsi_get_lun(req->req.tmf.lun),
                              req->req.tmf.tag, req->req.tmf.subtype);

    switch (req->req.tmf.subtype) {
    case VIRTIO_SCSI_T_TMF_ABORT_TASK: {
        if (!d) {
            goto fail;
        }
        if (d->lun != virtio_scsi_get_lun(req->req.tmf.lun)) {
            goto incorrect_lun;
        }

        ctx = find_aio_context_for_tmf_tag(d, req);
        if (ctx) {
            virtio_scsi_defer_tmf_to_aio_context(req, ctx);
            ret = -EINPROGRESS;
        }
        break;
    }

    case VIRTIO_SCSI_T_TMF_QUERY_TASK:
        if (!d) {
            goto fail;
        }
        if (d->lun != virtio_scsi_get_lun(req->req.tmf.lun)) {
            goto incorrect_lun;
        }

        WITH_QEMU_LOCK_GUARD(&d->requests_lock) {
            QTAILQ_FOREACH(r, &d->requests, next) {
                VirtIOSCSIReq *cmd_req = r->hba_private;
                assert(cmd_req); /* request has hba_private while enqueued */

                if (cmd_req->req.cmd.tag == req->req.tmf.tag) {
                    /*
                     * "If the specified command is present in the task set,
                     * then return a service response set to FUNCTION
                     * SUCCEEDED".
                     */
                    req->resp.tmf.response = VIRTIO_SCSI_S_FUNCTION_SUCCEEDED;
                }
            }
        }
        break;

    case VIRTIO_SCSI_T_TMF_LOGICAL_UNIT_RESET:
        if (!d) {
            goto fail;
        }
        if (d->lun != virtio_scsi_get_lun(req->req.tmf.lun)) {
            goto incorrect_lun;
        }
        qatomic_inc(&s->resetting);
        device_cold_reset(&d->qdev);
        qatomic_dec(&s->resetting);
        break;

    case VIRTIO_SCSI_T_TMF_I_T_NEXUS_RESET: {
        BusChild *kid;
        int target = req->req.tmf.lun[1];
        qatomic_inc(&s->resetting);

        rcu_read_lock();
        QTAILQ_FOREACH_RCU(kid, &s->bus.qbus.children, sibling) {
            SCSIDevice *d1 = SCSI_DEVICE(kid->child);
            if (d1->channel == 0 && d1->id == target) {
                device_cold_reset(&d1->qdev);
            }
        }
        rcu_read_unlock();

        qatomic_dec(&s->resetting);
        break;
    }

    case VIRTIO_SCSI_T_TMF_ABORT_TASK_SET:
    case VIRTIO_SCSI_T_TMF_CLEAR_TASK_SET: {
        g_autoptr(GHashTable) aio_contexts = g_hash_table_new(NULL, NULL);

        if (!d) {
            goto fail;
        }
        if (d->lun != virtio_scsi_get_lun(req->req.tmf.lun)) {
            goto incorrect_lun;
        }

        qatomic_inc(&req->remaining);

        for (uint32_t i = 0; i < s->parent_obj.conf.num_queues; i++) {
            ctx = s->vq_aio_context[VIRTIO_SCSI_VQ_NUM_FIXED + i];

            if (!g_hash_table_add(aio_contexts, ctx)) {
                continue; /* skip previously added AioContext */
            }

            virtio_scsi_defer_tmf_to_aio_context(req, ctx);
        }

        virtio_scsi_tmf_dec_remaining(req);
        ret = -EINPROGRESS;
        break;
    }

    case VIRTIO_SCSI_T_TMF_QUERY_TASK_SET:
        if (!d) {
            goto fail;
        }
        if (d->lun != virtio_scsi_get_lun(req->req.tmf.lun)) {
            goto incorrect_lun;
        }

        WITH_QEMU_LOCK_GUARD(&d->requests_lock) {
            QTAILQ_FOREACH_SAFE(r, &d->requests, next, next) {
                /* Request has hba_private while enqueued */
                assert(r->hba_private);

                /*
                 * "If there is any command present in the task set, then
                 * return a service response set to FUNCTION SUCCEEDED".
                 */
                req->resp.tmf.response = VIRTIO_SCSI_S_FUNCTION_SUCCEEDED;
                break;
            }
        }
        break;

    case VIRTIO_SCSI_T_TMF_CLEAR_ACA:
    default:
        req->resp.tmf.response = VIRTIO_SCSI_S_FUNCTION_REJECTED;
        break;
    }

    object_unref(OBJECT(d));
    return ret;

incorrect_lun:
    req->resp.tmf.response = VIRTIO_SCSI_S_INCORRECT_LUN;
    object_unref(OBJECT(d));
    return ret;

fail:
    req->resp.tmf.response = VIRTIO_SCSI_S_BAD_TARGET;
    object_unref(OBJECT(d));
    return ret;
}

static void virtio_scsi_handle_ctrl_req(VirtIOSCSI *s, VirtIOSCSIReq *req)
{
    VirtIODevice *vdev = (VirtIODevice *)s;
    uint32_t type;
    int r = 0;

    if (iov_to_buf(req->elem.out_sg, req->elem.out_num, 0,
                &type, sizeof(type)) < sizeof(type)) {
        virtio_scsi_bad_req(req, &s->ctrl_lock);
        return;
    }

    virtio_tswap32s(vdev, &type);
    if (type == VIRTIO_SCSI_T_TMF) {
        if (virtio_scsi_parse_req(req, sizeof(VirtIOSCSICtrlTMFReq),
                    sizeof(VirtIOSCSICtrlTMFResp)) < 0) {
            virtio_scsi_bad_req(req, &s->ctrl_lock);
            return;
        } else {
            r = virtio_scsi_do_tmf(s, req);
        }

    } else if (type == VIRTIO_SCSI_T_AN_QUERY ||
               type == VIRTIO_SCSI_T_AN_SUBSCRIBE) {
        if (virtio_scsi_parse_req(req, sizeof(VirtIOSCSICtrlANReq),
                    sizeof(VirtIOSCSICtrlANResp)) < 0) {
            virtio_scsi_bad_req(req, &s->ctrl_lock);
            return;
        } else {
            req->req.an.event_requested =
                virtio_tswap32(VIRTIO_DEVICE(s), req->req.an.event_requested);
            trace_virtio_scsi_an_req(virtio_scsi_get_lun(req->req.an.lun),
                                     req->req.an.event_requested);
            req->resp.an.event_actual = 0;
            req->resp.an.response = VIRTIO_SCSI_S_OK;
        }
    }
    if (r == 0) {
        if (type == VIRTIO_SCSI_T_TMF)
            trace_virtio_scsi_tmf_resp(virtio_scsi_get_lun(req->req.tmf.lun),
                                       req->req.tmf.tag,
                                       req->resp.tmf.response);
        else if (type == VIRTIO_SCSI_T_AN_QUERY ||
                 type == VIRTIO_SCSI_T_AN_SUBSCRIBE)
            trace_virtio_scsi_an_resp(virtio_scsi_get_lun(req->req.an.lun),
                                      req->resp.an.response);
        virtio_scsi_complete_req(req, &s->ctrl_lock);
    } else {
        assert(r == -EINPROGRESS);
    }
}

static void virtio_scsi_handle_ctrl_vq(VirtIOSCSI *s, VirtQueue *vq)
{
    VirtIOSCSIReq *req;

    while ((req = virtio_scsi_pop_req(s, vq, &s->ctrl_lock))) {
        virtio_scsi_handle_ctrl_req(s, req);
    }
}

/*
 * If dataplane is configured but not yet started, do so now and return true on
 * success.
 *
 * Dataplane is started by the core virtio code but virtqueue handler functions
 * can also be invoked when a guest kicks before DRIVER_OK, so this helper
 * function helps us deal with manually starting ioeventfd in that case.
 */
static bool virtio_scsi_defer_to_dataplane(VirtIOSCSI *s)
{
    if (s->dataplane_started) {
        return false;
    }
    if (s->vq_aio_context[0] == qemu_get_aio_context()) {
        return false; /* not using IOThreads */
    }

    virtio_device_start_ioeventfd(&s->parent_obj.parent_obj);
    return !s->dataplane_fenced;
}

static void virtio_scsi_handle_ctrl(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIOSCSI *s = (VirtIOSCSI *)vdev;

    if (virtio_scsi_defer_to_dataplane(s)) {
        return;
    }

    virtio_scsi_handle_ctrl_vq(s, vq);
}

static void virtio_scsi_complete_cmd_req(VirtIOSCSIReq *req)
{
    trace_virtio_scsi_cmd_resp(virtio_scsi_get_lun(req->req.cmd.lun),
                               req->req.cmd.tag,
                               req->resp.cmd.response,
                               req->resp.cmd.status);
    /* Sense data is not in req->resp and is copied separately
     * in virtio_scsi_command_complete.
     */
    req->resp_size = sizeof(VirtIOSCSICmdResp);
    virtio_scsi_complete_req(req, NULL);
}

static void virtio_scsi_command_failed(SCSIRequest *r)
{
    VirtIOSCSIReq *req = r->hba_private;

    if (r->io_canceled) {
        return;
    }

    req->resp.cmd.status = GOOD;
    switch (r->host_status) {
    case SCSI_HOST_NO_LUN:
        req->resp.cmd.response = VIRTIO_SCSI_S_INCORRECT_LUN;
        break;
    case SCSI_HOST_BUSY:
        req->resp.cmd.response = VIRTIO_SCSI_S_BUSY;
        break;
    case SCSI_HOST_TIME_OUT:
    case SCSI_HOST_ABORTED:
        req->resp.cmd.response = VIRTIO_SCSI_S_ABORTED;
        break;
    case SCSI_HOST_BAD_RESPONSE:
        req->resp.cmd.response = VIRTIO_SCSI_S_BAD_TARGET;
        break;
    case SCSI_HOST_RESET:
        req->resp.cmd.response = VIRTIO_SCSI_S_RESET;
        break;
    case SCSI_HOST_TRANSPORT_DISRUPTED:
        req->resp.cmd.response = VIRTIO_SCSI_S_TRANSPORT_FAILURE;
        break;
    case SCSI_HOST_TARGET_FAILURE:
        req->resp.cmd.response = VIRTIO_SCSI_S_TARGET_FAILURE;
        break;
    case SCSI_HOST_RESERVATION_ERROR:
        req->resp.cmd.response = VIRTIO_SCSI_S_NEXUS_FAILURE;
        break;
    case SCSI_HOST_ALLOCATION_FAILURE:
    case SCSI_HOST_MEDIUM_ERROR:
    case SCSI_HOST_ERROR:
    default:
        req->resp.cmd.response = VIRTIO_SCSI_S_FAILURE;
        break;
    }
    virtio_scsi_complete_cmd_req(req);
}

static void virtio_scsi_command_complete(SCSIRequest *r, size_t resid)
{
    VirtIOSCSIReq *req = r->hba_private;
    uint8_t sense[SCSI_SENSE_BUF_SIZE];
    uint32_t sense_len;
    VirtIODevice *vdev = VIRTIO_DEVICE(req->dev);

    if (r->io_canceled) {
        return;
    }

    req->resp.cmd.response = VIRTIO_SCSI_S_OK;
    req->resp.cmd.status = r->status;
    if (req->resp.cmd.status == GOOD) {
        req->resp.cmd.resid = virtio_tswap32(vdev, resid);
    } else {
        req->resp.cmd.resid = 0;
        sense_len = scsi_req_get_sense(r, sense, sizeof(sense));
        sense_len = MIN(sense_len, req->resp_iov.size - sizeof(req->resp.cmd));
        qemu_iovec_from_buf(&req->resp_iov, sizeof(req->resp.cmd),
                            sense, sense_len);
        req->resp.cmd.sense_len = virtio_tswap32(vdev, sense_len);
    }
    virtio_scsi_complete_cmd_req(req);
}

static int virtio_scsi_parse_cdb(SCSIDevice *dev, SCSICommand *cmd,
                                 uint8_t *buf, size_t buf_len,
                                 void *hba_private)
{
    VirtIOSCSIReq *req = hba_private;

    if (cmd->len == 0) {
        cmd->len = MIN(VIRTIO_SCSI_CDB_DEFAULT_SIZE, SCSI_CMD_BUF_SIZE);
        memcpy(cmd->buf, buf, cmd->len);
    }

    /* Extract the direction and mode directly from the request, for
     * host device passthrough.
     */
    cmd->xfer = req->qsgl.size;
    cmd->mode = req->mode;
    return 0;
}

static QEMUSGList *virtio_scsi_get_sg_list(SCSIRequest *r)
{
    VirtIOSCSIReq *req = r->hba_private;

    return &req->qsgl;
}

static void virtio_scsi_request_cancelled(SCSIRequest *r)
{
    VirtIOSCSIReq *req = r->hba_private;

    if (!req) {
        return;
    }
    if (qatomic_read(&req->dev->resetting)) {
        req->resp.cmd.response = VIRTIO_SCSI_S_RESET;
    } else {
        req->resp.cmd.response = VIRTIO_SCSI_S_ABORTED;
    }
    virtio_scsi_complete_cmd_req(req);
}

static void virtio_scsi_fail_cmd_req(VirtIOSCSIReq *req)
{
    req->resp.cmd.response = VIRTIO_SCSI_S_FAILURE;
    virtio_scsi_complete_cmd_req(req);
}

static int virtio_scsi_handle_cmd_req_prepare(VirtIOSCSI *s, VirtIOSCSIReq *req)
{
    VirtIOSCSICommon *vs = VIRTIO_SCSI_COMMON(s);
    SCSIDevice *d;
    int rc;

    rc = virtio_scsi_parse_req(req, sizeof(VirtIOSCSICmdReq) + vs->cdb_size,
                               sizeof(VirtIOSCSICmdResp) + vs->sense_size);
    if (rc < 0) {
        if (rc == -ENOTSUP) {
            virtio_scsi_fail_cmd_req(req);
            return -ENOTSUP;
        } else {
            virtio_scsi_bad_req(req, NULL);
            return -EINVAL;
        }
    }
    trace_virtio_scsi_cmd_req(virtio_scsi_get_lun(req->req.cmd.lun),
                              req->req.cmd.tag, req->req.cmd.cdb[0]);

    d = virtio_scsi_device_get(s, req->req.cmd.lun);
    if (!d) {
        req->resp.cmd.response = VIRTIO_SCSI_S_BAD_TARGET;
        virtio_scsi_complete_cmd_req(req);
        return -ENOENT;
    }
    req->sreq = scsi_req_new(d, req->req.cmd.tag,
                             virtio_scsi_get_lun(req->req.cmd.lun),
                             req->req.cmd.cdb, vs->cdb_size, req);

    if (req->sreq->cmd.mode != SCSI_XFER_NONE
        && (req->sreq->cmd.mode != req->mode ||
            req->sreq->cmd.xfer > req->qsgl.size)) {
        req->resp.cmd.response = VIRTIO_SCSI_S_OVERRUN;
        virtio_scsi_complete_cmd_req(req);
        object_unref(OBJECT(d));
        return -ENOBUFS;
    }
    scsi_req_ref(req->sreq);
    defer_call_begin();
    object_unref(OBJECT(d));
    return 0;
}

static void virtio_scsi_handle_cmd_req_submit(VirtIOSCSI *s, VirtIOSCSIReq *req)
{
    SCSIRequest *sreq = req->sreq;
    if (scsi_req_enqueue(sreq)) {
        scsi_req_continue(sreq);
    }
    defer_call_end();
    scsi_req_unref(sreq);
}

static void virtio_scsi_handle_cmd_vq(VirtIOSCSI *s, VirtQueue *vq)
{
    VirtIOSCSIReq *req, *next;
    int ret = 0;
    bool suppress_notifications = virtio_queue_get_notification(vq);

    QTAILQ_HEAD(, VirtIOSCSIReq) reqs = QTAILQ_HEAD_INITIALIZER(reqs);

    do {
        if (suppress_notifications) {
            virtio_queue_set_notification(vq, 0);
        }

        while ((req = virtio_scsi_pop_req(s, vq, NULL))) {
            ret = virtio_scsi_handle_cmd_req_prepare(s, req);
            if (!ret) {
                QTAILQ_INSERT_TAIL(&reqs, req, next);
            } else if (ret == -EINVAL) {
                /* The device is broken and shouldn't process any request */
                while (!QTAILQ_EMPTY(&reqs)) {
                    req = QTAILQ_FIRST(&reqs);
                    QTAILQ_REMOVE(&reqs, req, next);
                    defer_call_end();
                    scsi_req_unref(req->sreq);
                    virtqueue_detach_element(req->vq, &req->elem, 0);
                    virtio_scsi_free_req(req);
                }
            }
        }

        if (suppress_notifications) {
            virtio_queue_set_notification(vq, 1);
        }
    } while (ret != -EINVAL && !virtio_queue_empty(vq));

    QTAILQ_FOREACH_SAFE(req, &reqs, next, next) {
        virtio_scsi_handle_cmd_req_submit(s, req);
    }
}

static void virtio_scsi_handle_cmd(VirtIODevice *vdev, VirtQueue *vq)
{
    /* use non-QOM casts in the data path */
    VirtIOSCSI *s = (VirtIOSCSI *)vdev;

    if (virtio_scsi_defer_to_dataplane(s)) {
        return;
    }

    virtio_scsi_handle_cmd_vq(s, vq);
}

static void virtio_scsi_get_config(VirtIODevice *vdev,
                                   uint8_t *config)
{
    VirtIOSCSIConfig *scsiconf = (VirtIOSCSIConfig *)config;
    VirtIOSCSICommon *s = VIRTIO_SCSI_COMMON(vdev);

    virtio_stl_p(vdev, &scsiconf->num_queues, s->conf.num_queues);
    virtio_stl_p(vdev, &scsiconf->seg_max,
                 s->conf.seg_max_adjust ? s->conf.virtqueue_size - 2 : 128 - 2);
    virtio_stl_p(vdev, &scsiconf->max_sectors, s->conf.max_sectors);
    virtio_stl_p(vdev, &scsiconf->cmd_per_lun, s->conf.cmd_per_lun);
    virtio_stl_p(vdev, &scsiconf->event_info_size, sizeof(VirtIOSCSIEvent));
    virtio_stl_p(vdev, &scsiconf->sense_size, s->sense_size);
    virtio_stl_p(vdev, &scsiconf->cdb_size, s->cdb_size);
    virtio_stw_p(vdev, &scsiconf->max_channel, VIRTIO_SCSI_MAX_CHANNEL);
    virtio_stw_p(vdev, &scsiconf->max_target, VIRTIO_SCSI_MAX_TARGET);
    virtio_stl_p(vdev, &scsiconf->max_lun, VIRTIO_SCSI_MAX_LUN);
}

static void virtio_scsi_set_config(VirtIODevice *vdev,
                                   const uint8_t *config)
{
    VirtIOSCSIConfig *scsiconf = (VirtIOSCSIConfig *)config;
    VirtIOSCSICommon *vs = VIRTIO_SCSI_COMMON(vdev);

    if ((uint32_t) virtio_ldl_p(vdev, &scsiconf->sense_size) >= 65536 ||
        (uint32_t) virtio_ldl_p(vdev, &scsiconf->cdb_size) >= 256) {
        virtio_error(vdev,
                     "bad data written to virtio-scsi configuration space");
        return;
    }

    vs->sense_size = virtio_ldl_p(vdev, &scsiconf->sense_size);
    vs->cdb_size = virtio_ldl_p(vdev, &scsiconf->cdb_size);
}

static uint64_t virtio_scsi_get_features(VirtIODevice *vdev,
                                         uint64_t requested_features,
                                         Error **errp)
{
    VirtIOSCSI *s = VIRTIO_SCSI(vdev);

    /* Firstly sync all virtio-scsi possible supported features */
    requested_features |= s->host_features;
    return requested_features;
}

static void virtio_scsi_reset(VirtIODevice *vdev)
{
    VirtIOSCSI *s = VIRTIO_SCSI(vdev);
    VirtIOSCSICommon *vs = VIRTIO_SCSI_COMMON(vdev);

    assert(!s->dataplane_started);

    virtio_scsi_flush_defer_tmf_to_aio_context(s);

    qatomic_inc(&s->resetting);
    bus_cold_reset(BUS(&s->bus));
    qatomic_dec(&s->resetting);

    vs->sense_size = VIRTIO_SCSI_SENSE_DEFAULT_SIZE;
    vs->cdb_size = VIRTIO_SCSI_CDB_DEFAULT_SIZE;

    WITH_QEMU_LOCK_GUARD(&s->event_lock) {
        s->events_dropped = false;
    }
}

typedef struct {
    uint32_t event;
    uint32_t reason;
    union {
        /* Used by messages specific to a device */
        struct {
            uint32_t id;
            uint32_t lun;
        } address;
    };
} VirtIOSCSIEventInfo;

static void virtio_scsi_push_event(VirtIOSCSI *s,
                                   const VirtIOSCSIEventInfo *info)
{
    VirtIOSCSICommon *vs = VIRTIO_SCSI_COMMON(s);
    VirtIOSCSIReq *req;
    VirtIOSCSIEvent *evt;
    VirtIODevice *vdev = VIRTIO_DEVICE(s);
    uint32_t event = info->event;
    uint32_t reason = info->reason;

    if (!(vdev->status & VIRTIO_CONFIG_S_DRIVER_OK)) {
        return;
    }

    req = virtio_scsi_pop_req(s, vs->event_vq, &s->event_lock);
    WITH_QEMU_LOCK_GUARD(&s->event_lock) {
        if (!req) {
            s->events_dropped = true;
            return;
        }

        if (s->events_dropped) {
            event |= VIRTIO_SCSI_T_EVENTS_MISSED;
            s->events_dropped = false;
        }
    }

    if (virtio_scsi_parse_req(req, 0, sizeof(VirtIOSCSIEvent))) {
        virtio_scsi_bad_req(req, &s->event_lock);
        return;
    }

    evt = &req->resp.event;
    memset(evt, 0, sizeof(VirtIOSCSIEvent));
    evt->event = virtio_tswap32(vdev, event);
    evt->reason = virtio_tswap32(vdev, reason);
    if (event != VIRTIO_SCSI_T_EVENTS_MISSED) {
        evt->lun[0] = 1;
        evt->lun[1] = info->address.id;

        /* Linux wants us to keep the same encoding we use for REPORT LUNS.  */
        if (info->address.lun >= 256) {
            evt->lun[2] = (info->address.lun >> 8) | 0x40;
        }
        evt->lun[3] = info->address.lun & 0xFF;
    }
    trace_virtio_scsi_event(virtio_scsi_get_lun(evt->lun), event, reason);

    virtio_scsi_complete_req(req, &s->event_lock);
}

static void virtio_scsi_handle_event_vq(VirtIOSCSI *s, VirtQueue *vq)
{
    bool events_dropped;

    WITH_QEMU_LOCK_GUARD(&s->event_lock) {
        events_dropped = s->events_dropped;
    }

    if (events_dropped) {
        VirtIOSCSIEventInfo info = {
            .event = VIRTIO_SCSI_T_NO_EVENT,
        };
        virtio_scsi_push_event(s, &info);
    }
}

static void virtio_scsi_handle_event(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIOSCSI *s = VIRTIO_SCSI(vdev);

    if (virtio_scsi_defer_to_dataplane(s)) {
        return;
    }

    virtio_scsi_handle_event_vq(s, vq);
}

static void virtio_scsi_change(SCSIBus *bus, SCSIDevice *dev, SCSISense sense)
{
    VirtIOSCSI *s = container_of(bus, VirtIOSCSI, bus);
    VirtIODevice *vdev = VIRTIO_DEVICE(s);

    if (virtio_vdev_has_feature(vdev, VIRTIO_SCSI_F_CHANGE) &&
        dev->type != TYPE_ROM) {
        VirtIOSCSIEventInfo info = {
            .event   = VIRTIO_SCSI_T_PARAM_CHANGE,
            .reason  = sense.asc | (sense.ascq << 8),
            .address = {
                .id  = dev->id,
                .lun = dev->lun,
            },
        };

        virtio_scsi_push_event(s, &info);
    }
}

static void virtio_scsi_pre_hotplug(HotplugHandler *hotplug_dev,
                                    DeviceState *dev, Error **errp)
{
    SCSIDevice *sd = SCSI_DEVICE(dev);
    sd->hba_supports_iothread = true;
}

static void virtio_scsi_hotplug(HotplugHandler *hotplug_dev, DeviceState *dev,
                                Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(hotplug_dev);
    VirtIOSCSI *s = VIRTIO_SCSI(vdev);
    AioContext *ctx = s->vq_aio_context[VIRTIO_SCSI_VQ_NUM_FIXED];
    SCSIDevice *sd = SCSI_DEVICE(dev);

    if (ctx != qemu_get_aio_context() && !s->dataplane_fenced) {
        /*
         * Try to make the BlockBackend's AioContext match ours. Ignore failure
         * because I/O will still work although block jobs and other users
         * might be slower when multiple AioContexts use a BlockBackend.
         */
        blk_set_aio_context(sd->conf.blk, ctx, NULL);
    }

    if (virtio_vdev_has_feature(vdev, VIRTIO_SCSI_F_HOTPLUG)) {
        VirtIOSCSIEventInfo info = {
            .event   = VIRTIO_SCSI_T_TRANSPORT_RESET,
            .reason  = VIRTIO_SCSI_EVT_RESET_RESCAN,
            .address = {
                .id  = sd->id,
                .lun = sd->lun,
            },
        };

        virtio_scsi_push_event(s, &info);
        scsi_bus_set_ua(&s->bus, SENSE_CODE(REPORTED_LUNS_CHANGED));
    }
}

static void virtio_scsi_hotunplug(HotplugHandler *hotplug_dev, DeviceState *dev,
                                  Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(hotplug_dev);
    VirtIOSCSI *s = VIRTIO_SCSI(vdev);
    SCSIDevice *sd = SCSI_DEVICE(dev);
    VirtIOSCSIEventInfo info = {
        .event   = VIRTIO_SCSI_T_TRANSPORT_RESET,
        .reason  = VIRTIO_SCSI_EVT_RESET_REMOVED,
        .address = {
            .id  = sd->id,
            .lun = sd->lun,
        },
    };

    qdev_simple_device_unplug_cb(hotplug_dev, dev, errp);

    if (s->vq_aio_context[VIRTIO_SCSI_VQ_NUM_FIXED] != qemu_get_aio_context()) {
        /* If other users keep the BlockBackend in the iothread, that's ok */
        blk_set_aio_context(sd->conf.blk, qemu_get_aio_context(), NULL);
    }

    if (virtio_vdev_has_feature(vdev, VIRTIO_SCSI_F_HOTPLUG)) {
        virtio_scsi_push_event(s, &info);
        scsi_bus_set_ua(&s->bus, SENSE_CODE(REPORTED_LUNS_CHANGED));
    }
}

/* Suspend virtqueue ioeventfd processing during drain */
static void virtio_scsi_drained_begin(SCSIBus *bus)
{
    VirtIOSCSI *s = container_of(bus, VirtIOSCSI, bus);
    VirtIODevice *vdev = VIRTIO_DEVICE(s);
    uint32_t total_queues = VIRTIO_SCSI_VQ_NUM_FIXED +
                            s->parent_obj.conf.num_queues;

    /*
     * Drain is called when stopping dataplane but the host notifier has
     * already been detached. Detaching multiple times is a no-op if nothing
     * else is using the monitoring same file descriptor, but avoid it just in
     * case.
     *
     * Also, don't detach if dataplane has not even been started yet because
     * the host notifier isn't attached.
     */
    if (s->dataplane_stopping || !s->dataplane_started) {
        return;
    }

    for (uint32_t i = 0; i < total_queues; i++) {
        VirtQueue *vq = virtio_get_queue(vdev, i);
        virtio_queue_aio_detach_host_notifier(vq, s->vq_aio_context[i]);
    }
}

/* Resume virtqueue ioeventfd processing after drain */
static void virtio_scsi_drained_end(SCSIBus *bus)
{
    VirtIOSCSI *s = container_of(bus, VirtIOSCSI, bus);
    VirtIOSCSICommon *vs = VIRTIO_SCSI_COMMON(s);
    VirtIODevice *vdev = VIRTIO_DEVICE(s);
    uint32_t total_queues = VIRTIO_SCSI_VQ_NUM_FIXED +
                            s->parent_obj.conf.num_queues;

    /*
     * Drain is called when stopping dataplane. Keep the host notifier detached
     * so it's not left dangling after dataplane is stopped.
     *
     * Also, don't attach if dataplane has not even been started yet. We're not
     * ready.
     */
    if (s->dataplane_stopping || !s->dataplane_started) {
        return;
    }

    for (uint32_t i = 0; i < total_queues; i++) {
        VirtQueue *vq = virtio_get_queue(vdev, i);
        AioContext *ctx = s->vq_aio_context[i];

        if (vq == vs->event_vq) {
            virtio_queue_aio_attach_host_notifier_no_poll(vq, ctx);
        } else {
            virtio_queue_aio_attach_host_notifier(vq, ctx);
        }
    }
}

static struct SCSIBusInfo virtio_scsi_scsi_info = {
    .tcq = true,
    .max_channel = VIRTIO_SCSI_MAX_CHANNEL,
    .max_target = VIRTIO_SCSI_MAX_TARGET,
    .max_lun = VIRTIO_SCSI_MAX_LUN,

    .complete = virtio_scsi_command_complete,
    .fail = virtio_scsi_command_failed,
    .cancel = virtio_scsi_request_cancelled,
    .change = virtio_scsi_change,
    .parse_cdb = virtio_scsi_parse_cdb,
    .get_sg_list = virtio_scsi_get_sg_list,
    .save_request = virtio_scsi_save_request,
    .load_request = virtio_scsi_load_request,
    .drained_begin = virtio_scsi_drained_begin,
    .drained_end = virtio_scsi_drained_end,
};

void virtio_scsi_common_realize(DeviceState *dev,
                                VirtIOHandleOutput ctrl,
                                VirtIOHandleOutput evt,
                                VirtIOHandleOutput cmd,
                                Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIOSCSICommon *s = VIRTIO_SCSI_COMMON(dev);
    int i;

    virtio_init(vdev, VIRTIO_ID_SCSI, sizeof(VirtIOSCSIConfig));

    if (s->conf.num_queues == VIRTIO_SCSI_AUTO_NUM_QUEUES) {
        s->conf.num_queues = 1;
    }
    if (s->conf.num_queues == 0 ||
            s->conf.num_queues > VIRTIO_QUEUE_MAX - VIRTIO_SCSI_VQ_NUM_FIXED) {
        error_setg(errp, "Invalid number of queues (= %" PRIu32 "), "
                         "must be a positive integer less than %d.",
                   s->conf.num_queues,
                   VIRTIO_QUEUE_MAX - VIRTIO_SCSI_VQ_NUM_FIXED);
        virtio_cleanup(vdev);
        return;
    }
    if (s->conf.virtqueue_size <= 2) {
        error_setg(errp, "invalid virtqueue_size property (= %" PRIu32 "), "
                   "must be > 2", s->conf.virtqueue_size);
        return;
    }
    s->cmd_vqs = g_new0(VirtQueue *, s->conf.num_queues);
    s->sense_size = VIRTIO_SCSI_SENSE_DEFAULT_SIZE;
    s->cdb_size = VIRTIO_SCSI_CDB_DEFAULT_SIZE;

    s->ctrl_vq = virtio_add_queue(vdev, s->conf.virtqueue_size, ctrl);
    s->event_vq = virtio_add_queue(vdev, s->conf.virtqueue_size, evt);
    for (i = 0; i < s->conf.num_queues; i++) {
        s->cmd_vqs[i] = virtio_add_queue(vdev, s->conf.virtqueue_size, cmd);
    }
}

static void virtio_scsi_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIOSCSI *s = VIRTIO_SCSI(dev);
    Error *err = NULL;

    qemu_mutex_init(&s->ctrl_lock);
    qemu_mutex_init(&s->event_lock);

    virtio_scsi_common_realize(dev,
                               virtio_scsi_handle_ctrl,
                               virtio_scsi_handle_event,
                               virtio_scsi_handle_cmd,
                               &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }

    scsi_bus_init_named(&s->bus, sizeof(s->bus), dev,
                       &virtio_scsi_scsi_info, vdev->bus_name);
    /* override default SCSI bus hotplug-handler, with virtio-scsi's one */
    qbus_set_hotplug_handler(BUS(&s->bus), OBJECT(dev));

    virtio_scsi_dataplane_setup(s, errp);
}

void virtio_scsi_common_unrealize(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIOSCSICommon *vs = VIRTIO_SCSI_COMMON(dev);
    int i;

    virtio_delete_queue(vs->ctrl_vq);
    virtio_delete_queue(vs->event_vq);
    for (i = 0; i < vs->conf.num_queues; i++) {
        virtio_delete_queue(vs->cmd_vqs[i]);
    }
    g_free(vs->cmd_vqs);
    virtio_cleanup(vdev);
}

/* main loop */
static void virtio_scsi_device_unrealize(DeviceState *dev)
{
    VirtIOSCSI *s = VIRTIO_SCSI(dev);

    virtio_scsi_dataplane_cleanup(s);
    qbus_set_hotplug_handler(BUS(&s->bus), NULL);
    virtio_scsi_common_unrealize(dev);
    qemu_mutex_destroy(&s->event_lock);
    qemu_mutex_destroy(&s->ctrl_lock);
}

static const Property virtio_scsi_properties[] = {
    DEFINE_PROP_UINT32("num_queues", VirtIOSCSI, parent_obj.conf.num_queues,
                       VIRTIO_SCSI_AUTO_NUM_QUEUES),
    DEFINE_PROP_UINT32("virtqueue_size", VirtIOSCSI,
                                         parent_obj.conf.virtqueue_size, 256),
    DEFINE_PROP_BOOL("seg_max_adjust", VirtIOSCSI,
                      parent_obj.conf.seg_max_adjust, true),
    DEFINE_PROP_UINT32("max_sectors", VirtIOSCSI, parent_obj.conf.max_sectors,
                                                  0xFFFF),
    DEFINE_PROP_UINT32("cmd_per_lun", VirtIOSCSI, parent_obj.conf.cmd_per_lun,
                                                  128),
    DEFINE_PROP_BIT("hotplug", VirtIOSCSI, host_features,
                                           VIRTIO_SCSI_F_HOTPLUG, true),
    DEFINE_PROP_BIT("param_change", VirtIOSCSI, host_features,
                                                VIRTIO_SCSI_F_CHANGE, true),
    DEFINE_PROP_LINK("iothread", VirtIOSCSI, parent_obj.conf.iothread,
                     TYPE_IOTHREAD, IOThread *),
    DEFINE_PROP_IOTHREAD_VQ_MAPPING_LIST("iothread-vq-mapping", VirtIOSCSI,
            parent_obj.conf.iothread_vq_mapping_list),
};

static const VMStateDescription vmstate_virtio_scsi = {
    .name = "virtio-scsi",
    .minimum_version_id = 1,
    .version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_VIRTIO_DEVICE,
        VMSTATE_END_OF_LIST()
    },
};

static void virtio_scsi_common_class_init(ObjectClass *klass, void *data)
{
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    vdc->get_config = virtio_scsi_get_config;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
}

static void virtio_scsi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);
    HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(klass);

    device_class_set_props(dc, virtio_scsi_properties);
    dc->vmsd = &vmstate_virtio_scsi;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    vdc->realize = virtio_scsi_device_realize;
    vdc->unrealize = virtio_scsi_device_unrealize;
    vdc->set_config = virtio_scsi_set_config;
    vdc->get_features = virtio_scsi_get_features;
    vdc->reset = virtio_scsi_reset;
    vdc->start_ioeventfd = virtio_scsi_dataplane_start;
    vdc->stop_ioeventfd = virtio_scsi_dataplane_stop;
    hc->pre_plug = virtio_scsi_pre_hotplug;
    hc->plug = virtio_scsi_hotplug;
    hc->unplug = virtio_scsi_hotunplug;
}

static const TypeInfo virtio_scsi_common_info = {
    .name = TYPE_VIRTIO_SCSI_COMMON,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VirtIOSCSICommon),
    .abstract = true,
    .class_init = virtio_scsi_common_class_init,
};

static const TypeInfo virtio_scsi_info = {
    .name = TYPE_VIRTIO_SCSI,
    .parent = TYPE_VIRTIO_SCSI_COMMON,
    .instance_size = sizeof(VirtIOSCSI),
    .class_init = virtio_scsi_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_HOTPLUG_HANDLER },
        { }
    }
};

static void virtio_register_types(void)
{
    type_register_static(&virtio_scsi_common_info);
    type_register_static(&virtio_scsi_info);
}

type_init(virtio_register_types)
