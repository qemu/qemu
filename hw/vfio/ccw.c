/*
 * vfio based subchannel assignment support
 *
 * Copyright 2017 IBM Corp.
 * Copyright 2019 Red Hat, Inc.
 *
 * Author(s): Dong Jia Shi <bjsdjshi@linux.vnet.ibm.com>
 *            Xiao Feng Ren <renxiaof@linux.vnet.ibm.com>
 *            Pierre Morel <pmorel@linux.vnet.ibm.com>
 *            Cornelia Huck <cohuck@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include "qemu/osdep.h"
#include <linux/vfio.h>
#include <linux/vfio_ccw.h>
#include <sys/ioctl.h>

#include "qapi/error.h"
#include "hw/sysbus.h"
#include "hw/vfio/vfio.h"
#include "hw/vfio/vfio-common.h"
#include "hw/s390x/s390-ccw.h"
#include "hw/s390x/vfio-ccw.h"
#include "hw/qdev-properties.h"
#include "hw/s390x/ccw-device.h"
#include "exec/address-spaces.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"

struct VFIOCCWDevice {
    S390CCWDevice cdev;
    VFIODevice vdev;
    uint64_t io_region_size;
    uint64_t io_region_offset;
    struct ccw_io_region *io_region;
    uint64_t async_cmd_region_size;
    uint64_t async_cmd_region_offset;
    struct ccw_cmd_region *async_cmd_region;
    EventNotifier io_notifier;
    bool force_orb_pfch;
    bool warned_orb_pfch;
};

static inline void warn_once_pfch(VFIOCCWDevice *vcdev, SubchDev *sch,
                                  const char *msg)
{
    warn_report_once_cond(&vcdev->warned_orb_pfch,
                          "vfio-ccw (devno %x.%x.%04x): %s",
                          sch->cssid, sch->ssid, sch->devno, msg);
}

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

static IOInstEnding vfio_ccw_handle_request(SubchDev *sch)
{
    S390CCWDevice *cdev = sch->driver_data;
    VFIOCCWDevice *vcdev = DO_UPCAST(VFIOCCWDevice, cdev, cdev);
    struct ccw_io_region *region = vcdev->io_region;
    int ret;

    if (!(sch->orb.ctrl0 & ORB_CTRL0_MASK_PFCH)) {
        if (!(vcdev->force_orb_pfch)) {
            warn_once_pfch(vcdev, sch, "requires PFCH flag set");
            sch_gen_unit_exception(sch);
            css_inject_io_interrupt(sch);
            return IOINST_CC_EXPECTED;
        } else {
            sch->orb.ctrl0 |= ORB_CTRL0_MASK_PFCH;
            warn_once_pfch(vcdev, sch, "PFCH flag forced");
        }
    }

    QEMU_BUILD_BUG_ON(sizeof(region->orb_area) != sizeof(ORB));
    QEMU_BUILD_BUG_ON(sizeof(region->scsw_area) != sizeof(SCSW));
    QEMU_BUILD_BUG_ON(sizeof(region->irb_area) != sizeof(IRB));

    memset(region, 0, sizeof(*region));

    memcpy(region->orb_area, &sch->orb, sizeof(ORB));
    memcpy(region->scsw_area, &sch->curr_status.scsw, sizeof(SCSW));

again:
    ret = pwrite(vcdev->vdev.fd, region,
                 vcdev->io_region_size, vcdev->io_region_offset);
    if (ret != vcdev->io_region_size) {
        if (errno == EAGAIN) {
            goto again;
        }
        error_report("vfio-ccw: wirte I/O region failed with errno=%d", errno);
        ret = -errno;
    } else {
        ret = region->ret_code;
    }
    switch (ret) {
    case 0:
        return IOINST_CC_EXPECTED;
    case -EBUSY:
        return IOINST_CC_BUSY;
    case -ENODEV:
    case -EACCES:
        return IOINST_CC_NOT_OPERATIONAL;
    case -EFAULT:
    default:
        sch_gen_unit_exception(sch);
        css_inject_io_interrupt(sch);
        return IOINST_CC_EXPECTED;
    }
}

static int vfio_ccw_handle_clear(SubchDev *sch)
{
    S390CCWDevice *cdev = sch->driver_data;
    VFIOCCWDevice *vcdev = DO_UPCAST(VFIOCCWDevice, cdev, cdev);
    struct ccw_cmd_region *region = vcdev->async_cmd_region;
    int ret;

    if (!vcdev->async_cmd_region) {
        /* Async command region not available, fall back to emulation */
        return -ENOSYS;
    }

    memset(region, 0, sizeof(*region));
    region->command = VFIO_CCW_ASYNC_CMD_CSCH;

again:
    ret = pwrite(vcdev->vdev.fd, region,
                 vcdev->async_cmd_region_size, vcdev->async_cmd_region_offset);
    if (ret != vcdev->async_cmd_region_size) {
        if (errno == EAGAIN) {
            goto again;
        }
        error_report("vfio-ccw: write cmd region failed with errno=%d", errno);
        ret = -errno;
    } else {
        ret = region->ret_code;
    }
    switch (ret) {
    case 0:
    case -ENODEV:
    case -EACCES:
        return 0;
    case -EFAULT:
    default:
        sch_gen_unit_exception(sch);
        css_inject_io_interrupt(sch);
        return 0;
    }
}

static int vfio_ccw_handle_halt(SubchDev *sch)
{
    S390CCWDevice *cdev = sch->driver_data;
    VFIOCCWDevice *vcdev = DO_UPCAST(VFIOCCWDevice, cdev, cdev);
    struct ccw_cmd_region *region = vcdev->async_cmd_region;
    int ret;

    if (!vcdev->async_cmd_region) {
        /* Async command region not available, fall back to emulation */
        return -ENOSYS;
    }

    memset(region, 0, sizeof(*region));
    region->command = VFIO_CCW_ASYNC_CMD_HSCH;

again:
    ret = pwrite(vcdev->vdev.fd, region,
                 vcdev->async_cmd_region_size, vcdev->async_cmd_region_offset);
    if (ret != vcdev->async_cmd_region_size) {
        if (errno == EAGAIN) {
            goto again;
        }
        error_report("vfio-ccw: write cmd region failed with errno=%d", errno);
        ret = -errno;
    } else {
        ret = region->ret_code;
    }
    switch (ret) {
    case 0:
    case -EBUSY:
    case -ENODEV:
    case -EACCES:
        return 0;
    case -EFAULT:
    default:
        sch_gen_unit_exception(sch);
        css_inject_io_interrupt(sch);
        return 0;
    }
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
    SCHIB *schib = &sch->curr_status;
    SCSW s;
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
            schib->scsw.flags |= SCSW_FLAGS_MASK_CC;
            schib->scsw.ctrl &= ~SCSW_CTRL_MASK_STCTL;
            schib->scsw.ctrl |= (SCSW_STCTL_ALERT | SCSW_STCTL_STATUS_PEND);
            goto read_err;
        case EFAULT:
            /* Memory problem, generate channel data check. */
            schib->scsw.ctrl &= ~SCSW_ACTL_START_PEND;
            schib->scsw.cstat = SCSW_CSTAT_DATA_CHECK;
            schib->scsw.ctrl &= ~SCSW_CTRL_MASK_STCTL;
            schib->scsw.ctrl |= SCSW_STCTL_PRIMARY | SCSW_STCTL_SECONDARY |
                       SCSW_STCTL_ALERT | SCSW_STCTL_STATUS_PEND;
            goto read_err;
        default:
            /* Error, generate channel program check. */
            schib->scsw.ctrl &= ~SCSW_ACTL_START_PEND;
            schib->scsw.cstat = SCSW_CSTAT_PROG_CHECK;
            schib->scsw.ctrl &= ~SCSW_CTRL_MASK_STCTL;
            schib->scsw.ctrl |= SCSW_STCTL_PRIMARY | SCSW_STCTL_SECONDARY |
                       SCSW_STCTL_ALERT | SCSW_STCTL_STATUS_PEND;
            goto read_err;
        }
    } else if (size != vcdev->io_region_size) {
        /* Information transfer error, generate channel-control check. */
        schib->scsw.ctrl &= ~SCSW_ACTL_START_PEND;
        schib->scsw.cstat = SCSW_CSTAT_CHN_CTRL_CHK;
        schib->scsw.ctrl &= ~SCSW_CTRL_MASK_STCTL;
        schib->scsw.ctrl |= SCSW_STCTL_PRIMARY | SCSW_STCTL_SECONDARY |
                   SCSW_STCTL_ALERT | SCSW_STCTL_STATUS_PEND;
        goto read_err;
    }

    memcpy(&irb, region->irb_area, sizeof(IRB));

    /* Update control block via irb. */
    s = schib->scsw;
    copy_scsw_to_guest(&s, &irb.scsw);
    schib->scsw = s;

    /* If a uint check is pending, copy sense data. */
    if ((schib->scsw.dstat & SCSW_DSTAT_UNIT_CHECK) &&
        (schib->pmcw.chars & PMCW_CHARS_MASK_CSENSE)) {
        memcpy(sch->sense_data, irb.ecw, sizeof(irb.ecw));
    }

read_err:
    css_inject_io_interrupt(sch);
}

static void vfio_ccw_register_io_notifier(VFIOCCWDevice *vcdev, Error **errp)
{
    VFIODevice *vdev = &vcdev->vdev;
    struct vfio_irq_info *irq_info;
    size_t argsz;
    int fd;

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

    fd = event_notifier_get_fd(&vcdev->io_notifier);
    qemu_set_fd_handler(fd, vfio_ccw_io_notifier_handler, NULL, vcdev);

    if (vfio_set_irq_signaling(vdev, VFIO_CCW_IO_IRQ_INDEX, 0,
                               VFIO_IRQ_SET_ACTION_TRIGGER, fd, errp)) {
        qemu_set_fd_handler(fd, NULL, NULL, vcdev);
        event_notifier_cleanup(&vcdev->io_notifier);
    }

out_free_info:
    g_free(irq_info);
}

static void vfio_ccw_unregister_io_notifier(VFIOCCWDevice *vcdev)
{
    Error *err = NULL;

    if (vfio_set_irq_signaling(&vcdev->vdev, VFIO_CCW_IO_IRQ_INDEX, 0,
                               VFIO_IRQ_SET_ACTION_TRIGGER, -1, &err)) {
        error_reportf_err(err, VFIO_MSG_PREFIX, vcdev->vdev.name);
    }

    qemu_set_fd_handler(event_notifier_get_fd(&vcdev->io_notifier),
                        NULL, NULL, vcdev);
    event_notifier_cleanup(&vcdev->io_notifier);
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

    /*
     * We always expect at least the I/O region to be present. We also
     * may have a variable number of regions governed by capabilities.
     */
    if (vdev->num_regions < VFIO_CCW_CONFIG_REGION_INDEX + 1) {
        error_setg(errp, "vfio: too few regions (%u), expected at least %u",
                   vdev->num_regions, VFIO_CCW_CONFIG_REGION_INDEX + 1);
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

    /* check for the optional async command region */
    ret = vfio_get_dev_region_info(vdev, VFIO_REGION_TYPE_CCW,
                                   VFIO_REGION_SUBTYPE_CCW_ASYNC_CMD, &info);
    if (!ret) {
        vcdev->async_cmd_region_size = info->size;
        if (sizeof(*vcdev->async_cmd_region) != vcdev->async_cmd_region_size) {
            error_setg(errp, "vfio: Unexpected size of the async cmd region");
            g_free(vcdev->io_region);
            g_free(info);
            return;
        }
        vcdev->async_cmd_region_offset = info->offset;
        vcdev->async_cmd_region = g_malloc0(info->size);
    }

    g_free(info);
}

static void vfio_ccw_put_region(VFIOCCWDevice *vcdev)
{
    g_free(vcdev->async_cmd_region);
    g_free(vcdev->io_region);
}

static void vfio_ccw_put_device(VFIOCCWDevice *vcdev)
{
    g_free(vcdev->vdev.name);
    vfio_put_base_device(&vcdev->vdev);
}

static void vfio_ccw_get_device(VFIOGroup *group, VFIOCCWDevice *vcdev,
                                Error **errp)
{
    char *name = g_strdup_printf("%x.%x.%04x", vcdev->cdev.hostid.cssid,
                                 vcdev->cdev.hostid.ssid,
                                 vcdev->cdev.hostid.devid);
    VFIODevice *vbasedev;

    QLIST_FOREACH(vbasedev, &group->device_list, next) {
        if (strcmp(vbasedev->name, name) == 0) {
            error_setg(errp, "vfio: subchannel %s has already been attached",
                       name);
            goto out_err;
        }
    }

    /*
     * All vfio-ccw devices are believed to operate in a way compatible with
     * memory ballooning, ie. pages pinned in the host are in the current
     * working set of the guest driver and therefore never overlap with pages
     * available to the guest balloon driver.  This needs to be set before
     * vfio_get_device() for vfio common to handle the balloon inhibitor.
     */
    vcdev->vdev.balloon_allowed = true;

    if (vfio_get_device(group, vcdev->cdev.mdevid, &vcdev->vdev, errp)) {
        goto out_err;
    }

    vcdev->vdev.ops = &vfio_ccw_ops;
    vcdev->vdev.type = VFIO_DEVICE_TYPE_CCW;
    vcdev->vdev.name = name;
    vcdev->vdev.dev = &vcdev->cdev.parent_obj.parent_obj;

    return;

out_err:
    g_free(name);
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

    vfio_ccw_get_device(group, vcdev, &err);
    if (err) {
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
    vfio_ccw_put_device(vcdev);
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
    vfio_ccw_put_device(vcdev);
    vfio_put_group(group);

    if (cdc->unrealize) {
        cdc->unrealize(cdev, errp);
    }
}

static Property vfio_ccw_properties[] = {
    DEFINE_PROP_STRING("sysfsdev", VFIOCCWDevice, vdev.sysfsdev),
    DEFINE_PROP_BOOL("force-orb-pfch", VFIOCCWDevice, force_orb_pfch, false),
    DEFINE_PROP_END_OF_LIST(),
};

static const VMStateDescription vfio_ccw_vmstate = {
    .name = "vfio-ccw",
    .unmigratable = 1,
};

static void vfio_ccw_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    S390CCWDeviceClass *cdc = S390_CCW_DEVICE_CLASS(klass);

    dc->props = vfio_ccw_properties;
    dc->vmsd = &vfio_ccw_vmstate;
    dc->desc = "VFIO-based subchannel assignment";
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->realize = vfio_ccw_realize;
    dc->unrealize = vfio_ccw_unrealize;
    dc->reset = vfio_ccw_reset;

    cdc->handle_request = vfio_ccw_handle_request;
    cdc->handle_halt = vfio_ccw_handle_halt;
    cdc->handle_clear = vfio_ccw_handle_clear;
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
