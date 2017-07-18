/*
 * vfio based subchannel assignment support
 *
 * Copyright 2017 IBM Corp.
 * Author(s): Dong Jia Shi <bjsdjshi@linux.vnet.ibm.com>
 *            Xiao Feng Ren <renxiaof@linux.vnet.ibm.com>
 *            Pierre Morel <pmorel@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or(at
 * your option) any version. See the COPYING file in the top-level
 * directory.
 */

#include <linux/vfio.h>
#include <linux/vfio_ccw.h>
#include <sys/ioctl.h>

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "hw/vfio/vfio.h"
#include "hw/vfio/vfio-common.h"
#include "hw/s390x/s390-ccw.h"
#include "hw/s390x/ccw-device.h"
#include "qemu/error-report.h"

#define TYPE_VFIO_CCW "vfio-ccw"
typedef struct VFIOCCWDevice {
    S390CCWDevice cdev;
    VFIODevice vdev;
    uint64_t io_region_size;
    uint64_t io_region_offset;
    struct ccw_io_region *io_region;
    EventNotifier io_notifier;
} VFIOCCWDevice;

static void vfio_ccw_compute_needs_reset(VFIODevice *vdev)
{
    vdev->needs_reset = false;
}

/*
 * We don't need vfio_hot_reset_multi and vfio_eoi operations for
 * vfio_ccw device now.
 */
struct VFIODeviceOps vfio_ccw_ops = {
    .vfio_compute_needs_reset = vfio_ccw_compute_needs_reset,
};

static int vfio_ccw_handle_request(ORB *orb, SCSW *scsw, void *data)
{
    S390CCWDevice *cdev = data;
    VFIOCCWDevice *vcdev = DO_UPCAST(VFIOCCWDevice, cdev, cdev);
    struct ccw_io_region *region = vcdev->io_region;
    int ret;

    QEMU_BUILD_BUG_ON(sizeof(region->orb_area) != sizeof(ORB));
    QEMU_BUILD_BUG_ON(sizeof(region->scsw_area) != sizeof(SCSW));
    QEMU_BUILD_BUG_ON(sizeof(region->irb_area) != sizeof(IRB));

    memset(region, 0, sizeof(*region));

    memcpy(region->orb_area, orb, sizeof(ORB));
    memcpy(region->scsw_area, scsw, sizeof(SCSW));

again:
    ret = pwrite(vcdev->vdev.fd, region,
                 vcdev->io_region_size, vcdev->io_region_offset);
    if (ret != vcdev->io_region_size) {
        if (errno == EAGAIN) {
            goto again;
        }
        error_report("vfio-ccw: wirte I/O region failed with errno=%d", errno);
        return -errno;
    }

    return region->ret_code;
}

static void vfio_ccw_reset(DeviceState *dev)
{
    CcwDevice *ccw_dev = DO_UPCAST(CcwDevice, parent_obj, dev);
    S390CCWDevice *cdev = DO_UPCAST(S390CCWDevice, parent_obj, ccw_dev);
    VFIOCCWDevice *vcdev = DO_UPCAST(VFIOCCWDevice, cdev, cdev);

    ioctl(vcdev->vdev.fd, VFIO_DEVICE_RESET);
}

static void vfio_ccw_io_notifier_handler(void *opaque)
{
    VFIOCCWDevice *vcdev = opaque;
    struct ccw_io_region *region = vcdev->io_region;
    S390CCWDevice *cdev = S390_CCW_DEVICE(vcdev);
    CcwDevice *ccw_dev = CCW_DEVICE(cdev);
    SubchDev *sch = ccw_dev->sch;
    SCSW *s = &sch->curr_status.scsw;
    PMCW *p = &sch->curr_status.pmcw;
    IRB irb;
    int size;

    if (!event_notifier_test_and_clear(&vcdev->io_notifier)) {
        return;
    }

    size = pread(vcdev->vdev.fd, region, vcdev->io_region_size,
                 vcdev->io_region_offset);
    if (size == -1) {
        switch (errno) {
        case ENODEV:
            /* Generate a deferred cc 3 condition. */
            s->flags |= SCSW_FLAGS_MASK_CC;
            s->ctrl &= ~SCSW_CTRL_MASK_STCTL;
            s->ctrl |= (SCSW_STCTL_ALERT | SCSW_STCTL_STATUS_PEND);
            goto read_err;
        case EFAULT:
            /* Memory problem, generate channel data check. */
            s->ctrl &= ~SCSW_ACTL_START_PEND;
            s->cstat = SCSW_CSTAT_DATA_CHECK;
            s->ctrl &= ~SCSW_CTRL_MASK_STCTL;
            s->ctrl |= SCSW_STCTL_PRIMARY | SCSW_STCTL_SECONDARY |
                       SCSW_STCTL_ALERT | SCSW_STCTL_STATUS_PEND;
            goto read_err;
        default:
            /* Error, generate channel program check. */
            s->ctrl &= ~SCSW_ACTL_START_PEND;
            s->cstat = SCSW_CSTAT_PROG_CHECK;
            s->ctrl &= ~SCSW_CTRL_MASK_STCTL;
            s->ctrl |= SCSW_STCTL_PRIMARY | SCSW_STCTL_SECONDARY |
                       SCSW_STCTL_ALERT | SCSW_STCTL_STATUS_PEND;
            goto read_err;
        }
    } else if (size != vcdev->io_region_size) {
        /* Information transfer error, generate channel-control check. */
        s->ctrl &= ~SCSW_ACTL_START_PEND;
        s->cstat = SCSW_CSTAT_CHN_CTRL_CHK;
        s->ctrl &= ~SCSW_CTRL_MASK_STCTL;
        s->ctrl |= SCSW_STCTL_PRIMARY | SCSW_STCTL_SECONDARY |
                   SCSW_STCTL_ALERT | SCSW_STCTL_STATUS_PEND;
        goto read_err;
    }

    memcpy(&irb, region->irb_area, sizeof(IRB));

    /* Update control block via irb. */
    copy_scsw_to_guest(s, &irb.scsw);

    /* If a uint check is pending, copy sense data. */
    if ((s->dstat & SCSW_DSTAT_UNIT_CHECK) &&
        (p->chars & PMCW_CHARS_MASK_CSENSE)) {
        memcpy(sch->sense_data, irb.ecw, sizeof(irb.ecw));
    }

read_err:
    css_inject_io_interrupt(sch);
}

static void vfio_ccw_register_io_notifier(VFIOCCWDevice *vcdev, Error **errp)
{
    VFIODevice *vdev = &vcdev->vdev;
    struct vfio_irq_info *irq_info;
    struct vfio_irq_set *irq_set;
    size_t argsz;
    int32_t *pfd;

    if (vdev->num_irqs < VFIO_CCW_IO_IRQ_INDEX + 1) {
        error_setg(errp, "vfio: unexpected number of io irqs %u",
                   vdev->num_irqs);
        return;
    }

    argsz = sizeof(*irq_info);
    irq_info = g_malloc0(argsz);
    irq_info->index = VFIO_CCW_IO_IRQ_INDEX;
    irq_info->argsz = argsz;
    if (ioctl(vdev->fd, VFIO_DEVICE_GET_IRQ_INFO,
              irq_info) < 0 || irq_info->count < 1) {
        error_setg_errno(errp, errno, "vfio: Error getting irq info");
        goto out_free_info;
    }

    if (event_notifier_init(&vcdev->io_notifier, 0)) {
        error_setg_errno(errp, errno,
                         "vfio: Unable to init event notifier for IO");
        goto out_free_info;
    }

    argsz = sizeof(*irq_set) + sizeof(*pfd);
    irq_set = g_malloc0(argsz);
    irq_set->argsz = argsz;
    irq_set->flags = VFIO_IRQ_SET_DATA_EVENTFD |
                     VFIO_IRQ_SET_ACTION_TRIGGER;
    irq_set->index = VFIO_CCW_IO_IRQ_INDEX;
    irq_set->start = 0;
    irq_set->count = 1;
    pfd = (int32_t *) &irq_set->data;

    *pfd = event_notifier_get_fd(&vcdev->io_notifier);
    qemu_set_fd_handler(*pfd, vfio_ccw_io_notifier_handler, NULL, vcdev);
    if (ioctl(vdev->fd, VFIO_DEVICE_SET_IRQS, irq_set)) {
        error_setg(errp, "vfio: Failed to set up io notification");
        qemu_set_fd_handler(*pfd, NULL, NULL, vcdev);
        event_notifier_cleanup(&vcdev->io_notifier);
    }

    g_free(irq_set);

out_free_info:
    g_free(irq_info);
}

static void vfio_ccw_unregister_io_notifier(VFIOCCWDevice *vcdev)
{
    struct vfio_irq_set *irq_set;
    size_t argsz;
    int32_t *pfd;

    argsz = sizeof(*irq_set) + sizeof(*pfd);
    irq_set = g_malloc0(argsz);
    irq_set->argsz = argsz;
    irq_set->flags = VFIO_IRQ_SET_DATA_EVENTFD |
                     VFIO_IRQ_SET_ACTION_TRIGGER;
    irq_set->index = VFIO_CCW_IO_IRQ_INDEX;
    irq_set->start = 0;
    irq_set->count = 1;
    pfd = (int32_t *) &irq_set->data;
    *pfd = -1;

    if (ioctl(vcdev->vdev.fd, VFIO_DEVICE_SET_IRQS, irq_set)) {
        error_report("vfio: Failed to de-assign device io fd: %m");
    }

    qemu_set_fd_handler(event_notifier_get_fd(&vcdev->io_notifier),
                        NULL, NULL, vcdev);
    event_notifier_cleanup(&vcdev->io_notifier);

    g_free(irq_set);
}

static void vfio_ccw_get_region(VFIOCCWDevice *vcdev, Error **errp)
{
    VFIODevice *vdev = &vcdev->vdev;
    struct vfio_region_info *info;
    int ret;

    /* Sanity check device */
    if (!(vdev->flags & VFIO_DEVICE_FLAGS_CCW)) {
        error_setg(errp, "vfio: Um, this isn't a vfio-ccw device");
        return;
    }

    if (vdev->num_regions < VFIO_CCW_CONFIG_REGION_INDEX + 1) {
        error_setg(errp, "vfio: Unexpected number of the I/O region %u",
                   vdev->num_regions);
        return;
    }

    ret = vfio_get_region_info(vdev, VFIO_CCW_CONFIG_REGION_INDEX, &info);
    if (ret) {
        error_setg_errno(errp, -ret, "vfio: Error getting config info");
        return;
    }

    vcdev->io_region_size = info->size;
    if (sizeof(*vcdev->io_region) != vcdev->io_region_size) {
        error_setg(errp, "vfio: Unexpected size of the I/O region");
        g_free(info);
        return;
    }

    vcdev->io_region_offset = info->offset;
    vcdev->io_region = g_malloc0(info->size);

    g_free(info);
}

static void vfio_ccw_put_region(VFIOCCWDevice *vcdev)
{
    g_free(vcdev->io_region);
}

static void vfio_put_device(VFIOCCWDevice *vcdev)
{
    g_free(vcdev->vdev.name);
    vfio_put_base_device(&vcdev->vdev);
}

static VFIOGroup *vfio_ccw_get_group(S390CCWDevice *cdev, Error **errp)
{
    char *tmp, group_path[PATH_MAX];
    ssize_t len;
    int groupid;

    tmp = g_strdup_printf("/sys/bus/css/devices/%x.%x.%04x/%s/iommu_group",
                          cdev->hostid.cssid, cdev->hostid.ssid,
                          cdev->hostid.devid, cdev->mdevid);
    len = readlink(tmp, group_path, sizeof(group_path));
    g_free(tmp);

    if (len <= 0 || len >= sizeof(group_path)) {
        error_setg(errp, "vfio: no iommu_group found");
        return NULL;
    }

    group_path[len] = 0;

    if (sscanf(basename(group_path), "%d", &groupid) != 1) {
        error_setg(errp, "vfio: failed to read %s", group_path);
        return NULL;
    }

    return vfio_get_group(groupid, &address_space_memory, errp);
}

static void vfio_ccw_realize(DeviceState *dev, Error **errp)
{
    VFIODevice *vbasedev;
    VFIOGroup *group;
    CcwDevice *ccw_dev = DO_UPCAST(CcwDevice, parent_obj, dev);
    S390CCWDevice *cdev = DO_UPCAST(S390CCWDevice, parent_obj, ccw_dev);
    VFIOCCWDevice *vcdev = DO_UPCAST(VFIOCCWDevice, cdev, cdev);
    S390CCWDeviceClass *cdc = S390_CCW_DEVICE_GET_CLASS(cdev);
    Error *err = NULL;

    /* Call the class init function for subchannel. */
    if (cdc->realize) {
        cdc->realize(cdev, vcdev->vdev.sysfsdev, &err);
        if (err) {
            goto out_err_propagate;
        }
    }

    group = vfio_ccw_get_group(cdev, &err);
    if (!group) {
        goto out_group_err;
    }

    vcdev->vdev.ops = &vfio_ccw_ops;
    vcdev->vdev.type = VFIO_DEVICE_TYPE_CCW;
    vcdev->vdev.name = g_strdup_printf("%x.%x.%04x", cdev->hostid.cssid,
                                       cdev->hostid.ssid, cdev->hostid.devid);
    vcdev->vdev.dev = dev;
    QLIST_FOREACH(vbasedev, &group->device_list, next) {
        if (strcmp(vbasedev->name, vcdev->vdev.name) == 0) {
            error_setg(&err, "vfio: subchannel %s has already been attached",
                       vcdev->vdev.name);
            goto out_device_err;
        }
    }

    if (vfio_get_device(group, cdev->mdevid, &vcdev->vdev, &err)) {
        goto out_device_err;
    }

    vfio_ccw_get_region(vcdev, &err);
    if (err) {
        goto out_region_err;
    }

    vfio_ccw_register_io_notifier(vcdev, &err);
    if (err) {
        goto out_notifier_err;
    }

    return;

out_notifier_err:
    vfio_ccw_put_region(vcdev);
out_region_err:
    vfio_put_device(vcdev);
out_device_err:
    vfio_put_group(group);
out_group_err:
    if (cdc->unrealize) {
        cdc->unrealize(cdev, NULL);
    }
out_err_propagate:
    error_propagate(errp, err);
}

static void vfio_ccw_unrealize(DeviceState *dev, Error **errp)
{
    CcwDevice *ccw_dev = DO_UPCAST(CcwDevice, parent_obj, dev);
    S390CCWDevice *cdev = DO_UPCAST(S390CCWDevice, parent_obj, ccw_dev);
    VFIOCCWDevice *vcdev = DO_UPCAST(VFIOCCWDevice, cdev, cdev);
    S390CCWDeviceClass *cdc = S390_CCW_DEVICE_GET_CLASS(cdev);
    VFIOGroup *group = vcdev->vdev.group;

    vfio_ccw_unregister_io_notifier(vcdev);
    vfio_ccw_put_region(vcdev);
    vfio_put_device(vcdev);
    vfio_put_group(group);

    if (cdc->unrealize) {
        cdc->unrealize(cdev, errp);
    }
}

static Property vfio_ccw_properties[] = {
    DEFINE_PROP_STRING("sysfsdev", VFIOCCWDevice, vdev.sysfsdev),
    DEFINE_PROP_END_OF_LIST(),
};

static const VMStateDescription vfio_ccw_vmstate = {
    .name = TYPE_VFIO_CCW,
    .unmigratable = 1,
};

static void vfio_ccw_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    S390CCWDeviceClass *cdc = S390_CCW_DEVICE_CLASS(klass);

    dc->props = vfio_ccw_properties;
    dc->vmsd = &vfio_ccw_vmstate;
    dc->desc = "VFIO-based subchannel assignment";
    dc->realize = vfio_ccw_realize;
    dc->unrealize = vfio_ccw_unrealize;
    dc->reset = vfio_ccw_reset;

    cdc->handle_request = vfio_ccw_handle_request;
}

static const TypeInfo vfio_ccw_info = {
    .name = TYPE_VFIO_CCW,
    .parent = TYPE_S390_CCW,
    .instance_size = sizeof(VFIOCCWDevice),
    .class_init = vfio_ccw_class_init,
};

static void register_vfio_ccw_type(void)
{
    type_register_static(&vfio_ccw_info);
}

type_init(register_vfio_ccw_type)
