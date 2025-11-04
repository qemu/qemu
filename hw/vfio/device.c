/*
 * VFIO device
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

#include "hw/vfio/vfio-device.h"
#include "hw/vfio/pci.h"
#include "hw/hw.h"
#include "trace.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/units.h"
#include "migration/cpr.h"
#include "migration/blocker.h"
#include "monitor/monitor.h"
#include "vfio-helpers.h"

VFIODeviceList vfio_device_list =
    QLIST_HEAD_INITIALIZER(vfio_device_list);

/*
 * We want to differentiate hot reset of multiple in-use devices vs
 * hot reset of a single in-use device. VFIO_DEVICE_RESET will already
 * handle the case of doing hot resets when there is only a single
 * device per bus. The in-use here refers to how many VFIODevices are
 * affected. A hot reset that affects multiple devices, but only a
 * single in-use device, means that we can call it from our bus
 * ->reset() callback since the extent is effectively a single
 * device. This allows us to make use of it in the hotplug path. When
 * there are multiple in-use devices, we can only trigger the hot
 * reset during a system reset and thus from our reset handler. We
 * separate _one vs _multi here so that we don't overlap and do a
 * double reset on the system reset path where both our reset handler
 * and ->reset() callback are used. Calling _one() will only do a hot
 * reset for the one in-use devices case, calling _multi() will do
 * nothing if a _one() would have been sufficient.
 */
void vfio_device_reset_handler(void *opaque)
{
    VFIODevice *vbasedev;

    trace_vfio_device_reset_handler();
    QLIST_FOREACH(vbasedev, &vfio_device_list, global_next) {
        if (vbasedev->dev->realized) {
            vbasedev->ops->vfio_compute_needs_reset(vbasedev);
        }
    }

    QLIST_FOREACH(vbasedev, &vfio_device_list, global_next) {
        if (vbasedev->dev->realized && vbasedev->needs_reset) {
            vbasedev->ops->vfio_hot_reset_multi(vbasedev);
        }
    }
}

/*
 * Common VFIO interrupt disable
 */
void vfio_device_irq_disable(VFIODevice *vbasedev, int index)
{
    struct vfio_irq_set irq_set = {
        .argsz = sizeof(irq_set),
        .flags = VFIO_IRQ_SET_DATA_NONE | VFIO_IRQ_SET_ACTION_TRIGGER,
        .index = index,
        .start = 0,
        .count = 0,
    };

    vbasedev->io_ops->set_irqs(vbasedev, &irq_set);
}

void vfio_device_irq_unmask(VFIODevice *vbasedev, int index)
{
    struct vfio_irq_set irq_set = {
        .argsz = sizeof(irq_set),
        .flags = VFIO_IRQ_SET_DATA_NONE | VFIO_IRQ_SET_ACTION_UNMASK,
        .index = index,
        .start = 0,
        .count = 1,
    };

    vbasedev->io_ops->set_irqs(vbasedev, &irq_set);
}

void vfio_device_irq_mask(VFIODevice *vbasedev, int index)
{
    struct vfio_irq_set irq_set = {
        .argsz = sizeof(irq_set),
        .flags = VFIO_IRQ_SET_DATA_NONE | VFIO_IRQ_SET_ACTION_MASK,
        .index = index,
        .start = 0,
        .count = 1,
    };

    vbasedev->io_ops->set_irqs(vbasedev, &irq_set);
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
    if (!vfio_pci_from_vfio_device(vbasedev)) {
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

bool vfio_device_irq_set_signaling(VFIODevice *vbasedev, int index, int subindex,
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

    if (!vbasedev->io_ops->set_irqs(vbasedev, irq_set)) {
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

int vfio_device_get_irq_info(VFIODevice *vbasedev, int index,
                             struct vfio_irq_info *info)
{
    memset(info, 0, sizeof(*info));

    info->argsz = sizeof(*info);
    info->index = index;

    return vbasedev->io_ops->get_irq_info(vbasedev, info);
}

int vfio_device_get_region_info(VFIODevice *vbasedev, int index,
                                struct vfio_region_info **info)
{
    size_t argsz = sizeof(struct vfio_region_info);
    int fd = -1;
    int ret;

    /*
     * We only set up the region info cache for the initial number of regions.
     *
     * Since a VFIO device may later increase the number of regions then use
     * such regions with an index past ->num_initial_regions, don't attempt to
     * use the info cache in those cases.
     */
    if (index < vbasedev->num_initial_regions) {
        /* check cache */
        if (vbasedev->reginfo[index] != NULL) {
            *info = vbasedev->reginfo[index];
            return 0;
        }
    }

    *info = g_malloc0(argsz);

    (*info)->index = index;
retry:
    (*info)->argsz = argsz;

    ret = vbasedev->io_ops->get_region_info(vbasedev, *info, &fd);
    if (ret != 0) {
        g_free(*info);
        *info = NULL;
        return ret;
    }

    if ((*info)->argsz > argsz) {
        argsz = (*info)->argsz;
        *info = g_realloc(*info, argsz);

        if (fd != -1) {
            close(fd);
            fd = -1;
        }

        goto retry;
    }

    if (index < vbasedev->num_initial_regions) {
        /* fill cache */
        vbasedev->reginfo[index] = *info;
        if (vbasedev->region_fds != NULL) {
            vbasedev->region_fds[index] = fd;
        }
    }

    return 0;
}

int vfio_device_get_region_fd(VFIODevice *vbasedev, int index)
{
        return vbasedev->region_fds ?
               vbasedev->region_fds[index] :
               vbasedev->fd;
}

int vfio_device_get_region_info_type(VFIODevice *vbasedev, uint32_t type,
                                     uint32_t subtype, struct vfio_region_info **info)
{
    int i;

    for (i = 0; i < vbasedev->num_initial_regions; i++) {
        struct vfio_info_cap_header *hdr;
        struct vfio_region_info_cap_type *cap_type;

        if (vfio_device_get_region_info(vbasedev, i, info)) {
            continue;
        }

        hdr = vfio_get_region_info_cap(*info, VFIO_REGION_INFO_CAP_TYPE);
        if (!hdr) {
            continue;
        }

        cap_type = container_of(hdr, struct vfio_region_info_cap_type, header);

        trace_vfio_device_get_region_info_type(vbasedev->name, i,
                                               cap_type->type, cap_type->subtype);

        if (cap_type->type == type && cap_type->subtype == subtype) {
            return 0;
        }
    }

    *info = NULL;
    return -ENODEV;
}

bool vfio_device_has_region_cap(VFIODevice *vbasedev, int region, uint16_t cap_type)
{
    struct vfio_region_info *info = NULL;
    bool ret = false;

    if (!vfio_device_get_region_info(vbasedev, region, &info)) {
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
        if (!vbasedev->name) {

            if (vbasedev->dev->id) {
                vbasedev->name = g_strdup(vbasedev->dev->id);
                return true;
            } else {
                /*
                 * Assign a name so any function printing it will not break.
                 * The fd number changes across processes, so this cannot be
                 * used as an invariant name for CPR.
                 */
                vbasedev->name = g_strdup_printf("VFIO_FD%d", vbasedev->fd);
                error_setg(&vbasedev->cpr.id_blocker,
                           "vfio device with fd=%d needs an id property",
                           vbasedev->fd);
                return migrate_add_blocker_modes(&vbasedev->cpr.id_blocker,
                                                 BIT(MIG_MODE_CPR_TRANSFER),
                                                 errp) == 0;
            }
        }
    }

    return true;
}

void vfio_device_free_name(VFIODevice *vbasedev)
{
    g_clear_pointer(&vbasedev->name, g_free);
    migrate_del_blocker(&vbasedev->cpr.id_blocker);
}

void vfio_device_set_fd(VFIODevice *vbasedev, const char *str, Error **errp)
{
    vbasedev->fd = cpr_get_fd_param(vbasedev->dev->id, str, 0, errp);
}

static VFIODeviceIOOps vfio_device_io_ops_ioctl;

void vfio_device_init(VFIODevice *vbasedev, int type, VFIODeviceOps *ops,
                      DeviceState *dev, bool ram_discard)
{
    vbasedev->type = type;
    vbasedev->ops = ops;
    vbasedev->io_ops = &vfio_device_io_ops_ioctl;
    vbasedev->dev = dev;
    vbasedev->fd = -1;
    vbasedev->use_region_fds = false;

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

bool vfio_device_hiod_create_and_realize(VFIODevice *vbasedev,
                                         const char *typename, Error **errp)
{
    HostIOMMUDevice *hiod;

    if (vbasedev->mdev) {
        return true;
    }

    hiod = HOST_IOMMU_DEVICE(object_new(typename));

    if (!HOST_IOMMU_DEVICE_GET_CLASS(hiod)->realize(hiod, vbasedev, errp)) {
        object_unref(hiod);
        return false;
    }

    vbasedev->hiod = hiod;
    return true;
}

VFIODevice *vfio_get_vfio_device(Object *obj)
{
    if (object_dynamic_cast(obj, TYPE_VFIO_PCI)) {
        return &VFIO_PCI_DEVICE(obj)->vbasedev;
    } else {
        return NULL;
    }
}

bool vfio_device_attach_by_iommu_type(const char *iommu_type, char *name,
                                      VFIODevice *vbasedev, AddressSpace *as,
                                      Error **errp)
{
    const VFIOIOMMUClass *ops =
        VFIO_IOMMU_CLASS(object_class_by_name(iommu_type));

    assert(ops);

    return ops->attach_device(name, vbasedev, as, errp);
}

bool vfio_device_attach(char *name, VFIODevice *vbasedev,
                        AddressSpace *as, Error **errp)
{
    const char *iommu_type = vbasedev->iommufd ?
                             TYPE_VFIO_IOMMU_IOMMUFD :
                             TYPE_VFIO_IOMMU_LEGACY;

    return vfio_device_attach_by_iommu_type(iommu_type, name, vbasedev,
                                            as, errp);
}

void vfio_device_detach(VFIODevice *vbasedev)
{
    if (!vbasedev->bcontainer) {
        return;
    }
    VFIO_IOMMU_GET_CLASS(vbasedev->bcontainer)->detach_device(vbasedev);
}

void vfio_device_prepare(VFIODevice *vbasedev, VFIOContainer *bcontainer,
                         struct vfio_device_info *info)
{
    int i;

    vbasedev->num_irqs = info->num_irqs;
    vbasedev->num_initial_regions = info->num_regions;
    vbasedev->flags = info->flags;
    vbasedev->reset_works = !!(info->flags & VFIO_DEVICE_FLAGS_RESET);

    vbasedev->bcontainer = bcontainer;
    QLIST_INSERT_HEAD(&bcontainer->device_list, vbasedev, container_next);

    QLIST_INSERT_HEAD(&vfio_device_list, vbasedev, global_next);

    vbasedev->reginfo = g_new0(struct vfio_region_info *,
                               vbasedev->num_initial_regions);
    if (vbasedev->use_region_fds) {
        vbasedev->region_fds = g_new0(int, vbasedev->num_initial_regions);
        for (i = 0; i < vbasedev->num_initial_regions; i++) {
            vbasedev->region_fds[i] = -1;
        }
    }
}

void vfio_device_unprepare(VFIODevice *vbasedev)
{
    int i;

    for (i = 0; i < vbasedev->num_initial_regions; i++) {
        g_free(vbasedev->reginfo[i]);
        if (vbasedev->region_fds != NULL && vbasedev->region_fds[i] != -1) {
            close(vbasedev->region_fds[i]);
        }
    }

    g_clear_pointer(&vbasedev->reginfo, g_free);
    g_clear_pointer(&vbasedev->region_fds, g_free);

    QLIST_REMOVE(vbasedev, container_next);
    QLIST_REMOVE(vbasedev, global_next);
    vbasedev->bcontainer = NULL;
}

/*
 * Traditional ioctl() based io
 */

static int vfio_device_io_device_feature(VFIODevice *vbasedev,
                                         struct vfio_device_feature *feature)
{
    int ret;

    ret = ioctl(vbasedev->fd, VFIO_DEVICE_FEATURE, feature);

    return ret < 0 ? -errno : ret;
}

static int vfio_device_io_get_region_info(VFIODevice *vbasedev,
                                          struct vfio_region_info *info,
                                          int *fd)
{
    int ret;

    *fd = -1;

    ret = ioctl(vbasedev->fd, VFIO_DEVICE_GET_REGION_INFO, info);

    return ret < 0 ? -errno : ret;
}

static int vfio_device_io_get_irq_info(VFIODevice *vbasedev,
                                       struct vfio_irq_info *info)
{
    int ret;

    ret = ioctl(vbasedev->fd, VFIO_DEVICE_GET_IRQ_INFO, info);

    return ret < 0 ? -errno : ret;
}

static int vfio_device_io_set_irqs(VFIODevice *vbasedev,
                                   struct vfio_irq_set *irqs)
{
    int ret;

    ret = ioctl(vbasedev->fd, VFIO_DEVICE_SET_IRQS, irqs);

    return ret < 0 ? -errno : ret;
}

static int vfio_device_io_region_read(VFIODevice *vbasedev, uint8_t index,
                                      off_t off, uint32_t size, void *data)
{
    struct vfio_region_info *info;
    int ret;

    ret = vfio_device_get_region_info(vbasedev, index, &info);
    if (ret != 0) {
        return ret;
    }

    ret = pread(vbasedev->fd, data, size, info->offset + off);

    return ret < 0 ? -errno : ret;
}

static int vfio_device_io_region_write(VFIODevice *vbasedev, uint8_t index,
                                       off_t off, uint32_t size, void *data,
                                       bool post)
{
    struct vfio_region_info *info;
    int ret;

    ret = vfio_device_get_region_info(vbasedev, index, &info);
    if (ret != 0) {
        return ret;
    }

    ret = pwrite(vbasedev->fd, data, size, info->offset + off);

    return ret < 0 ? -errno : ret;
}

static VFIODeviceIOOps vfio_device_io_ops_ioctl = {
    .device_feature = vfio_device_io_device_feature,
    .get_region_info = vfio_device_io_get_region_info,
    .get_irq_info = vfio_device_io_get_irq_info,
    .set_irqs = vfio_device_io_set_irqs,
    .region_read = vfio_device_io_region_read,
    .region_write = vfio_device_io_region_write,
};
