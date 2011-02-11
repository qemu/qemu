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

    VirtQueue *ctrl_vq;
    VirtQueue *event_vq;
    VirtQueue *cmd_vq;
    uint32_t sense_size;
    uint32_t cdb_size;
} VirtIOSCSI;

static void virtio_scsi_handle_ctrl(VirtIODevice *vdev, VirtQueue *vq)
{
    /* TODO */
}

static void virtio_scsi_handle_cmd(VirtIODevice *vdev, VirtQueue *vq)
{
    /* TODO */
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

VirtIODevice *virtio_scsi_init(DeviceState *dev, VirtIOSCSIConf *proxyconf)
{
    VirtIOSCSI *s;

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

    /* TODO savevm */

    return &s->vdev;
}

void virtio_scsi_exit(VirtIODevice *vdev)
{
    virtio_cleanup(vdev);
}
