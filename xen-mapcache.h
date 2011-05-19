/*
 * Copyright (C) 2011       Citrix Ltd.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef XEN_MAPCACHE_H
#define XEN_MAPCACHE_H

#include <sys/mman.h>
#include "trace.h"

void     qemu_map_cache_init(void);
uint8_t  *qemu_map_cache(target_phys_addr_t phys_addr, target_phys_addr_t size, uint8_t lock);
ram_addr_t qemu_ram_addr_from_mapcache(void *ptr);
void     qemu_invalidate_entry(uint8_t *buffer);
void     qemu_invalidate_map_cache(void);

uint8_t *xen_map_block(target_phys_addr_t phys_addr, target_phys_addr_t size);

static inline void xen_unmap_block(void *addr, ram_addr_t size)
{
    trace_xen_unmap_block(addr, size);

    if (munmap(addr, size) != 0) {
        hw_error("xen_unmap_block: %s", strerror(errno));
    }
}


#define mapcache_lock()   ((void)0)
#define mapcache_unlock() ((void)0)

#endif /* !XEN_MAPCACHE_H */
