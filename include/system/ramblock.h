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

#include "exec/cpu-common.h"
#include "qemu/rcu.h"
#include "exec/ramlist.h"
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

#endif
