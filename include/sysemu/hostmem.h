/*
 * QEMU Host Memory Backend
 *
 * Copyright (C) 2013-2014 Red Hat Inc
 *
 * Authors:
 *   Igor Mammedov <imammedo@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef SYSEMU_HOSTMEM_H
#define SYSEMU_HOSTMEM_H

#include "sysemu/numa.h"
#include "qapi/qapi-types-machine.h"
#include "qom/object.h"
#include "exec/memory.h"
#include "qemu/bitmap.h"

#define TYPE_MEMORY_BACKEND "memory-backend"
OBJECT_DECLARE_TYPE(HostMemoryBackend, HostMemoryBackendClass,
                    MEMORY_BACKEND)

/* hostmem-ram.c */
/**
 * @TYPE_MEMORY_BACKEND_RAM:
 * name of backend that uses mmap on the anonymous RAM
 */

#define TYPE_MEMORY_BACKEND_RAM "memory-backend-ram"

/* hostmem-file.c */
/**
 * @TYPE_MEMORY_BACKEND_FILE:
 * name of backend that uses mmap on a file descriptor
 */
#define TYPE_MEMORY_BACKEND_FILE "memory-backend-file"


/**
 * HostMemoryBackendClass:
 * @parent_class: opaque parent class container
 */
struct HostMemoryBackendClass {
    ObjectClass parent_class;

    void (*alloc)(HostMemoryBackend *backend, Error **errp);
};

/**
 * @HostMemoryBackend
 *
 * @parent: opaque parent object container
 * @size: amount of memory backend provides
 * @mr: MemoryRegion representing host memory belonging to backend
 * @prealloc_threads: number of threads to be used for preallocatining RAM
 */
struct HostMemoryBackend {
    /* private */
    Object parent;

    /* protected */
    uint64_t size;
    bool merge, dump, use_canonical_path;
    bool prealloc, is_mapped, share, reserve;
    uint32_t prealloc_threads;
    DECLARE_BITMAP(host_nodes, MAX_NODES + 1);
    HostMemPolicy policy;

    MemoryRegion mr;
};

bool host_memory_backend_mr_inited(HostMemoryBackend *backend);
MemoryRegion *host_memory_backend_get_memory(HostMemoryBackend *backend);

void host_memory_backend_set_mapped(HostMemoryBackend *backend, bool mapped);
bool host_memory_backend_is_mapped(HostMemoryBackend *backend);
size_t host_memory_backend_pagesize(HostMemoryBackend *memdev);
char *host_memory_backend_get_name(HostMemoryBackend *backend);

#endif
