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

#ifndef SYSTEM_RAMBLOCK_H
#define SYSTEM_RAMBLOCK_H

#include "qemu/rcu.h"
#include "system/ram_addr.h"
#include "system/ramlist.h"
#include "system/hostmem.h"

#define TYPE_RAM_BLOCK_ATTRIBUTES "ram-block-attributes"
OBJECT_DECLARE_SIMPLE_TYPE(RamBlockAttributes, RAM_BLOCK_ATTRIBUTES)

struct RAMBlock {
    struct rcu_head rcu;
    struct MemoryRegion *mr;
    uint8_t *host;
    uint8_t *colo_cache; /* For colo, VM's ram cache */
    ram_addr_t offset;
    ram_addr_t used_length;
    ram_addr_t max_length;
    void (*resized)(const char*, uint64_t length, void *host);
    uint32_t flags;
    /* Protected by the BQL.  */
    char idstr[256];
    /* RCU-enabled, writes protected by the ramlist lock */
    QLIST_ENTRY(RAMBlock) next;
    QLIST_HEAD(, RAMBlockNotifier) ramblock_notifiers;
    Error *cpr_blocker;
    int fd;
    uint64_t fd_offset;
    int guest_memfd;
    RamBlockAttributes *attributes;
    size_t page_size;
    /* dirty bitmap used during migration */
    unsigned long *bmap;

    /*
     * Below fields are only used by mapped-ram migration
     */
    /* bitmap of pages present in the migration file */
    unsigned long *file_bmap;
    /*
     * offset in the file pages belonging to this ramblock are saved,
     * used only during migration to a file.
     */
    off_t bitmap_offset;
    uint64_t pages_offset;

    /* Bitmap of already received pages.  Only used on destination side. */
    unsigned long *receivedmap;

    /*
     * bitmap to track already cleared dirty bitmap.  When the bit is
     * set, it means the corresponding memory chunk needs a log-clear.
     * Set this up to non-NULL to enable the capability to postpone
     * and split clearing of dirty bitmap on the remote node (e.g.,
     * KVM).  The bitmap will be set only when doing global sync.
     *
     * It is only used during src side of ram migration, and it is
     * protected by the global ram_state.bitmap_mutex.
     *
     * NOTE: this bitmap is different comparing to the other bitmaps
     * in that one bit can represent multiple guest pages (which is
     * decided by the `clear_bmap_shift' variable below).  On
     * destination side, this should always be NULL, and the variable
     * `clear_bmap_shift' is meaningless.
     */
    unsigned long *clear_bmap;
    uint8_t clear_bmap_shift;

    /*
     * RAM block length that corresponds to the used_length on the migration
     * source (after RAM block sizes were synchronized). Especially, after
     * starting to run the guest, used_length and postcopy_length can differ.
     * Used to register/unregister uffd handlers and as the size of the received
     * bitmap. Receiving any page beyond this length will bail out, as it
     * could not have been valid on the source.
     */
    ram_addr_t postcopy_length;
};

struct RamBlockAttributes {
    Object parent;

    RAMBlock *ram_block;

    /* 1-setting of the bitmap represents ram is populated (shared) */
    unsigned bitmap_size;
    unsigned long *bitmap;

    QLIST_HEAD(, RamDiscardListener) rdl_list;
};

/* @offset: the offset within the RAMBlock */
int ram_block_discard_range(RAMBlock *rb, uint64_t offset, size_t length);
/* @offset: the offset within the RAMBlock */
int ram_block_discard_guest_memfd_range(RAMBlock *rb, uint64_t offset,
                                        size_t length);

RamBlockAttributes *ram_block_attributes_create(RAMBlock *ram_block);
void ram_block_attributes_destroy(RamBlockAttributes *attr);
int ram_block_attributes_state_change(RamBlockAttributes *attr, uint64_t offset,
                                      uint64_t size, bool to_discard);

/**
 * ram_block_is_pmem: Whether the RAM block is of persistent memory
 */
bool ram_block_is_pmem(RAMBlock *rb);

static inline bool offset_in_ramblock(RAMBlock *b, ram_addr_t offset)
{
    return b && b->host && (offset < b->used_length);
}

static inline void *ramblock_ptr(RAMBlock *block, ram_addr_t offset)
{
    assert(offset_in_ramblock(block, offset));
    return (char *)block->host + offset;
}

/* memory API */

void qemu_ram_remap(ram_addr_t addr);
/* This should not be used by devices.  */
ram_addr_t qemu_ram_addr_from_host(void *ptr);
ram_addr_t qemu_ram_addr_from_host_nofail(void *ptr);
RAMBlock *qemu_ram_block_by_name(const char *name);

/*
 * Translates a host ptr back to a RAMBlock and an offset in that RAMBlock.
 *
 * @ptr: The host pointer to translate.
 * @round_offset: Whether to round the result offset down to a target page
 * @offset: Will be set to the offset within the returned RAMBlock.
 *
 * Returns: RAMBlock (or NULL if not found)
 *
 * By the time this function returns, the returned pointer is not protected
 * by RCU anymore.  If the caller is not within an RCU critical section and
 * does not hold the BQL, it must have other means of protecting the
 * pointer, such as a reference to the memory region that owns the RAMBlock.
 */
RAMBlock *qemu_ram_block_from_host(void *ptr, bool round_offset,
                                   ram_addr_t *offset);
ram_addr_t qemu_ram_block_host_offset(RAMBlock *rb, void *host);
void qemu_ram_set_idstr(RAMBlock *block, const char *name, DeviceState *dev);
void qemu_ram_unset_idstr(RAMBlock *block);
const char *qemu_ram_get_idstr(RAMBlock *rb);
void *qemu_ram_get_host_addr(RAMBlock *rb);
ram_addr_t qemu_ram_get_offset(RAMBlock *rb);
ram_addr_t qemu_ram_get_fd_offset(RAMBlock *rb);
ram_addr_t qemu_ram_get_used_length(RAMBlock *rb);
ram_addr_t qemu_ram_get_max_length(RAMBlock *rb);
bool qemu_ram_is_shared(RAMBlock *rb);
bool qemu_ram_is_noreserve(RAMBlock *rb);
bool qemu_ram_is_uf_zeroable(RAMBlock *rb);
void qemu_ram_set_uf_zeroable(RAMBlock *rb);
bool qemu_ram_is_migratable(RAMBlock *rb);
void qemu_ram_set_migratable(RAMBlock *rb);
void qemu_ram_unset_migratable(RAMBlock *rb);
bool qemu_ram_is_named_file(RAMBlock *rb);
int qemu_ram_get_fd(RAMBlock *rb);

size_t qemu_ram_pagesize(RAMBlock *block);
size_t qemu_ram_pagesize_largest(void);
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
