/*
 * generic functions used by VFIO devices
 *
 * Copyright Red Hat, Inc. 2012
 *
 * Authors:
 *  Alex Williamson <alex.williamson@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Based on qemu-kvm device-assignment:
 *  Adapted for KVM by Qumranet.
 *  Copyright (c) 2007, Neocleus, Alex Novik (alex@neocleus.com)
 *  Copyright (c) 2007, Neocleus, Guy Zana (guy@neocleus.com)
 *  Copyright (C) 2008, Qumranet, Amit Shah (amit.shah@qumranet.com)
 *  Copyright (C) 2008, Red Hat, Amit Shah (amit.shah@redhat.com)
 *  Copyright (C) 2008, IBM, Muli Ben-Yehuda (muli@il.ibm.com)
 */

#include "qemu/osdep.h"
#include <sys/ioctl.h>
#ifdef CONFIG_KVM
#include <linux/kvm.h>
#endif
#include <linux/vfio.h>

#include "exec/target_page.h"
#include "hw/vfio/vfio-device.h"
#include "hw/vfio/pci.h"
#include "system/address-spaces.h"
#include "system/memory.h"
#include "hw/hw.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qemu/range.h"
#include "system/kvm.h"
#include "system/reset.h"
#include "system/runstate.h"
#include "trace.h"
#include "qapi/error.h"
#include "migration/misc.h"
#include "migration/qemu-file.h"
#include "system/tcg.h"
#include "system/tpm.h"
#include "vfio-migration-internal.h"
#include "vfio-helpers.h"
#include "vfio-listener.h"

/*
 * Device state interfaces
 */


static bool vfio_log_sync_needed(const VFIOContainer *bcontainer)
{
    VFIODevice *vbasedev;

    if (!vfio_container_dirty_tracking_is_started(bcontainer)) {
        return false;
    }

    QLIST_FOREACH(vbasedev, &bcontainer->device_list, container_next) {
        VFIOMigration *migration = vbasedev->migration;

        if (!migration) {
            return false;
        }

        if (vbasedev->pre_copy_dirty_page_tracking == ON_OFF_AUTO_OFF &&
            (vfio_device_state_is_running(vbasedev) ||
             vfio_device_state_is_precopy(vbasedev))) {
            return false;
        }
    }
    return true;
}

static bool vfio_listener_skipped_section(MemoryRegionSection *section)
{
    return (!memory_region_is_ram(section->mr) &&
            !memory_region_is_iommu(section->mr)) ||
           memory_region_is_protected(section->mr) ||
           /*
            * Sizing an enabled 64-bit BAR can cause spurious mappings to
            * addresses in the upper part of the 64-bit address space.  These
            * are never accessed by the CPU and beyond the address width of
            * some IOMMU hardware.  TODO: VFIO should tell us the IOMMU width.
            */
           section->offset_within_address_space & (1ULL << 63);
}

/*
 * Called with rcu_read_lock held.
 * The returned MemoryRegion must not be accessed after calling rcu_read_unlock.
 */
static MemoryRegion *vfio_translate_iotlb(IOMMUTLBEntry *iotlb, hwaddr *xlat_p,
                                          Error **errp)
{
    MemoryRegion *mr;

    mr = memory_translate_iotlb(iotlb, xlat_p, errp);
    if (mr && memory_region_has_ram_discard_manager(mr)) {
        /*
         * Malicious VMs might trigger discarding of IOMMU-mapped memory. The
         * pages will remain pinned inside vfio until unmapped, resulting in a
         * higher memory consumption than expected. If memory would get
         * populated again later, there would be an inconsistency between pages
         * pinned by vfio and pages seen by QEMU. This is the case until
         * unmapped from the IOMMU (e.g., during device reset).
         *
         * With malicious guests, we really only care about pinning more memory
         * than expected. RLIMIT_MEMLOCK set for the user/process can never be
         * exceeded and can be used to mitigate this problem.
         */
        warn_report_once("Using vfio with vIOMMUs and coordinated discarding of"
                         " RAM (e.g., virtio-mem) works, however, malicious"
                         " guests can trigger pinning of more memory than"
                         " intended via an IOMMU. It's possible to mitigate "
                         " by setting/adjusting RLIMIT_MEMLOCK.");
    }
    return mr;
}

static void vfio_iommu_map_notify(IOMMUNotifier *n, IOMMUTLBEntry *iotlb)
{
    VFIOGuestIOMMU *giommu = container_of(n, VFIOGuestIOMMU, n);
    VFIOContainer *bcontainer = giommu->bcontainer;
    hwaddr iova = iotlb->iova + giommu->iommu_offset;
    MemoryRegion *mr;
    hwaddr xlat;
    void *vaddr;
    int ret;
    Error *local_err = NULL;

    trace_vfio_iommu_map_notify(iotlb->perm == IOMMU_NONE ? "UNMAP" : "MAP",
                                iova, iova + iotlb->addr_mask);

    if (iotlb->target_as != &address_space_memory) {
        error_setg(&local_err,
                   "Wrong target AS \"%s\", only system memory is allowed",
                   iotlb->target_as->name ? iotlb->target_as->name : "none");
        if (migration_is_running()) {
            migration_file_set_error(-EINVAL, local_err);
        } else {
            error_report_err(local_err);
        }
        return;
    }

    rcu_read_lock();

    if ((iotlb->perm & IOMMU_RW) != IOMMU_NONE) {
        bool read_only;

        mr = vfio_translate_iotlb(iotlb, &xlat, &local_err);
        if (!mr) {
            error_report_err(local_err);
            goto out;
        }
        vaddr = memory_region_get_ram_ptr(mr) + xlat;
        read_only = !(iotlb->perm & IOMMU_WO) || mr->readonly;

        /*
         * vaddr is only valid until rcu_read_unlock(). But after
         * vfio_dma_map has set up the mapping the pages will be
         * pinned by the kernel. This makes sure that the RAM backend
         * of vaddr will always be there, even if the memory object is
         * destroyed and its backing memory munmap-ed.
         */
        ret = vfio_container_dma_map(bcontainer, iova,
                                     iotlb->addr_mask + 1, vaddr,
                                     read_only, mr);
        if (ret) {
            error_report("vfio_container_dma_map(%p, 0x%"HWADDR_PRIx", "
                         "0x%"HWADDR_PRIx", %p) = %d (%s)",
                         bcontainer, iova,
                         iotlb->addr_mask + 1, vaddr, ret, strerror(-ret));
        }
    } else {
        ret = vfio_container_dma_unmap(bcontainer, iova,
                                       iotlb->addr_mask + 1, iotlb, false);
        if (ret) {
            error_setg(&local_err,
                       "vfio_container_dma_unmap(%p, 0x%"HWADDR_PRIx", "
                       "0x%"HWADDR_PRIx") = %d (%s)",
                       bcontainer, iova,
                       iotlb->addr_mask + 1, ret, strerror(-ret));
            if (migration_is_running()) {
                migration_file_set_error(ret, local_err);
            } else {
                error_report_err(local_err);
            }
        }
    }
out:
    rcu_read_unlock();
}

static void vfio_ram_discard_notify_discard(RamDiscardListener *rdl,
                                            MemoryRegionSection *section)
{
    VFIORamDiscardListener *vrdl = container_of(rdl, VFIORamDiscardListener,
                                                listener);
    VFIOContainer *bcontainer = vrdl->bcontainer;
    const hwaddr size = int128_get64(section->size);
    const hwaddr iova = section->offset_within_address_space;
    int ret;

    /* Unmap with a single call. */
    ret = vfio_container_dma_unmap(bcontainer, iova, size , NULL, false);
    if (ret) {
        error_report("%s: vfio_container_dma_unmap() failed: %s", __func__,
                     strerror(-ret));
    }
}

static int vfio_ram_discard_notify_populate(RamDiscardListener *rdl,
                                            MemoryRegionSection *section)
{
    VFIORamDiscardListener *vrdl = container_of(rdl, VFIORamDiscardListener,
                                                listener);
    VFIOContainer *bcontainer = vrdl->bcontainer;
    const hwaddr end = section->offset_within_region +
                       int128_get64(section->size);
    hwaddr start, next, iova;
    void *vaddr;
    int ret;

    /*
     * Map in (aligned within memory region) minimum granularity, so we can
     * unmap in minimum granularity later.
     */
    for (start = section->offset_within_region; start < end; start = next) {
        next = ROUND_UP(start + 1, vrdl->granularity);
        next = MIN(next, end);

        iova = start - section->offset_within_region +
               section->offset_within_address_space;
        vaddr = memory_region_get_ram_ptr(section->mr) + start;

        ret = vfio_container_dma_map(bcontainer, iova, next - start,
                                     vaddr, section->readonly, section->mr);
        if (ret) {
            /* Rollback */
            vfio_ram_discard_notify_discard(rdl, section);
            return ret;
        }
    }
    return 0;
}

static bool vfio_ram_discard_register_listener(VFIOContainer *bcontainer,
                                               MemoryRegionSection *section,
                                               Error **errp)
{
    RamDiscardManager *rdm = memory_region_get_ram_discard_manager(section->mr);
    int target_page_size = qemu_target_page_size();
    VFIORamDiscardListener *vrdl;

    /* Ignore some corner cases not relevant in practice. */
    g_assert(QEMU_IS_ALIGNED(section->offset_within_region, target_page_size));
    g_assert(QEMU_IS_ALIGNED(section->offset_within_address_space,
                             target_page_size));
    g_assert(QEMU_IS_ALIGNED(int128_get64(section->size), target_page_size));

    vrdl = g_new0(VFIORamDiscardListener, 1);
    vrdl->bcontainer = bcontainer;
    vrdl->mr = section->mr;
    vrdl->offset_within_address_space = section->offset_within_address_space;
    vrdl->size = int128_get64(section->size);
    vrdl->granularity = ram_discard_manager_get_min_granularity(rdm,
                                                                section->mr);

    g_assert(vrdl->granularity && is_power_of_2(vrdl->granularity));
    g_assert(bcontainer->pgsizes &&
             vrdl->granularity >= 1ULL << ctz64(bcontainer->pgsizes));

    ram_discard_listener_init(&vrdl->listener,
                              vfio_ram_discard_notify_populate,
                              vfio_ram_discard_notify_discard, true);
    ram_discard_manager_register_listener(rdm, &vrdl->listener, section);
    QLIST_INSERT_HEAD(&bcontainer->vrdl_list, vrdl, next);

    /*
     * Sanity-check if we have a theoretically problematic setup where we could
     * exceed the maximum number of possible DMA mappings over time. We assume
     * that each mapped section in the same address space as a RamDiscardManager
     * section consumes exactly one DMA mapping, with the exception of
     * RamDiscardManager sections; i.e., we don't expect to have gIOMMU sections
     * in the same address space as RamDiscardManager sections.
     *
     * We assume that each section in the address space consumes one memslot.
     * We take the number of KVM memory slots as a best guess for the maximum
     * number of sections in the address space we could have over time,
     * also consuming DMA mappings.
     */
    if (bcontainer->dma_max_mappings) {
        unsigned int vrdl_count = 0, vrdl_mappings = 0, max_memslots = 512;

#ifdef CONFIG_KVM
        if (kvm_enabled()) {
            max_memslots = kvm_get_max_memslots();
        }
#endif

        QLIST_FOREACH(vrdl, &bcontainer->vrdl_list, next) {
            hwaddr start, end;

            start = QEMU_ALIGN_DOWN(vrdl->offset_within_address_space,
                                    vrdl->granularity);
            end = ROUND_UP(vrdl->offset_within_address_space + vrdl->size,
                           vrdl->granularity);
            vrdl_mappings += (end - start) / vrdl->granularity;
            vrdl_count++;
        }

        if (vrdl_mappings + max_memslots - vrdl_count >
            bcontainer->dma_max_mappings) {
            error_setg(errp, "%s: possibly running out of DMA mappings. E.g., try"
                        " increasing the 'block-size' of virtio-mem devies."
                        " Maximum possible DMA mappings: %d, Maximum possible"
                        " memslots: %d", __func__, bcontainer->dma_max_mappings,
                        max_memslots);
            return false;
        }
    }
    return true;
}

static void vfio_ram_discard_unregister_listener(VFIOContainer *bcontainer,
                                                 MemoryRegionSection *section)
{
    RamDiscardManager *rdm = memory_region_get_ram_discard_manager(section->mr);
    VFIORamDiscardListener *vrdl = NULL;

    QLIST_FOREACH(vrdl, &bcontainer->vrdl_list, next) {
        if (vrdl->mr == section->mr &&
            vrdl->offset_within_address_space ==
            section->offset_within_address_space) {
            break;
        }
    }

    if (!vrdl) {
        hw_error("vfio: Trying to unregister missing RAM discard listener");
    }

    ram_discard_manager_unregister_listener(rdm, &vrdl->listener);
    QLIST_REMOVE(vrdl, next);
    g_free(vrdl);
}

static bool vfio_known_safe_misalignment(MemoryRegionSection *section)
{
    MemoryRegion *mr = section->mr;

    if (!TPM_IS_CRB(mr->owner)) {
        return false;
    }

    /* this is a known safe misaligned region, just trace for debug purpose */
    trace_vfio_known_safe_misalignment(memory_region_name(mr),
                                       section->offset_within_address_space,
                                       section->offset_within_region,
                                       qemu_real_host_page_size());
    return true;
}

static bool vfio_listener_valid_section(MemoryRegionSection *section,
                                        const char *name)
{
    if (vfio_listener_skipped_section(section)) {
        trace_vfio_listener_region_skip(name,
                section->offset_within_address_space,
                section->offset_within_address_space +
                int128_get64(int128_sub(section->size, int128_one())));
        return false;
    }

    if (unlikely((section->offset_within_address_space &
                  ~qemu_real_host_page_mask()) !=
                 (section->offset_within_region & ~qemu_real_host_page_mask()))) {
        if (!vfio_known_safe_misalignment(section)) {
            error_report("%s received unaligned region %s iova=0x%"PRIx64
                         " offset_within_region=0x%"PRIx64
                         " qemu_real_host_page_size=0x%"PRIxPTR,
                         __func__, memory_region_name(section->mr),
                         section->offset_within_address_space,
                         section->offset_within_region,
                         qemu_real_host_page_size());
        }
        return false;
    }

    return true;
}

static bool vfio_get_section_iova_range(VFIOContainer *bcontainer,
                                        MemoryRegionSection *section,
                                        hwaddr *out_iova, hwaddr *out_end,
                                        Int128 *out_llend)
{
    Int128 llend;
    hwaddr iova;

    iova = REAL_HOST_PAGE_ALIGN(section->offset_within_address_space);
    llend = int128_make64(section->offset_within_address_space);
    llend = int128_add(llend, section->size);
    llend = int128_and(llend, int128_exts64(qemu_real_host_page_mask()));

    if (int128_ge(int128_make64(iova), llend)) {
        return false;
    }

    *out_iova = iova;
    *out_end = int128_get64(int128_sub(llend, int128_one()));
    if (out_llend) {
        *out_llend = llend;
    }
    return true;
}

static void vfio_listener_begin(MemoryListener *listener)
{
    VFIOContainer *bcontainer = container_of(listener, VFIOContainer,
                                             listener);
    void (*listener_begin)(VFIOContainer *bcontainer);

    listener_begin = VFIO_IOMMU_GET_CLASS(bcontainer)->listener_begin;

    if (listener_begin) {
        listener_begin(bcontainer);
    }
}

static void vfio_listener_commit(MemoryListener *listener)
{
    VFIOContainer *bcontainer = container_of(listener, VFIOContainer,
                                             listener);
    void (*listener_commit)(VFIOContainer *bcontainer);

    listener_commit = VFIO_IOMMU_GET_CLASS(bcontainer)->listener_commit;

    if (listener_commit) {
        listener_commit(bcontainer);
    }
}

static void vfio_device_error_append(VFIODevice *vbasedev, Error **errp)
{
    /*
     * MMIO region mapping failures are not fatal but in this case PCI
     * peer-to-peer transactions are broken.
     */
    if (vfio_pci_from_vfio_device(vbasedev)) {
        error_append_hint(errp, "%s: PCI peer-to-peer transactions "
                          "on BARs are not supported.\n", vbasedev->name);
    }
}

VFIORamDiscardListener *vfio_find_ram_discard_listener(
    VFIOContainer *bcontainer, MemoryRegionSection *section)
{
    VFIORamDiscardListener *vrdl = NULL;

    QLIST_FOREACH(vrdl, &bcontainer->vrdl_list, next) {
        if (vrdl->mr == section->mr &&
            vrdl->offset_within_address_space ==
            section->offset_within_address_space) {
            break;
        }
    }

    if (!vrdl) {
        hw_error("vfio: Trying to sync missing RAM discard listener");
        /* does not return */
    }
    return vrdl;
}

static void vfio_listener_region_add(MemoryListener *listener,
                                     MemoryRegionSection *section)
{
    VFIOContainer *bcontainer = container_of(listener, VFIOContainer,
                                             listener);
    vfio_container_region_add(bcontainer, section, false);
}

void vfio_container_region_add(VFIOContainer *bcontainer,
                               MemoryRegionSection *section,
                               bool cpr_remap)
{
    hwaddr iova, end;
    Int128 llend, llsize;
    void *vaddr;
    int ret;
    Error *err = NULL;

    if (!vfio_listener_valid_section(section, "region_add")) {
        return;
    }

    if (!vfio_get_section_iova_range(bcontainer, section, &iova, &end,
                                     &llend)) {
        if (memory_region_is_ram_device(section->mr)) {
            trace_vfio_listener_region_add_no_dma_map(
                memory_region_name(section->mr),
                section->offset_within_address_space,
                int128_getlo(section->size),
                qemu_real_host_page_size());
        }
        return;
    }

    /* PPC64/pseries machine only */
    if (!vfio_container_add_section_window(bcontainer, section, &err)) {
        goto mmio_dma_error;
    }

    memory_region_ref(section->mr);

    if (memory_region_is_iommu(section->mr)) {
        VFIOGuestIOMMU *giommu;
        IOMMUMemoryRegion *iommu_mr = IOMMU_MEMORY_REGION(section->mr);
        int iommu_idx;

        trace_vfio_listener_region_add_iommu(section->mr->name, iova, end);

        if (cpr_remap) {
            vfio_cpr_giommu_remap(bcontainer, section);
        }

        /*
         * FIXME: For VFIO iommu types which have KVM acceleration to
         * avoid bouncing all map/unmaps through qemu this way, this
         * would be the right place to wire that up (tell the KVM
         * device emulation the VFIO iommu handles to use).
         */
        giommu = g_malloc0(sizeof(*giommu));
        giommu->iommu_mr = iommu_mr;
        giommu->iommu_offset = section->offset_within_address_space -
                               section->offset_within_region;
        giommu->bcontainer = bcontainer;
        llend = int128_add(int128_make64(section->offset_within_region),
                           section->size);
        llend = int128_sub(llend, int128_one());
        iommu_idx = memory_region_iommu_attrs_to_index(iommu_mr,
                                                       MEMTXATTRS_UNSPECIFIED);
        iommu_notifier_init(&giommu->n, vfio_iommu_map_notify,
                            IOMMU_NOTIFIER_IOTLB_EVENTS,
                            section->offset_within_region,
                            int128_get64(llend),
                            iommu_idx);

        ret = memory_region_register_iommu_notifier(section->mr, &giommu->n,
                                                    &err);
        if (ret) {
            g_free(giommu);
            goto fail;
        }
        QLIST_INSERT_HEAD(&bcontainer->giommu_list, giommu, giommu_next);
        memory_region_iommu_replay(giommu->iommu_mr, &giommu->n);

        return;
    }

    /* Here we assume that memory_region_is_ram(section->mr)==true */

    /*
     * For RAM memory regions with a RamDiscardManager, we only want to map the
     * actually populated parts - and update the mapping whenever we're notified
     * about changes.
     */
    if (memory_region_has_ram_discard_manager(section->mr)) {
        if (!cpr_remap) {
            if (!vfio_ram_discard_register_listener(bcontainer, section, &err)) {
                goto fail;
            }
        } else if (!vfio_cpr_ram_discard_replay_populated(bcontainer,
                                                          section)) {
            error_setg(&err,
                       "vfio_cpr_ram_discard_register_listener for %s failed",
                       memory_region_name(section->mr));
            goto fail;
        }
        return;
    }

    vaddr = memory_region_get_ram_ptr(section->mr) +
            section->offset_within_region +
            (iova - section->offset_within_address_space);

    trace_vfio_listener_region_add_ram(iova, end, vaddr);

    llsize = int128_sub(llend, int128_make64(iova));

    if (memory_region_is_ram_device(section->mr)) {
        hwaddr pgmask = (1ULL << ctz64(bcontainer->pgsizes)) - 1;

        if ((iova & pgmask) || (int128_get64(llsize) & pgmask)) {
            trace_vfio_listener_region_add_no_dma_map(
                memory_region_name(section->mr),
                section->offset_within_address_space,
                int128_getlo(section->size),
                pgmask + 1);
            return;
        }
    }

    ret = vfio_container_dma_map(bcontainer, iova, int128_get64(llsize),
                                 vaddr, section->readonly, section->mr);
    if (ret) {
        error_setg(&err, "vfio_container_dma_map(%p, 0x%"HWADDR_PRIx", "
                   "0x%"HWADDR_PRIx", %p) = %d (%s)",
                   bcontainer, iova, int128_get64(llsize), vaddr, ret,
                   strerror(-ret));
    mmio_dma_error:
        if (memory_region_is_ram_device(section->mr)) {
            /* Allow unexpected mappings not to be fatal for RAM devices */
            VFIODevice *vbasedev =
                vfio_get_vfio_device(memory_region_owner(section->mr));
            vfio_device_error_append(vbasedev, &err);
            warn_report_err_once(err);
            return;
        }
        goto fail;
    }

    return;

fail:
    if (!bcontainer->initialized) {
        /*
         * At machine init time or when the device is attached to the
         * VM, store the first error in the container so we can
         * gracefully fail the device realize routine.
         */
        if (!bcontainer->error) {
            error_propagate_prepend(&bcontainer->error, err,
                                    "Region %s: ",
                                    memory_region_name(section->mr));
        } else {
            error_free(err);
        }
    } else {
        /*
         * At runtime, there's not much we can do other than throw a
         * hardware error.
         */
        error_report_err(err);
        hw_error("vfio: DMA mapping failed, unable to continue");
    }
}

static void vfio_listener_region_del(MemoryListener *listener,
                                     MemoryRegionSection *section)
{
    VFIOContainer *bcontainer = container_of(listener, VFIOContainer,
                                             listener);
    hwaddr iova, end;
    Int128 llend, llsize;
    int ret;
    bool try_unmap = true;

    if (!vfio_listener_valid_section(section, "region_del")) {
        return;
    }

    if (memory_region_is_iommu(section->mr)) {
        VFIOGuestIOMMU *giommu;

        trace_vfio_listener_region_del_iommu(section->mr->name);
        QLIST_FOREACH(giommu, &bcontainer->giommu_list, giommu_next) {
            if (MEMORY_REGION(giommu->iommu_mr) == section->mr &&
                giommu->n.start == section->offset_within_region) {
                memory_region_unregister_iommu_notifier(section->mr,
                                                        &giommu->n);
                QLIST_REMOVE(giommu, giommu_next);
                g_free(giommu);
                break;
            }
        }

        /*
         * FIXME: We assume the one big unmap below is adequate to
         * remove any individual page mappings in the IOMMU which
         * might have been copied into VFIO. This works for a page table
         * based IOMMU where a big unmap flattens a large range of IO-PTEs.
         * That may not be true for all IOMMU types.
         */
    }

    if (!vfio_get_section_iova_range(bcontainer, section, &iova, &end,
                                     &llend)) {
        return;
    }

    llsize = int128_sub(llend, int128_make64(iova));

    trace_vfio_listener_region_del(iova, end);

    if (memory_region_is_ram_device(section->mr)) {
        hwaddr pgmask;

        pgmask = (1ULL << ctz64(bcontainer->pgsizes)) - 1;
        try_unmap = !((iova & pgmask) || (int128_get64(llsize) & pgmask));
    } else if (memory_region_has_ram_discard_manager(section->mr)) {
        vfio_ram_discard_unregister_listener(bcontainer, section);
        /* Unregistering will trigger an unmap. */
        try_unmap = false;
    }

    if (try_unmap) {
        bool unmap_all = false;

        if (int128_eq(llsize, int128_2_64())) {
            assert(!iova);
            unmap_all = true;
            llsize = int128_zero();
        }
        ret = vfio_container_dma_unmap(bcontainer, iova, int128_get64(llsize),
                                       NULL, unmap_all);
        if (ret) {
            error_report("vfio_container_dma_unmap(%p, 0x%"HWADDR_PRIx", "
                         "0x%"HWADDR_PRIx") = %d (%s)",
                         bcontainer, iova, int128_get64(llsize), ret,
                         strerror(-ret));
        }
    }

    memory_region_unref(section->mr);

    /* PPC64/pseries machine only */
    vfio_container_del_section_window(bcontainer, section);
}

typedef struct VFIODirtyRanges {
    hwaddr min32;
    hwaddr max32;
    hwaddr min64;
    hwaddr max64;
    hwaddr minpci64;
    hwaddr maxpci64;
} VFIODirtyRanges;

typedef struct VFIODirtyRangesListener {
    VFIOContainer *bcontainer;
    VFIODirtyRanges ranges;
    MemoryListener listener;
} VFIODirtyRangesListener;

static bool vfio_section_is_vfio_pci(MemoryRegionSection *section,
                                     VFIOContainer *bcontainer)
{
    VFIOPCIDevice *pcidev;
    VFIODevice *vbasedev;
    Object *owner;

    owner = memory_region_owner(section->mr);

    QLIST_FOREACH(vbasedev, &bcontainer->device_list, container_next) {
        if (!vfio_pci_from_vfio_device(vbasedev)) {
            continue;
        }
        pcidev = container_of(vbasedev, VFIOPCIDevice, vbasedev);
        if (OBJECT(pcidev) == owner) {
            return true;
        }
    }

    return false;
}

static void vfio_dirty_tracking_update_range(VFIODirtyRanges *range,
                                             hwaddr iova, hwaddr end,
                                             bool update_pci)
{
    hwaddr *min, *max;

    /*
     * The address space passed to the dirty tracker is reduced to three ranges:
     * one for 32-bit DMA ranges, one for 64-bit DMA ranges and one for the
     * PCI 64-bit hole.
     *
     * The underlying reports of dirty will query a sub-interval of each of
     * these ranges.
     *
     * The purpose of the three range handling is to handle known cases of big
     * holes in the address space, like the x86 AMD 1T hole, and firmware (like
     * OVMF) which may relocate the pci-hole64 to the end of the address space.
     * The latter would otherwise generate large ranges for tracking, stressing
     * the limits of supported hardware. The pci-hole32 will always be below 4G
     * (overlapping or not) so it doesn't need special handling and is part of
     * the 32-bit range.
     *
     * The alternative would be an IOVATree but that has a much bigger runtime
     * overhead and unnecessary complexity.
     */
    if (update_pci && iova >= UINT32_MAX) {
        min = &range->minpci64;
        max = &range->maxpci64;
    } else {
        min = (end <= UINT32_MAX) ? &range->min32 : &range->min64;
        max = (end <= UINT32_MAX) ? &range->max32 : &range->max64;
    }
    if (*min > iova) {
        *min = iova;
    }
    if (*max < end) {
        *max = end;
    }

    trace_vfio_device_dirty_tracking_update(iova, end, *min, *max);
}

static void vfio_dirty_tracking_update(MemoryListener *listener,
                                       MemoryRegionSection *section)
{
    VFIODirtyRangesListener *dirty =
        container_of(listener, VFIODirtyRangesListener, listener);
    hwaddr iova, end;

    if (!vfio_listener_valid_section(section, "tracking_update") ||
        !vfio_get_section_iova_range(dirty->bcontainer, section,
                                     &iova, &end, NULL)) {
        return;
    }

    vfio_dirty_tracking_update_range(&dirty->ranges, iova, end,
                      vfio_section_is_vfio_pci(section, dirty->bcontainer));
}

static const MemoryListener vfio_dirty_tracking_listener = {
    .name = "vfio-tracking",
    .region_add = vfio_dirty_tracking_update,
};

static void vfio_dirty_tracking_init(VFIOContainer *bcontainer,
                                     VFIODirtyRanges *ranges)
{
    VFIODirtyRangesListener dirty;

    memset(&dirty, 0, sizeof(dirty));
    dirty.ranges.min32 = UINT32_MAX;
    dirty.ranges.min64 = UINT64_MAX;
    dirty.ranges.minpci64 = UINT64_MAX;
    dirty.listener = vfio_dirty_tracking_listener;
    dirty.bcontainer = bcontainer;

    memory_listener_register(&dirty.listener,
                             bcontainer->space->as);

    *ranges = dirty.ranges;

    /*
     * The memory listener is synchronous, and used to calculate the range
     * to dirty tracking. Unregister it after we are done as we are not
     * interested in any follow-up updates.
     */
    memory_listener_unregister(&dirty.listener);
}

static void vfio_devices_dma_logging_stop(VFIOContainer *bcontainer)
{
    uint64_t buf[DIV_ROUND_UP(sizeof(struct vfio_device_feature),
                              sizeof(uint64_t))] = {};
    struct vfio_device_feature *feature = (struct vfio_device_feature *)buf;
    VFIODevice *vbasedev;

    feature->argsz = sizeof(buf);
    feature->flags = VFIO_DEVICE_FEATURE_SET |
                     VFIO_DEVICE_FEATURE_DMA_LOGGING_STOP;

    QLIST_FOREACH(vbasedev, &bcontainer->device_list, container_next) {
        int ret;

        if (!vbasedev->dirty_tracking) {
            continue;
        }

        ret = vbasedev->io_ops->device_feature(vbasedev, feature);

        if (ret != 0) {
            warn_report("%s: Failed to stop DMA logging, err %d (%s)",
                        vbasedev->name, -ret, strerror(-ret));
        }
        vbasedev->dirty_tracking = false;
    }
}

static struct vfio_device_feature *
vfio_device_feature_dma_logging_start_create(VFIOContainer *bcontainer,
                                             VFIODirtyRanges *tracking)
{
    struct vfio_device_feature *feature;
    size_t feature_size;
    struct vfio_device_feature_dma_logging_control *control;
    struct vfio_device_feature_dma_logging_range *ranges;

    feature_size = sizeof(struct vfio_device_feature) +
                   sizeof(struct vfio_device_feature_dma_logging_control);
    feature = g_try_malloc0(feature_size);
    if (!feature) {
        errno = ENOMEM;
        return NULL;
    }
    feature->argsz = feature_size;
    feature->flags = VFIO_DEVICE_FEATURE_SET |
                     VFIO_DEVICE_FEATURE_DMA_LOGGING_START;

    control = (struct vfio_device_feature_dma_logging_control *)feature->data;
    control->page_size = qemu_real_host_page_size();

    /*
     * DMA logging uAPI guarantees to support at least a number of ranges that
     * fits into a single host kernel base page.
     */
    control->num_ranges = !!tracking->max32 + !!tracking->max64 +
        !!tracking->maxpci64;
    ranges = g_try_new0(struct vfio_device_feature_dma_logging_range,
                        control->num_ranges);
    if (!ranges) {
        g_free(feature);
        errno = ENOMEM;

        return NULL;
    }

    control->ranges = (uintptr_t)ranges;
    if (tracking->max32) {
        ranges->iova = tracking->min32;
        ranges->length = (tracking->max32 - tracking->min32) + 1;
        ranges++;
    }
    if (tracking->max64) {
        ranges->iova = tracking->min64;
        ranges->length = (tracking->max64 - tracking->min64) + 1;
        ranges++;
    }
    if (tracking->maxpci64) {
        ranges->iova = tracking->minpci64;
        ranges->length = (tracking->maxpci64 - tracking->minpci64) + 1;
    }

    trace_vfio_device_dirty_tracking_start(control->num_ranges,
                                           tracking->min32, tracking->max32,
                                           tracking->min64, tracking->max64,
                                           tracking->minpci64, tracking->maxpci64);

    return feature;
}

static void vfio_device_feature_dma_logging_start_destroy(
    struct vfio_device_feature *feature)
{
    struct vfio_device_feature_dma_logging_control *control =
        (struct vfio_device_feature_dma_logging_control *)feature->data;
    struct vfio_device_feature_dma_logging_range *ranges =
        (struct vfio_device_feature_dma_logging_range *)(uintptr_t)control->ranges;

    g_free(ranges);
    g_free(feature);
}

static bool vfio_devices_dma_logging_start(VFIOContainer *bcontainer,
                                          Error **errp)
{
    struct vfio_device_feature *feature;
    VFIODirtyRanges ranges;
    VFIODevice *vbasedev;
    int ret = 0;

    vfio_dirty_tracking_init(bcontainer, &ranges);
    feature = vfio_device_feature_dma_logging_start_create(bcontainer,
                                                           &ranges);
    if (!feature) {
        error_setg_errno(errp, errno, "Failed to prepare DMA logging");
        return false;
    }

    QLIST_FOREACH(vbasedev, &bcontainer->device_list, container_next) {
        if (vbasedev->dirty_tracking) {
            continue;
        }

        ret = vbasedev->io_ops->device_feature(vbasedev, feature);
        if (ret) {
            error_setg_errno(errp, -ret, "%s: Failed to start DMA logging",
                             vbasedev->name);
            goto out;
        }
        vbasedev->dirty_tracking = true;
    }

out:
    if (ret) {
        vfio_devices_dma_logging_stop(bcontainer);
    }

    vfio_device_feature_dma_logging_start_destroy(feature);

    return ret == 0;
}

static bool vfio_listener_log_global_start(MemoryListener *listener,
                                           Error **errp)
{
    ERRP_GUARD();
    VFIOContainer *bcontainer = container_of(listener, VFIOContainer,
                                             listener);
    bool ret;

    if (vfio_container_devices_dirty_tracking_is_supported(bcontainer)) {
        ret = vfio_devices_dma_logging_start(bcontainer, errp);
    } else {
        ret = vfio_container_set_dirty_page_tracking(bcontainer, true, errp) == 0;
    }

    if (!ret) {
        error_prepend(errp, "vfio: Could not start dirty page tracking - ");
    }
    return ret;
}

static void vfio_listener_log_global_stop(MemoryListener *listener)
{
    VFIOContainer *bcontainer = container_of(listener, VFIOContainer,
                                             listener);
    Error *local_err = NULL;
    int ret = 0;

    if (vfio_container_devices_dirty_tracking_is_supported(bcontainer)) {
        vfio_devices_dma_logging_stop(bcontainer);
    } else {
        ret = vfio_container_set_dirty_page_tracking(bcontainer, false,
                                                     &local_err);
    }

    if (ret) {
        error_prepend(&local_err,
                      "vfio: Could not stop dirty page tracking - ");
        if (migration_is_running()) {
            migration_file_set_error(ret, local_err);
        } else {
            error_report_err(local_err);
        }
    }
}

typedef struct {
    IOMMUNotifier n;
    VFIOGuestIOMMU *giommu;
} vfio_giommu_dirty_notifier;

static void vfio_iommu_map_dirty_notify(IOMMUNotifier *n, IOMMUTLBEntry *iotlb)
{
    vfio_giommu_dirty_notifier *gdn = container_of(n,
                                                vfio_giommu_dirty_notifier, n);
    VFIOGuestIOMMU *giommu = gdn->giommu;
    VFIOContainer *bcontainer = giommu->bcontainer;
    hwaddr iova = iotlb->iova + giommu->iommu_offset;
    hwaddr translated_addr;
    Error *local_err = NULL;
    int ret = -EINVAL;
    MemoryRegion *mr;
    hwaddr xlat;

    trace_vfio_iommu_map_dirty_notify(iova, iova + iotlb->addr_mask);

    if (iotlb->target_as != &address_space_memory) {
        error_setg(&local_err,
                   "Wrong target AS \"%s\", only system memory is allowed",
                   iotlb->target_as->name ? iotlb->target_as->name : "none");
        goto out;
    }

    rcu_read_lock();
    mr = vfio_translate_iotlb(iotlb, &xlat, &local_err);
    if (!mr) {
        goto out_unlock;
    }
    translated_addr = memory_region_get_ram_addr(mr) + xlat;

    ret = vfio_container_query_dirty_bitmap(bcontainer, iova, iotlb->addr_mask + 1,
                                translated_addr, &local_err);
    if (ret) {
        error_prepend(&local_err,
                      "vfio_iommu_map_dirty_notify(%p, 0x%"HWADDR_PRIx", "
                      "0x%"HWADDR_PRIx") failed - ", bcontainer, iova,
                      iotlb->addr_mask + 1);
    }

out_unlock:
    rcu_read_unlock();

out:
    if (ret) {
        if (migration_is_running()) {
            migration_file_set_error(ret, local_err);
        } else {
            error_report_err(local_err);
        }
    }
}

static int vfio_ram_discard_query_dirty_bitmap(MemoryRegionSection *section,
                                             void *opaque)
{
    const hwaddr size = int128_get64(section->size);
    const hwaddr iova = section->offset_within_address_space;
    const hwaddr translated_addr = memory_region_get_ram_addr(section->mr) +
                                   section->offset_within_region;
    VFIORamDiscardListener *vrdl = opaque;
    Error *local_err = NULL;
    int ret;

    /*
     * Sync the whole mapped region (spanning multiple individual mappings)
     * in one go.
     */
    ret = vfio_container_query_dirty_bitmap(vrdl->bcontainer, iova, size,
                                            translated_addr, &local_err);
    if (ret) {
        error_report_err(local_err);
    }
    return ret;
}

static int
vfio_sync_ram_discard_listener_dirty_bitmap(VFIOContainer *bcontainer,
                                            MemoryRegionSection *section)
{
    RamDiscardManager *rdm = memory_region_get_ram_discard_manager(section->mr);
    VFIORamDiscardListener *vrdl =
        vfio_find_ram_discard_listener(bcontainer, section);

    /*
     * We only want/can synchronize the bitmap for actually mapped parts -
     * which correspond to populated parts. Replay all populated parts.
     */
    return ram_discard_manager_replay_populated(rdm, section,
                                                vfio_ram_discard_query_dirty_bitmap,
                                                &vrdl);
}

static int vfio_sync_iommu_dirty_bitmap(VFIOContainer *bcontainer,
                                        MemoryRegionSection *section)
{
    VFIOGuestIOMMU *giommu;
    bool found = false;
    Int128 llend;
    vfio_giommu_dirty_notifier gdn;
    int idx;

    QLIST_FOREACH(giommu, &bcontainer->giommu_list, giommu_next) {
        if (MEMORY_REGION(giommu->iommu_mr) == section->mr &&
            giommu->n.start == section->offset_within_region) {
            found = true;
            break;
        }
    }

    if (!found) {
        return 0;
    }

    gdn.giommu = giommu;
    idx = memory_region_iommu_attrs_to_index(giommu->iommu_mr,
                                             MEMTXATTRS_UNSPECIFIED);

    llend = int128_add(int128_make64(section->offset_within_region),
                       section->size);
    llend = int128_sub(llend, int128_one());

    iommu_notifier_init(&gdn.n, vfio_iommu_map_dirty_notify, IOMMU_NOTIFIER_MAP,
                        section->offset_within_region, int128_get64(llend),
                        idx);
    memory_region_iommu_replay(giommu->iommu_mr, &gdn.n);

    return 0;
}

static int vfio_sync_dirty_bitmap(VFIOContainer *bcontainer,
                                  MemoryRegionSection *section, Error **errp)
{
    hwaddr translated_addr;

    if (memory_region_is_iommu(section->mr)) {
        return vfio_sync_iommu_dirty_bitmap(bcontainer, section);
    } else if (memory_region_has_ram_discard_manager(section->mr)) {
        int ret;

        ret = vfio_sync_ram_discard_listener_dirty_bitmap(bcontainer, section);
        if (ret) {
            error_setg(errp,
                       "Failed to sync dirty bitmap with RAM discard listener");
        }
        return ret;
    }

    translated_addr = memory_region_get_ram_addr(section->mr) +
                      section->offset_within_region;

    return vfio_container_query_dirty_bitmap(bcontainer,
                   REAL_HOST_PAGE_ALIGN(section->offset_within_address_space),
                   int128_get64(section->size), translated_addr, errp);
}

static void vfio_listener_log_sync(MemoryListener *listener,
        MemoryRegionSection *section)
{
    VFIOContainer *bcontainer = container_of(listener, VFIOContainer,
                                             listener);
    int ret;
    Error *local_err = NULL;

    if (vfio_listener_skipped_section(section)) {
        return;
    }

    if (vfio_log_sync_needed(bcontainer)) {
        ret = vfio_sync_dirty_bitmap(bcontainer, section, &local_err);
        if (ret) {
            if (migration_is_running()) {
                migration_file_set_error(ret, local_err);
            } else {
                error_report_err(local_err);
            }
        }
    }
}

static const MemoryListener vfio_memory_listener = {
    .name = "vfio",
    .begin = vfio_listener_begin,
    .commit = vfio_listener_commit,
    .region_add = vfio_listener_region_add,
    .region_del = vfio_listener_region_del,
    .log_global_start = vfio_listener_log_global_start,
    .log_global_stop = vfio_listener_log_global_stop,
    .log_sync = vfio_listener_log_sync,
};

bool vfio_listener_register(VFIOContainer *bcontainer, Error **errp)
{
    bcontainer->listener = vfio_memory_listener;
    memory_listener_register(&bcontainer->listener, bcontainer->space->as);

    if (bcontainer->error) {
        error_propagate_prepend(errp, bcontainer->error,
                                "memory listener initialization failed: ");
        return false;
    }

    return true;
}

void vfio_listener_unregister(VFIOContainer *bcontainer)
{
    memory_listener_unregister(&bcontainer->listener);
}
