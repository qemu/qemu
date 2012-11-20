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

#ifndef HOSTMEM_H
#define HOSTMEM_H

#include "exec/memory.h"
#include "qemu/thread.h"

typedef struct {
    void *host_addr;
    hwaddr guest_addr;
    uint64_t size;
    bool readonly;
} HostMemRegion;

typedef struct {
    /* The listener is invoked when regions change and a new list of regions is
     * built up completely before they are installed.
     */
    MemoryListener listener;
    HostMemRegion *new_regions;
    size_t num_new_regions;

    /* Current regions are accessed from multiple threads either to lookup
     * addresses or to install a new list of regions.  The lock protects the
     * pointer and the regions.
     */
    QemuMutex current_regions_lock;
    HostMemRegion *current_regions;
    size_t num_current_regions;
} HostMem;

void hostmem_init(HostMem *hostmem);
void hostmem_finalize(HostMem *hostmem);

/**
 * Map a guest physical address to a pointer
 *
 * Note that there is map/unmap mechanism here.  The caller must ensure that
 * mapped memory is no longer used across events like hot memory unplug.  This
 * can be done with other mechanisms like bdrv_drain_all() that quiesce
 * in-flight I/O.
 */
void *hostmem_lookup(HostMem *hostmem, hwaddr phys, hwaddr len, bool is_write);

#endif /* HOSTMEM_H */
