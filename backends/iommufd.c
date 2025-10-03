/*
 * iommufd container backend
 *
 * Copyright (C) 2023 Intel Corporation.
 * Copyright Red Hat, Inc. 2023
 *
 * Authors: Yi Liu <yi.l.liu@intel.com>
 *          Eric Auger <eric.auger@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "system/iommufd.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qom/object_interfaces.h"
#include "qemu/error-report.h"
#include "migration/cpr.h"
#include "monitor/monitor.h"
#include "trace.h"
#include "hw/vfio/vfio-device.h"
#include <sys/ioctl.h>
#include <linux/iommufd.h>

static const char *iommufd_fd_name(IOMMUFDBackend *be)
{
    return object_get_canonical_path_component(OBJECT(be));
}

static void iommufd_backend_init(Object *obj)
{
    IOMMUFDBackend *be = IOMMUFD_BACKEND(obj);

    be->fd = -1;
    be->users = 0;
    be->owned = true;
}

static void iommufd_backend_finalize(Object *obj)
{
    IOMMUFDBackend *be = IOMMUFD_BACKEND(obj);

    if (be->owned) {
        close(be->fd);
        be->fd = -1;
    }
}

static void iommufd_backend_set_fd(Object *obj, const char *str, Error **errp)
{
    ERRP_GUARD();
    IOMMUFDBackend *be = IOMMUFD_BACKEND(obj);
    int fd = -1;

    fd = monitor_fd_param(monitor_cur(), str, errp);
    if (fd == -1) {
        error_prepend(errp, "Could not parse remote object fd %s:", str);
        return;
    }
    be->fd = fd;
    be->owned = false;
    trace_iommu_backend_set_fd(be->fd);
}

static bool iommufd_backend_can_be_deleted(UserCreatable *uc)
{
    IOMMUFDBackend *be = IOMMUFD_BACKEND(uc);

    return !be->users;
}

static void iommufd_backend_complete(UserCreatable *uc, Error **errp)
{
    IOMMUFDBackend *be = IOMMUFD_BACKEND(uc);
    const char *name = iommufd_fd_name(be);

    if (!be->owned) {
        /* fd came from the command line. Fetch updated value from cpr state. */
        if (cpr_is_incoming()) {
            be->fd = cpr_find_fd(name, 0);
        } else {
            cpr_save_fd(name, 0, be->fd);
        }
    }
}

static void iommufd_backend_class_init(ObjectClass *oc, const void *data)
{
    UserCreatableClass *ucc = USER_CREATABLE_CLASS(oc);

    ucc->can_be_deleted = iommufd_backend_can_be_deleted;
    ucc->complete = iommufd_backend_complete;

    object_class_property_add_str(oc, "fd", NULL, iommufd_backend_set_fd);
}

bool iommufd_change_process_capable(IOMMUFDBackend *be)
{
    struct iommu_ioas_change_process args = {.size = sizeof(args)};

    /*
     * Call IOMMU_IOAS_CHANGE_PROCESS to verify it is a recognized ioctl.
     * This is a no-op if the process has not changed since DMA was mapped.
     */
    return !ioctl(be->fd, IOMMU_IOAS_CHANGE_PROCESS, &args);
}

bool iommufd_change_process(IOMMUFDBackend *be, Error **errp)
{
    struct iommu_ioas_change_process args = {.size = sizeof(args)};
    bool ret = !ioctl(be->fd, IOMMU_IOAS_CHANGE_PROCESS, &args);

    if (!ret) {
        error_setg_errno(errp, errno, "IOMMU_IOAS_CHANGE_PROCESS fd %d failed",
                         be->fd);
    }
    trace_iommufd_change_process(be->fd, ret);
    return ret;
}

bool iommufd_backend_connect(IOMMUFDBackend *be, Error **errp)
{
    int fd;

    if (be->owned && !be->users) {
        fd = cpr_open_fd("/dev/iommu", O_RDWR, iommufd_fd_name(be), 0, errp);
        if (fd < 0) {
            return false;
        }
        be->fd = fd;
    }
    if (!be->users && !vfio_iommufd_cpr_register_iommufd(be, errp)) {
        if (be->owned) {
            close(be->fd);
            be->fd = -1;
        }
        return false;
    }
    be->users++;

    trace_iommufd_backend_connect(be->fd, be->owned, be->users);
    return true;
}

void iommufd_backend_disconnect(IOMMUFDBackend *be)
{
    if (!be->users) {
        goto out;
    }
    be->users--;
    if (!be->users) {
        vfio_iommufd_cpr_unregister_iommufd(be);
        if (be->owned) {
            cpr_delete_fd(iommufd_fd_name(be), 0);
            close(be->fd);
            be->fd = -1;
        }
    }
out:
    trace_iommufd_backend_disconnect(be->fd, be->users);
}

bool iommufd_backend_alloc_ioas(IOMMUFDBackend *be, uint32_t *ioas_id,
                                Error **errp)
{
    int fd = be->fd;
    struct iommu_ioas_alloc alloc_data  = {
        .size = sizeof(alloc_data),
        .flags = 0,
    };

    if (ioctl(fd, IOMMU_IOAS_ALLOC, &alloc_data)) {
        error_setg_errno(errp, errno, "Failed to allocate ioas");
        return false;
    }

    *ioas_id = alloc_data.out_ioas_id;
    trace_iommufd_backend_alloc_ioas(fd, *ioas_id);

    return true;
}

void iommufd_backend_free_id(IOMMUFDBackend *be, uint32_t id)
{
    int ret, fd = be->fd;
    struct iommu_destroy des = {
        .size = sizeof(des),
        .id = id,
    };

    ret = ioctl(fd, IOMMU_DESTROY, &des);
    trace_iommufd_backend_free_id(fd, id, ret);
    if (ret) {
        error_report("Failed to free id: %u %m", id);
    }
}

int iommufd_backend_map_dma(IOMMUFDBackend *be, uint32_t ioas_id, hwaddr iova,
                            uint64_t size, void *vaddr, bool readonly)
{
    int ret, fd = be->fd;
    struct iommu_ioas_map map = {
        .size = sizeof(map),
        .flags = IOMMU_IOAS_MAP_READABLE |
                 IOMMU_IOAS_MAP_FIXED_IOVA,
        .ioas_id = ioas_id,
        .__reserved = 0,
        .user_va = (uintptr_t)vaddr,
        .iova = iova,
        .length = size,
    };

    if (!readonly) {
        map.flags |= IOMMU_IOAS_MAP_WRITEABLE;
    }

    ret = ioctl(fd, IOMMU_IOAS_MAP, &map);
    trace_iommufd_backend_map_dma(fd, ioas_id, iova, size,
                                  vaddr, readonly, ret);
    if (ret) {
        ret = -errno;

        /* TODO: Not support mapping hardware PCI BAR region for now. */
        if (errno == EFAULT) {
            warn_report("IOMMU_IOAS_MAP failed: %m, PCI BAR?");
        }
    }
    return ret;
}

int iommufd_backend_map_file_dma(IOMMUFDBackend *be, uint32_t ioas_id,
                                 hwaddr iova, uint64_t size,
                                 int mfd, unsigned long start, bool readonly)
{
    int ret, fd = be->fd;
    struct iommu_ioas_map_file map = {
        .size = sizeof(map),
        .flags = IOMMU_IOAS_MAP_READABLE |
                 IOMMU_IOAS_MAP_FIXED_IOVA,
        .ioas_id = ioas_id,
        .fd = mfd,
        .start = start,
        .iova = iova,
        .length = size,
    };

    if (cpr_is_incoming()) {
        return 0;
    }

    if (!readonly) {
        map.flags |= IOMMU_IOAS_MAP_WRITEABLE;
    }

    ret = ioctl(fd, IOMMU_IOAS_MAP_FILE, &map);
    trace_iommufd_backend_map_file_dma(fd, ioas_id, iova, size, mfd, start,
                                       readonly, ret);
    if (ret) {
        ret = -errno;

        /* TODO: Not support mapping hardware PCI BAR region for now. */
        if (errno == EFAULT) {
            warn_report("IOMMU_IOAS_MAP_FILE failed: %m, PCI BAR?");
        }
    }
    return ret;
}

int iommufd_backend_unmap_dma(IOMMUFDBackend *be, uint32_t ioas_id,
                              hwaddr iova, uint64_t size)
{
    int ret, fd = be->fd;
    struct iommu_ioas_unmap unmap = {
        .size = sizeof(unmap),
        .ioas_id = ioas_id,
        .iova = iova,
        .length = size,
    };

    if (cpr_is_incoming()) {
        return 0;
    }

    ret = ioctl(fd, IOMMU_IOAS_UNMAP, &unmap);
    /*
     * IOMMUFD takes mapping as some kind of object, unmapping
     * nonexistent mapping is treated as deleting a nonexistent
     * object and return ENOENT. This is different from legacy
     * backend which allows it. vIOMMU may trigger a lot of
     * redundant unmapping, to avoid flush the log, treat them
     * as succeess for IOMMUFD just like legacy backend.
     */
    if (ret && errno == ENOENT) {
        trace_iommufd_backend_unmap_dma_non_exist(fd, ioas_id, iova, size, ret);
        ret = 0;
    } else {
        trace_iommufd_backend_unmap_dma(fd, ioas_id, iova, size, ret);
    }

    if (ret) {
        ret = -errno;
    }
    return ret;
}

bool iommufd_backend_alloc_hwpt(IOMMUFDBackend *be, uint32_t dev_id,
                                uint32_t pt_id, uint32_t flags,
                                uint32_t data_type, uint32_t data_len,
                                void *data_ptr, uint32_t *out_hwpt,
                                Error **errp)
{
    int ret, fd = be->fd;
    struct iommu_hwpt_alloc alloc_hwpt = {
        .size = sizeof(struct iommu_hwpt_alloc),
        .flags = flags,
        .dev_id = dev_id,
        .pt_id = pt_id,
        .data_type = data_type,
        .data_len = data_len,
        .data_uptr = (uintptr_t)data_ptr,
    };

    ret = ioctl(fd, IOMMU_HWPT_ALLOC, &alloc_hwpt);
    trace_iommufd_backend_alloc_hwpt(fd, dev_id, pt_id, flags, data_type,
                                     data_len, (uintptr_t)data_ptr,
                                     alloc_hwpt.out_hwpt_id, ret);
    if (ret) {
        error_setg_errno(errp, errno, "Failed to allocate hwpt");
        return false;
    }

    *out_hwpt = alloc_hwpt.out_hwpt_id;
    return true;
}

bool iommufd_backend_set_dirty_tracking(IOMMUFDBackend *be,
                                        uint32_t hwpt_id, bool start,
                                        Error **errp)
{
    int ret;
    struct iommu_hwpt_set_dirty_tracking set_dirty = {
            .size = sizeof(set_dirty),
            .hwpt_id = hwpt_id,
            .flags = start ? IOMMU_HWPT_DIRTY_TRACKING_ENABLE : 0,
    };

    ret = ioctl(be->fd, IOMMU_HWPT_SET_DIRTY_TRACKING, &set_dirty);
    trace_iommufd_backend_set_dirty(be->fd, hwpt_id, start, ret ? errno : 0);
    if (ret) {
        error_setg_errno(errp, errno,
                         "IOMMU_HWPT_SET_DIRTY_TRACKING(hwpt_id %u) failed",
                         hwpt_id);
        return false;
    }

    return true;
}

bool iommufd_backend_get_dirty_bitmap(IOMMUFDBackend *be,
                                      uint32_t hwpt_id,
                                      uint64_t iova, ram_addr_t size,
                                      uint64_t page_size, uint64_t *data,
                                      Error **errp)
{
    int ret;
    struct iommu_hwpt_get_dirty_bitmap get_dirty_bitmap = {
        .size = sizeof(get_dirty_bitmap),
        .hwpt_id = hwpt_id,
        .iova = iova,
        .length = size,
        .page_size = page_size,
        .data = (uintptr_t)data,
    };

    ret = ioctl(be->fd, IOMMU_HWPT_GET_DIRTY_BITMAP, &get_dirty_bitmap);
    trace_iommufd_backend_get_dirty_bitmap(be->fd, hwpt_id, iova, size,
                                           page_size, ret ? errno : 0);
    if (ret) {
        error_setg_errno(errp, errno,
                         "IOMMU_HWPT_GET_DIRTY_BITMAP (iova: 0x%"HWADDR_PRIx
                         " size: 0x"RAM_ADDR_FMT") failed", iova, size);
        return false;
    }

    return true;
}

bool iommufd_backend_get_device_info(IOMMUFDBackend *be, uint32_t devid,
                                     uint32_t *type, void *data, uint32_t len,
                                     uint64_t *caps, Error **errp)
{
    struct iommu_hw_info info = {
        .size = sizeof(info),
        .dev_id = devid,
        .data_len = len,
        .data_uptr = (uintptr_t)data,
    };

    if (ioctl(be->fd, IOMMU_GET_HW_INFO, &info)) {
        error_setg_errno(errp, errno, "Failed to get hardware info");
        return false;
    }

    g_assert(type);
    *type = info.out_data_type;
    g_assert(caps);
    *caps = info.out_capabilities;

    return true;
}

bool iommufd_backend_invalidate_cache(IOMMUFDBackend *be, uint32_t id,
                                      uint32_t data_type, uint32_t entry_len,
                                      uint32_t *entry_num, void *data,
                                      Error **errp)
{
    int ret, fd = be->fd;
    uint32_t total_entries = *entry_num;
    struct iommu_hwpt_invalidate cache = {
        .size = sizeof(cache),
        .hwpt_id = id,
        .data_type = data_type,
        .entry_len = entry_len,
        .entry_num = total_entries,
        .data_uptr = (uintptr_t)data,
    };

    ret = ioctl(fd, IOMMU_HWPT_INVALIDATE, &cache);
    trace_iommufd_backend_invalidate_cache(fd, id, data_type, entry_len,
                                           total_entries, cache.entry_num,
                                           (uintptr_t)data, ret ? errno : 0);
    *entry_num = cache.entry_num;

    if (ret) {
        error_setg_errno(errp, errno, "IOMMU_HWPT_INVALIDATE failed:"
                         " total %d entries, processed %d entries",
                         total_entries, cache.entry_num);
    } else if (total_entries != cache.entry_num) {
        error_setg(errp, "IOMMU_HWPT_INVALIDATE succeed but with unprocessed"
                         " entries: total %d entries, processed %d entries."
                         " Kernel BUG?!", total_entries, cache.entry_num);
        return false;
    }

    return !ret;
}

bool host_iommu_device_iommufd_attach_hwpt(HostIOMMUDeviceIOMMUFD *idev,
                                           uint32_t hwpt_id, Error **errp)
{
    HostIOMMUDeviceIOMMUFDClass *idevc =
        HOST_IOMMU_DEVICE_IOMMUFD_GET_CLASS(idev);

    g_assert(idevc->attach_hwpt);
    return idevc->attach_hwpt(idev, hwpt_id, errp);
}

bool host_iommu_device_iommufd_detach_hwpt(HostIOMMUDeviceIOMMUFD *idev,
                                           Error **errp)
{
    HostIOMMUDeviceIOMMUFDClass *idevc =
        HOST_IOMMU_DEVICE_IOMMUFD_GET_CLASS(idev);

    g_assert(idevc->detach_hwpt);
    return idevc->detach_hwpt(idev, errp);
}

static int hiod_iommufd_get_cap(HostIOMMUDevice *hiod, int cap, Error **errp)
{
    HostIOMMUDeviceCaps *caps = &hiod->caps;

    switch (cap) {
    case HOST_IOMMU_DEVICE_CAP_IOMMU_TYPE:
        return caps->type;
    case HOST_IOMMU_DEVICE_CAP_AW_BITS:
        return vfio_device_get_aw_bits(hiod->agent);
    default:
        error_setg(errp, "%s: unsupported capability %x", hiod->name, cap);
        return -EINVAL;
    }
}

static void hiod_iommufd_class_init(ObjectClass *oc, const void *data)
{
    HostIOMMUDeviceClass *hioc = HOST_IOMMU_DEVICE_CLASS(oc);

    hioc->get_cap = hiod_iommufd_get_cap;
};

static const TypeInfo types[] = {
    {
        .name = TYPE_IOMMUFD_BACKEND,
        .parent = TYPE_OBJECT,
        .instance_size = sizeof(IOMMUFDBackend),
        .instance_init = iommufd_backend_init,
        .instance_finalize = iommufd_backend_finalize,
        .class_size = sizeof(IOMMUFDBackendClass),
        .class_init = iommufd_backend_class_init,
        .interfaces = (const InterfaceInfo[]) {
            { TYPE_USER_CREATABLE },
            { }
        }
    }, {
        .name = TYPE_HOST_IOMMU_DEVICE_IOMMUFD,
        .parent = TYPE_HOST_IOMMU_DEVICE,
        .instance_size = sizeof(HostIOMMUDeviceIOMMUFD),
        .class_size = sizeof(HostIOMMUDeviceIOMMUFDClass),
        .class_init = hiod_iommufd_class_init,
        .abstract = true,
    }
};

DEFINE_TYPES(types)
