/*
 * Host IOMMU device abstract declaration
 *
 * Copyright (C) 2024 Intel Corporation.
 *
 * Authors: Zhenzhong Duan <zhenzhong.duan@intel.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#ifndef HOST_IOMMU_DEVICE_H
#define HOST_IOMMU_DEVICE_H

#include "qom/object.h"
#include "qapi/error.h"

#define TYPE_HOST_IOMMU_DEVICE "host-iommu-device"
OBJECT_DECLARE_TYPE(HostIOMMUDevice, HostIOMMUDeviceClass, HOST_IOMMU_DEVICE)

struct HostIOMMUDevice {
    Object parent_obj;

    char *name;
};

/**
 * struct HostIOMMUDeviceClass - The base class for all host IOMMU devices.
 *
 * Different types of host devices (e.g., VFIO or VDPA device) or devices
 * with different backend (e.g., VFIO legacy container or IOMMUFD backend)
 * will have different implementations of the HostIOMMUDeviceClass.
 */
struct HostIOMMUDeviceClass {
    ObjectClass parent_class;

    /**
     * @realize: initialize host IOMMU device instance further.
     *
     * Mandatory callback.
     *
     * @hiod: pointer to a host IOMMU device instance.
     *
     * @opaque: pointer to agent device of this host IOMMU device,
     *          e.g., VFIO base device or VDPA device.
     *
     * @errp: pass an Error out when realize fails.
     *
     * Returns: true on success, false on failure.
     */
    bool (*realize)(HostIOMMUDevice *hiod, void *opaque, Error **errp);
};
#endif
