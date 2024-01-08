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
#ifdef CONFIG_KVM
#include <linux/kvm.h>
#endif
#include "sysemu/kvm.h"
#include "exec/address-spaces.h"

#include "hw/vfio/vfio-common.h"
#include "hw/hw.h"
#include "exec/ram_addr.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "trace.h"

typedef struct VFIOSpaprContainer {
    VFIOContainer container;
    MemoryListener prereg_listener;
    QLIST_HEAD(, VFIOHostDMAWindow) hostwin_list;
} VFIOSpaprContainer;

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
    VFIOSpaprContainer *scontainer = container_of(listener, VFIOSpaprContainer,
                                                  prereg_listener);
    VFIOContainer *container = &scontainer->container;
    VFIOContainerBase *bcontainer = &container->bcontainer;
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
        if (!bcontainer->initialized) {
            if (!bcontainer->error) {
                error_setg_errno(&bcontainer->error, -ret,
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
    VFIOSpaprContainer *scontainer = container_of(listener, VFIOSpaprContainer,
                                                  prereg_listener);
    VFIOContainer *container = &scontainer->container;
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

static const MemoryListener vfio_prereg_listener = {
    .name = "vfio-pre-reg",
    .region_add = vfio_prereg_listener_region_add,
    .region_del = vfio_prereg_listener_region_del,
};

static void vfio_host_win_add(VFIOSpaprContainer *scontainer, hwaddr min_iova,
                              hwaddr max_iova, uint64_t iova_pgsizes)
{
    VFIOHostDMAWindow *hostwin;

    QLIST_FOREACH(hostwin, &scontainer->hostwin_list, hostwin_next) {
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
    QLIST_INSERT_HEAD(&scontainer->hostwin_list, hostwin, hostwin_next);
}

static int vfio_host_win_del(VFIOSpaprContainer *scontainer,
                             hwaddr min_iova, hwaddr max_iova)
{
    VFIOHostDMAWindow *hostwin;

    QLIST_FOREACH(hostwin, &scontainer->hostwin_list, hostwin_next) {
        if (hostwin->min_iova == min_iova && hostwin->max_iova == max_iova) {
            QLIST_REMOVE(hostwin, hostwin_next);
            g_free(hostwin);
            return 0;
        }
    }

    return -1;
}

static VFIOHostDMAWindow *vfio_find_hostwin(VFIOSpaprContainer *container,
                                            hwaddr iova, hwaddr end)
{
    VFIOHostDMAWindow *hostwin;
    bool hostwin_found = false;

    QLIST_FOREACH(hostwin, &container->hostwin_list, hostwin_next) {
        if (hostwin->min_iova <= iova && end <= hostwin->max_iova) {
            hostwin_found = true;
            break;
        }
    }

    return hostwin_found ? hostwin : NULL;
}

static int vfio_spapr_remove_window(VFIOContainer *container,
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

static int vfio_spapr_create_window(VFIOContainer *container,
                                    MemoryRegionSection *section,
                                    hwaddr *pgsize)
{
    int ret = 0;
    VFIOContainerBase *bcontainer = &container->bcontainer;
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
    pgmask = bcontainer->pgsizes & (pagesize | (pagesize - 1));
    pagesize = pgmask ? (1ULL << (63 - clz64(pgmask))) : 0;
    if (!pagesize) {
        error_report("Host doesn't support page size 0x%"PRIx64
                     ", the supported mask is 0x%lx",
                     memory_region_iommu_get_min_page_size(iommu_mr),
                     bcontainer->pgsizes);
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

static int
vfio_spapr_container_add_section_window(VFIOContainerBase *bcontainer,
                                        MemoryRegionSection *section,
                                        Error **errp)
{
    VFIOContainer *container = container_of(bcontainer, VFIOContainer,
                                            bcontainer);
    VFIOSpaprContainer *scontainer = container_of(container, VFIOSpaprContainer,
                                                  container);
    VFIOHostDMAWindow *hostwin;
    hwaddr pgsize = 0;
    int ret;

    /*
     * VFIO_SPAPR_TCE_IOMMU supports a single host window between
     * [dma32_window_start, dma32_window_size), we need to ensure
     * the section fall in this range.
     */
    if (container->iommu_type == VFIO_SPAPR_TCE_IOMMU) {
        hwaddr iova, end;

        iova = section->offset_within_address_space;
        end = iova + int128_get64(section->size) - 1;

        if (!vfio_find_hostwin(scontainer, iova, end)) {
            error_setg(errp, "Container %p can't map guest IOVA region"
                       " 0x%"HWADDR_PRIx"..0x%"HWADDR_PRIx, container,
                       iova, end);
            return -EINVAL;
        }
        return 0;
    }

    if (container->iommu_type != VFIO_SPAPR_TCE_v2_IOMMU) {
        return 0;
    }

    /* For now intersections are not allowed, we may relax this later */
    QLIST_FOREACH(hostwin, &scontainer->hostwin_list, hostwin_next) {
        if (ranges_overlap(hostwin->min_iova,
                           hostwin->max_iova - hostwin->min_iova + 1,
                           section->offset_within_address_space,
                           int128_get64(section->size))) {
            error_setg(errp,
                "region [0x%"PRIx64",0x%"PRIx64"] overlaps with existing"
                "host DMA window [0x%"PRIx64",0x%"PRIx64"]",
                section->offset_within_address_space,
                section->offset_within_address_space +
                    int128_get64(section->size) - 1,
                hostwin->min_iova, hostwin->max_iova);
            return -EINVAL;
        }
    }

    ret = vfio_spapr_create_window(container, section, &pgsize);
    if (ret) {
        error_setg_errno(errp, -ret, "Failed to create SPAPR window");
        return ret;
    }

    vfio_host_win_add(scontainer, section->offset_within_address_space,
                      section->offset_within_address_space +
                      int128_get64(section->size) - 1, pgsize);
#ifdef CONFIG_KVM
    if (kvm_enabled()) {
        VFIOGroup *group;
        IOMMUMemoryRegion *iommu_mr = IOMMU_MEMORY_REGION(section->mr);
        struct kvm_vfio_spapr_tce param;
        struct kvm_device_attr attr = {
            .group = KVM_DEV_VFIO_GROUP,
            .attr = KVM_DEV_VFIO_GROUP_SET_SPAPR_TCE,
            .addr = (uint64_t)(unsigned long)&param,
        };

        if (!memory_region_iommu_get_attr(iommu_mr, IOMMU_ATTR_SPAPR_TCE_FD,
                                          &param.tablefd)) {
            QLIST_FOREACH(group, &container->group_list, container_next) {
                param.groupfd = group->fd;
                if (ioctl(vfio_kvm_device_fd, KVM_SET_DEVICE_ATTR, &attr)) {
                    error_setg_errno(errp, errno,
                                     "vfio: failed GROUP_SET_SPAPR_TCE for "
                                     "KVM VFIO device %d and group fd %d",
                                     param.tablefd, param.groupfd);
                    return -errno;
                }
                trace_vfio_spapr_group_attach(param.groupfd, param.tablefd);
            }
        }
    }
#endif
    return 0;
}

static void
vfio_spapr_container_del_section_window(VFIOContainerBase *bcontainer,
                                        MemoryRegionSection *section)
{
    VFIOContainer *container = container_of(bcontainer, VFIOContainer,
                                            bcontainer);
    VFIOSpaprContainer *scontainer = container_of(container, VFIOSpaprContainer,
                                                  container);

    if (container->iommu_type != VFIO_SPAPR_TCE_v2_IOMMU) {
        return;
    }

    vfio_spapr_remove_window(container,
                             section->offset_within_address_space);
    if (vfio_host_win_del(scontainer,
                          section->offset_within_address_space,
                          section->offset_within_address_space +
                          int128_get64(section->size) - 1) < 0) {
        hw_error("%s: Cannot delete missing window at %"HWADDR_PRIx,
                 __func__, section->offset_within_address_space);
    }
}

static void vfio_spapr_container_release(VFIOContainerBase *bcontainer)
{
    VFIOContainer *container = container_of(bcontainer, VFIOContainer,
                                            bcontainer);
    VFIOSpaprContainer *scontainer = container_of(container, VFIOSpaprContainer,
                                                  container);
    VFIOHostDMAWindow *hostwin, *next;

    if (container->iommu_type == VFIO_SPAPR_TCE_v2_IOMMU) {
        memory_listener_unregister(&scontainer->prereg_listener);
    }
    QLIST_FOREACH_SAFE(hostwin, &scontainer->hostwin_list, hostwin_next,
                       next) {
        QLIST_REMOVE(hostwin, hostwin_next);
        g_free(hostwin);
    }
}

static int vfio_spapr_container_setup(VFIOContainerBase *bcontainer,
                                      Error **errp)
{
    VFIOContainer *container = container_of(bcontainer, VFIOContainer,
                                            bcontainer);
    VFIOSpaprContainer *scontainer = container_of(container, VFIOSpaprContainer,
                                                  container);
    struct vfio_iommu_spapr_tce_info info;
    bool v2 = container->iommu_type == VFIO_SPAPR_TCE_v2_IOMMU;
    int ret, fd = container->fd;

    QLIST_INIT(&scontainer->hostwin_list);

    /*
     * The host kernel code implementing VFIO_IOMMU_DISABLE is called
     * when container fd is closed so we do not call it explicitly
     * in this file.
     */
    if (!v2) {
        ret = ioctl(fd, VFIO_IOMMU_ENABLE);
        if (ret) {
            error_setg_errno(errp, errno, "failed to enable container");
            return -errno;
        }
    } else {
        scontainer->prereg_listener = vfio_prereg_listener;

        memory_listener_register(&scontainer->prereg_listener,
                                 &address_space_memory);
        if (bcontainer->error) {
            ret = -1;
            error_propagate_prepend(errp, bcontainer->error,
                    "RAM memory listener initialization failed: ");
            goto listener_unregister_exit;
        }
    }

    info.argsz = sizeof(info);
    ret = ioctl(fd, VFIO_IOMMU_SPAPR_TCE_GET_INFO, &info);
    if (ret) {
        error_setg_errno(errp, errno,
                         "VFIO_IOMMU_SPAPR_TCE_GET_INFO failed");
        ret = -errno;
        goto listener_unregister_exit;
    }

    if (v2) {
        bcontainer->pgsizes = info.ddw.pgsizes;
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
            goto listener_unregister_exit;
        }
    } else {
        /* The default table uses 4K pages */
        bcontainer->pgsizes = 0x1000;
        vfio_host_win_add(scontainer, info.dma32_window_start,
                          info.dma32_window_start +
                          info.dma32_window_size - 1,
                          0x1000);
    }

    return 0;

listener_unregister_exit:
    if (v2) {
        memory_listener_unregister(&scontainer->prereg_listener);
    }
    return ret;
}

static void vfio_iommu_spapr_class_init(ObjectClass *klass, void *data)
{
    VFIOIOMMUClass *vioc = VFIO_IOMMU_CLASS(klass);

    vioc->add_window = vfio_spapr_container_add_section_window;
    vioc->del_window = vfio_spapr_container_del_section_window;
    vioc->release = vfio_spapr_container_release;
    vioc->setup = vfio_spapr_container_setup;
};

static const TypeInfo types[] = {
    {
        .name = TYPE_VFIO_IOMMU_SPAPR,
        .parent = TYPE_VFIO_IOMMU_LEGACY,
        .class_init = vfio_iommu_spapr_class_init,
    },
};

DEFINE_TYPES(types)
