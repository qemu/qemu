/*
 * vfio based device assignment support - platform devices
 *
 * Copyright Linaro Limited, 2014
 *
 * Authors:
 *  Kim Phillips <kim.phillips@linaro.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Based on vfio based PCI device assignment support:
 *  Copyright Red Hat, Inc. 2012
 */

#ifndef HW_VFIO_VFIO_PLATFORM_H
#define HW_VFIO_VFIO_PLATFORM_H

#include "hw/sysbus.h"
#include "hw/vfio/vfio-common.h"

#define TYPE_VFIO_PLATFORM "vfio-platform"

typedef struct VFIOPlatformDevice {
    SysBusDevice sbdev;
    VFIODevice vbasedev; /* not a QOM object */
    VFIORegion **regions;
    char *compat; /* compatibility string */
} VFIOPlatformDevice;

typedef struct VFIOPlatformDeviceClass {
    /*< private >*/
    SysBusDeviceClass parent_class;
    /*< public >*/
} VFIOPlatformDeviceClass;

#define VFIO_PLATFORM_DEVICE(obj) \
     OBJECT_CHECK(VFIOPlatformDevice, (obj), TYPE_VFIO_PLATFORM)
#define VFIO_PLATFORM_DEVICE_CLASS(klass) \
     OBJECT_CLASS_CHECK(VFIOPlatformDeviceClass, (klass), TYPE_VFIO_PLATFORM)
#define VFIO_PLATFORM_DEVICE_GET_CLASS(obj) \
     OBJECT_GET_CLASS(VFIOPlatformDeviceClass, (obj), TYPE_VFIO_PLATFORM)

#endif /*HW_VFIO_VFIO_PLATFORM_H*/
