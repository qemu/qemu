/*
 * Virtio PCI Bindings
 *
 * Copyright IBM, Corp. 2007
 * Copyright (c) 2009 CodeSourcery
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *  Paul Brook        <paul@codesourcery.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#ifndef QEMU_VIRTIO_PCI_H
#define QEMU_VIRTIO_PCI_H

#include "hw/pci/msi.h"
#include "hw/virtio/virtio-blk.h"
#include "hw/virtio/virtio-net.h"
#include "hw/virtio/virtio-rng.h"
#include "hw/virtio/virtio-serial.h"
#include "hw/virtio/virtio-scsi.h"
#include "hw/virtio/virtio-balloon.h"
#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/virtio-9p.h"
#ifdef CONFIG_VIRTFS
#include "hw/9pfs/virtio-9p.h"
#endif
#ifdef CONFIG_VHOST_SCSI
#include "hw/virtio/vhost-scsi.h"
#endif

typedef struct VirtIOPCIProxy VirtIOPCIProxy;
typedef struct VirtIOBlkPCI VirtIOBlkPCI;
typedef struct VirtIOSCSIPCI VirtIOSCSIPCI;
typedef struct VirtIOBalloonPCI VirtIOBalloonPCI;
typedef struct VirtIOSerialPCI VirtIOSerialPCI;
typedef struct VirtIONetPCI VirtIONetPCI;
typedef struct VHostSCSIPCI VHostSCSIPCI;
typedef struct VirtIORngPCI VirtIORngPCI;

/* virtio-pci-bus */

typedef struct VirtioBusState VirtioPCIBusState;
typedef struct VirtioBusClass VirtioPCIBusClass;

#define TYPE_VIRTIO_PCI_BUS "virtio-pci-bus"
#define VIRTIO_PCI_BUS(obj) \
        OBJECT_CHECK(VirtioPCIBusState, (obj), TYPE_VIRTIO_PCI_BUS)
#define VIRTIO_PCI_BUS_GET_CLASS(obj) \
        OBJECT_GET_CLASS(VirtioPCIBusClass, obj, TYPE_VIRTIO_PCI_BUS)
#define VIRTIO_PCI_BUS_CLASS(klass) \
        OBJECT_CLASS_CHECK(VirtioPCIBusClass, klass, TYPE_VIRTIO_PCI_BUS)

/* Performance improves when virtqueue kick processing is decoupled from the
 * vcpu thread using ioeventfd for some devices. */
#define VIRTIO_PCI_FLAG_USE_IOEVENTFD_BIT 1
#define VIRTIO_PCI_FLAG_USE_IOEVENTFD   (1 << VIRTIO_PCI_FLAG_USE_IOEVENTFD_BIT)

typedef struct {
    MSIMessage msg;
    int virq;
    unsigned int users;
} VirtIOIRQFD;

/*
 * virtio-pci: This is the PCIDevice which has a virtio-pci-bus.
 */
#define TYPE_VIRTIO_PCI "virtio-pci"
#define VIRTIO_PCI_GET_CLASS(obj) \
        OBJECT_GET_CLASS(VirtioPCIClass, obj, TYPE_VIRTIO_PCI)
#define VIRTIO_PCI_CLASS(klass) \
        OBJECT_CLASS_CHECK(VirtioPCIClass, klass, TYPE_VIRTIO_PCI)
#define VIRTIO_PCI(obj) \
        OBJECT_CHECK(VirtIOPCIProxy, (obj), TYPE_VIRTIO_PCI)

typedef struct VirtioPCIClass {
    PCIDeviceClass parent_class;
    int (*init)(VirtIOPCIProxy *vpci_dev);
} VirtioPCIClass;

struct VirtIOPCIProxy {
    PCIDevice pci_dev;
    MemoryRegion bar;
    uint32_t flags;
    uint32_t class_code;
    uint32_t nvectors;
    uint32_t host_features;
    bool ioeventfd_disabled;
    bool ioeventfd_started;
    VirtIOIRQFD *vector_irqfd;
    int nvqs_with_notifiers;
    VirtioBusState bus;
};


/*
 * virtio-scsi-pci: This extends VirtioPCIProxy.
 */
#define TYPE_VIRTIO_SCSI_PCI "virtio-scsi-pci"
#define VIRTIO_SCSI_PCI(obj) \
        OBJECT_CHECK(VirtIOSCSIPCI, (obj), TYPE_VIRTIO_SCSI_PCI)

struct VirtIOSCSIPCI {
    VirtIOPCIProxy parent_obj;
    VirtIOSCSI vdev;
};

#ifdef CONFIG_VHOST_SCSI
/*
 * vhost-scsi-pci: This extends VirtioPCIProxy.
 */
#define TYPE_VHOST_SCSI_PCI "vhost-scsi-pci"
#define VHOST_SCSI_PCI(obj) \
        OBJECT_CHECK(VHostSCSIPCI, (obj), TYPE_VHOST_SCSI_PCI)

struct VHostSCSIPCI {
    VirtIOPCIProxy parent_obj;
    VHostSCSI vdev;
};
#endif

/*
 * virtio-blk-pci: This extends VirtioPCIProxy.
 */
#define TYPE_VIRTIO_BLK_PCI "virtio-blk-pci"
#define VIRTIO_BLK_PCI(obj) \
        OBJECT_CHECK(VirtIOBlkPCI, (obj), TYPE_VIRTIO_BLK_PCI)

struct VirtIOBlkPCI {
    VirtIOPCIProxy parent_obj;
    VirtIOBlock vdev;
    VirtIOBlkConf blk;
};

/*
 * virtio-balloon-pci: This extends VirtioPCIProxy.
 */
#define TYPE_VIRTIO_BALLOON_PCI "virtio-balloon-pci"
#define VIRTIO_BALLOON_PCI(obj) \
        OBJECT_CHECK(VirtIOBalloonPCI, (obj), TYPE_VIRTIO_BALLOON_PCI)

struct VirtIOBalloonPCI {
    VirtIOPCIProxy parent_obj;
    VirtIOBalloon vdev;
};

/*
 * virtio-serial-pci: This extends VirtioPCIProxy.
 */
#define TYPE_VIRTIO_SERIAL_PCI "virtio-serial-pci"
#define VIRTIO_SERIAL_PCI(obj) \
        OBJECT_CHECK(VirtIOSerialPCI, (obj), TYPE_VIRTIO_SERIAL_PCI)

struct VirtIOSerialPCI {
    VirtIOPCIProxy parent_obj;
    VirtIOSerial vdev;
};

/*
 * virtio-net-pci: This extends VirtioPCIProxy.
 */
#define TYPE_VIRTIO_NET_PCI "virtio-net-pci"
#define VIRTIO_NET_PCI(obj) \
        OBJECT_CHECK(VirtIONetPCI, (obj), TYPE_VIRTIO_NET_PCI)

struct VirtIONetPCI {
    VirtIOPCIProxy parent_obj;
    VirtIONet vdev;
};

/*
 * virtio-9p-pci: This extends VirtioPCIProxy.
 */

#ifdef CONFIG_VIRTFS

#define TYPE_VIRTIO_9P_PCI "virtio-9p-pci"
#define VIRTIO_9P_PCI(obj) \
        OBJECT_CHECK(V9fsPCIState, (obj), TYPE_VIRTIO_9P_PCI)

typedef struct V9fsPCIState {
    VirtIOPCIProxy parent_obj;
    V9fsState vdev;
} V9fsPCIState;

#endif

/*
 * virtio-rng-pci: This extends VirtioPCIProxy.
 */
#define TYPE_VIRTIO_RNG_PCI "virtio-rng-pci"
#define VIRTIO_RNG_PCI(obj) \
        OBJECT_CHECK(VirtIORngPCI, (obj), TYPE_VIRTIO_RNG_PCI)

struct VirtIORngPCI {
    VirtIOPCIProxy parent_obj;
    VirtIORNG vdev;
};

/* Virtio ABI version, if we increment this, we break the guest driver. */
#define VIRTIO_PCI_ABI_VERSION          0

#endif
