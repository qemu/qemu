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
#include "sysemu/hostmem.h"
#include "sysemu/sysemu.h"
#include "qapi/visitor.h"
#include "qapi/qmp/qerror.h"
#include "qemu/config-file.h"
#include "qom/object_interfaces.h"

static void
host_memory_backend_get_size(Object *obj, Visitor *v, void *opaque,
                             const char *name, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);
    uint64_t value = backend->size;

    visit_type_size(v, &value, name, errp);
}

static void
host_memory_backend_set_size(Object *obj, Visitor *v, void *opaque,
                             const char *name, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);
    Error *local_err = NULL;
    uint64_t value;

    if (memory_region_size(&backend->mr)) {
        error_setg(&local_err, "cannot change property value");
        goto out;
    }

    visit_type_size(v, &value, name, &local_err);
    if (local_err) {
        goto out;
    }
    if (!value) {
        error_setg(&local_err, "Property '%s.%s' doesn't take value '%"
                   PRIu64 "'", object_get_typename(obj), name, value);
        goto out;
    }
    backend->size = value;
out:
    error_propagate(errp, local_err);
}

static void host_memory_backend_init(Object *obj)
{
    object_property_add(obj, "size", "int",
                        host_memory_backend_get_size,
                        host_memory_backend_set_size, NULL, NULL, NULL);
}

static void host_memory_backend_finalize(Object *obj)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);

    if (memory_region_size(&backend->mr)) {
        memory_region_destroy(&backend->mr);
    }
}

MemoryRegion *
host_memory_backend_get_memory(HostMemoryBackend *backend, Error **errp)
{
    return memory_region_size(&backend->mr) ? &backend->mr : NULL;
}

static void
host_memory_backend_memory_complete(UserCreatable *uc, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(uc);
    HostMemoryBackendClass *bc = MEMORY_BACKEND_GET_CLASS(uc);

    if (bc->alloc) {
        bc->alloc(backend, errp);
    }
}

static void
host_memory_backend_class_init(ObjectClass *oc, void *data)
{
    UserCreatableClass *ucc = USER_CREATABLE_CLASS(oc);

    ucc->complete = host_memory_backend_memory_complete;
}

static const TypeInfo host_memory_backend_info = {
    .name = TYPE_MEMORY_BACKEND,
    .parent = TYPE_OBJECT,
    .abstract = true,
    .class_size = sizeof(HostMemoryBackendClass),
    .class_init = host_memory_backend_class_init,
    .instance_size = sizeof(HostMemoryBackend),
    .instance_init = host_memory_backend_init,
    .instance_finalize = host_memory_backend_finalize,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};

static void register_types(void)
{
    type_register_static(&host_memory_backend_info);
}

type_init(register_types);
