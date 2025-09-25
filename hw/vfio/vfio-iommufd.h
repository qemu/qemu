/*
 * VFIO iommufd
 *
 * Copyright Red Hat, Inc. 2025
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_VFIO_VFIO_IOMMUFD_H
#define HW_VFIO_VFIO_IOMMUFD_H

#include "hw/vfio/vfio-container-base.h"

typedef struct VFIODevice VFIODevice;

typedef struct VFIOIOASHwpt {
    uint32_t hwpt_id;
    uint32_t hwpt_flags;
    QLIST_HEAD(, VFIODevice) device_list;
    QLIST_ENTRY(VFIOIOASHwpt) next;
} VFIOIOASHwpt;

typedef struct IOMMUFDBackend IOMMUFDBackend;

typedef struct VFIOIOMMUFDContainer {
    VFIOContainer bcontainer;
    IOMMUFDBackend *be;
    uint32_t ioas_id;
    QLIST_HEAD(, VFIOIOASHwpt) hwpt_list;
} VFIOIOMMUFDContainer;

OBJECT_DECLARE_SIMPLE_TYPE(VFIOIOMMUFDContainer, VFIO_IOMMU_IOMMUFD);

#endif /* HW_VFIO_VFIO_IOMMUFD_H */
