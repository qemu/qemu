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

/* Override CDB/sense data size: they are dynamic (guest controlled) in QEMU */
#define VIRTIO_SCSI_CDB_SIZE 0
#define VIRTIO_SCSI_SENSE_SIZE 0
#include "standard-headers/linux/virtio_scsi.h"
#include "hw/virtio/virtio.h"
#include "hw/pci/pci.h"
#include "hw/scsi/scsi.h"
#include "chardev/char-fe.h"
#include "sysemu/iothread.h"

#define TYPE_VIRTIO_SCSI_COMMON "virtio-scsi-common"
#define VIRTIO_SCSI_COMMON(obj) \
        OBJECT_CHECK(VirtIOSCSICommon, (obj), TYPE_VIRTIO_SCSI_COMMON)

#define TYPE_VIRTIO_SCSI "virtio-scsi-device"
#define VIRTIO_SCSI(obj) \
        OBJECT_CHECK(VirtIOSCSI, (obj), TYPE_VIRTIO_SCSI)

#define VIRTIO_SCSI_MAX_CHANNEL 0
#define VIRTIO_SCSI_MAX_TARGET  255
#define VIRTIO_SCSI_MAX_LUN     16383

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
    uint32_t max_sectors;
    uint32_t cmd_per_lun;
#ifdef CONFIG_VHOST_SCSI
    char *vhostfd;
    char *wwpn;
#endif
    CharBackend chardev;
    uint32_t boot_tpgt;
    IOThread *iothread;
};

struct VirtIOSCSI;

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

    bool dataplane_started;
    bool dataplane_starting;
    bool dataplane_stopping;
    bool dataplane_fenced;
    uint32_t host_features;
} VirtIOSCSI;

typedef struct VirtIOSCSIReq {
    /* Note:
     * - fields up to resp_iov are initialized by virtio_scsi_init_req;
     * - fields starting at vring are zeroed by virtio_scsi_init_req.
     * */
    VirtQueueElement elem;

    VirtIOSCSI *dev;
    VirtQueue *vq;
    QEMUSGList qsgl;
    QEMUIOVector resp_iov;

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
        VirtIOSCSICmdReq      cmd;
        VirtIOSCSICtrlTMFReq  tmf;
        VirtIOSCSICtrlANReq   an;
    } req;
} VirtIOSCSIReq;

static inline void virtio_scsi_acquire(VirtIOSCSI *s)
{
    if (s->ctx) {
        aio_context_acquire(s->ctx);
    }
}

static inline void virtio_scsi_release(VirtIOSCSI *s)
{
    if (s->ctx) {
        aio_context_release(s->ctx);
    }
}

void virtio_scsi_common_realize(DeviceState *dev,
                                VirtIOHandleOutput ctrl,
                                VirtIOHandleOutput evt,
                                VirtIOHandleOutput cmd,
                                Error **errp);

void virtio_scsi_common_unrealize(DeviceState *dev, Error **errp);
bool virtio_scsi_handle_event_vq(VirtIOSCSI *s, VirtQueue *vq);
bool virtio_scsi_handle_cmd_vq(VirtIOSCSI *s, VirtQueue *vq);
bool virtio_scsi_handle_ctrl_vq(VirtIOSCSI *s, VirtQueue *vq);
void virtio_scsi_init_req(VirtIOSCSI *s, VirtQueue *vq, VirtIOSCSIReq *req);
void virtio_scsi_free_req(VirtIOSCSIReq *req);
void virtio_scsi_push_event(VirtIOSCSI *s, SCSIDevice *dev,
                            uint32_t event, uint32_t reason);

void virtio_scsi_dataplane_setup(VirtIOSCSI *s, Error **errp);
int virtio_scsi_dataplane_start(VirtIODevice *s);
void virtio_scsi_dataplane_stop(VirtIODevice *s);

#endif /* QEMU_VIRTIO_SCSI_H */
