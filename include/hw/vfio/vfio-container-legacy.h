/*
 * VFIO container
 *
 * Copyright Red Hat, Inc. 2025
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_VFIO_CONTAINER_LEGACY_H
#define HW_VFIO_CONTAINER_LEGACY_H

#include "hw/vfio/vfio-container.h"
#include "hw/vfio/vfio-cpr.h"

typedef struct VFIOLegacyContainer VFIOLegacyContainer;
typedef struct VFIODevice VFIODevice;

typedef struct VFIOGroup {
    int fd;
    int groupid;
    VFIOLegacyContainer *container;
    QLIST_HEAD(, VFIODevice) device_list;
    QLIST_ENTRY(VFIOGroup) next;
    QLIST_ENTRY(VFIOGroup) container_next;
    bool ram_block_discard_allowed;
} VFIOGroup;

struct VFIOLegacyContainer {
    VFIOContainer parent_obj;

    int fd; /* /dev/vfio/vfio, empowered by the attached groups */
    unsigned iommu_type;
    QLIST_HEAD(, VFIOGroup) group_list;
    VFIOContainerCPR cpr;
};

OBJECT_DECLARE_SIMPLE_TYPE(VFIOLegacyContainer, VFIO_IOMMU_LEGACY);

#endif /* HW_VFIO_CONTAINER_LEGACY_H */
