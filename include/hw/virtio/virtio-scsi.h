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

#define TYPE_VIRTIO_SCSI "virtio-scsi"
#define VIRTIO_SCSI(obj) \
        OBJECT_CHECK(VirtIOSCSI, (obj), TYPE_VIRTIO_SCSI)


/* The ID for virtio_scsi */
#define VIRTIO_ID_SCSI  8

/* Feature Bits */
#define VIRTIO_SCSI_F_INOUT                    0
#define VIRTIO_SCSI_F_HOTPLUG                  1
#define VIRTIO_SCSI_F_CHANGE                   2

struct VirtIOSCSIConf {
    uint32_t num_queues;
    uint32_t max_sectors;
    uint32_t cmd_per_lun;
};

typedef struct VirtIOSCSI {
    VirtIODevice parent_obj;
    VirtIOSCSIConf conf;

    SCSIBus bus;
    uint32_t sense_size;
    uint32_t cdb_size;
    int resetting;
    bool events_dropped;
    VirtQueue *ctrl_vq;
    VirtQueue *event_vq;
    VirtQueue **cmd_vqs;
} VirtIOSCSI;

#define DEFINE_VIRTIO_SCSI_PROPERTIES(_state, _conf_field)                     \
    DEFINE_PROP_UINT32("num_queues", _state, _conf_field.num_queues, 1),       \
    DEFINE_PROP_UINT32("max_sectors", _state, _conf_field.max_sectors, 0xFFFF),\
    DEFINE_PROP_UINT32("cmd_per_lun", _state, _conf_field.cmd_per_lun, 128)

#define DEFINE_VIRTIO_SCSI_FEATURES(_state, _feature_field)                    \
    DEFINE_VIRTIO_COMMON_FEATURES(_state, _feature_field),                     \
    DEFINE_PROP_BIT("hotplug", _state, _feature_field, VIRTIO_SCSI_F_HOTPLUG,  \
                                                       true),                  \
    DEFINE_PROP_BIT("param_change", _state, _feature_field,                    \
                                            VIRTIO_SCSI_F_CHANGE, true)

#endif /* _QEMU_VIRTIO_SCSI_H */
