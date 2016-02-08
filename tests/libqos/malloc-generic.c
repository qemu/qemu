/*
 * Basic libqos generic malloc support
 *
 * Copyright (c) 2014 Marc Marí
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <glib.h>
#include "libqos/malloc-generic.h"
#include "libqos/malloc.h"

/*
 * Mostly for valgrind happiness, but it does offer
 * a chokepoint for debugging guest memory leaks, too.
 */
void generic_alloc_uninit(QGuestAllocator *allocator)
{
    alloc_uninit(allocator);
}

QGuestAllocator *generic_alloc_init_flags(uint64_t base_addr, uint64_t size,
                                        uint32_t page_size, QAllocOpts flags)
{
    QGuestAllocator *s;
    uint64_t start = base_addr + (1 << 20); /* Start at 1MB */

    s = alloc_init_flags(flags, start, start + size);
    alloc_set_page_size(s, page_size);

    return s;
}

inline QGuestAllocator *generic_alloc_init(uint64_t base_addr, uint64_t size,
                                                            uint32_t page_size)
{
    return generic_alloc_init_flags(base_addr, size, page_size, ALLOC_NO_FLAGS);
}
