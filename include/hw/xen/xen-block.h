/*
 * Copyright (c) 2018  Citrix Systems Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_XEN_BLOCK_H
#define HW_XEN_BLOCK_H

#include "hw/xen/xen-bus.h"
#include "hw/block/block.h"
#include "hw/block/dataplane/xen-block.h"
#include "sysemu/iothread.h"
#include "qom/object.h"

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
    BlockConf conf;
    unsigned int max_ring_page_order;
    IOThread *iothread;
} XenBlockProperties;

typedef struct XenBlockDrive {
    char *id;
    char *node_name;
} XenBlockDrive;

typedef struct XenBlockIOThread {
    char *id;
} XenBlockIOThread;

struct XenBlockDevice {
    XenDevice xendev;
    XenBlockProperties props;
    const char *device_type;
    unsigned int info;
    XenBlockDataPlane *dataplane;
    XenBlockDrive *drive;
    XenBlockIOThread *iothread;
};
typedef struct XenBlockDevice XenBlockDevice;

typedef void (*XenBlockDeviceRealize)(XenBlockDevice *blockdev, Error **errp);
typedef void (*XenBlockDeviceUnrealize)(XenBlockDevice *blockdev);

struct XenBlockDeviceClass {
    /*< private >*/
    XenDeviceClass parent_class;
    /*< public >*/
    XenBlockDeviceRealize realize;
    XenBlockDeviceUnrealize unrealize;
};
typedef struct XenBlockDeviceClass XenBlockDeviceClass;

#define TYPE_XEN_BLOCK_DEVICE  "xen-block"
DECLARE_OBJ_CHECKERS(XenBlockDevice, XenBlockDeviceClass,
                     XEN_BLOCK_DEVICE, TYPE_XEN_BLOCK_DEVICE)

struct XenDiskDevice {
    XenBlockDevice blockdev;
};
typedef struct XenDiskDevice XenDiskDevice;

#define TYPE_XEN_DISK_DEVICE  "xen-disk"
DECLARE_INSTANCE_CHECKER(XenDiskDevice, XEN_DISK_DEVICE,
                         TYPE_XEN_DISK_DEVICE)

struct XenCDRomDevice {
    XenBlockDevice blockdev;
};
typedef struct XenCDRomDevice XenCDRomDevice;

#define TYPE_XEN_CDROM_DEVICE  "xen-cdrom"
DECLARE_INSTANCE_CHECKER(XenCDRomDevice, XEN_CDROM_DEVICE,
                         TYPE_XEN_CDROM_DEVICE)

#endif /* HW_XEN_BLOCK_H */
