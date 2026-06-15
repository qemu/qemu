/*
 * Intel IOMMU acceleration with nested translation
 *
 * Copyright (C) 2026 Intel Corporation.
 *
 * Authors: Zhenzhong Duan <zhenzhong.duan@intel.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_I386_INTEL_IOMMU_ACCEL_H
#define HW_I386_INTEL_IOMMU_ACCEL_H
#include CONFIG_DEVICES

typedef struct VTDAccelPASIDCacheEntry {
    VTDHostIOMMUDevice *vtd_hiod;
    VTDPASIDEntry pasid_entry;
    uint32_t pasid;
    uint32_t fs_hwpt_id;
    QLIST_ENTRY(VTDAccelPASIDCacheEntry) next;
} VTDAccelPASIDCacheEntry;

#ifdef CONFIG_VTD_ACCEL
bool vtd_check_hiod_accel(IntelIOMMUState *s, VTDHostIOMMUDevice *vtd_hiod,
                          Error **errp);
VTDHostIOMMUDevice *vtd_find_hiod_iommufd(VTDAddressSpace *as);
void vtd_flush_host_piotlb_all_locked(IntelIOMMUState *s, uint16_t domain_id,
                                      uint32_t pasid, hwaddr addr,
                                      uint64_t npages, bool ih);
void vtd_accel_pasid_cache_sync(IntelIOMMUState *s, VTDPASIDCacheInfo *pc_info);
void vtd_accel_pasid_cache_reset(IntelIOMMUState *s);
void vtd_iommu_ops_update_accel(PCIIOMMUOps *ops);
#else
static inline bool vtd_check_hiod_accel(IntelIOMMUState *s,
                                        VTDHostIOMMUDevice *vtd_hiod,
                                        Error **errp)
{
    error_setg(errp, "host IOMMU cannot be checked!");
    error_append_hint(errp, "CONFIG_VTD_ACCEL is not enabled");
    return false;
}

static inline VTDHostIOMMUDevice *vtd_find_hiod_iommufd(VTDAddressSpace *as)
{
    return NULL;
}

static inline bool vtd_propagate_guest_pasid(VTDAddressSpace *vtd_as,
                                             Error **errp)
{
    return true;
}

static inline void vtd_flush_host_piotlb_all_locked(IntelIOMMUState *s,
                                                    uint16_t domain_id,
                                                    uint32_t pasid, hwaddr addr,
                                                    uint64_t npages, bool ih)
{
}

static inline void vtd_accel_pasid_cache_sync(IntelIOMMUState *s,
                                              VTDPASIDCacheInfo *pc_info)
{
}

static inline void vtd_accel_pasid_cache_reset(IntelIOMMUState *s)
{
}

static inline void vtd_iommu_ops_update_accel(PCIIOMMUOps *ops)
{
}
#endif
#endif
