/*
 * Declarations for cpu physical memory functions
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

#ifndef RAM_ADDR_H
#define RAM_ADDR_H

#ifndef CONFIG_USER_ONLY
#include "hw/xen/xen.h"

ram_addr_t qemu_ram_alloc_from_file(ram_addr_t size, MemoryRegion *mr,
                                    bool share, const char *mem_path,
                                    Error **errp);
ram_addr_t qemu_ram_alloc_from_ptr(ram_addr_t size, void *host,
                                   MemoryRegion *mr, Error **errp);
ram_addr_t qemu_ram_alloc(ram_addr_t size, MemoryRegion *mr, Error **errp);
ram_addr_t qemu_ram_alloc_resizeable(ram_addr_t size, ram_addr_t max_size,
                                     void (*resized)(const char*,
                                                     uint64_t length,
                                                     void *host),
                                     MemoryRegion *mr, Error **errp);
int qemu_get_ram_fd(ram_addr_t addr);
void *qemu_get_ram_block_host_ptr(ram_addr_t addr);
void *qemu_get_ram_ptr(ram_addr_t addr);
void qemu_ram_free(ram_addr_t addr);
void qemu_ram_free_from_ptr(ram_addr_t addr);

int qemu_ram_resize(ram_addr_t base, ram_addr_t newsize, Error **errp);

#define DIRTY_CLIENTS_ALL     ((1 << DIRTY_MEMORY_NUM) - 1)
#define DIRTY_CLIENTS_NOCODE  (DIRTY_CLIENTS_ALL & ~(1 << DIRTY_MEMORY_CODE))

static inline bool cpu_physical_memory_get_dirty(ram_addr_t start,
                                                 ram_addr_t length,
                                                 unsigned client)
{
    unsigned long end, page, next;

    assert(client < DIRTY_MEMORY_NUM);

    end = TARGET_PAGE_ALIGN(start + length) >> TARGET_PAGE_BITS;
    page = start >> TARGET_PAGE_BITS;
    next = find_next_bit(ram_list.dirty_memory[client], end, page);

    return next < end;
}

static inline bool cpu_physical_memory_all_dirty(ram_addr_t start,
                                                 ram_addr_t length,
                                                 unsigned client)
{
    unsigned long end, page, next;

    assert(client < DIRTY_MEMORY_NUM);

    end = TARGET_PAGE_ALIGN(start + length) >> TARGET_PAGE_BITS;
    page = start >> TARGET_PAGE_BITS;
    next = find_next_zero_bit(ram_list.dirty_memory[client], end, page);

    return next >= end;
}

static inline bool cpu_physical_memory_get_dirty_flag(ram_addr_t addr,
                                                      unsigned client)
{
    return cpu_physical_memory_get_dirty(addr, 1, client);
}

static inline bool cpu_physical_memory_is_clean(ram_addr_t addr)
{
    bool vga = cpu_physical_memory_get_dirty_flag(addr, DIRTY_MEMORY_VGA);
    bool code = cpu_physical_memory_get_dirty_flag(addr, DIRTY_MEMORY_CODE);
    bool migration =
        cpu_physical_memory_get_dirty_flag(addr, DIRTY_MEMORY_MIGRATION);
    return !(vga && code && migration);
}

static inline uint8_t cpu_physical_memory_range_includes_clean(ram_addr_t start,
                                                               ram_addr_t length,
                                                               uint8_t mask)
{
    uint8_t ret = 0;

    if (mask & (1 << DIRTY_MEMORY_VGA) &&
        !cpu_physical_memory_all_dirty(start, length, DIRTY_MEMORY_VGA)) {
        ret |= (1 << DIRTY_MEMORY_VGA);
    }
    if (mask & (1 << DIRTY_MEMORY_CODE) &&
        !cpu_physical_memory_all_dirty(start, length, DIRTY_MEMORY_CODE)) {
        ret |= (1 << DIRTY_MEMORY_CODE);
    }
    if (mask & (1 << DIRTY_MEMORY_MIGRATION) &&
        !cpu_physical_memory_all_dirty(start, length, DIRTY_MEMORY_MIGRATION)) {
        ret |= (1 << DIRTY_MEMORY_MIGRATION);
    }
    return ret;
}

static inline void cpu_physical_memory_set_dirty_flag(ram_addr_t addr,
                                                      unsigned client)
{
    assert(client < DIRTY_MEMORY_NUM);
    set_bit_atomic(addr >> TARGET_PAGE_BITS, ram_list.dirty_memory[client]);
}

static inline void cpu_physical_memory_set_dirty_range(ram_addr_t start,
                                                       ram_addr_t length,
                                                       uint8_t mask)
{
    unsigned long end, page;
    unsigned long **d = ram_list.dirty_memory;

    end = TARGET_PAGE_ALIGN(start + length) >> TARGET_PAGE_BITS;
    page = start >> TARGET_PAGE_BITS;
    if (likely(mask & (1 << DIRTY_MEMORY_MIGRATION))) {
        bitmap_set_atomic(d[DIRTY_MEMORY_MIGRATION], page, end - page);
    }
    if (unlikely(mask & (1 << DIRTY_MEMORY_VGA))) {
        bitmap_set_atomic(d[DIRTY_MEMORY_VGA], page, end - page);
    }
    if (unlikely(mask & (1 << DIRTY_MEMORY_CODE))) {
        bitmap_set_atomic(d[DIRTY_MEMORY_CODE], page, end - page);
    }
    xen_modified_memory(start, length);
}

#if !defined(_WIN32)
static inline void cpu_physical_memory_set_dirty_lebitmap(unsigned long *bitmap,
                                                          ram_addr_t start,
                                                          ram_addr_t pages)
{
    unsigned long i, j;
    unsigned long page_number, c;
    hwaddr addr;
    ram_addr_t ram_addr;
    unsigned long len = (pages + HOST_LONG_BITS - 1) / HOST_LONG_BITS;
    unsigned long hpratio = getpagesize() / TARGET_PAGE_SIZE;
    unsigned long page = BIT_WORD(start >> TARGET_PAGE_BITS);

    /* start address is aligned at the start of a word? */
    if ((((page * BITS_PER_LONG) << TARGET_PAGE_BITS) == start) &&
        (hpratio == 1)) {
        long k;
        long nr = BITS_TO_LONGS(pages);

        for (k = 0; k < nr; k++) {
            if (bitmap[k]) {
                unsigned long temp = leul_to_cpu(bitmap[k]);
                unsigned long **d = ram_list.dirty_memory;

                atomic_or(&d[DIRTY_MEMORY_MIGRATION][page + k], temp);
                atomic_or(&d[DIRTY_MEMORY_VGA][page + k], temp);
                if (tcg_enabled()) {
                    atomic_or(&d[DIRTY_MEMORY_CODE][page + k], temp);
                }
            }
        }
        xen_modified_memory(start, pages << TARGET_PAGE_BITS);
    } else {
        uint8_t clients = tcg_enabled() ? DIRTY_CLIENTS_ALL : DIRTY_CLIENTS_NOCODE;
        /*
         * bitmap-traveling is faster than memory-traveling (for addr...)
         * especially when most of the memory is not dirty.
         */
        for (i = 0; i < len; i++) {
            if (bitmap[i] != 0) {
                c = leul_to_cpu(bitmap[i]);
                do {
                    j = ctzl(c);
                    c &= ~(1ul << j);
                    page_number = (i * HOST_LONG_BITS + j) * hpratio;
                    addr = page_number * TARGET_PAGE_SIZE;
                    ram_addr = start + addr;
                    cpu_physical_memory_set_dirty_range(ram_addr,
                                       TARGET_PAGE_SIZE * hpratio, clients);
                } while (c != 0);
            }
        }
    }
}
#endif /* not _WIN32 */

bool cpu_physical_memory_test_and_clear_dirty(ram_addr_t start,
                                              ram_addr_t length,
                                              unsigned client);

static inline void cpu_physical_memory_clear_dirty_range(ram_addr_t start,
                                                         ram_addr_t length)
{
    cpu_physical_memory_test_and_clear_dirty(start, length, DIRTY_MEMORY_MIGRATION);
    cpu_physical_memory_test_and_clear_dirty(start, length, DIRTY_MEMORY_VGA);
    cpu_physical_memory_test_and_clear_dirty(start, length, DIRTY_MEMORY_CODE);
}


static inline
uint64_t cpu_physical_memory_sync_dirty_bitmap(unsigned long *dest,
                                               ram_addr_t start,
                                               ram_addr_t length)
{
    ram_addr_t addr;
    unsigned long page = BIT_WORD(start >> TARGET_PAGE_BITS);
    uint64_t num_dirty = 0;

    /* start address is aligned at the start of a word? */
    if (((page * BITS_PER_LONG) << TARGET_PAGE_BITS) == start) {
        int k;
        int nr = BITS_TO_LONGS(length >> TARGET_PAGE_BITS);
        unsigned long *src = ram_list.dirty_memory[DIRTY_MEMORY_MIGRATION];

        for (k = page; k < page + nr; k++) {
            if (src[k]) {
                unsigned long bits = atomic_xchg(&src[k], 0);
                unsigned long new_dirty;
                new_dirty = ~dest[k];
                dest[k] |= bits;
                new_dirty &= bits;
                num_dirty += ctpopl(new_dirty);
            }
        }
    } else {
        for (addr = 0; addr < length; addr += TARGET_PAGE_SIZE) {
            if (cpu_physical_memory_test_and_clear_dirty(
                        start + addr,
                        TARGET_PAGE_SIZE,
                        DIRTY_MEMORY_MIGRATION)) {
                long k = (start + addr) >> TARGET_PAGE_BITS;
                if (!test_and_set_bit(k, dest)) {
                    num_dirty++;
                }
            }
        }
    }

    return num_dirty;
}

#endif
#endif
