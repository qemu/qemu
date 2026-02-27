/*
 * low level and IOMMU backend agnostic helpers used by VFIO devices,
 * related to regions, interrupts, capabilities
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

#include "system/kvm.h"
#include "exec/cpu-common.h"
#include "hw/vfio/vfio-device.h"
#include "hw/core/hw-error.h"
#include "qapi/error.h"
#include "vfio-helpers.h"

int vfio_bitmap_alloc(VFIOBitmap *vbmap, hwaddr size)
{
    vbmap->pages = REAL_HOST_PAGE_ALIGN(size) / qemu_real_host_page_size();
    vbmap->size = ROUND_UP(vbmap->pages, sizeof(__u64) * BITS_PER_BYTE) /
                                         BITS_PER_BYTE;
    vbmap->bitmap = g_try_malloc0(vbmap->size);
    if (!vbmap->bitmap) {
        return -ENOMEM;
    }

    return 0;
}

struct vfio_info_cap_header *
vfio_get_cap(void *ptr, uint32_t cap_offset, uint16_t id)
{
    struct vfio_info_cap_header *hdr;

    for (hdr = ptr + cap_offset; hdr != ptr; hdr = ptr + hdr->next) {
        if (hdr->id == id) {
            return hdr;
        }
    }

    return NULL;
}

struct vfio_info_cap_header *
vfio_get_region_info_cap(struct vfio_region_info *info, uint16_t id)
{
    if (!(info->flags & VFIO_REGION_INFO_FLAG_CAPS)) {
        return NULL;
    }

    return vfio_get_cap((void *)info, info->cap_offset, id);
}

struct vfio_info_cap_header *
vfio_get_device_info_cap(struct vfio_device_info *info, uint16_t id)
{
    if (!(info->flags & VFIO_DEVICE_FLAGS_CAPS)) {
        return NULL;
    }

    return vfio_get_cap((void *)info, info->cap_offset, id);
}

struct vfio_info_cap_header *
vfio_get_iommu_type1_info_cap(struct vfio_iommu_type1_info *info, uint16_t id)
{
    if (!(info->flags & VFIO_IOMMU_INFO_CAPS)) {
        return NULL;
    }

    return vfio_get_cap((void *)info, info->cap_offset, id);
}

bool vfio_get_info_dma_avail(struct vfio_iommu_type1_info *info,
                             unsigned int *avail)
{
    struct vfio_info_cap_header *hdr;
    struct vfio_iommu_type1_info_dma_avail *cap;

    /* If the capability cannot be found, assume no DMA limiting */
    hdr = vfio_get_iommu_type1_info_cap(info,
                                        VFIO_IOMMU_TYPE1_INFO_DMA_AVAIL);
    if (!hdr) {
        return false;
    }

    if (avail != NULL) {
        cap = (void *) hdr;
        *avail = cap->avail;
    }

    return true;
}

#ifdef CONFIG_KVM
/*
 * We have a single VFIO pseudo device per KVM VM.  Once created it lives
 * for the life of the VM.  Closing the file descriptor only drops our
 * reference to it and the device's reference to kvm.  Therefore once
 * initialized, this file descriptor is only released on QEMU exit and
 * we'll re-use it should another vfio device be attached before then.
 */
int vfio_kvm_device_fd = -1;

/*
 * Confidential virtual machines:
 * During reset of confidential vms, the kvm vm file descriptor changes.
 * In this case, the old vfio kvm file descriptor is
 * closed and a new descriptor is created against the new kvm vm file
 * descriptor.
 */

typedef struct VFIODeviceFd {
    int fd;
    QLIST_ENTRY(VFIODeviceFd) node;
} VFIODeviceFd;

static QLIST_HEAD(, VFIODeviceFd) vfio_device_fds =
    QLIST_HEAD_INITIALIZER(vfio_device_fds);

static void vfio_device_fd_list_add(int fd)
{
    VFIODeviceFd *file_fd;
    file_fd = g_malloc0(sizeof(*file_fd));
    file_fd->fd = fd;
    QLIST_INSERT_HEAD(&vfio_device_fds, file_fd, node);
}

static void vfio_device_fd_list_remove(int fd)
{
    VFIODeviceFd *file_fd, *next;

    QLIST_FOREACH_SAFE(file_fd, &vfio_device_fds, node, next) {
        if (file_fd->fd == fd) {
            QLIST_REMOVE(file_fd, node);
            g_free(file_fd);
            break;
        }
    }
}

static int vfio_device_fd_rebind(NotifierWithReturn *notifier, void *data,
                                  Error **errp)
{
    VFIODeviceFd *file_fd;
    struct kvm_device_attr attr = {
        .group = KVM_DEV_VFIO_FILE,
        .attr = KVM_DEV_VFIO_FILE_ADD,
    };
    struct kvm_create_device cd = {
        .type = KVM_DEV_TYPE_VFIO,
    };

    /* we are not interested in pre vmfd change notification */
    if (((VmfdChangeNotifier *)data)->pre) {
        return 0;
    }

    if (kvm_vm_ioctl(kvm_state, KVM_CREATE_DEVICE, &cd)) {
        error_setg_errno(errp, errno, "Failed to create KVM VFIO device");
        return -errno;
    }

    if (vfio_kvm_device_fd != -1) {
        close(vfio_kvm_device_fd);
    }

    vfio_kvm_device_fd = cd.fd;

    QLIST_FOREACH(file_fd, &vfio_device_fds, node) {
        attr.addr = (uint64_t)(unsigned long)&file_fd->fd;
        if (ioctl(vfio_kvm_device_fd, KVM_SET_DEVICE_ATTR, &attr)) {
            error_setg_errno(errp, errno,
                             "Failed to add fd %d to KVM VFIO device",
                             file_fd->fd);
            return -errno;
        }
    }
    return 0;
}

static struct NotifierWithReturn vfio_vmfd_change_notifier = {
    .notify = vfio_device_fd_rebind,
};

#endif

void vfio_kvm_device_close(void)
{
#ifdef CONFIG_KVM
    kvm_close();
    if (vfio_kvm_device_fd != -1) {
        close(vfio_kvm_device_fd);
        vfio_kvm_device_fd = -1;
    }
#endif
}

int vfio_kvm_device_add_fd(int fd, Error **errp)
{
#ifdef CONFIG_KVM
    struct kvm_device_attr attr = {
        .group = KVM_DEV_VFIO_FILE,
        .attr = KVM_DEV_VFIO_FILE_ADD,
        .addr = (uint64_t)(unsigned long)&fd,
    };

    if (!kvm_enabled()) {
        return 0;
    }

    if (vfio_kvm_device_fd < 0) {
        struct kvm_create_device cd = {
            .type = KVM_DEV_TYPE_VFIO,
        };

        if (kvm_vm_ioctl(kvm_state, KVM_CREATE_DEVICE, &cd)) {
            error_setg_errno(errp, errno, "Failed to create KVM VFIO device");
            return -errno;
        }

        vfio_kvm_device_fd = cd.fd;
        /*
         * If the vm file descriptor changes, add a notifier so that we can
         * re-create the vfio_kvm_device_fd.
         */
        kvm_vmfd_add_change_notifier(&vfio_vmfd_change_notifier);
    }

    if (ioctl(vfio_kvm_device_fd, KVM_SET_DEVICE_ATTR, &attr)) {
        error_setg_errno(errp, errno, "Failed to add fd %d to KVM VFIO device",
                         fd);
        return -errno;
    }

    vfio_device_fd_list_add(fd);
#endif
    return 0;
}

int vfio_kvm_device_del_fd(int fd, Error **errp)
{
#ifdef CONFIG_KVM
    struct kvm_device_attr attr = {
        .group = KVM_DEV_VFIO_FILE,
        .attr = KVM_DEV_VFIO_FILE_DEL,
        .addr = (uint64_t)(unsigned long)&fd,
    };

    if (vfio_kvm_device_fd < 0) {
        error_setg(errp, "KVM VFIO device isn't created yet");
        return -EINVAL;
    }

    if (ioctl(vfio_kvm_device_fd, KVM_SET_DEVICE_ATTR, &attr)) {
        error_setg_errno(errp, errno,
                         "Failed to remove fd %d from KVM VFIO device", fd);
        return -errno;
    }

    vfio_device_fd_list_remove(fd);
#endif
    return 0;
}

struct vfio_device_info *vfio_get_device_info(int fd)
{
    struct vfio_device_info *info;
    uint32_t argsz = sizeof(*info);

    info = g_malloc0(argsz);

retry:
    info->argsz = argsz;

    if (ioctl(fd, VFIO_DEVICE_GET_INFO, info)) {
        g_free(info);
        return NULL;
    }

    if (info->argsz > argsz) {
        argsz = info->argsz;
        info = g_realloc(info, argsz);
        goto retry;
    }

    return info;
}
