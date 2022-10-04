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
#include "cpu.h"
#include "sysemu/xen.h"
#include "sysemu/tcg.h"
#include "exec/ramlist.h"
#include "exec/ramblock.h"

extern uint64_t total_dirty_pages;

/**
 * clear_bmap_size: calculate clear bitmap size
 *
 * @pages: number of guest pages
 * @shift: guest page number shift
 *
 * Returns: number of bits for the clear bitmap
 */
static inline long clear_bmap_size(uint64_t pages, uint8_t shift)
{
    return DIV_ROUND_UP(pages, 1UL << shift);
}

/**
 * clear_bmap_set: set clear bitmap for the page range.  Must be with
 * bitmap_mutex held.
 *
 * @rb: the ramblock to operate on
 * @start: the start page number
 * @size: number of pages to set in the bitmap
 *
 * Returns: None
 */
static inline void clear_bmap_set(RAMBlock *rb, uint64_t start,
                                  uint64_t npages)
{
    uint8_t shift = rb->clear_bmap_shift;

    bitmap_set(rb->clear_bmap, start >> shift, clear_bmap_size(npages, shift));
}

/**
 * clear_bmap_test_and_clear: test clear bitmap for the page, clear if set.
 * Must be with bitmap_mutex held.
 *
 * @rb: the ramblock to operate on
 * @page: the page number to check
 *
 * Returns: true if the bit was set, false otherwise
 */
static inline bool clear_bmap_test_and_clear(RAMBlock *rb, uint64_t page)
{
    uint8_t shift = rb->clear_bmap_shift;

    return bitmap_test_and_clear(rb->clear_bmap, page >> shift, 1);
}

static inline bool offset_in_ramblock(RAMBlock *b, ram_addr_t offset)
{
    return (b && b->host && offset < b->used_length) ? true : false;
}

static inline void *ramblock_ptr(RAMBlock *block, ram_addr_t offset)
{
    assert(offset_in_ramblock(block, offset));
    return (char *)block->host + offset;
}

static inline unsigned long int ramblock_recv_bitmap_offset(void *host_addr,
                                                            RAMBlock *rb)
{
    uint64_t host_addr_offset =
            (uint64_t)(uintptr_t)(host_addr - (void *)rb->host);
    return host_addr_offset >> TARGET_PAGE_BITS;
}

bool ramblock_is_pmem(RAMBlock *rb);

long qemu_minrampagesize(void);
long qemu_maxrampagesize(void);

/**
 * qemu_ram_alloc_from_file,
 * qemu_ram_alloc_from_fd:  Allocate a ram block from the specified backing
 *                          file or device
 *
 * Parameters:
 *  @size: the size in bytes of the ram block
 *  @mr: the memory region where the ram block is
 *  @ram_flags: RamBlock flags. Supported flags: RAM_SHARED, RAM_PMEM,
 *              RAM_NORESERVE.
 *  @mem_path or @fd: specify the backing file or device
 *  @readonly: true to open @path for reading, false for read/write.
 *  @errp: pointer to Error*, to store an error if it happens
 *
 * Return:
 *  On success, return a pointer to the ram block.
 *  On failure, return NULL.
 */
RAMBlock *qemu_ram_alloc_from_file(ram_addr_t size, MemoryRegion *mr,
                                   uint32_t ram_flags, const char *mem_path,
                                   bool readonly, Error **errp);
RAMBlock *qemu_ram_alloc_from_fd(ram_addr_t size, MemoryRegion *mr,
                                 uint32_t ram_flags, int fd, off_t offset,
                                 bool readonly, Error **errp);

RAMBlock *qemu_ram_alloc_from_ptr(ram_addr_t size, void *host,
                                  MemoryRegion *mr, Error **errp);
RAMBlock *qemu_ram_alloc(ram_addr_t size, uint32_t ram_flags, MemoryRegion *mr,
                         Error **errp);
RAMBlock *qemu_ram_alloc_resizeable(ram_addr_t size, ram_addr_t max_size,
                                    void (*resized)(const char*,
                                                    uint64_t length,
                                                    void *host),
                                    MemoryRegion *mr, Error **errp);
void qemu_ram_free(RAMBlock *block);

int qemu_ram_resize(RAMBlock *block, ram_addr_t newsize, Error **errp);

void qemu_ram_msync(RAMBlock *block, ram_addr_t start, ram_addr_t length);

/* Clear whole block of mem */
static inline void qemu_ram_block_writeback(RAMBlock *block)
{
    qemu_ram_msync(block, 0, block->used_length);
}

#define DIRTY_CLIENTS_ALL     ((1 << DIRTY_MEMORY_NUM) - 1)
#define DIRTY_CLIENTS_NOCODE  (DIRTY_CLIENTS_ALL & ~(1 << DIRTY_MEMORY_CODE))

static inline bool cpu_physical_memory_get_dirty(ram_addr_t start,
                                                 ram_addr_t length,
                                                 unsigned client)
{
    DirtyMemoryBlocks *blocks;
    unsigned long end, page;
    unsigned long idx, offset, base;
    bool dirty = false;

    assert(client < DIRTY_MEMORY_NUM);

    end = TARGET_PAGE_ALIGN(start + length) >> TARGET_PAGE_BITS;
    page = start >> TARGET_PAGE_BITS;

    WITH_RCU_READ_LOCK_GUARD() {
        blocks = qatomic_rcu_read(&ram_list.dirty_memory[client]);

        idx = page / DIRTY_MEMORY_BLOCK_SIZE;
        offset = page % DIRTY_MEMORY_BLOCK_SIZE;
        base = page - offset;
        while (page < end) {
            unsigned long next = MIN(end, base + DIRTY_MEMORY_BLOCK_SIZE);
            unsigned long num = next - base;
            unsigned long found = find_next_bit(blocks->blocks[idx],
                                                num, offset);
            if (found < num) {
                dirty = true;
                break;
            }

            page = next;
            idx++;
            offset = 0;
            base += DIRTY_MEMORY_BLOCK_SIZE;
        }
    }

    return dirty;
}

static inline bool cpu_physical_memory_all_dirty(ram_addr_t start,
                                                 ram_addr_t length,
                                                 unsigned client)
{
    DirtyMemoryBlocks *blocks;
    unsigned long end, page;
    unsigned long idx, offset, base;
    bool dirty = true;

    assert(client < DIRTY_MEMORY_NUM);

    end = TARGET_PAGE_ALIGN(start + length) >> TARGET_PAGE_BITS;
    page = start >> TARGET_PAGE_BITS;

    RCU_READ_LOCK_GUARD();

    blocks = qatomic_rcu_read(&ram_list.dirty_memory[client]);

    idx = page / DIRTY_MEMORY_BLOCK_SIZE;
    offset = page % DIRTY_MEMORY_BLOCK_SIZE;
    base = page - offset;
    while (page < end) {
        unsigned long next = MIN(end, base + DIRTY_MEMORY_BLOCK_SIZE);
        unsigned long num = next - base;
        unsigned long found = find_next_zero_bit(blocks->blocks[idx], num, offset);
        if (found < num) {
            dirty = false;
            break;
        }

        page = next;
        idx++;
        offset = 0;
        base += DIRTY_MEMORY_BLOCK_SIZE;
    }

    return dirty;
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
    unsigned long page, idx, offset;
    DirtyMemoryBlocks *blocks;

    assert(client < DIRTY_MEMORY_NUM);

    page = addr >> TARGET_PAGE_BITS;
    idx = page / DIRTY_MEMORY_BLOCK_SIZE;
    offset = page % DIRTY_MEMORY_BLOCK_SIZE;

    RCU_READ_LOCK_GUARD();

    blocks = qatomic_rcu_read(&ram_list.dirty_memory[client]);

    set_bit_atomic(offset, blocks->blocks[idx]);
}

static inline void cpu_physical_memory_set_dirty_range(ram_addr_t start,
                                                       ram_addr_t length,
                                                       uint8_t mask)
{
    DirtyMemoryBlocks *blocks[DIRTY_MEMORY_NUM];
    unsigned long end, page;
    unsigned long idx, offset, base;
    int i;

    if (!mask && !xen_enabled()) {
        return;
    }

    end = TARGET_PAGE_ALIGN(start + length) >> TARGET_PAGE_BITS;
    page = start >> TARGET_PAGE_BITS;

    WITH_RCU_READ_LOCK_GUARD() {
        for (i = 0; i < DIRTY_MEMORY_NUM; i++) {
            blocks[i] = qatomic_rcu_read(&ram_list.dirty_memory[i]);
        }

        idx = page / DIRTY_MEMORY_BLOCK_SIZE;
        offset = page % DIRTY_MEMORY_BLOCK_SIZE;
        base = page - offset;
        while (page < end) {
            unsigned long next = MIN(end, base + DIRTY_MEMORY_BLOCK_SIZE);

            if (likely(mask & (1 << DIRTY_MEMORY_MIGRATION))) {
                bitmap_set_atomic(blocks[DIRTY_MEMORY_MIGRATION]->blocks[idx],
                                  offset, next - page);
            }
            if (unlikely(mask & (1 << DIRTY_MEMORY_VGA))) {
                bitmap_set_atomic(blocks[DIRTY_MEMORY_VGA]->blocks[idx],
                                  offset, next - page);
            }
            if (unlikely(mask & (1 << DIRTY_MEMORY_CODE))) {
                bitmap_set_atomic(blocks[DIRTY_MEMORY_CODE]->blocks[idx],
                                  offset, next - page);
            }

            page = next;
            idx++;
            offset = 0;
            base += DIRTY_MEMORY_BLOCK_SIZE;
        }
    }

    xen_hvm_modified_memory(start, length);
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
    unsigned long hpratio = qemu_real_host_page_size() / TARGET_PAGE_SIZE;
    unsigned long page = BIT_WORD(start >> TARGET_PAGE_BITS);

    /* start address is aligned at the start of a word? */
    if ((((page * BITS_PER_LONG) << TARGET_PAGE_BITS) == start) &&
        (hpratio == 1)) {
        unsigned long **blocks[DIRTY_MEMORY_NUM];
        unsigned long idx;
        unsigned long offset;
        long k;
        long nr = BITS_TO_LONGS(pages);

        idx = (start >> TARGET_PAGE_BITS) / DIRTY_MEMORY_BLOCK_SIZE;
        offset = BIT_WORD((start >> TARGET_PAGE_BITS) %
                          DIRTY_MEMORY_BLOCK_SIZE);

        WITH_RCU_READ_LOCK_GUARD() {
            for (i = 0; i < DIRTY_MEMORY_NUM; i++) {
                blocks[i] =
                    qatomic_rcu_read(&ram_list.dirty_memory[i])->blocks;
            }

            for (k = 0; k < nr; k++) {
                if (bitmap[k]) {
                    unsigned long temp = leul_to_cpu(bitmap[k]);

                    qatomic_or(&blocks[DIRTY_MEMORY_VGA][idx][offset], temp);

                    if (global_dirty_tracking) {
                        qatomic_or(
                                &blocks[DIRTY_MEMORY_MIGRATION][idx][offset],
                                temp);
                        if (unlikely(
                            global_dirty_tracking & GLOBAL_DIRTY_DIRTY_RATE)) {
                            total_dirty_pages += ctpopl(temp);
                        }
                    }

                    if (tcg_enabled()) {
                        qatomic_or(&blocks[DIRTY_MEMORY_CODE][idx][offset],
                                   temp);
                    }
                }

                if (++offset >= BITS_TO_LONGS(DIRTY_MEMORY_BLOCK_SIZE)) {
                    offset = 0;
                    idx++;
                }
            }
        }

        xen_hvm_modified_memory(start, pages << TARGET_PAGE_BITS);
    } else {
        uint8_t clients = tcg_enabled() ? DIRTY_CLIENTS_ALL : DIRTY_CLIENTS_NOCODE;

        if (!global_dirty_tracking) {
            clients &= ~(1 << DIRTY_MEMORY_MIGRATION);
        }

        /*
         * bitmap-traveling is faster than memory-traveling (for addr...)
         * especially when most of the memory is not dirty.
         */
        for (i = 0; i < len; i++) {
            if (bitmap[i] != 0) {
                c = leul_to_cpu(bitmap[i]);
                if (unlikely(global_dirty_tracking & GLOBAL_DIRTY_DIRTY_RATE)) {
                    total_dirty_pages += ctpopl(c);
                }
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

DirtyBitmapSnapshot *cpu_physical_memory_snapshot_and_clear_dirty
    (MemoryRegion *mr, hwaddr offset, hwaddr length, unsigned client);

bool cpu_physical_memory_snapshot_get_dirty(DirtyBitmapSnapshot *snap,
                                            ram_addr_t start,
                                            ram_addr_t length);

static inline void cpu_physical_memory_clear_dirty_range(ram_addr_t start,
                                                         ram_addr_t length)
{
    cpu_physical_memory_test_and_clear_dirty(start, length, DIRTY_MEMORY_MIGRATION);
    cpu_physical_memory_test_and_clear_dirty(start, length, DIRTY_MEMORY_VGA);
    cpu_physical_memory_test_and_clear_dirty(start, length, DIRTY_MEMORY_CODE);
}


/* Called with RCU critical section */
static inline
uint64_t cpu_physical_memory_sync_dirty_bitmap(RAMBlock *rb,
                                               ram_addr_t start,
                                               ram_addr_t length)
{
    ram_addr_t addr;
    unsigned long word = BIT_WORD((start + rb->offset) >> TARGET_PAGE_BITS);
    uint64_t num_dirty = 0;
    unsigned long *dest = rb->bmap;

    /* start address and length is aligned at the start of a word? */
    if (((word * BITS_PER_LONG) << TARGET_PAGE_BITS) ==
         (start + rb->offset) &&
        !(length & ((BITS_PER_LONG << TARGET_PAGE_BITS) - 1))) {
        int k;
        int nr = BITS_TO_LONGS(length >> TARGET_PAGE_BITS);
        unsigned long * const *src;
        unsigned long idx = (word * BITS_PER_LONG) / DIRTY_MEMORY_BLOCK_SIZE;
        unsigned long offset = BIT_WORD((word * BITS_PER_LONG) %
                                        DIRTY_MEMORY_BLOCK_SIZE);
        unsigned long page = BIT_WORD(start >> TARGET_PAGE_BITS);

        src = qatomic_rcu_read(
                &ram_list.dirty_memory[DIRTY_MEMORY_MIGRATION])->blocks;

        for (k = page; k < page + nr; k++) {
            if (src[idx][offset]) {
                unsigned long bits = qatomic_xchg(&src[idx][offset], 0);
                unsigned long new_dirty;
                new_dirty = ~dest[k];
                dest[k] |= bits;
                new_dirty &= bits;
                num_dirty += ctpopl(new_dirty);
            }

            if (++offset >= BITS_TO_LONGS(DIRTY_MEMORY_BLOCK_SIZE)) {
                offset = 0;
                idx++;
            }
        }

        if (rb->clear_bmap) {
            /*
             * Postpone the dirty bitmap clear to the point before we
             * really send the pages, also we will split the clear
             * dirty procedure into smaller chunks.
             */
            clear_bmap_set(rb, start >> TARGET_PAGE_BITS,
                           length >> TARGET_PAGE_BITS);
        } else {
            /* Slow path - still do that in a huge chunk */
            memory_region_clear_dirty_bitmap(rb->mr, start, length);
        }
    } else {
        ram_addr_t offset = rb->offset;

        for (addr = 0; addr < length; addr += TARGET_PAGE_SIZE) {
            if (cpu_physical_memory_test_and_clear_dirty(
                        start + addr + offset,
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
