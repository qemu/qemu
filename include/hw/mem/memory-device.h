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

#include "hw/qdev-core.h"
#include "qapi/qapi-types-machine.h"
#include "qom/object.h"

#define TYPE_MEMORY_DEVICE "memory-device"

typedef struct MemoryDeviceClass MemoryDeviceClass;
DECLARE_CLASS_CHECKERS(MemoryDeviceClass, MEMORY_DEVICE,
                       TYPE_MEMORY_DEVICE)
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
 * Some memory device might not own a memory region in certain device
 * configurations. Such devices can logically get (un)plugged, however,
 * empty memory devices are mostly ignored by the memory device code.
 *
 * Conceptually, memory devices only span one memory region. If multiple
 * successive memory regions are used, a covering memory region has to
 * be provided. Scattered memory regions are not supported for single
 * devices.
 *
 * The device memory region returned via @get_memory_region may either be a
 * single RAM memory region or a memory region container with subregions
 * that are RAM memory regions or aliases to RAM memory regions. Other
 * memory regions or subregions are not supported.
 *
 * If the device memory region returned via @get_memory_region is a
 * memory region container, it's supported to dynamically (un)map subregions
 * as long as the number of memslots returned by @get_memslots() won't
 * be exceeded and as long as all memory regions are of the same kind (e.g.,
 * all RAM or all ROM).
 */
struct MemoryDeviceClass {
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
     * Return the memory region of the memory device. If the device is
     * completely empty, returns NULL without an error.
     *
     * Called when (un)plugging the memory device, to (un)map the
     * memory region in guest physical memory, but also to detect the
     * required alignment during address assignment or when the size of the
     * memory region is required.
     */
    MemoryRegion *(*get_memory_region)(MemoryDeviceState *md, Error **errp);

    /*
     * Optional: Instruct the memory device to decide how many memory slots
     * it requires, not exceeding the given limit.
     *
     * Called exactly once when pre-plugging the memory device, before
     * querying the number of memslots using @get_memslots the first time.
     */
    void (*decide_memslots)(MemoryDeviceState *md, unsigned int limit);

    /*
     * Optional for memory devices that require only a single memslot,
     * required for all other memory devices: Return the number of memslots
     * (distinct RAM memory regions in the device memory region) that are
     * required by the device.
     *
     * If this function is not implemented, the assumption is "1".
     *
     * Called when (un)plugging the memory device, to check if the requirements
     * can be satisfied, and to do proper accounting.
     */
    unsigned int (*get_memslots)(MemoryDeviceState *md);

    /*
     * Optional: Return the desired minimum alignment of the device in guest
     * physical address space. The final alignment is computed based on this
     * alignment and the alignment requirements of the memory region.
     *
     * Called when plugging the memory device to detect the required alignment
     * during address assignment.
     */
    uint64_t (*get_min_alignment)(const MemoryDeviceState *md);

    /*
     * Translate the memory device into #MemoryDeviceInfo.
     */
    void (*fill_device_info)(const MemoryDeviceState *md,
                             MemoryDeviceInfo *info);
};

/*
 * Traditionally, KVM/vhost in many setups supported 509 memslots, whereby
 * 253 memslots were "reserved" for boot memory and other devices (such
 * as PCI BARs, which can get mapped dynamically) and 256 memslots were
 * dedicated for DIMMs. These magic numbers worked reliably in the past.
 *
 * Further, using many memslots can negatively affect performance, so setting
 * the soft-limit of memslots used by memory devices to the traditional
 * DIMM limit of 256 sounds reasonable.
 *
 * If we have less than 509 memslots, we will instruct memory devices that
 * support automatically deciding how many memslots to use to only use a single
 * one.
 *
 * Hotplugging vhost devices with at least 509 memslots is not expected to
 * cause problems, not even when devices automatically decided how many memslots
 * to use.
 */
#define MEMORY_DEVICES_SOFT_MEMSLOT_LIMIT 256
#define MEMORY_DEVICES_SAFE_MAX_MEMSLOTS 509

MemoryDeviceInfoList *qmp_memory_device_list(void);
uint64_t get_plugged_memory_size(void);
unsigned int memory_devices_get_reserved_memslots(void);
bool memory_devices_memslot_auto_decision_active(void);
void memory_device_pre_plug(MemoryDeviceState *md, MachineState *ms,
                            const uint64_t *legacy_align, Error **errp);
void memory_device_plug(MemoryDeviceState *md, MachineState *ms);
void memory_device_unplug(MemoryDeviceState *md, MachineState *ms);
uint64_t memory_device_get_region_size(const MemoryDeviceState *md,
                                       Error **errp);

#endif
