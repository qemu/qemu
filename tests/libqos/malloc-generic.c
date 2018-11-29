/*
 * Basic libqos generic malloc support
 *
 * Copyright (c) 2014 Marc Marí
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqos/malloc-generic.h"
#include "libqos/malloc.h"

void generic_alloc_init(QGuestAllocator *s, uint64_t base_addr,
                        uint64_t size, uint32_t page_size)
{
    uint64_t start = base_addr + (1 << 20); /* Start at 1MB */

    alloc_init(s, 0, start, start + size, page_size);
}
