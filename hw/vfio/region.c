/*
 * VFIO regions
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

#include "hw/vfio/vfio-region.h"
#include "hw/vfio/vfio-device.h"
#include "hw/hw.h"
#include "trace.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/units.h"
#include "monitor/monitor.h"
#include "vfio-helpers.h"

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
    int ret;

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
        hw_error("vfio: unsupported write size, %u bytes", size);
        break;
    }

    ret = vbasedev->io_ops->region_write(vbasedev, region->nr,
                                         addr, size, &buf, region->post_wr);
    if (ret != size) {
        error_report("%s(%s:region%d+0x%"HWADDR_PRIx", 0x%"PRIx64
                     ",%d) failed: %s",
                     __func__, vbasedev->name, region->nr,
                     addr, data, size, strwriteerror(ret));
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
    int ret;

    ret = vbasedev->io_ops->region_read(vbasedev, region->nr, addr, size, &buf);
    if (ret != size) {
        error_report("%s(%s:region%d+0x%"HWADDR_PRIx", %d) failed: %s",
                     __func__, vbasedev->name, region->nr,
                     addr, size, strreaderror(ret));
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
        hw_error("vfio: unsupported read size, %u bytes", size);
        break;
    }

    trace_vfio_region_read(vbasedev->name, region->nr, addr, size, data);

    /* Same as write above */
    vbasedev->ops->vfio_eoi(vbasedev);

    return data;
}

static const MemoryRegionOps vfio_region_ops = {
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
        if (sparse->areas[i].size) {
            trace_vfio_region_sparse_mmap_entry(i, sparse->areas[i].offset,
                                            sparse->areas[i].offset +
                                            sparse->areas[i].size - 1);
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
    struct vfio_region_info *info = NULL;
    int ret;

    ret = vfio_device_get_region_info(vbasedev, index, &info);
    if (ret) {
        return ret;
    }

    region->vbasedev = vbasedev;
    region->flags = info->flags;
    region->size = info->size;
    region->fd_offset = info->offset;
    region->nr = index;
    region->post_wr = false;

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

    trace_vfio_region_setup(vbasedev->name, index, name,
                            region->flags, region->fd_offset, region->size);
    return 0;
}

static void vfio_subregion_unmap(VFIORegion *region, int index)
{
    trace_vfio_region_unmap(memory_region_name(&region->mmaps[index].mem),
                            region->mmaps[index].offset,
                            region->mmaps[index].offset +
                            region->mmaps[index].size - 1);
    memory_region_del_subregion(region->mem, &region->mmaps[index].mem);
    munmap(region->mmaps[index].mmap, region->mmaps[index].size);
    object_unparent(OBJECT(&region->mmaps[index].mem));
    region->mmaps[index].mmap = NULL;
}

int vfio_region_mmap(VFIORegion *region)
{
    int i, ret, prot = 0;
    char *name;
    int fd;

    if (!region->mem) {
        return 0;
    }

    prot |= region->flags & VFIO_REGION_INFO_FLAG_READ ? PROT_READ : 0;
    prot |= region->flags & VFIO_REGION_INFO_FLAG_WRITE ? PROT_WRITE : 0;

    for (i = 0; i < region->nr_mmaps; i++) {
        size_t align = MIN(1ULL << ctz64(region->mmaps[i].size), 1 * GiB);
        void *map_base, *map_align;

        /*
         * Align the mmap for more efficient mapping in the kernel.  Ideally
         * we'd know the PMD and PUD mapping sizes to use as discrete alignment
         * intervals, but we don't.  As of Linux v6.12, the largest PUD size
         * supporting huge pfnmap is 1GiB (ARCH_SUPPORTS_PUD_PFNMAP is only set
         * on x86_64).  Align by power-of-two size, capped at 1GiB.
         *
         * NB. qemu_memalign() and friends actually allocate memory, whereas
         * the region size here can exceed host memory, therefore we manually
         * create an oversized anonymous mapping and clean it up for alignment.
         */
        map_base = mmap(0, region->mmaps[i].size + align, PROT_NONE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (map_base == MAP_FAILED) {
            ret = -errno;
            goto no_mmap;
        }

        fd = vfio_device_get_region_fd(region->vbasedev, region->nr);

        map_align = (void *)ROUND_UP((uintptr_t)map_base, (uintptr_t)align);
        munmap(map_base, map_align - map_base);
        munmap(map_align + region->mmaps[i].size,
               align - (map_align - map_base));

        region->mmaps[i].mmap = mmap(map_align, region->mmaps[i].size, prot,
                                     MAP_SHARED | MAP_FIXED, fd,
                                     region->fd_offset +
                                     region->mmaps[i].offset);
        if (region->mmaps[i].mmap == MAP_FAILED) {
            ret = -errno;
            goto no_mmap;
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

no_mmap:
    trace_vfio_region_mmap_fault(memory_region_name(region->mem), i,
                                 region->fd_offset + region->mmaps[i].offset,
                                 region->fd_offset + region->mmaps[i].offset +
                                 region->mmaps[i].size - 1, ret);

    region->mmaps[i].mmap = NULL;

    for (i--; i >= 0; i--) {
        vfio_subregion_unmap(region, i);
    }

    return ret;
}

void vfio_region_unmap(VFIORegion *region)
{
    int i;

    if (!region->mem) {
        return;
    }

    for (i = 0; i < region->nr_mmaps; i++) {
        if (region->mmaps[i].mmap) {
            vfio_subregion_unmap(region, i);
        }
    }
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
        }
    }

    g_free(region->mem);
    g_free(region->mmaps);

    trace_vfio_region_finalize(region->vbasedev->name, region->nr);

    region->mem = NULL;
    region->mmaps = NULL;
    region->nr_mmaps = 0;
    region->size = 0;
    region->flags = 0;
    region->nr = 0;
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
