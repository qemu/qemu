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

typedef struct MemoryDeviceState MemoryDeviceState;

/**
 * MemoryDeviceClass:
 *
 * All memory devices need to implement TYPE_MEMORY_DEVICE as an interface.
 *
 * A memory device is a device that owns a memory region which is
 * mapped into guest physical address space at a certain address. The
 * address in guest physical memory can either be specified explicitly
 * or get assigned automatically.
 *
 * Conceptually, memory devices only span one memory region. If multiple
 * successive memory regions are used, a covering memory region has to
 * be provided. Scattered memory regions are not supported for single
 * devices.
 */
typedef struct MemoryDeviceClass {
    /* private */
    InterfaceClass parent_class;

    /*
     * Return the address of the memory device in guest physical memory.
     *
     * Called when (un)plugging a memory device or when iterating over
     * all memory devices mapped into guest physical address space.
     *
     * If "0" is returned, no address has been specified by the user and
     * no address has been assigned to this memory device yet.
     */
    uint64_t (*get_addr)(const MemoryDeviceState *md);

    /*
     * Set the address of the memory device in guest physical memory.
     *
     * Called when plugging the memory device to configure the determined
     * address in guest physical memory.
     */
    void (*set_addr)(MemoryDeviceState *md, uint64_t addr, Error **errp);

    /*
     * Return the amount of memory provided by the memory device currently
     * usable ("plugged") by the VM.
     *
     * Called when calculating the total amount of ram available to the
     * VM (e.g. to report memory stats to the user).
     *
     * This is helpful for devices that dynamically manage the amount of
     * memory accessible by the guest via the reserved memory region. For
     * most devices, this corresponds to the size of the memory region.
     */
    uint64_t (*get_plugged_size)(const MemoryDeviceState *md, Error **errp);

    /*
     * Return the memory region of the memory device.
     *
     * Called when (un)plugging the memory device, to (un)map the
     * memory region in guest physical memory, but also to detect the
     * required alignment during address assignment or when the size of the
     * memory region is required.
     */
    MemoryRegion *(*get_memory_region)(MemoryDeviceState *md, Error **errp);

    /*
     * Translate the memory device into #MemoryDeviceInfo.
     */
    void (*fill_device_info)(const MemoryDeviceState *md,
                             MemoryDeviceInfo *info);
} MemoryDeviceClass;

MemoryDeviceInfoList *qmp_memory_device_list(void);
uint64_t get_plugged_memory_size(void);
void memory_device_pre_plug(MemoryDeviceState *md, MachineState *ms,
                            const uint64_t *legacy_align, Error **errp);
void memory_device_plug(MemoryDeviceState *md, MachineState *ms);
void memory_device_unplug(MemoryDeviceState *md, MachineState *ms);
uint64_t memory_device_get_region_size(const MemoryDeviceState *md,
                                       Error **errp);

#endif
