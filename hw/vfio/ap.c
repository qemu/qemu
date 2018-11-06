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

#include <linux/vfio.h>
#include <sys/ioctl.h>
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "hw/vfio/vfio.h"
#include "hw/vfio/vfio-common.h"
#include "hw/s390x/ap-device.h"
#include "qemu/error-report.h"
#include "qemu/queue.h"
#include "qemu/option.h"
#include "qemu/config-file.h"
#include "cpu.h"
#include "kvm_s390x.h"
#include "sysemu/sysemu.h"
#include "hw/s390x/ap-bridge.h"
#include "exec/address-spaces.h"

#define VFIO_AP_DEVICE_TYPE      "vfio-ap"

typedef struct VFIOAPDevice {
    APDevice apdev;
    VFIODevice vdev;
} VFIOAPDevice;

#define VFIO_AP_DEVICE(obj) \
        OBJECT_CHECK(VFIOAPDevice, (obj), VFIO_AP_DEVICE_TYPE)

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

static void vfio_ap_put_device(VFIOAPDevice *vapdev)
{
    g_free(vapdev->vdev.name);
    vfio_put_base_device(&vapdev->vdev);
}

static VFIOGroup *vfio_ap_get_group(VFIOAPDevice *vapdev, Error **errp)
{
    GError *gerror = NULL;
    char *symlink, *group_path;
    int groupid;

    symlink = g_strdup_printf("%s/iommu_group", vapdev->vdev.sysfsdev);
    group_path = g_file_read_link(symlink, &gerror);
    g_free(symlink);

    if (!group_path) {
        error_setg(errp, "%s: no iommu_group found for %s: %s",
                   VFIO_AP_DEVICE_TYPE, vapdev->vdev.sysfsdev, gerror->message);
        return NULL;
    }

    if (sscanf(basename(group_path), "%d", &groupid) != 1) {
        error_setg(errp, "vfio: failed to read %s", group_path);
        g_free(group_path);
        return NULL;
    }

    g_free(group_path);

    return vfio_get_group(groupid, &address_space_memory, errp);
}

static void vfio_ap_realize(DeviceState *dev, Error **errp)
{
    int ret;
    char *mdevid;
    Error *local_err = NULL;
    VFIOGroup *vfio_group;
    APDevice *apdev = AP_DEVICE(dev);
    VFIOAPDevice *vapdev = VFIO_AP_DEVICE(apdev);

    vfio_group = vfio_ap_get_group(vapdev, &local_err);
    if (!vfio_group) {
        goto out_err;
    }

    vapdev->vdev.ops = &vfio_ap_ops;
    vapdev->vdev.type = VFIO_DEVICE_TYPE_AP;
    mdevid = basename(vapdev->vdev.sysfsdev);
    vapdev->vdev.name = g_strdup_printf("%s", mdevid);
    vapdev->vdev.dev = dev;

    ret = vfio_get_device(vfio_group, mdevid, &vapdev->vdev, &local_err);
    if (ret) {
        goto out_get_dev_err;
    }

    return;

out_get_dev_err:
    vfio_ap_put_device(vapdev);
    vfio_put_group(vfio_group);
out_err:
    error_propagate(errp, local_err);
}

static void vfio_ap_unrealize(DeviceState *dev, Error **errp)
{
    APDevice *apdev = AP_DEVICE(dev);
    VFIOAPDevice *vapdev = VFIO_AP_DEVICE(apdev);
    VFIOGroup *group = vapdev->vdev.group;

    vfio_ap_put_device(vapdev);
    vfio_put_group(group);
}

static Property vfio_ap_properties[] = {
    DEFINE_PROP_STRING("sysfsdev", VFIOAPDevice, vdev.sysfsdev),
    DEFINE_PROP_END_OF_LIST(),
};

static void vfio_ap_reset(DeviceState *dev)
{
    int ret;
    APDevice *apdev = AP_DEVICE(dev);
    VFIOAPDevice *vapdev = VFIO_AP_DEVICE(apdev);

    ret = ioctl(vapdev->vdev.fd, VFIO_DEVICE_RESET);
    if (ret) {
        error_report("%s: failed to reset %s device: %s", __func__,
                     vapdev->vdev.name, strerror(errno));
    }
}

static const VMStateDescription vfio_ap_vmstate = {
    .name = VFIO_AP_DEVICE_TYPE,
    .unmigratable = 1,
};

static void vfio_ap_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->props = vfio_ap_properties;
    dc->vmsd = &vfio_ap_vmstate;
    dc->desc = "VFIO-based AP device assignment";
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->realize = vfio_ap_realize;
    dc->unrealize = vfio_ap_unrealize;
    dc->hotpluggable = false;
    dc->reset = vfio_ap_reset;
    dc->bus_type = TYPE_AP_BUS;
}

static const TypeInfo vfio_ap_info = {
    .name = VFIO_AP_DEVICE_TYPE,
    .parent = AP_DEVICE_TYPE,
    .instance_size = sizeof(VFIOAPDevice),
    .class_init = vfio_ap_class_init,
};

static void vfio_ap_type_init(void)
{
    type_register_static(&vfio_ap_info);
}

type_init(vfio_ap_type_init)
