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

#include "hw/vfio/vfio-common.h"
#include "hw/vfio/vfio.h"
#include "exec/address-spaces.h"
#include "exec/memory.h"
#include "hw/hw.h"
#include "qemu/error-report.h"
#include "qemu/range.h"
#include "sysemu/kvm.h"
#include "trace.h"
#include "qapi/error.h"

struct vfio_group_head vfio_group_list =
    QLIST_HEAD_INITIALIZER(vfio_group_list);
struct vfio_as_head vfio_address_spaces =
    QLIST_HEAD_INITIALIZER(vfio_address_spaces);

#ifdef CONFIG_KVM
/*
 * We have a single VFIO pseudo device per KVM VM.  Once created it lives
 * for the life of the VM.  Closing the file descriptor only drops our
 * reference to it and the device's reference to kvm.  Therefore once
 * initialized, this file descriptor is only released on QEMU exit and
 * we'll re-use it should another vfio device be attached before then.
 */
static int vfio_kvm_device_fd = -1;
#endif

/*
 * Common VFIO interrupt disable
 */
void vfio_disable_irqindex(VFIODevice *vbasedev, int index)
{
    struct vfio_irq_set irq_set = {
        .argsz = sizeof(irq_set),
        .flags = VFIO_IRQ_SET_DATA_NONE | VFIO_IRQ_SET_ACTION_TRIGGER,
        .index = index,
        .start = 0,
        .count = 0,
    };

    ioctl(vbasedev->fd, VFIO_DEVICE_SET_IRQS, &irq_set);
}

void vfio_unmask_single_irqindex(VFIODevice *vbasedev, int index)
{
    struct vfio_irq_set irq_set = {
        .argsz = sizeof(irq_set),
        .flags = VFIO_IRQ_SET_DATA_NONE | VFIO_IRQ_SET_ACTION_UNMASK,
        .index = index,
        .start = 0,
        .count = 1,
    };

    ioctl(vbasedev->fd, VFIO_DEVICE_SET_IRQS, &irq_set);
}

void vfio_mask_single_irqindex(VFIODevice *vbasedev, int index)
{
    struct vfio_irq_set irq_set = {
        .argsz = sizeof(irq_set),
        .flags = VFIO_IRQ_SET_DATA_NONE | VFIO_IRQ_SET_ACTION_MASK,
        .index = index,
        .start = 0,
        .count = 1,
    };

    ioctl(vbasedev->fd, VFIO_DEVICE_SET_IRQS, &irq_set);
}

/*
 * IO Port/MMIO - Beware of the endians, VFIO is always little endian
 */
void vfio_region_write(void *opaque, hwaddr addr,
                       uint64_t data, unsigned size)
{
    VFIORegion *region = opaque;
    VFIODevice *vbasedev = region->vbasedev;
    union {
        uint8_t byte;
        uint16_t word;
        uint32_t dword;
        uint64_t qword;
    } buf;

    switch (size) {
    case 1:
        buf.byte = data;
        break;
    case 2:
        buf.word = cpu_to_le16(data);
        break;
    case 4:
        buf.dword = cpu_to_le32(data);
        break;
    case 8:
        buf.qword = cpu_to_le64(data);
        break;
    default:
        hw_error("vfio: unsupported write size, %d bytes", size);
        break;
    }

    if (pwrite(vbasedev->fd, &buf, size, region->fd_offset + addr) != size) {
        error_report("%s(%s:region%d+0x%"HWADDR_PRIx", 0x%"PRIx64
                     ",%d) failed: %m",
                     __func__, vbasedev->name, region->nr,
                     addr, data, size);
    }

    trace_vfio_region_write(vbasedev->name, region->nr, addr, data, size);

    /*
     * A read or write to a BAR always signals an INTx EOI.  This will
     * do nothing if not pending (including not in INTx mode).  We assume
     * that a BAR access is in response to an interrupt and that BAR
     * accesses will service the interrupt.  Unfortunately, we don't know
     * which access will service the interrupt, so we're potentially
     * getting quite a few host interrupts per guest interrupt.
     */
    vbasedev->ops->vfio_eoi(vbasedev);
}

uint64_t vfio_region_read(void *opaque,
                          hwaddr addr, unsigned size)
{
    VFIORegion *region = opaque;
    VFIODevice *vbasedev = region->vbasedev;
    union {
        uint8_t byte;
        uint16_t word;
        uint32_t dword;
        uint64_t qword;
    } buf;
    uint64_t data = 0;

    if (pread(vbasedev->fd, &buf, size, region->fd_offset + addr) != size) {
        error_report("%s(%s:region%d+0x%"HWADDR_PRIx", %d) failed: %m",
                     __func__, vbasedev->name, region->nr,
                     addr, size);
        return (uint64_t)-1;
    }
    switch (size) {
    case 1:
        data = buf.byte;
        break;
    case 2:
        data = le16_to_cpu(buf.word);
        break;
    case 4:
        data = le32_to_cpu(buf.dword);
        break;
    case 8:
        data = le64_to_cpu(buf.qword);
        break;
    default:
        hw_error("vfio: unsupported read size, %d bytes", size);
        break;
    }

    trace_vfio_region_read(vbasedev->name, region->nr, addr, size, data);

    /* Same as write above */
    vbasedev->ops->vfio_eoi(vbasedev);

    return data;
}

const MemoryRegionOps vfio_region_ops = {
    .read = vfio_region_read,
    .write = vfio_region_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
};

/*
 * DMA - Mapping and unmapping for the "type1" IOMMU interface used on x86
 */
static int vfio_dma_unmap(VFIOContainer *container,
                          hwaddr iova, ram_addr_t size)
{
    struct vfio_iommu_type1_dma_unmap unmap = {
        .argsz = sizeof(unmap),
        .flags = 0,
        .iova = iova,
        .size = size,
    };

    if (ioctl(container->fd, VFIO_IOMMU_UNMAP_DMA, &unmap)) {
        error_report("VFIO_UNMAP_DMA: %d", -errno);
        return -errno;
    }

    return 0;
}

static int vfio_dma_map(VFIOContainer *container, hwaddr iova,
                        ram_addr_t size, void *vaddr, bool readonly)
{
    struct vfio_iommu_type1_dma_map map = {
        .argsz = sizeof(map),
        .flags = VFIO_DMA_MAP_FLAG_READ,
        .vaddr = (__u64)(uintptr_t)vaddr,
        .iova = iova,
        .size = size,
    };

    if (!readonly) {
        map.flags |= VFIO_DMA_MAP_FLAG_WRITE;
    }

    /*
     * Try the mapping, if it fails with EBUSY, unmap the region and try
     * again.  This shouldn't be necessary, but we sometimes see it in
     * the VGA ROM space.
     */
    if (ioctl(container->fd, VFIO_IOMMU_MAP_DMA, &map) == 0 ||
        (errno == EBUSY && vfio_dma_unmap(container, iova, size) == 0 &&
         ioctl(container->fd, VFIO_IOMMU_MAP_DMA, &map) == 0)) {
        return 0;
    }

    error_report("VFIO_MAP_DMA: %d", -errno);
    return -errno;
}

static void vfio_host_win_add(VFIOContainer *container,
                              hwaddr min_iova, hwaddr max_iova,
                              uint64_t iova_pgsizes)
{
    VFIOHostDMAWindow *hostwin;

    QLIST_FOREACH(hostwin, &container->hostwin_list, hostwin_next) {
        if (ranges_overlap(hostwin->min_iova,
                           hostwin->max_iova - hostwin->min_iova + 1,
                           min_iova,
                           max_iova - min_iova + 1)) {
            hw_error("%s: Overlapped IOMMU are not enabled", __func__);
        }
    }

    hostwin = g_malloc0(sizeof(*hostwin));

    hostwin->min_iova = min_iova;
    hostwin->max_iova = max_iova;
    hostwin->iova_pgsizes = iova_pgsizes;
    QLIST_INSERT_HEAD(&container->hostwin_list, hostwin, hostwin_next);
}

static int vfio_host_win_del(VFIOContainer *container, hwaddr min_iova,
                             hwaddr max_iova)
{
    VFIOHostDMAWindow *hostwin;

    QLIST_FOREACH(hostwin, &container->hostwin_list, hostwin_next) {
        if (hostwin->min_iova == min_iova && hostwin->max_iova == max_iova) {
            QLIST_REMOVE(hostwin, hostwin_next);
            return 0;
        }
    }

    return -1;
}

static bool vfio_listener_skipped_section(MemoryRegionSection *section)
{
    return (!memory_region_is_ram(section->mr) &&
            !memory_region_is_iommu(section->mr)) ||
           /*
            * Sizing an enabled 64-bit BAR can cause spurious mappings to
            * addresses in the upper part of the 64-bit address space.  These
            * are never accessed by the CPU and beyond the address width of
            * some IOMMU hardware.  TODO: VFIO should tell us the IOMMU width.
            */
           section->offset_within_address_space & (1ULL << 63);
}

/* Called with rcu_read_lock held.  */
static bool vfio_get_vaddr(IOMMUTLBEntry *iotlb, void **vaddr,
                           bool *read_only)
{
    MemoryRegion *mr;
    hwaddr xlat;
    hwaddr len = iotlb->addr_mask + 1;
    bool writable = iotlb->perm & IOMMU_WO;

    /*
     * The IOMMU TLB entry we have just covers translation through
     * this IOMMU to its immediate target.  We need to translate
     * it the rest of the way through to memory.
     */
    mr = address_space_translate(&address_space_memory,
                                 iotlb->translated_addr,
                                 &xlat, &len, writable);
    if (!memory_region_is_ram(mr)) {
        error_report("iommu map to non memory area %"HWADDR_PRIx"",
                     xlat);
        return false;
    }

    /*
     * Translation truncates length to the IOMMU page size,
     * check that it did not truncate too much.
     */
    if (len & iotlb->addr_mask) {
        error_report("iommu has granularity incompatible with target AS");
        return false;
    }

    *vaddr = memory_region_get_ram_ptr(mr) + xlat;
    *read_only = !writable || mr->readonly;

    return true;
}

static void vfio_iommu_map_notify(IOMMUNotifier *n, IOMMUTLBEntry *iotlb)
{
    VFIOGuestIOMMU *giommu = container_of(n, VFIOGuestIOMMU, n);
    VFIOContainer *container = giommu->container;
    hwaddr iova = iotlb->iova + giommu->iommu_offset;
    bool read_only;
    void *vaddr;
    int ret;

    trace_vfio_iommu_map_notify(iotlb->perm == IOMMU_NONE ? "UNMAP" : "MAP",
                                iova, iova + iotlb->addr_mask);

    if (iotlb->target_as != &address_space_memory) {
        error_report("Wrong target AS \"%s\", only system memory is allowed",
                     iotlb->target_as->name ? iotlb->target_as->name : "none");
        return;
    }

    rcu_read_lock();

    if ((iotlb->perm & IOMMU_RW) != IOMMU_NONE) {
        if (!vfio_get_vaddr(iotlb, &vaddr, &read_only)) {
            goto out;
        }
        /*
         * vaddr is only valid until rcu_read_unlock(). But after
         * vfio_dma_map has set up the mapping the pages will be
         * pinned by the kernel. This makes sure that the RAM backend
         * of vaddr will always be there, even if the memory object is
         * destroyed and its backing memory munmap-ed.
         */
        ret = vfio_dma_map(container, iova,
                           iotlb->addr_mask + 1, vaddr,
                           read_only);
        if (ret) {
            error_report("vfio_dma_map(%p, 0x%"HWADDR_PRIx", "
                         "0x%"HWADDR_PRIx", %p) = %d (%m)",
                         container, iova,
                         iotlb->addr_mask + 1, vaddr, ret);
        }
    } else {
        ret = vfio_dma_unmap(container, iova, iotlb->addr_mask + 1);
        if (ret) {
            error_report("vfio_dma_unmap(%p, 0x%"HWADDR_PRIx", "
                         "0x%"HWADDR_PRIx") = %d (%m)",
                         container, iova,
                         iotlb->addr_mask + 1, ret);
        }
    }
out:
    rcu_read_unlock();
}

static void vfio_listener_region_add(MemoryListener *listener,
                                     MemoryRegionSection *section)
{
    VFIOContainer *container = container_of(listener, VFIOContainer, listener);
    hwaddr iova, end;
    Int128 llend, llsize;
    void *vaddr;
    int ret;
    VFIOHostDMAWindow *hostwin;
    bool hostwin_found;

    if (vfio_listener_skipped_section(section)) {
        trace_vfio_listener_region_add_skip(
                section->offset_within_address_space,
                section->offset_within_address_space +
                int128_get64(int128_sub(section->size, int128_one())));
        return;
    }

    if (unlikely((section->offset_within_address_space & ~TARGET_PAGE_MASK) !=
                 (section->offset_within_region & ~TARGET_PAGE_MASK))) {
        error_report("%s received unaligned region", __func__);
        return;
    }

    iova = TARGET_PAGE_ALIGN(section->offset_within_address_space);
    llend = int128_make64(section->offset_within_address_space);
    llend = int128_add(llend, section->size);
    llend = int128_and(llend, int128_exts64(TARGET_PAGE_MASK));

    if (int128_ge(int128_make64(iova), llend)) {
        return;
    }
    end = int128_get64(int128_sub(llend, int128_one()));

    if (container->iommu_type == VFIO_SPAPR_TCE_v2_IOMMU) {
        VFIOHostDMAWindow *hostwin;
        hwaddr pgsize = 0;

        /* For now intersections are not allowed, we may relax this later */
        QLIST_FOREACH(hostwin, &container->hostwin_list, hostwin_next) {
            if (ranges_overlap(hostwin->min_iova,
                               hostwin->max_iova - hostwin->min_iova + 1,
                               section->offset_within_address_space,
                               int128_get64(section->size))) {
                ret = -1;
                goto fail;
            }
        }

        ret = vfio_spapr_create_window(container, section, &pgsize);
        if (ret) {
            goto fail;
        }

        vfio_host_win_add(container, section->offset_within_address_space,
                          section->offset_within_address_space +
                          int128_get64(section->size) - 1, pgsize);
    }

    hostwin_found = false;
    QLIST_FOREACH(hostwin, &container->hostwin_list, hostwin_next) {
        if (hostwin->min_iova <= iova && end <= hostwin->max_iova) {
            hostwin_found = true;
            break;
        }
    }

    if (!hostwin_found) {
        error_report("vfio: IOMMU container %p can't map guest IOVA region"
                     " 0x%"HWADDR_PRIx"..0x%"HWADDR_PRIx,
                     container, iova, end);
        ret = -EFAULT;
        goto fail;
    }

    memory_region_ref(section->mr);

    if (memory_region_is_iommu(section->mr)) {
        VFIOGuestIOMMU *giommu;
        IOMMUMemoryRegion *iommu_mr = IOMMU_MEMORY_REGION(section->mr);

        trace_vfio_listener_region_add_iommu(iova, end);
        /*
         * FIXME: For VFIO iommu types which have KVM acceleration to
         * avoid bouncing all map/unmaps through qemu this way, this
         * would be the right place to wire that up (tell the KVM
         * device emulation the VFIO iommu handles to use).
         */
        giommu = g_malloc0(sizeof(*giommu));
        giommu->iommu = iommu_mr;
        giommu->iommu_offset = section->offset_within_address_space -
                               section->offset_within_region;
        giommu->container = container;
        llend = int128_add(int128_make64(section->offset_within_region),
                           section->size);
        llend = int128_sub(llend, int128_one());
        iommu_notifier_init(&giommu->n, vfio_iommu_map_notify,
                            IOMMU_NOTIFIER_ALL,
                            section->offset_within_region,
                            int128_get64(llend));
        QLIST_INSERT_HEAD(&container->giommu_list, giommu, giommu_next);

        memory_region_register_iommu_notifier(section->mr, &giommu->n);
        memory_region_iommu_replay(giommu->iommu, &giommu->n);

        return;
    }

    /* Here we assume that memory_region_is_ram(section->mr)==true */

    vaddr = memory_region_get_ram_ptr(section->mr) +
            section->offset_within_region +
            (iova - section->offset_within_address_space);

    trace_vfio_listener_region_add_ram(iova, end, vaddr);

    llsize = int128_sub(llend, int128_make64(iova));

    ret = vfio_dma_map(container, iova, int128_get64(llsize),
                       vaddr, section->readonly);
    if (ret) {
        error_report("vfio_dma_map(%p, 0x%"HWADDR_PRIx", "
                     "0x%"HWADDR_PRIx", %p) = %d (%m)",
                     container, iova, int128_get64(llsize), vaddr, ret);
        goto fail;
    }

    return;

fail:
    /*
     * On the initfn path, store the first error in the container so we
     * can gracefully fail.  Runtime, there's not much we can do other
     * than throw a hardware error.
     */
    if (!container->initialized) {
        if (!container->error) {
            container->error = ret;
        }
    } else {
        hw_error("vfio: DMA mapping failed, unable to continue");
    }
}

static void vfio_listener_region_del(MemoryListener *listener,
                                     MemoryRegionSection *section)
{
    VFIOContainer *container = container_of(listener, VFIOContainer, listener);
    hwaddr iova, end;
    Int128 llend, llsize;
    int ret;

    if (vfio_listener_skipped_section(section)) {
        trace_vfio_listener_region_del_skip(
                section->offset_within_address_space,
                section->offset_within_address_space +
                int128_get64(int128_sub(section->size, int128_one())));
        return;
    }

    if (unlikely((section->offset_within_address_space & ~TARGET_PAGE_MASK) !=
                 (section->offset_within_region & ~TARGET_PAGE_MASK))) {
        error_report("%s received unaligned region", __func__);
        return;
    }

    if (memory_region_is_iommu(section->mr)) {
        VFIOGuestIOMMU *giommu;

        QLIST_FOREACH(giommu, &container->giommu_list, giommu_next) {
            if (MEMORY_REGION(giommu->iommu) == section->mr &&
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

    iova = TARGET_PAGE_ALIGN(section->offset_within_address_space);
    llend = int128_make64(section->offset_within_address_space);
    llend = int128_add(llend, section->size);
    llend = int128_and(llend, int128_exts64(TARGET_PAGE_MASK));

    if (int128_ge(int128_make64(iova), llend)) {
        return;
    }
    end = int128_get64(int128_sub(llend, int128_one()));

    llsize = int128_sub(llend, int128_make64(iova));

    trace_vfio_listener_region_del(iova, end);

    ret = vfio_dma_unmap(container, iova, int128_get64(llsize));
    memory_region_unref(section->mr);
    if (ret) {
        error_report("vfio_dma_unmap(%p, 0x%"HWADDR_PRIx", "
                     "0x%"HWADDR_PRIx") = %d (%m)",
                     container, iova, int128_get64(llsize), ret);
    }

    if (container->iommu_type == VFIO_SPAPR_TCE_v2_IOMMU) {
        vfio_spapr_remove_window(container,
                                 section->offset_within_address_space);
        if (vfio_host_win_del(container,
                              section->offset_within_address_space,
                              section->offset_within_address_space +
                              int128_get64(section->size) - 1) < 0) {
            hw_error("%s: Cannot delete missing window at %"HWADDR_PRIx,
                     __func__, section->offset_within_address_space);
        }
    }
}

static const MemoryListener vfio_memory_listener = {
    .region_add = vfio_listener_region_add,
    .region_del = vfio_listener_region_del,
};

static void vfio_listener_release(VFIOContainer *container)
{
    memory_listener_unregister(&container->listener);
    if (container->iommu_type == VFIO_SPAPR_TCE_v2_IOMMU) {
        memory_listener_unregister(&container->prereg_listener);
    }
}

static struct vfio_info_cap_header *
vfio_get_region_info_cap(struct vfio_region_info *info, uint16_t id)
{
    struct vfio_info_cap_header *hdr;
    void *ptr = info;

    if (!(info->flags & VFIO_REGION_INFO_FLAG_CAPS)) {
        return NULL;
    }

    for (hdr = ptr + info->cap_offset; hdr != ptr; hdr = ptr + hdr->next) {
        if (hdr->id == id) {
            return hdr;
        }
    }

    return NULL;
}

static int vfio_setup_region_sparse_mmaps(VFIORegion *region,
                                          struct vfio_region_info *info)
{
    struct vfio_info_cap_header *hdr;
    struct vfio_region_info_cap_sparse_mmap *sparse;
    int i, j;

    hdr = vfio_get_region_info_cap(info, VFIO_REGION_INFO_CAP_SPARSE_MMAP);
    if (!hdr) {
        return -ENODEV;
    }

    sparse = container_of(hdr, struct vfio_region_info_cap_sparse_mmap, header);

    trace_vfio_region_sparse_mmap_header(region->vbasedev->name,
                                         region->nr, sparse->nr_areas);

    region->mmaps = g_new0(VFIOMmap, sparse->nr_areas);

    for (i = 0, j = 0; i < sparse->nr_areas; i++) {
        trace_vfio_region_sparse_mmap_entry(i, sparse->areas[i].offset,
                                            sparse->areas[i].offset +
                                            sparse->areas[i].size);

        if (sparse->areas[i].size) {
            region->mmaps[j].offset = sparse->areas[i].offset;
            region->mmaps[j].size = sparse->areas[i].size;
            j++;
        }
    }

    region->nr_mmaps = j;
    region->mmaps = g_realloc(region->mmaps, j * sizeof(VFIOMmap));

    return 0;
}

int vfio_region_setup(Object *obj, VFIODevice *vbasedev, VFIORegion *region,
                      int index, const char *name)
{
    struct vfio_region_info *info;
    int ret;

    ret = vfio_get_region_info(vbasedev, index, &info);
    if (ret) {
        return ret;
    }

    region->vbasedev = vbasedev;
    region->flags = info->flags;
    region->size = info->size;
    region->fd_offset = info->offset;
    region->nr = index;

    if (region->size) {
        region->mem = g_new0(MemoryRegion, 1);
        memory_region_init_io(region->mem, obj, &vfio_region_ops,
                              region, name, region->size);

        if (!vbasedev->no_mmap &&
            region->flags & VFIO_REGION_INFO_FLAG_MMAP) {

            ret = vfio_setup_region_sparse_mmaps(region, info);

            if (ret) {
                region->nr_mmaps = 1;
                region->mmaps = g_new0(VFIOMmap, region->nr_mmaps);
                region->mmaps[0].offset = 0;
                region->mmaps[0].size = region->size;
            }
        }
    }

    g_free(info);

    trace_vfio_region_setup(vbasedev->name, index, name,
                            region->flags, region->fd_offset, region->size);
    return 0;
}

int vfio_region_mmap(VFIORegion *region)
{
    int i, prot = 0;
    char *name;

    if (!region->mem) {
        return 0;
    }

    prot |= region->flags & VFIO_REGION_INFO_FLAG_READ ? PROT_READ : 0;
    prot |= region->flags & VFIO_REGION_INFO_FLAG_WRITE ? PROT_WRITE : 0;

    for (i = 0; i < region->nr_mmaps; i++) {
        region->mmaps[i].mmap = mmap(NULL, region->mmaps[i].size, prot,
                                     MAP_SHARED, region->vbasedev->fd,
                                     region->fd_offset +
                                     region->mmaps[i].offset);
        if (region->mmaps[i].mmap == MAP_FAILED) {
            int ret = -errno;

            trace_vfio_region_mmap_fault(memory_region_name(region->mem), i,
                                         region->fd_offset +
                                         region->mmaps[i].offset,
                                         region->fd_offset +
                                         region->mmaps[i].offset +
                                         region->mmaps[i].size - 1, ret);

            region->mmaps[i].mmap = NULL;

            for (i--; i >= 0; i--) {
                memory_region_del_subregion(region->mem, &region->mmaps[i].mem);
                munmap(region->mmaps[i].mmap, region->mmaps[i].size);
                object_unparent(OBJECT(&region->mmaps[i].mem));
                region->mmaps[i].mmap = NULL;
            }

            return ret;
        }

        name = g_strdup_printf("%s mmaps[%d]",
                               memory_region_name(region->mem), i);
        memory_region_init_ram_device_ptr(&region->mmaps[i].mem,
                                          memory_region_owner(region->mem),
                                          name, region->mmaps[i].size,
                                          region->mmaps[i].mmap);
        g_free(name);
        memory_region_add_subregion(region->mem, region->mmaps[i].offset,
                                    &region->mmaps[i].mem);

        trace_vfio_region_mmap(memory_region_name(&region->mmaps[i].mem),
                               region->mmaps[i].offset,
                               region->mmaps[i].offset +
                               region->mmaps[i].size - 1);
    }

    return 0;
}

void vfio_region_exit(VFIORegion *region)
{
    int i;

    if (!region->mem) {
        return;
    }

    for (i = 0; i < region->nr_mmaps; i++) {
        if (region->mmaps[i].mmap) {
            memory_region_del_subregion(region->mem, &region->mmaps[i].mem);
        }
    }

    trace_vfio_region_exit(region->vbasedev->name, region->nr);
}

void vfio_region_finalize(VFIORegion *region)
{
    int i;

    if (!region->mem) {
        return;
    }

    for (i = 0; i < region->nr_mmaps; i++) {
        if (region->mmaps[i].mmap) {
            munmap(region->mmaps[i].mmap, region->mmaps[i].size);
            object_unparent(OBJECT(&region->mmaps[i].mem));
        }
    }

    object_unparent(OBJECT(region->mem));

    g_free(region->mem);
    g_free(region->mmaps);

    trace_vfio_region_finalize(region->vbasedev->name, region->nr);
}

void vfio_region_mmaps_set_enabled(VFIORegion *region, bool enabled)
{
    int i;

    if (!region->mem) {
        return;
    }

    for (i = 0; i < region->nr_mmaps; i++) {
        if (region->mmaps[i].mmap) {
            memory_region_set_enabled(&region->mmaps[i].mem, enabled);
        }
    }

    trace_vfio_region_mmaps_set_enabled(memory_region_name(region->mem),
                                        enabled);
}

void vfio_reset_handler(void *opaque)
{
    VFIOGroup *group;
    VFIODevice *vbasedev;

    QLIST_FOREACH(group, &vfio_group_list, next) {
        QLIST_FOREACH(vbasedev, &group->device_list, next) {
            if (vbasedev->dev->realized) {
                vbasedev->ops->vfio_compute_needs_reset(vbasedev);
            }
        }
    }

    QLIST_FOREACH(group, &vfio_group_list, next) {
        QLIST_FOREACH(vbasedev, &group->device_list, next) {
            if (vbasedev->dev->realized && vbasedev->needs_reset) {
                vbasedev->ops->vfio_hot_reset_multi(vbasedev);
            }
        }
    }
}

static void vfio_kvm_device_add_group(VFIOGroup *group)
{
#ifdef CONFIG_KVM
    struct kvm_device_attr attr = {
        .group = KVM_DEV_VFIO_GROUP,
        .attr = KVM_DEV_VFIO_GROUP_ADD,
        .addr = (uint64_t)(unsigned long)&group->fd,
    };

    if (!kvm_enabled()) {
        return;
    }

    if (vfio_kvm_device_fd < 0) {
        struct kvm_create_device cd = {
            .type = KVM_DEV_TYPE_VFIO,
        };

        if (kvm_vm_ioctl(kvm_state, KVM_CREATE_DEVICE, &cd)) {
            error_report("Failed to create KVM VFIO device: %m");
            return;
        }

        vfio_kvm_device_fd = cd.fd;
    }

    if (ioctl(vfio_kvm_device_fd, KVM_SET_DEVICE_ATTR, &attr)) {
        error_report("Failed to add group %d to KVM VFIO device: %m",
                     group->groupid);
    }
#endif
}

static void vfio_kvm_device_del_group(VFIOGroup *group)
{
#ifdef CONFIG_KVM
    struct kvm_device_attr attr = {
        .group = KVM_DEV_VFIO_GROUP,
        .attr = KVM_DEV_VFIO_GROUP_DEL,
        .addr = (uint64_t)(unsigned long)&group->fd,
    };

    if (vfio_kvm_device_fd < 0) {
        return;
    }

    if (ioctl(vfio_kvm_device_fd, KVM_SET_DEVICE_ATTR, &attr)) {
        error_report("Failed to remove group %d from KVM VFIO device: %m",
                     group->groupid);
    }
#endif
}

static VFIOAddressSpace *vfio_get_address_space(AddressSpace *as)
{
    VFIOAddressSpace *space;

    QLIST_FOREACH(space, &vfio_address_spaces, list) {
        if (space->as == as) {
            return space;
        }
    }

    /* No suitable VFIOAddressSpace, create a new one */
    space = g_malloc0(sizeof(*space));
    space->as = as;
    QLIST_INIT(&space->containers);

    QLIST_INSERT_HEAD(&vfio_address_spaces, space, list);

    return space;
}

static void vfio_put_address_space(VFIOAddressSpace *space)
{
    if (QLIST_EMPTY(&space->containers)) {
        QLIST_REMOVE(space, list);
        g_free(space);
    }
}

static int vfio_connect_container(VFIOGroup *group, AddressSpace *as,
                                  Error **errp)
{
    VFIOContainer *container;
    int ret, fd;
    VFIOAddressSpace *space;

    space = vfio_get_address_space(as);

    QLIST_FOREACH(container, &space->containers, next) {
        if (!ioctl(group->fd, VFIO_GROUP_SET_CONTAINER, &container->fd)) {
            group->container = container;
            QLIST_INSERT_HEAD(&container->group_list, group, container_next);
            return 0;
        }
    }

    fd = qemu_open("/dev/vfio/vfio", O_RDWR);
    if (fd < 0) {
        error_setg_errno(errp, errno, "failed to open /dev/vfio/vfio");
        ret = -errno;
        goto put_space_exit;
    }

    ret = ioctl(fd, VFIO_GET_API_VERSION);
    if (ret != VFIO_API_VERSION) {
        error_setg(errp, "supported vfio version: %d, "
                   "reported version: %d", VFIO_API_VERSION, ret);
        ret = -EINVAL;
        goto close_fd_exit;
    }

    container = g_malloc0(sizeof(*container));
    container->space = space;
    container->fd = fd;
    if (ioctl(fd, VFIO_CHECK_EXTENSION, VFIO_TYPE1_IOMMU) ||
        ioctl(fd, VFIO_CHECK_EXTENSION, VFIO_TYPE1v2_IOMMU)) {
        bool v2 = !!ioctl(fd, VFIO_CHECK_EXTENSION, VFIO_TYPE1v2_IOMMU);
        struct vfio_iommu_type1_info info;

        ret = ioctl(group->fd, VFIO_GROUP_SET_CONTAINER, &fd);
        if (ret) {
            error_setg_errno(errp, errno, "failed to set group container");
            ret = -errno;
            goto free_container_exit;
        }

        container->iommu_type = v2 ? VFIO_TYPE1v2_IOMMU : VFIO_TYPE1_IOMMU;
        ret = ioctl(fd, VFIO_SET_IOMMU, container->iommu_type);
        if (ret) {
            error_setg_errno(errp, errno, "failed to set iommu for container");
            ret = -errno;
            goto free_container_exit;
        }

        /*
         * FIXME: This assumes that a Type1 IOMMU can map any 64-bit
         * IOVA whatsoever.  That's not actually true, but the current
         * kernel interface doesn't tell us what it can map, and the
         * existing Type1 IOMMUs generally support any IOVA we're
         * going to actually try in practice.
         */
        info.argsz = sizeof(info);
        ret = ioctl(fd, VFIO_IOMMU_GET_INFO, &info);
        /* Ignore errors */
        if (ret || !(info.flags & VFIO_IOMMU_INFO_PGSIZES)) {
            /* Assume 4k IOVA page size */
            info.iova_pgsizes = 4096;
        }
        vfio_host_win_add(container, 0, (hwaddr)-1, info.iova_pgsizes);
    } else if (ioctl(fd, VFIO_CHECK_EXTENSION, VFIO_SPAPR_TCE_IOMMU) ||
               ioctl(fd, VFIO_CHECK_EXTENSION, VFIO_SPAPR_TCE_v2_IOMMU)) {
        struct vfio_iommu_spapr_tce_info info;
        bool v2 = !!ioctl(fd, VFIO_CHECK_EXTENSION, VFIO_SPAPR_TCE_v2_IOMMU);

        ret = ioctl(group->fd, VFIO_GROUP_SET_CONTAINER, &fd);
        if (ret) {
            error_setg_errno(errp, errno, "failed to set group container");
            ret = -errno;
            goto free_container_exit;
        }
        container->iommu_type =
            v2 ? VFIO_SPAPR_TCE_v2_IOMMU : VFIO_SPAPR_TCE_IOMMU;
        ret = ioctl(fd, VFIO_SET_IOMMU, container->iommu_type);
        if (ret) {
            error_setg_errno(errp, errno, "failed to set iommu for container");
            ret = -errno;
            goto free_container_exit;
        }

        /*
         * The host kernel code implementing VFIO_IOMMU_DISABLE is called
         * when container fd is closed so we do not call it explicitly
         * in this file.
         */
        if (!v2) {
            ret = ioctl(fd, VFIO_IOMMU_ENABLE);
            if (ret) {
                error_setg_errno(errp, errno, "failed to enable container");
                ret = -errno;
                goto free_container_exit;
            }
        } else {
            container->prereg_listener = vfio_prereg_listener;

            memory_listener_register(&container->prereg_listener,
                                     &address_space_memory);
            if (container->error) {
                memory_listener_unregister(&container->prereg_listener);
                ret = container->error;
                error_setg(errp,
                    "RAM memory listener initialization failed for container");
                goto free_container_exit;
            }
        }

        info.argsz = sizeof(info);
        ret = ioctl(fd, VFIO_IOMMU_SPAPR_TCE_GET_INFO, &info);
        if (ret) {
            error_setg_errno(errp, errno,
                             "VFIO_IOMMU_SPAPR_TCE_GET_INFO failed");
            ret = -errno;
            if (v2) {
                memory_listener_unregister(&container->prereg_listener);
            }
            goto free_container_exit;
        }

        if (v2) {
            /*
             * There is a default window in just created container.
             * To make region_add/del simpler, we better remove this
             * window now and let those iommu_listener callbacks
             * create/remove them when needed.
             */
            ret = vfio_spapr_remove_window(container, info.dma32_window_start);
            if (ret) {
                error_setg_errno(errp, -ret,
                                 "failed to remove existing window");
                goto free_container_exit;
            }
        } else {
            /* The default table uses 4K pages */
            vfio_host_win_add(container, info.dma32_window_start,
                              info.dma32_window_start +
                              info.dma32_window_size - 1,
                              0x1000);
        }
    } else {
        error_setg(errp, "No available IOMMU models");
        ret = -EINVAL;
        goto free_container_exit;
    }

    vfio_kvm_device_add_group(group);

    QLIST_INIT(&container->group_list);
    QLIST_INSERT_HEAD(&space->containers, container, next);

    group->container = container;
    QLIST_INSERT_HEAD(&container->group_list, group, container_next);

    container->listener = vfio_memory_listener;

    memory_listener_register(&container->listener, container->space->as);

    if (container->error) {
        ret = container->error;
        error_setg_errno(errp, -ret,
                         "memory listener initialization failed for container");
        goto listener_release_exit;
    }

    container->initialized = true;

    return 0;
listener_release_exit:
    QLIST_REMOVE(group, container_next);
    QLIST_REMOVE(container, next);
    vfio_kvm_device_del_group(group);
    vfio_listener_release(container);

free_container_exit:
    g_free(container);

close_fd_exit:
    close(fd);

put_space_exit:
    vfio_put_address_space(space);

    return ret;
}

static void vfio_disconnect_container(VFIOGroup *group)
{
    VFIOContainer *container = group->container;

    if (ioctl(group->fd, VFIO_GROUP_UNSET_CONTAINER, &container->fd)) {
        error_report("vfio: error disconnecting group %d from container",
                     group->groupid);
    }

    QLIST_REMOVE(group, container_next);
    group->container = NULL;

    if (QLIST_EMPTY(&container->group_list)) {
        VFIOAddressSpace *space = container->space;
        VFIOGuestIOMMU *giommu, *tmp;

        vfio_listener_release(container);
        QLIST_REMOVE(container, next);

        QLIST_FOREACH_SAFE(giommu, &container->giommu_list, giommu_next, tmp) {
            memory_region_unregister_iommu_notifier(
                    MEMORY_REGION(giommu->iommu), &giommu->n);
            QLIST_REMOVE(giommu, giommu_next);
            g_free(giommu);
        }

        trace_vfio_disconnect_container(container->fd);
        close(container->fd);
        g_free(container);

        vfio_put_address_space(space);
    }
}

VFIOGroup *vfio_get_group(int groupid, AddressSpace *as, Error **errp)
{
    VFIOGroup *group;
    char path[32];
    struct vfio_group_status status = { .argsz = sizeof(status) };

    QLIST_FOREACH(group, &vfio_group_list, next) {
        if (group->groupid == groupid) {
            /* Found it.  Now is it already in the right context? */
            if (group->container->space->as == as) {
                return group;
            } else {
                error_setg(errp, "group %d used in multiple address spaces",
                           group->groupid);
                return NULL;
            }
        }
    }

    group = g_malloc0(sizeof(*group));

    snprintf(path, sizeof(path), "/dev/vfio/%d", groupid);
    group->fd = qemu_open(path, O_RDWR);
    if (group->fd < 0) {
        error_setg_errno(errp, errno, "failed to open %s", path);
        goto free_group_exit;
    }

    if (ioctl(group->fd, VFIO_GROUP_GET_STATUS, &status)) {
        error_setg_errno(errp, errno, "failed to get group %d status", groupid);
        goto close_fd_exit;
    }

    if (!(status.flags & VFIO_GROUP_FLAGS_VIABLE)) {
        error_setg(errp, "group %d is not viable", groupid);
        error_append_hint(errp,
                          "Please ensure all devices within the iommu_group "
                          "are bound to their vfio bus driver.\n");
        goto close_fd_exit;
    }

    group->groupid = groupid;
    QLIST_INIT(&group->device_list);

    if (vfio_connect_container(group, as, errp)) {
        error_prepend(errp, "failed to setup container for group %d: ",
                      groupid);
        goto close_fd_exit;
    }

    if (QLIST_EMPTY(&vfio_group_list)) {
        qemu_register_reset(vfio_reset_handler, NULL);
    }

    QLIST_INSERT_HEAD(&vfio_group_list, group, next);

    return group;

close_fd_exit:
    close(group->fd);

free_group_exit:
    g_free(group);

    return NULL;
}

void vfio_put_group(VFIOGroup *group)
{
    if (!group || !QLIST_EMPTY(&group->device_list)) {
        return;
    }

    vfio_kvm_device_del_group(group);
    vfio_disconnect_container(group);
    QLIST_REMOVE(group, next);
    trace_vfio_put_group(group->fd);
    close(group->fd);
    g_free(group);

    if (QLIST_EMPTY(&vfio_group_list)) {
        qemu_unregister_reset(vfio_reset_handler, NULL);
    }
}

int vfio_get_device(VFIOGroup *group, const char *name,
                    VFIODevice *vbasedev, Error **errp)
{
    struct vfio_device_info dev_info = { .argsz = sizeof(dev_info) };
    int ret, fd;

    fd = ioctl(group->fd, VFIO_GROUP_GET_DEVICE_FD, name);
    if (fd < 0) {
        error_setg_errno(errp, errno, "error getting device from group %d",
                         group->groupid);
        error_append_hint(errp,
                      "Verify all devices in group %d are bound to vfio-<bus> "
                      "or pci-stub and not already in use\n", group->groupid);
        return fd;
    }

    ret = ioctl(fd, VFIO_DEVICE_GET_INFO, &dev_info);
    if (ret) {
        error_setg_errno(errp, errno, "error getting device info");
        close(fd);
        return ret;
    }

    vbasedev->fd = fd;
    vbasedev->group = group;
    QLIST_INSERT_HEAD(&group->device_list, vbasedev, next);

    vbasedev->num_irqs = dev_info.num_irqs;
    vbasedev->num_regions = dev_info.num_regions;
    vbasedev->flags = dev_info.flags;

    trace_vfio_get_device(name, dev_info.flags, dev_info.num_regions,
                          dev_info.num_irqs);

    vbasedev->reset_works = !!(dev_info.flags & VFIO_DEVICE_FLAGS_RESET);
    return 0;
}

void vfio_put_base_device(VFIODevice *vbasedev)
{
    if (!vbasedev->group) {
        return;
    }
    QLIST_REMOVE(vbasedev, next);
    vbasedev->group = NULL;
    trace_vfio_put_base_device(vbasedev->fd);
    close(vbasedev->fd);
}

int vfio_get_region_info(VFIODevice *vbasedev, int index,
                         struct vfio_region_info **info)
{
    size_t argsz = sizeof(struct vfio_region_info);

    *info = g_malloc0(argsz);

    (*info)->index = index;
retry:
    (*info)->argsz = argsz;

    if (ioctl(vbasedev->fd, VFIO_DEVICE_GET_REGION_INFO, *info)) {
        g_free(*info);
        *info = NULL;
        return -errno;
    }

    if ((*info)->argsz > argsz) {
        argsz = (*info)->argsz;
        *info = g_realloc(*info, argsz);

        goto retry;
    }

    return 0;
}

int vfio_get_dev_region_info(VFIODevice *vbasedev, uint32_t type,
                             uint32_t subtype, struct vfio_region_info **info)
{
    int i;

    for (i = 0; i < vbasedev->num_regions; i++) {
        struct vfio_info_cap_header *hdr;
        struct vfio_region_info_cap_type *cap_type;

        if (vfio_get_region_info(vbasedev, i, info)) {
            continue;
        }

        hdr = vfio_get_region_info_cap(*info, VFIO_REGION_INFO_CAP_TYPE);
        if (!hdr) {
            g_free(*info);
            continue;
        }

        cap_type = container_of(hdr, struct vfio_region_info_cap_type, header);

        trace_vfio_get_dev_region(vbasedev->name, i,
                                  cap_type->type, cap_type->subtype);

        if (cap_type->type == type && cap_type->subtype == subtype) {
            return 0;
        }

        g_free(*info);
    }

    *info = NULL;
    return -ENODEV;
}

/*
 * Interfaces for IBM EEH (Enhanced Error Handling)
 */
static bool vfio_eeh_container_ok(VFIOContainer *container)
{
    /*
     * As of 2016-03-04 (linux-4.5) the host kernel EEH/VFIO
     * implementation is broken if there are multiple groups in a
     * container.  The hardware works in units of Partitionable
     * Endpoints (== IOMMU groups) and the EEH operations naively
     * iterate across all groups in the container, without any logic
     * to make sure the groups have their state synchronized.  For
     * certain operations (ENABLE) that might be ok, until an error
     * occurs, but for others (GET_STATE) it's clearly broken.
     */

    /*
     * XXX Once fixed kernels exist, test for them here
     */

    if (QLIST_EMPTY(&container->group_list)) {
        return false;
    }

    if (QLIST_NEXT(QLIST_FIRST(&container->group_list), container_next)) {
        return false;
    }

    return true;
}

static int vfio_eeh_container_op(VFIOContainer *container, uint32_t op)
{
    struct vfio_eeh_pe_op pe_op = {
        .argsz = sizeof(pe_op),
        .op = op,
    };
    int ret;

    if (!vfio_eeh_container_ok(container)) {
        error_report("vfio/eeh: EEH_PE_OP 0x%x: "
                     "kernel requires a container with exactly one group", op);
        return -EPERM;
    }

    ret = ioctl(container->fd, VFIO_EEH_PE_OP, &pe_op);
    if (ret < 0) {
        error_report("vfio/eeh: EEH_PE_OP 0x%x failed: %m", op);
        return -errno;
    }

    return ret;
}

static VFIOContainer *vfio_eeh_as_container(AddressSpace *as)
{
    VFIOAddressSpace *space = vfio_get_address_space(as);
    VFIOContainer *container = NULL;

    if (QLIST_EMPTY(&space->containers)) {
        /* No containers to act on */
        goto out;
    }

    container = QLIST_FIRST(&space->containers);

    if (QLIST_NEXT(container, next)) {
        /* We don't yet have logic to synchronize EEH state across
         * multiple containers */
        container = NULL;
        goto out;
    }

out:
    vfio_put_address_space(space);
    return container;
}

bool vfio_eeh_as_ok(AddressSpace *as)
{
    VFIOContainer *container = vfio_eeh_as_container(as);

    return (container != NULL) && vfio_eeh_container_ok(container);
}

int vfio_eeh_as_op(AddressSpace *as, uint32_t op)
{
    VFIOContainer *container = vfio_eeh_as_container(as);

    if (!container) {
        return -ENODEV;
    }
    return vfio_eeh_container_op(container, op);
}
