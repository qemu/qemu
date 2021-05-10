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
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "sysemu/hostmem.h"
#include "qom/object_interfaces.h"
#include "qom/object.h"

OBJECT_DECLARE_SIMPLE_TYPE(HostMemoryBackendFile, MEMORY_BACKEND_FILE)


struct HostMemoryBackendFile {
    HostMemoryBackend parent_obj;

    char *mem_path;
    uint64_t align;
    bool discard_data;
    bool is_pmem;
    bool readonly;
};

static void
file_backend_memory_alloc(HostMemoryBackend *backend, Error **errp)
{
#ifndef CONFIG_POSIX
    error_setg(errp, "backend '%s' not supported on this host",
               object_get_typename(OBJECT(backend)));
#else
    HostMemoryBackendFile *fb = MEMORY_BACKEND_FILE(backend);
    uint32_t ram_flags;
    gchar *name;

    if (!backend->size) {
        error_setg(errp, "can't create backend with size 0");
        return;
    }
    if (!fb->mem_path) {
        error_setg(errp, "mem-path property not set");
        return;
    }

    name = host_memory_backend_get_name(backend);
    ram_flags = backend->share ? RAM_SHARED : 0;
    ram_flags |= backend->reserve ? 0 : RAM_NORESERVE;
    ram_flags |= fb->is_pmem ? RAM_PMEM : 0;
    memory_region_init_ram_from_file(&backend->mr, OBJECT(backend), name,
                                     backend->size, fb->align, ram_flags,
                                     fb->mem_path, fb->readonly, errp);
    g_free(name);
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
        error_setg(errp, "cannot change property 'mem-path' of %s",
                   object_get_typename(o));
        return;
    }
    g_free(fb->mem_path);
    fb->mem_path = g_strdup(str);
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

static void file_memory_backend_get_align(Object *o, Visitor *v,
                                          const char *name, void *opaque,
                                          Error **errp)
{
    HostMemoryBackendFile *fb = MEMORY_BACKEND_FILE(o);
    uint64_t val = fb->align;

    visit_type_size(v, name, &val, errp);
}

static void file_memory_backend_set_align(Object *o, Visitor *v,
                                          const char *name, void *opaque,
                                          Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(o);
    HostMemoryBackendFile *fb = MEMORY_BACKEND_FILE(o);
    uint64_t val;

    if (host_memory_backend_mr_inited(backend)) {
        error_setg(errp, "cannot change property '%s' of %s", name,
                   object_get_typename(o));
        return;
    }

    if (!visit_type_size(v, name, &val, errp)) {
        return;
    }
    fb->align = val;
}

#ifdef CONFIG_LIBPMEM
static bool file_memory_backend_get_pmem(Object *o, Error **errp)
{
    return MEMORY_BACKEND_FILE(o)->is_pmem;
}

static void file_memory_backend_set_pmem(Object *o, bool value, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(o);
    HostMemoryBackendFile *fb = MEMORY_BACKEND_FILE(o);

    if (host_memory_backend_mr_inited(backend)) {
        error_setg(errp, "cannot change property 'pmem' of %s.",
                   object_get_typename(o));
        return;
    }

    fb->is_pmem = value;
}
#endif /* CONFIG_LIBPMEM */

static bool file_memory_backend_get_readonly(Object *obj, Error **errp)
{
    HostMemoryBackendFile *fb = MEMORY_BACKEND_FILE(obj);

    return fb->readonly;
}

static void file_memory_backend_set_readonly(Object *obj, bool value,
                                             Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);
    HostMemoryBackendFile *fb = MEMORY_BACKEND_FILE(obj);

    if (host_memory_backend_mr_inited(backend)) {
        error_setg(errp, "cannot change property 'readonly' of %s.",
                   object_get_typename(obj));
        return;
    }

    fb->readonly = value;
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

    object_class_property_add_bool(oc, "discard-data",
        file_memory_backend_get_discard_data, file_memory_backend_set_discard_data);
    object_class_property_add_str(oc, "mem-path",
        get_mem_path, set_mem_path);
    object_class_property_add(oc, "align", "int",
        file_memory_backend_get_align,
        file_memory_backend_set_align,
        NULL, NULL);
#ifdef CONFIG_LIBPMEM
    object_class_property_add_bool(oc, "pmem",
        file_memory_backend_get_pmem, file_memory_backend_set_pmem);
#endif
    object_class_property_add_bool(oc, "readonly",
        file_memory_backend_get_readonly,
        file_memory_backend_set_readonly);
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
