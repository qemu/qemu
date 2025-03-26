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
#include "hw/vfio/vfio-common.h"
#include "hw/vfio/pci.h"
#include "hw/hw.h"
#include "trace.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/units.h"
#include "monitor/monitor.h"
#include "vfio-helpers.h"

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

static inline const char *action_to_str(int action)
{
    switch (action) {
    case VFIO_IRQ_SET_ACTION_MASK:
        return "MASK";
    case VFIO_IRQ_SET_ACTION_UNMASK:
        return "UNMASK";
    case VFIO_IRQ_SET_ACTION_TRIGGER:
        return "TRIGGER";
    default:
        return "UNKNOWN ACTION";
    }
}

static const char *index_to_str(VFIODevice *vbasedev, int index)
{
    if (vbasedev->type != VFIO_DEVICE_TYPE_PCI) {
        return NULL;
    }

    switch (index) {
    case VFIO_PCI_INTX_IRQ_INDEX:
        return "INTX";
    case VFIO_PCI_MSI_IRQ_INDEX:
        return "MSI";
    case VFIO_PCI_MSIX_IRQ_INDEX:
        return "MSIX";
    case VFIO_PCI_ERR_IRQ_INDEX:
        return "ERR";
    case VFIO_PCI_REQ_IRQ_INDEX:
        return "REQ";
    default:
        return NULL;
    }
}

bool vfio_set_irq_signaling(VFIODevice *vbasedev, int index, int subindex,
                            int action, int fd, Error **errp)
{
    ERRP_GUARD();
    g_autofree struct vfio_irq_set *irq_set = NULL;
    int argsz;
    const char *name;
    int32_t *pfd;

    argsz = sizeof(*irq_set) + sizeof(*pfd);

    irq_set = g_malloc0(argsz);
    irq_set->argsz = argsz;
    irq_set->flags = VFIO_IRQ_SET_DATA_EVENTFD | action;
    irq_set->index = index;
    irq_set->start = subindex;
    irq_set->count = 1;
    pfd = (int32_t *)&irq_set->data;
    *pfd = fd;

    if (!ioctl(vbasedev->fd, VFIO_DEVICE_SET_IRQS, irq_set)) {
        return true;
    }

    error_setg_errno(errp, errno, "VFIO_DEVICE_SET_IRQS failure");

    name = index_to_str(vbasedev, index);
    if (name) {
        error_prepend(errp, "%s-%d: ", name, subindex);
    } else {
        error_prepend(errp, "index %d-%d: ", index, subindex);
    }
    error_prepend(errp,
                  "Failed to %s %s eventfd signaling for interrupt ",
                  fd < 0 ? "tear down" : "set up", action_to_str(action));
    return false;
}

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
    }

    if (ioctl(vfio_kvm_device_fd, KVM_SET_DEVICE_ATTR, &attr)) {
        error_setg_errno(errp, errno, "Failed to add fd %d to KVM VFIO device",
                         fd);
        return -errno;
    }
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

bool vfio_has_region_cap(VFIODevice *vbasedev, int region, uint16_t cap_type)
{
    g_autofree struct vfio_region_info *info = NULL;
    bool ret = false;

    if (!vfio_get_region_info(vbasedev, region, &info)) {
        if (vfio_get_region_info_cap(info, cap_type)) {
            ret = true;
        }
    }

    return ret;
}

bool vfio_device_get_name(VFIODevice *vbasedev, Error **errp)
{
    ERRP_GUARD();
    struct stat st;

    if (vbasedev->fd < 0) {
        if (stat(vbasedev->sysfsdev, &st) < 0) {
            error_setg_errno(errp, errno, "no such host device");
            error_prepend(errp, VFIO_MSG_PREFIX, vbasedev->sysfsdev);
            return false;
        }
        /* User may specify a name, e.g: VFIO platform device */
        if (!vbasedev->name) {
            vbasedev->name = g_path_get_basename(vbasedev->sysfsdev);
        }
    } else {
        if (!vbasedev->iommufd) {
            error_setg(errp, "Use FD passing only with iommufd backend");
            return false;
        }
        /*
         * Give a name with fd so any function printing out vbasedev->name
         * will not break.
         */
        if (!vbasedev->name) {
            vbasedev->name = g_strdup_printf("VFIO_FD%d", vbasedev->fd);
        }
    }

    return true;
}

void vfio_device_set_fd(VFIODevice *vbasedev, const char *str, Error **errp)
{
    ERRP_GUARD();
    int fd = monitor_fd_param(monitor_cur(), str, errp);

    if (fd < 0) {
        error_prepend(errp, "Could not parse remote object fd %s:", str);
        return;
    }
    vbasedev->fd = fd;
}

void vfio_device_init(VFIODevice *vbasedev, int type, VFIODeviceOps *ops,
                      DeviceState *dev, bool ram_discard)
{
    vbasedev->type = type;
    vbasedev->ops = ops;
    vbasedev->dev = dev;
    vbasedev->fd = -1;

    vbasedev->ram_block_discard_allowed = ram_discard;
}

int vfio_device_get_aw_bits(VFIODevice *vdev)
{
    /*
     * iova_ranges is a sorted list. For old kernels that support
     * VFIO but not support query of iova ranges, iova_ranges is NULL,
     * in this case HOST_IOMMU_DEVICE_CAP_AW_BITS_MAX(64) is returned.
     */
    GList *l = g_list_last(vdev->bcontainer->iova_ranges);

    if (l) {
        Range *range = l->data;
        return range_get_last_bit(range) + 1;
    }

    return HOST_IOMMU_DEVICE_CAP_AW_BITS_MAX;
}

bool vfio_device_is_mdev(VFIODevice *vbasedev)
{
    g_autofree char *subsys = NULL;
    g_autofree char *tmp = NULL;

    if (!vbasedev->sysfsdev) {
        return false;
    }

    tmp = g_strdup_printf("%s/subsystem", vbasedev->sysfsdev);
    subsys = realpath(tmp, NULL);
    return subsys && (strcmp(subsys, "/sys/bus/mdev") == 0);
}

bool vfio_device_hiod_realize(VFIODevice *vbasedev, Error **errp)
{
    HostIOMMUDevice *hiod = vbasedev->hiod;

    if (!hiod) {
        return true;
    }

    return HOST_IOMMU_DEVICE_GET_CLASS(hiod)->realize(hiod, vbasedev, errp);
}

VFIODevice *vfio_get_vfio_device(Object *obj)
{
    if (object_dynamic_cast(obj, TYPE_VFIO_PCI)) {
        return &VFIO_PCI(obj)->vbasedev;
    } else {
        return NULL;
    }
}
