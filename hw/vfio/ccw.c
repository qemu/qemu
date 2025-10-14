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
#include CONFIG_DEVICES /* CONFIG_IOMMUFD */
#include <linux/vfio.h>
#include <linux/vfio_ccw.h>
#include <sys/ioctl.h>

#include "qapi/error.h"
#include "hw/vfio/vfio-device.h"
#include "system/iommufd.h"
#include "hw/s390x/s390-ccw.h"
#include "hw/s390x/vfio-ccw.h"
#include "hw/qdev-properties.h"
#include "hw/s390x/ccw-device.h"
#include "system/address-spaces.h"
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
    uint64_t schib_region_size;
    uint64_t schib_region_offset;
    struct ccw_schib_region *schib_region;
    uint64_t crw_region_size;
    uint64_t crw_region_offset;
    struct ccw_crw_region *crw_region;
    EventNotifier io_notifier;
    EventNotifier crw_notifier;
    EventNotifier req_notifier;
    bool force_orb_pfch;
};

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
    VFIOCCWDevice *vcdev = VFIO_CCW(sch->driver_data);
    struct ccw_io_region *region = vcdev->io_region;
    int ret;

    if (!(sch->orb.ctrl0 & ORB_CTRL0_MASK_PFCH) && vcdev->force_orb_pfch) {
        sch->orb.ctrl0 |= ORB_CTRL0_MASK_PFCH;
        warn_report_once("vfio-ccw (devno %x.%x.%04x): PFCH flag forced",
                         sch->cssid, sch->ssid, sch->devno);
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
        error_report("vfio-ccw: write I/O region failed with errno=%d", errno);
        ret = errno ? -errno : -EFAULT;
    } else {
        ret = 0;
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

static IOInstEnding vfio_ccw_handle_store(SubchDev *sch)
{
    VFIOCCWDevice *vcdev = VFIO_CCW(sch->driver_data);
    SCHIB *schib = &sch->curr_status;
    struct ccw_schib_region *region = vcdev->schib_region;
    SCHIB *s;
    int ret;

    /* schib region not available so nothing else to do */
    if (!region) {
        return IOINST_CC_EXPECTED;
    }

    memset(region, 0, sizeof(*region));
    ret = pread(vcdev->vdev.fd, region, vcdev->schib_region_size,
                vcdev->schib_region_offset);

    if (ret == -1) {
        /*
         * Device is probably damaged, but store subchannel does not
         * have a nonzero cc defined for this scenario.  Log an error,
         * and presume things are otherwise fine.
         */
        error_report("vfio-ccw: store region read failed with errno=%d", errno);
        return IOINST_CC_EXPECTED;
    }

    /*
     * Selectively copy path-related bits of the SCHIB,
     * rather than copying the entire struct.
     */
    s = (SCHIB *)region->schib_area;
    schib->pmcw.pnom = s->pmcw.pnom;
    schib->pmcw.lpum = s->pmcw.lpum;
    schib->pmcw.pam = s->pmcw.pam;
    schib->pmcw.pom = s->pmcw.pom;

    if (s->scsw.flags & SCSW_FLAGS_MASK_PNO) {
        schib->scsw.flags |= SCSW_FLAGS_MASK_PNO;
    }

    return IOINST_CC_EXPECTED;
}

static int vfio_ccw_handle_clear(SubchDev *sch)
{
    VFIOCCWDevice *vcdev = VFIO_CCW(sch->driver_data);
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
        ret = errno ? -errno : -EFAULT;
    } else {
        ret = 0;
    }
    switch (ret) {
    case 0:
    case -ENODEV:
    case -EACCES:
        return ret;
    case -EFAULT:
    default:
        sch_gen_unit_exception(sch);
        css_inject_io_interrupt(sch);
        return 0;
    }
}

static int vfio_ccw_handle_halt(SubchDev *sch)
{
    VFIOCCWDevice *vcdev = VFIO_CCW(sch->driver_data);
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
        ret = errno ? -errno : -EFAULT;
    } else {
        ret = 0;
    }
    switch (ret) {
    case 0:
    case -EBUSY:
    case -ENODEV:
    case -EACCES:
        return ret;
    case -EFAULT:
    default:
        sch_gen_unit_exception(sch);
        css_inject_io_interrupt(sch);
        return 0;
    }
}

static void vfio_ccw_reset(DeviceState *dev)
{
    VFIOCCWDevice *vcdev = VFIO_CCW(dev);

    ioctl(vcdev->vdev.fd, VFIO_DEVICE_RESET);
}

static void vfio_ccw_crw_read(VFIOCCWDevice *vcdev)
{
    struct ccw_crw_region *region = vcdev->crw_region;
    CRW crw;
    int size;

    /* Keep reading CRWs as long as data is returned */
    do {
        memset(region, 0, sizeof(*region));
        size = pread(vcdev->vdev.fd, region, vcdev->crw_region_size,
                     vcdev->crw_region_offset);

        if (size == -1) {
            error_report("vfio-ccw: Read crw region failed with errno=%d",
                         errno);
            break;
        }

        if (region->crw == 0) {
            /* No more CRWs to queue */
            break;
        }

        memcpy(&crw, &region->crw, sizeof(CRW));

        css_crw_add_to_queue(crw);
    } while (1);
}

static void vfio_ccw_req_notifier_handler(void *opaque)
{
    VFIOCCWDevice *vcdev = opaque;
    Error *err = NULL;

    if (!event_notifier_test_and_clear(&vcdev->req_notifier)) {
        return;
    }

    qdev_unplug(DEVICE(vcdev), &err);
    if (err) {
        warn_reportf_err(err, VFIO_MSG_PREFIX, vcdev->vdev.name);
    }
}

static void vfio_ccw_crw_notifier_handler(void *opaque)
{
    VFIOCCWDevice *vcdev = opaque;

    while (event_notifier_test_and_clear(&vcdev->crw_notifier)) {
        vfio_ccw_crw_read(vcdev);
    }
}

static void vfio_ccw_io_notifier_handler(void *opaque)
{
    VFIOCCWDevice *vcdev = opaque;
    struct ccw_io_region *region = vcdev->io_region;
    CcwDevice *ccw_dev = CCW_DEVICE(vcdev);
    SubchDev *sch = ccw_dev->sch;
    SCHIB *schib = &sch->curr_status;
    SCSW s;
    IRB irb;
    ESW esw;
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

    copy_esw_to_guest(&esw, &irb.esw);
    sch->esw = esw;

    /* If a uint check is pending, copy sense data. */
    if ((schib->scsw.dstat & SCSW_DSTAT_UNIT_CHECK) &&
        (schib->pmcw.chars & PMCW_CHARS_MASK_CSENSE)) {
        memcpy(sch->sense_data, irb.ecw, sizeof(irb.ecw));
    }

read_err:
    css_inject_io_interrupt(sch);
}

static bool vfio_ccw_register_irq_notifier(VFIOCCWDevice *vcdev,
                                           unsigned int irq,
                                           Error **errp)
{
    VFIODevice *vdev = &vcdev->vdev;
    struct vfio_irq_info irq_info;
    int ret;
    int fd;
    EventNotifier *notifier;
    IOHandler *fd_read;

    switch (irq) {
    case VFIO_CCW_IO_IRQ_INDEX:
        notifier = &vcdev->io_notifier;
        fd_read = vfio_ccw_io_notifier_handler;
        break;
    case VFIO_CCW_CRW_IRQ_INDEX:
        notifier = &vcdev->crw_notifier;
        fd_read = vfio_ccw_crw_notifier_handler;
        break;
    case VFIO_CCW_REQ_IRQ_INDEX:
        notifier = &vcdev->req_notifier;
        fd_read = vfio_ccw_req_notifier_handler;
        break;
    default:
        error_setg(errp, "vfio: Unsupported device irq(%d)", irq);
        return false;
    }

    if (vdev->num_irqs < irq + 1) {
        error_setg(errp, "vfio: IRQ %u not available (number of irqs %u)",
                   irq, vdev->num_irqs);
        return false;
    }

    ret = vfio_device_get_irq_info(vdev, irq, &irq_info);

    if (ret < 0) {
        error_setg_errno(errp, -ret, "vfio: Error getting irq info");
        return false;
    }

    if (irq_info.count < 1) {
        error_setg(errp, "vfio: Error getting irq info, count=0");
        return false;
    }

    if (event_notifier_init(notifier, 0)) {
        error_setg_errno(errp, errno,
                         "vfio: Unable to init event notifier for irq (%d)",
                         irq);
        return false;
    }

    fd = event_notifier_get_fd(notifier);
    qemu_set_fd_handler(fd, fd_read, NULL, vcdev);

    if (!vfio_device_irq_set_signaling(vdev, irq, 0,
                                       VFIO_IRQ_SET_ACTION_TRIGGER, fd, errp)) {
        qemu_set_fd_handler(fd, NULL, NULL, vcdev);
        event_notifier_cleanup(notifier);
    }

    return true;
}

static void vfio_ccw_unregister_irq_notifier(VFIOCCWDevice *vcdev,
                                             unsigned int irq)
{
    Error *err = NULL;
    EventNotifier *notifier;

    switch (irq) {
    case VFIO_CCW_IO_IRQ_INDEX:
        notifier = &vcdev->io_notifier;
        break;
    case VFIO_CCW_CRW_IRQ_INDEX:
        notifier = &vcdev->crw_notifier;
        break;
    case VFIO_CCW_REQ_IRQ_INDEX:
        notifier = &vcdev->req_notifier;
        break;
    default:
        error_report("vfio: Unsupported device irq(%d)", irq);
        return;
    }

    if (!vfio_device_irq_set_signaling(&vcdev->vdev, irq, 0,
                                       VFIO_IRQ_SET_ACTION_TRIGGER, -1, &err)) {
        warn_reportf_err(err, VFIO_MSG_PREFIX, vcdev->vdev.name);
    }

    qemu_set_fd_handler(event_notifier_get_fd(notifier),
                        NULL, NULL, vcdev);
    event_notifier_cleanup(notifier);
}

static bool vfio_ccw_get_region(VFIOCCWDevice *vcdev, Error **errp)
{
    VFIODevice *vdev = &vcdev->vdev;
    struct vfio_region_info *info;
    int ret;

    /* Sanity check device */
    if (!(vdev->flags & VFIO_DEVICE_FLAGS_CCW)) {
        error_setg(errp, "vfio: Um, this isn't a vfio-ccw device");
        return false;
    }

    /*
     * We always expect at least the I/O region to be present. We also
     * may have a variable number of regions governed by capabilities.
     */
    if (vdev->num_initial_regions < VFIO_CCW_CONFIG_REGION_INDEX + 1) {
        error_setg(errp, "vfio: too few regions (%u), expected at least %u",
                   vdev->num_initial_regions, VFIO_CCW_CONFIG_REGION_INDEX + 1);
        return false;
    }

    ret = vfio_device_get_region_info(vdev, VFIO_CCW_CONFIG_REGION_INDEX, &info);
    if (ret) {
        error_setg_errno(errp, -ret, "vfio: Error getting config info");
        return false;
    }

    vcdev->io_region_size = info->size;
    if (sizeof(*vcdev->io_region) != vcdev->io_region_size) {
        error_setg(errp, "vfio: Unexpected size of the I/O region");
        goto out_err;
    }

    vcdev->io_region_offset = info->offset;
    vcdev->io_region = g_malloc0(info->size);

    /* check for the optional async command region */
    ret = vfio_device_get_region_info_type(vdev, VFIO_REGION_TYPE_CCW,
                                           VFIO_REGION_SUBTYPE_CCW_ASYNC_CMD, &info);
    if (!ret) {
        vcdev->async_cmd_region_size = info->size;
        if (sizeof(*vcdev->async_cmd_region) != vcdev->async_cmd_region_size) {
            error_setg(errp, "vfio: Unexpected size of the async cmd region");
            goto out_err;
        }
        vcdev->async_cmd_region_offset = info->offset;
        vcdev->async_cmd_region = g_malloc0(info->size);
    }

    ret = vfio_device_get_region_info_type(vdev, VFIO_REGION_TYPE_CCW,
                                           VFIO_REGION_SUBTYPE_CCW_SCHIB, &info);
    if (!ret) {
        vcdev->schib_region_size = info->size;
        if (sizeof(*vcdev->schib_region) != vcdev->schib_region_size) {
            error_setg(errp, "vfio: Unexpected size of the schib region");
            goto out_err;
        }
        vcdev->schib_region_offset = info->offset;
        vcdev->schib_region = g_malloc(info->size);
    }

    ret = vfio_device_get_region_info_type(vdev, VFIO_REGION_TYPE_CCW,
                                           VFIO_REGION_SUBTYPE_CCW_CRW, &info);

    if (!ret) {
        vcdev->crw_region_size = info->size;
        if (sizeof(*vcdev->crw_region) != vcdev->crw_region_size) {
            error_setg(errp, "vfio: Unexpected size of the CRW region");
            goto out_err;
        }
        vcdev->crw_region_offset = info->offset;
        vcdev->crw_region = g_malloc(info->size);
    }

    return true;

out_err:
    g_free(vcdev->crw_region);
    g_free(vcdev->schib_region);
    g_free(vcdev->async_cmd_region);
    g_free(vcdev->io_region);
    return false;
}

static void vfio_ccw_put_region(VFIOCCWDevice *vcdev)
{
    g_free(vcdev->crw_region);
    g_free(vcdev->schib_region);
    g_free(vcdev->async_cmd_region);
    g_free(vcdev->io_region);
}

static void vfio_ccw_realize(DeviceState *dev, Error **errp)
{
    S390CCWDevice *cdev = S390_CCW_DEVICE(dev);
    VFIOCCWDevice *vcdev = VFIO_CCW(cdev);
    S390CCWDeviceClass *cdc = S390_CCW_DEVICE_GET_CLASS(cdev);
    VFIODevice *vbasedev = &vcdev->vdev;
    Error *err = NULL;

    /* Call the class init function for subchannel. */
    if (cdc->realize) {
        if (!cdc->realize(cdev, vcdev->vdev.sysfsdev, errp)) {
            return;
        }
    }

    if (!vfio_device_get_name(vbasedev, errp)) {
        goto out_unrealize;
    }

    if (!vfio_device_attach(cdev->mdevid, vbasedev,
                            &address_space_memory, errp)) {
        goto out_attach_dev_err;
    }

    if (!vfio_ccw_get_region(vcdev, errp)) {
        goto out_region_err;
    }

    if (!vfio_ccw_register_irq_notifier(vcdev, VFIO_CCW_IO_IRQ_INDEX, errp)) {
        goto out_io_notifier_err;
    }

    if (vcdev->crw_region) {
        if (!vfio_ccw_register_irq_notifier(vcdev, VFIO_CCW_CRW_IRQ_INDEX,
                                            errp)) {
            goto out_irq_notifier_err;
        }
    }

    if (!vfio_ccw_register_irq_notifier(vcdev, VFIO_CCW_REQ_IRQ_INDEX, &err)) {
        /*
         * Report this error, but do not make it a failing condition.
         * Lack of this IRQ in the host does not prevent normal operation.
         */
        warn_report_err(err);
    }

    return;

out_irq_notifier_err:
    vfio_ccw_unregister_irq_notifier(vcdev, VFIO_CCW_REQ_IRQ_INDEX);
    vfio_ccw_unregister_irq_notifier(vcdev, VFIO_CCW_CRW_IRQ_INDEX);
    vfio_ccw_unregister_irq_notifier(vcdev, VFIO_CCW_IO_IRQ_INDEX);
out_io_notifier_err:
    vfio_ccw_put_region(vcdev);
out_region_err:
    vfio_device_detach(vbasedev);
out_attach_dev_err:
    vfio_device_free_name(vbasedev);
out_unrealize:
    if (cdc->unrealize) {
        cdc->unrealize(cdev);
    }
}

static void vfio_ccw_unrealize(DeviceState *dev)
{
    S390CCWDevice *cdev = S390_CCW_DEVICE(dev);
    VFIOCCWDevice *vcdev = VFIO_CCW(cdev);
    S390CCWDeviceClass *cdc = S390_CCW_DEVICE_GET_CLASS(cdev);

    vfio_ccw_unregister_irq_notifier(vcdev, VFIO_CCW_REQ_IRQ_INDEX);
    vfio_ccw_unregister_irq_notifier(vcdev, VFIO_CCW_CRW_IRQ_INDEX);
    vfio_ccw_unregister_irq_notifier(vcdev, VFIO_CCW_IO_IRQ_INDEX);
    vfio_ccw_put_region(vcdev);
    vfio_device_detach(&vcdev->vdev);
    vfio_device_free_name(&vcdev->vdev);

    if (cdc->unrealize) {
        cdc->unrealize(cdev);
    }
}

static const Property vfio_ccw_properties[] = {
    DEFINE_PROP_STRING("sysfsdev", VFIOCCWDevice, vdev.sysfsdev),
    DEFINE_PROP_BOOL("force-orb-pfch", VFIOCCWDevice, force_orb_pfch, false),
#ifdef CONFIG_IOMMUFD
    DEFINE_PROP_LINK("iommufd", VFIOCCWDevice, vdev.iommufd,
                     TYPE_IOMMUFD_BACKEND, IOMMUFDBackend *),
#endif
    DEFINE_PROP_CCW_LOADPARM("loadparm", CcwDevice, loadparm),
};

static const VMStateDescription vfio_ccw_vmstate = {
    .name = "vfio-ccw",
    .unmigratable = 1,
};

static void vfio_ccw_instance_init(Object *obj)
{
    VFIOCCWDevice *vcdev = VFIO_CCW(obj);
    VFIODevice *vbasedev = &vcdev->vdev;

    /* CCW device is mdev type device */
    vbasedev->mdev = true;

    /*
     * All vfio-ccw devices are believed to operate in a way compatible with
     * discarding of memory in RAM blocks, ie. pages pinned in the host are
     * in the current working set of the guest driver and therefore never
     * overlap e.g., with pages available to the guest balloon driver.  This
     * needs to be set before vfio_get_device() for vfio common to handle
     * ram_block_discard_disable().
     */
    vfio_device_init(vbasedev, VFIO_DEVICE_TYPE_CCW, &vfio_ccw_ops,
                     DEVICE(vcdev), true);
}

#ifdef CONFIG_IOMMUFD
static void vfio_ccw_set_fd(Object *obj, const char *str, Error **errp)
{
    vfio_device_set_fd(&VFIO_CCW(obj)->vdev, str, errp);
}
#endif

static void vfio_ccw_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    S390CCWDeviceClass *cdc = S390_CCW_DEVICE_CLASS(klass);

    device_class_set_props(dc, vfio_ccw_properties);
#ifdef CONFIG_IOMMUFD
    object_class_property_add_str(klass, "fd", NULL, vfio_ccw_set_fd);
#endif
    dc->vmsd = &vfio_ccw_vmstate;
    dc->desc = "VFIO-based subchannel assignment";
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->realize = vfio_ccw_realize;
    dc->unrealize = vfio_ccw_unrealize;
    device_class_set_legacy_reset(dc, vfio_ccw_reset);

    cdc->handle_request = vfio_ccw_handle_request;
    cdc->handle_halt = vfio_ccw_handle_halt;
    cdc->handle_clear = vfio_ccw_handle_clear;
    cdc->handle_store = vfio_ccw_handle_store;

    object_class_property_set_description(klass, /* 2.10 */
                                          "sysfsdev",
                                          "Host sysfs path of assigned device");
    object_class_property_set_description(klass, /* 3.0 */
                                          "force-orb-pfch",
                                          "Force unlimited prefetch");
#ifdef CONFIG_IOMMUFD
    object_class_property_set_description(klass, /* 9.0 */
                                          "iommufd",
                                          "Set host IOMMUFD backend device");
#endif
    object_class_property_set_description(klass, /* 9.2 */
                                          "loadparm",
                                          "Define which devices that can be used for booting");
}

static const TypeInfo vfio_ccw_info = {
    .name = TYPE_VFIO_CCW,
    .parent = TYPE_S390_CCW,
    .instance_size = sizeof(VFIOCCWDevice),
    .instance_init = vfio_ccw_instance_init,
    .class_init = vfio_ccw_class_init,
};

static void register_vfio_ccw_type(void)
{
    type_register_static(&vfio_ccw_info);
}

type_init(register_vfio_ccw_type)
