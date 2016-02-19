/*
 * VFIO AMD XGBE device
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

#ifndef HW_VFIO_VFIO_AMD_XGBE_H
#define HW_VFIO_VFIO_AMD_XGBE_H

#include "hw/vfio/vfio-platform.h"

#define TYPE_VFIO_AMD_XGBE "vfio-amd-xgbe"

/**
 * This device exposes:
 * - 5 MMIO regions: MAC, PCS, SerDes Rx/Tx regs,
     SerDes Integration Registers 1/2 & 2/2
 * - 2 level sensitive IRQs and optional DMA channel IRQs
 */
struct VFIOAmdXgbeDevice {
    VFIOPlatformDevice vdev;
};

typedef struct VFIOAmdXgbeDevice VFIOAmdXgbeDevice;

struct VFIOAmdXgbeDeviceClass {
    /*< private >*/
    VFIOPlatformDeviceClass parent_class;
    /*< public >*/
    DeviceRealize parent_realize;
};

typedef struct VFIOAmdXgbeDeviceClass VFIOAmdXgbeDeviceClass;

#define VFIO_AMD_XGBE_DEVICE(obj) \
     OBJECT_CHECK(VFIOAmdXgbeDevice, (obj), TYPE_VFIO_AMD_XGBE)
#define VFIO_AMD_XGBE_DEVICE_CLASS(klass) \
     OBJECT_CLASS_CHECK(VFIOAmdXgbeDeviceClass, (klass), \
                        TYPE_VFIO_AMD_XGBE)
#define VFIO_AMD_XGBE_DEVICE_GET_CLASS(obj) \
     OBJECT_GET_CLASS(VFIOAmdXgbeDeviceClass, (obj), \
                      TYPE_VFIO_AMD_XGBE)

#endif
