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
#include "hw/core/iommu.h"
#include "hw/pci/pci_bus.h"
#include "trace.h"

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

    return true;
}

VTDHostIOMMUDevice *vtd_find_hiod_iommufd(VTDAddressSpace *as)
{
    IntelIOMMUState *s = as->iommu_state;
    struct vtd_as_key key = {
        .bus = as->bus,
        .devfn = as->devfn,
    };
    VTDHostIOMMUDevice *vtd_hiod = g_hash_table_lookup(s->vtd_host_iommu_dev,
                                                       &key);

    if (vtd_hiod && vtd_hiod->hiod &&
        object_dynamic_cast(OBJECT(vtd_hiod->hiod),
                            TYPE_HOST_IOMMU_DEVICE_IOMMUFD)) {
        return vtd_hiod;
    }
    return NULL;
}

static bool vtd_create_fs_hwpt(HostIOMMUDeviceIOMMUFD *idev,
                               VTDPASIDEntry *pe, uint32_t *fs_hwpt_id,
                               Error **errp)
{
    struct iommu_hwpt_vtd_s1 vtd = {};

    vtd.flags = (VTD_SM_PASID_ENTRY_SRE(pe) ? IOMMU_VTD_S1_SRE : 0) |
                (VTD_SM_PASID_ENTRY_WPE(pe) ? IOMMU_VTD_S1_WPE : 0) |
                (VTD_SM_PASID_ENTRY_EAFE(pe) ? IOMMU_VTD_S1_EAFE : 0);
    vtd.addr_width = vtd_pe_get_fs_aw(pe);
    vtd.pgtbl_addr = (uint64_t)vtd_pe_get_fspt_base(pe);

    return iommufd_backend_alloc_hwpt(idev->iommufd, idev->devid, idev->hwpt_id,
                                      0, IOMMU_HWPT_DATA_VTD_S1, sizeof(vtd),
                                      &vtd, fs_hwpt_id, errp);
}

static void vtd_destroy_old_fs_hwpt(HostIOMMUDeviceIOMMUFD *idev,
                                    VTDAddressSpace *vtd_as)
{
    if (!vtd_as->fs_hwpt_id) {
        return;
    }
    iommufd_backend_free_id(idev->iommufd, vtd_as->fs_hwpt_id);
    vtd_as->fs_hwpt_id = 0;
}

static bool vtd_device_attach_iommufd(VTDHostIOMMUDevice *vtd_hiod,
                                      VTDAddressSpace *vtd_as, Error **errp)
{
    HostIOMMUDeviceIOMMUFD *idev = HOST_IOMMU_DEVICE_IOMMUFD(vtd_hiod->hiod);
    VTDPASIDEntry *pe = &vtd_as->pasid_cache_entry.pasid_entry;
    uint32_t hwpt_id = idev->hwpt_id;
    bool ret;

    /*
     * We can get here only if flts=on, the supported PGTT is FST or PT.
     * Catch invalid PGTT when processing invalidation request to avoid
     * attaching to wrong hwpt.
     */
    if (!vtd_pe_pgtt_is_fst(pe) && !vtd_pe_pgtt_is_pt(pe)) {
        error_setg(errp, "Invalid PGTT type %d",
                   (uint8_t)VTD_SM_PASID_ENTRY_PGTT(pe));
        return false;
    }

    if (vtd_pe_pgtt_is_fst(pe)) {
        if (!vtd_create_fs_hwpt(idev, pe, &hwpt_id, errp)) {
            return false;
        }
    }

    ret = host_iommu_device_iommufd_attach_hwpt(idev, hwpt_id, errp);
    trace_vtd_device_attach_hwpt(idev->devid, vtd_as->pasid, hwpt_id, ret);
    if (ret) {
        /* Destroy old fs_hwpt if it's a replacement */
        vtd_destroy_old_fs_hwpt(idev, vtd_as);
        if (vtd_pe_pgtt_is_fst(pe)) {
            vtd_as->fs_hwpt_id = hwpt_id;
        }
    } else if (vtd_pe_pgtt_is_fst(pe)) {
        iommufd_backend_free_id(idev->iommufd, hwpt_id);
    }

    return ret;
}

static bool vtd_device_detach_iommufd(VTDHostIOMMUDevice *vtd_hiod,
                                      VTDAddressSpace *vtd_as, Error **errp)
{
    HostIOMMUDeviceIOMMUFD *idev = HOST_IOMMU_DEVICE_IOMMUFD(vtd_hiod->hiod);
    IntelIOMMUState *s = vtd_as->iommu_state;
    uint32_t pasid = vtd_as->pasid;
    bool ret;

    if (s->dmar_enabled && s->root_scalable) {
        ret = host_iommu_device_iommufd_detach_hwpt(idev, errp);
        trace_vtd_device_detach_hwpt(idev->devid, pasid, ret);
    } else {
        /*
         * If DMAR remapping is disabled or guest switches to legacy mode,
         * we fallback to the default HWPT which contains shadow page table.
         * So guest DMA could still work.
         */
        ret = host_iommu_device_iommufd_attach_hwpt(idev, idev->hwpt_id, errp);
        trace_vtd_device_reattach_def_hwpt(idev->devid, pasid, idev->hwpt_id,
                                           ret);
    }

    if (ret) {
        vtd_destroy_old_fs_hwpt(idev, vtd_as);
    }

    return ret;
}

bool vtd_propagate_guest_pasid(VTDAddressSpace *vtd_as, Error **errp)
{
    VTDPASIDCacheEntry *pc_entry = &vtd_as->pasid_cache_entry;
    VTDHostIOMMUDevice *vtd_hiod = vtd_find_hiod_iommufd(vtd_as);

    /* Ignore emulated device or legacy VFIO backed device */
    if (!vtd_as->iommu_state->fsts || !vtd_hiod) {
        return true;
    }

    if (pc_entry->valid) {
        return vtd_device_attach_iommufd(vtd_hiod, vtd_as, errp);
    }

    return vtd_device_detach_iommufd(vtd_hiod, vtd_as, errp);
}

/*
 * This function is a loop function for the s->vtd_address_spaces
 * list with VTDPIOTLBInvInfo as execution filter. It propagates
 * the piotlb invalidation to host.
 */
static void vtd_flush_host_piotlb_locked(gpointer key, gpointer value,
                                         gpointer user_data)
{
    VTDPIOTLBInvInfo *piotlb_info = user_data;
    VTDAddressSpace *vtd_as = value;
    VTDHostIOMMUDevice *vtd_hiod = vtd_find_hiod_iommufd(vtd_as);
    VTDPASIDCacheEntry *pc_entry = &vtd_as->pasid_cache_entry;
    uint16_t did;

    if (!vtd_hiod) {
        return;
    }

    assert(vtd_as->pasid == PCI_NO_PASID);

    /* Nothing to do if there is no first stage HWPT attached */
    if (!pc_entry->valid ||
        !vtd_pe_pgtt_is_fst(&pc_entry->pasid_entry)) {
        return;
    }

    did = VTD_SM_PASID_ENTRY_DID(&pc_entry->pasid_entry);

    if (piotlb_info->domain_id == did && piotlb_info->pasid == PASID_0) {
        HostIOMMUDeviceIOMMUFD *idev =
            HOST_IOMMU_DEVICE_IOMMUFD(vtd_hiod->hiod);
        uint32_t entry_num = 1; /* Only implement one request for simplicity */
        Error *local_err = NULL;
        struct iommu_hwpt_vtd_s1_invalidate *cache = piotlb_info->inv_data;

        if (!iommufd_backend_invalidate_cache(idev->iommufd, vtd_as->fs_hwpt_id,
                                              IOMMU_HWPT_INVALIDATE_DATA_VTD_S1,
                                              sizeof(*cache), &entry_num, cache,
                                              &local_err)) {
            /* Something wrong in kernel, but trying to continue */
            error_report_err(local_err);
        }
    }
}

void vtd_flush_host_piotlb_all_locked(IntelIOMMUState *s, uint16_t domain_id,
                                      uint32_t pasid, hwaddr addr,
                                      uint64_t npages, bool ih)
{
    struct iommu_hwpt_vtd_s1_invalidate cache_info = { 0 };
    VTDPIOTLBInvInfo piotlb_info;

    cache_info.addr = addr;
    cache_info.npages = npages;
    cache_info.flags = ih ? IOMMU_VTD_INV_FLAGS_LEAF : 0;

    piotlb_info.domain_id = domain_id;
    piotlb_info.pasid = pasid;
    piotlb_info.inv_data = &cache_info;

    /*
     * Go through each vtd_as instance in s->vtd_address_spaces, find out
     * affected host devices which need host piotlb invalidation. Piotlb
     * invalidation should check pasid cache per architecture point of view.
     */
    g_hash_table_foreach(s->vtd_address_spaces,
                         vtd_flush_host_piotlb_locked, &piotlb_info);
}

static uint64_t vtd_get_host_iommu_quirks(uint32_t type,
                                          void *caps, uint32_t size)
{
    struct iommu_hw_info_vtd *vtd = caps;
    uint64_t quirks = 0;

    if (type == IOMMU_HW_INFO_TYPE_INTEL_VTD &&
        sizeof(struct iommu_hw_info_vtd) <= size &&
        vtd->flags & IOMMU_HW_INFO_VTD_ERRATA_772415_SPR17) {
        quirks |= HOST_IOMMU_QUIRK_NESTING_PARENT_BYPASS_RO;
    }

    return quirks;
}

void vtd_iommu_ops_update_accel(PCIIOMMUOps *ops)
{
    ops->get_host_iommu_quirks = vtd_get_host_iommu_quirks;
}
