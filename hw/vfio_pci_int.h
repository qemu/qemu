/*
 * vfio based device assignment support
 *
 * Copyright Red Hat, Inc. 2012
 *
 * Authors:
 *  Alex Williamson <alex.williamson@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#ifndef HW_VFIO_PCI_INT_H
#define HW_VFIO_PCI_INT_H

#include "qemu-common.h"
#include "qemu-queue.h"
#include "pci.h"
#include "event_notifier.h"

typedef struct VFIOBAR {
    off_t fd_offset; /* offset of BAR within device fd */
    int fd; /* device fd, allows us to pass VFIOBAR as opaque data */
    MemoryRegion mem; /* slow, read/write access */
    MemoryRegion mmap_mem; /* direct mapped access */
    void *mmap;
    size_t size;
    uint32_t flags; /* VFIO region flags (rd/wr/mmap) */
    uint8_t nr; /* cache the BAR number for debug */
} VFIOBAR;

typedef struct VFIOINTx {
    bool pending; /* interrupt pending */
    bool kvm_accel; /* set when QEMU bypass through KVM enabled */
    uint8_t pin; /* which pin to pull for qemu_set_irq */
    EventNotifier interrupt; /* eventfd triggered on interrupt */
    EventNotifier unmask; /* eventfd for unmask on QEMU bypass */
    PCIINTxRoute route; /* routing info for QEMU bypass */
    uint32_t mmap_timeout; /* delay to re-enable mmaps after interrupt */
    QEMUTimer *mmap_timer; /* enable mmaps after periods w/o interrupts */
} VFIOINTx;

struct VFIODevice;

typedef struct VFIOMSIVector {
    EventNotifier interrupt; /* eventfd triggered on interrupt */
    struct VFIODevice *vdev; /* back pointer to device */
    int virq; /* KVM irqchip route for QEMU bypass */
    bool use;
} VFIOMSIVector;

enum {
    VFIO_INT_NONE = 0,
    VFIO_INT_INTx = 1,
    VFIO_INT_MSI  = 2,
    VFIO_INT_MSIX = 3,
};

struct VFIOGroup;

typedef struct VFIOContainer {
    int fd; /* /dev/vfio/vfio, empowered by the attached groups */
    struct {
        /* enable abstraction to support various iommu backends */
        union {
            MemoryListener listener; /* Used by type1 iommu */
        };
        void (*release)(struct VFIOContainer *);
    } iommu_data;
    QLIST_HEAD(, VFIOGroup) group_list;
    QLIST_ENTRY(VFIOContainer) next;
} VFIOContainer;

/* Cache of MSI-X setup plus extra mmap and memory region for split BAR map */
typedef struct VFIOMSIXInfo {
    uint8_t table_bar;
    uint8_t pba_bar;
    uint16_t entries;
    uint32_t table_offset;
    uint32_t pba_offset;
    MemoryRegion mmap_mem;
    void *mmap;
} VFIOMSIXInfo;

typedef struct VFIODevice {
    PCIDevice pdev;
    int fd;
    VFIOINTx intx;
    unsigned int config_size;
    off_t config_offset; /* Offset of config space region within device fd */
    unsigned int rom_size;
    off_t rom_offset; /* Offset of ROM region within device fd */
    int msi_cap_size;
    VFIOMSIVector *msi_vectors;
    VFIOMSIXInfo *msix;
    int nr_vectors; /* Number of MSI/MSIX vectors currently in use */
    int interrupt; /* Current interrupt type */
    VFIOBAR bars[PCI_NUM_REGIONS - 1]; /* No ROM */
    PCIHostDeviceAddress host;
    QLIST_ENTRY(VFIODevice) next;
    struct VFIOGroup *group;
    bool reset_works;
} VFIODevice;

typedef struct VFIOGroup {
    int fd;
    int groupid;
    VFIOContainer *container;
    QLIST_HEAD(, VFIODevice) device_list;
    QLIST_ENTRY(VFIOGroup) next;
    QLIST_ENTRY(VFIOGroup) container_next;
} VFIOGroup;

#endif /* HW_VFIO_PCI_INT_H */
