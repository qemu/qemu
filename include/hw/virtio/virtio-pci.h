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
#include "qom/object.h"


/* virtio-pci-bus */

typedef struct VirtioBusState VirtioPCIBusState;
typedef struct VirtioBusClass VirtioPCIBusClass;

#define TYPE_VIRTIO_PCI_BUS "virtio-pci-bus"
DECLARE_OBJ_CHECKERS(VirtioPCIBusState, VirtioPCIBusClass,
                     VIRTIO_PCI_BUS, TYPE_VIRTIO_PCI_BUS)

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
    VIRTIO_PCI_FLAG_AER_BIT,
    VIRTIO_PCI_FLAG_ATS_PAGE_ALIGNED_BIT,
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

/* Advanced Error Reporting capability */
#define VIRTIO_PCI_FLAG_AER (1 << VIRTIO_PCI_FLAG_AER_BIT)

/* Page Aligned Address space Translation Service */
#define VIRTIO_PCI_FLAG_ATS_PAGE_ALIGNED \
  (1 << VIRTIO_PCI_FLAG_ATS_PAGE_ALIGNED_BIT)

typedef struct {
    MSIMessage msg;
    int virq;
    unsigned int users;
} VirtIOIRQFD;

/*
 * virtio-pci: This is the PCIDevice which has a virtio-pci-bus.
 */
#define TYPE_VIRTIO_PCI "virtio-pci"
OBJECT_DECLARE_TYPE(VirtIOPCIProxy, VirtioPCIClass, VIRTIO_PCI)

struct VirtioPCIClass {
    PCIDeviceClass parent_class;
    DeviceRealize parent_dc_realize;
    void (*realize)(VirtIOPCIProxy *vpci_dev, Error **errp);
};

typedef struct VirtIOPCIRegion {
    MemoryRegion mr;
    uint32_t offset;
    uint32_t size;
    uint32_t type;
} VirtIOPCIRegion;

typedef struct VirtIOPCIQueue {
  uint16_t num;
  bool enabled;
  /*
   * No need to migrate the reset status, because it is always 0
   * when the migration starts.
   */
  bool reset;
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
    /* Transitional device id */
    uint16_t trans_devid;
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

uint16_t virtio_pci_get_trans_devid(uint16_t device_id);
uint16_t virtio_pci_get_class_id(uint16_t device_id);

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

/**
 * virtio_pci_optimal_num_queues:
 * @fixed_queues: number of queues that are always present
 *
 * Returns: The optimal number of queues for a multi-queue device, excluding
 * @fixed_queues.
 */
unsigned virtio_pci_optimal_num_queues(unsigned fixed_queues);
void virtio_pci_set_guest_notifier_fd_handler(VirtIODevice *vdev, VirtQueue *vq,
                                              int n, bool assign,
                                              bool with_irqfd);
#endif
