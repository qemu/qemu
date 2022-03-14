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
#include "hw/virtio/virtio.h"
#include "standard-headers/linux/vhost_types.h"

typedef struct VhostVDPAHostNotifier {
    MemoryRegion mr;
    void *addr;
} VhostVDPAHostNotifier;

typedef struct vhost_vdpa {
    int device_fd;
    int index;
    uint32_t msg_type;
    bool iotlb_batch_begin_sent;
    MemoryListener listener;
    struct vhost_vdpa_iova_range iova_range;
    uint64_t acked_features;
    bool shadow_vqs_enabled;
    /* IOVA mapping used by the Shadow Virtqueue */
    VhostIOVATree *iova_tree;
    GPtrArray *shadow_vqs;
    struct vhost_dev *dev;
    VhostVDPAHostNotifier notifier[VIRTIO_QUEUE_MAX];
} VhostVDPA;

#endif
