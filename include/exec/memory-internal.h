/*
 * Declarations for obsolete exec.c functions
 *
 * Copyright 2011 Red Hat, Inc. and/or its affiliates
 *
 * Authors:
 *  Avi Kivity <avi@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 *
 */

/*
 * This header is for use by exec.c and memory.c ONLY.  Do not include it.
 * The functions declared here will be removed soon.
 */

#ifndef MEMORY_INTERNAL_H
#define MEMORY_INTERNAL_H

#ifndef CONFIG_USER_ONLY
#include "hw/xen.h"

typedef struct PhysPageEntry PhysPageEntry;

struct PhysPageEntry {
    uint16_t is_leaf : 1;
     /* index into phys_sections (is_leaf) or phys_map_nodes (!is_leaf) */
    uint16_t ptr : 15;
};

typedef struct AddressSpaceDispatch AddressSpaceDispatch;

struct AddressSpaceDispatch {
    /* This is a multi-level map on the physical address space.
     * The bottom level has pointers to MemoryRegionSections.
     */
    PhysPageEntry phys_map;
    MemoryListener listener;
};

void address_space_init_dispatch(AddressSpace *as);
void address_space_destroy_dispatch(AddressSpace *as);

ram_addr_t qemu_ram_alloc_from_ptr(ram_addr_t size, void *host,
                                   MemoryRegion *mr);
ram_addr_t qemu_ram_alloc(ram_addr_t size, MemoryRegion *mr);
void qemu_ram_free(ram_addr_t addr);
void qemu_ram_free_from_ptr(ram_addr_t addr);

struct MemoryRegion;
struct MemoryRegionSection;

void qemu_register_coalesced_mmio(hwaddr addr, ram_addr_t size);
void qemu_unregister_coalesced_mmio(hwaddr addr, ram_addr_t size);

#define VGA_DIRTY_FLAG       0x01
#define CODE_DIRTY_FLAG      0x02
#define MIGRATION_DIRTY_FLAG 0x08

static inline int cpu_physical_memory_get_dirty_flags(ram_addr_t addr)
{
    return ram_list.phys_dirty[addr >> TARGET_PAGE_BITS];
}

/* read dirty bit (return 0 or 1) */
static inline int cpu_physical_memory_is_dirty(ram_addr_t addr)
{
    return cpu_physical_memory_get_dirty_flags(addr) == 0xff;
}

static inline int cpu_physical_memory_get_dirty(ram_addr_t start,
                                                ram_addr_t length,
                                                int dirty_flags)
{
    int ret = 0;
    ram_addr_t addr, end;

    end = TARGET_PAGE_ALIGN(start + length);
    start &= TARGET_PAGE_MASK;
    for (addr = start; addr < end; addr += TARGET_PAGE_SIZE) {
        ret |= cpu_physical_memory_get_dirty_flags(addr) & dirty_flags;
    }
    return ret;
}

static inline int cpu_physical_memory_set_dirty_flags(ram_addr_t addr,
                                                      int dirty_flags)
{
    return ram_list.phys_dirty[addr >> TARGET_PAGE_BITS] |= dirty_flags;
}

static inline void cpu_physical_memory_set_dirty(ram_addr_t addr)
{
    cpu_physical_memory_set_dirty_flags(addr, 0xff);
}

static inline int cpu_physical_memory_clear_dirty_flags(ram_addr_t addr,
                                                        int dirty_flags)
{
    int mask = ~dirty_flags;

    return ram_list.phys_dirty[addr >> TARGET_PAGE_BITS] &= mask;
}

static inline void cpu_physical_memory_set_dirty_range(ram_addr_t start,
                                                       ram_addr_t length,
                                                       int dirty_flags)
{
    ram_addr_t addr, end;

    end = TARGET_PAGE_ALIGN(start + length);
    start &= TARGET_PAGE_MASK;
    for (addr = start; addr < end; addr += TARGET_PAGE_SIZE) {
        cpu_physical_memory_set_dirty_flags(addr, dirty_flags);
    }
    xen_modified_memory(addr, length);
}

static inline void cpu_physical_memory_mask_dirty_range(ram_addr_t start,
                                                        ram_addr_t length,
                                                        int dirty_flags)
{
    ram_addr_t addr, end;

    end = TARGET_PAGE_ALIGN(start + length);
    start &= TARGET_PAGE_MASK;
    for (addr = start; addr < end; addr += TARGET_PAGE_SIZE) {
        cpu_physical_memory_clear_dirty_flags(addr, dirty_flags);
    }
}

void cpu_physical_memory_reset_dirty(ram_addr_t start, ram_addr_t end,
                                     int dirty_flags);

extern const IORangeOps memory_region_iorange_ops;

#endif

#endif
