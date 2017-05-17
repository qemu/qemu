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
#include <sys/ioctl.h>

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "hw/vfio/vfio.h"
#include "hw/vfio/vfio-common.h"
#include "hw/s390x/s390-ccw.h"
#include "hw/s390x/ccw-device.h"

#define TYPE_VFIO_CCW "vfio-ccw"
typedef struct VFIOCCWDevice {
    S390CCWDevice cdev;
    VFIODevice vdev;
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

static void vfio_ccw_reset(DeviceState *dev)
{
    CcwDevice *ccw_dev = DO_UPCAST(CcwDevice, parent_obj, dev);
    S390CCWDevice *cdev = DO_UPCAST(S390CCWDevice, parent_obj, ccw_dev);
    VFIOCCWDevice *vcdev = DO_UPCAST(VFIOCCWDevice, cdev, cdev);

    ioctl(vcdev->vdev.fd, VFIO_DEVICE_RESET);
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

    return;

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

    dc->props = vfio_ccw_properties;
    dc->vmsd = &vfio_ccw_vmstate;
    dc->desc = "VFIO-based subchannel assignment";
    dc->realize = vfio_ccw_realize;
    dc->unrealize = vfio_ccw_unrealize;
    dc->reset = vfio_ccw_reset;
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
