/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qom/object.h"
#include "glib.h"
#include "system/memory.h"

RamDiscardManager *memory_region_get_ram_discard_manager(MemoryRegion *mr)
{
    return mr->rdm;
}

int memory_region_add_ram_discard_source(MemoryRegion *mr,
                                         RamDiscardSource *source)
{
    if (!mr->rdm) {
        mr->rdm = ram_discard_manager_new(mr);
    }
    return ram_discard_manager_add_source(mr->rdm, source);
}

int memory_region_del_ram_discard_source(MemoryRegion *mr,
                                         RamDiscardSource *source)
{
    RamDiscardManager *rdm = mr->rdm;

    if (!rdm) {
        return 0;
    }

    return ram_discard_manager_del_source(rdm, source);
}

uint64_t memory_region_size(const MemoryRegion *mr)
{
    return int128_get64(mr->size);
}

MemoryRegionSection *memory_region_section_new_copy(MemoryRegionSection *s)
{
    MemoryRegionSection *copy = g_new(MemoryRegionSection, 1);
    *copy = *s;
    return copy;
}

void memory_region_section_free_copy(MemoryRegionSection *s)
{
    g_free(s);
}
