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

#include "virtio-scsi.h"
#include <hw/scsi.h>
#include <hw/scsi-defs.h>

#define VIRTIO_SCSI_VQ_SIZE     128
#define VIRTIO_SCSI_CDB_SIZE    32
#define VIRTIO_SCSI_SENSE_SIZE  96
#define VIRTIO_SCSI_MAX_CHANNEL 0
#define VIRTIO_SCSI_MAX_TARGET  255
#define VIRTIO_SCSI_MAX_LUN     16383

/* Response codes */
#define VIRTIO_SCSI_S_OK                       0
#define VIRTIO_SCSI_S_OVERRUN                  1
#define VIRTIO_SCSI_S_ABORTED                  2
#define VIRTIO_SCSI_S_BAD_TARGET               3
#define VIRTIO_SCSI_S_RESET                    4
#define VIRTIO_SCSI_S_BUSY                     5
#define VIRTIO_SCSI_S_TRANSPORT_FAILURE        6
#define VIRTIO_SCSI_S_TARGET_FAILURE           7
#define VIRTIO_SCSI_S_NEXUS_FAILURE            8
#define VIRTIO_SCSI_S_FAILURE                  9
#define VIRTIO_SCSI_S_FUNCTION_SUCCEEDED       10
#define VIRTIO_SCSI_S_FUNCTION_REJECTED        11
#define VIRTIO_SCSI_S_INCORRECT_LUN            12

/* Controlq type codes.  */
#define VIRTIO_SCSI_T_TMF                      0
#define VIRTIO_SCSI_T_AN_QUERY                 1
#define VIRTIO_SCSI_T_AN_SUBSCRIBE             2

/* Valid TMF subtypes.  */
#define VIRTIO_SCSI_T_TMF_ABORT_TASK           0
#define VIRTIO_SCSI_T_TMF_ABORT_TASK_SET       1
#define VIRTIO_SCSI_T_TMF_CLEAR_ACA            2
#define VIRTIO_SCSI_T_TMF_CLEAR_TASK_SET       3
#define VIRTIO_SCSI_T_TMF_I_T_NEXUS_RESET      4
#define VIRTIO_SCSI_T_TMF_LOGICAL_UNIT_RESET   5
#define VIRTIO_SCSI_T_TMF_QUERY_TASK           6
#define VIRTIO_SCSI_T_TMF_QUERY_TASK_SET       7

/* Events.  */
#define VIRTIO_SCSI_T_EVENTS_MISSED            0x80000000
#define VIRTIO_SCSI_T_NO_EVENT                 0
#define VIRTIO_SCSI_T_TRANSPORT_RESET          1
#define VIRTIO_SCSI_T_ASYNC_NOTIFY             2

/* SCSI command request, followed by data-out */
typedef struct {
    uint8_t lun[8];              /* Logical Unit Number */
    uint64_t tag;                /* Command identifier */
    uint8_t task_attr;           /* Task attribute */
    uint8_t prio;
    uint8_t crn;
    uint8_t cdb[];
} QEMU_PACKED VirtIOSCSICmdReq;

/* Response, followed by sense data and data-in */
typedef struct {
    uint32_t sense_len;          /* Sense data length */
    uint32_t resid;              /* Residual bytes in data buffer */
    uint16_t status_qualifier;   /* Status qualifier */
    uint8_t status;              /* Command completion status */
    uint8_t response;            /* Response values */
    uint8_t sense[];
} QEMU_PACKED VirtIOSCSICmdResp;

/* Task Management Request */
typedef struct {
    uint32_t type;
    uint32_t subtype;
    uint8_t lun[8];
    uint64_t tag;
} QEMU_PACKED VirtIOSCSICtrlTMFReq;

typedef struct {
    uint8_t response;
} QEMU_PACKED VirtIOSCSICtrlTMFResp;

/* Asynchronous notification query/subscription */
typedef struct {
    uint32_t type;
    uint8_t lun[8];
    uint32_t event_requested;
} QEMU_PACKED VirtIOSCSICtrlANReq;

typedef struct {
    uint32_t event_actual;
    uint8_t response;
} QEMU_PACKED VirtIOSCSICtrlANResp;

typedef struct {
    uint32_t event;
    uint8_t lun[8];
    uint32_t reason;
} QEMU_PACKED VirtIOSCSIEvent;

typedef struct {
    uint32_t num_queues;
    uint32_t seg_max;
    uint32_t max_sectors;
    uint32_t cmd_per_lun;
    uint32_t event_info_size;
    uint32_t sense_size;
    uint32_t cdb_size;
    uint16_t max_channel;
    uint16_t max_target;
    uint32_t max_lun;
} QEMU_PACKED VirtIOSCSIConfig;

typedef struct {
    VirtIODevice vdev;
    DeviceState *qdev;
    VirtIOSCSIConf *conf;

    SCSIBus bus;
    VirtQueue *ctrl_vq;
    VirtQueue *event_vq;
    VirtQueue *cmd_vq;
    uint32_t sense_size;
    uint32_t cdb_size;
    int resetting;
} VirtIOSCSI;

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

static void virtio_scsi_complete_req(VirtIOSCSIReq *req)
{
    VirtIOSCSI *s = req->dev;
    VirtQueue *vq = req->vq;
    virtqueue_push(vq, &req->elem, req->qsgl.size + req->elem.in_sg[0].iov_len);
    qemu_sglist_destroy(&req->qsgl);
    if (req->sreq) {
        req->sreq->hba_private = NULL;
        scsi_req_unref(req->sreq);
    }
    g_free(req);
    virtio_notify(&s->vdev, vq);
}

static void virtio_scsi_bad_req(void)
{
    error_report("wrong size for virtio-scsi headers");
    exit(1);
}

static void qemu_sgl_init_external(QEMUSGList *qsgl, struct iovec *sg,
                                   target_phys_addr_t *addr, int num)
{
    memset(qsgl, 0, sizeof(*qsgl));
    while (num--) {
        qemu_sglist_add(qsgl, *(addr++), (sg++)->iov_len);
    }
}

static void virtio_scsi_parse_req(VirtIOSCSI *s, VirtQueue *vq,
                                  VirtIOSCSIReq *req)
{
    assert(req->elem.out_num && req->elem.in_num);
    req->vq = vq;
    req->dev = s;
    req->sreq = NULL;
    req->req.buf = req->elem.out_sg[0].iov_base;
    req->resp.buf = req->elem.in_sg[0].iov_base;

    if (req->elem.out_num > 1) {
        qemu_sgl_init_external(&req->qsgl, &req->elem.out_sg[1],
                               &req->elem.out_addr[1],
                               req->elem.out_num - 1);
    } else {
        qemu_sgl_init_external(&req->qsgl, &req->elem.in_sg[1],
                               &req->elem.in_addr[1],
                               req->elem.in_num - 1);
    }
}

static VirtIOSCSIReq *virtio_scsi_pop_req(VirtIOSCSI *s, VirtQueue *vq)
{
    VirtIOSCSIReq *req;
    req = g_malloc(sizeof(*req));
    if (!virtqueue_pop(vq, &req->elem)) {
        g_free(req);
        return NULL;
    }

    virtio_scsi_parse_req(s, vq, req);
    return req;
}

static void virtio_scsi_save_request(QEMUFile *f, SCSIRequest *sreq)
{
    VirtIOSCSIReq *req = sreq->hba_private;

    qemu_put_buffer(f, (unsigned char *)&req->elem, sizeof(req->elem));
}

static void *virtio_scsi_load_request(QEMUFile *f, SCSIRequest *sreq)
{
    SCSIBus *bus = sreq->bus;
    VirtIOSCSI *s = container_of(bus, VirtIOSCSI, bus);
    VirtIOSCSIReq *req;

    req = g_malloc(sizeof(*req));
    qemu_get_buffer(f, (unsigned char *)&req->elem, sizeof(req->elem));
    virtio_scsi_parse_req(s, s->cmd_vq, req);

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
    DeviceState *qdev;
    int target;

    /* Here VIRTIO_SCSI_S_OK means "FUNCTION COMPLETE".  */
    req->resp.tmf->response = VIRTIO_SCSI_S_OK;

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
            if (r->tag == req->req.tmf->tag) {
                break;
            }
        }
        if (r && r->hba_private) {
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
        QTAILQ_FOREACH(qdev, &s->bus.qbus.children, sibling) {
             d = DO_UPCAST(SCSIDevice, qdev, qdev);
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
        int out_size, in_size;
        if (req->elem.out_num < 1 || req->elem.in_num < 1) {
            virtio_scsi_bad_req();
            continue;
        }

        out_size = req->elem.out_sg[0].iov_len;
        in_size = req->elem.in_sg[0].iov_len;
        if (req->req.tmf->type == VIRTIO_SCSI_T_TMF) {
            if (out_size < sizeof(VirtIOSCSICtrlTMFReq) ||
                in_size < sizeof(VirtIOSCSICtrlTMFResp)) {
                virtio_scsi_bad_req();
            }
            virtio_scsi_do_tmf(s, req);

        } else if (req->req.tmf->type == VIRTIO_SCSI_T_AN_QUERY ||
                   req->req.tmf->type == VIRTIO_SCSI_T_AN_SUBSCRIBE) {
            if (out_size < sizeof(VirtIOSCSICtrlANReq) ||
                in_size < sizeof(VirtIOSCSICtrlANResp)) {
                virtio_scsi_bad_req();
            }
            req->resp.an->event_actual = 0;
            req->resp.an->response = VIRTIO_SCSI_S_OK;
        }
        virtio_scsi_complete_req(req);
    }
}

static void virtio_scsi_command_complete(SCSIRequest *r, uint32_t status,
                                         size_t resid)
{
    VirtIOSCSIReq *req = r->hba_private;

    req->resp.cmd->response = VIRTIO_SCSI_S_OK;
    req->resp.cmd->status = status;
    if (req->resp.cmd->status == GOOD) {
        req->resp.cmd->resid = resid;
    } else {
        req->resp.cmd->resid = 0;
        req->resp.cmd->sense_len =
            scsi_req_get_sense(r, req->resp.cmd->sense, VIRTIO_SCSI_SENSE_SIZE);
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
    VirtIOSCSI *s = (VirtIOSCSI *)vdev;
    VirtIOSCSIReq *req;
    int n;

    while ((req = virtio_scsi_pop_req(s, vq))) {
        SCSIDevice *d;
        int out_size, in_size;
        if (req->elem.out_num < 1 || req->elem.in_num < 1) {
            virtio_scsi_bad_req();
        }

        out_size = req->elem.out_sg[0].iov_len;
        in_size = req->elem.in_sg[0].iov_len;
        if (out_size < sizeof(VirtIOSCSICmdReq) + s->cdb_size ||
            in_size < sizeof(VirtIOSCSICmdResp) + s->sense_size) {
            virtio_scsi_bad_req();
        }

        if (req->elem.out_num > 1 && req->elem.in_num > 1) {
            virtio_scsi_fail_cmd_req(req);
            continue;
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
    VirtIOSCSI *s = (VirtIOSCSI *)vdev;

    stl_raw(&scsiconf->num_queues, s->conf->num_queues);
    stl_raw(&scsiconf->seg_max, 128 - 2);
    stl_raw(&scsiconf->max_sectors, s->conf->max_sectors);
    stl_raw(&scsiconf->cmd_per_lun, s->conf->cmd_per_lun);
    stl_raw(&scsiconf->event_info_size, sizeof(VirtIOSCSIEvent));
    stl_raw(&scsiconf->sense_size, s->sense_size);
    stl_raw(&scsiconf->cdb_size, s->cdb_size);
    stl_raw(&scsiconf->max_channel, VIRTIO_SCSI_MAX_CHANNEL);
    stl_raw(&scsiconf->max_target, VIRTIO_SCSI_MAX_TARGET);
    stl_raw(&scsiconf->max_lun, VIRTIO_SCSI_MAX_LUN);
}

static void virtio_scsi_set_config(VirtIODevice *vdev,
                                   const uint8_t *config)
{
    VirtIOSCSIConfig *scsiconf = (VirtIOSCSIConfig *)config;
    VirtIOSCSI *s = (VirtIOSCSI *)vdev;

    if ((uint32_t) ldl_raw(&scsiconf->sense_size) >= 65536 ||
        (uint32_t) ldl_raw(&scsiconf->cdb_size) >= 256) {
        error_report("bad data written to virtio-scsi configuration space");
        exit(1);
    }

    s->sense_size = ldl_raw(&scsiconf->sense_size);
    s->cdb_size = ldl_raw(&scsiconf->cdb_size);
}

static uint32_t virtio_scsi_get_features(VirtIODevice *vdev,
                                         uint32_t requested_features)
{
    return requested_features;
}

static void virtio_scsi_reset(VirtIODevice *vdev)
{
    VirtIOSCSI *s = (VirtIOSCSI *)vdev;

    s->sense_size = VIRTIO_SCSI_SENSE_SIZE;
    s->cdb_size = VIRTIO_SCSI_CDB_SIZE;
}

/* The device does not have anything to save beyond the virtio data.
 * Request data is saved with callbacks from SCSI devices.
 */
static void virtio_scsi_save(QEMUFile *f, void *opaque)
{
    VirtIOSCSI *s = opaque;
    virtio_save(&s->vdev, f);
}

static int virtio_scsi_load(QEMUFile *f, void *opaque, int version_id)
{
    VirtIOSCSI *s = opaque;
    virtio_load(&s->vdev, f);
    return 0;
}

static struct SCSIBusInfo virtio_scsi_scsi_info = {
    .tcq = true,
    .max_channel = VIRTIO_SCSI_MAX_CHANNEL,
    .max_target = VIRTIO_SCSI_MAX_TARGET,
    .max_lun = VIRTIO_SCSI_MAX_LUN,

    .complete = virtio_scsi_command_complete,
    .cancel = virtio_scsi_request_cancelled,
    .get_sg_list = virtio_scsi_get_sg_list,
    .save_request = virtio_scsi_save_request,
    .load_request = virtio_scsi_load_request,
};

VirtIODevice *virtio_scsi_init(DeviceState *dev, VirtIOSCSIConf *proxyconf)
{
    VirtIOSCSI *s;
    static int virtio_scsi_id;

    s = (VirtIOSCSI *)virtio_common_init("virtio-scsi", VIRTIO_ID_SCSI,
                                         sizeof(VirtIOSCSIConfig),
                                         sizeof(VirtIOSCSI));

    s->qdev = dev;
    s->conf = proxyconf;

    /* TODO set up vdev function pointers */
    s->vdev.get_config = virtio_scsi_get_config;
    s->vdev.set_config = virtio_scsi_set_config;
    s->vdev.get_features = virtio_scsi_get_features;
    s->vdev.reset = virtio_scsi_reset;

    s->ctrl_vq = virtio_add_queue(&s->vdev, VIRTIO_SCSI_VQ_SIZE,
                                   virtio_scsi_handle_ctrl);
    s->event_vq = virtio_add_queue(&s->vdev, VIRTIO_SCSI_VQ_SIZE,
                                   NULL);
    s->cmd_vq = virtio_add_queue(&s->vdev, VIRTIO_SCSI_VQ_SIZE,
                                   virtio_scsi_handle_cmd);

    scsi_bus_new(&s->bus, dev, &virtio_scsi_scsi_info);
    if (!dev->hotplugged) {
        scsi_bus_legacy_handle_cmdline(&s->bus);
    }

    register_savevm(dev, "virtio-scsi", virtio_scsi_id++, 1,
                    virtio_scsi_save, virtio_scsi_load, s);

    return &s->vdev;
}

void virtio_scsi_exit(VirtIODevice *vdev)
{
    VirtIOSCSI *s = (VirtIOSCSI *)vdev;
    unregister_savevm(s->qdev, "virtio-scsi", s);
    virtio_cleanup(vdev);
}
