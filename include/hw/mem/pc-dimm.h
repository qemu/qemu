/*
 * PC DIMM device
 *
 * Copyright ProfitBricks GmbH 2012
 * Copyright (C) 2013-2014 Red Hat Inc
 *
 * Authors:
 *  Vasilis Liaskovitis <vasilis.liaskovitis@profitbricks.com>
 *  Igor Mammedov <imammedo@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_PC_DIMM_H
#define QEMU_PC_DIMM_H

#include "exec/memory.h"
#include "sysemu/hostmem.h"
#include "hw/qdev.h"

#define DEFAULT_PC_DIMMSIZE (1024*1024*1024)

#define TYPE_PC_DIMM "pc-dimm"
#define PC_DIMM(obj) \
    OBJECT_CHECK(PCDIMMDevice, (obj), TYPE_PC_DIMM)
#define PC_DIMM_CLASS(oc) \
    OBJECT_CLASS_CHECK(PCDIMMDeviceClass, (oc), TYPE_PC_DIMM)
#define PC_DIMM_GET_CLASS(obj) \
    OBJECT_GET_CLASS(PCDIMMDeviceClass, (obj), TYPE_PC_DIMM)

#define PC_DIMM_ADDR_PROP "addr"
#define PC_DIMM_SLOT_PROP "slot"
#define PC_DIMM_NODE_PROP "node"
#define PC_DIMM_SIZE_PROP "size"
#define PC_DIMM_MEMDEV_PROP "memdev"

#define PC_DIMM_UNASSIGNED_SLOT -1

/**
 * PCDIMMDevice:
 * @addr: starting guest physical address, where @PCDIMMDevice is mapped.
 *         Default value: 0, means that address is auto-allocated.
 * @node: numa node to which @PCDIMMDevice is attached.
 * @slot: slot number into which @PCDIMMDevice is plugged in.
 *        Default value: -1, means that slot is auto-allocated.
 * @hostmem: host memory backend providing memory for @PCDIMMDevice
 */
typedef struct PCDIMMDevice {
    /* private */
    DeviceState parent_obj;

    /* public */
    uint64_t addr;
    uint32_t node;
    int32_t slot;
    HostMemoryBackend *hostmem;
} PCDIMMDevice;

/**
 * PCDIMMDeviceClass:
 * @get_memory_region: returns #MemoryRegion associated with @dimm
 */
typedef struct PCDIMMDeviceClass {
    /* private */
    DeviceClass parent_class;

    /* public */
    MemoryRegion *(*get_memory_region)(PCDIMMDevice *dimm);
} PCDIMMDeviceClass;

/**
 * MemoryHotplugState:
 * @base: address in guest RAM address space where hotplug memory
 * address space begins.
 * @mr: hotplug memory address space container
 */
typedef struct MemoryHotplugState {
    hwaddr base;
    MemoryRegion mr;
} MemoryHotplugState;

uint64_t pc_dimm_get_free_addr(uint64_t address_space_start,
                               uint64_t address_space_size,
                               uint64_t *hint, uint64_t align, uint64_t size,
                               Error **errp);

int pc_dimm_get_free_slot(const int *hint, int max_slots, Error **errp);

int qmp_pc_dimm_device_list(Object *obj, void *opaque);
uint64_t pc_existing_dimms_capacity(Error **errp);
void pc_dimm_memory_plug(DeviceState *dev, MemoryHotplugState *hpms,
                         MemoryRegion *mr, uint64_t align, Error **errp);
void pc_dimm_memory_unplug(DeviceState *dev, MemoryHotplugState *hpms,
                           MemoryRegion *mr);
#endif
