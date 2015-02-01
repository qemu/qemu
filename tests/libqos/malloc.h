/*
 * libqos malloc support
 *
 * Copyright IBM, Corp. 2012-2013
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef LIBQOS_MALLOC_H
#define LIBQOS_MALLOC_H

#include <stdint.h>
#include <sys/types.h>
#include "qemu/queue.h"

#define MLIST_ENTNAME entries

typedef enum {
    ALLOC_NO_FLAGS    = 0x00,
    ALLOC_LEAK_WARN   = 0x01,
    ALLOC_LEAK_ASSERT = 0x02,
    ALLOC_PARANOID    = 0x04
} QAllocOpts;

typedef QTAILQ_HEAD(MemList, MemBlock) MemList;
typedef struct MemBlock {
    QTAILQ_ENTRY(MemBlock) MLIST_ENTNAME;
    uint64_t size;
    uint64_t addr;
} MemBlock;

typedef struct QGuestAllocator {
    QAllocOpts opts;
    uint64_t start;
    uint64_t end;
    uint32_t page_size;

    MemList used;
    MemList free;
} QGuestAllocator;

MemBlock *mlist_new(uint64_t addr, uint64_t size);
void alloc_uninit(QGuestAllocator *allocator);

/* Always returns page aligned values */
uint64_t guest_alloc(QGuestAllocator *allocator, size_t size);
void guest_free(QGuestAllocator *allocator, uint64_t addr);

#endif
