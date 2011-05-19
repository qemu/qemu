/*
 * Copyright (C) 2011       Citrix Ltd.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "config.h"

#include "exec-all.h"
#include "qemu-common.h"
#include "cpu-common.h"
#include "xen-mapcache.h"

void qemu_map_cache_init(void)
{
}

uint8_t *qemu_map_cache(target_phys_addr_t phys_addr, target_phys_addr_t size, uint8_t lock)
{
    return qemu_get_ram_ptr(phys_addr);
}

ram_addr_t qemu_ram_addr_from_mapcache(void *ptr)
{
    return -1;
}

void qemu_invalidate_map_cache(void)
{
}

void qemu_invalidate_entry(uint8_t *buffer)
{
}
uint8_t *xen_map_block(target_phys_addr_t phys_addr, target_phys_addr_t size)
{
    return NULL;
}
