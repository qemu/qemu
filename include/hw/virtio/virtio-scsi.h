/*
 * Virtio SCSI HBA
 *
 * Copyright IBM, Corp. 2010
 *
 * Authors:
 *  Stefan Hajnoczi    <stefanha@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef _QEMU_VIRTIO_SCSI_H
#define _QEMU_VIRTIO_SCSI_H

#include "hw/virtio/virtio.h"
#include "hw/pci/pci.h"
#include "hw/scsi/scsi.h"
#include "sysemu/iothread.h"
#include "hw/virtio/dataplane/vring.h"

#define TYPE_VIRTIO_SCSI_COMMON "virtio-scsi-common"
#define VIRTIO_SCSI_COMMON(obj) \
        OBJECT_CHECK(VirtIOSCSICommon, (obj), TYPE_VIRTIO_SCSI_COMMON)

#define TYPE_VIRTIO_SCSI "virtio-scsi-device"
#define VIRTIO_SCSI(obj) \
        OBJECT_CHECK(VirtIOSCSI, (obj), TYPE_VIRTIO_SCSI)


/* The ID for virtio_scsi */
#define VIRTIO_ID_SCSI  8

/* Feature Bits */
#define VIRTIO_SCSI_F_INOUT                    0
#define VIRTIO_SCSI_F_HOTPLUG                  1
#define VIRTIO_SCSI_F_CHANGE                   2

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
#define VIRTIO_SCSI_T_PARAM_CHANGE             3

/* Reasons for transport reset event */
#define VIRTIO_SCSI_EVT_RESET_HARD             0
#define VIRTIO_SCSI_EVT_RESET_RESCAN           1
#define VIRTIO_SCSI_EVT_RESET_REMOVED          2

/* SCSI command request, followed by CDB and data-out */
typedef struct {
    uint8_t lun[8];              /* Logical Unit Number */
    uint64_t tag;                /* Command identifier */
    uint8_t task_attr;           /* Task attribute */
    uint8_t prio;
    uint8_t crn;
} QEMU_PACKED VirtIOSCSICmdReq;

/* Response, followed by sense data and data-in */
typedef struct {
    uint32_t sense_len;          /* Sense data length */
    uint32_t resid;              /* Residual bytes in data buffer */
    uint16_t status_qualifier;   /* Status qualifier */
    uint8_t status;              /* Command completion status */
    uint8_t response;            /* Response values */
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

struct VirtIOSCSIConf {
    uint32_t num_queues;
    uint32_t max_sectors;
    uint32_t cmd_per_lun;
    char *vhostfd;
    char *wwpn;
    IOThread *iothread;
};

struct VirtIOSCSI;

typedef struct {
    struct VirtIOSCSI *parent;
    Vring vring;
    EventNotifier host_notifier;
    EventNotifier guest_notifier;
} VirtIOSCSIVring;

typedef struct VirtIOSCSICommon {
    VirtIODevice parent_obj;
    VirtIOSCSIConf conf;

    uint32_t sense_size;
    uint32_t cdb_size;
    VirtQueue *ctrl_vq;
    VirtQueue *event_vq;
    VirtQueue **cmd_vqs;
} VirtIOSCSICommon;

typedef struct VirtIOSCSI {
    VirtIOSCSICommon parent_obj;

    SCSIBus bus;
    int resetting;
    bool events_dropped;

    /* Fields for dataplane below */
    AioContext *ctx; /* one iothread per virtio-scsi-pci for now */

    /* Vring is used instead of vq in dataplane code, because of the underlying
     * memory layer thread safety */
    VirtIOSCSIVring *ctrl_vring;
    VirtIOSCSIVring *event_vring;
    VirtIOSCSIVring **cmd_vrings;
    bool dataplane_started;
    bool dataplane_starting;
    bool dataplane_stopping;
    bool dataplane_disabled;
    Notifier migration_state_notifier;
} VirtIOSCSI;

typedef struct VirtIOSCSIReq {
    VirtIOSCSI *dev;
    VirtQueue *vq;
    QEMUSGList qsgl;
    QEMUIOVector resp_iov;

    /* Note:
     * - fields before elem are initialized by virtio_scsi_init_req;
     * - elem is uninitialized at the time of allocation.
     * - fields after elem are zeroed by virtio_scsi_init_req.
     * */

    VirtQueueElement elem;
    /* Set by dataplane code. */
    VirtIOSCSIVring *vring;

    union {
        /* Used for two-stage request submission */
        QTAILQ_ENTRY(VirtIOSCSIReq) next;

        /* Used for cancellation of request during TMFs */
        int remaining;
    };

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
        struct {
            VirtIOSCSICmdReq  cmd;
            uint8_t           cdb[];
        } QEMU_PACKED;
        VirtIOSCSICtrlTMFReq  tmf;
        VirtIOSCSICtrlANReq   an;
    } req;
} VirtIOSCSIReq;

QEMU_BUILD_BUG_ON(offsetof(VirtIOSCSIReq, req.cdb) !=
                  offsetof(VirtIOSCSIReq, req.cmd) + sizeof(VirtIOSCSICmdReq));

#define DEFINE_VIRTIO_SCSI_PROPERTIES(_state, _conf_field)                     \
    DEFINE_PROP_UINT32("num_queues", _state, _conf_field.num_queues, 1),       \
    DEFINE_PROP_UINT32("max_sectors", _state, _conf_field.max_sectors, 0xFFFF),\
    DEFINE_PROP_UINT32("cmd_per_lun", _state, _conf_field.cmd_per_lun, 128)

#define DEFINE_VIRTIO_SCSI_FEATURES(_state, _feature_field)                    \
    DEFINE_PROP_BIT("any_layout", _state, _feature_field,                      \
                    VIRTIO_F_ANY_LAYOUT, true),                                \
    DEFINE_PROP_BIT("hotplug", _state, _feature_field, VIRTIO_SCSI_F_HOTPLUG,  \
                                                       true),                  \
    DEFINE_PROP_BIT("param_change", _state, _feature_field,                    \
                                            VIRTIO_SCSI_F_CHANGE, true)

typedef void (*HandleOutput)(VirtIODevice *, VirtQueue *);

void virtio_scsi_common_realize(DeviceState *dev, Error **errp,
                                HandleOutput ctrl, HandleOutput evt,
                                HandleOutput cmd);

void virtio_scsi_common_unrealize(DeviceState *dev, Error **errp);
void virtio_scsi_handle_ctrl_req(VirtIOSCSI *s, VirtIOSCSIReq *req);
bool virtio_scsi_handle_cmd_req_prepare(VirtIOSCSI *s, VirtIOSCSIReq *req);
void virtio_scsi_handle_cmd_req_submit(VirtIOSCSI *s, VirtIOSCSIReq *req);
VirtIOSCSIReq *virtio_scsi_init_req(VirtIOSCSI *s, VirtQueue *vq);
void virtio_scsi_free_req(VirtIOSCSIReq *req);
void virtio_scsi_push_event(VirtIOSCSI *s, SCSIDevice *dev,
                            uint32_t event, uint32_t reason);

void virtio_scsi_set_iothread(VirtIOSCSI *s, IOThread *iothread);
void virtio_scsi_dataplane_start(VirtIOSCSI *s);
void virtio_scsi_dataplane_stop(VirtIOSCSI *s);
void virtio_scsi_vring_push_notify(VirtIOSCSIReq *req);
VirtIOSCSIReq *virtio_scsi_pop_req_vring(VirtIOSCSI *s,
                                         VirtIOSCSIVring *vring);

#endif /* _QEMU_VIRTIO_SCSI_H */
