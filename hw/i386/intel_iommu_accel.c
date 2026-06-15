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
    uint8_t hpasid = VTD_ECAP_GET_PSS(vtd->ecap_reg) + 1;
    PCIBus *bus = vtd_hiod->bus;
    PCIDevice *pdev = bus->devices[vtd_hiod->devfn];

    if (!object_dynamic_cast(OBJECT(hiod), TYPE_HOST_IOMMU_DEVICE_IOMMUFD)) {
        error_setg(errp, "Need IOMMUFD backend when fsts=on");
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

    /* Only do the check when host device support PASIDs */
    if (caps->max_pasid_log2 && s->pasid > hpasid) {
        error_setg(errp, "PASID bits size %d > host IOMMU PASID bits size %d",
                   s->pasid, hpasid);
        return false;
    }

    if (pci_device_get_iommu_bus_devfn(pdev, &bus, NULL, NULL)) {
        error_setg(errp, "Host device downstream to a PCI bridge is "
                   "unsupported when fsts=on");
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

static bool vtd_create_fs_hwpt(VTDHostIOMMUDevice *vtd_hiod,
                               VTDPASIDEntry *pe, uint32_t *fs_hwpt_id,
                               Error **errp)
{
    HostIOMMUDeviceIOMMUFD *hiodi = HOST_IOMMU_DEVICE_IOMMUFD(vtd_hiod->hiod);
    struct iommu_hwpt_vtd_s1 vtd = {};
    uint32_t flags = vtd_hiod->iommu_state->pasid ? IOMMU_HWPT_ALLOC_PASID : 0;

    vtd.flags = (VTD_SM_PASID_ENTRY_SRE(pe) ? IOMMU_VTD_S1_SRE : 0) |
                (VTD_SM_PASID_ENTRY_WPE(pe) ? IOMMU_VTD_S1_WPE : 0) |
                (VTD_SM_PASID_ENTRY_EAFE(pe) ? IOMMU_VTD_S1_EAFE : 0);
    vtd.addr_width = vtd_pe_get_fs_aw(pe);
    vtd.pgtbl_addr = (uint64_t)vtd_pe_get_fspt_base(pe);

    return iommufd_backend_alloc_hwpt(hiodi->iommufd, hiodi->devid,
                                      hiodi->hwpt_id, flags,
                                      IOMMU_HWPT_DATA_VTD_S1, sizeof(vtd), &vtd,
                                      fs_hwpt_id, errp);
}

static void vtd_destroy_old_fs_hwpt(VTDAccelPASIDCacheEntry *vtd_pce)
{
    HostIOMMUDeviceIOMMUFD *hiodi =
        HOST_IOMMU_DEVICE_IOMMUFD(vtd_pce->vtd_hiod->hiod);

    if (!vtd_pce->fs_hwpt_id) {
        return;
    }
    iommufd_backend_free_id(hiodi->iommufd, vtd_pce->fs_hwpt_id);
    vtd_pce->fs_hwpt_id = 0;
}

static bool vtd_device_attach_iommufd(VTDAccelPASIDCacheEntry *vtd_pce,
                                      Error **errp)
{
    VTDHostIOMMUDevice *vtd_hiod = vtd_pce->vtd_hiod;
    HostIOMMUDeviceIOMMUFD *hiodi = HOST_IOMMU_DEVICE_IOMMUFD(vtd_hiod->hiod);
    VTDPASIDEntry *pe = &vtd_pce->pasid_entry;
    uint32_t hwpt_id = hiodi->hwpt_id, pasid = vtd_pce->pasid;
    bool ret;

    /*
     * We can get here only if fsts=on, the supported PGTT is FST or PT.
     * Catch invalid PGTT when processing invalidation request to avoid
     * attaching to wrong hwpt.
     */
    if (!vtd_pe_pgtt_is_fst(pe) && !vtd_pe_pgtt_is_pt(pe)) {
        error_setg(errp, "Invalid PGTT type %d",
                   (uint8_t)VTD_SM_PASID_ENTRY_PGTT(pe));
        return false;
    }

    if (vtd_pe_pgtt_is_fst(pe)) {
        if (!vtd_create_fs_hwpt(vtd_hiod, pe, &hwpt_id, errp)) {
            return false;
        }
    }

    ret = host_iommu_device_iommufd_attach_hwpt(hiodi, pasid, hwpt_id, errp);
    trace_vtd_device_attach_hwpt(hiodi->devid, pasid, hwpt_id, ret);
    if (ret) {
        /* Destroy old fs_hwpt if it's a replacement */
        vtd_destroy_old_fs_hwpt(vtd_pce);
        if (vtd_pe_pgtt_is_fst(pe)) {
            vtd_pce->fs_hwpt_id = hwpt_id;
        }
    } else if (vtd_pe_pgtt_is_fst(pe)) {
        iommufd_backend_free_id(hiodi->iommufd, hwpt_id);
    }

    return ret;
}

static bool vtd_device_detach_iommufd(VTDAccelPASIDCacheEntry *vtd_pce,
                                      Error **errp)
{
    VTDHostIOMMUDevice *vtd_hiod = vtd_pce->vtd_hiod;
    HostIOMMUDeviceIOMMUFD *hiodi = HOST_IOMMU_DEVICE_IOMMUFD(vtd_hiod->hiod);

    IntelIOMMUState *s = vtd_hiod->iommu_state;
    uint32_t pasid = vtd_pce->pasid;
    bool ret;

    if (pasid != IOMMU_NO_PASID || (s->dmar_enabled && s->root_scalable)) {
        ret = host_iommu_device_iommufd_detach_hwpt(hiodi, pasid, errp);
        trace_vtd_device_detach_hwpt(hiodi->devid, pasid, ret);
    } else {
        /*
         * If DMAR remapping is disabled or guest switches to legacy mode,
         * we fallback to the default HWPT which contains shadow page table.
         * So guest DMA could still work.
         */
        ret = host_iommu_device_iommufd_attach_hwpt(hiodi, IOMMU_NO_PASID,
                                                    hiodi->hwpt_id, errp);
        trace_vtd_device_reattach_def_hwpt(hiodi->devid, IOMMU_NO_PASID,
                                           hiodi->hwpt_id, ret);
    }

    if (ret) {
        vtd_destroy_old_fs_hwpt(vtd_pce);
    }

    return ret;
}

/*
 * This function is a loop function for the s->vtd_host_iommu_dev
 * and vtd_hiod->pasid_cache_list lists with VTDPIOTLBInvInfo as
 * execution filter. It propagates the piotlb invalidation to host.
 */
static void vtd_flush_host_piotlb_locked(VTDAccelPASIDCacheEntry *vtd_pce,
                                         VTDPIOTLBInvInfo *piotlb_info)
{
    VTDHostIOMMUDevice *vtd_hiod = vtd_pce->vtd_hiod;
    VTDPASIDEntry *pe = &vtd_pce->pasid_entry;
    uint16_t did;

    /* Nothing to do if there is no first stage HWPT attached */
    if (!vtd_pe_pgtt_is_fst(pe)) {
        return;
    }

    did = VTD_SM_PASID_ENTRY_DID(pe);

    if (piotlb_info->domain_id == did && piotlb_info->pasid == vtd_pce->pasid) {
        HostIOMMUDeviceIOMMUFD *hiodi =
            HOST_IOMMU_DEVICE_IOMMUFD(vtd_hiod->hiod);
        uint32_t entry_num = 1; /* Only implement one request for simplicity */
        Error *local_err = NULL;
        struct iommu_hwpt_vtd_s1_invalidate *cache = piotlb_info->inv_data;

        if (!iommufd_backend_invalidate_cache(hiodi->iommufd,
                                              vtd_pce->fs_hwpt_id,
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
    VTDHostIOMMUDevice *vtd_hiod;
    GHashTableIter hiod_it;

    cache_info.addr = addr;
    cache_info.npages = npages;
    cache_info.flags = ih ? IOMMU_VTD_INV_FLAGS_LEAF : 0;

    piotlb_info.domain_id = domain_id;
    piotlb_info.pasid = pasid;
    piotlb_info.inv_data = &cache_info;

    /*
     * Go through each vtd_pce in vtd_hiod->pasid_cache_list for each host
     * device, find out affected host device pasid which need host piotlb
     * invalidation. Piotlb invalidation should check pasid cache per
     * architecture point of view.
     */
    g_hash_table_iter_init(&hiod_it, s->vtd_host_iommu_dev);
    while (g_hash_table_iter_next(&hiod_it, NULL, (void **)&vtd_hiod)) {
        VTDAccelPASIDCacheEntry *vtd_pce;

        QLIST_FOREACH(vtd_pce, &vtd_hiod->pasid_cache_list, next) {
            vtd_flush_host_piotlb_locked(vtd_pce, &piotlb_info);
        }
    }
}

static void vtd_accel_fill_pc(VTDHostIOMMUDevice *vtd_hiod, uint32_t pasid,
                              VTDPASIDEntry *pe)
{
    VTDAccelPASIDCacheEntry *vtd_pce;
    Error *local_err = NULL;

    QLIST_FOREACH(vtd_pce, &vtd_hiod->pasid_cache_list, next) {
        if (vtd_pce->pasid == pasid) {
            if (vtd_pasid_entry_compare(pe, &vtd_pce->pasid_entry)) {
                vtd_pce->pasid_entry = *pe;

                if (!vtd_device_attach_iommufd(vtd_pce, &local_err)) {
                    error_reportf_err(local_err, "%s",
                                      "Replacing HWPT attachment failed: ");
                }
            }
            return;
        }
    }

    vtd_pce = g_malloc0(sizeof(VTDAccelPASIDCacheEntry));
    vtd_pce->vtd_hiod = vtd_hiod;
    vtd_pce->pasid = pasid;
    vtd_pce->pasid_entry = *pe;
    QLIST_INSERT_HEAD(&vtd_hiod->pasid_cache_list, vtd_pce, next);

    if (!vtd_device_attach_iommufd(vtd_pce, &local_err)) {
        error_reportf_err(local_err, "%s", "Attaching to HWPT failed: ");
    }
}

static void vtd_accel_delete_pc(VTDAccelPASIDCacheEntry *vtd_pce,
                                VTDPASIDCacheInfo *pc_info)
{
    Error *local_err = NULL;

    if (!vtd_device_detach_iommufd(vtd_pce, &local_err)) {
        error_reportf_err(local_err, "%s", "Detaching from HWPT failed: ");
    }

    QLIST_REMOVE(vtd_pce, next);
    g_free(vtd_pce);

    if (pc_info->type == VTD_INV_DESC_PASIDC_G_PASID_SI) {
        pc_info->accel_pce_deleted = true;
    }
}

static void
vtd_accel_pasid_cache_invalidate_one(VTDAccelPASIDCacheEntry *vtd_pce,
                                     VTDPASIDCacheInfo *pc_info)
{
    VTDHostIOMMUDevice *vtd_hiod = vtd_pce->vtd_hiod;
    VTDPASIDEntry pe;
    uint16_t did;

    /*
     * VTD_INV_DESC_PASIDC_G_DSI and VTD_INV_DESC_PASIDC_G_PASID_SI require
     * DID check. If DID doesn't match the value in cache or memory, then
     * it's not a pasid entry we want to invalidate.
     */
    switch (pc_info->type) {
    case VTD_INV_DESC_PASIDC_G_PASID_SI:
        if (pc_info->pasid != vtd_pce->pasid) {
            return;
        }
        /* Fall through */
    case VTD_INV_DESC_PASIDC_G_DSI:
        did = VTD_SM_PASID_ENTRY_DID(&vtd_pce->pasid_entry);
        if (pc_info->did != did) {
            return;
        }
    }

    if (vtd_dev_get_pe_from_pasid(vtd_hiod->iommu_state, vtd_hiod->bus,
                                  vtd_hiod->devfn, vtd_pce->pasid, &pe)) {
        /*
         * No valid pasid entry in guest memory. e.g. pasid entry was modified
         * to be either all-zero or non-present. Either case means existing
         * pasid cache should be invalidated.
         */
        vtd_accel_delete_pc(vtd_pce, pc_info);
    }
}

static void vtd_accel_pasid_cache_invalidate(VTDHostIOMMUDevice *vtd_hiod,
                                             VTDPASIDCacheInfo *pc_info)
{
    VTDAccelPASIDCacheEntry *vtd_pce, *next;

    QLIST_FOREACH_SAFE(vtd_pce, &vtd_hiod->pasid_cache_list, next, next) {
        vtd_accel_pasid_cache_invalidate_one(vtd_pce, pc_info);
    }
}

/*
 * This function walks over PASID range within [start, end) in a single
 * PASID table for entries matching @info type/did, then create
 * VTDAccelPASIDCacheEntry if not exist yet.
 */
static void vtd_sm_pasid_table_walk_one(VTDHostIOMMUDevice *vtd_hiod,
                                        dma_addr_t pt_base, int start, int end,
                                        VTDPASIDCacheInfo *info)
{
    IntelIOMMUState *s = vtd_hiod->iommu_state;
    VTDPASIDEntry pe;
    int pasid;

    for (pasid = start; pasid < end; pasid++) {
        if (vtd_get_pe_in_pasid_leaf_table(s, pasid, pt_base, &pe) ||
            !vtd_pe_present(&pe)) {
            continue;
        }

        if ((info->type == VTD_INV_DESC_PASIDC_G_DSI ||
             info->type == VTD_INV_DESC_PASIDC_G_PASID_SI) &&
            (info->did != VTD_SM_PASID_ENTRY_DID(&pe))) {
            /*
             * VTD_PASID_CACHE_DOMSI and VTD_PASID_CACHE_PASIDSI
             * requires domain id check. If domain id check fail,
             * go to next pasid.
             */
            continue;
        }

        vtd_accel_fill_pc(vtd_hiod, pasid, &pe);
    }
}

/*
 * In VT-d scalable mode translation, PASID dir + PASID table is used.
 * This function aims at looping over a range of PASIDs in the given
 * two level table to identify the pasid config in guest.
 */
static void vtd_sm_pasid_table_walk(VTDHostIOMMUDevice *vtd_hiod,
                                    dma_addr_t pdt_base, int start, int end,
                                    VTDPASIDCacheInfo *info)
{
    VTDPASIDDirEntry pdire;
    int pasid = start;
    int pasid_next;
    dma_addr_t pt_base;

    while (pasid < end) {
        pasid_next = (pasid + VTD_PASID_TABLE_ENTRY_NUM) &
                     ~(VTD_PASID_TABLE_ENTRY_NUM - 1);
        pasid_next = pasid_next < end ? pasid_next : end;

        if (!vtd_get_pdire_from_pdir_table(pdt_base, pasid, &pdire)
            && vtd_pdire_present(&pdire)) {
            pt_base = pdire.val & VTD_PASID_TABLE_BASE_ADDR_MASK;
            vtd_sm_pasid_table_walk_one(vtd_hiod, pt_base, pasid, pasid_next,
                                        info);
        }
        pasid = pasid_next;
    }
}

static void vtd_accel_replay_pasid_bind_for_dev(VTDHostIOMMUDevice *vtd_hiod,
                                                int start, int end,
                                                VTDPASIDCacheInfo *pc_info)
{
    IntelIOMMUState *s = vtd_hiod->iommu_state;
    VTDContextEntry ce;
    int dev_max_pasid = 1 << vtd_hiod->hiod->caps.max_pasid_log2;

    if (!vtd_dev_to_context_entry(s, pci_bus_num(vtd_hiod->bus),
                                  vtd_hiod->devfn, &ce)) {
        VTDPASIDCacheInfo walk_info = *pc_info;
        uint32_t ce_max_pasid = vtd_sm_ce_get_pdt_entry_num(&ce) *
                                VTD_PASID_TABLE_ENTRY_NUM;

        end = MIN(end, MIN(dev_max_pasid, ce_max_pasid));

        vtd_sm_pasid_table_walk(vtd_hiod, VTD_CE_GET_PASID_DIR_TABLE(&ce),
                                start, end, &walk_info);
    }
}

/*
 * This function replays the guest pasid bindings by walking the two level
 * guest PASID table. For each valid pasid entry, it creates an entry
 * VTDAccelPASIDCacheEntry dynamically if not exist yet. This entry holds
 * info specific to a pasid
 */
void vtd_accel_pasid_cache_sync(IntelIOMMUState *s, VTDPASIDCacheInfo *pc_info)
{
    int start = IOMMU_NO_PASID, end = 1 << s->pasid;
    VTDHostIOMMUDevice *vtd_hiod;
    GHashTableIter hiod_it;

    if (!s->fsts) {
        return;
    }

    switch (pc_info->type) {
    case VTD_INV_DESC_PASIDC_G_PASID_SI:
        start = pc_info->pasid;
        end = pc_info->pasid + 1;
        /* fall through */
    case VTD_INV_DESC_PASIDC_G_DSI:
        /*
         * loop all assigned devices, do domain id check in
         * vtd_sm_pasid_table_walk_one() after get pasid entry.
         */
        break;
    case VTD_INV_DESC_PASIDC_G_GLOBAL:
        /* loop all assigned devices */
        break;
    default:
        g_assert_not_reached();
    }

    /*
     * Loop all the vtd_hiod instances to sync the "pasid cache" per the
     * guest pasid configuration.
     *
     * VTD translation callback never accesses vtd_hiod and its corresponding
     * cached pasid entry, so no iommu lock needed here.
     */
    g_hash_table_iter_init(&hiod_it, s->vtd_host_iommu_dev);
    while (g_hash_table_iter_next(&hiod_it, NULL, (void **)&vtd_hiod)) {
        if (!object_dynamic_cast(OBJECT(vtd_hiod->hiod),
                                 TYPE_HOST_IOMMU_DEVICE_IOMMUFD)) {
            continue;
        }

        /*
         * The replay path inevitably needs to iterate through existing
         * PASID cache entries. Since cached PASID entries that are marked
         * for removal don't need to be iterated, we intentionally handle
         * removals before additions to optimize the replay process.
         */
        vtd_accel_pasid_cache_invalidate(vtd_hiod, pc_info);

        if (pc_info->accel_pce_deleted) {
            pc_info->accel_pce_deleted = false;
        } else {
            vtd_accel_replay_pasid_bind_for_dev(vtd_hiod, start, end, pc_info);
        }
    }
}

/* Fake a global pasid cache invalidation to remove all pasid cache entries */
void vtd_accel_pasid_cache_reset(IntelIOMMUState *s)
{
    VTDPASIDCacheInfo pc_info = { .type = VTD_INV_DESC_PASIDC_G_GLOBAL };
    VTDHostIOMMUDevice *vtd_hiod;
    GHashTableIter hiod_it;

    g_hash_table_iter_init(&hiod_it, s->vtd_host_iommu_dev);
    while (g_hash_table_iter_next(&hiod_it, NULL, (void **)&vtd_hiod)) {
        vtd_accel_pasid_cache_invalidate(vtd_hiod, &pc_info);
    }
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
