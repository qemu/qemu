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

#ifndef MEMORY_MAPPING_H
#define MEMORY_MAPPING_H

#include "qemu/queue.h"
#include "exec/cpu-defs.h"
#include "exec/memory.h"

typedef struct GuestPhysBlock {
    /* visible to guest, reflects PCI hole, etc */
    hwaddr target_start;

    /* implies size */
    hwaddr target_end;

    /* points into host memory */
    uint8_t *host_addr;

    /* points to the MemoryRegion that this block belongs to */
    MemoryRegion *mr;

    QTAILQ_ENTRY(GuestPhysBlock) next;
} GuestPhysBlock;

/* point-in-time snapshot of guest-visible physical mappings */
typedef struct GuestPhysBlockList {
    unsigned num;
    QTAILQ_HEAD(, GuestPhysBlock) head;
} GuestPhysBlockList;

/* The physical and virtual address in the memory mapping are contiguous. */
typedef struct MemoryMapping {
    hwaddr phys_addr;
    target_ulong virt_addr;
    ram_addr_t length;
    QTAILQ_ENTRY(MemoryMapping) next;
} MemoryMapping;

struct MemoryMappingList {
    unsigned int num;
    MemoryMapping *last_mapping;
    QTAILQ_HEAD(, MemoryMapping) head;
};

/*
 * add or merge the memory region [phys_addr, phys_addr + length) into the
 * memory mapping's list. The region's virtual address starts with virt_addr,
 * and is contiguous. The list is sorted by phys_addr.
 */
void memory_mapping_list_add_merge_sorted(MemoryMappingList *list,
                                          hwaddr phys_addr,
                                          hwaddr virt_addr,
                                          ram_addr_t length);

void memory_mapping_list_free(MemoryMappingList *list);

void memory_mapping_list_init(MemoryMappingList *list);

void guest_phys_blocks_free(GuestPhysBlockList *list);
void guest_phys_blocks_init(GuestPhysBlockList *list);
void guest_phys_blocks_append(GuestPhysBlockList *list);

void qemu_get_guest_memory_mapping(MemoryMappingList *list,
                                   const GuestPhysBlockList *guest_phys_blocks,
                                   Error **errp);

/* get guest's memory mapping without do paging(virtual address is 0). */
void qemu_get_guest_simple_memory_mapping(MemoryMappingList *list,
                                  const GuestPhysBlockList *guest_phys_blocks);

void memory_mapping_filter(MemoryMappingList *list, int64_t begin,
                           int64_t length);

#endif
