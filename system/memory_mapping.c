/*
 * QEMU memory mapping
 *
 * Copyright Fujitsu, Corp. 2011, 2012
 *
 * Authors:
 *     Wen Congyang <wency@cn.fujitsu.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"

#include "sysemu/memory_mapping.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"
#include "hw/core/cpu.h"

//#define DEBUG_GUEST_PHYS_REGION_ADD

static void memory_mapping_list_add_mapping_sorted(MemoryMappingList *list,
                                                   MemoryMapping *mapping)
{
    MemoryMapping *p;

    QTAILQ_FOREACH(p, &list->head, next) {
        if (p->phys_addr >= mapping->phys_addr) {
            QTAILQ_INSERT_BEFORE(p, mapping, next);
            return;
        }
    }
    QTAILQ_INSERT_TAIL(&list->head, mapping, next);
}

static void create_new_memory_mapping(MemoryMappingList *list,
                                      hwaddr phys_addr,
                                      hwaddr virt_addr,
                                      ram_addr_t length)
{
    MemoryMapping *memory_mapping;

    memory_mapping = g_new(MemoryMapping, 1);
    memory_mapping->phys_addr = phys_addr;
    memory_mapping->virt_addr = virt_addr;
    memory_mapping->length = length;
    list->last_mapping = memory_mapping;
    list->num++;
    memory_mapping_list_add_mapping_sorted(list, memory_mapping);
}

static inline bool mapping_contiguous(MemoryMapping *map,
                                      hwaddr phys_addr,
                                      hwaddr virt_addr)
{
    return phys_addr == map->phys_addr + map->length &&
           virt_addr == map->virt_addr + map->length;
}

/*
 * [map->phys_addr, map->phys_addr + map->length) and
 * [phys_addr, phys_addr + length) have intersection?
 */
static inline bool mapping_have_same_region(MemoryMapping *map,
                                            hwaddr phys_addr,
                                            ram_addr_t length)
{
    return !(phys_addr + length < map->phys_addr ||
             phys_addr >= map->phys_addr + map->length);
}

/*
 * [map->phys_addr, map->phys_addr + map->length) and
 * [phys_addr, phys_addr + length) have intersection. The virtual address in the
 * intersection are the same?
 */
static inline bool mapping_conflict(MemoryMapping *map,
                                    hwaddr phys_addr,
                                    hwaddr virt_addr)
{
    return virt_addr - map->virt_addr != phys_addr - map->phys_addr;
}

/*
 * [map->virt_addr, map->virt_addr + map->length) and
 * [virt_addr, virt_addr + length) have intersection. And the physical address
 * in the intersection are the same.
 */
static inline void mapping_merge(MemoryMapping *map,
                                 hwaddr virt_addr,
                                 ram_addr_t length)
{
    if (virt_addr < map->virt_addr) {
        map->length += map->virt_addr - virt_addr;
        map->virt_addr = virt_addr;
    }

    if ((virt_addr + length) >
        (map->virt_addr + map->length)) {
        map->length = virt_addr + length - map->virt_addr;
    }
}

void memory_mapping_list_add_merge_sorted(MemoryMappingList *list,
                                          hwaddr phys_addr,
                                          hwaddr virt_addr,
                                          ram_addr_t length)
{
    MemoryMapping *memory_mapping, *last_mapping;

    if (QTAILQ_EMPTY(&list->head)) {
        create_new_memory_mapping(list, phys_addr, virt_addr, length);
        return;
    }

    last_mapping = list->last_mapping;
    if (last_mapping) {
        if (mapping_contiguous(last_mapping, phys_addr, virt_addr)) {
            last_mapping->length += length;
            return;
        }
    }

    QTAILQ_FOREACH(memory_mapping, &list->head, next) {
        if (mapping_contiguous(memory_mapping, phys_addr, virt_addr)) {
            memory_mapping->length += length;
            list->last_mapping = memory_mapping;
            return;
        }

        if (phys_addr + length < memory_mapping->phys_addr) {
            /* create a new region before memory_mapping */
            break;
        }

        if (mapping_have_same_region(memory_mapping, phys_addr, length)) {
            if (mapping_conflict(memory_mapping, phys_addr, virt_addr)) {
                continue;
            }

            /* merge this region into memory_mapping */
            mapping_merge(memory_mapping, virt_addr, length);
            list->last_mapping = memory_mapping;
            return;
        }
    }

    /* this region can not be merged into any existed memory mapping. */
    create_new_memory_mapping(list, phys_addr, virt_addr, length);
}

void memory_mapping_list_free(MemoryMappingList *list)
{
    MemoryMapping *p, *q;

    QTAILQ_FOREACH_SAFE(p, &list->head, next, q) {
        QTAILQ_REMOVE(&list->head, p, next);
        g_free(p);
    }

    list->num = 0;
    list->last_mapping = NULL;
}

void memory_mapping_list_init(MemoryMappingList *list)
{
    list->num = 0;
    list->last_mapping = NULL;
    QTAILQ_INIT(&list->head);
}

void guest_phys_blocks_free(GuestPhysBlockList *list)
{
    GuestPhysBlock *p, *q;

    QTAILQ_FOREACH_SAFE(p, &list->head, next, q) {
        QTAILQ_REMOVE(&list->head, p, next);
        memory_region_unref(p->mr);
        g_free(p);
    }
    list->num = 0;
}

void guest_phys_blocks_init(GuestPhysBlockList *list)
{
    list->num = 0;
    QTAILQ_INIT(&list->head);
}

typedef struct GuestPhysListener {
    GuestPhysBlockList *list;
    MemoryListener listener;
} GuestPhysListener;

static void guest_phys_block_add_section(GuestPhysListener *g,
                                         MemoryRegionSection *section)
{
    const hwaddr target_start = section->offset_within_address_space;
    const hwaddr target_end = target_start + int128_get64(section->size);
    uint8_t *host_addr = memory_region_get_ram_ptr(section->mr) +
                         section->offset_within_region;
    GuestPhysBlock *predecessor = NULL;

    /* find continuity in guest physical address space */
    if (!QTAILQ_EMPTY(&g->list->head)) {
        hwaddr predecessor_size;

        predecessor = QTAILQ_LAST(&g->list->head);
        predecessor_size = predecessor->target_end - predecessor->target_start;

        /* the memory API guarantees monotonically increasing traversal */
        g_assert(predecessor->target_end <= target_start);

        /* we want continuity in both guest-physical and host-virtual memory */
        if (predecessor->target_end < target_start ||
            predecessor->host_addr + predecessor_size != host_addr ||
            predecessor->mr != section->mr) {
            predecessor = NULL;
        }
    }

    if (predecessor == NULL) {
        /* isolated mapping, allocate it and add it to the list */
        GuestPhysBlock *block = g_malloc0(sizeof *block);

        block->target_start = target_start;
        block->target_end   = target_end;
        block->host_addr    = host_addr;
        block->mr           = section->mr;
        memory_region_ref(section->mr);

        QTAILQ_INSERT_TAIL(&g->list->head, block, next);
        ++g->list->num;
    } else {
        /* expand predecessor until @target_end; predecessor's start doesn't
         * change
         */
        predecessor->target_end = target_end;
    }

#ifdef DEBUG_GUEST_PHYS_REGION_ADD
    fprintf(stderr, "%s: target_start=" HWADDR_FMT_plx " target_end="
            HWADDR_FMT_plx ": %s (count: %u)\n", __func__, target_start,
            target_end, predecessor ? "joined" : "added", g->list->num);
#endif
}

static int guest_phys_ram_populate_cb(MemoryRegionSection *section,
                                      void *opaque)
{
    GuestPhysListener *g = opaque;

    guest_phys_block_add_section(g, section);
    return 0;
}

static void guest_phys_blocks_region_add(MemoryListener *listener,
                                         MemoryRegionSection *section)
{
    GuestPhysListener *g = container_of(listener, GuestPhysListener, listener);

    /* we only care about RAM */
    if (!memory_region_is_ram(section->mr) ||
        memory_region_is_ram_device(section->mr) ||
        memory_region_is_nonvolatile(section->mr)) {
        return;
    }

    /* for special sparse regions, only add populated parts */
    if (memory_region_has_ram_discard_manager(section->mr)) {
        RamDiscardManager *rdm;

        rdm = memory_region_get_ram_discard_manager(section->mr);
        ram_discard_manager_replay_populated(rdm, section,
                                             guest_phys_ram_populate_cb, g);
        return;
    }

    guest_phys_block_add_section(g, section);
}

void guest_phys_blocks_append(GuestPhysBlockList *list)
{
    GuestPhysListener g = { 0 };

    g.list = list;
    g.listener.region_add = &guest_phys_blocks_region_add;
    memory_listener_register(&g.listener, &address_space_memory);
    memory_listener_unregister(&g.listener);
}

static CPUState *find_paging_enabled_cpu(void)
{
    CPUState *cpu;

    CPU_FOREACH(cpu) {
        if (cpu_paging_enabled(cpu)) {
            return cpu;
        }
    }

    return NULL;
}

bool qemu_get_guest_memory_mapping(MemoryMappingList *list,
                                   const GuestPhysBlockList *guest_phys_blocks,
                                   Error **errp)
{
    ERRP_GUARD();
    CPUState *cpu, *first_paging_enabled_cpu;
    GuestPhysBlock *block;
    ram_addr_t offset, length;

    first_paging_enabled_cpu = find_paging_enabled_cpu();
    if (first_paging_enabled_cpu) {
        for (cpu = first_paging_enabled_cpu; cpu != NULL;
             cpu = CPU_NEXT(cpu)) {
            if (!cpu_get_memory_mapping(cpu, list, errp)) {
                return false;
            }
        }
        return true;
    }

    /*
     * If the guest doesn't use paging, the virtual address is equal to physical
     * address.
     */
    QTAILQ_FOREACH(block, &guest_phys_blocks->head, next) {
        offset = block->target_start;
        length = block->target_end - block->target_start;
        create_new_memory_mapping(list, offset, offset, length);
    }
    return true;
}

void qemu_get_guest_simple_memory_mapping(MemoryMappingList *list,
                                   const GuestPhysBlockList *guest_phys_blocks)
{
    GuestPhysBlock *block;

    QTAILQ_FOREACH(block, &guest_phys_blocks->head, next) {
        create_new_memory_mapping(list, block->target_start, 0,
                                  block->target_end - block->target_start);
    }
}

void memory_mapping_filter(MemoryMappingList *list, int64_t begin,
                           int64_t length)
{
    MemoryMapping *cur, *next;

    QTAILQ_FOREACH_SAFE(cur, &list->head, next, next) {
        if (cur->phys_addr >= begin + length ||
            cur->phys_addr + cur->length <= begin) {
            QTAILQ_REMOVE(&list->head, cur, next);
            g_free(cur);
            list->num--;
            continue;
        }

        if (cur->phys_addr < begin) {
            cur->length -= begin - cur->phys_addr;
            if (cur->virt_addr) {
                cur->virt_addr += begin - cur->phys_addr;
            }
            cur->phys_addr = begin;
        }

        if (cur->phys_addr + cur->length > begin + length) {
            cur->length -= cur->phys_addr + cur->length - begin - length;
        }
    }
}
