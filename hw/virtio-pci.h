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
#include "virtio-blk.h"
#include "virtio-net.h"
#include "virtio-rng.h"
#include "virtio-serial.h"
#include "virtio-scsi.h"
#include "virtio-bus.h"

typedef struct VirtIOPCIProxy VirtIOPCIProxy;

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
    VirtIODevice *vdev;
    MemoryRegion bar;
    uint32_t flags;
    uint32_t class_code;
    uint32_t nvectors;
    VirtIOBlkConf blk;
    NICConf nic;
    uint32_t host_features;
#ifdef CONFIG_LINUX
    V9fsConf fsconf;
#endif
    virtio_serial_conf serial;
    virtio_net_conf net;
    VirtIOSCSIConf scsi;
    VirtIORNGConf rng;
    bool ioeventfd_disabled;
    bool ioeventfd_started;
    VirtIOIRQFD *vector_irqfd;
    int nvqs_with_notifiers;
    VirtioBusState bus;
};

void virtio_init_pci(VirtIOPCIProxy *proxy, VirtIODevice *vdev);
void virtio_pci_reset(DeviceState *d);
void virtio_pci_bus_new(VirtioBusState *bus, VirtIOPCIProxy *dev);

/* Virtio ABI version, if we increment this, we break the guest driver. */
#define VIRTIO_PCI_ABI_VERSION          0

#endif
