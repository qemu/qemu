/*
 * VFIO calxeda xgmac device
 *
 * Copyright Linaro Limited, 2014
 *
 * Authors:
 *  Eric Auger <eric.auger@linaro.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef HW_VFIO_VFIO_CALXEDA_XGMAC_H
#define HW_VFIO_VFIO_CALXEDA_XGMAC_H

#include "hw/vfio/vfio-platform.h"
#include "qom/object.h"

#define TYPE_VFIO_CALXEDA_XGMAC "vfio-calxeda-xgmac"

/**
 * This device exposes:
 * - a single MMIO region corresponding to its register space
 * - 3 IRQS (main and 2 power related IRQs)
 */
struct VFIOCalxedaXgmacDevice {
    VFIOPlatformDevice vdev;
};
typedef struct VFIOCalxedaXgmacDevice VFIOCalxedaXgmacDevice;

struct VFIOCalxedaXgmacDeviceClass {
    /*< private >*/
    VFIOPlatformDeviceClass parent_class;
    /*< public >*/
    DeviceRealize parent_realize;
};
typedef struct VFIOCalxedaXgmacDeviceClass VFIOCalxedaXgmacDeviceClass;

DECLARE_OBJ_CHECKERS(VFIOCalxedaXgmacDevice, VFIOCalxedaXgmacDeviceClass,
                     VFIO_CALXEDA_XGMAC_DEVICE, TYPE_VFIO_CALXEDA_XGMAC)

#endif
