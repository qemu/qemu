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

#include "qom/object.h"
#include "hw/virtio/virtio-bus.h"
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

/*
 * modules can reference this symbol to avoid being loaded
 * into system emulators without ccw support
 */
extern bool have_virtio_ccw;

struct VirtIOCCWDeviceClass {
    CCWDeviceClass parent_class;
    void (*realize)(VirtioCcwDevice *dev, Error **errp);
    void (*unrealize)(VirtioCcwDevice *dev);
    ResettablePhases parent_phases;
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

VirtIODevice *virtio_ccw_get_vdev(SubchDev *sch);

#endif
