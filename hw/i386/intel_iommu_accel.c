/*
 * Intel IOMMU acceleration with nested translation
 *
 * Copyright (C) 2026 Intel Corporation.
 *
 * Authors: Zhenzhong Duan <zhenzhong.duan@intel.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "system/iommufd.h"
#include "intel_iommu_internal.h"
#include "intel_iommu_accel.h"
#include "hw/pci/pci_bus.h"

bool vtd_check_hiod_accel(IntelIOMMUState *s, VTDHostIOMMUDevice *vtd_hiod,
                          Error **errp)
{
    HostIOMMUDevice *hiod = vtd_hiod->hiod;
    struct HostIOMMUDeviceCaps *caps = &hiod->caps;
    struct iommu_hw_info_vtd *vtd = &caps->vendor_caps.vtd;
    PCIBus *bus = vtd_hiod->bus;
    PCIDevice *pdev = bus->devices[vtd_hiod->devfn];

    if (!object_dynamic_cast(OBJECT(hiod), TYPE_HOST_IOMMU_DEVICE_IOMMUFD)) {
        error_setg(errp, "Need IOMMUFD backend when x-flts=on");
        return false;
    }

    if (caps->type != IOMMU_HW_INFO_TYPE_INTEL_VTD) {
        error_setg(errp, "Incompatible host platform IOMMU type %d",
                   caps->type);
        return false;
    }

    if (s->fs1gp && !(vtd->cap_reg & VTD_CAP_FS1GP)) {
        error_setg(errp,
                   "First stage 1GB large page is unsupported by host IOMMU");
        return false;
    }

    if (pci_device_get_iommu_bus_devfn(pdev, &bus, NULL, NULL)) {
        error_setg(errp, "Host device downstream to a PCI bridge is "
                   "unsupported when x-flts=on");
        return false;
    }

    error_setg(errp,
               "host IOMMU is incompatible with guest first stage translation");
    return false;
}
