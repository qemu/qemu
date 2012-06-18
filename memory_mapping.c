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

#include "cpu.h"
#include "cpu-all.h"
#include "memory_mapping.h"

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
                                      target_phys_addr_t phys_addr,
                                      target_phys_addr_t virt_addr,
                                      ram_addr_t length)
{
    MemoryMapping *memory_mapping;

    memory_mapping = g_malloc(sizeof(MemoryMapping));
    memory_mapping->phys_addr = phys_addr;
    memory_mapping->virt_addr = virt_addr;
    memory_mapping->length = length;
    list->last_mapping = memory_mapping;
    list->num++;
    memory_mapping_list_add_mapping_sorted(list, memory_mapping);
}

static inline bool mapping_contiguous(MemoryMapping *map,
                                      target_phys_addr_t phys_addr,
                                      target_phys_addr_t virt_addr)
{
    return phys_addr == map->phys_addr + map->length &&
           virt_addr == map->virt_addr + map->length;
}

/*
 * [map->phys_addr, map->phys_addr + map->length) and
 * [phys_addr, phys_addr + length) have intersection?
 */
static inline bool mapping_have_same_region(MemoryMapping *map,
                                            target_phys_addr_t phys_addr,
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
                                    target_phys_addr_t phys_addr,
                                    target_phys_addr_t virt_addr)
{
    return virt_addr - map->virt_addr != phys_addr - map->phys_addr;
}

/*
 * [map->virt_addr, map->virt_addr + map->length) and
 * [virt_addr, virt_addr + length) have intersection. And the physical address
 * in the intersection are the same.
 */
static inline void mapping_merge(MemoryMapping *map,
                                 target_phys_addr_t virt_addr,
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
                                          target_phys_addr_t phys_addr,
                                          target_phys_addr_t virt_addr,
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

static CPUArchState *find_paging_enabled_cpu(CPUArchState *start_cpu)
{
    CPUArchState *env;

    for (env = start_cpu; env != NULL; env = env->next_cpu) {
        if (cpu_paging_enabled(env)) {
            return env;
        }
    }

    return NULL;
}

int qemu_get_guest_memory_mapping(MemoryMappingList *list)
{
    CPUArchState *env, *first_paging_enabled_cpu;
    RAMBlock *block;
    ram_addr_t offset, length;
    int ret;

    first_paging_enabled_cpu = find_paging_enabled_cpu(first_cpu);
    if (first_paging_enabled_cpu) {
        for (env = first_paging_enabled_cpu; env != NULL; env = env->next_cpu) {
            ret = cpu_get_memory_mapping(list, env);
            if (ret < 0) {
                return -1;
            }
        }
        return 0;
    }

    /*
     * If the guest doesn't use paging, the virtual address is equal to physical
     * address.
     */
    QLIST_FOREACH(block, &ram_list.blocks, next) {
        offset = block->offset;
        length = block->length;
        create_new_memory_mapping(list, offset, offset, length);
    }

    return 0;
}

void qemu_get_guest_simple_memory_mapping(MemoryMappingList *list)
{
    RAMBlock *block;

    QLIST_FOREACH(block, &ram_list.blocks, next) {
        create_new_memory_mapping(list, block->offset, 0, block->length);
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
