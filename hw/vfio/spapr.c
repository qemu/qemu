/*
 * DMA memory preregistration
 *
 * Authors:
 *  Alexey Kardashevskiy <aik@ozlabs.ru>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <sys/ioctl.h>
#include <linux/vfio.h>

#include "hw/vfio/vfio-common.h"
#include "hw/hw.h"
#include "exec/ram_addr.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "trace.h"

static bool vfio_prereg_listener_skipped_section(MemoryRegionSection *section)
{
    if (memory_region_is_iommu(section->mr)) {
        hw_error("Cannot possibly preregister IOMMU memory");
    }

    return !memory_region_is_ram(section->mr) ||
            memory_region_is_ram_device(section->mr);
}

static void *vfio_prereg_gpa_to_vaddr(MemoryRegionSection *section, hwaddr gpa)
{
    return memory_region_get_ram_ptr(section->mr) +
        section->offset_within_region +
        (gpa - section->offset_within_address_space);
}

static void vfio_prereg_listener_region_add(MemoryListener *listener,
                                            MemoryRegionSection *section)
{
    VFIOContainer *container = container_of(listener, VFIOContainer,
                                            prereg_listener);
    const hwaddr gpa = section->offset_within_address_space;
    hwaddr end;
    int ret;
    hwaddr page_mask = qemu_real_host_page_mask();
    struct vfio_iommu_spapr_register_memory reg = {
        .argsz = sizeof(reg),
        .flags = 0,
    };

    if (vfio_prereg_listener_skipped_section(section)) {
        trace_vfio_prereg_listener_region_add_skip(
                section->offset_within_address_space,
                section->offset_within_address_space +
                int128_get64(int128_sub(section->size, int128_one())));
        return;
    }

    if (unlikely((section->offset_within_address_space & ~page_mask) ||
                 (section->offset_within_region & ~page_mask) ||
                 (int128_get64(section->size) & ~page_mask))) {
        error_report("%s received unaligned region", __func__);
        return;
    }

    end = section->offset_within_address_space + int128_get64(section->size);
    if (gpa >= end) {
        return;
    }

    memory_region_ref(section->mr);

    reg.vaddr = (uintptr_t) vfio_prereg_gpa_to_vaddr(section, gpa);
    reg.size = end - gpa;

    ret = ioctl(container->fd, VFIO_IOMMU_SPAPR_REGISTER_MEMORY, &reg);
    trace_vfio_prereg_register(reg.vaddr, reg.size, ret ? -errno : 0);
    if (ret) {
        /*
         * On the initfn path, store the first error in the container so we
         * can gracefully fail.  Runtime, there's not much we can do other
         * than throw a hardware error.
         */
        if (!container->initialized) {
            if (!container->error) {
                error_setg_errno(&container->error, -ret,
                                 "Memory registering failed");
            }
        } else {
            hw_error("vfio: Memory registering failed, unable to continue");
        }
    }
}

static void vfio_prereg_listener_region_del(MemoryListener *listener,
                                            MemoryRegionSection *section)
{
    VFIOContainer *container = container_of(listener, VFIOContainer,
                                            prereg_listener);
    const hwaddr gpa = section->offset_within_address_space;
    hwaddr end;
    int ret;
    hwaddr page_mask = qemu_real_host_page_mask();
    struct vfio_iommu_spapr_register_memory reg = {
        .argsz = sizeof(reg),
        .flags = 0,
    };

    if (vfio_prereg_listener_skipped_section(section)) {
        trace_vfio_prereg_listener_region_del_skip(
                section->offset_within_address_space,
                section->offset_within_address_space +
                int128_get64(int128_sub(section->size, int128_one())));
        return;
    }

    if (unlikely((section->offset_within_address_space & ~page_mask) ||
                 (section->offset_within_region & ~page_mask) ||
                 (int128_get64(section->size) & ~page_mask))) {
        error_report("%s received unaligned region", __func__);
        return;
    }

    end = section->offset_within_address_space + int128_get64(section->size);
    if (gpa >= end) {
        return;
    }

    reg.vaddr = (uintptr_t) vfio_prereg_gpa_to_vaddr(section, gpa);
    reg.size = end - gpa;

    ret = ioctl(container->fd, VFIO_IOMMU_SPAPR_UNREGISTER_MEMORY, &reg);
    trace_vfio_prereg_unregister(reg.vaddr, reg.size, ret ? -errno : 0);
}

const MemoryListener vfio_prereg_listener = {
    .name = "vfio-pre-reg",
    .region_add = vfio_prereg_listener_region_add,
    .region_del = vfio_prereg_listener_region_del,
};

int vfio_spapr_create_window(VFIOContainer *container,
                             MemoryRegionSection *section,
                             hwaddr *pgsize)
{
    int ret = 0;
    IOMMUMemoryRegion *iommu_mr = IOMMU_MEMORY_REGION(section->mr);
    uint64_t pagesize = memory_region_iommu_get_min_page_size(iommu_mr), pgmask;
    unsigned entries, bits_total, bits_per_level, max_levels;
    struct vfio_iommu_spapr_tce_create create = { .argsz = sizeof(create) };
    long rampagesize = qemu_minrampagesize();

    /*
     * The host might not support the guest supported IOMMU page size,
     * so we will use smaller physical IOMMU pages to back them.
     */
    if (pagesize > rampagesize) {
        pagesize = rampagesize;
    }
    pgmask = container->pgsizes & (pagesize | (pagesize - 1));
    pagesize = pgmask ? (1ULL << (63 - clz64(pgmask))) : 0;
    if (!pagesize) {
        error_report("Host doesn't support page size 0x%"PRIx64
                     ", the supported mask is 0x%lx",
                     memory_region_iommu_get_min_page_size(iommu_mr),
                     container->pgsizes);
        return -EINVAL;
    }

    /*
     * FIXME: For VFIO iommu types which have KVM acceleration to
     * avoid bouncing all map/unmaps through qemu this way, this
     * would be the right place to wire that up (tell the KVM
     * device emulation the VFIO iommu handles to use).
     */
    create.window_size = int128_get64(section->size);
    create.page_shift = ctz64(pagesize);
    /*
     * SPAPR host supports multilevel TCE tables. We try to guess optimal
     * levels number and if this fails (for example due to the host memory
     * fragmentation), we increase levels. The DMA address structure is:
     * rrrrrrrr rxxxxxxx xxxxxxxx xxxxxxxx  xxxxxxxx xxxxxxxx xxxxxxxx iiiiiiii
     * where:
     *   r = reserved (bits >= 55 are reserved in the existing hardware)
     *   i = IOMMU page offset (64K in this example)
     *   x = bits to index a TCE which can be split to equal chunks to index
     *      within the level.
     * The aim is to split "x" to smaller possible number of levels.
     */
    entries = create.window_size >> create.page_shift;
    /* bits_total is number of "x" needed */
    bits_total = ctz64(entries * sizeof(uint64_t));
    /*
     * bits_per_level is a safe guess of how much we can allocate per level:
     * 8 is the current minimum for CONFIG_FORCE_MAX_ZONEORDER and MAX_ORDER
     * is usually bigger than that.
     * Below we look at qemu_real_host_page_size as TCEs are allocated from
     * system pages.
     */
    bits_per_level = ctz64(qemu_real_host_page_size()) + 8;
    create.levels = bits_total / bits_per_level;
    if (bits_total % bits_per_level) {
        ++create.levels;
    }
    max_levels = (64 - create.page_shift) / ctz64(qemu_real_host_page_size());
    for ( ; create.levels <= max_levels; ++create.levels) {
        ret = ioctl(container->fd, VFIO_IOMMU_SPAPR_TCE_CREATE, &create);
        if (!ret) {
            break;
        }
    }
    if (ret) {
        error_report("Failed to create a window, ret = %d (%m)", ret);
        return -errno;
    }

    if (create.start_addr != section->offset_within_address_space) {
        vfio_spapr_remove_window(container, create.start_addr);

        error_report("Host doesn't support DMA window at %"HWADDR_PRIx", must be %"PRIx64,
                     section->offset_within_address_space,
                     (uint64_t)create.start_addr);
        return -EINVAL;
    }
    trace_vfio_spapr_create_window(create.page_shift,
                                   create.levels,
                                   create.window_size,
                                   create.start_addr);
    *pgsize = pagesize;

    return 0;
}

int vfio_spapr_remove_window(VFIOContainer *container,
                             hwaddr offset_within_address_space)
{
    struct vfio_iommu_spapr_tce_remove remove = {
        .argsz = sizeof(remove),
        .start_addr = offset_within_address_space,
    };
    int ret;

    ret = ioctl(container->fd, VFIO_IOMMU_SPAPR_TCE_REMOVE, &remove);
    if (ret) {
        error_report("Failed to remove window at %"PRIx64,
                     (uint64_t)remove.start_addr);
        return -errno;
    }

    trace_vfio_spapr_remove_window(offset_within_address_space);

    return 0;
}
