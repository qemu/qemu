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
#ifndef QEMU_RAM_H
#define QEMU_RAM_H

#include "sysemu/sysemu.h" /* for MAX_NODES */
#include "qom/object.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "qemu/option.h"
#include "qemu/bitmap.h"

#define TYPE_MEMORY_BACKEND "memory-backend"
#define MEMORY_BACKEND(obj) \
    OBJECT_CHECK(HostMemoryBackend, (obj), TYPE_MEMORY_BACKEND)
#define MEMORY_BACKEND_GET_CLASS(obj) \
    OBJECT_GET_CLASS(HostMemoryBackendClass, (obj), TYPE_MEMORY_BACKEND)
#define MEMORY_BACKEND_CLASS(klass) \
    OBJECT_CLASS_CHECK(HostMemoryBackendClass, (klass), TYPE_MEMORY_BACKEND)

typedef struct HostMemoryBackend HostMemoryBackend;
typedef struct HostMemoryBackendClass HostMemoryBackendClass;

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
 * @id: unique identification string in memdev namespace
 * @mr: MemoryRegion representing host memory belonging to backend
 */
struct HostMemoryBackend {
    /* private */
    Object parent;

    /* protected */
    uint64_t size;
    bool merge, dump;
    bool prealloc, force_prealloc;
    DECLARE_BITMAP(host_nodes, MAX_NODES + 1);
    HostMemPolicy policy;

    MemoryRegion mr;
};

MemoryRegion *host_memory_backend_get_memory(HostMemoryBackend *backend,
                                             Error **errp);

#endif
