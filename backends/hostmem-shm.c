/*
 * QEMU host POSIX shared memory object backend
 *
 * Copyright (C) 2024 Red Hat Inc
 *
 * Authors:
 *   Stefano Garzarella <sgarzare@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "system/hostmem.h"
#include "qapi/error.h"
#include "migration/cpr.h"

#define TYPE_MEMORY_BACKEND_SHM "memory-backend-shm"

OBJECT_DECLARE_SIMPLE_TYPE(HostMemoryBackendShm, MEMORY_BACKEND_SHM)

struct HostMemoryBackendShm {
    HostMemoryBackend parent_obj;
};

static bool
shm_backend_memory_alloc(HostMemoryBackend *backend, Error **errp)
{
    g_autofree char *backend_name = host_memory_backend_get_name(backend);
    uint32_t ram_flags;
    int fd = cpr_find_fd(backend_name, 0);

    if (!backend->size) {
        error_setg(errp, "can't create shm backend with size 0");
        return false;
    }

    if (!backend->share) {
        error_setg(errp, "can't create shm backend with `share=off`");
        return false;
    }

    if (fd >= 0) {
        goto have_fd;
    }

    fd = qemu_shm_alloc(backend->size, errp);
    if (fd < 0) {
        return false;
    }
    cpr_save_fd(backend_name, 0, fd);

have_fd:
    /* Let's do the same as memory-backend-ram,share=on would do. */
    ram_flags = RAM_SHARED;
    ram_flags |= backend->reserve ? 0 : RAM_NORESERVE;
    ram_flags |= backend->guest_memfd ? RAM_GUEST_MEMFD : 0;

    return memory_region_init_ram_from_fd(&backend->mr, OBJECT(backend),
                                              backend_name, backend->size,
                                              ram_flags, fd, 0, errp);
}

static void
shm_backend_instance_init(Object *obj)
{
    HostMemoryBackendShm *m = MEMORY_BACKEND_SHM(obj);

    MEMORY_BACKEND(m)->share = true;
}

static void
shm_backend_class_init(ObjectClass *oc, const void *data)
{
    HostMemoryBackendClass *bc = MEMORY_BACKEND_CLASS(oc);

    bc->alloc = shm_backend_memory_alloc;
}

static const TypeInfo shm_backend_info = {
    .name = TYPE_MEMORY_BACKEND_SHM,
    .parent = TYPE_MEMORY_BACKEND,
    .instance_init = shm_backend_instance_init,
    .class_init = shm_backend_class_init,
    .instance_size = sizeof(HostMemoryBackendShm),
};

static void register_types(void)
{
    type_register_static(&shm_backend_info);
}

type_init(register_types);
