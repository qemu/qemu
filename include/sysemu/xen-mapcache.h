/*
 * Copyright (C) 2011       Citrix Ltd.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef XEN_MAPCACHE_H
#define XEN_MAPCACHE_H

#include "exec/cpu-common.h"
#include "sysemu/xen.h"

typedef hwaddr (*phys_offset_to_gaddr_t)(hwaddr phys_offset,
                                         ram_addr_t size);
#ifdef CONFIG_XEN_IS_POSSIBLE

void xen_map_cache_init(phys_offset_to_gaddr_t f,
                        void *opaque);
uint8_t *xen_map_cache(hwaddr phys_addr, hwaddr size,
                       uint8_t lock, bool dma);
ram_addr_t xen_ram_addr_from_mapcache(void *ptr);
void xen_invalidate_map_cache_entry(uint8_t *buffer);
void xen_invalidate_map_cache(void);
uint8_t *xen_replace_cache_entry(hwaddr old_phys_addr,
                                 hwaddr new_phys_addr,
                                 hwaddr size);
#else

static inline void xen_map_cache_init(phys_offset_to_gaddr_t f,
                                      void *opaque)
{
}

static inline uint8_t *xen_map_cache(hwaddr phys_addr,
                                     hwaddr size,
                                     uint8_t lock,
                                     bool dma)
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

static inline uint8_t *xen_replace_cache_entry(hwaddr old_phys_addr,
                                               hwaddr new_phys_addr,
                                               hwaddr size)
{
    abort();
}

#endif

#endif /* XEN_MAPCACHE_H */
