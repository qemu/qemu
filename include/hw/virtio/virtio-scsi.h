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

#ifndef QEMU_VIRTIO_SCSI_H
#define QEMU_VIRTIO_SCSI_H
#include "qom/object.h"

/* Override CDB/sense data size: they are dynamic (guest controlled) in QEMU */
#define VIRTIO_SCSI_CDB_SIZE 0
#define VIRTIO_SCSI_SENSE_SIZE 0
#include "standard-headers/linux/virtio_scsi.h"
#include "hw/virtio/virtio.h"
#include "hw/scsi/scsi.h"
#include "chardev/char-fe.h"
#include "qapi/qapi-types-virtio.h"
#include "system/iothread.h"

#define TYPE_VIRTIO_SCSI_COMMON "virtio-scsi-common"
OBJECT_DECLARE_SIMPLE_TYPE(VirtIOSCSICommon, VIRTIO_SCSI_COMMON)

#define TYPE_VIRTIO_SCSI "virtio-scsi-device"
OBJECT_DECLARE_SIMPLE_TYPE(VirtIOSCSI, VIRTIO_SCSI)

#define VIRTIO_SCSI_MAX_CHANNEL 0
#define VIRTIO_SCSI_MAX_TARGET  255
#define VIRTIO_SCSI_MAX_LUN     16383

/* Number of virtqueues that are always present */
#define VIRTIO_SCSI_VQ_NUM_FIXED    2

#define VIRTIO_SCSI_AUTO_NUM_QUEUES UINT32_MAX

typedef struct virtio_scsi_cmd_req VirtIOSCSICmdReq;
typedef struct virtio_scsi_cmd_resp VirtIOSCSICmdResp;
typedef struct virtio_scsi_ctrl_tmf_req VirtIOSCSICtrlTMFReq;
typedef struct virtio_scsi_ctrl_tmf_resp VirtIOSCSICtrlTMFResp;
typedef struct virtio_scsi_ctrl_an_req VirtIOSCSICtrlANReq;
typedef struct virtio_scsi_ctrl_an_resp VirtIOSCSICtrlANResp;
typedef struct virtio_scsi_event VirtIOSCSIEvent;
typedef struct virtio_scsi_config VirtIOSCSIConfig;

struct VirtIOSCSIConf {
    uint32_t num_queues;
    uint32_t virtqueue_size;
    bool worker_per_virtqueue;
    bool seg_max_adjust;
    uint32_t max_sectors;
    uint32_t cmd_per_lun;
    char *vhostfd;
    char *wwpn;
    CharFrontend chardev;
    uint32_t boot_tpgt;
    IOThread *iothread;
    IOThreadVirtQueueMappingList *iothread_vq_mapping_list;
};

struct VirtIOSCSI;

struct VirtIOSCSICommon {
    VirtIODevice parent_obj;
    VirtIOSCSIConf conf;

    uint32_t sense_size;
    uint32_t cdb_size;
    VirtQueue *ctrl_vq;
    VirtQueue *event_vq;
    VirtQueue **cmd_vqs;
};

struct VirtIOSCSIReq;

struct VirtIOSCSI {
    VirtIOSCSICommon parent_obj;

    SCSIBus bus;
    int resetting; /* written from main loop thread, read from any thread */

    QemuMutex event_lock; /* protects event_vq and events_dropped */
    bool events_dropped;

    QemuMutex ctrl_lock; /* protects ctrl_vq */

    /* Fields for dataplane below */
    AioContext **vq_aio_context; /* per-virtqueue AioContext pointer */

    bool dataplane_started;
    bool dataplane_starting;
    bool dataplane_stopping;
    bool dataplane_fenced;
    uint32_t host_features;
};

void virtio_scsi_common_realize(DeviceState *dev,
                                VirtIOHandleOutput ctrl,
                                VirtIOHandleOutput evt,
                                VirtIOHandleOutput cmd,
                                Error **errp);

void virtio_scsi_common_unrealize(DeviceState *dev);

void virtio_scsi_dataplane_setup(VirtIOSCSI *s, Error **errp);
void virtio_scsi_dataplane_cleanup(VirtIOSCSI *s);
int virtio_scsi_dataplane_start(VirtIODevice *s);
void virtio_scsi_dataplane_stop(VirtIODevice *s);

#endif /* QEMU_VIRTIO_SCSI_H */
