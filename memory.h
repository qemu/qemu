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

#include <stdint.h>
#include <stdbool.h>
#include "qemu-common.h"
#include "cpu-common.h"
#include "targphys.h"
#include "qemu-queue.h"
#include "iorange.h"
#include "ioport.h"
#include "int128.h"

typedef struct MemoryRegionOps MemoryRegionOps;
typedef struct MemoryRegion MemoryRegion;
typedef struct MemoryRegionPortio MemoryRegionPortio;
typedef struct MemoryRegionMmio MemoryRegionMmio;

/* Must match *_DIRTY_FLAGS in cpu-all.h.  To be replaced with dynamic
 * registration.
 */
#define DIRTY_MEMORY_VGA       0
#define DIRTY_MEMORY_CODE      1
#define DIRTY_MEMORY_MIGRATION 3

struct MemoryRegionMmio {
    CPUReadMemoryFunc *read[3];
    CPUWriteMemoryFunc *write[3];
};

/* Internal use; thunks between old-style IORange and MemoryRegions. */
typedef struct MemoryRegionIORange MemoryRegionIORange;
struct MemoryRegionIORange {
    IORange iorange;
    MemoryRegion *mr;
    target_phys_addr_t offset;
};

/*
 * Memory region callbacks
 */
struct MemoryRegionOps {
    /* Read from the memory region. @addr is relative to @mr; @size is
     * in bytes. */
    uint64_t (*read)(void *opaque,
                     target_phys_addr_t addr,
                     unsigned size);
    /* Write to the memory region. @addr is relative to @mr; @size is
     * in bytes. */
    void (*write)(void *opaque,
                  target_phys_addr_t addr,
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
        bool (*accepts)(void *opaque, target_phys_addr_t addr,
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

    /* If .read and .write are not present, old_portio may be used for
     * backwards compatibility with old portio registration
     */
    const MemoryRegionPortio *old_portio;
    /* If .read and .write are not present, old_mmio may be used for
     * backwards compatibility with old mmio registration
     */
    const MemoryRegionMmio old_mmio;
};

typedef struct CoalescedMemoryRange CoalescedMemoryRange;
typedef struct MemoryRegionIoeventfd MemoryRegionIoeventfd;

struct MemoryRegion {
    /* All fields are private - violators will be prosecuted */
    const MemoryRegionOps *ops;
    void *opaque;
    MemoryRegion *parent;
    Int128 size;
    target_phys_addr_t addr;
    void (*destructor)(MemoryRegion *mr);
    ram_addr_t ram_addr;
    bool subpage;
    bool terminates;
    bool readable;
    bool ram;
    bool readonly; /* For RAM regions */
    bool enabled;
    bool rom_device;
    bool warning_printed; /* For reservations */
    MemoryRegion *alias;
    target_phys_addr_t alias_offset;
    unsigned priority;
    bool may_overlap;
    QTAILQ_HEAD(subregions, MemoryRegion) subregions;
    QTAILQ_ENTRY(MemoryRegion) subregions_link;
    QTAILQ_HEAD(coalesced_ranges, CoalescedMemoryRange) coalesced;
    const char *name;
    uint8_t dirty_log_mask;
    unsigned ioeventfd_nb;
    MemoryRegionIoeventfd *ioeventfds;
};

struct MemoryRegionPortio {
    uint32_t offset;
    uint32_t len;
    unsigned size;
    IOPortReadFunc *read;
    IOPortWriteFunc *write;
};

#define PORTIO_END_OF_LIST() { }

typedef struct MemoryRegionSection MemoryRegionSection;

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
    MemoryRegion *address_space;
    target_phys_addr_t offset_within_region;
    uint64_t size;
    target_phys_addr_t offset_within_address_space;
    bool readonly;
};

typedef struct MemoryListener MemoryListener;

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
                        bool match_data, uint64_t data, int fd);
    void (*eventfd_del)(MemoryListener *listener, MemoryRegionSection *section,
                        bool match_data, uint64_t data, int fd);
    /* Lower = earlier (during add), later (during del) */
    unsigned priority;
    MemoryRegion *address_space_filter;
    QTAILQ_ENTRY(MemoryListener) link;
};

/**
 * memory_region_init: Initialize a memory region
 *
 * The region typically acts as a container for other memory regions.  Use
 * memory_region_add_subregion() to add subregions.
 *
 * @mr: the #MemoryRegion to be initialized
 * @name: used for debugging; not visible to the user or ABI
 * @size: size of the region; any subregions beyond this size will be clipped
 */
void memory_region_init(MemoryRegion *mr,
                        const char *name,
                        uint64_t size);
/**
 * memory_region_init_io: Initialize an I/O memory region.
 *
 * Accesses into the region will cause the callbacks in @ops to be called.
 * if @size is nonzero, subregions will be clipped to @size.
 *
 * @mr: the #MemoryRegion to be initialized.
 * @ops: a structure containing read and write callbacks to be used when
 *       I/O is performed on the region.
 * @opaque: passed to to the read and write callbacks of the @ops structure.
 * @name: used for debugging; not visible to the user or ABI
 * @size: size of the region.
 */
void memory_region_init_io(MemoryRegion *mr,
                           const MemoryRegionOps *ops,
                           void *opaque,
                           const char *name,
                           uint64_t size);

/**
 * memory_region_init_ram:  Initialize RAM memory region.  Accesses into the
 *                          region will modify memory directly.
 *
 * @mr: the #MemoryRegion to be initialized.
 * @name: the name of the region.
 * @size: size of the region.
 */
void memory_region_init_ram(MemoryRegion *mr,
                            const char *name,
                            uint64_t size);

/**
 * memory_region_init_ram:  Initialize RAM memory region from a user-provided.
 *                          pointer.  Accesses into the region will modify
 *                          memory directly.
 *
 * @mr: the #MemoryRegion to be initialized.
 * @name: the name of the region.
 * @size: size of the region.
 * @ptr: memory to be mapped; must contain at least @size bytes.
 */
void memory_region_init_ram_ptr(MemoryRegion *mr,
                                const char *name,
                                uint64_t size,
                                void *ptr);

/**
 * memory_region_init_alias: Initialize a memory region that aliases all or a
 *                           part of another memory region.
 *
 * @mr: the #MemoryRegion to be initialized.
 * @name: used for debugging; not visible to the user or ABI
 * @orig: the region to be referenced; @mr will be equivalent to
 *        @orig between @offset and @offset + @size - 1.
 * @offset: start of the section in @orig to be referenced.
 * @size: size of the region.
 */
void memory_region_init_alias(MemoryRegion *mr,
                              const char *name,
                              MemoryRegion *orig,
                              target_phys_addr_t offset,
                              uint64_t size);

/**
 * memory_region_init_rom_device:  Initialize a ROM memory region.  Writes are
 *                                 handled via callbacks.
 *
 * @mr: the #MemoryRegion to be initialized.
 * @ops: callbacks for write access handling.
 * @name: the name of the region.
 * @size: size of the region.
 */
void memory_region_init_rom_device(MemoryRegion *mr,
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
 * @name: used for debugging; not visible to the user or ABI
 * @size: size of the region.
 */
void memory_region_init_reservation(MemoryRegion *mr,
                                    const char *name,
                                    uint64_t size);
/**
 * memory_region_destroy: Destroy a memory region and reclaim all resources.
 *
 * @mr: the region to be destroyed.  May not currently be a subregion
 *      (see memory_region_add_subregion()) or referenced in an alias
 *      (see memory_region_init_alias()).
 */
void memory_region_destroy(MemoryRegion *mr);

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
 * memory_region_name: get a memory region's name
 *
 * Returns the string that was used to initialize the memory region.
 *
 * @mr: the memory region being queried
 */
const char *memory_region_name(MemoryRegion *mr);

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
bool memory_region_get_dirty(MemoryRegion *mr, target_phys_addr_t addr,
                             target_phys_addr_t size, unsigned client);

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
void memory_region_set_dirty(MemoryRegion *mr, target_phys_addr_t addr,
                             target_phys_addr_t size);

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
void memory_region_reset_dirty(MemoryRegion *mr, target_phys_addr_t addr,
                               target_phys_addr_t size, unsigned client);

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
 * memory_region_rom_device_set_readable: enable/disable ROM readability
 *
 * Allows a ROM device (initialized with memory_region_init_rom_device() to
 * to be marked as readable (default) or not readable.  When it is readable,
 * the device is mapped to guest memory.  When not readable, reads are
 * forwarded to the #MemoryRegion.read function.
 *
 * @mr: the memory region to be updated
 * @readable: whether reads are satisified directly (%true) or via callbacks
 *            (%false)
 */
void memory_region_rom_device_set_readable(MemoryRegion *mr, bool readable);

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
                                  target_phys_addr_t offset,
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
                               target_phys_addr_t addr,
                               unsigned size,
                               bool match_data,
                               uint64_t data,
                               int fd);

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
                               target_phys_addr_t addr,
                               unsigned size,
                               bool match_data,
                               uint64_t data,
                               int fd);
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
                                 target_phys_addr_t offset,
                                 MemoryRegion *subregion);
/**
 * memory_region_add_subregion: Add a subregion to a container, with overlap.
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
                                         target_phys_addr_t offset,
                                         MemoryRegion *subregion,
                                         unsigned priority);

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
 * Dynamically updates the address of a region, relative to its parent.
 * May be used on regions are currently part of a memory hierarchy.
 *
 * @mr: the region to be updated
 * @addr: new address, relative to parent region
 */
void memory_region_set_address(MemoryRegion *mr, target_phys_addr_t addr);

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
                                    target_phys_addr_t offset);

/**
 * memory_region_find: locate a MemoryRegion in an address space
 *
 * Locates the first #MemoryRegion within an address space given by
 * @address_space that overlaps the range given by @addr and @size.
 *
 * Returns a #MemoryRegionSection that describes a contiguous overlap.
 * It will have the following characteristics:
 *    .@offset_within_address_space >= @addr
 *    .@offset_within_address_space + .@size <= @addr + @size
 *    .@size = 0 iff no overlap was found
 *    .@mr is non-%NULL iff an overlap was found
 *
 * @address_space: a top-level (i.e. parentless) region that contains
 *       the region to be found
 * @addr: start of the area within @address_space to be searched
 * @size: size of the area to be searched
 */
MemoryRegionSection memory_region_find(MemoryRegion *address_space,
                                       target_phys_addr_t addr, uint64_t size);


/**
 * memory_global_sync_dirty_bitmap: synchronize the dirty log for all memory
 *
 * Synchronizes the dirty page log for an entire address space.
 * @address_space: a top-level (i.e. parentless) region that contains the
 *       memory being synchronized
 */
void memory_global_sync_dirty_bitmap(MemoryRegion *address_space);

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
void memory_listener_register(MemoryListener *listener, MemoryRegion *filter);

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
 * memory_global_dirty_log_stop: begin dirty logging for all regions
 */
void memory_global_dirty_log_stop(void);

void mtree_info(fprintf_function mon_printf, void *f);

#endif

#endif
