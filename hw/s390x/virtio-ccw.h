/*
 * virtio ccw target definitions
 *
 * Copyright 2012 IBM Corp.
 * Author(s): Cornelia Huck <cornelia.huck@de.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#ifndef HW_S390X_VIRTIO_CCW_H
#define HW_S390X_VIRTIO_CCW_H

#include <hw/virtio-blk.h>
#include <hw/virtio-net.h>
#include <hw/virtio-serial.h>
#include <hw/virtio-scsi.h>
#include <hw/virtio-bus.h>

#define VIRTUAL_CSSID 0xfe

#define VIRTIO_CCW_CU_TYPE 0x3832
#define VIRTIO_CCW_CHPID_TYPE 0x32

#define CCW_CMD_SET_VQ       0x13
#define CCW_CMD_VDEV_RESET   0x33
#define CCW_CMD_READ_FEAT    0x12
#define CCW_CMD_WRITE_FEAT   0x11
#define CCW_CMD_READ_CONF    0x22
#define CCW_CMD_WRITE_CONF   0x21
#define CCW_CMD_WRITE_STATUS 0x31
#define CCW_CMD_SET_IND      0x43
#define CCW_CMD_SET_CONF_IND 0x53
#define CCW_CMD_READ_VQ_CONF 0x32

#define TYPE_VIRTIO_CCW_DEVICE "virtio-ccw-device"
#define VIRTIO_CCW_DEVICE(obj) \
     OBJECT_CHECK(VirtioCcwDevice, (obj), TYPE_VIRTIO_CCW_DEVICE)
#define VIRTIO_CCW_DEVICE_CLASS(klass) \
     OBJECT_CLASS_CHECK(VirtIOCCWDeviceClass, (klass), TYPE_VIRTIO_CCW_DEVICE)
#define VIRTIO_CCW_DEVICE_GET_CLASS(obj) \
     OBJECT_GET_CLASS(VirtIOCCWDeviceClass, (obj), TYPE_VIRTIO_CCW_DEVICE)

typedef struct VirtioBusState VirtioCcwBusState;
typedef struct VirtioBusClass VirtioCcwBusClass;

#define TYPE_VIRTIO_CCW_BUS "virtio-ccw-bus"
#define VIRTIO_CCW_BUS(obj) \
     OBJECT_CHECK(VirtioCcwBus, (obj), TYPE_VIRTIO_CCW_BUS)
#define VIRTIO_CCW_BUS_GET_CLASS(obj) \
    OBJECT_CHECK(VirtioCcwBusState, (obj), TYPE_VIRTIO_CCW_BUS)
#define VIRTIO_CCW_BUS_CLASS(klass) \
    OBJECT_CLASS_CHECK(VirtioCcwBusClass, klass, TYPE_VIRTIO_CCW_BUS)

typedef struct VirtioCcwDevice VirtioCcwDevice;

void virtio_ccw_bus_new(VirtioBusState *bus, VirtioCcwDevice *dev);

typedef struct VirtIOCCWDeviceClass {
    DeviceClass parent_class;
    int (*init)(VirtioCcwDevice *dev);
    int (*exit)(VirtioCcwDevice *dev);
} VirtIOCCWDeviceClass;

/* Change here if we want to support more feature bits. */
#define VIRTIO_CCW_FEATURE_SIZE 1

struct VirtioCcwDevice {
    DeviceState parent_obj;
    SubchDev *sch;
    VirtIODevice *vdev;
    char *bus_id;
    VirtIOBlkConf blk;
    NICConf nic;
    uint32_t host_features[VIRTIO_CCW_FEATURE_SIZE];
    virtio_serial_conf serial;
    virtio_net_conf net;
    VirtIOSCSIConf scsi;
    VirtioBusState bus;
    /* Guest provided values: */
    hwaddr indicators;
    hwaddr indicators2;
};

/* virtual css bus type */
typedef struct VirtualCssBus {
    BusState parent_obj;
} VirtualCssBus;

#define TYPE_VIRTUAL_CSS_BUS "virtual-css-bus"
#define VIRTUAL_CSS_BUS(obj) \
     OBJECT_CHECK(VirtualCssBus, (obj), TYPE_VIRTUAL_CSS_BUS)

VirtualCssBus *virtual_css_bus_init(void);
void virtio_ccw_device_update_status(SubchDev *sch);
VirtIODevice *virtio_ccw_get_vdev(SubchDev *sch);
#endif
