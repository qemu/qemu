/*
 * Thread-safe guest to host memory mapping
 *
 * Copyright 2012 Red Hat, Inc. and/or its affiliates
 *
 * Authors:
 *   Stefan Hajnoczi <stefanha@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "exec/address-spaces.h"
#include "hw/virtio/dataplane/hostmem.h"

static int hostmem_lookup_cmp(const void *phys_, const void *region_)
{
    hwaddr phys = *(const hwaddr *)phys_;
    const HostMemRegion *region = region_;

    if (phys < region->guest_addr) {
        return -1;
    } else if (phys >= region->guest_addr + region->size) {
        return 1;
    } else {
        return 0;
    }
}

/**
 * Map guest physical address to host pointer
 */
void *hostmem_lookup(HostMem *hostmem, hwaddr phys, hwaddr len, bool is_write)
{
    HostMemRegion *region;
    void *host_addr = NULL;
    hwaddr offset_within_region;

    qemu_mutex_lock(&hostmem->current_regions_lock);
    region = bsearch(&phys, hostmem->current_regions,
                     hostmem->num_current_regions,
                     sizeof(hostmem->current_regions[0]),
                     hostmem_lookup_cmp);
    if (!region) {
        goto out;
    }
    if (is_write && region->readonly) {
        goto out;
    }
    offset_within_region = phys - region->guest_addr;
    if (len <= region->size - offset_within_region) {
        host_addr = region->host_addr + offset_within_region;
    }
out:
    qemu_mutex_unlock(&hostmem->current_regions_lock);

    return host_addr;
}

/**
 * Install new regions list
 */
static void hostmem_listener_commit(MemoryListener *listener)
{
    HostMem *hostmem = container_of(listener, HostMem, listener);

    qemu_mutex_lock(&hostmem->current_regions_lock);
    g_free(hostmem->current_regions);
    hostmem->current_regions = hostmem->new_regions;
    hostmem->num_current_regions = hostmem->num_new_regions;
    qemu_mutex_unlock(&hostmem->current_regions_lock);

    /* Reset new regions list */
    hostmem->new_regions = NULL;
    hostmem->num_new_regions = 0;
}

/**
 * Add a MemoryRegionSection to the new regions list
 */
static void hostmem_append_new_region(HostMem *hostmem,
                                      MemoryRegionSection *section)
{
    void *ram_ptr = memory_region_get_ram_ptr(section->mr);
    size_t num = hostmem->num_new_regions;
    size_t new_size = (num + 1) * sizeof(hostmem->new_regions[0]);

    hostmem->new_regions = g_realloc(hostmem->new_regions, new_size);
    hostmem->new_regions[num] = (HostMemRegion){
        .host_addr = ram_ptr + section->offset_within_region,
        .guest_addr = section->offset_within_address_space,
        .size = section->size,
        .readonly = section->readonly,
    };
    hostmem->num_new_regions++;
}

static void hostmem_listener_append_region(MemoryListener *listener,
                                           MemoryRegionSection *section)
{
    HostMem *hostmem = container_of(listener, HostMem, listener);

    /* Ignore non-RAM regions, we may not be able to map them */
    if (!memory_region_is_ram(section->mr)) {
        return;
    }

    /* Ignore regions with dirty logging, we cannot mark them dirty */
    if (memory_region_is_logging(section->mr)) {
        return;
    }

    hostmem_append_new_region(hostmem, section);
}

/* We don't implement most MemoryListener callbacks, use these nop stubs */
static void hostmem_listener_dummy(MemoryListener *listener)
{
}

static void hostmem_listener_section_dummy(MemoryListener *listener,
                                           MemoryRegionSection *section)
{
}

static void hostmem_listener_eventfd_dummy(MemoryListener *listener,
                                           MemoryRegionSection *section,
                                           bool match_data, uint64_t data,
                                           EventNotifier *e)
{
}

static void hostmem_listener_coalesced_mmio_dummy(MemoryListener *listener,
                                                  MemoryRegionSection *section,
                                                  hwaddr addr, hwaddr len)
{
}

void hostmem_init(HostMem *hostmem)
{
    memset(hostmem, 0, sizeof(*hostmem));

    qemu_mutex_init(&hostmem->current_regions_lock);

    hostmem->listener = (MemoryListener){
        .begin = hostmem_listener_dummy,
        .commit = hostmem_listener_commit,
        .region_add = hostmem_listener_append_region,
        .region_del = hostmem_listener_section_dummy,
        .region_nop = hostmem_listener_append_region,
        .log_start = hostmem_listener_section_dummy,
        .log_stop = hostmem_listener_section_dummy,
        .log_sync = hostmem_listener_section_dummy,
        .log_global_start = hostmem_listener_dummy,
        .log_global_stop = hostmem_listener_dummy,
        .eventfd_add = hostmem_listener_eventfd_dummy,
        .eventfd_del = hostmem_listener_eventfd_dummy,
        .coalesced_mmio_add = hostmem_listener_coalesced_mmio_dummy,
        .coalesced_mmio_del = hostmem_listener_coalesced_mmio_dummy,
        .priority = 10,
    };

    memory_listener_register(&hostmem->listener, &address_space_memory);
    if (hostmem->num_new_regions > 0) {
        hostmem_listener_commit(&hostmem->listener);
    }
}

void hostmem_finalize(HostMem *hostmem)
{
    memory_listener_unregister(&hostmem->listener);
    g_free(hostmem->new_regions);
    g_free(hostmem->current_regions);
    qemu_mutex_destroy(&hostmem->current_regions_lock);
}
