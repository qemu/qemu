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

#include "hw/virtio/virtio-scsi.h"
#include "qemu/error-report.h"
#include "qemu/iov.h"
#include <hw/scsi/scsi.h>
#include <block/scsi.h>
#include <hw/virtio/virtio-bus.h>

typedef struct VirtIOSCSIReq {
    VirtIOSCSI *dev;
    VirtQueue *vq;
    VirtQueueElement elem;
    QEMUSGList qsgl;
    SCSIRequest *sreq;
    union {
        char                  *buf;
        VirtIOSCSICmdReq      *cmd;
        VirtIOSCSICtrlTMFReq  *tmf;
        VirtIOSCSICtrlANReq   *an;
    } req;
    union {
        char                  *buf;
        VirtIOSCSICmdResp     *cmd;
        VirtIOSCSICtrlTMFResp *tmf;
        VirtIOSCSICtrlANResp  *an;
        VirtIOSCSIEvent       *event;
    } resp;
} VirtIOSCSIReq;

static inline int virtio_scsi_get_lun(uint8_t *lun)
{
    return ((lun[2] << 8) | lun[3]) & 0x3FFF;
}

static inline SCSIDevice *virtio_scsi_device_find(VirtIOSCSI *s, uint8_t *lun)
{
    if (lun[0] != 1) {
        return NULL;
    }
    if (lun[2] != 0 && !(lun[2] >= 0x40 && lun[2] < 0x80)) {
        return NULL;
    }
    return scsi_device_find(&s->bus, 0, lun[1], virtio_scsi_get_lun(lun));
}

static VirtIOSCSIReq *virtio_scsi_init_req(VirtIOSCSI *s, VirtQueue *vq)
{
    VirtIOSCSIReq *req;
    req = g_malloc(sizeof(*req));

    req->vq = vq;
    req->dev = s;
    req->sreq = NULL;
    qemu_sglist_init(&req->qsgl, DEVICE(s), 8, &address_space_memory);
    return req;
}

static void virtio_scsi_free_req(VirtIOSCSIReq *req)
{
    qemu_sglist_destroy(&req->qsgl);
    g_free(req);
}

static void virtio_scsi_complete_req(VirtIOSCSIReq *req)
{
    VirtIOSCSI *s = req->dev;
    VirtQueue *vq = req->vq;
    VirtIODevice *vdev = VIRTIO_DEVICE(s);
    virtqueue_push(vq, &req->elem, req->qsgl.size + req->elem.in_sg[0].iov_len);
    if (req->sreq) {
        req->sreq->hba_private = NULL;
        scsi_req_unref(req->sreq);
    }
    virtio_scsi_free_req(req);
    virtio_notify(vdev, vq);
}

static void virtio_scsi_bad_req(void)
{
    error_report("wrong size for virtio-scsi headers");
    exit(1);
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
    if (req->elem.in_num == 0) {
        return -EINVAL;
    }

    if (req->elem.out_sg[0].iov_len < req_size) {
        return -EINVAL;
    }
    if (req->elem.out_num) {
        req->req.buf = req->elem.out_sg[0].iov_base;
    }

    if (req->elem.in_sg[0].iov_len < resp_size) {
        return -EINVAL;
    }
    req->resp.buf = req->elem.in_sg[0].iov_base;

    if (req->elem.out_num > 1) {
        qemu_sgl_concat(req, &req->elem.out_sg[1],
                        &req->elem.out_addr[1],
                        req->elem.out_num - 1, 0);
    } else {
        qemu_sgl_concat(req, &req->elem.in_sg[1],
                        &req->elem.in_addr[1],
                        req->elem.in_num - 1, 0);
    }

    return 0;
}

static VirtIOSCSIReq *virtio_scsi_pop_req(VirtIOSCSI *s, VirtQueue *vq)
{
    VirtIOSCSIReq *req = virtio_scsi_init_req(s, vq);
    if (!virtqueue_pop(vq, &req->elem)) {
        virtio_scsi_free_req(req);
        return NULL;
    }
    return req;
}

static void virtio_scsi_save_request(QEMUFile *f, SCSIRequest *sreq)
{
    VirtIOSCSIReq *req = sreq->hba_private;
    VirtIOSCSICommon *vs = VIRTIO_SCSI_COMMON(req->dev);
    uint32_t n = virtio_queue_get_id(req->vq) - 2;

    assert(n < vs->conf.num_queues);
    qemu_put_be32s(f, &n);
    qemu_put_buffer(f, (unsigned char *)&req->elem, sizeof(req->elem));
}

static void *virtio_scsi_load_request(QEMUFile *f, SCSIRequest *sreq)
{
    SCSIBus *bus = sreq->bus;
    VirtIOSCSI *s = container_of(bus, VirtIOSCSI, bus);
    VirtIOSCSICommon *vs = VIRTIO_SCSI_COMMON(s);
    VirtIOSCSIReq *req;
    uint32_t n;

    qemu_get_be32s(f, &n);
    assert(n < vs->conf.num_queues);
    req = virtio_scsi_init_req(s, vs->cmd_vqs[n]);
    qemu_get_buffer(f, (unsigned char *)&req->elem, sizeof(req->elem));
    /* TODO: add a way for SCSIBusInfo's load_request to fail,
     * and fail migration instead of asserting here.
     * When we do, we might be able to re-enable NDEBUG below.
     */
#ifdef NDEBUG
#error building with NDEBUG is not supported
#endif
    assert(req->elem.in_num <= ARRAY_SIZE(req->elem.in_sg));
    assert(req->elem.out_num <= ARRAY_SIZE(req->elem.out_sg));

    if (virtio_scsi_parse_req(req, sizeof(VirtIOSCSICmdReq) + vs->cdb_size,
                              sizeof(VirtIOSCSICmdResp) + vs->sense_size) < 0) {
        error_report("invalid SCSI request migration data");
        exit(1);
    }

    scsi_req_ref(sreq);
    req->sreq = sreq;
    if (req->sreq->cmd.mode != SCSI_XFER_NONE) {
        int req_mode =
            (req->elem.in_num > 1 ? SCSI_XFER_FROM_DEV : SCSI_XFER_TO_DEV);

        assert(req->sreq->cmd.mode == req_mode);
    }
    return req;
}

static void virtio_scsi_do_tmf(VirtIOSCSI *s, VirtIOSCSIReq *req)
{
    SCSIDevice *d = virtio_scsi_device_find(s, req->req.tmf->lun);
    SCSIRequest *r, *next;
    BusChild *kid;
    int target;

    /* Here VIRTIO_SCSI_S_OK means "FUNCTION COMPLETE".  */
    req->resp.tmf->response = VIRTIO_SCSI_S_OK;

    tswap32s(&req->req.tmf->subtype);
    switch (req->req.tmf->subtype) {
    case VIRTIO_SCSI_T_TMF_ABORT_TASK:
    case VIRTIO_SCSI_T_TMF_QUERY_TASK:
        if (!d) {
            goto fail;
        }
        if (d->lun != virtio_scsi_get_lun(req->req.tmf->lun)) {
            goto incorrect_lun;
        }
        QTAILQ_FOREACH_SAFE(r, &d->requests, next, next) {
            VirtIOSCSIReq *cmd_req = r->hba_private;
            if (cmd_req && cmd_req->req.cmd->tag == req->req.tmf->tag) {
                break;
            }
        }
        if (r) {
            /*
             * Assert that the request has not been completed yet, we
             * check for it in the loop above.
             */
            assert(r->hba_private);
            if (req->req.tmf->subtype == VIRTIO_SCSI_T_TMF_QUERY_TASK) {
                /* "If the specified command is present in the task set, then
                 * return a service response set to FUNCTION SUCCEEDED".
                 */
                req->resp.tmf->response = VIRTIO_SCSI_S_FUNCTION_SUCCEEDED;
            } else {
                scsi_req_cancel(r);
            }
        }
        break;

    case VIRTIO_SCSI_T_TMF_LOGICAL_UNIT_RESET:
        if (!d) {
            goto fail;
        }
        if (d->lun != virtio_scsi_get_lun(req->req.tmf->lun)) {
            goto incorrect_lun;
        }
        s->resetting++;
        qdev_reset_all(&d->qdev);
        s->resetting--;
        break;

    case VIRTIO_SCSI_T_TMF_ABORT_TASK_SET:
    case VIRTIO_SCSI_T_TMF_CLEAR_TASK_SET:
    case VIRTIO_SCSI_T_TMF_QUERY_TASK_SET:
        if (!d) {
            goto fail;
        }
        if (d->lun != virtio_scsi_get_lun(req->req.tmf->lun)) {
            goto incorrect_lun;
        }
        QTAILQ_FOREACH_SAFE(r, &d->requests, next, next) {
            if (r->hba_private) {
                if (req->req.tmf->subtype == VIRTIO_SCSI_T_TMF_QUERY_TASK_SET) {
                    /* "If there is any command present in the task set, then
                     * return a service response set to FUNCTION SUCCEEDED".
                     */
                    req->resp.tmf->response = VIRTIO_SCSI_S_FUNCTION_SUCCEEDED;
                    break;
                } else {
                    scsi_req_cancel(r);
                }
            }
        }
        break;

    case VIRTIO_SCSI_T_TMF_I_T_NEXUS_RESET:
        target = req->req.tmf->lun[1];
        s->resetting++;
        QTAILQ_FOREACH(kid, &s->bus.qbus.children, sibling) {
             d = DO_UPCAST(SCSIDevice, qdev, kid->child);
             if (d->channel == 0 && d->id == target) {
                qdev_reset_all(&d->qdev);
             }
        }
        s->resetting--;
        break;

    case VIRTIO_SCSI_T_TMF_CLEAR_ACA:
    default:
        req->resp.tmf->response = VIRTIO_SCSI_S_FUNCTION_REJECTED;
        break;
    }

    return;

incorrect_lun:
    req->resp.tmf->response = VIRTIO_SCSI_S_INCORRECT_LUN;
    return;

fail:
    req->resp.tmf->response = VIRTIO_SCSI_S_BAD_TARGET;
}

static void virtio_scsi_handle_ctrl(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIOSCSI *s = (VirtIOSCSI *)vdev;
    VirtIOSCSIReq *req;

    while ((req = virtio_scsi_pop_req(s, vq))) {
        int type;

        if (iov_to_buf(req->elem.out_sg, req->elem.out_num, 0,
                       &type, sizeof(type)) < sizeof(type)) {
            virtio_scsi_bad_req();
            continue;
        }

        tswap32s(&req->req.tmf->type);
        if (req->req.tmf->type == VIRTIO_SCSI_T_TMF) {
            if (virtio_scsi_parse_req(req, sizeof(VirtIOSCSICtrlTMFReq),
                                      sizeof(VirtIOSCSICtrlTMFResp)) < 0) {
                virtio_scsi_bad_req();
            } else {
                virtio_scsi_do_tmf(s, req);
            }

        } else if (req->req.tmf->type == VIRTIO_SCSI_T_AN_QUERY ||
                   req->req.tmf->type == VIRTIO_SCSI_T_AN_SUBSCRIBE) {
            if (virtio_scsi_parse_req(req, sizeof(VirtIOSCSICtrlANReq),
                                      sizeof(VirtIOSCSICtrlANResp)) < 0) {
                virtio_scsi_bad_req();
            } else {
                req->resp.an->event_actual = 0;
                req->resp.an->response = VIRTIO_SCSI_S_OK;
            }
        }
        virtio_scsi_complete_req(req);
    }
}

static void virtio_scsi_command_complete(SCSIRequest *r, uint32_t status,
                                         size_t resid)
{
    VirtIOSCSIReq *req = r->hba_private;
    VirtIOSCSI *s = req->dev;
    VirtIOSCSICommon *vs = VIRTIO_SCSI_COMMON(s);
    uint32_t sense_len;

    if (r->io_canceled) {
        return;
    }

    req->resp.cmd->response = VIRTIO_SCSI_S_OK;
    req->resp.cmd->status = status;
    if (req->resp.cmd->status == GOOD) {
        req->resp.cmd->resid = tswap32(resid);
    } else {
        req->resp.cmd->resid = 0;
        sense_len = scsi_req_get_sense(r, req->resp.cmd->sense,
                                       vs->sense_size);
        req->resp.cmd->sense_len = tswap32(sense_len);
    }
    virtio_scsi_complete_req(req);
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
    if (req->dev->resetting) {
        req->resp.cmd->response = VIRTIO_SCSI_S_RESET;
    } else {
        req->resp.cmd->response = VIRTIO_SCSI_S_ABORTED;
    }
    virtio_scsi_complete_req(req);
}

static void virtio_scsi_fail_cmd_req(VirtIOSCSIReq *req)
{
    req->resp.cmd->response = VIRTIO_SCSI_S_FAILURE;
    virtio_scsi_complete_req(req);
}

static void virtio_scsi_handle_cmd(VirtIODevice *vdev, VirtQueue *vq)
{
    /* use non-QOM casts in the data path */
    VirtIOSCSI *s = (VirtIOSCSI *)vdev;
    VirtIOSCSICommon *vs = &s->parent_obj;

    VirtIOSCSIReq *req;
    int n;

    while ((req = virtio_scsi_pop_req(s, vq))) {
        SCSIDevice *d;
        int rc;
        if (req->elem.out_num > 1 && req->elem.in_num > 1) {
            virtio_scsi_fail_cmd_req(req);
            continue;
        }

        rc = virtio_scsi_parse_req(req, sizeof(VirtIOSCSICmdReq) + vs->cdb_size,
                                   sizeof(VirtIOSCSICmdResp) + vs->sense_size);
        if (rc < 0) {
            virtio_scsi_bad_req();
        }

        d = virtio_scsi_device_find(s, req->req.cmd->lun);
        if (!d) {
            req->resp.cmd->response = VIRTIO_SCSI_S_BAD_TARGET;
            virtio_scsi_complete_req(req);
            continue;
        }
        req->sreq = scsi_req_new(d, req->req.cmd->tag,
                                 virtio_scsi_get_lun(req->req.cmd->lun),
                                 req->req.cmd->cdb, req);

        if (req->sreq->cmd.mode != SCSI_XFER_NONE) {
            int req_mode =
                (req->elem.in_num > 1 ? SCSI_XFER_FROM_DEV : SCSI_XFER_TO_DEV);

            if (req->sreq->cmd.mode != req_mode ||
                req->sreq->cmd.xfer > req->qsgl.size) {
                req->resp.cmd->response = VIRTIO_SCSI_S_OVERRUN;
                virtio_scsi_complete_req(req);
                continue;
            }
        }

        n = scsi_req_enqueue(req->sreq);
        if (n) {
            scsi_req_continue(req->sreq);
        }
    }
}

static void virtio_scsi_get_config(VirtIODevice *vdev,
                                   uint8_t *config)
{
    VirtIOSCSIConfig *scsiconf = (VirtIOSCSIConfig *)config;
    VirtIOSCSICommon *s = VIRTIO_SCSI_COMMON(vdev);

    stl_p(&scsiconf->num_queues, s->conf.num_queues);
    stl_p(&scsiconf->seg_max, 128 - 2);
    stl_p(&scsiconf->max_sectors, s->conf.max_sectors);
    stl_p(&scsiconf->cmd_per_lun, s->conf.cmd_per_lun);
    stl_p(&scsiconf->event_info_size, sizeof(VirtIOSCSIEvent));
    stl_p(&scsiconf->sense_size, s->sense_size);
    stl_p(&scsiconf->cdb_size, s->cdb_size);
    stw_p(&scsiconf->max_channel, VIRTIO_SCSI_MAX_CHANNEL);
    stw_p(&scsiconf->max_target, VIRTIO_SCSI_MAX_TARGET);
    stl_p(&scsiconf->max_lun, VIRTIO_SCSI_MAX_LUN);
}

static void virtio_scsi_set_config(VirtIODevice *vdev,
                                   const uint8_t *config)
{
    VirtIOSCSIConfig *scsiconf = (VirtIOSCSIConfig *)config;
    VirtIOSCSICommon *vs = VIRTIO_SCSI_COMMON(vdev);

    if ((uint32_t) ldl_p(&scsiconf->sense_size) >= 65536 ||
        (uint32_t) ldl_p(&scsiconf->cdb_size) >= 256) {
        error_report("bad data written to virtio-scsi configuration space");
        exit(1);
    }

    vs->sense_size = ldl_p(&scsiconf->sense_size);
    vs->cdb_size = ldl_p(&scsiconf->cdb_size);
}

static uint32_t virtio_scsi_get_features(VirtIODevice *vdev,
                                         uint32_t requested_features)
{
    return requested_features;
}

static void virtio_scsi_reset(VirtIODevice *vdev)
{
    VirtIOSCSI *s = VIRTIO_SCSI(vdev);
    VirtIOSCSICommon *vs = VIRTIO_SCSI_COMMON(vdev);

    s->resetting++;
    qbus_reset_all(&s->bus.qbus);
    s->resetting--;

    vs->sense_size = VIRTIO_SCSI_SENSE_SIZE;
    vs->cdb_size = VIRTIO_SCSI_CDB_SIZE;
    s->events_dropped = false;
}

/* The device does not have anything to save beyond the virtio data.
 * Request data is saved with callbacks from SCSI devices.
 */
static void virtio_scsi_save(QEMUFile *f, void *opaque)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(opaque);
    virtio_save(vdev, f);
}

static int virtio_scsi_load(QEMUFile *f, void *opaque, int version_id)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(opaque);
    int ret;

    ret = virtio_load(vdev, f);
    if (ret) {
        return ret;
    }
    return 0;
}

static void virtio_scsi_push_event(VirtIOSCSI *s, SCSIDevice *dev,
                                   uint32_t event, uint32_t reason)
{
    VirtIOSCSICommon *vs = VIRTIO_SCSI_COMMON(s);
    VirtIOSCSIReq *req;
    VirtIOSCSIEvent *evt;
    VirtIODevice *vdev = VIRTIO_DEVICE(s);
    int in_size;

    if (!(vdev->status & VIRTIO_CONFIG_S_DRIVER_OK)) {
        return;
    }

    req = virtio_scsi_pop_req(s, vs->event_vq);
    if (!req) {
        s->events_dropped = true;
        return;
    }

    if (req->elem.out_num || req->elem.in_num != 1) {
        virtio_scsi_bad_req();
    }

    if (s->events_dropped) {
        event |= VIRTIO_SCSI_T_EVENTS_MISSED;
        s->events_dropped = false;
    }

    in_size = req->elem.in_sg[0].iov_len;
    if (in_size < sizeof(VirtIOSCSIEvent)) {
        virtio_scsi_bad_req();
    }

    evt = req->resp.event;
    memset(evt, 0, sizeof(VirtIOSCSIEvent));
    evt->event = event;
    evt->reason = reason;
    if (!dev) {
        assert(event == VIRTIO_SCSI_T_EVENTS_MISSED);
    } else {
        evt->lun[0] = 1;
        evt->lun[1] = dev->id;

        /* Linux wants us to keep the same encoding we use for REPORT LUNS.  */
        if (dev->lun >= 256) {
            evt->lun[2] = (dev->lun >> 8) | 0x40;
        }
        evt->lun[3] = dev->lun & 0xFF;
    }
    virtio_scsi_complete_req(req);
}

static void virtio_scsi_handle_event(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIOSCSI *s = VIRTIO_SCSI(vdev);

    if (s->events_dropped) {
        virtio_scsi_push_event(s, NULL, VIRTIO_SCSI_T_NO_EVENT, 0);
    }
}

static void virtio_scsi_change(SCSIBus *bus, SCSIDevice *dev, SCSISense sense)
{
    VirtIOSCSI *s = container_of(bus, VirtIOSCSI, bus);
    VirtIODevice *vdev = VIRTIO_DEVICE(s);

    if (((vdev->guest_features >> VIRTIO_SCSI_F_CHANGE) & 1) &&
        dev->type != TYPE_ROM) {
        virtio_scsi_push_event(s, dev, VIRTIO_SCSI_T_PARAM_CHANGE,
                               sense.asc | (sense.ascq << 8));
    }
}

static void virtio_scsi_hotplug(SCSIBus *bus, SCSIDevice *dev)
{
    VirtIOSCSI *s = container_of(bus, VirtIOSCSI, bus);
    VirtIODevice *vdev = VIRTIO_DEVICE(s);

    if ((vdev->guest_features >> VIRTIO_SCSI_F_HOTPLUG) & 1) {
        virtio_scsi_push_event(s, dev, VIRTIO_SCSI_T_TRANSPORT_RESET,
                               VIRTIO_SCSI_EVT_RESET_RESCAN);
    }
}

static void virtio_scsi_hot_unplug(SCSIBus *bus, SCSIDevice *dev)
{
    VirtIOSCSI *s = container_of(bus, VirtIOSCSI, bus);
    VirtIODevice *vdev = VIRTIO_DEVICE(s);

    if ((vdev->guest_features >> VIRTIO_SCSI_F_HOTPLUG) & 1) {
        virtio_scsi_push_event(s, dev, VIRTIO_SCSI_T_TRANSPORT_RESET,
                               VIRTIO_SCSI_EVT_RESET_REMOVED);
    }
}

static struct SCSIBusInfo virtio_scsi_scsi_info = {
    .tcq = true,
    .max_channel = VIRTIO_SCSI_MAX_CHANNEL,
    .max_target = VIRTIO_SCSI_MAX_TARGET,
    .max_lun = VIRTIO_SCSI_MAX_LUN,

    .complete = virtio_scsi_command_complete,
    .cancel = virtio_scsi_request_cancelled,
    .change = virtio_scsi_change,
    .hotplug = virtio_scsi_hotplug,
    .hot_unplug = virtio_scsi_hot_unplug,
    .get_sg_list = virtio_scsi_get_sg_list,
    .save_request = virtio_scsi_save_request,
    .load_request = virtio_scsi_load_request,
};

void virtio_scsi_common_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIOSCSICommon *s = VIRTIO_SCSI_COMMON(dev);
    int i;

    virtio_init(vdev, "virtio-scsi", VIRTIO_ID_SCSI,
                sizeof(VirtIOSCSIConfig));

    s->cmd_vqs = g_malloc0(s->conf.num_queues * sizeof(VirtQueue *));
    s->sense_size = VIRTIO_SCSI_SENSE_SIZE;
    s->cdb_size = VIRTIO_SCSI_CDB_SIZE;

    s->ctrl_vq = virtio_add_queue(vdev, VIRTIO_SCSI_VQ_SIZE,
                                  virtio_scsi_handle_ctrl);
    s->event_vq = virtio_add_queue(vdev, VIRTIO_SCSI_VQ_SIZE,
                                   virtio_scsi_handle_event);
    for (i = 0; i < s->conf.num_queues; i++) {
        s->cmd_vqs[i] = virtio_add_queue(vdev, VIRTIO_SCSI_VQ_SIZE,
                                         virtio_scsi_handle_cmd);
    }
}

static void virtio_scsi_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIOSCSI *s = VIRTIO_SCSI(dev);
    static int virtio_scsi_id;
    Error *err = NULL;

    virtio_scsi_common_realize(dev, &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }

    scsi_bus_new(&s->bus, sizeof(s->bus), dev,
                 &virtio_scsi_scsi_info, vdev->bus_name);

    if (!dev->hotplugged) {
        scsi_bus_legacy_handle_cmdline(&s->bus, &err);
        if (err != NULL) {
            error_propagate(errp, err);
            return;
        }
    }

    register_savevm(dev, "virtio-scsi", virtio_scsi_id++, 1,
                    virtio_scsi_save, virtio_scsi_load, s);
}

void virtio_scsi_common_unrealize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIOSCSICommon *vs = VIRTIO_SCSI_COMMON(dev);

    g_free(vs->cmd_vqs);
    virtio_cleanup(vdev);
}

static void virtio_scsi_device_unrealize(DeviceState *dev, Error **errp)
{
    VirtIOSCSI *s = VIRTIO_SCSI(dev);

    unregister_savevm(dev, "virtio-scsi", s);

    virtio_scsi_common_unrealize(dev, errp);
}

static Property virtio_scsi_properties[] = {
    DEFINE_VIRTIO_SCSI_PROPERTIES(VirtIOSCSI, parent_obj.conf),
    DEFINE_PROP_END_OF_LIST(),
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

    dc->props = virtio_scsi_properties;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    vdc->realize = virtio_scsi_device_realize;
    vdc->unrealize = virtio_scsi_device_unrealize;
    vdc->set_config = virtio_scsi_set_config;
    vdc->get_features = virtio_scsi_get_features;
    vdc->reset = virtio_scsi_reset;
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
};

static void virtio_register_types(void)
{
    type_register_static(&virtio_scsi_common_info);
    type_register_static(&virtio_scsi_info);
}

type_init(virtio_register_types)
