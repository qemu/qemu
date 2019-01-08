/*
 * Copyright (c) 2018  Citrix Systems Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_XEN_BLOCK_H
#define HW_XEN_BLOCK_H

#include "hw/xen/xen-bus.h"

typedef enum XenBlockVdevType {
    XEN_BLOCK_VDEV_TYPE_INVALID,
    XEN_BLOCK_VDEV_TYPE_DP,
    XEN_BLOCK_VDEV_TYPE_XVD,
    XEN_BLOCK_VDEV_TYPE_HD,
    XEN_BLOCK_VDEV_TYPE_SD,
    XEN_BLOCK_VDEV_TYPE__MAX
} XenBlockVdevType;

typedef struct XenBlockVdev {
    XenBlockVdevType type;
    unsigned long disk;
    unsigned long partition;
    unsigned long number;
} XenBlockVdev;

typedef struct XenBlockProperties {
    XenBlockVdev vdev;
} XenBlockProperties;

typedef struct XenBlockDevice {
    XenDevice xendev;
    XenBlockProperties props;
} XenBlockDevice;

typedef void (*XenBlockDeviceRealize)(XenBlockDevice *blockdev, Error **errp);
typedef void (*XenBlockDeviceUnrealize)(XenBlockDevice *blockdev, Error **errp);

typedef struct XenBlockDeviceClass {
    /*< private >*/
    XenDeviceClass parent_class;
    /*< public >*/
    XenBlockDeviceRealize realize;
    XenBlockDeviceUnrealize unrealize;
} XenBlockDeviceClass;

#define TYPE_XEN_BLOCK_DEVICE  "xen-block"
#define XEN_BLOCK_DEVICE(obj) \
     OBJECT_CHECK(XenBlockDevice, (obj), TYPE_XEN_BLOCK_DEVICE)
#define XEN_BLOCK_DEVICE_CLASS(class) \
     OBJECT_CLASS_CHECK(XenBlockDeviceClass, (class), TYPE_XEN_BLOCK_DEVICE)
#define XEN_BLOCK_DEVICE_GET_CLASS(obj) \
     OBJECT_GET_CLASS(XenBlockDeviceClass, (obj), TYPE_XEN_BLOCK_DEVICE)

typedef struct XenDiskDevice {
    XenBlockDevice blockdev;
} XenDiskDevice;

#define TYPE_XEN_DISK_DEVICE  "xen-disk"
#define XEN_DISK_DEVICE(obj) \
     OBJECT_CHECK(XenDiskDevice, (obj), TYPE_XEN_DISK_DEVICE)

typedef struct XenCDRomDevice {
    XenBlockDevice blockdev;
} XenCDRomDevice;

#define TYPE_XEN_CDROM_DEVICE  "xen-cdrom"
#define XEN_CDROM_DEVICE(obj) \
     OBJECT_CHECK(XenCDRomDevice, (obj), TYPE_XEN_CDROM_DEVICE)

#endif /* HW_XEN_BLOCK_H */
