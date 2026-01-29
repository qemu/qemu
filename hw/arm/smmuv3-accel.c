/*
 * Copyright (c) 2025 Huawei Technologies R & D (UK) Ltd
 * Copyright (C) 2025 NVIDIA
 * Written by Nicolin Chen, Shameer Kolothum
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "trace.h"

#include "hw/arm/smmuv3.h"
#include "hw/core/iommu.h"
#include "hw/pci/pci_bridge.h"
#include "hw/pci-host/gpex.h"
#include "hw/vfio/pci.h"

#include "smmuv3-internal.h"
#include "smmuv3-accel.h"

/*
 * The root region aliases the global system memory, and shared_as_sysmem
 * provides a shared Address Space referencing it. This Address Space is used
 * by all vfio-pci devices behind all accelerated SMMUv3 instances within a VM.
 */
static MemoryRegion root, sysmem;
static AddressSpace *shared_as_sysmem;

static SMMUv3AccelDevice *smmuv3_accel_get_dev(SMMUState *bs, SMMUPciBus *sbus,
                                               PCIBus *bus, int devfn)
{
    SMMUDevice *sdev = sbus->pbdev[devfn];
    SMMUv3AccelDevice *accel_dev;

    if (sdev) {
        return container_of(sdev, SMMUv3AccelDevice, sdev);
    }

    accel_dev = g_new0(SMMUv3AccelDevice, 1);
    sdev = &accel_dev->sdev;

    sbus->pbdev[devfn] = sdev;
    smmu_init_sdev(bs, sdev, bus, devfn);
    return accel_dev;
}

static uint32_t smmuv3_accel_gbpa_hwpt(SMMUv3State *s, SMMUv3AccelState *accel)
{
    return FIELD_EX32(s->gbpa, GBPA, ABORT) ?
           accel->abort_hwpt_id : accel->bypass_hwpt_id;
}

static bool
smmuv3_accel_alloc_viommu(SMMUv3State *s, HostIOMMUDeviceIOMMUFD *idev,
                          Error **errp)
{
    SMMUv3AccelState *accel = s->s_accel;
    struct iommu_hwpt_arm_smmuv3 bypass_data = {
        .ste = { SMMU_STE_CFG_BYPASS | SMMU_STE_VALID, 0x0ULL },
    };
    struct iommu_hwpt_arm_smmuv3 abort_data = {
        .ste = { SMMU_STE_VALID, 0x0ULL },
    };
    uint32_t s2_hwpt_id = idev->hwpt_id;
    uint32_t viommu_id, hwpt_id;
    IOMMUFDViommu *viommu;

    if (!iommufd_backend_alloc_viommu(idev->iommufd, idev->devid,
                                      IOMMU_VIOMMU_TYPE_ARM_SMMUV3,
                                      s2_hwpt_id, &viommu_id, errp)) {
        return false;
    }

    viommu = g_new0(IOMMUFDViommu, 1);
    viommu->viommu_id = viommu_id;
    viommu->s2_hwpt_id = s2_hwpt_id;
    viommu->iommufd = idev->iommufd;

    /*
     * Pre-allocate HWPTs for S1 bypass and abort cases. These will be attached
     * later for guest STEs or GBPAs that require bypass or abort configuration.
     */
    if (!iommufd_backend_alloc_hwpt(idev->iommufd, idev->devid, viommu_id,
                                    0, IOMMU_HWPT_DATA_ARM_SMMUV3,
                                    sizeof(abort_data), &abort_data,
                                    &accel->abort_hwpt_id, errp)) {
        goto free_viommu;
    }

    if (!iommufd_backend_alloc_hwpt(idev->iommufd, idev->devid, viommu_id,
                                    0, IOMMU_HWPT_DATA_ARM_SMMUV3,
                                    sizeof(bypass_data), &bypass_data,
                                    &accel->bypass_hwpt_id, errp)) {
        goto free_abort_hwpt;
    }

    /* Attach a HWPT based on SMMUv3 GBPA.ABORT value */
    hwpt_id = smmuv3_accel_gbpa_hwpt(s, accel);
    if (!host_iommu_device_iommufd_attach_hwpt(idev, hwpt_id, errp)) {
        goto free_bypass_hwpt;
    }
    accel->viommu = viommu;
    return true;

free_bypass_hwpt:
    iommufd_backend_free_id(idev->iommufd, accel->bypass_hwpt_id);
free_abort_hwpt:
    iommufd_backend_free_id(idev->iommufd, accel->abort_hwpt_id);
free_viommu:
    iommufd_backend_free_id(idev->iommufd, viommu->viommu_id);
    g_free(viommu);
    return false;
}

static bool smmuv3_accel_set_iommu_device(PCIBus *bus, void *opaque, int devfn,
                                          HostIOMMUDevice *hiod, Error **errp)
{
    HostIOMMUDeviceIOMMUFD *idev = HOST_IOMMU_DEVICE_IOMMUFD(hiod);
    SMMUState *bs = opaque;
    SMMUv3State *s = ARM_SMMUV3(bs);
    SMMUPciBus *sbus = smmu_get_sbus(bs, bus);
    SMMUv3AccelDevice *accel_dev = smmuv3_accel_get_dev(bs, sbus, bus, devfn);

    if (!idev) {
        return true;
    }

    if (accel_dev->idev) {
        if (accel_dev->idev != idev) {
            error_setg(errp, "Device already has an associated idev 0x%x",
                       idev->devid);
            return false;
        }
        return true;
    }

    if (s->s_accel->viommu) {
        goto done;
    }

    if (!smmuv3_accel_alloc_viommu(s, idev, errp)) {
        error_append_hint(errp, "Unable to alloc vIOMMU: idev devid 0x%x: ",
                          idev->devid);
        return false;
    }

done:
    accel_dev->idev = idev;
    accel_dev->s_accel = s->s_accel;
    QLIST_INSERT_HEAD(&s->s_accel->device_list, accel_dev, next);
    trace_smmuv3_accel_set_iommu_device(devfn, idev->devid);
    return true;
}

static void smmuv3_accel_unset_iommu_device(PCIBus *bus, void *opaque,
                                            int devfn)
{
    SMMUState *bs = opaque;
    SMMUPciBus *sbus = g_hash_table_lookup(bs->smmu_pcibus_by_busptr, bus);
    HostIOMMUDeviceIOMMUFD *idev;
    SMMUv3AccelDevice *accel_dev;
    SMMUv3AccelState *accel;
    SMMUDevice *sdev;

    if (!sbus) {
        return;
    }

    sdev = sbus->pbdev[devfn];
    if (!sdev) {
        return;
    }

    accel_dev = container_of(sdev, SMMUv3AccelDevice, sdev);
    idev = accel_dev->idev;
    accel = accel_dev->s_accel;
    /* Re-attach the default s2 hwpt id */
    if (!host_iommu_device_iommufd_attach_hwpt(idev, idev->hwpt_id, NULL)) {
        error_report("Unable to attach the default HW pagetable: idev devid "
                     "0x%x", idev->devid);
    }

    accel_dev->idev = NULL;
    accel_dev->s_accel = NULL;
    QLIST_REMOVE(accel_dev, next);
    trace_smmuv3_accel_unset_iommu_device(devfn, idev->devid);

    if (QLIST_EMPTY(&accel->device_list)) {
        iommufd_backend_free_id(accel->viommu->iommufd, accel->bypass_hwpt_id);
        iommufd_backend_free_id(accel->viommu->iommufd, accel->abort_hwpt_id);
        iommufd_backend_free_id(accel->viommu->iommufd,
                                accel->viommu->viommu_id);
        g_free(accel->viommu);
        accel->viommu = NULL;
    }
}

/*
 * Only allow PCIe bridges, pxb-pcie roots, and GPEX roots so vfio-pci
 * endpoints can sit downstream. Accelerated SMMUv3 requires a vfio-pci
 * endpoint using the iommufd backend; all other device types are rejected.
 * This avoids supporting emulated endpoints, which would complicate IOTLB
 * invalidation and hurt performance.
 */
static bool smmuv3_accel_pdev_allowed(PCIDevice *pdev, bool *vfio_pci)
{

    if (object_dynamic_cast(OBJECT(pdev), TYPE_PCI_BRIDGE) ||
        object_dynamic_cast(OBJECT(pdev), TYPE_PXB_PCIE_DEV) ||
        object_dynamic_cast(OBJECT(pdev), TYPE_GPEX_ROOT_DEVICE)) {
        return true;
    } else if ((object_dynamic_cast(OBJECT(pdev), TYPE_VFIO_PCI))) {
        *vfio_pci = true;
        if (object_property_get_link(OBJECT(pdev), "iommufd", NULL)) {
            return true;
        }
    }
    return false;
}

static bool smmuv3_accel_supports_as(PCIBus *bus, void *opaque, int devfn,
                                     Error **errp)
{
    PCIDevice *pdev = pci_find_device(bus, pci_bus_num(bus), devfn);
    bool vfio_pci = false;

    if (pdev && !smmuv3_accel_pdev_allowed(pdev, &vfio_pci)) {
        if (vfio_pci) {
            error_setg(errp, "vfio-pci endpoint devices without an iommufd "
                       "backend not allowed when using arm-smmuv3,accel=on");

        } else {
            error_setg(errp, "Emulated endpoint devices are not allowed when "
                       "using arm-smmuv3,accel=on");
        }
        return false;
    }
    return true;
}
/*
 * Find or add an address space for the given PCI device.
 *
 * If a device matching @bus and @devfn already exists, return its
 * corresponding address space. Otherwise, create a new device entry
 * and initialize address space for it.
 */
static AddressSpace *smmuv3_accel_find_add_as(PCIBus *bus, void *opaque,
                                              int devfn)
{
    PCIDevice *pdev = pci_find_device(bus, pci_bus_num(bus), devfn);
    SMMUState *bs = opaque;
    SMMUPciBus *sbus = smmu_get_sbus(bs, bus);
    SMMUv3AccelDevice *accel_dev = smmuv3_accel_get_dev(bs, sbus, bus, devfn);
    SMMUDevice *sdev = &accel_dev->sdev;
    bool vfio_pci = false;

    if (pdev && !smmuv3_accel_pdev_allowed(pdev, &vfio_pci)) {
        /* Should never be here: supports_address_space() filters these out */
        g_assert_not_reached();
    }

    /*
     * In the accelerated mode, a vfio-pci device attached via the iommufd
     * backend must remain in the system address space. Such a device is
     * always translated by its physical SMMU (using either a stage-2-only
     * STE or a nested STE), where the parent stage-2 page table is allocated
     * by the VFIO core to back the system address space.
     *
     * Return the shared_as_sysmem aliased to the global system memory in this
     * case. Sharing address_space_memory also allows devices under different
     * vSMMU instances in the same VM to reuse a single nesting parent HWPT in
     * the VFIO core.
     *
     * For non-endpoint emulated devices such as PCIe root ports and bridges,
     * which may use the normal emulated translation path and software IOTLBs,
     * return the SMMU's IOMMU address space.
     */
    if (vfio_pci) {
        return shared_as_sysmem;
    } else {
        return &sdev->as;
    }
}

static uint64_t smmuv3_accel_get_viommu_flags(void *opaque)
{
    /*
     * We return VIOMMU_FLAG_WANT_NESTING_PARENT to inform VFIO core to create a
     * nesting parent which is required for accelerated SMMUv3 support.
     * The real HW nested support should be reported from host SMMUv3 and if
     * it doesn't, the nesting parent allocation will fail anyway in VFIO core.
     */
    return VIOMMU_FLAG_WANT_NESTING_PARENT;
}

static const PCIIOMMUOps smmuv3_accel_ops = {
    .supports_address_space = smmuv3_accel_supports_as,
    .get_address_space = smmuv3_accel_find_add_as,
    .get_viommu_flags = smmuv3_accel_get_viommu_flags,
    .set_iommu_device = smmuv3_accel_set_iommu_device,
    .unset_iommu_device = smmuv3_accel_unset_iommu_device,
};

static void smmuv3_accel_as_init(SMMUv3State *s)
{

    if (shared_as_sysmem) {
        return;
    }

    memory_region_init(&root, OBJECT(s), "root", UINT64_MAX);
    memory_region_init_alias(&sysmem, OBJECT(s), "smmuv3-accel-sysmem",
                             get_system_memory(), 0,
                             memory_region_size(get_system_memory()));
    memory_region_add_subregion(&root, 0, &sysmem);

    shared_as_sysmem = g_new0(AddressSpace, 1);
    address_space_init(shared_as_sysmem, &root, "smmuv3-accel-as-sysmem");
}

void smmuv3_accel_init(SMMUv3State *s)
{
    SMMUState *bs = ARM_SMMU(s);

    s->s_accel = g_new0(SMMUv3AccelState, 1);
    bs->iommu_ops = &smmuv3_accel_ops;
    smmuv3_accel_as_init(s);
}
