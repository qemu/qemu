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

#include "hw/virtio/virtio.h"

typedef struct vhost_vdpa {
    int device_fd;
    uint32_t msg_type;
    MemoryListener listener;
} VhostVDPA;

extern AddressSpace address_space_memory;
extern int vhost_vdpa_get_device_id(struct vhost_dev *dev,
                                   uint32_t *device_id);
#endif
