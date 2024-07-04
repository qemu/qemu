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
#include "sysemu/hostmem.h"
#include "qapi/error.h"

#define TYPE_MEMORY_BACKEND_SHM "memory-backend-shm"

OBJECT_DECLARE_SIMPLE_TYPE(HostMemoryBackendShm, MEMORY_BACKEND_SHM)

struct HostMemoryBackendShm {
    HostMemoryBackend parent_obj;
};

static bool
shm_backend_memory_alloc(HostMemoryBackend *backend, Error **errp)
{
    g_autoptr(GString) shm_name = g_string_new(NULL);
    g_autofree char *backend_name = NULL;
    uint32_t ram_flags;
    int fd, oflag;
    mode_t mode;

    if (!backend->size) {
        error_setg(errp, "can't create shm backend with size 0");
        return false;
    }

    if (!backend->share) {
        error_setg(errp, "can't create shm backend with `share=off`");
        return false;
    }

    /*
     * Let's use `mode = 0` because we don't want other processes to open our
     * memory unless we share the file descriptor with them.
     */
    mode = 0;
    oflag = O_RDWR | O_CREAT | O_EXCL;
    backend_name = host_memory_backend_get_name(backend);

    /*
     * Some operating systems allow creating anonymous POSIX shared memory
     * objects (e.g. FreeBSD provides the SHM_ANON constant), but this is not
     * defined by POSIX, so let's create a unique name.
     *
     * From Linux's shm_open(3) man-page:
     *   For  portable  use,  a shared  memory  object should be identified
     *   by a name of the form /somename;"
     */
    g_string_printf(shm_name, "/qemu-" FMT_pid "-shm-%s", getpid(),
                    backend_name);

    fd = shm_open(shm_name->str, oflag, mode);
    if (fd < 0) {
        error_setg_errno(errp, errno,
                         "failed to create POSIX shared memory");
        return false;
    }

    /*
     * We have the file descriptor, so we no longer need to expose the
     * POSIX shared memory object. However it will remain allocated as long as
     * there are file descriptors pointing to it.
     */
    shm_unlink(shm_name->str);

    if (ftruncate(fd, backend->size) == -1) {
        error_setg_errno(errp, errno,
                         "failed to resize POSIX shared memory to %" PRIu64,
                         backend->size);
        close(fd);
        return false;
    }

    ram_flags = RAM_SHARED;
    ram_flags |= backend->reserve ? 0 : RAM_NORESERVE;

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
shm_backend_class_init(ObjectClass *oc, void *data)
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
