/*
 * Basic libqos generic malloc support
 *
 * Copyright (c) 2014 Marc Marí
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef LIBQOS_MALLOC_GENERIC_H
#define LIBQOS_MALLOC_GENERIC_H

#include "libqos/malloc.h"

QGuestAllocator *generic_alloc_init(uint64_t base_addr, uint64_t size,
                                                            uint32_t page_size);
QGuestAllocator *generic_alloc_init_flags(uint64_t base_addr, uint64_t size,
                                        uint32_t page_size, QAllocOpts flags);
void generic_alloc_uninit(QGuestAllocator *allocator);

#endif
