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

#ifndef EXEC_OBSOLETE_H
#define EXEC_OBSOLETE_H

#ifndef WANT_EXEC_OBSOLETE
#error Do not include exec-obsolete.h
#endif

#ifndef CONFIG_USER_ONLY

ram_addr_t qemu_ram_alloc_from_ptr(ram_addr_t size, void *host,
                                   MemoryRegion *mr);
ram_addr_t qemu_ram_alloc(ram_addr_t size, MemoryRegion *mr);
void qemu_ram_free(ram_addr_t addr);
void qemu_ram_free_from_ptr(ram_addr_t addr);

struct MemoryRegion;
struct MemoryRegionSection;
void cpu_register_physical_memory_log(struct MemoryRegionSection *section,
                                      bool readonly);

void qemu_register_coalesced_mmio(target_phys_addr_t addr, ram_addr_t size);
void qemu_unregister_coalesced_mmio(target_phys_addr_t addr, ram_addr_t size);

int cpu_physical_memory_set_dirty_tracking(int enable);

#define VGA_DIRTY_FLAG       0x01
#define CODE_DIRTY_FLAG      0x02
#define MIGRATION_DIRTY_FLAG 0x08

/* read dirty bit (return 0 or 1) */
static inline int cpu_physical_memory_is_dirty(ram_addr_t addr)
{
    return ram_list.phys_dirty[addr >> TARGET_PAGE_BITS] == 0xff;
}

static inline int cpu_physical_memory_get_dirty_flags(ram_addr_t addr)
{
    return ram_list.phys_dirty[addr >> TARGET_PAGE_BITS];
}

static inline int cpu_physical_memory_get_dirty(ram_addr_t start,
                                                ram_addr_t length,
                                                int dirty_flags)
{
    int ret = 0;
    uint8_t *p;
    ram_addr_t addr, end;

    end = TARGET_PAGE_ALIGN(start + length);
    start &= TARGET_PAGE_MASK;
    p = ram_list.phys_dirty + (start >> TARGET_PAGE_BITS);
    for (addr = start; addr < end; addr += TARGET_PAGE_SIZE) {
        ret |= *p++ & dirty_flags;
    }
    return ret;
}

static inline void cpu_physical_memory_set_dirty(ram_addr_t addr)
{
    ram_list.phys_dirty[addr >> TARGET_PAGE_BITS] = 0xff;
}

static inline int cpu_physical_memory_set_dirty_flags(ram_addr_t addr,
                                                      int dirty_flags)
{
    return ram_list.phys_dirty[addr >> TARGET_PAGE_BITS] |= dirty_flags;
}

static inline void cpu_physical_memory_set_dirty_range(ram_addr_t start,
                                                       ram_addr_t length,
                                                       int dirty_flags)
{
    uint8_t *p;
    ram_addr_t addr, end;

    end = TARGET_PAGE_ALIGN(start + length);
    start &= TARGET_PAGE_MASK;
    p = ram_list.phys_dirty + (start >> TARGET_PAGE_BITS);
    for (addr = start; addr < end; addr += TARGET_PAGE_SIZE) {
        *p++ |= dirty_flags;
    }
}

static inline void cpu_physical_memory_mask_dirty_range(ram_addr_t start,
                                                        ram_addr_t length,
                                                        int dirty_flags)
{
    int mask;
    uint8_t *p;
    ram_addr_t addr, end;

    end = TARGET_PAGE_ALIGN(start + length);
    start &= TARGET_PAGE_MASK;
    mask = ~dirty_flags;
    p = ram_list.phys_dirty + (start >> TARGET_PAGE_BITS);
    for (addr = start; addr < end; addr += TARGET_PAGE_SIZE) {
        *p++ &= mask;
    }
}

void cpu_physical_memory_reset_dirty(ram_addr_t start, ram_addr_t end,
                                     int dirty_flags);

extern const IORangeOps memory_region_iorange_ops;

#endif

#endif
