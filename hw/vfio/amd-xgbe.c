/*
 * AMD XGBE VFIO device
 *
 * Copyright Linaro Limited, 2015
 *
 * Authors:
 *  Eric Auger <eric.auger@linaro.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "hw/vfio/vfio-amd-xgbe.h"
#include "migration/vmstate.h"
#include "qemu/module.h"

static void amd_xgbe_realize(DeviceState *dev, Error **errp)
{
    VFIOPlatformDevice *vdev = VFIO_PLATFORM_DEVICE(dev);
    VFIOAmdXgbeDeviceClass *k = VFIO_AMD_XGBE_DEVICE_GET_CLASS(dev);

    vdev->compat = g_strdup("amd,xgbe-seattle-v1a");
    vdev->num_compat = 1;

    k->parent_realize(dev, errp);
}

static const VMStateDescription vfio_platform_amd_xgbe_vmstate = {
    .name = "vfio-amd-xgbe",
    .unmigratable = 1,
};

static void vfio_amd_xgbe_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VFIOAmdXgbeDeviceClass *vcxc =
        VFIO_AMD_XGBE_DEVICE_CLASS(klass);
    device_class_set_parent_realize(dc, amd_xgbe_realize,
                                    &vcxc->parent_realize);
    dc->desc = "VFIO AMD XGBE";
    dc->vmsd = &vfio_platform_amd_xgbe_vmstate;
    /* Supported by TYPE_VIRT_MACHINE */
    dc->user_creatable = true;
}

static const TypeInfo vfio_amd_xgbe_dev_info = {
    .name = TYPE_VFIO_AMD_XGBE,
    .parent = TYPE_VFIO_PLATFORM,
    .instance_size = sizeof(VFIOAmdXgbeDevice),
    .class_init = vfio_amd_xgbe_class_init,
    .class_size = sizeof(VFIOAmdXgbeDeviceClass),
};

static void register_amd_xgbe_dev_type(void)
{
    type_register_static(&vfio_amd_xgbe_dev_info);
}

type_init(register_amd_xgbe_dev_type)
