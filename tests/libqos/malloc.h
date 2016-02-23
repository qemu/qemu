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

#include "qemu/queue.h"

typedef enum {
    ALLOC_NO_FLAGS    = 0x00,
    ALLOC_LEAK_WARN   = 0x01,
    ALLOC_LEAK_ASSERT = 0x02,
    ALLOC_PARANOID    = 0x04
} QAllocOpts;

typedef struct QGuestAllocator QGuestAllocator;

void alloc_uninit(QGuestAllocator *allocator);

/* Always returns page aligned values */
uint64_t guest_alloc(QGuestAllocator *allocator, size_t size);
void guest_free(QGuestAllocator *allocator, uint64_t addr);
void migrate_allocator(QGuestAllocator *src, QGuestAllocator *dst);

QGuestAllocator *alloc_init(uint64_t start, uint64_t end);
QGuestAllocator *alloc_init_flags(QAllocOpts flags,
                                  uint64_t start, uint64_t end);
void alloc_set_page_size(QGuestAllocator *allocator, size_t page_size);
void alloc_set_flags(QGuestAllocator *allocator, QAllocOpts opts);

#endif
