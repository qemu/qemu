/**
 * Copyright © 2022 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef REMOTE_IOMMU_H
#define REMOTE_IOMMU_H

#include "hw/pci/pci_bus.h"
#include "hw/pci/pci.h"

#ifndef INT2VOIDP
#define INT2VOIDP(i) (void *)(uintptr_t)(i)
#endif

typedef struct RemoteIommuElem {
    MemoryRegion *mr;

    AddressSpace as;
} RemoteIommuElem;

#define TYPE_REMOTE_IOMMU "x-remote-iommu"
OBJECT_DECLARE_SIMPLE_TYPE(RemoteIommu, REMOTE_IOMMU)

struct RemoteIommu {
    Object parent;

    GHashTable *elem_by_devfn;

    QemuMutex lock;
};

void remote_iommu_setup(PCIBus *pci_bus);

void remote_iommu_unplug_dev(PCIDevice *pci_dev);

#endif
