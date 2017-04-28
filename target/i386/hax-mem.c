/*
 * HAX memory mapping operations
 *
 * Copyright (c) 2015-16 Intel Corporation
 * Copyright 2016 Google, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/address-spaces.h"
#include "exec/exec-all.h"

#include "target/i386/hax-i386.h"
#include "qemu/queue.h"

#define DEBUG_HAX_MEM 0

#define DPRINTF(fmt, ...) \
    do { \
        if (DEBUG_HAX_MEM) { \
            fprintf(stdout, fmt, ## __VA_ARGS__); \
        } \
    } while (0)

/**
 * HAXMapping: describes a pending guest physical memory mapping
 *
 * @start_pa: a guest physical address marking the start of the region; must be
 *            page-aligned
 * @size: a guest physical address marking the end of the region; must be
 *          page-aligned
 * @host_va: the host virtual address of the start of the mapping
 * @flags: mapping parameters e.g. HAX_RAM_INFO_ROM or HAX_RAM_INFO_INVALID
 * @entry: additional fields for linking #HAXMapping instances together
 */
typedef struct HAXMapping {
    uint64_t start_pa;
    uint32_t size;
    uint64_t host_va;
    int flags;
    QTAILQ_ENTRY(HAXMapping) entry;
} HAXMapping;

/*
 * A doubly-linked list (actually a tail queue) of the pending page mappings
 * for the ongoing memory transaction.
 *
 * It is used to optimize the number of page mapping updates done through the
 * kernel module. For example, it's effective when a driver is digging an MMIO
 * hole inside an existing memory mapping. It will get a deletion of the whole
 * region, then the addition of the 2 remaining RAM areas around the hole and
 * finally the memory transaction commit. During the commit, it will effectively
 * send to the kernel only the removal of the pages from the MMIO hole after
 * having computed locally the result of the deletion and additions.
 */
static QTAILQ_HEAD(HAXMappingListHead, HAXMapping) mappings =
    QTAILQ_HEAD_INITIALIZER(mappings);

/**
 * hax_mapping_dump_list: dumps @mappings to stdout (for debugging)
 */
static void hax_mapping_dump_list(void)
{
    HAXMapping *entry;

    DPRINTF("%s updates:\n", __func__);
    QTAILQ_FOREACH(entry, &mappings, entry) {
        DPRINTF("\t%c 0x%016" PRIx64 "->0x%016" PRIx64 " VA 0x%016" PRIx64
                "%s\n", entry->flags & HAX_RAM_INFO_INVALID ? '-' : '+',
                entry->start_pa, entry->start_pa + entry->size, entry->host_va,
                entry->flags & HAX_RAM_INFO_ROM ? " ROM" : "");
    }
}

static void hax_insert_mapping_before(HAXMapping *next, uint64_t start_pa,
                                      uint32_t size, uint64_t host_va,
                                      uint8_t flags)
{
    HAXMapping *entry;

    entry = g_malloc0(sizeof(*entry));
    entry->start_pa = start_pa;
    entry->size = size;
    entry->host_va = host_va;
    entry->flags = flags;
    if (!next) {
        QTAILQ_INSERT_TAIL(&mappings, entry, entry);
    } else {
        QTAILQ_INSERT_BEFORE(next, entry, entry);
    }
}

static bool hax_mapping_is_opposite(HAXMapping *entry, uint64_t host_va,
                                    uint8_t flags)
{
    /* removed then added without change for the read-only flag */
    bool nop_flags = (entry->flags ^ flags) == HAX_RAM_INFO_INVALID;

    return (entry->host_va == host_va) && nop_flags;
}

static void hax_update_mapping(uint64_t start_pa, uint32_t size,
                               uint64_t host_va, uint8_t flags)
{
    uint64_t end_pa = start_pa + size;
    HAXMapping *entry, *next;

    QTAILQ_FOREACH_SAFE(entry, &mappings, entry, next) {
        uint32_t chunk_sz;
        if (start_pa >= entry->start_pa + entry->size) {
            continue;
        }
        if (start_pa < entry->start_pa) {
            chunk_sz = end_pa <= entry->start_pa ? size
                                                 : entry->start_pa - start_pa;
            hax_insert_mapping_before(entry, start_pa, chunk_sz,
                                      host_va, flags);
            start_pa += chunk_sz;
            host_va += chunk_sz;
            size -= chunk_sz;
        } else if (start_pa > entry->start_pa) {
            /* split the existing chunk at start_pa */
            chunk_sz = start_pa - entry->start_pa;
            hax_insert_mapping_before(entry, entry->start_pa, chunk_sz,
                                      entry->host_va, entry->flags);
            entry->start_pa += chunk_sz;
            entry->host_va += chunk_sz;
            entry->size -= chunk_sz;
        }
        /* now start_pa == entry->start_pa */
        chunk_sz = MIN(size, entry->size);
        if (chunk_sz) {
            bool nop = hax_mapping_is_opposite(entry, host_va, flags);
            bool partial = chunk_sz < entry->size;
            if (partial) {
                /* remove the beginning of the existing chunk */
                entry->start_pa += chunk_sz;
                entry->host_va += chunk_sz;
                entry->size -= chunk_sz;
                if (!nop) {
                    hax_insert_mapping_before(entry, start_pa, chunk_sz,
                                              host_va, flags);
                }
            } else { /* affects the full mapping entry */
                if (nop) { /* no change to this mapping, remove it */
                    QTAILQ_REMOVE(&mappings, entry, entry);
                    g_free(entry);
                } else { /* update mapping properties */
                    entry->host_va = host_va;
                    entry->flags = flags;
                }
            }
            start_pa += chunk_sz;
            host_va += chunk_sz;
            size -= chunk_sz;
        }
        if (!size) { /* we are done */
            break;
        }
    }
    if (size) { /* add the leftover */
        hax_insert_mapping_before(NULL, start_pa, size, host_va, flags);
    }
}

static void hax_process_section(MemoryRegionSection *section, uint8_t flags)
{
    MemoryRegion *mr = section->mr;
    hwaddr start_pa = section->offset_within_address_space;
    ram_addr_t size = int128_get64(section->size);
    unsigned int delta;
    uint64_t host_va;

    /* We only care about RAM and ROM regions */
    if (!memory_region_is_ram(mr)) {
        if (memory_region_is_romd(mr)) {
            /* HAXM kernel module does not support ROMD yet  */
            fprintf(stderr, "%s: Warning: Ignoring ROMD region 0x%016" PRIx64
                    "->0x%016" PRIx64 "\n", __func__, start_pa,
                    start_pa + size);
        }
        return;
    }

    /* Adjust start_pa and size so that they are page-aligned. (Cf
     * kvm_set_phys_mem() in kvm-all.c).
     */
    delta = qemu_real_host_page_size - (start_pa & ~qemu_real_host_page_mask);
    delta &= ~qemu_real_host_page_mask;
    if (delta > size) {
        return;
    }
    start_pa += delta;
    size -= delta;
    size &= qemu_real_host_page_mask;
    if (!size || (start_pa & ~qemu_real_host_page_mask)) {
        return;
    }

    host_va = (uintptr_t)memory_region_get_ram_ptr(mr)
            + section->offset_within_region + delta;
    if (memory_region_is_rom(section->mr)) {
        flags |= HAX_RAM_INFO_ROM;
    }

    /* the kernel module interface uses 32-bit sizes (but we could split...) */
    g_assert(size <= UINT32_MAX);

    hax_update_mapping(start_pa, size, host_va, flags);
}

static void hax_region_add(MemoryListener *listener,
                           MemoryRegionSection *section)
{
    memory_region_ref(section->mr);
    hax_process_section(section, 0);
}

static void hax_region_del(MemoryListener *listener,
                           MemoryRegionSection *section)
{
    hax_process_section(section, HAX_RAM_INFO_INVALID);
    memory_region_unref(section->mr);
}

static void hax_transaction_begin(MemoryListener *listener)
{
    g_assert(QTAILQ_EMPTY(&mappings));
}

static void hax_transaction_commit(MemoryListener *listener)
{
    if (!QTAILQ_EMPTY(&mappings)) {
        HAXMapping *entry, *next;

        if (DEBUG_HAX_MEM) {
            hax_mapping_dump_list();
        }
        QTAILQ_FOREACH_SAFE(entry, &mappings, entry, next) {
            if (entry->flags & HAX_RAM_INFO_INVALID) {
                /* for unmapping, put the values expected by the kernel */
                entry->flags = HAX_RAM_INFO_INVALID;
                entry->host_va = 0;
            }
            if (hax_set_ram(entry->start_pa, entry->size,
                            entry->host_va, entry->flags)) {
                fprintf(stderr, "%s: Failed mapping @0x%016" PRIx64 "+0x%"
                        PRIx32 " flags %02x\n", __func__, entry->start_pa,
                        entry->size, entry->flags);
            }
            QTAILQ_REMOVE(&mappings, entry, entry);
            g_free(entry);
        }
    }
}

/* currently we fake the dirty bitmap sync, always dirty */
static void hax_log_sync(MemoryListener *listener,
                         MemoryRegionSection *section)
{
    MemoryRegion *mr = section->mr;

    if (!memory_region_is_ram(mr)) {
        /* Skip MMIO regions */
        return;
    }

    memory_region_set_dirty(mr, 0, int128_get64(section->size));
}

static MemoryListener hax_memory_listener = {
    .begin = hax_transaction_begin,
    .commit = hax_transaction_commit,
    .region_add = hax_region_add,
    .region_del = hax_region_del,
    .log_sync = hax_log_sync,
    .priority = 10,
};

static void hax_ram_block_added(RAMBlockNotifier *n, void *host, size_t size)
{
    /*
     * In HAX, QEMU allocates the virtual address, and HAX kernel
     * populates the memory with physical memory. Currently we have no
     * paging, so user should make sure enough free memory in advance.
     */
    if (hax_populate_ram((uint64_t)(uintptr_t)host, size) < 0) {
        fprintf(stderr, "HAX failed to populate RAM");
        abort();
    }
}

static struct RAMBlockNotifier hax_ram_notifier = {
    .ram_block_added = hax_ram_block_added,
};

void hax_memory_init(void)
{
    ram_block_notifier_add(&hax_ram_notifier);
    memory_listener_register(&hax_memory_listener, &address_space_memory);
}
