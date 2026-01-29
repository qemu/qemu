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

static int smmuv3_oas_bits(uint32_t oas)
{
    static const int map[] = { 32, 36, 40, 42, 44, 48, 52, 56 };

    g_assert(oas < ARRAY_SIZE(map));
    return map[oas];
}

static bool
smmuv3_accel_check_hw_compatible(SMMUv3State *s,
                                 struct iommu_hw_info_arm_smmuv3 *info,
                                 Error **errp)
{
    /* QEMU SMMUv3 supports both linear and 2-level stream tables */
    if (FIELD_EX32(info->idr[0], IDR0, STLEVEL) !=
                FIELD_EX32(s->idr[0], IDR0, STLEVEL)) {
        error_setg(errp, "Host SMMUv3 Stream Table format mismatch "
                   "(host STLEVEL=%u, QEMU STLEVEL=%u)",
                   FIELD_EX32(info->idr[0], IDR0, STLEVEL),
                   FIELD_EX32(s->idr[0], IDR0, STLEVEL));
        return false;
    }

    /* QEMU SMMUv3 supports only little-endian translation table walks */
    if (FIELD_EX32(info->idr[0], IDR0, TTENDIAN) >
                FIELD_EX32(s->idr[0], IDR0, TTENDIAN)) {
        error_setg(errp, "Host SMMUv3 doesn't support Little-endian "
                   "translation table");
        return false;
    }

    /* QEMU SMMUv3 supports only AArch64 translation table format */
    if (FIELD_EX32(info->idr[0], IDR0, TTF) <
                FIELD_EX32(s->idr[0], IDR0, TTF)) {
        error_setg(errp, "Host SMMUv3 doesn't support AArch64 translation "
                   "table format");
        return false;
    }

    /* QEMU SMMUv3 supports SIDSIZE 16 */
    if (FIELD_EX32(info->idr[1], IDR1, SIDSIZE) <
                FIELD_EX32(s->idr[1], IDR1, SIDSIZE)) {
        error_setg(errp, "Host SMMUv3 SIDSIZE not compatible "
                   "(host=%u, QEMU=%u)",
                   FIELD_EX32(info->idr[1], IDR1, SIDSIZE),
                   FIELD_EX32(s->idr[1], IDR1, SIDSIZE));
        return false;
    }

    /* Check SSIDSIZE value opted-in is compatible with Host SMMUv3 SSIDSIZE */
    if (FIELD_EX32(info->idr[1], IDR1, SSIDSIZE) <
                FIELD_EX32(s->idr[1], IDR1, SSIDSIZE)) {
        error_setg(errp, "Host SMMUv3 SSIDSIZE not compatible "
                   "(host=%u, QEMU=%u)",
                   FIELD_EX32(info->idr[1], IDR1, SSIDSIZE),
                   FIELD_EX32(s->idr[1], IDR1, SSIDSIZE));
        return false;
    }

    /* User can disable QEMU SMMUv3 Range Invalidation support */
    if (FIELD_EX32(info->idr[3], IDR3, RIL) <
                FIELD_EX32(s->idr[3], IDR3, RIL)) {
        error_setg(errp, "Host SMMUv3 doesn't support Range Invalidation");
        return false;
    }
    /* Check OAS value opted is compatible with Host SMMUv3 IPA */
    if (FIELD_EX32(info->idr[5], IDR5, OAS) <
                FIELD_EX32(s->idr[5], IDR5, OAS)) {
        error_setg(errp, "Host SMMUv3 supports only %d-bit IPA, but the vSMMU "
                   "OAS implies %d-bit IPA",
                   smmuv3_oas_bits(FIELD_EX32(info->idr[5], IDR5, OAS)),
                   smmuv3_oas_bits(FIELD_EX32(s->idr[5], IDR5, OAS)));
        return false;
    }

    /* QEMU SMMUv3 supports GRAN4K/GRAN16K/GRAN64K translation granules */
    if (FIELD_EX32(info->idr[5], IDR5, GRAN4K) !=
                FIELD_EX32(s->idr[5], IDR5, GRAN4K)) {
        error_setg(errp, "Host SMMUv3 doesn't support 4K translation granule");
        return false;
    }
    if (FIELD_EX32(info->idr[5], IDR5, GRAN16K) !=
                FIELD_EX32(s->idr[5], IDR5, GRAN16K)) {
        error_setg(errp, "Host SMMUv3 doesn't support 16K translation granule");
        return false;
    }
    if (FIELD_EX32(info->idr[5], IDR5, GRAN64K) !=
                FIELD_EX32(s->idr[5], IDR5, GRAN64K)) {
        error_setg(errp, "Host SMMUv3 doesn't support 64K translation granule");
        return false;
    }

    return true;
}

static bool
smmuv3_accel_hw_compatible(SMMUv3State *s, HostIOMMUDeviceIOMMUFD *idev,
                           Error **errp)
{
    struct iommu_hw_info_arm_smmuv3 info;
    uint32_t data_type;
    uint64_t caps;

    if (!iommufd_backend_get_device_info(idev->iommufd, idev->devid, &data_type,
                                         &info, sizeof(info), &caps, NULL,
                                         errp)) {
        return false;
    }

    if (data_type != IOMMU_HW_INFO_TYPE_ARM_SMMUV3) {
        error_setg(errp, "Wrong data type (%d) for Host SMMUv3 device info",
                     data_type);
        return false;
    }

    if (!smmuv3_accel_check_hw_compatible(s, &info, errp)) {
        return false;
    }
    return true;
}

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
smmuv3_accel_alloc_vdev(SMMUv3AccelDevice *accel_dev, int sid, Error **errp)
{
    SMMUv3AccelState *accel = accel_dev->s_accel;
    HostIOMMUDeviceIOMMUFD *idev = accel_dev->idev;
    IOMMUFDVdev *vdev = accel_dev->vdev;
    uint32_t vdevice_id;

    if (!idev || vdev) {
        return true;
    }

    if (!iommufd_backend_alloc_vdev(idev->iommufd, idev->devid,
                                    accel->viommu->viommu_id, sid,
                                    &vdevice_id, errp)) {
            return false;
    }

    vdev = g_new(IOMMUFDVdev, 1);
    vdev->vdevice_id = vdevice_id;
    vdev->virt_id = sid;
    accel_dev->vdev = vdev;
    return true;
}

static SMMUS1Hwpt *
smmuv3_accel_dev_alloc_translate(SMMUv3AccelDevice *accel_dev, STE *ste,
                                 Error **errp)
{
    uint64_t ste_0 = (uint64_t)ste->word[0] | (uint64_t)ste->word[1] << 32;
    uint64_t ste_1 = (uint64_t)ste->word[2] | (uint64_t)ste->word[3] << 32;
    HostIOMMUDeviceIOMMUFD *idev = accel_dev->idev;
    SMMUv3AccelState *accel = accel_dev->s_accel;
    struct iommu_hwpt_arm_smmuv3 nested_data = {
        .ste = {
            cpu_to_le64(ste_0 & STE0_MASK),
            cpu_to_le64(ste_1 & STE1_MASK),
        },
    };
    uint32_t hwpt_id = 0, flags = 0;
    SMMUS1Hwpt *s1_hwpt;

    if (!iommufd_backend_alloc_hwpt(idev->iommufd, idev->devid,
                                    accel->viommu->viommu_id, flags,
                                    IOMMU_HWPT_DATA_ARM_SMMUV3,
                                    sizeof(nested_data), &nested_data,
                                    &hwpt_id, errp)) {
            return NULL;
    }

    s1_hwpt = g_new0(SMMUS1Hwpt, 1);
    s1_hwpt->hwpt_id = hwpt_id;
    trace_smmuv3_accel_translate_ste(accel_dev->vdev->virt_id, hwpt_id,
                                     nested_data.ste[1], nested_data.ste[0]);
    return s1_hwpt;
}

bool smmuv3_accel_install_ste(SMMUv3State *s, SMMUDevice *sdev, int sid,
                              Error **errp)
{
    SMMUEventInfo event = {.type = SMMU_EVT_NONE, .sid = sid,
                           .inval_ste_allowed = true};
    SMMUv3AccelState *accel = s->s_accel;
    SMMUv3AccelDevice *accel_dev;
    HostIOMMUDeviceIOMMUFD *idev;
    uint32_t config, hwpt_id = 0;
    SMMUS1Hwpt *s1_hwpt = NULL;
    const char *type;
    STE ste;

    if (!accel || !accel->viommu) {
        return true;
    }

    accel_dev = container_of(sdev, SMMUv3AccelDevice, sdev);
    if (!accel_dev->s_accel) {
        return true;
    }

    idev = accel_dev->idev;
    if (!smmuv3_accel_alloc_vdev(accel_dev, sid, errp)) {
        return false;
    }

    if (smmu_find_ste(sdev->smmu, sid, &ste, &event)) {
        /* No STE found, nothing to install */
        return true;
    }

    /*
     * Install the STE based on SMMU enabled/config:
     * - attach a pre-allocated HWPT for abort/bypass
     * - or a new HWPT for translate STE
     *
     * Note: The vdev remains associated with accel_dev even if HWPT
     * attach/alloc fails, since the Guest–Host SID mapping stays
     * valid as long as the device is behind the accelerated SMMUv3.
     */
    if (!smmu_enabled(s)) {
        hwpt_id = smmuv3_accel_gbpa_hwpt(s, accel);
    } else {
        config = STE_CONFIG(&ste);

        if (!STE_VALID(&ste) || STE_CFG_ABORT(config)) {
            hwpt_id = accel->abort_hwpt_id;
        } else if (STE_CFG_BYPASS(config)) {
            hwpt_id = accel->bypass_hwpt_id;
        } else if (STE_CFG_S1_TRANSLATE(config)) {
            s1_hwpt = smmuv3_accel_dev_alloc_translate(accel_dev, &ste, errp);
            if (!s1_hwpt) {
                return false;
            }
            hwpt_id = s1_hwpt->hwpt_id;
       }
    }

    if (!hwpt_id) {
        error_setg(errp, "Invalid STE config for sid 0x%x",
                   smmu_get_sid(&accel_dev->sdev));
        return false;
    }

    if (!host_iommu_device_iommufd_attach_hwpt(idev, hwpt_id, errp)) {
        if (s1_hwpt) {
            iommufd_backend_free_id(idev->iommufd, s1_hwpt->hwpt_id);
            g_free(s1_hwpt);
        }
        return false;
    }

    /* Free the previous s1_hwpt */
    if (accel_dev->s1_hwpt) {
        iommufd_backend_free_id(idev->iommufd, accel_dev->s1_hwpt->hwpt_id);
        g_free(accel_dev->s1_hwpt);
    }

    accel_dev->s1_hwpt = s1_hwpt;
    if (hwpt_id == accel->abort_hwpt_id) {
        type = "abort";
    } else if (hwpt_id == accel->bypass_hwpt_id) {
        type = "bypass";
    } else {
        type = "translate";
    }

    trace_smmuv3_accel_install_ste(sid, type, hwpt_id);
    return true;
}

bool smmuv3_accel_install_ste_range(SMMUv3State *s, SMMUSIDRange *range,
                                    Error **errp)
{
    SMMUv3AccelState *accel = s->s_accel;
    SMMUv3AccelDevice *accel_dev;
    Error *local_err = NULL;
    bool all_ok = true;

    if (!accel || !accel->viommu) {
        return true;
    }

    QLIST_FOREACH(accel_dev, &accel->device_list, next) {
        uint32_t sid = smmu_get_sid(&accel_dev->sdev);

        if (sid >= range->start && sid <= range->end) {
            if (!smmuv3_accel_install_ste(s, &accel_dev->sdev,
                                          sid, &local_err)) {
                error_append_hint(&local_err, "Device 0x%x: Failed to install "
                                  "STE\n", sid);
                error_report_err(local_err);
                local_err = NULL;
                all_ok = false;
            }
        }
    }

    if (!all_ok) {
        error_setg(errp, "Failed to install all STEs properly");
    }
    return all_ok;
}

/*
 * This issues the invalidation cmd to the host SMMUv3.
 *
 * sdev is non-NULL for SID based invalidations (e.g. CFGI_CD), and NULL for
 * non SID invalidations such as SMMU_CMD_TLBI_NH_ASID and SMMU_CMD_TLBI_NH_VA.
 */
bool smmuv3_accel_issue_inv_cmd(SMMUv3State *bs, void *cmd, SMMUDevice *sdev,
                                Error **errp)
{
    SMMUv3State *s = ARM_SMMUV3(bs);
    SMMUv3AccelState *accel = s->s_accel;
    uint32_t entry_num = 1;

    /*
     * No accel or viommu means no VFIO/IOMMUFD devices, nothing to
     * invalidate.
     */
    if (!accel || !accel->viommu) {
        return true;
    }

    /*
     * SID based invalidations (e.g. CFGI_CD) apply only to vfio-pci endpoints
     * with a valid vIOMMU vdev.
     */
    if (sdev && !container_of(sdev, SMMUv3AccelDevice, sdev)->vdev) {
        return true;
    }

    /* Single command (entry_num = 1); no need to check returned entry_num */
    return iommufd_backend_invalidate_cache(
                   accel->viommu->iommufd, accel->viommu->viommu_id,
                   IOMMU_VIOMMU_INVALIDATE_DATA_ARM_SMMUV3,
                   sizeof(Cmd), &entry_num, cmd, errp);
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

    /*
     * Check the host SMMUv3 associated with the dev is compatible with the
     * QEMU SMMUv3 accel.
     */
    if (!smmuv3_accel_hw_compatible(s, idev, errp)) {
        return false;
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
    IOMMUFDVdev *vdev;
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

    if (accel_dev->s1_hwpt) {
        iommufd_backend_free_id(accel_dev->idev->iommufd,
                                accel_dev->s1_hwpt->hwpt_id);
        g_free(accel_dev->s1_hwpt);
        accel_dev->s1_hwpt = NULL;
    }

    vdev = accel_dev->vdev;
    if (vdev) {
        iommufd_backend_free_id(accel->viommu->iommufd, vdev->vdevice_id);
        g_free(vdev);
        accel_dev->vdev = NULL;
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

static uint64_t smmuv3_accel_get_msi_gpa(PCIBus *bus, void *opaque, int devfn)
{
    SMMUState *bs = opaque;
    SMMUv3State *s = ARM_SMMUV3(bs);

    g_assert(s->msi_gpa);
    return s->msi_gpa;
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
    uint64_t flags = VIOMMU_FLAG_WANT_NESTING_PARENT;
    SMMUState *bs = opaque;
    SMMUv3State *s = ARM_SMMUV3(bs);

    if (s->ssidsize) {
        flags |= VIOMMU_FLAG_PASID_SUPPORTED;
    }
    return flags;
}

static const PCIIOMMUOps smmuv3_accel_ops = {
    .supports_address_space = smmuv3_accel_supports_as,
    .get_address_space = smmuv3_accel_find_add_as,
    .get_viommu_flags = smmuv3_accel_get_viommu_flags,
    .set_iommu_device = smmuv3_accel_set_iommu_device,
    .unset_iommu_device = smmuv3_accel_unset_iommu_device,
    .get_msi_direct_gpa = smmuv3_accel_get_msi_gpa,
};

void smmuv3_accel_idr_override(SMMUv3State *s)
{
    if (!s->accel) {
        return;
    }

    /* By default QEMU SMMUv3 has RIL. Update IDR3 if user has disabled it */
    s->idr[3] = FIELD_DP32(s->idr[3], IDR3, RIL, s->ril);

    /* QEMU SMMUv3 has no ATS. Advertise ATS if opt-in by property */
    s->idr[0] = FIELD_DP32(s->idr[0], IDR0, ATS, s->ats);

    /* Advertise 48-bit OAS in IDR5 when requested (default is 44 bits). */
    if (s->oas == SMMU_OAS_48BIT) {
        s->idr[5] = FIELD_DP32(s->idr[5], IDR5, OAS, SMMU_IDR5_OAS_48);
    }

    /*
     * By default QEMU SMMUv3 has no SubstreamID support. Update IDR1 if user
     * has enabled it.
     */
    s->idr[1] = FIELD_DP32(s->idr[1], IDR1, SSIDSIZE, s->ssidsize);
}

/* Based on SMUUv3 GPBA.ABORT configuration, attach a corresponding HWPT */
bool smmuv3_accel_attach_gbpa_hwpt(SMMUv3State *s, Error **errp)
{
    SMMUv3AccelState *accel = s->s_accel;
    SMMUv3AccelDevice *accel_dev;
    Error *local_err = NULL;
    bool all_ok = true;
    uint32_t hwpt_id;

    if (!accel || !accel->viommu) {
        return true;
    }

    hwpt_id = smmuv3_accel_gbpa_hwpt(s, accel);
    QLIST_FOREACH(accel_dev, &accel->device_list, next) {
        if (!host_iommu_device_iommufd_attach_hwpt(accel_dev->idev, hwpt_id,
                                                   &local_err)) {
            error_append_hint(&local_err, "Failed to attach GBPA hwpt %u for "
                              "idev devid %u", hwpt_id, accel_dev->idev->devid);
            error_report_err(local_err);
            local_err = NULL;
            all_ok = false;
        }
    }
    if (!all_ok) {
        error_setg(errp, "Failed to attach all GBPA based HWPTs properly");
    }
    return all_ok;
}

void smmuv3_accel_reset(SMMUv3State *s)
{
     /* Attach a HWPT based on GBPA reset value */
     smmuv3_accel_attach_gbpa_hwpt(s, NULL);
}

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
