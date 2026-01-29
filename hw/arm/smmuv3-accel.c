/*
 * Copyright (c) 2025 Huawei Technologies R & D (UK) Ltd
 * Copyright (C) 2025 NVIDIA
 * Written by Nicolin Chen, Shameer Kolothum
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/arm/smmuv3.h"
#include "smmuv3-accel.h"

/*
 * The root region aliases the global system memory, and shared_as_sysmem
 * provides a shared Address Space referencing it. This Address Space is used
 * by all vfio-pci devices behind all accelerated SMMUv3 instances within a VM.
 */
static MemoryRegion root, sysmem;
static AddressSpace *shared_as_sysmem;

static SMMUv3AccelDevice *smmuv3_accel_get_dev(SMMUState *bs, SMMUPciBus *sbus,
                                               PCIBus *bus, int devfn)
{
    SMMUDevice *sdev = sbus->pbdev[devfn];
    SMMUv3AccelDevice *accel_dev;

    if (sdev) {
        return container_of(sdev, SMMUv3AccelDevice, sdev);
    }

    accel_dev = g_new0(SMMUv3AccelDevice, 1);
    sdev = &accel_dev->sdev;

    sbus->pbdev[devfn] = sdev;
    smmu_init_sdev(bs, sdev, bus, devfn);
    return accel_dev;
}

/*
 * Find or add an address space for the given PCI device.
 *
 * If a device matching @bus and @devfn already exists, return its
 * corresponding address space. Otherwise, create a new device entry
 * and initialize address space for it.
 */
static AddressSpace *smmuv3_accel_find_add_as(PCIBus *bus, void *opaque,
                                              int devfn)
{
    SMMUState *bs = opaque;
    SMMUPciBus *sbus = smmu_get_sbus(bs, bus);
    SMMUv3AccelDevice *accel_dev = smmuv3_accel_get_dev(bs, sbus, bus, devfn);
    SMMUDevice *sdev = &accel_dev->sdev;

    return &sdev->as;
}

static const PCIIOMMUOps smmuv3_accel_ops = {
    .get_address_space = smmuv3_accel_find_add_as,
};

static void smmuv3_accel_as_init(SMMUv3State *s)
{

    if (shared_as_sysmem) {
        return;
    }

    memory_region_init(&root, OBJECT(s), "root", UINT64_MAX);
    memory_region_init_alias(&sysmem, OBJECT(s), "smmuv3-accel-sysmem",
                             get_system_memory(), 0,
                             memory_region_size(get_system_memory()));
    memory_region_add_subregion(&root, 0, &sysmem);

    shared_as_sysmem = g_new0(AddressSpace, 1);
    address_space_init(shared_as_sysmem, &root, "smmuv3-accel-as-sysmem");
}

void smmuv3_accel_init(SMMUv3State *s)
{
    SMMUState *bs = ARM_SMMU(s);

    bs->iommu_ops = &smmuv3_accel_ops;
    smmuv3_accel_as_init(s);
}
