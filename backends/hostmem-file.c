/*
 * QEMU Host Memory Backend for hugetlbfs
 *
 * Copyright (C) 2013-2014 Red Hat Inc
 *
 * Authors:
 *   Paolo Bonzini <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "sysemu/hostmem.h"
#include "sysemu/sysemu.h"
#include "qom/object_interfaces.h"

/* hostmem-file.c */
/**
 * @TYPE_MEMORY_BACKEND_FILE:
 * name of backend that uses mmap on a file descriptor
 */
#define TYPE_MEMORY_BACKEND_FILE "memory-backend-file"

#define MEMORY_BACKEND_FILE(obj) \
    OBJECT_CHECK(HostMemoryBackendFile, (obj), TYPE_MEMORY_BACKEND_FILE)

typedef struct HostMemoryBackendFile HostMemoryBackendFile;

struct HostMemoryBackendFile {
    HostMemoryBackend parent_obj;

    bool share;
    bool discard_data;
    char *mem_path;
};

static void
file_backend_memory_alloc(HostMemoryBackend *backend, Error **errp)
{
    HostMemoryBackendFile *fb = MEMORY_BACKEND_FILE(backend);

    if (!backend->size) {
        error_setg(errp, "can't create backend with size 0");
        return;
    }
    if (!fb->mem_path) {
        error_setg(errp, "mem-path property not set");
        return;
    }
#ifndef CONFIG_LINUX
    error_setg(errp, "-mem-path not supported on this host");
#else
    if (!host_memory_backend_mr_inited(backend)) {
        gchar *path;
        backend->force_prealloc = mem_prealloc;
        path = object_get_canonical_path(OBJECT(backend));
        memory_region_init_ram_from_file(&backend->mr, OBJECT(backend),
                                 path,
                                 backend->size, fb->share,
                                 fb->mem_path, errp);
        g_free(path);
    }
#endif
}

static char *get_mem_path(Object *o, Error **errp)
{
    HostMemoryBackendFile *fb = MEMORY_BACKEND_FILE(o);

    return g_strdup(fb->mem_path);
}

static void set_mem_path(Object *o, const char *str, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(o);
    HostMemoryBackendFile *fb = MEMORY_BACKEND_FILE(o);

    if (host_memory_backend_mr_inited(backend)) {
        error_setg(errp, "cannot change property value");
        return;
    }
    g_free(fb->mem_path);
    fb->mem_path = g_strdup(str);
}

static bool file_memory_backend_get_share(Object *o, Error **errp)
{
    HostMemoryBackendFile *fb = MEMORY_BACKEND_FILE(o);

    return fb->share;
}

static void file_memory_backend_set_share(Object *o, bool value, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(o);
    HostMemoryBackendFile *fb = MEMORY_BACKEND_FILE(o);

    if (host_memory_backend_mr_inited(backend)) {
        error_setg(errp, "cannot change property value");
        return;
    }
    fb->share = value;
}

static bool file_memory_backend_get_discard_data(Object *o, Error **errp)
{
    return MEMORY_BACKEND_FILE(o)->discard_data;
}

static void file_memory_backend_set_discard_data(Object *o, bool value,
                                               Error **errp)
{
    MEMORY_BACKEND_FILE(o)->discard_data = value;
}

static void file_backend_unparent(Object *obj)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);
    HostMemoryBackendFile *fb = MEMORY_BACKEND_FILE(obj);

    if (host_memory_backend_mr_inited(backend) && fb->discard_data) {
        void *ptr = memory_region_get_ram_ptr(&backend->mr);
        uint64_t sz = memory_region_size(&backend->mr);

        qemu_madvise(ptr, sz, QEMU_MADV_REMOVE);
    }
}

static void
file_backend_class_init(ObjectClass *oc, void *data)
{
    HostMemoryBackendClass *bc = MEMORY_BACKEND_CLASS(oc);

    bc->alloc = file_backend_memory_alloc;
    oc->unparent = file_backend_unparent;

    object_class_property_add_bool(oc, "share",
        file_memory_backend_get_share, file_memory_backend_set_share,
        &error_abort);
    object_class_property_add_bool(oc, "discard-data",
        file_memory_backend_get_discard_data, file_memory_backend_set_discard_data,
        &error_abort);
    object_class_property_add_str(oc, "mem-path",
        get_mem_path, set_mem_path,
        &error_abort);
}

static void file_backend_instance_finalize(Object *o)
{
    HostMemoryBackendFile *fb = MEMORY_BACKEND_FILE(o);

    g_free(fb->mem_path);
}

static const TypeInfo file_backend_info = {
    .name = TYPE_MEMORY_BACKEND_FILE,
    .parent = TYPE_MEMORY_BACKEND,
    .class_init = file_backend_class_init,
    .instance_finalize = file_backend_instance_finalize,
    .instance_size = sizeof(HostMemoryBackendFile),
};

static void register_types(void)
{
    type_register_static(&file_backend_info);
}

type_init(register_types);
