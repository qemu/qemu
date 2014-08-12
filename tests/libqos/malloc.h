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

typedef struct QGuestAllocator QGuestAllocator;

struct QGuestAllocator
{
    uint64_t (*alloc)(QGuestAllocator *allocator, size_t size);
    void (*free)(QGuestAllocator *allocator, uint64_t addr);
};

/* Always returns page aligned values */
static inline uint64_t guest_alloc(QGuestAllocator *allocator, size_t size)
{
    return allocator->alloc(allocator, size);
}

static inline void guest_free(QGuestAllocator *allocator, uint64_t addr)
{
    allocator->free(allocator, addr);
}

#endif
