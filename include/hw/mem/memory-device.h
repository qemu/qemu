/*
 * Memory Device Interface
 *
 * Copyright (c) 2018 Red Hat, Inc.
 *
 * Authors:
 *  David Hildenbrand <david@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef MEMORY_DEVICE_H
#define MEMORY_DEVICE_H

#include "qom/object.h"
#include "hw/qdev.h"

#define TYPE_MEMORY_DEVICE "memory-device"

#define MEMORY_DEVICE_CLASS(klass) \
     OBJECT_CLASS_CHECK(MemoryDeviceClass, (klass), TYPE_MEMORY_DEVICE)
#define MEMORY_DEVICE_GET_CLASS(obj) \
    OBJECT_GET_CLASS(MemoryDeviceClass, (obj), TYPE_MEMORY_DEVICE)
#define MEMORY_DEVICE(obj) \
     INTERFACE_CHECK(MemoryDeviceState, (obj), TYPE_MEMORY_DEVICE)

typedef struct MemoryDeviceState {
    Object parent_obj;
} MemoryDeviceState;

typedef struct MemoryDeviceClass {
    InterfaceClass parent_class;

    uint64_t (*get_addr)(const MemoryDeviceState *md);
    uint64_t (*get_plugged_size)(const MemoryDeviceState *md);
    uint64_t (*get_region_size)(const MemoryDeviceState *md);
    void (*fill_device_info)(const MemoryDeviceState *md,
                             MemoryDeviceInfo *info);
} MemoryDeviceClass;

MemoryDeviceInfoList *qmp_memory_device_list(void);
uint64_t get_plugged_memory_size(void);
uint64_t memory_device_get_free_addr(MachineState *ms, const uint64_t *hint,
                                     uint64_t align, uint64_t size,
                                     Error **errp);
void memory_device_plug_region(MachineState *ms, MemoryRegion *mr,
                               uint64_t addr);
void memory_device_unplug_region(MachineState *ms, MemoryRegion *mr);

#endif
