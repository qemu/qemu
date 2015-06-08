/*
 * vfio based device assignment support - platform devices
 *
 * Copyright Linaro Limited, 2014
 *
 * Authors:
 *  Kim Phillips <kim.phillips@linaro.org>
 *  Eric Auger <eric.auger@linaro.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Based on vfio based PCI device assignment support:
 *  Copyright Red Hat, Inc. 2012
 */

#include <linux/vfio.h>
#include <sys/ioctl.h>

#include "hw/vfio/vfio-platform.h"
#include "qemu/error-report.h"
#include "qemu/range.h"
#include "sysemu/sysemu.h"
#include "exec/memory.h"
#include "hw/sysbus.h"
#include "trace.h"
#include "hw/platform-bus.h"

/* VFIO skeleton */

static void vfio_platform_compute_needs_reset(VFIODevice *vbasedev)
{
    vbasedev->needs_reset = true;
}

/* not implemented yet */
static int vfio_platform_hot_reset_multi(VFIODevice *vbasedev)
{
    return -1;
}

/**
 * vfio_populate_device - Allocate and populate MMIO region
 * structs according to driver returned information
 * @vbasedev: the VFIO device handle
 *
 */
static int vfio_populate_device(VFIODevice *vbasedev)
{
    int i, ret = -1;
    VFIOPlatformDevice *vdev =
        container_of(vbasedev, VFIOPlatformDevice, vbasedev);

    if (!(vbasedev->flags & VFIO_DEVICE_FLAGS_PLATFORM)) {
        error_report("vfio: Um, this isn't a platform device");
        return ret;
    }

    vdev->regions = g_malloc0_n(vbasedev->num_regions,
                                sizeof(VFIORegion *));

    for (i = 0; i < vbasedev->num_regions; i++) {
        struct vfio_region_info reg_info = { .argsz = sizeof(reg_info) };
        VFIORegion *ptr;

        vdev->regions[i] = g_malloc0(sizeof(VFIORegion));
        ptr = vdev->regions[i];
        reg_info.index = i;
        ret = ioctl(vbasedev->fd, VFIO_DEVICE_GET_REGION_INFO, &reg_info);
        if (ret) {
            error_report("vfio: Error getting region %d info: %m", i);
            goto reg_error;
        }
        ptr->flags = reg_info.flags;
        ptr->size = reg_info.size;
        ptr->fd_offset = reg_info.offset;
        ptr->nr = i;
        ptr->vbasedev = vbasedev;

        trace_vfio_platform_populate_regions(ptr->nr,
                            (unsigned long)ptr->flags,
                            (unsigned long)ptr->size,
                            ptr->vbasedev->fd,
                            (unsigned long)ptr->fd_offset);
    }

    return 0;
reg_error:
    for (i = 0; i < vbasedev->num_regions; i++) {
        g_free(vdev->regions[i]);
    }
    g_free(vdev->regions);
    return ret;
}

/* specialized functions for VFIO Platform devices */
static VFIODeviceOps vfio_platform_ops = {
    .vfio_compute_needs_reset = vfio_platform_compute_needs_reset,
    .vfio_hot_reset_multi = vfio_platform_hot_reset_multi,
};

/**
 * vfio_base_device_init - perform preliminary VFIO setup
 * @vbasedev: the VFIO device handle
 *
 * Implement the VFIO command sequence that allows to discover
 * assigned device resources: group extraction, device
 * fd retrieval, resource query.
 * Precondition: the device name must be initialized
 */
static int vfio_base_device_init(VFIODevice *vbasedev)
{
    VFIOGroup *group;
    VFIODevice *vbasedev_iter;
    char path[PATH_MAX], iommu_group_path[PATH_MAX], *group_name;
    ssize_t len;
    struct stat st;
    int groupid;
    int ret;

    /* name must be set prior to the call */
    if (!vbasedev->name || strchr(vbasedev->name, '/')) {
        return -EINVAL;
    }

    /* Check that the host device exists */
    g_snprintf(path, sizeof(path), "/sys/bus/platform/devices/%s/",
               vbasedev->name);

    if (stat(path, &st) < 0) {
        error_report("vfio: error: no such host device: %s", path);
        return -errno;
    }

    g_strlcat(path, "iommu_group", sizeof(path));
    len = readlink(path, iommu_group_path, sizeof(iommu_group_path));
    if (len < 0 || len >= sizeof(iommu_group_path)) {
        error_report("vfio: error no iommu_group for device");
        return len < 0 ? -errno : -ENAMETOOLONG;
    }

    iommu_group_path[len] = 0;
    group_name = basename(iommu_group_path);

    if (sscanf(group_name, "%d", &groupid) != 1) {
        error_report("vfio: error reading %s: %m", path);
        return -errno;
    }

    trace_vfio_platform_base_device_init(vbasedev->name, groupid);

    group = vfio_get_group(groupid, &address_space_memory);
    if (!group) {
        error_report("vfio: failed to get group %d", groupid);
        return -ENOENT;
    }

    g_snprintf(path, sizeof(path), "%s", vbasedev->name);

    QLIST_FOREACH(vbasedev_iter, &group->device_list, next) {
        if (strcmp(vbasedev_iter->name, vbasedev->name) == 0) {
            error_report("vfio: error: device %s is already attached", path);
            vfio_put_group(group);
            return -EBUSY;
        }
    }
    ret = vfio_get_device(group, path, vbasedev);
    if (ret) {
        error_report("vfio: failed to get device %s", path);
        vfio_put_group(group);
        return ret;
    }

    ret = vfio_populate_device(vbasedev);
    if (ret) {
        error_report("vfio: failed to populate device %s", path);
        vfio_put_group(group);
    }

    return ret;
}

/**
 * vfio_map_region - initialize the 2 memory regions for a given
 * MMIO region index
 * @vdev: the VFIO platform device handle
 * @nr: the index of the region
 *
 * Init the top memory region and the mmapped memory region beneath
 * VFIOPlatformDevice is used since VFIODevice is not a QOM Object
 * and could not be passed to memory region functions
*/
static void vfio_map_region(VFIOPlatformDevice *vdev, int nr)
{
    VFIORegion *region = vdev->regions[nr];
    uint64_t size = region->size;
    char name[64];

    if (!size) {
        return;
    }

    g_snprintf(name, sizeof(name), "VFIO %s region %d",
               vdev->vbasedev.name, nr);

    /* A "slow" read/write mapping underlies all regions */
    memory_region_init_io(&region->mem, OBJECT(vdev), &vfio_region_ops,
                          region, name, size);

    g_strlcat(name, " mmap", sizeof(name));

    if (vfio_mmap_region(OBJECT(vdev), region, &region->mem,
                         &region->mmap_mem, &region->mmap, size, 0, name)) {
        error_report("%s unsupported. Performance may be slow", name);
    }
}

/**
 * vfio_platform_realize  - the device realize function
 * @dev: device state pointer
 * @errp: error
 *
 * initialize the device, its memory regions and IRQ structures
 * IRQ are started separately
 */
static void vfio_platform_realize(DeviceState *dev, Error **errp)
{
    VFIOPlatformDevice *vdev = VFIO_PLATFORM_DEVICE(dev);
    SysBusDevice *sbdev = SYS_BUS_DEVICE(dev);
    VFIODevice *vbasedev = &vdev->vbasedev;
    int i, ret;

    vbasedev->type = VFIO_DEVICE_TYPE_PLATFORM;
    vbasedev->ops = &vfio_platform_ops;

    trace_vfio_platform_realize(vbasedev->name, vdev->compat);

    ret = vfio_base_device_init(vbasedev);
    if (ret) {
        error_setg(errp, "vfio: vfio_base_device_init failed for %s",
                   vbasedev->name);
        return;
    }

    for (i = 0; i < vbasedev->num_regions; i++) {
        vfio_map_region(vdev, i);
        sysbus_init_mmio(sbdev, &vdev->regions[i]->mem);
    }
}

static const VMStateDescription vfio_platform_vmstate = {
    .name = TYPE_VFIO_PLATFORM,
    .unmigratable = 1,
};

static Property vfio_platform_dev_properties[] = {
    DEFINE_PROP_STRING("host", VFIOPlatformDevice, vbasedev.name),
    DEFINE_PROP_BOOL("x-mmap", VFIOPlatformDevice, vbasedev.allow_mmap, true),
    DEFINE_PROP_END_OF_LIST(),
};

static void vfio_platform_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = vfio_platform_realize;
    dc->props = vfio_platform_dev_properties;
    dc->vmsd = &vfio_platform_vmstate;
    dc->desc = "VFIO-based platform device assignment";
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo vfio_platform_dev_info = {
    .name = TYPE_VFIO_PLATFORM,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(VFIOPlatformDevice),
    .class_init = vfio_platform_class_init,
    .class_size = sizeof(VFIOPlatformDeviceClass),
    .abstract   = true,
};

static void register_vfio_platform_dev_type(void)
{
    type_register_static(&vfio_platform_dev_info);
}

type_init(register_vfio_platform_dev_type)
