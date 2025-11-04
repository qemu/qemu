/*
 * VFIO based AP matrix device assignment
 *
 * Copyright 2018 IBM Corp.
 * Author(s): Tony Krowiak <akrowiak@linux.ibm.com>
 *            Halil Pasic <pasic@linux.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include "qemu/osdep.h"
#include CONFIG_DEVICES /* CONFIG_IOMMUFD */
#include <linux/vfio.h>
#include <sys/ioctl.h>
#include "qapi/error.h"
#include "hw/vfio/vfio-device.h"
#include "system/iommufd.h"
#include "hw/s390x/ap-device.h"
#include "hw/s390x/css.h"
#include "qemu/error-report.h"
#include "qemu/event_notifier.h"
#include "qemu/lockable.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "qemu/config-file.h"
#include "kvm/kvm_s390x.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "hw/s390x/ap-bridge.h"
#include "system/address-spaces.h"
#include "qom/object.h"

#define TYPE_VFIO_AP_DEVICE      "vfio-ap"

struct VFIOAPDevice {
    APDevice apdev;
    VFIODevice vdev;
    EventNotifier req_notifier;
    EventNotifier cfg_notifier;
};

typedef struct APConfigChgEvent {
    QTAILQ_ENTRY(APConfigChgEvent) next;
} APConfigChgEvent;

static QTAILQ_HEAD(, APConfigChgEvent) cfg_chg_events =
    QTAILQ_HEAD_INITIALIZER(cfg_chg_events);

static QemuMutex cfg_chg_events_lock;

static void __attribute__((constructor)) vfio_ap_global_init(void)
{
    qemu_mutex_init(&cfg_chg_events_lock);
}

OBJECT_DECLARE_SIMPLE_TYPE(VFIOAPDevice, VFIO_AP_DEVICE)

static void vfio_ap_compute_needs_reset(VFIODevice *vdev)
{
    vdev->needs_reset = false;
}

/*
 * We don't need vfio_hot_reset_multi and vfio_eoi operations for
 * vfio-ap device now.
 */
struct VFIODeviceOps vfio_ap_ops = {
    .vfio_compute_needs_reset = vfio_ap_compute_needs_reset,
};

static void vfio_ap_req_notifier_handler(void *opaque)
{
    VFIOAPDevice *vapdev = opaque;
    Error *err = NULL;

    if (!event_notifier_test_and_clear(&vapdev->req_notifier)) {
        return;
    }

    qdev_unplug(DEVICE(vapdev), &err);

    if (err) {
        warn_reportf_err(err, VFIO_MSG_PREFIX, vapdev->vdev.name);
    }
}

static void vfio_ap_cfg_chg_notifier_handler(void *opaque)
{
    APConfigChgEvent *cfg_chg_event;
    VFIOAPDevice *vapdev = opaque;

    if (!event_notifier_test_and_clear(&vapdev->cfg_notifier)) {
        return;
    }

    cfg_chg_event = g_new0(APConfigChgEvent, 1);

    WITH_QEMU_LOCK_GUARD(&cfg_chg_events_lock) {
        QTAILQ_INSERT_TAIL(&cfg_chg_events, cfg_chg_event, next);
    }

    css_generate_css_crws(0);

}

int ap_chsc_sei_nt0_get_event(void *res)
{
    ChscSeiNt0Res *nt0_res  = (ChscSeiNt0Res *)res;
    APConfigChgEvent *cfg_chg_event;

    WITH_QEMU_LOCK_GUARD(&cfg_chg_events_lock) {
        if (QTAILQ_EMPTY(&cfg_chg_events)) {
            return EVENT_INFORMATION_NOT_STORED;
        }

        cfg_chg_event = QTAILQ_FIRST(&cfg_chg_events);
        QTAILQ_REMOVE(&cfg_chg_events, cfg_chg_event, next);
    }

    memset(nt0_res, 0, sizeof(*nt0_res));
    g_free(cfg_chg_event);
    nt0_res->flags |= PENDING_EVENT_INFO_BITMASK;
    nt0_res->length = sizeof(ChscSeiNt0Res);
    nt0_res->code = NT0_RES_RESPONSE_CODE;
    nt0_res->nt = NT0_RES_NT_DEFAULT;
    nt0_res->rs = NT0_RES_RS_AP_CHANGE;
    nt0_res->cc = NT0_RES_CC_AP_CHANGE;

    return EVENT_INFORMATION_STORED;
}

bool ap_chsc_sei_nt0_have_event(void)
{
    QEMU_LOCK_GUARD(&cfg_chg_events_lock);
    return !QTAILQ_EMPTY(&cfg_chg_events);
}

static bool vfio_ap_register_irq_notifier(VFIOAPDevice *vapdev,
                                          unsigned int irq, Error **errp)
{
    int fd;
    int ret;
    IOHandler *fd_read;
    EventNotifier *notifier;
    struct vfio_irq_info irq_info;
    VFIODevice *vdev = &vapdev->vdev;

    switch (irq) {
    case VFIO_AP_REQ_IRQ_INDEX:
        notifier = &vapdev->req_notifier;
        fd_read = vfio_ap_req_notifier_handler;
        break;
    case VFIO_AP_CFG_CHG_IRQ_INDEX:
        notifier = &vapdev->cfg_notifier;
        fd_read = vfio_ap_cfg_chg_notifier_handler;
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
    qemu_set_fd_handler(fd, fd_read, NULL, vapdev);

    if (!vfio_device_irq_set_signaling(vdev, irq, 0, VFIO_IRQ_SET_ACTION_TRIGGER, fd,
                                       errp)) {
        qemu_set_fd_handler(fd, NULL, NULL, vapdev);
        event_notifier_cleanup(notifier);
    }

    return true;
}

static void vfio_ap_unregister_irq_notifier(VFIOAPDevice *vapdev,
                                            unsigned int irq)
{
    Error *err = NULL;
    EventNotifier *notifier;

    switch (irq) {
    case VFIO_AP_REQ_IRQ_INDEX:
        notifier = &vapdev->req_notifier;
        break;
    case VFIO_AP_CFG_CHG_IRQ_INDEX:
        notifier = &vapdev->cfg_notifier;
        break;
    default:
        error_report("vfio: Unsupported device irq(%d)", irq);
        return;
    }

    if (!vfio_device_irq_set_signaling(&vapdev->vdev, irq, 0,
                                       VFIO_IRQ_SET_ACTION_TRIGGER, -1, &err)) {
        warn_reportf_err(err, VFIO_MSG_PREFIX, vapdev->vdev.name);
    }

    qemu_set_fd_handler(event_notifier_get_fd(notifier),
                        NULL, NULL, vapdev);
    event_notifier_cleanup(notifier);
}

static void vfio_ap_realize(DeviceState *dev, Error **errp)
{
    ERRP_GUARD();
    Error *err = NULL;
    VFIOAPDevice *vapdev = VFIO_AP_DEVICE(dev);
    VFIODevice *vbasedev = &vapdev->vdev;

    if (!vfio_device_get_name(vbasedev, errp)) {
        return;
    }

    if (!vfio_device_attach(vbasedev->name, vbasedev,
                            &address_space_memory, errp)) {
        goto error;
    }

    if (!vfio_ap_register_irq_notifier(vapdev, VFIO_AP_REQ_IRQ_INDEX, &err)) {
        /*
         * Report this error, but do not make it a failing condition.
         * Lack of this IRQ in the host does not prevent normal operation.
         */
        warn_report_err(err);
    }

    if (!vfio_ap_register_irq_notifier(vapdev, VFIO_AP_CFG_CHG_IRQ_INDEX, &err))
    {
        /*
         * Report this error, but do not make it a failing condition.
         * Lack of this IRQ in the host does not prevent normal operation.
         */
        warn_report_err(err);
    }

    return;

error:
    error_prepend(errp, VFIO_MSG_PREFIX, vbasedev->name);
    vfio_device_free_name(vbasedev);
}

static void vfio_ap_unrealize(DeviceState *dev)
{
    VFIOAPDevice *vapdev = VFIO_AP_DEVICE(dev);

    vfio_ap_unregister_irq_notifier(vapdev, VFIO_AP_REQ_IRQ_INDEX);
    vfio_ap_unregister_irq_notifier(vapdev, VFIO_AP_CFG_CHG_IRQ_INDEX);
    vfio_device_detach(&vapdev->vdev);
    vfio_device_free_name(&vapdev->vdev);
}

static const Property vfio_ap_properties[] = {
    DEFINE_PROP_STRING("sysfsdev", VFIOAPDevice, vdev.sysfsdev),
#ifdef CONFIG_IOMMUFD
    DEFINE_PROP_LINK("iommufd", VFIOAPDevice, vdev.iommufd,
                     TYPE_IOMMUFD_BACKEND, IOMMUFDBackend *),
#endif
};

static void vfio_ap_reset(DeviceState *dev)
{
    int ret;
    VFIOAPDevice *vapdev = VFIO_AP_DEVICE(dev);

    ret = ioctl(vapdev->vdev.fd, VFIO_DEVICE_RESET);
    if (ret) {
        error_report("%s: failed to reset %s device: %s", __func__,
                     vapdev->vdev.name, strerror(errno));
    }
}

static const VMStateDescription vfio_ap_vmstate = {
    .name = "vfio-ap",
    .unmigratable = 1,
};

static void vfio_ap_instance_init(Object *obj)
{
    VFIOAPDevice *vapdev = VFIO_AP_DEVICE(obj);
    VFIODevice *vbasedev = &vapdev->vdev;

    /*
     * vfio-ap devices operate in a way compatible with discarding of
     * memory in RAM blocks, as no pages are pinned in the host.
     * This needs to be set before vfio_get_device() for vfio common to
     * handle ram_block_discard_disable().
     */
    vfio_device_init(vbasedev, VFIO_DEVICE_TYPE_AP, &vfio_ap_ops,
                     DEVICE(vapdev), true);

    /* AP device is mdev type device */
    vbasedev->mdev = true;
}

#ifdef CONFIG_IOMMUFD
static void vfio_ap_set_fd(Object *obj, const char *str, Error **errp)
{
    vfio_device_set_fd(&VFIO_AP_DEVICE(obj)->vdev, str, errp);
}
#endif

static void vfio_ap_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, vfio_ap_properties);
#ifdef CONFIG_IOMMUFD
    object_class_property_add_str(klass, "fd", NULL, vfio_ap_set_fd);
#endif
    dc->vmsd = &vfio_ap_vmstate;
    dc->desc = "VFIO-based AP device assignment";
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->realize = vfio_ap_realize;
    dc->unrealize = vfio_ap_unrealize;
    dc->hotpluggable = true;
    device_class_set_legacy_reset(dc, vfio_ap_reset);
    dc->bus_type = TYPE_AP_BUS;

    object_class_property_set_description(klass, /* 3.1 */
                                          "sysfsdev",
                                          "Host sysfs path of assigned device");
#ifdef CONFIG_IOMMUFD
    object_class_property_set_description(klass, /* 9.0 */
                                          "iommufd",
                                          "Set host IOMMUFD backend device");
#endif
}

static const TypeInfo vfio_ap_info = {
    .name = TYPE_VFIO_AP_DEVICE,
    .parent = TYPE_AP_DEVICE,
    .instance_size = sizeof(VFIOAPDevice),
    .instance_init = vfio_ap_instance_init,
    .class_init = vfio_ap_class_init,
};

static void vfio_ap_type_init(void)
{
    type_register_static(&vfio_ap_info);
}

type_init(vfio_ap_type_init)
