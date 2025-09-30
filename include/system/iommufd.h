/*
 * iommufd container backend declaration
 *
 * Copyright (C) 2024 Intel Corporation.
 * Copyright Red Hat, Inc. 2024
 *
 * Authors: Yi Liu <yi.l.liu@intel.com>
 *          Eric Auger <eric.auger@redhat.com>
 *          Zhenzhong Duan <zhenzhong.duan@intel.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef SYSTEM_IOMMUFD_H
#define SYSTEM_IOMMUFD_H

#include "qom/object.h"
#include "exec/hwaddr.h"
#include "exec/cpu-common.h"
#include "system/host_iommu_device.h"

#define TYPE_IOMMUFD_BACKEND "iommufd"
OBJECT_DECLARE_TYPE(IOMMUFDBackend, IOMMUFDBackendClass, IOMMUFD_BACKEND)

struct IOMMUFDBackendClass {
    ObjectClass parent_class;
};

struct IOMMUFDBackend {
    Object parent;

    /*< protected >*/
    int fd;            /* /dev/iommu file descriptor */
    bool owned;        /* is the /dev/iommu opened internally */
    Error *cpr_blocker;/* set if be does not support CPR */
    uint32_t users;

    /*< public >*/
};

bool iommufd_backend_connect(IOMMUFDBackend *be, Error **errp);
void iommufd_backend_disconnect(IOMMUFDBackend *be);

bool iommufd_backend_alloc_ioas(IOMMUFDBackend *be, uint32_t *ioas_id,
                                Error **errp);
void iommufd_backend_free_id(IOMMUFDBackend *be, uint32_t id);
int iommufd_backend_map_file_dma(IOMMUFDBackend *be, uint32_t ioas_id,
                                 hwaddr iova, uint64_t size, int fd,
                                 unsigned long start, bool readonly);
int iommufd_backend_map_dma(IOMMUFDBackend *be, uint32_t ioas_id, hwaddr iova,
                            uint64_t size, void *vaddr, bool readonly);
int iommufd_backend_unmap_dma(IOMMUFDBackend *be, uint32_t ioas_id,
                              hwaddr iova, uint64_t size);
bool iommufd_backend_get_device_info(IOMMUFDBackend *be, uint32_t devid,
                                     uint32_t *type, void *data, uint32_t len,
                                     uint64_t *caps, Error **errp);
bool iommufd_backend_alloc_hwpt(IOMMUFDBackend *be, uint32_t dev_id,
                                uint32_t pt_id, uint32_t flags,
                                uint32_t data_type, uint32_t data_len,
                                void *data_ptr, uint32_t *out_hwpt,
                                Error **errp);
bool iommufd_backend_set_dirty_tracking(IOMMUFDBackend *be, uint32_t hwpt_id,
                                        bool start, Error **errp);
bool iommufd_backend_get_dirty_bitmap(IOMMUFDBackend *be, uint32_t hwpt_id,
                                      uint64_t iova, ram_addr_t size,
                                      uint64_t page_size, uint64_t *data,
                                      Error **errp);
bool iommufd_backend_invalidate_cache(IOMMUFDBackend *be, uint32_t id,
                                      uint32_t data_type, uint32_t entry_len,
                                      uint32_t *entry_num, void *data,
                                      Error **errp);

bool iommufd_change_process_capable(IOMMUFDBackend *be);
bool iommufd_change_process(IOMMUFDBackend *be, Error **errp);

#define TYPE_HOST_IOMMU_DEVICE_IOMMUFD TYPE_HOST_IOMMU_DEVICE "-iommufd"
OBJECT_DECLARE_TYPE(HostIOMMUDeviceIOMMUFD, HostIOMMUDeviceIOMMUFDClass,
                    HOST_IOMMU_DEVICE_IOMMUFD)

/* Overload of the host IOMMU device for the iommufd backend */
struct HostIOMMUDeviceIOMMUFD {
    HostIOMMUDevice parent_obj;

    IOMMUFDBackend *iommufd;
    uint32_t devid;
    uint32_t hwpt_id;
};

struct HostIOMMUDeviceIOMMUFDClass {
    HostIOMMUDeviceClass parent_class;

    /**
     * @attach_hwpt: attach host IOMMU device to IOMMUFD hardware page table.
     * VFIO and VDPA device can have different implementation.
     *
     * Mandatory callback.
     *
     * @idev: host IOMMU device backed by IOMMUFD backend.
     *
     * @hwpt_id: ID of IOMMUFD hardware page table.
     *
     * @errp: pass an Error out when attachment fails.
     *
     * Returns: true on success, false on failure.
     */
    bool (*attach_hwpt)(HostIOMMUDeviceIOMMUFD *idev, uint32_t hwpt_id,
                        Error **errp);
    /**
     * @detach_hwpt: detach host IOMMU device from IOMMUFD hardware page table.
     * VFIO and VDPA device can have different implementation.
     *
     * Mandatory callback.
     *
     * @idev: host IOMMU device backed by IOMMUFD backend.
     *
     * @errp: pass an Error out when attachment fails.
     *
     * Returns: true on success, false on failure.
     */
    bool (*detach_hwpt)(HostIOMMUDeviceIOMMUFD *idev, Error **errp);
};

bool host_iommu_device_iommufd_attach_hwpt(HostIOMMUDeviceIOMMUFD *idev,
                                           uint32_t hwpt_id, Error **errp);
bool host_iommu_device_iommufd_detach_hwpt(HostIOMMUDeviceIOMMUFD *idev,
                                           Error **errp);
#endif
