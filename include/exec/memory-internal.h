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
#include "hw/xen/xen.h"


typedef struct AddressSpaceDispatch AddressSpaceDispatch;

void address_space_init_dispatch(AddressSpace *as);
void address_space_destroy_dispatch(AddressSpace *as);

extern const MemoryRegionOps unassigned_mem_ops;

bool memory_region_access_valid(MemoryRegion *mr, hwaddr addr,
                                unsigned size, bool is_write);

ram_addr_t qemu_ram_alloc_from_ptr(ram_addr_t size, void *host,
                                   MemoryRegion *mr);
ram_addr_t qemu_ram_alloc(ram_addr_t size, MemoryRegion *mr);
void *qemu_get_ram_ptr(ram_addr_t addr);
void qemu_ram_free(ram_addr_t addr);
void qemu_ram_free_from_ptr(ram_addr_t addr);

static inline bool cpu_physical_memory_get_dirty_flag(ram_addr_t addr,
                                                      unsigned client)
{
    assert(client < DIRTY_MEMORY_NUM);
    return test_bit(addr >> TARGET_PAGE_BITS, ram_list.dirty_memory[client]);
}

/* read dirty bit (return 0 or 1) */
static inline bool cpu_physical_memory_is_dirty(ram_addr_t addr)
{
    bool vga = cpu_physical_memory_get_dirty_flag(addr, DIRTY_MEMORY_VGA);
    bool code = cpu_physical_memory_get_dirty_flag(addr, DIRTY_MEMORY_CODE);
    bool migration =
        cpu_physical_memory_get_dirty_flag(addr, DIRTY_MEMORY_MIGRATION);
    return vga && code && migration;
}

static inline int cpu_physical_memory_get_dirty(ram_addr_t start,
                                                ram_addr_t length,
                                                unsigned client)
{
    int ret = 0;
    ram_addr_t addr, end;

    end = TARGET_PAGE_ALIGN(start + length);
    start &= TARGET_PAGE_MASK;
    for (addr = start; addr < end; addr += TARGET_PAGE_SIZE) {
        ret |= cpu_physical_memory_get_dirty_flag(addr, client);
    }
    return ret;
}

static inline void cpu_physical_memory_set_dirty_flag(ram_addr_t addr,
                                                      unsigned client)
{
    assert(client < DIRTY_MEMORY_NUM);
    set_bit(addr >> TARGET_PAGE_BITS, ram_list.dirty_memory[client]);
}

static inline void cpu_physical_memory_set_dirty(ram_addr_t addr)
{
    cpu_physical_memory_set_dirty_flag(addr, DIRTY_MEMORY_MIGRATION);
    cpu_physical_memory_set_dirty_flag(addr, DIRTY_MEMORY_VGA);
    cpu_physical_memory_set_dirty_flag(addr, DIRTY_MEMORY_CODE);
}

static inline void cpu_physical_memory_set_dirty_range(ram_addr_t start,
                                                       ram_addr_t length)
{
    ram_addr_t addr, end;

    end = TARGET_PAGE_ALIGN(start + length);
    start &= TARGET_PAGE_MASK;
    for (addr = start; addr < end; addr += TARGET_PAGE_SIZE) {
        cpu_physical_memory_set_dirty(addr);
    }
    xen_modified_memory(addr, length);
}

static inline void cpu_physical_memory_mask_dirty_range(ram_addr_t start,
                                                        ram_addr_t length,
                                                        unsigned client)
{
    ram_addr_t addr, end;

    assert(client < DIRTY_MEMORY_NUM);
    end = TARGET_PAGE_ALIGN(start + length);
    start &= TARGET_PAGE_MASK;
    for (addr = start; addr < end; addr += TARGET_PAGE_SIZE) {
        clear_bit(addr >> TARGET_PAGE_BITS, ram_list.dirty_memory[client]);
    }
}

void cpu_physical_memory_reset_dirty(ram_addr_t start, ram_addr_t end,
                                     unsigned client);

#endif

#endif
