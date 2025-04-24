/*
 * Various stubs for xen functions
 *
 * Those functions are used only if xen_enabled(). This file is linked only if
 * CONFIG_XEN is not set, so they should never be called.
 *
 * Copyright (c) 2025 Linaro, Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "system/xen.h"
#include "system/xen-mapcache.h"

void xen_hvm_modified_memory(ram_addr_t start, ram_addr_t length)
{
    g_assert_not_reached();
}

void xen_ram_alloc(ram_addr_t ram_addr, ram_addr_t size,
                   struct MemoryRegion *mr, Error **errp)
{
    g_assert_not_reached();
}

bool xen_mr_is_memory(MemoryRegion *mr)
{
    g_assert_not_reached();
}

void xen_invalidate_map_cache_entry(uint8_t *buffer)
{
    g_assert_not_reached();
}

ram_addr_t xen_ram_addr_from_mapcache(void *ptr)
{
    g_assert_not_reached();
}

uint8_t *xen_map_cache(MemoryRegion *mr,
                       hwaddr phys_addr,
                       hwaddr size,
                       ram_addr_t ram_addr_offset,
                       uint8_t lock,
                       bool dma,
                       bool is_write)
{
    g_assert_not_reached();
}
