/*
 * Physical memory management API
 *
 * Copyright 2011 Red Hat, Inc. and/or its affiliates
 *
 * Authors:
 *  Avi Kivity <avi@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef MEMORY_H
#define MEMORY_H

#ifndef CONFIG_USER_ONLY

#define DIRTY_MEMORY_VGA       0
#define DIRTY_MEMORY_CODE      1
#define DIRTY_MEMORY_MIGRATION 2
#define DIRTY_MEMORY_NUM       3        /* num of dirty bits */

#include <stdint.h>
#include <stdbool.h>
#include "qemu-common.h"
#include "exec/cpu-common.h"
#ifndef CONFIG_USER_ONLY
#include "exec/hwaddr.h"
#endif
#include "qemu/queue.h"
#include "qemu/int128.h"
#include "qemu/notify.h"
#include "qapi/error.h"
#include "qom/object.h"

#define MAX_PHYS_ADDR_SPACE_BITS 62
#define MAX_PHYS_ADDR            (((hwaddr)1 << MAX_PHYS_ADDR_SPACE_BITS) - 1)

#define TYPE_MEMORY_REGION "qemu:memory-region"
#define MEMORY_REGION(obj) \
        OBJECT_CHECK(MemoryRegion, (obj), TYPE_MEMORY_REGION)

typedef struct MemoryRegionOps MemoryRegionOps;
typedef struct MemoryRegionMmio MemoryRegionMmio;

struct MemoryRegionMmio {
    CPUReadMemoryFunc *read[3];
    CPUWriteMemoryFunc *write[3];
};

typedef struct IOMMUTLBEntry IOMMUTLBEntry;

/* See address_space_translate: bit 0 is read, bit 1 is write.  */
typedef enum {
    IOMMU_NONE = 0,
    IOMMU_RO   = 1,
    IOMMU_WO   = 2,
    IOMMU_RW   = 3,
} IOMMUAccessFlags;

struct IOMMUTLBEntry {
    AddressSpace    *target_as;
    hwaddr           iova;
    hwaddr           translated_addr;
    hwaddr           addr_mask;  /* 0xfff = 4k translation */
    IOMMUAccessFlags perm;
};

/*
 * Memory region callbacks
 */
struct MemoryRegionOps {
    /* Read from the memory region. @addr is relative to @mr; @size is
     * in bytes. */
    uint64_t (*read)(void *opaque,
                     hwaddr addr,
                     unsigned size);
    /* Write to the memory region. @addr is relative to @mr; @size is
     * in bytes. */
    void (*write)(void *opaque,
                  hwaddr addr,
                  uint64_t data,
                  unsigned size);

    enum device_endian endianness;
    /* Guest-visible constraints: */
    struct {
        /* If nonzero, specify bounds on access sizes beyond which a machine
         * check is thrown.
         */
        unsigned min_access_size;
        unsigned max_access_size;
        /* If true, unaligned accesses are supported.  Otherwise unaligned
         * accesses throw machine checks.
         */
         bool unaligned;
        /*
         * If present, and returns #false, the transaction is not accepted
         * by the device (and results in machine dependent behaviour such
         * as a machine check exception).
         */
        bool (*accepts)(void *opaque, hwaddr addr,
                        unsigned size, bool is_write);
    } valid;
    /* Internal implementation constraints: */
    struct {
        /* If nonzero, specifies the minimum size implemented.  Smaller sizes
         * will be rounded upwards and a partial result will be returned.
         */
        unsigned min_access_size;
        /* If nonzero, specifies the maximum size implemented.  Larger sizes
         * will be done as a series of accesses with smaller sizes.
         */
        unsigned max_access_size;
        /* If true, unaligned accesses are supported.  Otherwise all accesses
         * are converted to (possibly multiple) naturally aligned accesses.
         */
        bool unaligned;
    } impl;

    /* If .read and .write are not present, old_mmio may be used for
     * backwards compatibility with old mmio registration
     */
    const MemoryRegionMmio old_mmio;
};

typedef struct MemoryRegionIOMMUOps MemoryRegionIOMMUOps;

struct MemoryRegionIOMMUOps {
    /* Return a TLB entry that contains a given address. */
    IOMMUTLBEntry (*translate)(MemoryRegion *iommu, hwaddr addr);
};

typedef struct CoalescedMemoryRange CoalescedMemoryRange;
typedef struct MemoryRegionIoeventfd MemoryRegionIoeventfd;

struct MemoryRegion {
    Object parent_obj;
    /* All fields are private - violators will be prosecuted */
    const MemoryRegionOps *ops;
    const MemoryRegionIOMMUOps *iommu_ops;
    void *opaque;
    MemoryRegion *container;
    Int128 size;
    hwaddr addr;
    void (*destructor)(MemoryRegion *mr);
    ram_addr_t ram_addr;
    bool subpage;
    bool terminates;
    bool romd_mode;
    bool ram;
    bool readonly; /* For RAM regions */
    bool enabled;
    bool rom_device;
    bool warning_printed; /* For reservations */
    bool flush_coalesced_mmio;
    MemoryRegion *alias;
    hwaddr alias_offset;
    int32_t priority;
    bool may_overlap;
    QTAILQ_HEAD(subregions, MemoryRegion) subregions;
    QTAILQ_ENTRY(MemoryRegion) subregions_link;
    QTAILQ_HEAD(coalesced_ranges, CoalescedMemoryRange) coalesced;
    uint8_t dirty_log_mask;
    unsigned ioeventfd_nb;
    MemoryRegionIoeventfd *ioeventfds;
    NotifierList iommu_notify;
};

/**
 * MemoryListener: callbacks structure for updates to the physical memory map
 *
 * Allows a component to adjust to changes in the guest-visible memory map.
 * Use with memory_listener_register() and memory_listener_unregister().
 */
struct MemoryListener {
    void (*begin)(MemoryListener *listener);
    void (*commit)(MemoryListener *listener);
    void (*region_add)(MemoryListener *listener, MemoryRegionSection *section);
    void (*region_del)(MemoryListener *listener, MemoryRegionSection *section);
    void (*region_nop)(MemoryListener *listener, MemoryRegionSection *section);
    void (*log_start)(MemoryListener *listener, MemoryRegionSection *section);
    void (*log_stop)(MemoryListener *listener, MemoryRegionSection *section);
    void (*log_sync)(MemoryListener *listener, MemoryRegionSection *section);
    void (*log_global_start)(MemoryListener *listener);
    void (*log_global_stop)(MemoryListener *listener);
    void (*eventfd_add)(MemoryListener *listener, MemoryRegionSection *section,
                        bool match_data, uint64_t data, EventNotifier *e);
    void (*eventfd_del)(MemoryListener *listener, MemoryRegionSection *section,
                        bool match_data, uint64_t data, EventNotifier *e);
    void (*coalesced_mmio_add)(MemoryListener *listener, MemoryRegionSection *section,
                               hwaddr addr, hwaddr len);
    void (*coalesced_mmio_del)(MemoryListener *listener, MemoryRegionSection *section,
                               hwaddr addr, hwaddr len);
    /* Lower = earlier (during add), later (during del) */
    unsigned priority;
    AddressSpace *address_space_filter;
    QTAILQ_ENTRY(MemoryListener) link;
};

/**
 * AddressSpace: describes a mapping of addresses to #MemoryRegion objects
 */
struct AddressSpace {
    /* All fields are private. */
    char *name;
    MemoryRegion *root;
    struct FlatView *current_map;
    int ioeventfd_nb;
    struct MemoryRegionIoeventfd *ioeventfds;
    struct AddressSpaceDispatch *dispatch;
    struct AddressSpaceDispatch *next_dispatch;
    MemoryListener dispatch_listener;

    QTAILQ_ENTRY(AddressSpace) address_spaces_link;
};

/**
 * MemoryRegionSection: describes a fragment of a #MemoryRegion
 *
 * @mr: the region, or %NULL if empty
 * @address_space: the address space the region is mapped in
 * @offset_within_region: the beginning of the section, relative to @mr's start
 * @size: the size of the section; will not exceed @mr's boundaries
 * @offset_within_address_space: the address of the first byte of the section
 *     relative to the region's address space
 * @readonly: writes to this section are ignored
 */
struct MemoryRegionSection {
    MemoryRegion *mr;
    AddressSpace *address_space;
    hwaddr offset_within_region;
    Int128 size;
    hwaddr offset_within_address_space;
    bool readonly;
};

/**
 * memory_region_init: Initialize a memory region
 *
 * The region typically acts as a container for other memory regions.  Use
 * memory_region_add_subregion() to add subregions.
 *
 * @mr: the #MemoryRegion to be initialized
 * @owner: the object that tracks the region's reference count
 * @name: used for debugging; not visible to the user or ABI
 * @size: size of the region; any subregions beyond this size will be clipped
 */
void memory_region_init(MemoryRegion *mr,
                        struct Object *owner,
                        const char *name,
                        uint64_t size);

/**
 * memory_region_ref: Add 1 to a memory region's reference count
 *
 * Whenever memory regions are accessed outside the BQL, they need to be
 * preserved against hot-unplug.  MemoryRegions actually do not have their
 * own reference count; they piggyback on a QOM object, their "owner".
 * This function adds a reference to the owner.
 *
 * All MemoryRegions must have an owner if they can disappear, even if the
 * device they belong to operates exclusively under the BQL.  This is because
 * the region could be returned at any time by memory_region_find, and this
 * is usually under guest control.
 *
 * @mr: the #MemoryRegion
 */
void memory_region_ref(MemoryRegion *mr);

/**
 * memory_region_unref: Remove 1 to a memory region's reference count
 *
 * Whenever memory regions are accessed outside the BQL, they need to be
 * preserved against hot-unplug.  MemoryRegions actually do not have their
 * own reference count; they piggyback on a QOM object, their "owner".
 * This function removes a reference to the owner and possibly destroys it.
 *
 * @mr: the #MemoryRegion
 */
void memory_region_unref(MemoryRegion *mr);

/**
 * memory_region_init_io: Initialize an I/O memory region.
 *
 * Accesses into the region will cause the callbacks in @ops to be called.
 * if @size is nonzero, subregions will be clipped to @size.
 *
 * @mr: the #MemoryRegion to be initialized.
 * @owner: the object that tracks the region's reference count
 * @ops: a structure containing read and write callbacks to be used when
 *       I/O is performed on the region.
 * @opaque: passed to to the read and write callbacks of the @ops structure.
 * @name: used for debugging; not visible to the user or ABI
 * @size: size of the region.
 */
void memory_region_init_io(MemoryRegion *mr,
                           struct Object *owner,
                           const MemoryRegionOps *ops,
                           void *opaque,
                           const char *name,
                           uint64_t size);

/**
 * memory_region_init_ram:  Initialize RAM memory region.  Accesses into the
 *                          region will modify memory directly.
 *
 * @mr: the #MemoryRegion to be initialized.
 * @owner: the object that tracks the region's reference count
 * @name: the name of the region.
 * @size: size of the region.
 */
void memory_region_init_ram(MemoryRegion *mr,
                            struct Object *owner,
                            const char *name,
                            uint64_t size);

#ifdef __linux__
/**
 * memory_region_init_ram_from_file:  Initialize RAM memory region with a
 *                                    mmap-ed backend.
 *
 * @mr: the #MemoryRegion to be initialized.
 * @owner: the object that tracks the region's reference count
 * @name: the name of the region.
 * @size: size of the region.
 * @share: %true if memory must be mmaped with the MAP_SHARED flag
 * @path: the path in which to allocate the RAM.
 * @errp: pointer to Error*, to store an error if it happens.
 */
void memory_region_init_ram_from_file(MemoryRegion *mr,
                                      struct Object *owner,
                                      const char *name,
                                      uint64_t size,
                                      bool share,
                                      const char *path,
                                      Error **errp);
#endif

/**
 * memory_region_init_ram_ptr:  Initialize RAM memory region from a
 *                              user-provided pointer.  Accesses into the
 *                              region will modify memory directly.
 *
 * @mr: the #MemoryRegion to be initialized.
 * @owner: the object that tracks the region's reference count
 * @name: the name of the region.
 * @size: size of the region.
 * @ptr: memory to be mapped; must contain at least @size bytes.
 */
void memory_region_init_ram_ptr(MemoryRegion *mr,
                                struct Object *owner,
                                const char *name,
                                uint64_t size,
                                void *ptr);

/**
 * memory_region_init_alias: Initialize a memory region that aliases all or a
 *                           part of another memory region.
 *
 * @mr: the #MemoryRegion to be initialized.
 * @owner: the object that tracks the region's reference count
 * @name: used for debugging; not visible to the user or ABI
 * @orig: the region to be referenced; @mr will be equivalent to
 *        @orig between @offset and @offset + @size - 1.
 * @offset: start of the section in @orig to be referenced.
 * @size: size of the region.
 */
void memory_region_init_alias(MemoryRegion *mr,
                              struct Object *owner,
                              const char *name,
                              MemoryRegion *orig,
                              hwaddr offset,
                              uint64_t size);

/**
 * memory_region_init_rom_device:  Initialize a ROM memory region.  Writes are
 *                                 handled via callbacks.
 *
 * @mr: the #MemoryRegion to be initialized.
 * @owner: the object that tracks the region's reference count
 * @ops: callbacks for write access handling.
 * @name: the name of the region.
 * @size: size of the region.
 */
void memory_region_init_rom_device(MemoryRegion *mr,
                                   struct Object *owner,
                                   const MemoryRegionOps *ops,
                                   void *opaque,
                                   const char *name,
                                   uint64_t size);

/**
 * memory_region_init_reservation: Initialize a memory region that reserves
 *                                 I/O space.
 *
 * A reservation region primariy serves debugging purposes.  It claims I/O
 * space that is not supposed to be handled by QEMU itself.  Any access via
 * the memory API will cause an abort().
 *
 * @mr: the #MemoryRegion to be initialized
 * @owner: the object that tracks the region's reference count
 * @name: used for debugging; not visible to the user or ABI
 * @size: size of the region.
 */
void memory_region_init_reservation(MemoryRegion *mr,
                                    struct Object *owner,
                                    const char *name,
                                    uint64_t size);

/**
 * memory_region_init_iommu: Initialize a memory region that translates
 * addresses
 *
 * An IOMMU region translates addresses and forwards accesses to a target
 * memory region.
 *
 * @mr: the #MemoryRegion to be initialized
 * @owner: the object that tracks the region's reference count
 * @ops: a function that translates addresses into the @target region
 * @name: used for debugging; not visible to the user or ABI
 * @size: size of the region.
 */
void memory_region_init_iommu(MemoryRegion *mr,
                              struct Object *owner,
                              const MemoryRegionIOMMUOps *ops,
                              const char *name,
                              uint64_t size);

/**
 * memory_region_owner: get a memory region's owner.
 *
 * @mr: the memory region being queried.
 */
struct Object *memory_region_owner(MemoryRegion *mr);

/**
 * memory_region_size: get a memory region's size.
 *
 * @mr: the memory region being queried.
 */
uint64_t memory_region_size(MemoryRegion *mr);

/**
 * memory_region_is_ram: check whether a memory region is random access
 *
 * Returns %true is a memory region is random access.
 *
 * @mr: the memory region being queried
 */
bool memory_region_is_ram(MemoryRegion *mr);

/**
 * memory_region_is_romd: check whether a memory region is in ROMD mode
 *
 * Returns %true if a memory region is a ROM device and currently set to allow
 * direct reads.
 *
 * @mr: the memory region being queried
 */
static inline bool memory_region_is_romd(MemoryRegion *mr)
{
    return mr->rom_device && mr->romd_mode;
}

/**
 * memory_region_is_iommu: check whether a memory region is an iommu
 *
 * Returns %true is a memory region is an iommu.
 *
 * @mr: the memory region being queried
 */
bool memory_region_is_iommu(MemoryRegion *mr);

/**
 * memory_region_notify_iommu: notify a change in an IOMMU translation entry.
 *
 * @mr: the memory region that was changed
 * @entry: the new entry in the IOMMU translation table.  The entry
 *         replaces all old entries for the same virtual I/O address range.
 *         Deleted entries have .@perm == 0.
 */
void memory_region_notify_iommu(MemoryRegion *mr,
                                IOMMUTLBEntry entry);

/**
 * memory_region_register_iommu_notifier: register a notifier for changes to
 * IOMMU translation entries.
 *
 * @mr: the memory region to observe
 * @n: the notifier to be added; the notifier receives a pointer to an
 *     #IOMMUTLBEntry as the opaque value; the pointer ceases to be
 *     valid on exit from the notifier.
 */
void memory_region_register_iommu_notifier(MemoryRegion *mr, Notifier *n);

/**
 * memory_region_unregister_iommu_notifier: unregister a notifier for
 * changes to IOMMU translation entries.
 *
 * @n: the notifier to be removed.
 */
void memory_region_unregister_iommu_notifier(Notifier *n);

/**
 * memory_region_name: get a memory region's name
 *
 * Returns the string that was used to initialize the memory region.
 *
 * @mr: the memory region being queried
 */
const char *memory_region_name(const MemoryRegion *mr);

/**
 * memory_region_is_logging: return whether a memory region is logging writes
 *
 * Returns %true if the memory region is logging writes
 *
 * @mr: the memory region being queried
 */
bool memory_region_is_logging(MemoryRegion *mr);

/**
 * memory_region_is_rom: check whether a memory region is ROM
 *
 * Returns %true is a memory region is read-only memory.
 *
 * @mr: the memory region being queried
 */
bool memory_region_is_rom(MemoryRegion *mr);

/**
 * memory_region_get_fd: Get a file descriptor backing a RAM memory region.
 *
 * Returns a file descriptor backing a file-based RAM memory region,
 * or -1 if the region is not a file-based RAM memory region.
 *
 * @mr: the RAM or alias memory region being queried.
 */
int memory_region_get_fd(MemoryRegion *mr);

/**
 * memory_region_get_ram_ptr: Get a pointer into a RAM memory region.
 *
 * Returns a host pointer to a RAM memory region (created with
 * memory_region_init_ram() or memory_region_init_ram_ptr()).  Use with
 * care.
 *
 * @mr: the memory region being queried.
 */
void *memory_region_get_ram_ptr(MemoryRegion *mr);

/**
 * memory_region_set_log: Turn dirty logging on or off for a region.
 *
 * Turns dirty logging on or off for a specified client (display, migration).
 * Only meaningful for RAM regions.
 *
 * @mr: the memory region being updated.
 * @log: whether dirty logging is to be enabled or disabled.
 * @client: the user of the logging information; %DIRTY_MEMORY_MIGRATION or
 *          %DIRTY_MEMORY_VGA.
 */
void memory_region_set_log(MemoryRegion *mr, bool log, unsigned client);

/**
 * memory_region_get_dirty: Check whether a range of bytes is dirty
 *                          for a specified client.
 *
 * Checks whether a range of bytes has been written to since the last
 * call to memory_region_reset_dirty() with the same @client.  Dirty logging
 * must be enabled.
 *
 * @mr: the memory region being queried.
 * @addr: the address (relative to the start of the region) being queried.
 * @size: the size of the range being queried.
 * @client: the user of the logging information; %DIRTY_MEMORY_MIGRATION or
 *          %DIRTY_MEMORY_VGA.
 */
bool memory_region_get_dirty(MemoryRegion *mr, hwaddr addr,
                             hwaddr size, unsigned client);

/**
 * memory_region_set_dirty: Mark a range of bytes as dirty in a memory region.
 *
 * Marks a range of bytes as dirty, after it has been dirtied outside
 * guest code.
 *
 * @mr: the memory region being dirtied.
 * @addr: the address (relative to the start of the region) being dirtied.
 * @size: size of the range being dirtied.
 */
void memory_region_set_dirty(MemoryRegion *mr, hwaddr addr,
                             hwaddr size);

/**
 * memory_region_test_and_clear_dirty: Check whether a range of bytes is dirty
 *                                     for a specified client. It clears them.
 *
 * Checks whether a range of bytes has been written to since the last
 * call to memory_region_reset_dirty() with the same @client.  Dirty logging
 * must be enabled.
 *
 * @mr: the memory region being queried.
 * @addr: the address (relative to the start of the region) being queried.
 * @size: the size of the range being queried.
 * @client: the user of the logging information; %DIRTY_MEMORY_MIGRATION or
 *          %DIRTY_MEMORY_VGA.
 */
bool memory_region_test_and_clear_dirty(MemoryRegion *mr, hwaddr addr,
                                        hwaddr size, unsigned client);
/**
 * memory_region_sync_dirty_bitmap: Synchronize a region's dirty bitmap with
 *                                  any external TLBs (e.g. kvm)
 *
 * Flushes dirty information from accelerators such as kvm and vhost-net
 * and makes it available to users of the memory API.
 *
 * @mr: the region being flushed.
 */
void memory_region_sync_dirty_bitmap(MemoryRegion *mr);

/**
 * memory_region_reset_dirty: Mark a range of pages as clean, for a specified
 *                            client.
 *
 * Marks a range of pages as no longer dirty.
 *
 * @mr: the region being updated.
 * @addr: the start of the subrange being cleaned.
 * @size: the size of the subrange being cleaned.
 * @client: the user of the logging information; %DIRTY_MEMORY_MIGRATION or
 *          %DIRTY_MEMORY_VGA.
 */
void memory_region_reset_dirty(MemoryRegion *mr, hwaddr addr,
                               hwaddr size, unsigned client);

/**
 * memory_region_set_readonly: Turn a memory region read-only (or read-write)
 *
 * Allows a memory region to be marked as read-only (turning it into a ROM).
 * only useful on RAM regions.
 *
 * @mr: the region being updated.
 * @readonly: whether rhe region is to be ROM or RAM.
 */
void memory_region_set_readonly(MemoryRegion *mr, bool readonly);

/**
 * memory_region_rom_device_set_romd: enable/disable ROMD mode
 *
 * Allows a ROM device (initialized with memory_region_init_rom_device() to
 * set to ROMD mode (default) or MMIO mode.  When it is in ROMD mode, the
 * device is mapped to guest memory and satisfies read access directly.
 * When in MMIO mode, reads are forwarded to the #MemoryRegion.read function.
 * Writes are always handled by the #MemoryRegion.write function.
 *
 * @mr: the memory region to be updated
 * @romd_mode: %true to put the region into ROMD mode
 */
void memory_region_rom_device_set_romd(MemoryRegion *mr, bool romd_mode);

/**
 * memory_region_set_coalescing: Enable memory coalescing for the region.
 *
 * Enabled writes to a region to be queued for later processing. MMIO ->write
 * callbacks may be delayed until a non-coalesced MMIO is issued.
 * Only useful for IO regions.  Roughly similar to write-combining hardware.
 *
 * @mr: the memory region to be write coalesced
 */
void memory_region_set_coalescing(MemoryRegion *mr);

/**
 * memory_region_add_coalescing: Enable memory coalescing for a sub-range of
 *                               a region.
 *
 * Like memory_region_set_coalescing(), but works on a sub-range of a region.
 * Multiple calls can be issued coalesced disjoint ranges.
 *
 * @mr: the memory region to be updated.
 * @offset: the start of the range within the region to be coalesced.
 * @size: the size of the subrange to be coalesced.
 */
void memory_region_add_coalescing(MemoryRegion *mr,
                                  hwaddr offset,
                                  uint64_t size);

/**
 * memory_region_clear_coalescing: Disable MMIO coalescing for the region.
 *
 * Disables any coalescing caused by memory_region_set_coalescing() or
 * memory_region_add_coalescing().  Roughly equivalent to uncacheble memory
 * hardware.
 *
 * @mr: the memory region to be updated.
 */
void memory_region_clear_coalescing(MemoryRegion *mr);

/**
 * memory_region_set_flush_coalesced: Enforce memory coalescing flush before
 *                                    accesses.
 *
 * Ensure that pending coalesced MMIO request are flushed before the memory
 * region is accessed. This property is automatically enabled for all regions
 * passed to memory_region_set_coalescing() and memory_region_add_coalescing().
 *
 * @mr: the memory region to be updated.
 */
void memory_region_set_flush_coalesced(MemoryRegion *mr);

/**
 * memory_region_clear_flush_coalesced: Disable memory coalescing flush before
 *                                      accesses.
 *
 * Clear the automatic coalesced MMIO flushing enabled via
 * memory_region_set_flush_coalesced. Note that this service has no effect on
 * memory regions that have MMIO coalescing enabled for themselves. For them,
 * automatic flushing will stop once coalescing is disabled.
 *
 * @mr: the memory region to be updated.
 */
void memory_region_clear_flush_coalesced(MemoryRegion *mr);

/**
 * memory_region_add_eventfd: Request an eventfd to be triggered when a word
 *                            is written to a location.
 *
 * Marks a word in an IO region (initialized with memory_region_init_io())
 * as a trigger for an eventfd event.  The I/O callback will not be called.
 * The caller must be prepared to handle failure (that is, take the required
 * action if the callback _is_ called).
 *
 * @mr: the memory region being updated.
 * @addr: the address within @mr that is to be monitored
 * @size: the size of the access to trigger the eventfd
 * @match_data: whether to match against @data, instead of just @addr
 * @data: the data to match against the guest write
 * @fd: the eventfd to be triggered when @addr, @size, and @data all match.
 **/
void memory_region_add_eventfd(MemoryRegion *mr,
                               hwaddr addr,
                               unsigned size,
                               bool match_data,
                               uint64_t data,
                               EventNotifier *e);

/**
 * memory_region_del_eventfd: Cancel an eventfd.
 *
 * Cancels an eventfd trigger requested by a previous
 * memory_region_add_eventfd() call.
 *
 * @mr: the memory region being updated.
 * @addr: the address within @mr that is to be monitored
 * @size: the size of the access to trigger the eventfd
 * @match_data: whether to match against @data, instead of just @addr
 * @data: the data to match against the guest write
 * @fd: the eventfd to be triggered when @addr, @size, and @data all match.
 */
void memory_region_del_eventfd(MemoryRegion *mr,
                               hwaddr addr,
                               unsigned size,
                               bool match_data,
                               uint64_t data,
                               EventNotifier *e);

/**
 * memory_region_add_subregion: Add a subregion to a container.
 *
 * Adds a subregion at @offset.  The subregion may not overlap with other
 * subregions (except for those explicitly marked as overlapping).  A region
 * may only be added once as a subregion (unless removed with
 * memory_region_del_subregion()); use memory_region_init_alias() if you
 * want a region to be a subregion in multiple locations.
 *
 * @mr: the region to contain the new subregion; must be a container
 *      initialized with memory_region_init().
 * @offset: the offset relative to @mr where @subregion is added.
 * @subregion: the subregion to be added.
 */
void memory_region_add_subregion(MemoryRegion *mr,
                                 hwaddr offset,
                                 MemoryRegion *subregion);
/**
 * memory_region_add_subregion_overlap: Add a subregion to a container
 *                                      with overlap.
 *
 * Adds a subregion at @offset.  The subregion may overlap with other
 * subregions.  Conflicts are resolved by having a higher @priority hide a
 * lower @priority. Subregions without priority are taken as @priority 0.
 * A region may only be added once as a subregion (unless removed with
 * memory_region_del_subregion()); use memory_region_init_alias() if you
 * want a region to be a subregion in multiple locations.
 *
 * @mr: the region to contain the new subregion; must be a container
 *      initialized with memory_region_init().
 * @offset: the offset relative to @mr where @subregion is added.
 * @subregion: the subregion to be added.
 * @priority: used for resolving overlaps; highest priority wins.
 */
void memory_region_add_subregion_overlap(MemoryRegion *mr,
                                         hwaddr offset,
                                         MemoryRegion *subregion,
                                         int priority);

/**
 * memory_region_get_ram_addr: Get the ram address associated with a memory
 *                             region
 *
 * DO NOT USE THIS FUNCTION.  This is a temporary workaround while the Xen
 * code is being reworked.
 */
ram_addr_t memory_region_get_ram_addr(MemoryRegion *mr);

/**
 * memory_region_del_subregion: Remove a subregion.
 *
 * Removes a subregion from its container.
 *
 * @mr: the container to be updated.
 * @subregion: the region being removed; must be a current subregion of @mr.
 */
void memory_region_del_subregion(MemoryRegion *mr,
                                 MemoryRegion *subregion);

/*
 * memory_region_set_enabled: dynamically enable or disable a region
 *
 * Enables or disables a memory region.  A disabled memory region
 * ignores all accesses to itself and its subregions.  It does not
 * obscure sibling subregions with lower priority - it simply behaves as
 * if it was removed from the hierarchy.
 *
 * Regions default to being enabled.
 *
 * @mr: the region to be updated
 * @enabled: whether to enable or disable the region
 */
void memory_region_set_enabled(MemoryRegion *mr, bool enabled);

/*
 * memory_region_set_address: dynamically update the address of a region
 *
 * Dynamically updates the address of a region, relative to its container.
 * May be used on regions are currently part of a memory hierarchy.
 *
 * @mr: the region to be updated
 * @addr: new address, relative to container region
 */
void memory_region_set_address(MemoryRegion *mr, hwaddr addr);

/*
 * memory_region_set_alias_offset: dynamically update a memory alias's offset
 *
 * Dynamically updates the offset into the target region that an alias points
 * to, as if the fourth argument to memory_region_init_alias() has changed.
 *
 * @mr: the #MemoryRegion to be updated; should be an alias.
 * @offset: the new offset into the target memory region
 */
void memory_region_set_alias_offset(MemoryRegion *mr,
                                    hwaddr offset);

/**
 * memory_region_present: checks if an address relative to a @container
 * translates into #MemoryRegion within @container
 *
 * Answer whether a #MemoryRegion within @container covers the address
 * @addr.
 *
 * @container: a #MemoryRegion within which @addr is a relative address
 * @addr: the area within @container to be searched
 */
bool memory_region_present(MemoryRegion *container, hwaddr addr);

/**
 * memory_region_is_mapped: returns true if #MemoryRegion is mapped
 * into any address space.
 *
 * @mr: a #MemoryRegion which should be checked if it's mapped
 */
bool memory_region_is_mapped(MemoryRegion *mr);

/**
 * memory_region_find: translate an address/size relative to a
 * MemoryRegion into a #MemoryRegionSection.
 *
 * Locates the first #MemoryRegion within @mr that overlaps the range
 * given by @addr and @size.
 *
 * Returns a #MemoryRegionSection that describes a contiguous overlap.
 * It will have the following characteristics:
 *    .@size = 0 iff no overlap was found
 *    .@mr is non-%NULL iff an overlap was found
 *
 * Remember that in the return value the @offset_within_region is
 * relative to the returned region (in the .@mr field), not to the
 * @mr argument.
 *
 * Similarly, the .@offset_within_address_space is relative to the
 * address space that contains both regions, the passed and the
 * returned one.  However, in the special case where the @mr argument
 * has no container (and thus is the root of the address space), the
 * following will hold:
 *    .@offset_within_address_space >= @addr
 *    .@offset_within_address_space + .@size <= @addr + @size
 *
 * @mr: a MemoryRegion within which @addr is a relative address
 * @addr: start of the area within @as to be searched
 * @size: size of the area to be searched
 */
MemoryRegionSection memory_region_find(MemoryRegion *mr,
                                       hwaddr addr, uint64_t size);

/**
 * address_space_sync_dirty_bitmap: synchronize the dirty log for all memory
 *
 * Synchronizes the dirty page log for an entire address space.
 * @as: the address space that contains the memory being synchronized
 */
void address_space_sync_dirty_bitmap(AddressSpace *as);

/**
 * memory_region_transaction_begin: Start a transaction.
 *
 * During a transaction, changes will be accumulated and made visible
 * only when the transaction ends (is committed).
 */
void memory_region_transaction_begin(void);

/**
 * memory_region_transaction_commit: Commit a transaction and make changes
 *                                   visible to the guest.
 */
void memory_region_transaction_commit(void);

/**
 * memory_listener_register: register callbacks to be called when memory
 *                           sections are mapped or unmapped into an address
 *                           space
 *
 * @listener: an object containing the callbacks to be called
 * @filter: if non-%NULL, only regions in this address space will be observed
 */
void memory_listener_register(MemoryListener *listener, AddressSpace *filter);

/**
 * memory_listener_unregister: undo the effect of memory_listener_register()
 *
 * @listener: an object containing the callbacks to be removed
 */
void memory_listener_unregister(MemoryListener *listener);

/**
 * memory_global_dirty_log_start: begin dirty logging for all regions
 */
void memory_global_dirty_log_start(void);

/**
 * memory_global_dirty_log_stop: end dirty logging for all regions
 */
void memory_global_dirty_log_stop(void);

void mtree_info(fprintf_function mon_printf, void *f);

/**
 * address_space_init: initializes an address space
 *
 * @as: an uninitialized #AddressSpace
 * @root: a #MemoryRegion that routes addesses for the address space
 * @name: an address space name.  The name is only used for debugging
 *        output.
 */
void address_space_init(AddressSpace *as, MemoryRegion *root, const char *name);


/**
 * address_space_destroy: destroy an address space
 *
 * Releases all resources associated with an address space.  After an address space
 * is destroyed, its root memory region (given by address_space_init()) may be destroyed
 * as well.
 *
 * @as: address space to be destroyed
 */
void address_space_destroy(AddressSpace *as);

/**
 * address_space_rw: read from or write to an address space.
 *
 * Return true if the operation hit any unassigned memory or encountered an
 * IOMMU fault.
 *
 * @as: #AddressSpace to be accessed
 * @addr: address within that address space
 * @buf: buffer with the data transferred
 * @is_write: indicates the transfer direction
 */
bool address_space_rw(AddressSpace *as, hwaddr addr, uint8_t *buf,
                      int len, bool is_write);

/**
 * address_space_write: write to address space.
 *
 * Return true if the operation hit any unassigned memory or encountered an
 * IOMMU fault.
 *
 * @as: #AddressSpace to be accessed
 * @addr: address within that address space
 * @buf: buffer with the data transferred
 */
bool address_space_write(AddressSpace *as, hwaddr addr,
                         const uint8_t *buf, int len);

/**
 * address_space_read: read from an address space.
 *
 * Return true if the operation hit any unassigned memory or encountered an
 * IOMMU fault.
 *
 * @as: #AddressSpace to be accessed
 * @addr: address within that address space
 * @buf: buffer with the data transferred
 */
bool address_space_read(AddressSpace *as, hwaddr addr, uint8_t *buf, int len);

/* address_space_translate: translate an address range into an address space
 * into a MemoryRegion and an address range into that section
 *
 * @as: #AddressSpace to be accessed
 * @addr: address within that address space
 * @xlat: pointer to address within the returned memory region section's
 * #MemoryRegion.
 * @len: pointer to length
 * @is_write: indicates the transfer direction
 */
MemoryRegion *address_space_translate(AddressSpace *as, hwaddr addr,
                                      hwaddr *xlat, hwaddr *len,
                                      bool is_write);

/* address_space_access_valid: check for validity of accessing an address
 * space range
 *
 * Check whether memory is assigned to the given address space range, and
 * access is permitted by any IOMMU regions that are active for the address
 * space.
 *
 * For now, addr and len should be aligned to a page size.  This limitation
 * will be lifted in the future.
 *
 * @as: #AddressSpace to be accessed
 * @addr: address within that address space
 * @len: length of the area to be checked
 * @is_write: indicates the transfer direction
 */
bool address_space_access_valid(AddressSpace *as, hwaddr addr, int len, bool is_write);

/* address_space_map: map a physical memory region into a host virtual address
 *
 * May map a subset of the requested range, given by and returned in @plen.
 * May return %NULL if resources needed to perform the mapping are exhausted.
 * Use only for reads OR writes - not for read-modify-write operations.
 * Use cpu_register_map_client() to know when retrying the map operation is
 * likely to succeed.
 *
 * @as: #AddressSpace to be accessed
 * @addr: address within that address space
 * @plen: pointer to length of buffer; updated on return
 * @is_write: indicates the transfer direction
 */
void *address_space_map(AddressSpace *as, hwaddr addr,
                        hwaddr *plen, bool is_write);

/* address_space_unmap: Unmaps a memory region previously mapped by address_space_map()
 *
 * Will also mark the memory as dirty if @is_write == %true.  @access_len gives
 * the amount of memory that was actually read or written by the caller.
 *
 * @as: #AddressSpace used
 * @addr: address within that address space
 * @len: buffer length as returned by address_space_map()
 * @access_len: amount of data actually transferred
 * @is_write: indicates the transfer direction
 */
void address_space_unmap(AddressSpace *as, void *buffer, hwaddr len,
                         int is_write, hwaddr access_len);


#endif

#endif
