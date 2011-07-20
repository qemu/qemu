/*
 * Copyright (C) 2011       Citrix Ltd.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef XEN_MAPCACHE_H
#define XEN_MAPCACHE_H

#include <stdlib.h>

#ifdef CONFIG_XEN

void xen_map_cache_init(void);
uint8_t *xen_map_cache(target_phys_addr_t phys_addr, target_phys_addr_t size,
                       uint8_t lock);
ram_addr_t xen_ram_addr_from_mapcache(void *ptr);
void xen_invalidate_map_cache_entry(uint8_t *buffer);
void xen_invalidate_map_cache(void);

#else

static inline void xen_map_cache_init(void)
{
}

static inline uint8_t *xen_map_cache(target_phys_addr_t phys_addr,
                                     target_phys_addr_t size,
                                     uint8_t lock)
{
    abort();
}

static inline ram_addr_t xen_ram_addr_from_mapcache(void *ptr)
{
    abort();
}

static inline void xen_invalidate_map_cache_entry(uint8_t *buffer)
{
}

static inline void xen_invalidate_map_cache(void)
{
}

#endif

#endif /* !XEN_MAPCACHE_H */
