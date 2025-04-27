/*
 * VFIO container
 *
 * Copyright Red Hat, Inc. 2025
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_VFIO_CONTAINER_H
#define HW_VFIO_CONTAINER_H

#include "hw/vfio/vfio-container-base.h"

typedef struct VFIOContainer VFIOContainer;
typedef struct VFIODevice VFIODevice;

typedef struct VFIOGroup {
    int fd;
    int groupid;
    VFIOContainer *container;
    QLIST_HEAD(, VFIODevice) device_list;
    QLIST_ENTRY(VFIOGroup) next;
    QLIST_ENTRY(VFIOGroup) container_next;
    bool ram_block_discard_allowed;
} VFIOGroup;

typedef struct VFIOContainer {
    VFIOContainerBase bcontainer;
    int fd; /* /dev/vfio/vfio, empowered by the attached groups */
    unsigned iommu_type;
    QLIST_HEAD(, VFIOGroup) group_list;
} VFIOContainer;

OBJECT_DECLARE_SIMPLE_TYPE(VFIOContainer, VFIO_IOMMU_LEGACY);

#endif /* HW_VFIO_CONTAINER_H */
