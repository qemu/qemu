/*
 * virtio ccw target definitions
 *
 * Copyright 2012,2015 IBM Corp.
 * Author(s): Cornelia Huck <cornelia.huck@de.ibm.com>
 *            Pierre Morel <pmorel@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#ifndef HW_S390X_VIRTIO_CCW_H
#define HW_S390X_VIRTIO_CCW_H

#include "hw/virtio/virtio-blk.h"
#include "hw/virtio/virtio-net.h"
#include "hw/virtio/virtio-serial.h"
#include "hw/virtio/virtio-scsi.h"
#include "qom/object.h"
#ifdef CONFIG_VHOST_SCSI
#include "hw/virtio/vhost-scsi.h"
#endif
#include "hw/virtio/virtio-balloon.h"
#include "hw/virtio/virtio-rng.h"
#include "hw/virtio/virtio-crypto.h"
#include "hw/virtio/virtio-bus.h"
#ifdef CONFIG_VHOST_VSOCK
#include "hw/virtio/vhost-vsock.h"
#endif /* CONFIG_VHOST_VSOCK */
#include "hw/virtio/virtio-gpu.h"
#include "hw/virtio/virtio-input.h"

#include "hw/s390x/s390_flic.h"
#include "hw/s390x/css.h"
#include "ccw-device.h"
#include "hw/s390x/css-bridge.h"

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
#define CCW_CMD_READ_STATUS  0x72
#define CCW_CMD_SET_IND_ADAPTER 0x73
#define CCW_CMD_SET_VIRTIO_REV 0x83

#define TYPE_VIRTIO_CCW_DEVICE "virtio-ccw-device"
OBJECT_DECLARE_TYPE(VirtioCcwDevice, VirtIOCCWDeviceClass, VIRTIO_CCW_DEVICE)

typedef struct VirtioBusState VirtioCcwBusState;
typedef struct VirtioBusClass VirtioCcwBusClass;

#define TYPE_VIRTIO_CCW_BUS "virtio-ccw-bus"
DECLARE_OBJ_CHECKERS(VirtioCcwBusState, VirtioCcwBusClass,
                     VIRTIO_CCW_BUS, TYPE_VIRTIO_CCW_BUS)


struct VirtIOCCWDeviceClass {
    CCWDeviceClass parent_class;
    void (*realize)(VirtioCcwDevice *dev, Error **errp);
    void (*unrealize)(VirtioCcwDevice *dev);
    void (*parent_reset)(DeviceState *dev);
};

/* Performance improves when virtqueue kick processing is decoupled from the
 * vcpu thread using ioeventfd for some devices. */
#define VIRTIO_CCW_FLAG_USE_IOEVENTFD_BIT 1
#define VIRTIO_CCW_FLAG_USE_IOEVENTFD   (1 << VIRTIO_CCW_FLAG_USE_IOEVENTFD_BIT)

struct VirtioCcwDevice {
    CcwDevice parent_obj;
    int revision;
    uint32_t max_rev;
    VirtioBusState bus;
    uint32_t flags;
    uint8_t thinint_isc;
    AdapterRoutes routes;
    /* Guest provided values: */
    IndAddr *indicators;
    IndAddr *indicators2;
    IndAddr *summary_indicator;
    uint64_t ind_bit;
    bool force_revision_1;
};

/* The maximum virtio revision we support. */
#define VIRTIO_CCW_MAX_REV 2
static inline int virtio_ccw_rev_max(VirtioCcwDevice *dev)
{
    return dev->max_rev;
}

/* virtio-scsi-ccw */

#define TYPE_VIRTIO_SCSI_CCW "virtio-scsi-ccw"
OBJECT_DECLARE_SIMPLE_TYPE(VirtIOSCSICcw, VIRTIO_SCSI_CCW)

struct VirtIOSCSICcw {
    VirtioCcwDevice parent_obj;
    VirtIOSCSI vdev;
};

#ifdef CONFIG_VHOST_SCSI
/* vhost-scsi-ccw */

#define TYPE_VHOST_SCSI_CCW "vhost-scsi-ccw"
OBJECT_DECLARE_SIMPLE_TYPE(VHostSCSICcw, VHOST_SCSI_CCW)

struct VHostSCSICcw {
    VirtioCcwDevice parent_obj;
    VHostSCSI vdev;
};
#endif

/* virtio-blk-ccw */

#define TYPE_VIRTIO_BLK_CCW "virtio-blk-ccw"
OBJECT_DECLARE_SIMPLE_TYPE(VirtIOBlkCcw, VIRTIO_BLK_CCW)

struct VirtIOBlkCcw {
    VirtioCcwDevice parent_obj;
    VirtIOBlock vdev;
};

/* virtio-balloon-ccw */

#define TYPE_VIRTIO_BALLOON_CCW "virtio-balloon-ccw"
OBJECT_DECLARE_SIMPLE_TYPE(VirtIOBalloonCcw, VIRTIO_BALLOON_CCW)

struct VirtIOBalloonCcw {
    VirtioCcwDevice parent_obj;
    VirtIOBalloon vdev;
};

/* virtio-serial-ccw */

#define TYPE_VIRTIO_SERIAL_CCW "virtio-serial-ccw"
OBJECT_DECLARE_SIMPLE_TYPE(VirtioSerialCcw, VIRTIO_SERIAL_CCW)

struct VirtioSerialCcw {
    VirtioCcwDevice parent_obj;
    VirtIOSerial vdev;
};

/* virtio-net-ccw */

#define TYPE_VIRTIO_NET_CCW "virtio-net-ccw"
OBJECT_DECLARE_SIMPLE_TYPE(VirtIONetCcw, VIRTIO_NET_CCW)

struct VirtIONetCcw {
    VirtioCcwDevice parent_obj;
    VirtIONet vdev;
};

/* virtio-rng-ccw */

#define TYPE_VIRTIO_RNG_CCW "virtio-rng-ccw"
OBJECT_DECLARE_SIMPLE_TYPE(VirtIORNGCcw, VIRTIO_RNG_CCW)

struct VirtIORNGCcw {
    VirtioCcwDevice parent_obj;
    VirtIORNG vdev;
};

/* virtio-crypto-ccw */

#define TYPE_VIRTIO_CRYPTO_CCW "virtio-crypto-ccw"
OBJECT_DECLARE_SIMPLE_TYPE(VirtIOCryptoCcw, VIRTIO_CRYPTO_CCW)

struct VirtIOCryptoCcw {
    VirtioCcwDevice parent_obj;
    VirtIOCrypto vdev;
};

VirtIODevice *virtio_ccw_get_vdev(SubchDev *sch);

#ifdef CONFIG_VIRTFS
#include "hw/9pfs/virtio-9p.h"

#define TYPE_VIRTIO_9P_CCW "virtio-9p-ccw"
OBJECT_DECLARE_SIMPLE_TYPE(V9fsCCWState, VIRTIO_9P_CCW)

struct V9fsCCWState {
    VirtioCcwDevice parent_obj;
    V9fsVirtioState vdev;
};

#endif /* CONFIG_VIRTFS */

#ifdef CONFIG_VHOST_VSOCK
#define TYPE_VHOST_VSOCK_CCW "vhost-vsock-ccw"
OBJECT_DECLARE_SIMPLE_TYPE(VHostVSockCCWState, VHOST_VSOCK_CCW)

struct VHostVSockCCWState {
    VirtioCcwDevice parent_obj;
    VHostVSock vdev;
};

#endif /* CONFIG_VHOST_VSOCK */

#define TYPE_VIRTIO_GPU_CCW "virtio-gpu-ccw"
OBJECT_DECLARE_SIMPLE_TYPE(VirtIOGPUCcw, VIRTIO_GPU_CCW)

struct VirtIOGPUCcw {
    VirtioCcwDevice parent_obj;
    VirtIOGPU vdev;
};

#define TYPE_VIRTIO_INPUT_CCW "virtio-input-ccw"
OBJECT_DECLARE_SIMPLE_TYPE(VirtIOInputCcw, VIRTIO_INPUT_CCW)

struct VirtIOInputCcw {
    VirtioCcwDevice parent_obj;
    VirtIOInput vdev;
};

#define TYPE_VIRTIO_INPUT_HID_CCW "virtio-input-hid-ccw"
#define TYPE_VIRTIO_KEYBOARD_CCW "virtio-keyboard-ccw"
#define TYPE_VIRTIO_MOUSE_CCW "virtio-mouse-ccw"
#define TYPE_VIRTIO_TABLET_CCW "virtio-tablet-ccw"
OBJECT_DECLARE_SIMPLE_TYPE(VirtIOInputHIDCcw, VIRTIO_INPUT_HID_CCW)

struct VirtIOInputHIDCcw {
    VirtioCcwDevice parent_obj;
    VirtIOInputHID vdev;
};

#endif
