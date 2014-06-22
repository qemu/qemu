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
#include "qapi/visitor.h"
#include "qapi-types.h"
#include "qapi-visit.h"
#include "qapi/qmp/qerror.h"
#include "qemu/config-file.h"
#include "qom/object_interfaces.h"

#ifdef CONFIG_NUMA
#include <numaif.h>
QEMU_BUILD_BUG_ON(HOST_MEM_POLICY_DEFAULT != MPOL_DEFAULT);
QEMU_BUILD_BUG_ON(HOST_MEM_POLICY_PREFERRED != MPOL_PREFERRED);
QEMU_BUILD_BUG_ON(HOST_MEM_POLICY_BIND != MPOL_BIND);
QEMU_BUILD_BUG_ON(HOST_MEM_POLICY_INTERLEAVE != MPOL_INTERLEAVE);
#endif

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

static void
host_memory_backend_get_host_nodes(Object *obj, Visitor *v, void *opaque,
                                   const char *name, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);
    uint16List *host_nodes = NULL;
    uint16List **node = &host_nodes;
    unsigned long value;

    value = find_first_bit(backend->host_nodes, MAX_NODES);
    if (value == MAX_NODES) {
        return;
    }

    *node = g_malloc0(sizeof(**node));
    (*node)->value = value;
    node = &(*node)->next;

    do {
        value = find_next_bit(backend->host_nodes, MAX_NODES, value + 1);
        if (value == MAX_NODES) {
            break;
        }

        *node = g_malloc0(sizeof(**node));
        (*node)->value = value;
        node = &(*node)->next;
    } while (true);

    visit_type_uint16List(v, &host_nodes, name, errp);
}

static void
host_memory_backend_set_host_nodes(Object *obj, Visitor *v, void *opaque,
                                   const char *name, Error **errp)
{
#ifdef CONFIG_NUMA
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);
    uint16List *l = NULL;

    visit_type_uint16List(v, &l, name, errp);

    while (l) {
        bitmap_set(backend->host_nodes, l->value, 1);
        l = l->next;
    }
#else
    error_setg(errp, "NUMA node binding are not supported by this QEMU");
#endif
}

static void
host_memory_backend_get_policy(Object *obj, Visitor *v, void *opaque,
                               const char *name, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);
    int policy = backend->policy;

    visit_type_enum(v, &policy, HostMemPolicy_lookup, NULL, name, errp);
}

static void
host_memory_backend_set_policy(Object *obj, Visitor *v, void *opaque,
                               const char *name, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);
    int policy;

    visit_type_enum(v, &policy, HostMemPolicy_lookup, NULL, name, errp);
    backend->policy = policy;

#ifndef CONFIG_NUMA
    if (policy != HOST_MEM_POLICY_DEFAULT) {
        error_setg(errp, "NUMA policies are not supported by this QEMU");
    }
#endif
}

static bool host_memory_backend_get_merge(Object *obj, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);

    return backend->merge;
}

static void host_memory_backend_set_merge(Object *obj, bool value, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);

    if (!memory_region_size(&backend->mr)) {
        backend->merge = value;
        return;
    }

    if (value != backend->merge) {
        void *ptr = memory_region_get_ram_ptr(&backend->mr);
        uint64_t sz = memory_region_size(&backend->mr);

        qemu_madvise(ptr, sz,
                     value ? QEMU_MADV_MERGEABLE : QEMU_MADV_UNMERGEABLE);
        backend->merge = value;
    }
}

static bool host_memory_backend_get_dump(Object *obj, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);

    return backend->dump;
}

static void host_memory_backend_set_dump(Object *obj, bool value, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);

    if (!memory_region_size(&backend->mr)) {
        backend->dump = value;
        return;
    }

    if (value != backend->dump) {
        void *ptr = memory_region_get_ram_ptr(&backend->mr);
        uint64_t sz = memory_region_size(&backend->mr);

        qemu_madvise(ptr, sz,
                     value ? QEMU_MADV_DODUMP : QEMU_MADV_DONTDUMP);
        backend->dump = value;
    }
}

static bool host_memory_backend_get_prealloc(Object *obj, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);

    return backend->prealloc || backend->force_prealloc;
}

static void host_memory_backend_set_prealloc(Object *obj, bool value,
                                             Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);

    if (backend->force_prealloc) {
        if (value) {
            error_setg(errp,
                       "remove -mem-prealloc to use the prealloc property");
            return;
        }
    }

    if (!memory_region_size(&backend->mr)) {
        backend->prealloc = value;
        return;
    }

    if (value && !backend->prealloc) {
        int fd = memory_region_get_fd(&backend->mr);
        void *ptr = memory_region_get_ram_ptr(&backend->mr);
        uint64_t sz = memory_region_size(&backend->mr);

        os_mem_prealloc(fd, ptr, sz);
        backend->prealloc = true;
    }
}

static void host_memory_backend_init(Object *obj)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);

    backend->merge = qemu_opt_get_bool(qemu_get_machine_opts(),
                                       "mem-merge", true);
    backend->dump = qemu_opt_get_bool(qemu_get_machine_opts(),
                                      "dump-guest-core", true);
    backend->prealloc = mem_prealloc;

    object_property_add_bool(obj, "merge",
                        host_memory_backend_get_merge,
                        host_memory_backend_set_merge, NULL);
    object_property_add_bool(obj, "dump",
                        host_memory_backend_get_dump,
                        host_memory_backend_set_dump, NULL);
    object_property_add_bool(obj, "prealloc",
                        host_memory_backend_get_prealloc,
                        host_memory_backend_set_prealloc, NULL);
    object_property_add(obj, "size", "int",
                        host_memory_backend_get_size,
                        host_memory_backend_set_size, NULL, NULL, NULL);
    object_property_add(obj, "host-nodes", "int",
                        host_memory_backend_get_host_nodes,
                        host_memory_backend_set_host_nodes, NULL, NULL, NULL);
    object_property_add(obj, "policy", "str",
                        host_memory_backend_get_policy,
                        host_memory_backend_set_policy, NULL, NULL, NULL);
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
    Error *local_err = NULL;
    void *ptr;
    uint64_t sz;

    if (bc->alloc) {
        bc->alloc(backend, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }

        ptr = memory_region_get_ram_ptr(&backend->mr);
        sz = memory_region_size(&backend->mr);

        if (backend->merge) {
            qemu_madvise(ptr, sz, QEMU_MADV_MERGEABLE);
        }
        if (!backend->dump) {
            qemu_madvise(ptr, sz, QEMU_MADV_DONTDUMP);
        }
#ifdef CONFIG_NUMA
        unsigned long lastbit = find_last_bit(backend->host_nodes, MAX_NODES);
        /* lastbit == MAX_NODES means maxnode = 0 */
        unsigned long maxnode = (lastbit + 1) % (MAX_NODES + 1);
        /* ensure policy won't be ignored in case memory is preallocated
         * before mbind(). note: MPOL_MF_STRICT is ignored on hugepages so
         * this doesn't catch hugepage case. */
        unsigned flags = MPOL_MF_STRICT;

        /* check for invalid host-nodes and policies and give more verbose
         * error messages than mbind(). */
        if (maxnode && backend->policy == MPOL_DEFAULT) {
            error_setg(errp, "host-nodes must be empty for policy default,"
                       " or you should explicitly specify a policy other"
                       " than default");
            return;
        } else if (maxnode == 0 && backend->policy != MPOL_DEFAULT) {
            error_setg(errp, "host-nodes must be set for policy %s",
                       HostMemPolicy_lookup[backend->policy]);
            return;
        }

        /* We can have up to MAX_NODES nodes, but we need to pass maxnode+1
         * as argument to mbind() due to an old Linux bug (feature?) which
         * cuts off the last specified node. This means backend->host_nodes
         * must have MAX_NODES+1 bits available.
         */
        assert(sizeof(backend->host_nodes) >=
               BITS_TO_LONGS(MAX_NODES + 1) * sizeof(unsigned long));
        assert(maxnode <= MAX_NODES);
        if (mbind(ptr, sz, backend->policy,
                  maxnode ? backend->host_nodes : NULL, maxnode + 1, flags)) {
            error_setg_errno(errp, errno,
                             "cannot bind memory to host NUMA nodes");
            return;
        }
#endif
        /* Preallocate memory after the NUMA policy has been instantiated.
         * This is necessary to guarantee memory is allocated with
         * specified NUMA policy in place.
         */
        if (backend->prealloc) {
            os_mem_prealloc(memory_region_get_fd(&backend->mr), ptr, sz);
        }
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
