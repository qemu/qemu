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

#ifndef SYSTEM_RAM_ADDR_H
#define SYSTEM_RAM_ADDR_H

#include "system/ramblock.h"
#include "exec/target_page.h"
#include "exec/hwaddr.h"

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

static inline unsigned long int ramblock_recv_bitmap_offset(void *host_addr,
                                                            RAMBlock *rb)
{
    uint64_t host_addr_offset =
            (uint64_t)(uintptr_t)(host_addr - (void *)rb->host);
    return host_addr_offset >> TARGET_PAGE_BITS;
}

/**
 * qemu_ram_alloc_from_file,
 * qemu_ram_alloc_from_fd:  Allocate a ram block from the specified backing
 *                          file or device
 *
 * Parameters:
 *  @size: the size in bytes of the ram block
 *  @max_size: the maximum size of the block after resizing
 *  @mr: the memory region where the ram block is
 *  @resized: callback after calls to qemu_ram_resize
 *  @ram_flags: RamBlock flags. Supported flags: RAM_SHARED, RAM_PMEM,
 *              RAM_NORESERVE, RAM_PROTECTED, RAM_NAMED_FILE, RAM_READONLY,
 *              RAM_READONLY_FD, RAM_GUEST_MEMFD
 *  @mem_path or @fd: specify the backing file or device
 *  @offset: Offset into target file
 *  @grow: extend file if necessary (but an empty file is always extended).
 *  @errp: pointer to Error*, to store an error if it happens
 *
 * Return:
 *  On success, return a pointer to the ram block.
 *  On failure, return NULL.
 */
typedef void (*qemu_ram_resize_cb)(const char *, uint64_t length, void *host);

RAMBlock *qemu_ram_alloc_from_file(ram_addr_t size, MemoryRegion *mr,
                                   uint32_t ram_flags, const char *mem_path,
                                   off_t offset, Error **errp);
RAMBlock *qemu_ram_alloc_from_fd(ram_addr_t size, ram_addr_t max_size,
                                 qemu_ram_resize_cb resized, MemoryRegion *mr,
                                 uint32_t ram_flags, int fd, off_t offset,
                                 bool grow,
                                 Error **errp);

RAMBlock *qemu_ram_alloc_from_ptr(ram_addr_t size, void *host,
                                  MemoryRegion *mr, Error **errp);
RAMBlock *qemu_ram_alloc(ram_addr_t size, uint32_t ram_flags, MemoryRegion *mr,
                         Error **errp);
RAMBlock *qemu_ram_alloc_resizeable(ram_addr_t size, ram_addr_t max_size,
                                    qemu_ram_resize_cb resized,
                                    MemoryRegion *mr, Error **errp);
void qemu_ram_free(RAMBlock *block);

int qemu_ram_resize(RAMBlock *block, ram_addr_t newsize, Error **errp);

void qemu_ram_msync(RAMBlock *block, ram_addr_t start, ram_addr_t length);

/* Clear whole block of mem */
static inline void qemu_ram_block_writeback(RAMBlock *block)
{
    qemu_ram_msync(block, 0, block->used_length);
}

#endif
