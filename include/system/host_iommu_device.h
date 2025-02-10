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

/**
 * struct HostIOMMUDeviceCaps - Define host IOMMU device capabilities.
 *
 * @type: host platform IOMMU type.
 *
 * @hw_caps: host platform IOMMU capabilities (e.g. on IOMMUFD this represents
 *           the @out_capabilities value returned from IOMMU_GET_HW_INFO ioctl)
 */
typedef struct HostIOMMUDeviceCaps {
    uint32_t type;
    uint64_t hw_caps;
} HostIOMMUDeviceCaps;

#define TYPE_HOST_IOMMU_DEVICE "host-iommu-device"
OBJECT_DECLARE_TYPE(HostIOMMUDevice, HostIOMMUDeviceClass, HOST_IOMMU_DEVICE)

struct HostIOMMUDevice {
    Object parent_obj;

    char *name;
    void *agent; /* pointer to agent device, ie. VFIO or VDPA device */
    PCIBus *aliased_bus;
    int aliased_devfn;
    HostIOMMUDeviceCaps caps;
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
    /**
     * @get_cap: check if a host IOMMU device capability is supported.
     *
     * Optional callback, if not implemented, hint not supporting query
     * of @cap.
     *
     * @hiod: pointer to a host IOMMU device instance.
     *
     * @cap: capability to check.
     *
     * @errp: pass an Error out when fails to query capability.
     *
     * Returns: <0 on failure, 0 if a @cap is unsupported, or else
     * 1 or some positive value for some special @cap,
     * i.e., HOST_IOMMU_DEVICE_CAP_AW_BITS.
     */
    int (*get_cap)(HostIOMMUDevice *hiod, int cap, Error **errp);
    /**
     * @get_iova_ranges: Return the list of usable iova_ranges along with
     * @hiod Host IOMMU device
     *
     * @hiod: handle to the host IOMMU device
     */
    GList* (*get_iova_ranges)(HostIOMMUDevice *hiod);
    /**
     *
     * @get_page_size_mask: Return the page size mask supported along this
     * @hiod Host IOMMU device
     *
     * @hiod: handle to the host IOMMU device
     */
    uint64_t (*get_page_size_mask)(HostIOMMUDevice *hiod);
};

/*
 * Host IOMMU device capability list.
 */
#define HOST_IOMMU_DEVICE_CAP_IOMMU_TYPE        0
#define HOST_IOMMU_DEVICE_CAP_AW_BITS           1

#define HOST_IOMMU_DEVICE_CAP_AW_BITS_MAX       64
#endif
