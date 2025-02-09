/*
 * Host IOMMU device abstract
 *
 * Copyright (C) 2024 Intel Corporation.
 *
 * Authors: Zhenzhong Duan <zhenzhong.duan@intel.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "system/host_iommu_device.h"

OBJECT_DEFINE_ABSTRACT_TYPE(HostIOMMUDevice,
                            host_iommu_device,
                            HOST_IOMMU_DEVICE,
                            OBJECT)

static void host_iommu_device_class_init(ObjectClass *oc, const void *data)
{
}

static void host_iommu_device_init(Object *obj)
{
}

static void host_iommu_device_finalize(Object *obj)
{
    HostIOMMUDevice *hiod = HOST_IOMMU_DEVICE(obj);

    g_free(hiod->name);
}
