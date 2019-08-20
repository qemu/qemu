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
#include "hw/virtio/virtio-bus.h"

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

enum {
    VIRTIO_PCI_FLAG_BUS_MASTER_BUG_MIGRATION_BIT,
    VIRTIO_PCI_FLAG_USE_IOEVENTFD_BIT,
    VIRTIO_PCI_FLAG_MIGRATE_EXTRA_BIT,
    VIRTIO_PCI_FLAG_MODERN_PIO_NOTIFY_BIT,
    VIRTIO_PCI_FLAG_DISABLE_PCIE_BIT,
    VIRTIO_PCI_FLAG_PAGE_PER_VQ_BIT,
    VIRTIO_PCI_FLAG_ATS_BIT,
    VIRTIO_PCI_FLAG_INIT_DEVERR_BIT,
    VIRTIO_PCI_FLAG_INIT_LNKCTL_BIT,
    VIRTIO_PCI_FLAG_INIT_PM_BIT,
    VIRTIO_PCI_FLAG_INIT_FLR_BIT,
};

/* Need to activate work-arounds for buggy guests at vmstate load. */
#define VIRTIO_PCI_FLAG_BUS_MASTER_BUG_MIGRATION \
    (1 << VIRTIO_PCI_FLAG_BUS_MASTER_BUG_MIGRATION_BIT)

/* Performance improves when virtqueue kick processing is decoupled from the
 * vcpu thread using ioeventfd for some devices. */
#define VIRTIO_PCI_FLAG_USE_IOEVENTFD   (1 << VIRTIO_PCI_FLAG_USE_IOEVENTFD_BIT)

/* virtio version flags */
#define VIRTIO_PCI_FLAG_DISABLE_PCIE (1 << VIRTIO_PCI_FLAG_DISABLE_PCIE_BIT)

/* migrate extra state */
#define VIRTIO_PCI_FLAG_MIGRATE_EXTRA (1 << VIRTIO_PCI_FLAG_MIGRATE_EXTRA_BIT)

/* have pio notification for modern device ? */
#define VIRTIO_PCI_FLAG_MODERN_PIO_NOTIFY \
    (1 << VIRTIO_PCI_FLAG_MODERN_PIO_NOTIFY_BIT)

/* page per vq flag to be used by split drivers within guests */
#define VIRTIO_PCI_FLAG_PAGE_PER_VQ \
    (1 << VIRTIO_PCI_FLAG_PAGE_PER_VQ_BIT)

/* address space translation service */
#define VIRTIO_PCI_FLAG_ATS (1 << VIRTIO_PCI_FLAG_ATS_BIT)

/* Init error enabling flags */
#define VIRTIO_PCI_FLAG_INIT_DEVERR (1 << VIRTIO_PCI_FLAG_INIT_DEVERR_BIT)

/* Init Link Control register */
#define VIRTIO_PCI_FLAG_INIT_LNKCTL (1 << VIRTIO_PCI_FLAG_INIT_LNKCTL_BIT)

/* Init Power Management */
#define VIRTIO_PCI_FLAG_INIT_PM (1 << VIRTIO_PCI_FLAG_INIT_PM_BIT)

/* Init Function Level Reset capability */
#define VIRTIO_PCI_FLAG_INIT_FLR (1 << VIRTIO_PCI_FLAG_INIT_FLR_BIT)

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
    DeviceRealize parent_dc_realize;
    void (*realize)(VirtIOPCIProxy *vpci_dev, Error **errp);
} VirtioPCIClass;

typedef struct VirtIOPCIRegion {
    MemoryRegion mr;
    uint32_t offset;
    uint32_t size;
    uint32_t type;
} VirtIOPCIRegion;

typedef struct VirtIOPCIQueue {
  uint16_t num;
  bool enabled;
  uint32_t desc[2];
  uint32_t avail[2];
  uint32_t used[2];
} VirtIOPCIQueue;

struct VirtIOPCIProxy {
    PCIDevice pci_dev;
    MemoryRegion bar;
    union {
        struct {
            VirtIOPCIRegion common;
            VirtIOPCIRegion isr;
            VirtIOPCIRegion device;
            VirtIOPCIRegion notify;
            VirtIOPCIRegion notify_pio;
        };
        VirtIOPCIRegion regs[5];
    };
    MemoryRegion modern_bar;
    MemoryRegion io_bar;
    uint32_t legacy_io_bar_idx;
    uint32_t msix_bar_idx;
    uint32_t modern_io_bar_idx;
    uint32_t modern_mem_bar_idx;
    int config_cap;
    uint32_t flags;
    bool disable_modern;
    bool ignore_backend_features;
    OnOffAuto disable_legacy;
    uint32_t class_code;
    uint32_t nvectors;
    uint32_t dfselect;
    uint32_t gfselect;
    uint32_t guest_features[2];
    VirtIOPCIQueue vqs[VIRTIO_QUEUE_MAX];

    VirtIOIRQFD *vector_irqfd;
    int nvqs_with_notifiers;
    VirtioBusState bus;
};

static inline bool virtio_pci_modern(VirtIOPCIProxy *proxy)
{
    return !proxy->disable_modern;
}

static inline bool virtio_pci_legacy(VirtIOPCIProxy *proxy)
{
    return proxy->disable_legacy == ON_OFF_AUTO_OFF;
}

static inline void virtio_pci_force_virtio_1(VirtIOPCIProxy *proxy)
{
    proxy->disable_modern = false;
    proxy->disable_legacy = ON_OFF_AUTO_ON;
}

static inline void virtio_pci_disable_modern(VirtIOPCIProxy *proxy)
{
    proxy->disable_modern = true;
}

/*
 * virtio-input-pci: This extends VirtioPCIProxy.
 */
#define TYPE_VIRTIO_INPUT_PCI "virtio-input-pci"

/* Virtio ABI version, if we increment this, we break the guest driver. */
#define VIRTIO_PCI_ABI_VERSION          0

/* Input for virtio_pci_types_register() */
typedef struct VirtioPCIDeviceTypeInfo {
    /*
     * Common base class for the subclasses below.
     *
     * Required only if transitional_name or non_transitional_name is set.
     *
     * We need a separate base type instead of making all types
     * inherit from generic_name for two reasons:
     * 1) generic_name implements INTERFACE_PCIE_DEVICE, but
     *    transitional_name does not.
     * 2) generic_name has the "disable-legacy" and "disable-modern"
     *    properties, transitional_name and non_transitional name don't.
     */
    const char *base_name;
    /*
     * Generic device type.  Optional.
     *
     * Supports both transitional and non-transitional modes,
     * using the disable-legacy and disable-modern properties.
     * If disable-legacy=auto, (non-)transitional mode is selected
     * depending on the bus where the device is plugged.
     *
     * Implements both INTERFACE_PCIE_DEVICE and INTERFACE_CONVENTIONAL_PCI_DEVICE,
     * but PCI Express is supported only in non-transitional mode.
     *
     * The only type implemented by QEMU 3.1 and older.
     */
    const char *generic_name;
    /*
     * The transitional device type.  Optional.
     *
     * Implements both INTERFACE_PCIE_DEVICE and INTERFACE_CONVENTIONAL_PCI_DEVICE.
     */
    const char *transitional_name;
    /*
     * The non-transitional device type.  Optional.
     *
     * Implements INTERFACE_CONVENTIONAL_PCI_DEVICE only.
     */
    const char *non_transitional_name;

    /* Parent type.  If NULL, TYPE_VIRTIO_PCI is used */
    const char *parent;

    /* Same as TypeInfo fields: */
    size_t instance_size;
    size_t class_size;
    void (*instance_init)(Object *obj);
    void (*class_init)(ObjectClass *klass, void *data);
    InterfaceInfo *interfaces;
} VirtioPCIDeviceTypeInfo;

/* Register virtio-pci type(s).  @t must be static. */
void virtio_pci_types_register(const VirtioPCIDeviceTypeInfo *t);

#endif
