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

typedef enum SVQTransitionState {
    SVQ_TSTATE_DISABLING = -1,
    SVQ_TSTATE_DONE,
    SVQ_TSTATE_ENABLING
} SVQTransitionState;

/* Info shared by all vhost_vdpa device models */
typedef struct vhost_vdpa_shared {
    int device_fd;
    MemoryListener listener;
    struct vhost_vdpa_iova_range iova_range;
    QLIST_HEAD(, vdpa_iommu) iommu_list;

    /* IOVA mapping used by the Shadow Virtqueue */
    VhostIOVATree *iova_tree;

    /* Copy of backend features */
    uint64_t backend_cap;

    bool iotlb_batch_begin_sent;

    /* Vdpa must send shadow addresses as IOTLB key for data queues, not GPA */
    bool shadow_data;

    /* SVQ switching is in progress, or already completed? */
    SVQTransitionState svq_switching;
} VhostVDPAShared;

typedef struct vhost_vdpa {
    int index;
    uint32_t address_space_id;
    uint64_t acked_features;
    bool shadow_vqs_enabled;
    /* Device suspended successfully */
    bool suspended;
    VhostVDPAShared *shared;
    GPtrArray *shadow_vqs;
    const VhostShadowVirtqueueOps *shadow_vq_ops;
    void *shadow_vq_ops_opaque;
    struct vhost_dev *dev;
    Error *migration_blocker;
    VhostVDPAHostNotifier notifier[VIRTIO_QUEUE_MAX];
    IOMMUNotifier n;
} VhostVDPA;

int vhost_vdpa_get_iova_range(int fd, struct vhost_vdpa_iova_range *iova_range);
int vhost_vdpa_set_vring_ready(struct vhost_vdpa *v, unsigned idx);

int vhost_vdpa_dma_map(VhostVDPAShared *s, uint32_t asid, hwaddr iova,
                       hwaddr size, void *vaddr, bool readonly);
int vhost_vdpa_dma_unmap(VhostVDPAShared *s, uint32_t asid, hwaddr iova,
                         hwaddr size);

typedef struct vdpa_iommu {
    VhostVDPAShared *dev_shared;
    IOMMUMemoryRegion *iommu_mr;
    hwaddr iommu_offset;
    IOMMUNotifier n;
    QLIST_ENTRY(vdpa_iommu) iommu_next;
} VDPAIOMMUState;


#endif
