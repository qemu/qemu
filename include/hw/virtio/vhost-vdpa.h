/*
 * vhost-vdpa.h
 *
 * Copyright(c) 2017-2018 Intel Corporation.
 * Copyright(c) 2020 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef HW_VIRTIO_VHOST_VDPA_H
#define HW_VIRTIO_VHOST_VDPA_H

#include <gmodule.h>

#include "hw/virtio/vhost-iova-tree.h"
#include "hw/virtio/vhost-shadow-virtqueue.h"
#include "hw/virtio/virtio.h"
#include "standard-headers/linux/vhost_types.h"

/*
 * ASID dedicated to map guest's addresses.  If SVQ is disabled it maps GPA to
 * qemu's IOVA.  If SVQ is enabled it maps also the SVQ vring here
 */
#define VHOST_VDPA_GUEST_PA_ASID 0

typedef struct VhostVDPAHostNotifier {
    MemoryRegion mr;
    void *addr;
} VhostVDPAHostNotifier;

typedef struct vhost_vdpa {
    int device_fd;
    int index;
    uint32_t msg_type;
    bool iotlb_batch_begin_sent;
    uint32_t address_space_id;
    MemoryListener listener;
    struct vhost_vdpa_iova_range iova_range;
    uint64_t acked_features;
    bool shadow_vqs_enabled;
    /* Vdpa must send shadow addresses as IOTLB key for data queues, not GPA */
    bool shadow_data;
    /* Device suspended successfully */
    bool suspended;
    /* IOVA mapping used by the Shadow Virtqueue */
    VhostIOVATree *iova_tree;
    GPtrArray *shadow_vqs;
    const VhostShadowVirtqueueOps *shadow_vq_ops;
    void *shadow_vq_ops_opaque;
    struct vhost_dev *dev;
    Error *migration_blocker;
    VhostVDPAHostNotifier notifier[VIRTIO_QUEUE_MAX];
} VhostVDPA;

int vhost_vdpa_get_iova_range(int fd, struct vhost_vdpa_iova_range *iova_range);

int vhost_vdpa_dma_map(struct vhost_vdpa *v, uint32_t asid, hwaddr iova,
                       hwaddr size, void *vaddr, bool readonly);
int vhost_vdpa_dma_unmap(struct vhost_vdpa *v, uint32_t asid, hwaddr iova,
                         hwaddr size);

#endif
