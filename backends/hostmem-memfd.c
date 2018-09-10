/*
 * QEMU host memfd memory backend
 *
 * Copyright (C) 2018 Red Hat Inc
 *
 * Authors:
 *   Marc-André Lureau <marcandre.lureau@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "qemu-common.h"
#include "sysemu/hostmem.h"
#include "sysemu/sysemu.h"
#include "qom/object_interfaces.h"
#include "qemu/memfd.h"
#include "qapi/error.h"

#define TYPE_MEMORY_BACKEND_MEMFD "memory-backend-memfd"

#define MEMORY_BACKEND_MEMFD(obj)                                        \
    OBJECT_CHECK(HostMemoryBackendMemfd, (obj), TYPE_MEMORY_BACKEND_MEMFD)

typedef struct HostMemoryBackendMemfd HostMemoryBackendMemfd;

struct HostMemoryBackendMemfd {
    HostMemoryBackend parent_obj;

    bool hugetlb;
    uint64_t hugetlbsize;
    bool seal;
};

static void
memfd_backend_memory_alloc(HostMemoryBackend *backend, Error **errp)
{
    HostMemoryBackendMemfd *m = MEMORY_BACKEND_MEMFD(backend);
    char *name;
    int fd;

    if (!backend->size) {
        error_setg(errp, "can't create backend with size 0");
        return;
    }

    backend->force_prealloc = mem_prealloc;
    fd = qemu_memfd_create(TYPE_MEMORY_BACKEND_MEMFD, backend->size,
                           m->hugetlb, m->hugetlbsize, m->seal ?
                           F_SEAL_GROW | F_SEAL_SHRINK | F_SEAL_SEAL : 0,
                           errp);
    if (fd == -1) {
        return;
    }

    name = object_get_canonical_path(OBJECT(backend));
    memory_region_init_ram_from_fd(&backend->mr, OBJECT(backend),
                                   name, backend->size,
                                   backend->share, fd, errp);
    g_free(name);
}

static bool
memfd_backend_get_hugetlb(Object *o, Error **errp)
{
    return MEMORY_BACKEND_MEMFD(o)->hugetlb;
}

static void
memfd_backend_set_hugetlb(Object *o, bool value, Error **errp)
{
    MEMORY_BACKEND_MEMFD(o)->hugetlb = value;
}

static void
memfd_backend_set_hugetlbsize(Object *obj, Visitor *v, const char *name,
                              void *opaque, Error **errp)
{
    HostMemoryBackendMemfd *m = MEMORY_BACKEND_MEMFD(obj);
    Error *local_err = NULL;
    uint64_t value;

    if (host_memory_backend_mr_inited(MEMORY_BACKEND(obj))) {
        error_setg(&local_err, "cannot change property value");
        goto out;
    }

    visit_type_size(v, name, &value, &local_err);
    if (local_err) {
        goto out;
    }
    if (!value) {
        error_setg(&local_err, "Property '%s.%s' doesn't take value '%"
                   PRIu64 "'", object_get_typename(obj), name, value);
        goto out;
    }
    m->hugetlbsize = value;
out:
    error_propagate(errp, local_err);
}

static void
memfd_backend_get_hugetlbsize(Object *obj, Visitor *v, const char *name,
                              void *opaque, Error **errp)
{
    HostMemoryBackendMemfd *m = MEMORY_BACKEND_MEMFD(obj);
    uint64_t value = m->hugetlbsize;

    visit_type_size(v, name, &value, errp);
}

static bool
memfd_backend_get_seal(Object *o, Error **errp)
{
    return MEMORY_BACKEND_MEMFD(o)->seal;
}

static void
memfd_backend_set_seal(Object *o, bool value, Error **errp)
{
    MEMORY_BACKEND_MEMFD(o)->seal = value;
}

static void
memfd_backend_instance_init(Object *obj)
{
    HostMemoryBackendMemfd *m = MEMORY_BACKEND_MEMFD(obj);

    /* default to sealed file */
    m->seal = true;
    MEMORY_BACKEND(m)->share = true;
}

static void
memfd_backend_class_init(ObjectClass *oc, void *data)
{
    HostMemoryBackendClass *bc = MEMORY_BACKEND_CLASS(oc);

    bc->alloc = memfd_backend_memory_alloc;

    if (qemu_memfd_check(MFD_HUGETLB)) {
        object_class_property_add_bool(oc, "hugetlb",
                                       memfd_backend_get_hugetlb,
                                       memfd_backend_set_hugetlb,
                                       &error_abort);
        object_class_property_set_description(oc, "hugetlb",
                                              "Use huge pages",
                                              &error_abort);
        object_class_property_add(oc, "hugetlbsize", "int",
                                  memfd_backend_get_hugetlbsize,
                                  memfd_backend_set_hugetlbsize,
                                  NULL, NULL, &error_abort);
        object_class_property_set_description(oc, "hugetlbsize",
                                              "Huge pages size (ex: 2M, 1G)",
                                              &error_abort);
    }
    if (qemu_memfd_check(MFD_ALLOW_SEALING)) {
        object_class_property_add_bool(oc, "seal",
                                       memfd_backend_get_seal,
                                       memfd_backend_set_seal,
                                       &error_abort);
        object_class_property_set_description(oc, "seal",
                                              "Seal growing & shrinking",
                                              &error_abort);
    }
}

static const TypeInfo memfd_backend_info = {
    .name = TYPE_MEMORY_BACKEND_MEMFD,
    .parent = TYPE_MEMORY_BACKEND,
    .instance_init = memfd_backend_instance_init,
    .class_init = memfd_backend_class_init,
    .instance_size = sizeof(HostMemoryBackendMemfd),
};

static void register_types(void)
{
    if (qemu_memfd_check(0)) {
        type_register_static(&memfd_backend_info);
    }
}

type_init(register_types);
